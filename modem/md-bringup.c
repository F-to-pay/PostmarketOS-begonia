// SPDX-License-Identifier: GPL-2.0
/*
 * md-bringup: step-by-step power-up of the MT6785 modem (MD1) in mainline.
 * The sequence is taken from the vendor eccci/mt6785/md_sys1_platform.c (md_cd_power_on):
 *   PMIC (vmodem, vsram_md) -> INFRA_AO_MD_SRCCLKENA (infracfg_ao+0xF0C, low byte 0x21)
 *   -> MTCMOS MD1 (genpd "md") -> [image, MPU, boot vector - NOT in THIS module].
 *
 * stage: 0 = only a snapshot of the state (changes nothing)
 *        1 = +regulators vmodem, vsram_md
 *        2 = +MD_SRCCLKENA (low byte 0x21, if not yet)
 *        3 = +the MD power domain (pm_runtime_get_sync) and reading the boot stats
 *        4 = START: reads MD_BOOT_VECTOR_EN (0x20000024), writes 1 (md_cd_let_md_go)
 *            and watches boot_stats, CCIF0/1 and the head of the shared memory for 5 s.
 *            The image is already in RAM (LK puts it there). The handshake (HS1/HS2) is NOT done.
 * When the module is unloaded everything is rolled back in reverse order.
 * Stage 4 is the only one that starts the modem core: a SoC hang is possible.
 */
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#define INFRACFG_AO_PHYS	0x10001000UL
#define MD_SRCCLKENA_OFF	0xF0C
#define SEC_AO_PHYS		0x1001a000UL		/* BROM status: +0x824/828/82c/830 == 1 */
#define MD1_CFG_PAGE_PHYS	0x1020e000UL		/* boot stats: +0x300, select: +0x700 */
#define MD_BOOT_VECTOR_EN	0x20000024UL		/* an "MD-side" register, AP window, 1 = start */
#define AP_CCIF0_PHYS		0x10209000UL		/* CON,BUSY,START,TCHNUM,RCHNUM,ACK = +0,4,8,C,10,14 */
#define AP_CCIF1_PHYS		0x1020b000UL
#define SMEM_NC_PHYS		0x8e000000UL		/* ap_md_nc_smem */
#define SMEM_C_PHYS		0x8c000000UL		/* ap_md_c_smem */
#define INFRA_PWRON_OFF		0x22c			/* MD_PCCIF4_PWR_ON (ccci_set_clk_cg) */
#define INFRA_RST_SET		0x150			/* ccci_reset_ccif_hw: bit 8 = CCIF reset */
#define INFRA_RST_CLR		0x154
#define CCIF_SRAM		0x100			/* PCCIF_CHDATA */
#define CCIF_SRAM_SZ		512
#define MDSS_DBG_MD_VIEW	0x40000800UL		/* nc-smem md-view 0x40000000 + 2K */
#define MDSS_DBG_SIZE		0x2800
#define SMEM_NC_CLEAR_SZ	0x100000		/* clear the first MiB of nc-smem, except the NCLR regions */
#define SM_CCIF_OFF		(160 * 1024)
#define SM_CCIF_EXP_OFF		(881 * 1024)
static size_t md_ring_total = 721 * 1024;	/* the real size of the normal ring buffers after md_rings_init */

static int stage;
static int rf = 1;
module_param(rf, int, 0444);
MODULE_PARM_DESC(rf, "1 = enable the RF power (vrf12/vrf18/vrfck/vbbck/vpa/va09) before starting MD");

static const char *const rf_names[6] = { "vrf12", "vrf18", "vrfck", "vbbck", "vpa", "va09" };

static int ungate;
module_param(ungate, int, 0444);
MODULE_PARM_DESC(ungate, "1 = open all infra gates before the start (imitates clk_ignore_unused)");
module_param(stage, int, 0444);
MODULE_PARM_DESC(stage, "0=snapshot, 1=+vmodem/vsram_md, 2=+SRCCLKENA, 3=+MD domain and boot stats, 4=START MD, 5=start with the vendor CCIF/clock/smem preparation, 6=+answer to HS1 (HS2)");

static int ack_hs = 1;
module_param(ack_hs, int, 0444);
MODULE_PARM_DESC(ack_hs, "write the ACK on channel 15 after HS1");

static int watch_ms = 5000;
module_param(watch_ms, int, 0444);
MODULE_PARM_DESC(watch_ms, "how many ms to watch after the start (stage 4)");

struct md_bu {
	struct device *dev;
	struct device *mddev;	/* the virtual device of the "md" domain */
	struct regulator *vmodem, *vsram;
	struct regulator *rfreg[6];
	u32 rf_mask;
	void __iomem *infra;	/* infracfg_ao + 0xF0C */
	void __iomem *sec_ao;	/* the security_ao page */
	void __iomem *mdcfg;	/* the MD1_CFG page */
	void __iomem *bvec;	/* MD_BOOT_VECTOR_EN */
	void __iomem *ccif0, *ccif1;
	struct task_struct *thr;
	bool misc_on, rpc_on, sta_on;
	void *rb;		/* nc-smem, write-combining (for the ring buffers) */
	void *smem_nc, *smem_c;	/* head of the shared memory (observation only) */
	void __iomem *infra_all;	/* infracfg_ao 0x000-0x3ff */
	void __iomem *ccif0_md, *ccif1_md;	/* MD sides of CCIF (+0x1000) */
	struct clk_bulk_data *clks;
	int nclks;
	bool clk_on, pwron_set;
	u32 srcclkena_orig;
	bool reg_modem_on, reg_sram_on, srcclk_set, dom_on;
};

static void md_snapshot(struct md_bu *m, const char *tag)
{
	dev_info(m->dev, "[%s] SRCCLKENA=0x%08x BROM=%x,%x,%x,%x vmodem=%d(%d uV) vsram_md=%d(%d uV) dom=%s\n",
		 tag, readl(m->infra),
		 readl(m->sec_ao + 0x824), readl(m->sec_ao + 0x828),
		 readl(m->sec_ao + 0x82c), readl(m->sec_ao + 0x830),
		 regulator_is_enabled(m->vmodem), regulator_get_voltage(m->vmodem),
		 regulator_is_enabled(m->vsram), regulator_get_voltage(m->vsram),
		 (m->mddev && !pm_runtime_suspended(m->mddev)) ? "on" : "off");
}

static void md_boot_stats(struct md_bu *m)
{
	u32 s0, s1;

	/* like md_cd_dump_md_bootup_status: select, two dummy reads, the third is the value */
	writel(0, m->mdcfg + 0x700);
	readl(m->mdcfg + 0x300); readl(m->mdcfg + 0x300);
	s0 = readl(m->mdcfg + 0x300);
	writel(1, m->mdcfg + 0x700);
	readl(m->mdcfg + 0x300); readl(m->mdcfg + 0x300);
	s1 = readl(m->mdcfg + 0x300);
	dev_info(m->dev, "md_boot_stats0=0x%x md_boot_stats1=0x%x\n", s0, s1);
}

static void md_clear_range(struct md_bu *m, void *base, size_t off, size_t len)
{
	dma_addr_t d;

	memset(base + off, 0, len);
	/* flush the cache to the PoC, so that MD sees zeros (arch_sync_dma_for_device) */
	d = dma_map_single(m->dev, base + off, len, DMA_TO_DEVICE);
	if (dma_mapping_error(m->dev, d)) {
		dev_warn(m->dev, "dma_map_single(0x%zx+0x%zx) failed - cache not flushed\n", off, len);
		return;
	}
	dma_unmap_single(m->dev, d, len, DMA_TO_DEVICE);
}

/* ccci_create_ringbuf + md_ccif_ring_buf_init / md_ccif_exp_ring_buf_init (MD_GENERATION 6295, QUEUE_NUM=16) */
#define RBF_CTL_LEN		40		/* 8 + sizeof(struct ccci_ringbuf)=24 + 8 */
#define RBF_NQ			16
static const u8 rbf_rx[RBF_NQ]  = { 80, 80, 40, 80, 20, 20, 64, 0, 8, 0, 0, 0, 0, 0, 0, 0 };	/* KiB */
static const u8 rbf_tx[RBF_NQ]  = { 128, 40, 8, 40, 20, 20, 64, 0, 8, 0, 0, 0, 0, 0, 0, 0 };
static const u8 rbf_xrx[RBF_NQ] = { 12, 32, 8, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const u8 rbf_xtx[RBF_NQ] = { 12, 32, 8, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static size_t md_rings_write(u8 *buf, const u8 *rx, const u8 *tx)
{
	size_t total = 0;
	int q;

	for (q = 0; q < RBF_NQ; q++) {
		u32 rxs = rx[q] * 1024, txs = tx[q] * 1024;
		size_t len = RBF_CTL_LEN + rxs + txs;
		u32 *w = (u32 *)(buf + total);

		memset(buf + total, 0, len);
		w[0] = 0xEE0000EE;
		w[1] = 0xEE0000EE;
		w[2] = 0;		/* rx_control.read */
		w[3] = 0;		/* rx_control.write */
		w[4] = rxs;		/* rx_control.length */
		w[5] = 0;
		w[6] = 0;
		w[7] = txs;		/* tx_control.length */
		*(u32 *)(buf + total + len - 8) = 0xFF0000FF;
		*(u32 *)(buf + total + len - 4) = 0xFF0000FF;
		total += len;
	}
	return total;
}

static void md_flush_ptr(struct md_bu *m, void *p, size_t len)
{
	dma_addr_t d = dma_map_single(m->dev, p, len, DMA_TO_DEVICE);

	if (!dma_mapping_error(m->dev, d))
		dma_unmap_single(m->dev, d, len, DMA_TO_DEVICE);
}

/* the equivalent of ccci_md_hif_start(1)/ring_buf_init */
static void md_rings_init(struct md_bu *m, u8 *nc)
{
	size_t t1 = md_rings_write(nc + SM_CCIF_OFF, rbf_rx, rbf_tx);
	size_t t2 = md_rings_write(nc + SM_CCIF_EXP_OFF, rbf_xrx, rbf_xtx);

	md_flush_ptr(m, nc + SM_CCIF_OFF, t1);
	md_flush_ptr(m, nc + SM_CCIF_EXP_OFF, t2);
	dev_info(m->dev, "prep: CCIF ring buffers: normal %zu B, exp %zu B\n", t1, t2);
	md_ring_total = t1;
}

/* the equivalent of ccci_md_hif_start(md,1) + ccci_set_clk_cg(1) + ccci_md_clear_smem(first) */
static int md_prep(struct md_bu *m)
{
	int i, ret;
	void __iomem *a = m->ccif0, *b = m->ccif0_md;

	ret = clk_bulk_prepare_enable(m->nclks, m->clks);
	if (ret) {
		dev_err(m->dev, "CCIF clocks: %d\n", ret);
		return ret;
	}
	m->clk_on = true;
	dev_info(m->dev, "prep: %d CCIF/DPMAIF clocks enabled\n", m->nclks);
	/* state of ALL infra gates (bit=1 -> clock off) before any changes */
	dev_info(m->dev, "prep: INFRA CG sta: cg0=%08x cg1=%08x cg2=%08x cg3=%08x\n",
		 readl(m->infra_all + 0x90), readl(m->infra_all + 0x94),
		 readl(m->infra_all + 0xac), readl(m->infra_all + 0xc8));
	if (ungate) {
		/* the equivalent of clk_ignore_unused for infra: open ALL gates (the ones mainline switched off as unused) */
		writel(0xffffffff, m->infra_all + 0x84);
		writel(0xffffffff, m->infra_all + 0x8c);
		writel(0xffffffff, m->infra_all + 0xa8);
		writel(0xffffffff, m->infra_all + 0xc4);
		dev_info(m->dev, "prep: ungate=1, after: cg0=%08x cg1=%08x cg2=%08x cg3=%08x\n",
			 readl(m->infra_all + 0x90), readl(m->infra_all + 0x94),
			 readl(m->infra_all + 0xac), readl(m->infra_all + 0xc8));
	}

	dev_info(m->dev, "prep: infra+0x22C was 0x%08x -> writing 1 (MD_PCCIF4_PWR_ON)\n",
		 readl(m->infra_all + INFRA_PWRON_OFF));
	writel(1, m->infra_all + INFRA_PWRON_OFF);
	m->pwron_set = true;

	/* CCIF reset (busy/irq), it does not touch the SRAM */
	writel(BIT(8), m->infra_all + INFRA_RST_SET);
	writel(BIT(8), m->infra_all + INFRA_RST_CLR);

	for (i = 0; i < CCIF_SRAM_SZ; i += 4) {
		writel(0, a + CCIF_SRAM + i);
		writel(0, b + CCIF_SRAM + i);
	}
	/* the last 12 bytes of the AP SRAM: magic "rtbn", the address and size of MDSS_DBG in the MD view */
	writel(0x7274626E, a + CCIF_SRAM + CCIF_SRAM_SZ - 12);
	writel(MDSS_DBG_MD_VIEW, a + CCIF_SRAM + CCIF_SRAM_SZ - 8);
	writel(MDSS_DBG_SIZE, a + CCIF_SRAM + CCIF_SRAM_SZ - 4);
	dev_info(m->dev, "prep: CCIF reset, SRAM cleared, magic written\n");

	/* smem nc: clear the first MiB except CCB_CTRL (96-100K) and AUDIO (108-160K), they are NCLR_FIRST */
	{
		void *nc = memremap(SMEM_NC_PHYS, SMEM_NC_CLEAR_SZ, MEMREMAP_WB);

		if (!nc) {
			dev_err(m->dev, "memremap nc-smem\n");
			return -ENOMEM;
		}
		md_clear_range(m, nc, 0, 96 * 1024);
		md_clear_range(m, nc, 100 * 1024, 8 * 1024);
		md_clear_range(m, nc, 160 * 1024, SMEM_NC_CLEAR_SZ - 160 * 1024);
		md_rings_init(m, nc);
		memunmap(nc);
	}
	dev_info(m->dev, "prep: nc-smem cleared\n");
	return 0;
}

static void md_unprep(struct md_bu *m)
{
	if (m->pwron_set) {
		writel(0, m->infra_all + INFRA_PWRON_OFF);
		m->pwron_set = false;
	}
	if (m->clk_on) {
		clk_bulk_disable_unprepare(m->nclks, m->clks);
		m->clk_on = false;
		dev_info(m->dev, "rollback: CCIF clocks off, 0x22C=0\n");
	}
}

static void md_dump_sram(struct md_bu *m, const char *tag)
{
	int i;
	u32 w[16];

	for (i = 0; i < 16; i++)
		w[i] = readl(m->ccif0 + CCIF_SRAM + 4 * i);
	dev_info(m->dev, "[%s] CCIF0 SRAM(boot trace) %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		 tag, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7],
		 w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15]);
}

static void md_dump_sram_full(struct md_bu *m)
{
	int i, j;

	for (i = 0; i < CCIF_SRAM_SZ / 4; i += 8) {
		u32 w[8];

		for (j = 0; j < 8; j++)
			w[j] = readl(m->ccif0 + CCIF_SRAM + 4 * (i + j));
		dev_info(m->dev, "SRAM[%03x] %08x %08x %08x %08x %08x %08x %08x %08x\n", 4 * i,
			 w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
	}
}

static void md_dump_hif(struct md_bu *m, const char *tag)
{
	dev_info(m->dev, "[%s] CCIF0 con=%x busy=%x tch=%x rch=%x ack=%x | CCIF1 con=%x busy=%x tch=%x rch=%x ack=%x\n",
		 tag,
		 readl(m->ccif0 + 0x0), readl(m->ccif0 + 0x4), readl(m->ccif0 + 0xc),
		 readl(m->ccif0 + 0x10), readl(m->ccif0 + 0x14),
		 readl(m->ccif1 + 0x0), readl(m->ccif1 + 0x4), readl(m->ccif1 + 0xc),
		 readl(m->ccif1 + 0x10), readl(m->ccif1 + 0x14));
}

/* head of the shared memory: if MD writes something there - it is alive */
static u32 md_smem_sum(struct md_bu *m, u32 *nc0, u32 *c0)
{
	u32 *a = m->smem_nc, *b = m->smem_c, sum = 0;
	int i;

	for (i = 0; i < 0x1000 / 4; i++)
		sum += a[i] * (i + 1) + b[i] * (i + 3);
	*nc0 = a[0];
	*c0 = b[0];
	return sum;
}

/* ---------- HS2: the AP answer to HS1 (the equivalent of ccci_md_prepare_runtime_data + md_cd_send_runtime_data_v2) ---------- */

#define MD_INIT_CHK_ID		0x5555FFFFu	/* observed in HS1 from MD: reserved */
#define MD_VIEW_NC		0x40000000u	/* md view of the start of nc-smem (0x8E000000 aligned to 32 MiB) */
#define SM_EE			0x0		/* MDCCCI_DBG, 2K; EXCEPTION_SHARE_MEMORY = the first 64K */
#define SM_EE_SZ		0x10000
#define SM_RT			(58 * 1024)	/* RAW_RUNTIME_DATA, 4K: AP 0x800 + MD 0x800 */
#define SM_NETD			(100 * 1024)	/* 4K */
#define SM_USB			(104 * 1024)	/* 4K */
#define SM_AUDIO		(108 * 1024)	/* 52K */
#define SM_CCIF			(160 * 1024)	/* CCISM_MCU, 721K */
#define SM_CCIF_SZ		(721 * 1024)
#define SM_CCIF_EXP		(881 * 1024)	/* CCISM_MCU_EXP, 121K */
#define SM_CCIF_EXP_SZ		(121 * 1024)
#define RT_AP_SZ		0x800
#define RT_MD_SZ		0x800
#define FEAT_COUNT		64
#define FEAT_MUST		2
#define FEAT_NOTSUP		1
#define SRAM_UP_HDR		0xf0		/* dl_header 16 + md_query_ap_feature 224 */
#define SRAM_AP_RT		0x100
#define AP_RT_SIZE		220		/* sizeof(struct ap_query_md_feature), MD_GENERATION>=6293 */

static int mmpu = 1;
module_param(mmpu, int, 0444);
MODULE_PARM_DESC(mmpu, "1 = announce MULTI_MD_MPU and send ap_query_md_feature_v2_1");

static unsigned long long fmask = ~0ULL;
module_param(fmask, ullong, 0444);
MODULE_PARM_DESC(fmask, "bit mask of the features the AP announces as supported (hang bisection)");

static int quiet;
module_param(quiet, int, 0444);
MODULE_PARM_DESC(quiet, "ms: after HS2 only sleep, read nothing (hang diagnostics)");

static int hs2 = 1;
module_param(hs2, int, 0444);
MODULE_PARM_DESC(hs2, "after HS1 send the answer (runtime data) - stage 6");

/* flush/invalidate the AP cache for a piece of nc-smem through the DMA API (arch_sync_dma_*) */
static void md_smem_flush(struct md_bu *m, size_t off, size_t len)
{
	dma_addr_t d = dma_map_single(m->dev, (u8 *)m->smem_nc + off, len, DMA_TO_DEVICE);

	if (!dma_mapping_error(m->dev, d))
		dma_unmap_single(m->dev, d, len, DMA_TO_DEVICE);
}

static void md_smem_inval(struct md_bu *m, size_t off, size_t len)
{
	dma_addr_t d = dma_map_single(m->dev, (u8 *)m->smem_nc + off, len, DMA_FROM_DEVICE);

	if (!dma_mapping_error(m->dev, d))
		dma_unmap_single(m->dev, d, len, DMA_FROM_DEVICE);
}

/* AP side: what the AP supports and which data it puts. Returns: 1 = MUST (data in d/len), 0 = NOT_SUPPORT */
static int md_ap_feature(struct md_bu *m, int id, u32 *d, u32 *len)
{
	u32 seed;

	memset(d, 0, 16);
	*len = 0;
	if (id > 0 && id < 64 && !((fmask >> id) & 1))	/* bisection: the feature is disabled by the parameter */
		return 0;
	switch (id) {
	case 0:		/* BOOT_INFO {boot_channel, booting_start_id, boot_attributes, boot_ready_id} */
		d[0] = 0;	/* CCCI_CONTROL_RX */
		d[1] = 0;	/* NORMAL_BOOT_ID, mdlog idle */
		*len = 16;
		return 1;
	case 1:		/* EXCEPTION_SHARE_MEMORY */
		d[0] = MD_VIEW_NC + SM_EE; d[1] = SM_EE_SZ; *len = 8; return 1;
	case 2:		/* CCIF_SHARE_MEMORY */
		d[0] = MD_VIEW_NC + SM_CCIF; d[1] = md_ring_total; *len = 8; return 1;
	case 5:		/* MISC_INFO_HIF_DMA_REMAP */
	case 8:		/* MISC_INFO_GPS_COCLOCK */
	case 12:	/* MISC_INFO_C2K (no C2K) */
	case 19:	/* EE_AFTER_EPOF */
		*len = 16; return 1;
	case 7:		/* MISC_INFO_RANDOM_SEED_NUM */
		get_random_bytes(&seed, sizeof(seed));
		d[0] = seed; *len = 16; return 1;
	case 9:		/* MISC_INFO_SBP_ID: sbp_code=0, wm bitmap=0 */
		*len = 16; return 1;
	case 10:	/* MISC_INFO_CCCI: bit0 seq check, bit1 MD status polling */
		d[0] = 3; *len = 16; return 1;
	case 11: {	/* MISC_INFO_CLIB_TIME: sec lo/hi, tz, dst */
		time64_t t = ktime_get_real_seconds();

		d[0] = lower_32_bits(t); d[1] = upper_32_bits(t); *len = 16; return 1;
	}
	case 17:	/* DT_NETD_SHARE_MEMORY */
		d[0] = MD_VIEW_NC + SM_NETD; d[1] = 4 * 1024; *len = 8; return 1;
	case 18:	/* DT_USB_SHARE_MEMORY */
		d[0] = MD_VIEW_NC + SM_USB; d[1] = 4 * 1024; *len = 8; return 1;
	case 26:	/* AUDIO_RAW_SHARE_MEMORY */
		d[0] = MD_VIEW_NC + SM_AUDIO; d[1] = 52 * 1024; *len = 8; return 1;
	case 27:	/* MULTI_MD_MPU: no data, switches the SRAM format to v2.1 */
		return mmpu ? 1 : 0;
	case 28:	/* CCISM_SHARE_MEMORY_EXP */
		d[0] = MD_VIEW_NC + SM_CCIF_EXP; d[1] = SM_CCIF_EXP_SZ; *len = 8; return 1;
	default:
		return 0;
	}
}

static u32 rt_put(u8 *base, u32 pos, int id, int mask, int ver, const u32 *data, u32 len)
{
	u8 *p = base + pos;

	p[0] = id;
	p[1] = (mask & 0xf) | (ver << 4);
	p[2] = p[3] = 0;
	memcpy(p + 4, &len, 4);
	if (len)
		memcpy(p + 8, data, len);
	return pos + 8 + len;
}

static int md_send_hs2(struct md_bu *m)
{
	void __iomem *sr = m->ccif0 + CCIF_SRAM;
	u8 *rt = (u8 *)m->smem_nc + SM_RT;
	u32 head = readl(sr + 0x10), tail = readl(sr + 0x14 + FEAT_COUNT), d[4], len, pos = 0;
	int i, nmust = 0, nopt_sup = 0, nopt_no = 0;

	if (head != 0x49434343 || tail != 0x49434343) {
		dev_err(m->dev, "HS2: MD patterns are wrong head=%x tail=%x\n", head, tail);
		return -EINVAL;
	}
	md_smem_inval(m, SM_RT, 0x1000);
	memset(rt, 0, RT_AP_SZ + RT_MD_SZ);

	for (i = 0; i < FEAT_COUNT; i++) {
		u8 b = readb(sr + 0x14 + i);
		int mdm = b & 0xf, mdv = b >> 4, ok = md_ap_feature(m, i, d, &len);

		if (mdm == 0) {					/* NOT_EXIST */
			pos = rt_put(rt, pos, i, 0, mdv, NULL, 0);
		} else if (mdm == FEAT_MUST) {
			if (!ok) {
				dev_err(m->dev, "HS2: feature %d is MUST for MD, the AP cannot do it\n", i);
				return -ENOTSUPP;
			}
			pos = rt_put(rt, pos, i, FEAT_MUST, mdv, d, len);
			nmust++;
		} else if (mdm == 3 && ok && mdv == 0) {		/* OPTIONAL, the versions match */
			pos = rt_put(rt, pos, i, FEAT_MUST, 0, d, len);
			nopt_sup++;
		} else {
			pos = rt_put(rt, pos, i, FEAT_NOTSUP, 0, NULL, 0);
			nopt_no++;
		}
	}
	if (pos > RT_AP_SZ) {
		dev_err(m->dev, "HS2: runtime data %u > %u\n", pos, RT_AP_SZ);
		return -E2BIG;
	}
	dev_info(m->dev, "HS2: runtime data %u B: MUST=%d optional+=%d optional-=%d\n", pos, nmust, nopt_sup, nopt_no);
	md_smem_flush(m, SM_RT, 0x1000);

	/* SRAM: up_header + ap_query_md_feature (v2) / ap_query_md_feature_v2_1 */
	{
		u32 base = SRAM_AP_RT + 4 + FEAT_COUNT;	/* the share_memory_support field */
		u32 sz = mmpu ? 156 : AP_RT_SIZE;

		for (i = 0; i < AP_RT_SIZE; i += 4)
			writel(0, sr + SRAM_AP_RT + i);
		writel(0, sr + SRAM_UP_HDR + 0);
		writel(16 + sz, sr + SRAM_UP_HDR + 4);		/* packet_size */
		writel(1, sr + SRAM_UP_HDR + 8);		/* CCCI_CONTROL_TX */
		writel(MD_INIT_CHK_ID, sr + SRAM_UP_HDR + 12);
		writel(0x43434349, sr + SRAM_AP_RT);		/* AP_FEATURE_QUERY_PATTERN */
		writeb(1 << 4, sr + SRAM_AP_RT + 4 + 1);	/* feature_set[1]: mask=0, version=1 */
		writel(mmpu ? 2 : 1, sr + base);		/* share_memory_support */
		writel(MD_VIEW_NC + SM_RT, sr + base + 4);	/* ap_runtime_data_addr */
		writel(RT_AP_SZ, sr + base + 8);
		writel(MD_VIEW_NC + SM_RT + RT_AP_SZ, sr + base + 12);	/* md_runtime_data_addr */
		writel(RT_MD_SZ, sr + base + 16);
		writel(MD_VIEW_NC, sr + base + 20);		/* (non)cached_mpu_start_addr */
		writel(0x1770000, sr + base + 24);		/* noncached total size */
		if (mmpu) {
			writel(0x4c000000, sr + base + 28);	/* cached_mpu_start_addr: 0x40000000 + 0xC000000 (LK tag) */
			writel(0x1780000, sr + base + 32);	/* cached_mpu_total_size */
			writel(0x43434349, sr + base + 84);	/* tail: offset 152 from the start of the structure */
		} else {
			writel(0x43434349, sr + base + 28);	/* tail */
		}
	}
	wmb();

	/* md_ccif_send(H2D_SRAM=15) */
	if (readl(m->ccif0 + 0x4) & BIT(15)) {
		dev_err(m->dev, "HS2: channel 15 busy\n");
		return -EBUSY;
	}
	writel(BIT(15), m->ccif0 + 0x4);
	writel(15, m->ccif0 + 0xc);
	dev_info(m->dev, "HS2: sent (TCHNUM=15), busy=%x\n", readl(m->ccif0 + 0x4));
	return 0;
}

/* print the readable strings from a piece of nc-smem: MD puts the exception text here (file/line/assert) */
static void md_dump_ascii(struct md_bu *m, const char *tag, size_t off, size_t len)
{
	const u8 *p = (u8 *)m->smem_nc + off;
	char buf[96];
	size_t i = 0, k;

	md_smem_inval(m, off, len);
	while (i < len) {
		k = 0;
		while (i + k < len && k < sizeof(buf) - 1 && p[i + k] >= 0x20 && p[i + k] < 0x7f)
			k++;
		if (k >= 5) {
			memcpy(buf, p + i, k);
			buf[k] = 0;
			dev_info(m->dev, "%s+0x%zx: \"%s\"\n", tag, off + i, buf);
			i += k;
		} else {
			i += k ? k : 1;
		}
	}
}

static void md_dump_words(struct md_bu *m, const char *tag, size_t off, int nwords)
{
	u32 *p = (u32 *)((u8 *)m->smem_nc + off);
	int i;

	md_smem_inval(m, off, nwords * 4);
	for (i = 0; i < nwords; i += 8)
		dev_info(m->dev, "%s[%04zx]: %08x %08x %08x %08x %08x %08x %08x %08x\n", tag, off + i * 4,
			 p[i], p[i + 1], p[i + 2], p[i + 3], p[i + 4], p[i + 5], p[i + 6], p[i + 7]);
}

/* a new RCHNUM value after HS1: show and acknowledge it (like md_ccif_isr + exception ACK) */
static void md_event(struct md_bu *m, u32 rch0, u32 rch1, s64 t_ms, bool *ex_acked)
{
	void __iomem *sr = m->ccif0 + CCIF_SRAM;

	dev_info(m->dev, "EVENT t=%lld: rch0=0x%x rch1=0x%x busy0=0x%x sram_hdr=%08x %08x %08x %08x\n", t_ms, rch0, rch1,
		 readl(m->ccif0 + 0x4), readl(sr), readl(sr + 4), readl(sr + 8), readl(sr + 12));
	{	/* look into the ring queues MD sent data through (D2H_RINGQ0+q = bit q) */
		size_t base = SM_CCIF_OFF;
		int q;

		for (q = 0; q < RBF_NQ; q++) {
			size_t len = RBF_CTL_LEN + (rbf_rx[q] + rbf_tx[q]) * 1024;

			if (rch0 & BIT(q)) {
				u32 *rc, *dt;

				md_smem_inval(m, base, len < 4096 ? len : 4096);
				rc = (u32 *)((u8 *)m->smem_nc + base + 8);
				dt = (u32 *)((u8 *)m->smem_nc + base + 32);
				dev_info(m->dev, "RING q%d: rx rd=%u wr=%u len=%u | tx rd=%u wr=%u len=%u\n", q,
					 rc[0], rc[1], rc[2], rc[3], rc[4], rc[5]);
				dev_info(m->dev, "RING q%d data: %08x %08x %08x %08x %08x %08x %08x %08x\n", q,
					 dt[0], dt[1], dt[2], dt[3], dt[4], dt[5], dt[6], dt[7]);
			}
			base += len;
		}
	}
	if (rch0 & BIT(16) && !*ex_acked) {		/* D2H_EXCEPTION_INIT: answer H2D_EXCEPTION_ACK (ch 16) */
		*ex_acked = true;
		dev_warn(m->dev, "MD RAISED AN EXCEPTION (ch16), sending H2D_EXCEPTION_ACK\n");
		writel(BIT(16), m->ccif0 + 0x4);
		writel(16, m->ccif0 + 0xc);
	}
	if (rch0 & 0xffff)
		writel(rch0 & 0xffff, m->ccif0 + 0x14);
	if (rch1 & 0xffff)
		writel(rch1 & 0xffff, m->ccif1 + 0x14);
}

/* ---------- ccci-lite: reading/writing CCIF ring packets and an RPC responder ---------- */

#define PKT_HDR			0xAABBAABBu
#define PKT_FTR			0xCCDDEEFFu
#define CH_CONTROL_RX		0
#define CH_RPC_RX		32
#define CH_RPC_TX		33
#define FS_NO_OP		(-1)
#define FS_FUNC_FAIL		(-5)
#define SEQ_MAX			128

static u16 tx_seq[SEQ_MAX];
static bool hs2_seen;

static u8 *rb_blk(struct md_bu *m, int q)
{
	size_t off = SM_CCIF_OFF;
	int j;

	for (j = 0; j < q; j++)
		off += RBF_CTL_LEN + (rbf_rx[j] + rbf_tx[j]) * 1024;
	return (u8 *)m->rb + off;
}

static void rb_in(const u8 *buf, u32 buflen, u32 pos, u8 *dst, u32 n)
{
	u32 first = min(n, buflen - pos);

	memcpy(dst, buf + pos, first);
	if (n > first)
		memcpy(dst + first, buf, n - first);
}

static void rb_out(u8 *buf, u32 buflen, u32 pos, const u8 *src, u32 n)
{
	u32 first = min(n, buflen - pos);

	memcpy(buf + pos, src, first);
	if (n > first)
		memcpy(buf, src + first, n - first);
}

/* read one ccci packet from the rx ring of queue q; >0 length, 0 empty, <0 error */
static int rb_read(struct md_bu *m, int q, u8 *out, int max)
{
	u8 *blk = rb_blk(m, q);
	u32 *c = (u32 *)(blk + 8);
	u32 rd = READ_ONCE(c[0]), wr = READ_ONCE(c[1]), len = c[2];
	u8 *buf = blk + 32;
	u32 hdr[2], ft[2], plen, tot;
	int size = (int)wr - (int)rd;

	if (!len || rd >= len || wr >= len)
		return 0;
	if (size < 0)
		size += len;
	if (size < 8 + 8 + 16)
		return 0;
	rb_in(buf, len, rd, (u8 *)hdr, 8);
	if (hdr[0] != PKT_HDR) {
		dev_err(m->dev, "q%d: bad ring packet header 0x%x (rd=%u wr=%u)\n", q, hdr[0], rd, wr);
		return -EIO;
	}
	plen = hdr[1];
	tot = (8 + plen + 8 + 7) & ~7u;
	if (tot > (u32)size)
		return 0;		/* the packet is not fully written yet */
	if (plen > (u32)max) {
		dev_err(m->dev, "q%d: packet of %u B is larger than the buffer, skipping\n", q, plen);
		WRITE_ONCE(c[0], (rd + tot) % len);
		return -E2BIG;
	}
	rb_in(buf, len, (rd + 8) % len, out, plen);
	rb_in(buf, len, (rd + tot - 8) % len, (u8 *)ft, 8);
	if (ft[0] != PKT_FTR || ft[1] != PKT_FTR)
		dev_err(m->dev, "q%d: bad tail 0x%x 0x%x\n", q, ft[0], ft[1]);
	wmb();
	WRITE_ONCE(c[0], (rd + tot) % len);
	wmb();
	return plen;
}

static DEFINE_SPINLOCK(rb_tx_lock);

/* put a ccci packet into the tx ring of queue q and kick MD through CCIF */
static int rb_write_locked(struct md_bu *m, int q, const u8 *data, u32 plen)
{
	u8 *blk = rb_blk(m, q);
	u32 *c = (u32 *)(blk + 8);
	u32 rxlen = c[2], rd = READ_ONCE(c[3]), wr = READ_ONCE(c[4]), len = c[5];
	u8 *buf = blk + 32 + rxlen;
	u32 hdr[2] = { PKT_HDR, plen }, ft[2] = { PKT_FTR, PKT_FTR };
	u32 need = (8 + plen + 8 + 7) & ~7u, room, pos;

	if (!len || wr >= len)
		return -EINVAL;
	if (rd == wr)
		room = len - 1;
	else if (rd < wr)
		room = len - wr - 1 + rd;
	else
		room = rd - wr - 1;
	if (need > room)
		return -ENOSPC;
	rb_out(buf, len, wr, (u8 *)hdr, 8);
	rb_out(buf, len, (wr + 8) % len, data, plen);
	pos = (wr + 8 + ((plen + 7) & ~7u)) % len;
	rb_out(buf, len, pos, (u8 *)ft, 8);
	wmb();
	WRITE_ONCE(c[4], (wr + need) % len);
	wmb();
	if (readl(m->ccif0 + 0x4) & BIT(q))
		dev_warn(m->dev, "CCIF channel %d busy\n", q);
	writel(BIT(q), m->ccif0 + 0x4);
	writel(q, m->ccif0 + 0xc);
	return 0;
}

static int rb_write(struct md_bu *m, int q, const u8 *data, u32 plen)
{
	unsigned long fl;
	int ret;

	spin_lock_irqsave(&rb_tx_lock, fl);
	ret = rb_write_locked(m, q, data, plen);
	spin_unlock_irqrestore(&rb_tx_lock, fl);
	return ret;
}

static void md_log_pkt(struct md_bu *m, const char *dir, int q, const u8 *p, int len)
{
	const u32 *h = (const u32 *)p;
	int n = len > 16 ? min(len - 16, 64) : 0;

	dev_info(m->dev, "%s q%d ch=%u seq=%u as=%u len=%d data0=%08x data1=%08x rsv=%08x | %*ph\n", dir, q,
		 h[2] & 0xffff, (h[2] >> 16) & 0x7fff, h[2] >> 31, len, h[0], h[1], h[3], n, p + 16);
}

/* Data from the native dtbo (Xiaomi begonia): gpio_usage_mapping, the second cell is the GPIO number in the MD view */
static const struct { const char *name; s32 id; } md_gpio_tab[] = {
	{ "GPIO_SIM1_SIO", 0x80 },  { "GPIO_SIM1_SRST", 0x81 }, { "GPIO_SIM1_SCLK", 0x82 },
	{ "GPIO_SIM2_SCLK", 0x83 }, { "GPIO_SIM2_SRST", 0x84 }, { "GPIO_SIM2_SIO", 0x85 },
	{ "GPIO_SIM1_HOT_PLUG", 0x8c }, { "GPIO_SIM2_HOT_PLUG", 0x8d },
};

/* md1_simN_hot_plug_eint: by type {NUM, DEBOUNCE(ms), POLA, SENS, SOCKET, DEDICATED, SRC_PIN}.
 * interrupts=<n 8(LEVEL_LOW)> -> pola=0 sens=0; debounce 100000 us -> 100 ms */
static const s32 md_eint_tab[2][7] = {
	{ 0, 100, 0, 0, 0, 0, 1 },
	{ 1, 100, 0, 0, 0, 0, 2 },
};

/* RPC: packet = ccci_header(16) + op_id + para_num + {len, data(4-align)}... The answer is sent to CH_RPC_TX */
static void md_rpc(struct md_bu *m, int q, const u8 *p, int len)
{
	const u32 *h = (const u32 *)p;
	u32 op = h[4], npar = h[5];
	const u8 *par = p + 24;
	u8 reply[128];
	u32 *r = (u32 *)reply;
	u32 i, off = 0;
	int ret;

	for (i = 0; i < npar && i < 6 && (int)(off + 4) <= len - 24; i++) {
		u32 pl = *(const u32 *)(par + off);

		if ((int)(off + 4 + pl) > len - 24)
			break;
		if (pl && pl < 64 && par[off + 4] >= 0x20 && par[off + 4] < 0x7f)
			dev_info(m->dev, "RPC op=0x%x par%u len=%u str=\"%.*s\"\n", op, i, pl, (int)pl, par + off + 4);
		else
			dev_info(m->dev, "RPC op=0x%x par%u len=%u %*ph\n", op, i, pl, (int)min(pl, 32u), par + off + 4);
		off += 4 + ((pl + 3) & ~3u);
	}

	/* answer: two parameters of 4 bytes {a, b}; values like in the vendor ccci_rpc_work_helper */
	{
		s32 a = FS_NO_OP, b = FS_NO_OP;
		u32 l2 = 4;
		const char *pname = (len >= 32 && par[4]) ? (const char *)par + 4 : "";
		u32 plen0 = len >= 28 ? *(const u32 *)par : 0;

		if (op == 0x4002 && plen0) {		/* GET_GPIO_NUM_OP: the GPIO number in the MD view by name */
			a = FS_FUNC_FAIL;
			for (i = 0; i < ARRAY_SIZE(md_gpio_tab); i++)
				if (!strncmp(pname, md_gpio_tab[i].name, plen0))
					a = md_gpio_tab[i].id;
			b = a;
		} else if (op == 0x400e) {		/* DTSI_QUERY: no md_attr_node node -> {0, 68 bytes of 0x0f} like the vendor */
			a = 0;
			l2 = 68;
		} else if (op == 0x4005 && plen0 && npar >= 3) {	/* GET_EINT_ATTR_OP: {code, value} */
			u32 poff = 4 + ((plen0 + 3) & ~3u), toff = poff + 4 + 4 + 4;
			u32 type = (int)(toff + 4) <= len - 24 ? *(const u32 *)(par + toff) : 99;
			int sim = (plen0 > 8 && !strncmp(pname, "MD1_SIM", 7)) ? pname[7] - '0' : 0;

			if (type >= 7) {
				a = b = -12;			/* ERR_SIM_HOT_PLUG_QUERY_TYPE */
			} else if (sim < 1 || sim > 2) {
				a = b = -11;			/* ERR_SIM_HOT_PLUG_QUERY_STRING: the node is not in DT */
			} else {
				a = 0;
				b = md_eint_tab[sim - 1][type];
			}
		}
		memset(reply, 0, sizeof(reply));
		r[0] = h[0];
		r[2] = CH_RPC_TX | ((u32)(tx_seq[CH_RPC_TX]++ & 0x7fff) << 16) | (hs2_seen ? 0 : BIT(31));
		r[3] = h[3];				/* reserved = request index */
		r[4] = op | 0xffff0000;
		r[5] = 2;
		r[6] = 4;
		r[7] = (u32)a;
		r[8] = l2;
		if (l2 == 4)
			r[9] = (u32)b;
		else
			memset(&r[9], 0x0f, l2);
		r[1] = 36 + l2;				/* data1 = the full length including the ccci_header */
		ret = rb_write(m, 1, reply, 36 + l2);
		dev_info(m->dev, "RPC op=0x%x -> {%d, %d}, rb_write=%d\n", op, a, b, ret);
	}
}

/* ---------- userspace ports (PORT_F_USER_HEADER): /dev/ccci_fs (ch14->15, q4) and /dev/ccci_rpc (ch32->33, q1) ----------
 * The daemon reads and writes FULL ccci packets (ccci_header 16 B + data); the module only moves them into the ring.
 */
#define CH_FS_RX		14
#define CH_FS_TX		15
#define FS_Q			4
#define RPC_Q			1
#define CCCI_MTU		(3584 - 128)

struct fs_item {
	struct list_head l;
	int len;
	u8 data[];
};

struct uport {
	const char *name;
	int txq, txch;
	struct list_head list;
	spinlock_t lock;
	wait_queue_head_t wq;
	struct miscdevice misc;
	unsigned long rx_cnt, tx_cnt;
};

static struct md_bu *fs_md;
#define to_up(f) container_of((f)->private_data, struct uport, misc)

static void uport_enqueue(struct uport *u, const u8 *p, int len)
{
	struct fs_item *it = kmalloc(sizeof(*it) + len, GFP_KERNEL);

	if (!it)
		return;
	it->len = len;
	memcpy(it->data, p, len);
	spin_lock(&u->lock);
	list_add_tail(&it->l, &u->list);
	u->rx_cnt++;
	spin_unlock(&u->lock);
	wake_up_interruptible(&u->wq);
}

static ssize_t uport_read(struct file *f, char __user *ubuf, size_t count, loff_t *pos)
{
	struct uport *u = to_up(f);
	struct fs_item *it;
	size_t n;

	if (list_empty(&u->list)) {
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(u->wq, !list_empty(&u->list)))
			return -ERESTARTSYS;
	}
	spin_lock(&u->lock);
	it = list_first_entry_or_null(&u->list, struct fs_item, l);
	if (it)
		list_del(&it->l);
	spin_unlock(&u->lock);
	if (!it)
		return -EAGAIN;
	n = min_t(size_t, count, it->len);
	if (copy_to_user(ubuf, it->data, n))
		n = -EFAULT;
	kfree(it);
	return n;
}

static ssize_t uport_write(struct file *f, const char __user *ubuf, size_t count, loff_t *pos)
{
	struct uport *u = to_up(f);
	u8 *b;
	u32 *h;
	int ret;

	if (count < 16 || count > CCCI_MTU + 20 || !fs_md)
		return -EINVAL;
	b = kmalloc(count, GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	if (copy_from_user(b, ubuf, count)) {
		kfree(b);
		return -EFAULT;
	}
	h = (u32 *)b;
	if (count == 16)
		h[0] = 0xFFFFFFFFu;		/* CCCI_MAGIC_NUM: header only */
	else
		h[1] = count;
	h[2] = u->txch | ((u32)(tx_seq[u->txch]++ & 0x7fff) << 16) | (hs2_seen ? 0 : BIT(31));
	ret = rb_write(fs_md, u->txq, b, count);
	u->tx_cnt++;
	if (u->txch != CH_FS_TX)
		md_log_pkt(fs_md, "TX", u->txq, b, count);
	kfree(b);
	return ret ? ret : count;
}

static __poll_t uport_poll(struct file *f, poll_table *wait)
{
	struct uport *u = to_up(f);

	poll_wait(f, &u->wq, wait);
	return (list_empty(&u->list) ? 0 : EPOLLIN | EPOLLRDNORM) | EPOLLOUT | EPOLLWRNORM;
}

static const struct file_operations uport_fops = {
	.owner = THIS_MODULE,
	.read = uport_read,
	.write = uport_write,
	.poll = uport_poll,
	.llseek = noop_llseek,
};

static struct uport up_fs = { .name = "ccci_fs", .txq = FS_Q, .txch = CH_FS_TX };
static struct uport up_rpc = { .name = "ccci_rpc", .txq = RPC_Q, .txch = CH_RPC_TX };

static int uport_register(struct uport *u)
{
	INIT_LIST_HEAD(&u->list);
	spin_lock_init(&u->lock);
	init_waitqueue_head(&u->wq);
	u->misc.minor = MISC_DYNAMIC_MINOR;
	u->misc.name = u->name;
	u->misc.fops = &uport_fops;
	u->misc.mode = 0666;
	return misc_register(&u->misc);
}

/* ---------- stubs of the vendor ccci interfaces for ccci_fsd ---------- */
static struct kobject *ccci_kobj;
static bool sta_stop;
static DECLARE_WAIT_QUEUE_HEAD(sta_wq);

static ssize_t ccci_ver_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "6\n");		/* ccci_port_ver = ECCCI_FSM */
}
static struct kobj_attribute ccci_ver_attr = __ATTR(version, 0444, ccci_ver_show, NULL);

/* /dev/ccci_md1_sta: broadcast of MD state events for a 32-bit client (48 B: compat_timeval 8, md_id, type, reason[32]) */
struct sta_ev {
	s32 sec, usec, md_id, type;
	char reason[32];
};
#define STA_Q 8
static struct sta_ev sta_q[STA_Q], sta_last;
static int sta_r, sta_w, sta_cnt;
static bool sta_has_last;
static DEFINE_SPINLOCK(sta_lock);

/* 9 = MD_STA_EV_HS1 (state BOOT_WAITING_FOR_HS2), 10 = READY, 11 = EXCEPTION, 12 = STOP */
static void sta_inject(int type)
{
	struct sta_ev e = { .md_id = 0, .type = type };
	struct timespec64 ts;
	unsigned long fl;

	ktime_get_real_ts64(&ts);
	e.sec = (s32)ts.tv_sec;
	e.usec = ts.tv_nsec / 1000;
	strscpy(e.reason, "----", sizeof(e.reason));
	spin_lock_irqsave(&sta_lock, fl);
	if (type >= 9) {
		sta_last = e;
		sta_has_last = true;
	}
	if (sta_cnt == STA_Q) {
		sta_r = (sta_r + 1) % STA_Q;
		sta_cnt--;
	}
	sta_q[sta_w] = e;
	sta_w = (sta_w + 1) % STA_Q;
	sta_cnt++;
	spin_unlock_irqrestore(&sta_lock, fl);
	wake_up_interruptible(&sta_wq);
}

static int sta_open(struct inode *i, struct file *f)
{
	unsigned long fl;

	spin_lock_irqsave(&sta_lock, fl);
	if (sta_has_last && !sta_cnt) {		/* like send_last_md_status_to_user */
		sta_q[sta_w] = sta_last;
		sta_w = (sta_w + 1) % STA_Q;
		sta_cnt++;
	}
	spin_unlock_irqrestore(&sta_lock, fl);
	return nonseekable_open(i, f);
}

static ssize_t sta_read(struct file *f, char __user *ubuf, size_t count, loff_t *pos)
{
	struct sta_ev e;
	unsigned long fl;

	for (;;) {
		spin_lock_irqsave(&sta_lock, fl);
		if (sta_cnt) {
			e = sta_q[sta_r];
			sta_r = (sta_r + 1) % STA_Q;
			sta_cnt--;
			spin_unlock_irqrestore(&sta_lock, fl);
			if (count < sizeof(e))
				return -EINVAL;
			return copy_to_user(ubuf, &e, sizeof(e)) ? -EFAULT : sizeof(e);
		}
		spin_unlock_irqrestore(&sta_lock, fl);
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(sta_wq, sta_cnt || sta_stop))
			return -ERESTARTSYS;
		if (sta_stop)
			return 0;
	}
}
static ssize_t sta_write(struct file *f, const char __user *ubuf, size_t count, loff_t *pos)
{
	return count;
}
static long sta_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	return 0;
}
static __poll_t sta_poll(struct file *f, poll_table *wait)
{
	poll_wait(f, &sta_wq, wait);
	return sta_stop ? EPOLLHUP : (sta_cnt ? EPOLLIN | EPOLLRDNORM : 0);
}
static const struct file_operations sta_fops = {
	.owner = THIS_MODULE,
	.open = sta_open,
	.read = sta_read,
	.write = sta_write,
	.unlocked_ioctl = sta_ioctl,
	.compat_ioctl = sta_ioctl,
	.poll = sta_poll,
	.llseek = noop_llseek,
};
static struct miscdevice sta_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ccci_md1_sta",
	.fops = &sta_fops,
	.mode = 0666,
};

static void md_handle_pkt(struct md_bu *m, int q, const u8 *p, int len)
{
	const u32 *h = (const u32 *)p;
	u32 ch = h[2] & 0xffff;

	md_log_pkt(m, "RX", q, p, len);
	if (ch == CH_RPC_RX && len >= 24) {
		u32 rop = ((const u32 *)p)[4];

		if (m->rpc_on && (rop == 0x400f || rop == 0x4010))
			uport_enqueue(&up_rpc, p, len);		/* Android properties -> ccci_rpcd */
		else
			md_rpc(m, q, p, len);
	} else if (ch == CH_FS_RX) {
		uport_enqueue(&up_fs, p, len);
	} else if (ch == CH_CONTROL_RX) {
		if (h[3] != MD_INIT_CHK_ID && !hs2_seen) {
			hs2_seen = true;
			sta_inject(10);			/* MD_STA_EV_READY */
		}
		dev_info(m->dev, "CONTROL from MD: msg=0x%x reserved=0x%x hs2_seen=%d\n", h[1], h[3], hs2_seen);
	}
}

/* serve all queues: acknowledge the CCIF interrupts and read out the packets */
static void md_service(struct md_bu *m, u32 rch0)
{
	static u8 pkt[8192];
	int q, n, r;

	if (rch0 & 0xffff)
		writel(rch0 & 0xffff, m->ccif0 + 0x14);
	for (q = 0; q < RBF_NQ; q++) {
		if (!rbf_rx[q])
			continue;
		for (n = 0; n < 16; n++) {
			r = rb_read(m, q, pkt, sizeof(pkt));
			if (r <= 0)
				break;
			md_handle_pkt(m, q, pkt, r);
		}
	}
}

static void md_go_and_watch(struct md_bu *m)
{
	u32 s0, s1, p0 = ~0u, p1 = ~0u, nc0, c0, sum, psum, rch0, rch1;
	ktime_t t0;
	bool hs1_done = false, ex_acked = false;
	u32 prev0 = 0, prev1 = 0;
	int i, n = watch_ms ? watch_ms : INT_MAX;

	dev_info(m->dev, "stage4: MD_BOOT_VECTOR_EN before the write = 0x%08x\n", readl(m->bvec));
	md_boot_stats(m);
	md_dump_hif(m, "before the start");
	psum = md_smem_sum(m, &nc0, &c0);
	dev_info(m->dev, "stage4: smem head sum=0x%x nc[0]=0x%x c[0]=0x%x\n", psum, nc0, c0);

	dev_warn(m->dev, "stage4: WRITING 1 TO MD_BOOT_VECTOR_EN, MD IS STARTING\n");
	t0 = ktime_get();
	writel(1, m->bvec);
	dev_info(m->dev, "stage4: MD_BOOT_VECTOR_EN after the write = 0x%08x\n", readl(m->bvec));

	for (i = 0; i < n; i++) {
		s64 t_ms;

		msleep(hs1_done ? 5 : 20);
		t_ms = ktime_to_ms(ktime_sub(ktime_get(), t0));
		if ((watch_ms && t_ms > watch_ms) || kthread_should_stop())
			break;
		/* the MD side (MD1_CFG) is polled only BEFORE HS1: after its normal boot starts MD
		 * reconfigures its blocks, reading their registers may hang the SoC bus. */
		if (!hs1_done) {
			writel(0, m->mdcfg + 0x700);
			readl(m->mdcfg + 0x300); readl(m->mdcfg + 0x300);
			s0 = readl(m->mdcfg + 0x300);
			writel(1, m->mdcfg + 0x700);
			readl(m->mdcfg + 0x300); readl(m->mdcfg + 0x300);
			s1 = readl(m->mdcfg + 0x300);
			if (s0 != p0 || s1 != p1) {
				dev_info(m->dev, "t=%lld ms: boot_stats0=0x%x stats1=0x%x\n", t_ms, s0, s1);
				p0 = s0; p1 = s1;
			}
		}
		rch0 = readl(m->ccif0 + 0x10);
		rch1 = readl(m->ccif1 + 0x10);
		if (!hs1_done && (rch0 & BIT(15))) {
			hs1_done = true;
			md_dump_sram_full(m);
			if (ack_hs)
				writel(rch0 & 0xffff, m->ccif0 + 0x14);
			dev_info(m->dev, "HS1 received t=%lld ms, ACK=0x%x\n", t_ms, rch0 & 0xffff);
			sta_inject(9);				/* MD_STA_EV_HS1: state BOOT_WAITING_FOR_HS2 */
			if (stage >= 6 && hs2)
				md_send_hs2(m);
			if (quiet) {	/* after HS2 not a single access to the MD hardware/memory */
				dev_info(m->dev, "quiet: sleeping %d ms without accesses\n", quiet);
				msleep(quiet);
				dev_info(m->dev, "quiet: woke up, the system is alive\n");
				break;
			}
		} else if (hs1_done) {
			if ((rch0 & ~0xffffu) && (rch0 != prev0)) {	/* exception channels (bit 16+) */
				md_event(m, rch0, rch1, t_ms, &ex_acked);
				prev0 = rch0;
			}
			md_service(m, rch0);
		}
	}
	sum = md_smem_sum(m, &nc0, &c0);
	dev_info(m->dev, "end of observation: smem sum=0x%x\n", sum);
	md_dump_words(m, "EE", 0, 64);
	md_dump_ascii(m, "EE-txt", 0, 0x3000);
	md_dump_words(m, "MD-rt", SM_RT + RT_AP_SZ, 32);
	md_dump_hif(m, "after observation");
	md_dump_sram_full(m);
	md_snapshot(m, "after the start");
}

static int md_thread(void *arg)
{
	struct md_bu *m = arg;

	md_go_and_watch(m);
	while (!kthread_should_stop())
		msleep(100);
	return 0;
}

static void md_rollback(struct md_bu *m)
{
	if (m->rb) {
		vunmap(m->rb);
		m->rb = NULL;
	}
	if (m->dom_on) {
		pm_runtime_put_sync(m->mddev);
		m->dom_on = false;
		dev_info(m->dev, "rollback: MD domain released\n");
	}
	if (m->mddev) {
		dev_pm_domain_detach(m->mddev, true);
		m->mddev = NULL;
	}
	if (m->srcclk_set) {
		writel(m->srcclkena_orig, m->infra);
		m->srcclk_set = false;
		dev_info(m->dev, "rollback: SRCCLKENA=0x%08x\n", m->srcclkena_orig);
	}
	for (int i = 5; i >= 0; i--)
		if (m->rf_mask & BIT(i)) {
			regulator_disable(m->rfreg[i]);
			m->rf_mask &= ~BIT(i);
		}
	if (m->reg_sram_on) {
		regulator_disable(m->vsram);
		m->reg_sram_on = false;
	}
	if (m->reg_modem_on) {
		regulator_disable(m->vmodem);
		m->reg_modem_on = false;
	}
	md_unprep(m);
	md_snapshot(m, "after rollback");
}

static int md_bu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct md_bu *m;
	u32 v;
	int ret;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->dev = dev;
	platform_set_drvdata(pdev, m);

	m->vmodem = devm_regulator_get(dev, "vmodem");
	if (IS_ERR(m->vmodem))
		return dev_err_probe(dev, PTR_ERR(m->vmodem), "vmodem\n");
	m->vsram = devm_regulator_get(dev, "vsram_md");
	if (IS_ERR(m->vsram))
		return dev_err_probe(dev, PTR_ERR(m->vsram), "vsram_md\n");

	for (v = 0; v < 6; v++) {
		m->rfreg[v] = devm_regulator_get_optional(dev, rf_names[v]);
		if (IS_ERR(m->rfreg[v])) {
			dev_info(dev, "RF regulator %s not found (%ld)\n", rf_names[v], PTR_ERR(m->rfreg[v]));
			m->rfreg[v] = NULL;
		}
	}
	m->infra = devm_ioremap(dev, INFRACFG_AO_PHYS + MD_SRCCLKENA_OFF, 4);
	m->sec_ao = devm_ioremap(dev, SEC_AO_PHYS, 0x1000);
	m->mdcfg = devm_ioremap(dev, MD1_CFG_PAGE_PHYS, 0x1000);
	if (!m->infra || !m->sec_ao || !m->mdcfg)
		return -ENOMEM;
	if (stage >= 5) {
		m->infra_all = devm_ioremap(dev, INFRACFG_AO_PHYS, 0x400);
		m->ccif0_md = devm_ioremap(dev, AP_CCIF0_PHYS + 0x1000, 0x300);
		m->ccif1_md = devm_ioremap(dev, AP_CCIF1_PHYS + 0x1000, 0x300);
		if (!m->infra_all || !m->ccif0_md || !m->ccif1_md)
			return -ENOMEM;
		ret = devm_clk_bulk_get_all(dev, &m->clks);
		if (ret < 0)
			return dev_err_probe(dev, ret, "CCIF clocks\n");
		m->nclks = ret;
		dev_info(dev, "clocks from DT: %d\n", m->nclks);
	}
	if (stage >= 4) {
		m->bvec = devm_ioremap(dev, MD_BOOT_VECTOR_EN, 4);
		m->ccif0 = devm_ioremap(dev, AP_CCIF0_PHYS, 0x300);
		m->ccif1 = devm_ioremap(dev, AP_CCIF1_PHYS, 0x300);
		m->smem_nc = devm_memremap(dev, SMEM_NC_PHYS, SMEM_NC_CLEAR_SZ, MEMREMAP_WB);
		m->smem_c = devm_memremap(dev, SMEM_C_PHYS, 0x1000, MEMREMAP_WB);
		if (!m->bvec || !m->ccif0 || !m->ccif1 || IS_ERR(m->smem_nc) || IS_ERR(m->smem_c))
			return -ENOMEM;
		{
			int i, np = SMEM_NC_CLEAR_SZ >> PAGE_SHIFT;
			struct page **pg = kmalloc_array(np, sizeof(*pg), GFP_KERNEL);

			if (!pg)
				return -ENOMEM;
			for (i = 0; i < np; i++)
				pg[i] = pfn_to_page((SMEM_NC_PHYS >> PAGE_SHIFT) + i);
			m->rb = vmap(pg, np, VM_MAP, pgprot_writecombine(PAGE_KERNEL));
			kfree(pg);
			if (!m->rb)
				return -ENOMEM;
		}
	}

	m->srcclkena_orig = readl(m->infra);
	dev_info(dev, "md-bringup stage=%d\n", stage);
	md_snapshot(m, "initial");

	if (stage >= 5) {
		ret = md_prep(m);
		if (ret)
			goto fail;
	}
	if (stage >= 1) {
		ret = regulator_enable(m->vmodem);
		if (ret) {
			dev_err(dev, "vmodem: %d\n", ret);
			goto fail;
		}
		m->reg_modem_on = true;
		msleep(10);
		md_snapshot(m, "vmodem on");
		ret = regulator_enable(m->vsram);
		if (ret) {
			dev_err(dev, "vsram_md: %d\n", ret);
			goto fail;
		}
		m->reg_sram_on = true;
		msleep(10);
		md_snapshot(m, "vsram_md on");
		if (rf) {
			for (v = 0; v < 6; v++) {
				if (!m->rfreg[v])
					continue;
				ret = regulator_enable(m->rfreg[v]);
				dev_info(dev, "RF %s on: ret=%d %d uV\n", rf_names[v], ret, regulator_get_voltage(m->rfreg[v]));
				if (!ret)
					m->rf_mask |= BIT(v);
			}
			msleep(10);
			ret = 0;
		}
	}
	if (stage >= 2) {
		v = readl(m->infra);
		if ((v & 0xff) != 0x21) {
			writel((v & ~0xffu) | 0x21, m->infra);
			m->srcclk_set = true;
		}
		msleep(5);
		md_snapshot(m, "SRCCLKENA");
	}
	if (stage >= 3) {
		/* the domain is attached ONLY here, after vmodem/vsram_md: md has an external buck (EXT_BUCK_ISO) */
		m->mddev = dev_pm_domain_attach_by_name(dev, "md");
		if (IS_ERR_OR_NULL(m->mddev)) {
			ret = m->mddev ? PTR_ERR(m->mddev) : -ENODEV;
			m->mddev = NULL;
			dev_err(dev, "attaching the md domain: %d\n", ret);
			goto fail;
		}
		ret = pm_runtime_resume_and_get(m->mddev);
		dev_info(dev, "MD domain: pm_runtime_resume_and_get=%d\n", ret);
		if (ret < 0)
			goto fail;
		m->dom_on = true;
		msleep(50);
		md_snapshot(m, "MD domain on");
		md_boot_stats(m);
	}
	if (stage >= 4) {
		fs_md = m;
		if (!uport_register(&up_fs))
			m->misc_on = true;
		if (!uport_register(&up_rpc))
			m->rpc_on = true;
		sta_stop = false;
		if (!misc_register(&sta_misc))
			m->sta_on = true;
		ccci_kobj = kobject_create_and_add("ccci", kernel_kobj);
		if (ccci_kobj && sysfs_create_file(ccci_kobj, &ccci_ver_attr.attr))
			dev_warn(dev, "sysfs ccci/version not created\n");
		m->thr = kthread_run(md_thread, m, "md-ccci");
		if (IS_ERR(m->thr)) {
			ret = PTR_ERR(m->thr);
			m->thr = NULL;
			goto fail;
		}
	}
	return 0;
fail:
	md_rollback(m);
	return ret;
}

static void md_bu_remove(struct platform_device *pdev)
{
	struct md_bu *m = platform_get_drvdata(pdev);

	if (m->thr) {
		kthread_stop(m->thr);
		m->thr = NULL;
	}
	if (m->misc_on) {
		misc_deregister(&up_fs.misc);
		m->misc_on = false;
	}
	if (m->rpc_on) {
		misc_deregister(&up_rpc.misc);
		m->rpc_on = false;
	}
	sta_stop = true;
	wake_up_interruptible(&sta_wq);
	if (m->sta_on) {
		misc_deregister(&sta_misc);
		m->sta_on = false;
	}
	if (ccci_kobj) {
		sysfs_remove_file(ccci_kobj, &ccci_ver_attr.attr);
		kobject_put(ccci_kobj);
		ccci_kobj = NULL;
	}
	fs_md = NULL;
	md_rollback(m);
}

static const struct of_device_id md_bu_of[] = {
	{ .compatible = "xiaomi,begonia-md-bringup" },
	{ }
};
MODULE_DEVICE_TABLE(of, md_bu_of);

static struct platform_driver md_bu_driver = {
	.probe = md_bu_probe,
	.remove = md_bu_remove,
	.driver = { .name = "md-bringup", .of_match_table = md_bu_of },
};
module_platform_driver(md_bu_driver);

MODULE_DESCRIPTION("MT6785 MD1 modem power bring-up (experimental)");
MODULE_LICENSE("GPL");
