// SPDX-License-Identifier: GPL-2.0
/*
 * Redmi Note 8 Pro (begonia, MT6785) - CONNSYS bring-up experiment.
 *
 * A port of the FIRST part of the vendor sequence (MTK_kernel_modules,
 * common_main/platform/mt6785.c + mtk_wcn_consys_hw.c: mtk_wcn_consys_hw_reg_ctrl(1, co_clock=1))
 * up to and including polling_consys_chipid(). Goal: read the CONSYS IP version
 * (0x10020600 expected) -> prove that power/clock/domain are brought up correctly.
 *
 * The rest (ROM patch, WMT, gen4m) is NOT here.
 *
 * Parameters:
 *   stage=0  (default) only a snapshot of the state, turns NOTHING on
 *   stage=1  power-on in the vendor order + reading the ID; then power-off if keep_on=0
 *   stage=2  + EMI remap, ROM patch (mcu/bt/wifi) -> EMI with read-back verification; the MCU stays in RESET
 *   stage=3  + releasing RESET (the MCU starts), polling 0x18002600==0x1D1E, CPUPCR, EMI dump
 *   stage=4  + BTIF (PIO, polling) and the exchange PDA CFG <-> event 02 01 01 00 00 (STP mandatory)
 *   stage=5  + the patch_mcu body over PDA (raw) and WMT reset (01 07 01 00 04 -> 02 07 01 00 00)
 *   stage=6  WITHOUT PDA: QUERY/SET_STP (mand) -> STP full (stop-and-wait) -> QUERY_STP -> SET_CHIP_ID
 *   stage=7  = 6 + WMT FUNC_CTRL: WiFi on (01 06 02 00 03 01 -> 02 06 01 00 00), then off
 *   stage=8  = 6, but CONSYS stays ON; the shim calls wlan_probe_cb of the gen4m driver (wlan_mt6632_axi.ko)
 *   keep_on=1  do not power off after the last stage (it powers off on rmmod)
 *   fw_dir   directory with soc1_0_ram_{mcu,bt,wifi}_<fw_sfx>_hdr.bin
 */
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include "wmt_exp.h"
#include <linux/crc16.h>
#include <linux/clk-provider.h>	/* __clk_is_enabled() - for diagnostics only */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sizes.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "h4_recv.h"

/* ---- constants from the vendor mt6785.h / mt6785.c (see NOTES.md) ---- */
#define CONSYS_IP_VER_OFFSET		0x10	/* top_misc_off + 0x10 */
#define CONSYS_CONF_ID_OFFSET		0x1c
#define CONSYS_HW_ID_OFFSET		0x00	/* mcu_cfg + 0x0 */
#define CONSYS_FW_ID_OFFSET		0x04
#define CONSYS_IP_VER_ID		0x10020600

#define PERICFG_CCU_CONFIG		0x438	/* INFRASYS_COMMON_AP2MD_PCCIF4_AP_PERI_AP_CCU_CONFIG */
#define PERICFG_CON_PWR_ON_MASK		GENMASK(1, 0)

/* WARNING: in the vendor code the variable is called topckgen_base, but it is INFRACFG_AO_BASE (0x10001000)! */
#define INFRACFG_EMI_MAPPING		0x380
#define INFRACFG_EMI_PERI_MAPPING	0x388

/* TOP_RGU (taken by the watchdog -> ioremap without request_mem_region) */
#define RGU_BASE			0x10007000
#define RGU_CPU_SW_RST			0x18
#define RGU_CPU_SW_RST_BIT		BIT(12)
#define RGU_KEY				(0x88U << 24)

/* pwrap (taken by the pwrap driver): DCXO_ENABLE */
#define PWRAP_BASE			0x1000d000
#define PWRAP_DCXO_ENABLE		0x190
#define PWRAP_DCXO_CONN_ENABLE		BIT(1)

/* pins of the interface to the A-die (pinctrl is taken by the pinctrl driver) */
#define PINMUX_BASE			0x10005000
#define PINMUX_01			0x430
#define PINMUX_01_MASK			0x0000ffff
#define PINMUX_01_VALUE			0x11110000
#define PINMUX_02			0x440
#define PINMUX_02_MASK			0xfff00000
#define PINMUX_02_VALUE			0x00011111
#define PINDRV_BASE			0x11ea0000
#define PINDRV_MASK			0xff000007
#define PINDRV_VALUE			0x0

/* CONN_HIF_ON (for CPUPCR); not in DT -> ioremap by address */
#define HIF_ON_BASE			0x18007000
#define HIF_ON_CPUPCR			0x104
#define MCU_VER_ID_OFFSET		0x600
#define MCU_VER_ID_ALIVE		0x1D1E
#define MCU_DBG_OFFSET			0x16c

/* ROM patch: the wmt_rom_patch header (48 bytes), the offset in EMI from u32@24 (LE): 0xF0 | off[23:8] | flags[7:0] */
#define PATCH_HDR_SIZE			48
#define PATCH_ADDR_FIELD		24
#define PATCH_BUILDTIME_SIZE		16
/* EMI layout (mt6785.h): coredump 0x68000..0x80000, fw log 0x36500..0x66500 */
#define EMI_COREDUMP_OFF		0x68000
#define EMI_COREDUMP_END		0x80000
#define EMI_FWLOG_OFF			0x36500
#define EMI_FWLOG_END			0x66500
#define EMI_PAGED_DUMP_OFF		0x8400	/* relative to coredump */
#define EMI_BT_BUILDTIME		0x68300
#define EMI_WIFI_BUILDTIME		0x68310
#define EMI_MCU_BUILDTIME		0x68320

/* BTIF (vendor DT btif@1100c000; PIO, no DMA and IRQ - polling LSR) */
#define BTIF_BASE			0x1100c000
#define BTIF_RBR			0x00
#define BTIF_THR			0x00
#define BTIF_IER			0x04
#define BTIF_FIFOCTRL			0x08
#define BTIF_IIR			0x08
#define BTIF_FAKELCR			0x0c
#define BTIF_LSR			0x14
#define BTIF_DMA_EN			0x4c
#define BTIF_TRI_LVL			0x60
#define BTIF_WAK			0x64
#define BTIF_HANDSHAKE			0x6c
#define BTIF_LSR_DR			BIT(0)
#define BTIF_LSR_THRE			BIT(5)
#define BTIF_LSR_TEMT			BIT(6)
/* AP_DMA BTIF RX channel (vendor DT: rx-dma 0x11000c00): the same register device, the ring in RAM is written by DMA, read by the host */
#define APDMA_RX_BASE			0x11000c00
#define ADMA_RX_INT_THRE_DONE		0x3		/* RX_DMA_INT_THRE | RX_DMA_INT_DONE (W1C / enable) */
/* AP_DMA BTIF TX channel (vendor DT: tx-dma 0x11000b80), a vFIFO ring in RAM; registers from btif_dma_priv.h */
#define APDMA_TX_BASE			0x11000b80
#define APDMA_VFF_SIZE			8192
#define ADMA_INT_FLAG			0x00
#define ADMA_INT_EN			0x04
#define ADMA_EN				0x08
#define ADMA_RST			0x0c
#define ADMA_STOP			0x10
#define ADMA_FLUSH			0x14
#define ADMA_VFF_ADDR			0x1c
#define ADMA_VFF_LEN			0x24
#define ADMA_VFF_THRE			0x28
#define ADMA_VFF_WPT			0x2c
#define ADMA_VFF_RPT			0x30
#define ADMA_INT_BUF_SIZE		0x38
#define ADMA_VFF_VALID			0x3c
#define ADMA_VFF_LEFT			0x40
#define ADMA_VFF_ADDR_H			0x54
#define ADMA_WPT_MASK			0x0000ffff
#define ADMA_WPT_WRAP			0x00010000
#define BTIF_DMA_EN_RX			BIT(0)
#define BTIF_DMA_EN_TX			BIT(1)	/* BIT(0) = RX DMA (btif_priv.h) */
#define CLK_IDX_INFRA_BTIF		26	/* dt-bindings mediatek,mt6785-clk.h */
#define CLK_IDX_INFRA_AP_DMA		40
#define STP_TASK_WMT			4	/* WMT_TASK_INDX */
#define PATCH_MCU_HDR_SIZE		28	/* WMT_PATCH */
#define PATCH_CHECKSUM_SIZE		2	/* OPT_PATCH_CHECKSUM */
#define PDA_FRAG_SIZE			1000
#define EMI_PATCH_MCU_BUILDTIME		0x68330

/* MT6359 PMIC DCXO_CW00: XO_EXTBUF2 = XO_WCN (clock buffer for CONN) */
#define MT6359_DCXO_CW00		0x788
#define XO_EXTBUF2_MODE_MASK		GENMASK(4, 3)
#define XO_EXTBUF2_EN_M			BIT(5)

/* the initial state of XO_WCN observed at the first boot (before any of our runs) */
static unsigned int dcxo_en_orig = 0x3;
module_param(dcxo_en_orig, uint, 0444);
MODULE_PARM_DESC(dcxo_en_orig, "pwrap DCXO_ENABLE to restore on power-off (orig 0x3)");
static unsigned int cw00_orig = 0x4e1d;
module_param(cw00_orig, uint, 0444);
MODULE_PARM_DESC(cw00_orig, "MT6359 DCXO_CW00 to restore on power-off (orig 0x4e1d)");
static bool restore_xo;
module_param(restore_xo, bool, 0444);
MODULE_PARM_DESC(restore_xo, "at probe: write dcxo_en_orig/cw00_orig back (undo earlier runs)");

/* candidate PDA addresses for patch_mcu (the address was not recovered from the launcher — try them in turn) */
static unsigned int pda_addrs[8] = { 0x0001c000, 0x0001c000, 0x0001c000, 0x0001c000 };
static int n_pda = 4;
/* modes: 0=normal; 1=body in 100 B chunks with a 100 us pause; 2=no body (CFG->reset); 3=normal, but reset after 1.5 s */
static unsigned int pda_modes[8] = { 0, 1, 2, 3 };
module_param_array(pda_modes, uint, NULL, 0444);
MODULE_PARM_DESC(pda_modes, "per-attempt mode: 0 normal, 1 paced body, 2 no body, 3 delayed reset");
module_param_array(pda_addrs, uint, &n_pda, 0444);
MODULE_PARM_DESC(pda_addrs, "PDA target address candidates for patch_mcu (tried in order, power-cycle between)");

static int stage;
module_param(stage, int, 0444);
MODULE_PARM_DESC(stage, "0=snapshot, 1=power-on+chip id, 2=+rom patch to EMI (MCU in reset), 3=+release reset, 4=+BTIF PDA cfg handshake, 5=+patch body+WMT reset, 6=no PDA: STP full-mode bring-up, 7=+WiFi func on/off, 8=bring-up and keep on for gen4m");
static bool keep_on;
module_param(keep_on, bool, 0444);

static unsigned int calib = 1;
module_param(calib, uint, 0444);
MODULE_PARM_DESC(calib, "stage 7/8, bitmask before WIFI on: 1=QUERY_A_DIE, 2=RF calibration (default 1)");
MODULE_PARM_DESC(keep_on, "do not power off after the last stage");
static char *fw_dir = "/home/user/connsys-fw";
module_param(fw_dir, charp, 0444);
MODULE_PARM_DESC(fw_dir, "dir with soc1_0_ram_{mcu,bt,wifi}_<fw_sfx>_hdr.bin");
static char *fw_sfx = "2a_1";
module_param(fw_sfx, charp, 0444);
MODULE_PARM_DESC(fw_sfx, "firmware name suffix");

struct stp_full;

struct consys {
	struct device *dev;
	struct stp_full *stp;		/* STP full state after bring-up (needed by the HCI layer) */
	struct hci_dev *hdev;
	struct task_struct *rx_task;	/* background BTIF/STP receive while there are users (hci0, /dev/stpgps) */
	int rx_users;
	struct mutex rx_lock;		/* starting/stopping the receive thread */
	struct mutex wmt_lock;		/* one synchronous WMT command while the thread runs */
	struct completion wmt_done;
	u8 wmt_evt[64];
	int wmt_evt_len;
	bool wmt_waiting;
	void (*type_cb[8])(void *ctx, const u8 *buf, int len);	/* receivers of STP frames by type (GPS=2 etc.) */
	void *type_ctx[8];
	struct mutex txlock;		/* serializes the transmission of frames and ACKs over BTIF */
	struct sk_buff *rx_skb;		/* assembling H4 packets */
	void __iomem *mcu;		/* CONN_MCU_CONFIG_BASE  0x18002000 */
	void __iomem *top_misc_off;	/* CONN_TOP_MISC_OFF     0x180b1000 */
	void __iomem *rgu;
	void __iomem *pwrap;
	void __iomem *pinmux;
	void __iomem *pindrv;
	void __iomem *hif_on;
	void __iomem *btif;
	void __iomem *txdma;		/* AP_DMA BTIF TX channel */
	void *vff;			/* TX vFIFO (coherent) */
	dma_addr_t vff_phys;
	void __iomem *rxdma;		/* AP_DMA BTIF RX channel */
	void *rvff;			/* RX vFIFO (coherent) */
	dma_addr_t rvff_phys;
	u32 rx_rpt;			/* our read pointer (with the 0x10000 wrap bit) */
	bool rxdma_on;
	struct clk *clk_btif;
	struct clk *clk_apdma;
	u32 pda_addr;
	unsigned int pda_mode;
	void __iomem *emi;		/* ioremap of the consys-emi reserve */
	struct regmap *pericfg;
	struct regmap *infracfg;	/* infracfg_ao (vendor: "topckgen_base") */
	struct regmap *pmic;
	struct clk *ccif4;
	struct regulator *vcn18;
	struct regulator *vcn33_1;
	struct regulator *vcn33_2;
	phys_addr_t emi_base;
	resource_size_t emi_size;
	bool powered;
};

static void consys_snapshot(struct consys *c, const char *tag)
{
	unsigned int cw00 = 0, ccu = 0;

	regmap_read(c->pmic, MT6359_DCXO_CW00, &cw00);
	regmap_read(c->pericfg, PERICFG_CCU_CONFIG, &ccu);
	dev_info(c->dev, "[%s] DCXO_CW00=0x%04x pwrap.DCXO_ENABLE=0x%08x RGU.SW_RST=0x%08x pericfg.CCU_CFG=0x%08x\n",
		 tag, cw00, readl(c->pwrap + PWRAP_DCXO_ENABLE),
		 readl(c->rgu + RGU_CPU_SW_RST), ccu);
	{
		unsigned int m1 = 0, m2 = 0;

		regmap_read(c->infracfg, INFRACFG_EMI_MAPPING, &m1);
		regmap_read(c->infracfg, INFRACFG_EMI_PERI_MAPPING, &m2);
		dev_info(c->dev, "[%s] infracfg_ao EMI_MAP(+0x380)=0x%08x PERI_MAP(+0x388)=0x%08x\n", tag, m1, m2);
	}
	dev_info(c->dev, "[%s] vcn18=%d vcn33_1_wifi=%d vcn33_2_wifi=%d ccif4=%d EMI=%pa+0x%llx\n",
		 tag, regulator_is_enabled(c->vcn18), regulator_is_enabled(c->vcn33_1),
		 regulator_is_enabled(c->vcn33_2), __clk_is_enabled(c->ccif4),
		 &c->emi_base, (unsigned long long)c->emi_size);
}

static void pinmux_set(struct consys *c, bool on)
{
	u32 v;

	v = readl(c->pinmux + PINMUX_01) & PINMUX_01_MASK;
	writel(on ? v | PINMUX_01_VALUE : v, c->pinmux + PINMUX_01);
	v = readl(c->pinmux + PINMUX_02) & PINMUX_02_MASK;
	writel(on ? v | PINMUX_02_VALUE : v, c->pinmux + PINMUX_02);
	if (on) {
		v = readl(c->pindrv) & PINDRV_MASK;	/* drive 2mA */
		writel(v | PINDRV_VALUE, c->pindrv);
	}
}

static void xo_wcn_set(struct consys *c, bool on)
{
	u32 en = readl(c->pwrap + PWRAP_DCXO_ENABLE);

	if (on) {
		/* vendor clk_buf_ctrl_internal(CLK_BUF_CONN, FORCE_ON) */
		writel(en & ~PWRAP_DCXO_CONN_ENABLE, c->pwrap + PWRAP_DCXO_ENABLE);
		regmap_update_bits(c->pmic, MT6359_DCXO_CW00,
				   XO_EXTBUF2_MODE_MASK | XO_EXTBUF2_EN_M, XO_EXTBUF2_EN_M);
	} else {
		/* restore the initial state (HW control), not "everything off" */
		writel((en & ~PWRAP_DCXO_CONN_ENABLE) | (dcxo_en_orig & PWRAP_DCXO_CONN_ENABLE),
		       c->pwrap + PWRAP_DCXO_ENABLE);
		regmap_update_bits(c->pmic, MT6359_DCXO_CW00,
				   XO_EXTBUF2_MODE_MASK | XO_EXTBUF2_EN_M,
				   cw00_orig & (XO_EXTBUF2_MODE_MASK | XO_EXTBUF2_EN_M));
	}
}


/* ---------- stage 2: EMI remap + ROM patch ---------- */

static int load_fw(struct consys *c, const char *name, void **buf, size_t *len)
{
	char path[256];
	struct file *f;
	loff_t pos = 0;
	ssize_t n;
	size_t size;
	void *p;

	snprintf(path, sizeof(path), "%s/%s", fw_dir, name);
	f = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(f)) {
		dev_err(c->dev, "open %s: %ld\n", path, PTR_ERR(f));
		return PTR_ERR(f);
	}
	size = i_size_read(file_inode(f));
	if (size <= PATCH_HDR_SIZE || size > SZ_2M) {
		dev_err(c->dev, "%s: bad size %zu\n", path, size);
		filp_close(f, NULL);
		return -EINVAL;
	}
	p = vmalloc(size);
	if (!p) {
		filp_close(f, NULL);
		return -ENOMEM;
	}
	n = kernel_read(f, p, size, &pos);
	filp_close(f, NULL);
	if (n != (ssize_t)size) {
		dev_err(c->dev, "%s: short read %zd/%zu\n", path, n, size);
		vfree(p);
		return n < 0 ? n : -EIO;
	}
	*buf = p;
	*len = size;
	return 0;
}

/* consys_emi_set_remapping_reg() + consys_reset_emi_coredump() */
static void emi_prepare(struct consys *c)
{
	unsigned int v1 = 0, v2 = 0;

	regmap_update_bits(c->infracfg, INFRACFG_EMI_MAPPING, GENMASK(19, 0),
			   (c->emi_base >> 16) & GENMASK(19, 0));
	regmap_update_bits(c->infracfg, INFRACFG_EMI_PERI_MAPPING, GENMASK(19, 0),
			   0x10003000 >> 16);
	regmap_read(c->infracfg, INFRACFG_EMI_MAPPING, &v1);
	regmap_read(c->infracfg, INFRACFG_EMI_PERI_MAPPING, &v2);
	dev_info(c->dev, "EMI remap (infracfg_ao): MAPPING(+0x380)=0x%08x (want base>>16=0x%llx) PERI(+0x388)=0x%08x\n",
		 v1, (unsigned long long)(c->emi_base >> 16), v2);

	memset_io(c->emi + EMI_COREDUMP_OFF, 0, 0x400);
	memset_io(c->emi + EMI_COREDUMP_OFF + EMI_PAGED_DUMP_OFF, 0, 0x8000);
}

static int rom_patch_download(struct consys *c)
{
	static const struct { const char *fmt, *what; u32 btoff; } pt[] = {
		{ "soc1_0_ram_bt_%s_hdr.bin",   "bt",   EMI_BT_BUILDTIME },
		{ "soc1_0_ram_wifi_%s_hdr.bin", "wifi", EMI_WIFI_BUILDTIME },
		{ "soc1_0_ram_mcu_%s_hdr.bin",  "mcu",  EMI_MCU_BUILDTIME },
	};
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(pt); i++) {
		char name[96], date[PATCH_BUILDTIME_SIZE + 1];
		size_t len, blen;
		void *buf = NULL, *chk = NULL;
		u32 addr, off;
		u8 *hdr, *body;

		snprintf(name, sizeof(name), pt[i].fmt, fw_sfx);
		ret = load_fw(c, name, &buf, &len);
		if (ret)
			return ret;
		hdr = buf;
		body = hdr + PATCH_HDR_SIZE;
		blen = len - PATCH_HDR_SIZE;
		addr = get_unaligned_le32(hdr + PATCH_ADDR_FIELD);
		off = addr & 0x00ffff00;
		memcpy(date, hdr, PATCH_BUILDTIME_SIZE);
		date[PATCH_BUILDTIME_SIZE] = 0;

		if ((addr >> 24) != 0xf0) {
			dev_err(c->dev, "[%s] header addr 0x%08x: not an MCU-EMI address (0xF0xxxxxx)\n",
				pt[i].what, addr);
			ret = -EINVAL;
			goto next;
		}
		if (off + blen > c->emi_size ||
		    (off < EMI_COREDUMP_END && off + blen > EMI_FWLOG_OFF)) {
			dev_err(c->dev, "[%s] range 0x%x+0x%zx collides with EMI fw-log/coredump area or overflows 0x%llx\n",
				pt[i].what, off, blen, (unsigned long long)c->emi_size);
			ret = -ERANGE;
			goto next;
		}
		dev_info(c->dev, "[%s] %s: build=%s hdr_addr=0x%08x -> EMI+0x%06x size=0x%zx\n",
			 pt[i].what, name, date, addr, off, blen);

		memcpy_toio(c->emi + off, body, blen);
		memcpy_toio(c->emi + pt[i].btoff, hdr, PATCH_BUILDTIME_SIZE);

		chk = vmalloc(blen);
		if (!chk) {
			ret = -ENOMEM;
			goto next;
		}
		memcpy_fromio(chk, c->emi + off, blen);
		if (memcmp(chk, body, blen)) {
			dev_err(c->dev, "[%s] readback MISMATCH\n", pt[i].what);
			ret = -EIO;
		} else {
			dev_info(c->dev, "[%s] readback OK\n", pt[i].what);
		}
next:
		vfree(chk);
		vfree(buf);
		if (ret)
			return ret;
	}
	return 0;
}

static void emi_dump(struct consys *c, const char *tag, u32 off)
{
	dev_info(c->dev, "EMI+0x%05x (%s): %08x %08x %08x %08x %08x %08x %08x %08x\n", off, tag,
		 readl(c->emi + off), readl(c->emi + off + 4), readl(c->emi + off + 8),
		 readl(c->emi + off + 12), readl(c->emi + off + 16), readl(c->emi + off + 20),
		 readl(c->emi + off + 24), readl(c->emi + off + 28));
}

/* ---------- stage 3: release reset, see whether the MCU comes alive ---------- */
static int mcu_start(struct consys *c)
{
	u32 v, id = 0;
	int i;

	/* consys_hw_reset_bit_set(DISABLE) */
	v = readl(c->rgu + RGU_CPU_SW_RST) & 0x00ffffff & ~RGU_CPU_SW_RST_BIT;
	writel(v | RGU_KEY, c->rgu + RGU_CPU_SW_RST);
	dev_info(c->dev, "MCU reset released: RGU.SW_RST=0x%08x\n", readl(c->rgu + RGU_CPU_SW_RST));

	for (i = 0; i <= 10; i++) {
		id = readl(c->mcu + MCU_VER_ID_OFFSET);
		if (id == MCU_VER_ID_ALIVE)
			break;
		dev_info(c->dev, "poll %d: 0x18002600=0x%x 0x1800216c=0x%x CPUPCR=0x%x\n", i, id,
			 readl(c->mcu + MCU_DBG_OFFSET), readl(c->hif_on + HIF_ON_CPUPCR));
		msleep(20);
	}
	dev_info(c->dev, "RESULT: 0x18002600=0x%x (alive marker 0x%x) -> %s\n", id, MCU_VER_ID_ALIVE,
		 id == MCU_VER_ID_ALIVE ? "MCU ALIVE" : "NOT confirmed");
	msleep(20);

	for (i = 0; i < 5; i++) {
		dev_info(c->dev, "CPUPCR[%d]=0x%08x\n", i, readl(c->hif_on + HIF_ON_CPUPCR));
		msleep(10);
	}
	emi_dump(c, "coredump hdr", EMI_COREDUMP_OFF);
	emi_dump(c, "build times", 0x68300);
	emi_dump(c, "fw log ring", EMI_FWLOG_OFF);
	emi_dump(c, "MET data", 0x2e500);
	return id == MCU_VER_ID_ALIVE ? 0 : -ETIMEDOUT;
}


/* ---------- stage 4/5: BTIF (PIO, polling) + STP mandatory + PDA ---------- */

static struct clk *infra_clk(unsigned int idx)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "mediatek,mt6785-infracfg");
	struct of_phandle_args a = { .np = np, .args_count = 1, .args = { idx } };
	struct clk *clk;

	if (!np)
		return ERR_PTR(-ENODEV);
	clk = of_clk_get_from_provider(&a);
	of_node_put(np);
	return clk;
}

static void btif_regs(struct consys *c, const char *tag)
{
	dev_info(c->dev, "BTIF[%s] LSR=0x%02x IIR=0x%02x IER=0x%02x TRI_LVL=0x%02x WAK=0x%x HANDSHAKE=0x%x DMA_EN=0x%x\n", tag,
		 readl(c->btif + BTIF_LSR), readl(c->btif + BTIF_IIR), readl(c->btif + BTIF_IER),
		 readl(c->btif + BTIF_TRI_LVL), readl(c->btif + BTIF_WAK),
		 readl(c->btif + BTIF_HANDSHAKE), readl(c->btif + BTIF_DMA_EN));
}

static void btif_exit(struct consys *c)
{
	if (!IS_ERR_OR_NULL(c->clk_apdma)) {
		clk_disable_unprepare(c->clk_apdma);
		clk_put(c->clk_apdma);
	}
	if (!IS_ERR_OR_NULL(c->clk_btif)) {
		clk_disable_unprepare(c->clk_btif);
		clk_put(c->clk_btif);
	}
	c->clk_apdma = c->clk_btif = NULL;
}

/* vendor hal_btif_hw_init(): FIFO enable+clear, handshake, thresholds, no DMA; IRQ not enabled (polling) */
static int btif_init(struct consys *c)
{
	int ret;
	u32 v;

	c->clk_btif = infra_clk(CLK_IDX_INFRA_BTIF);
	c->clk_apdma = infra_clk(CLK_IDX_INFRA_AP_DMA);
	if (IS_ERR(c->clk_btif) || IS_ERR(c->clk_apdma)) {
		dev_err(c->dev, "BTIF clocks: btif=%d apdma=%d\n", PTR_ERR_OR_ZERO(c->clk_btif),
			PTR_ERR_OR_ZERO(c->clk_apdma));
		ret = -ENODEV;
		goto err;
	}
	ret = clk_prepare_enable(c->clk_btif);
	if (ret)
		goto err;
	ret = clk_prepare_enable(c->clk_apdma);
	if (ret) {
		clk_disable_unprepare(c->clk_btif);
		goto err;
	}
	btif_regs(c, "clk-on");

	writel(0, c->btif + BTIF_FAKELCR);			/* normal mode */
	writel(readl(c->btif + BTIF_HANDSHAKE) | 1, c->btif + BTIF_HANDSHAKE);	/* new handshake */
	/* FIFOCTRL = 16550 FCR: bit0 FIFO enable, bit1 clr RX, bit2 clr TX (the vendor does an RMW through IIR, where bit0=1) */
	writel(0x1 | 0x2, c->btif + BTIF_FIFOCTRL);
	writel(0x1, c->btif + BTIF_FIFOCTRL);
	writel(0x1 | 0x4, c->btif + BTIF_FIFOCTRL);
	writel(0x1, c->btif + BTIF_FIFOCTRL);
	writel(8 | (1 << 4), c->btif + BTIF_TRI_LVL);		/* tx thr 8, rx thr 1, loopback off */
	v = readl(c->btif + BTIF_DMA_EN);
	writel((v & ~(BIT(0) | BIT(1))) | BIT(2), c->btif + BTIF_DMA_EN);	/* DMA off, autorst */
	writel(0, c->btif + BTIF_IER);				/* polling, no interrupts */
	btif_regs(c, "init");
	return 0;
err:
	btif_exit(c);
	return ret;
}

/* ap_wakeup_consys: WAK=0, >1/32k, WAK=1 */
static void btif_wake(struct consys *c)
{
	writel(readl(c->btif + BTIF_WAK) & ~BIT(0), c->btif + BTIF_WAK);
	usleep_range(128, 160);
	writel(readl(c->btif + BTIF_WAK) | BIT(0), c->btif + BTIF_WAK);
	usleep_range(500, 1000);
}

static int btif_tx(struct consys *c, const u8 *p, size_t len)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(500);
	size_t i = 0;

	while (i < len) {
		u32 lsr = readl(c->btif + BTIF_LSR);
		int room = (lsr & BTIF_LSR_TEMT) ? 16 : (lsr & BTIF_LSR_THRE) ? 8 : 0;

		if (!room) {
			if (time_after(jiffies, deadline)) {
				dev_err(c->dev, "BTIF tx stuck at %zu/%zu LSR=0x%x\n", i, len, lsr);
				return -ETIMEDOUT;
			}
			udelay(2);
			continue;
		}
		room = min_t(size_t, room, len - i);
		while (room--)
			writeb(p[i++], c->btif + BTIF_THR);
		deadline = jiffies + msecs_to_jiffies(500);
	}
	deadline = jiffies + msecs_to_jiffies(200);
	while (!(readl(c->btif + BTIF_LSR) & BTIF_LSR_TEMT)) {
		if (time_after(jiffies, deadline))
			return -ETIMEDOUT;
		udelay(5);
	}
	return 0;
}

/*
 * TX through the AP_DMA vFIFO, like the vendor (hal_btif_dma_hw_init + hal_dma_send_data). RX and the control frames stay PIO.
 * Enabled while the PDA body is transmitted; afterwards back to PIO.
 */
static bool pda_dma = true;
module_param(pda_dma, bool, 0444);
MODULE_PARM_DESC(pda_dma, "send PDA header/body through AP_DMA BTIF TX vFIFO instead of PIO");

static void btif_dma_regs(struct consys *c, const char *tag)
{
	void __iomem *d = c->txdma;

	dev_info(c->dev, "TXDMA[%s] EN=%x INT=%x WPT=%x RPT=%x VALID=%x LEFT=%x INTBUF=%x BTIF.DMA_EN=%x\n", tag,
		 readl(d + ADMA_EN), readl(d + ADMA_INT_FLAG), readl(d + ADMA_VFF_WPT), readl(d + ADMA_VFF_RPT),
		 readl(d + ADMA_VFF_VALID), readl(d + ADMA_VFF_LEFT), readl(d + ADMA_INT_BUF_SIZE),
		 readl(c->btif + BTIF_DMA_EN));
}

static int btif_dma_start(struct consys *c)
{
	void __iomem *d = c->txdma;
	int i;

	if (!c->vff) {
		dma_set_mask_and_coherent(c->dev, DMA_BIT_MASK(32));
		c->vff = dma_alloc_coherent(c->dev, APDMA_VFF_SIZE, &c->vff_phys, GFP_KERNEL);
		if (!c->vff)
			return -ENOMEM;
	}
	memset(c->vff, 0, APDMA_VFF_SIZE);

	/* warm reset of the channel, wait for EN=0 (vendor: up to 10x100us, otherwise a hard reset) */
	writel(1, d + ADMA_RST);
	for (i = 0; i < 10 && (readl(d + ADMA_EN) & 1); i++)
		udelay(100);
	if (i >= 10) {
		writel(2, d + ADMA_RST);
		writel(0, d + ADMA_RST);
	}
	writel(lower_32_bits(c->vff_phys), d + ADMA_VFF_ADDR);
	writel(upper_32_bits(c->vff_phys), d + ADMA_VFF_ADDR_H);
	writel(APDMA_VFF_SIZE, d + ADMA_VFF_LEN);
	writel(0, d + ADMA_VFF_WPT);
	writel(0, d + ADMA_VFF_RPT);
	writel(APDMA_VFF_SIZE - 7, d + ADMA_VFF_THRE);	/* DMA_TX_THRE(n) = n - 7 */
	writel(0, d + ADMA_INT_FLAG);
	writel(0, d + ADMA_INT_EN);			/* polling, no IRQ */
	writel(1, d + ADMA_EN);
	/* BTIF: TX into DMA mode */
	writel(readl(c->btif + BTIF_DMA_EN) | BTIF_DMA_EN_TX, c->btif + BTIF_DMA_EN);
	btif_dma_regs(c, "start");
	return 0;
}

static void btif_dma_stop(struct consys *c)
{
	void __iomem *d = c->txdma;

	writel(readl(c->btif + BTIF_DMA_EN) & ~BTIF_DMA_EN_TX, c->btif + BTIF_DMA_EN);
	writel(1, d + ADMA_STOP);
	writel(1, d + ADMA_RST);
	writel(0, d + ADMA_EN);
}

static void btif_dma_free(struct consys *c)
{
	if (c->vff) {
		dma_free_coherent(c->dev, APDMA_VFF_SIZE, c->vff, c->vff_phys);
		c->vff = NULL;
	}
}

static int btif_tx_dma(struct consys *c, const u8 *p, size_t len)
{
	void __iomem *d = c->txdma;
	unsigned long deadline = jiffies + msecs_to_jiffies(1000);

	while (len) {
		u32 left = readl(d + ADMA_VFF_LEFT) & 0xffff;
		u32 wr = readl(d + ADMA_VFF_WPT);
		u32 wpt = wr & ADMA_WPT_MASK, wrap = wr & ADMA_WPT_WRAP;
		u32 n, valid;

		if (!left) {
			if (time_after(jiffies, deadline)) {
				dev_err(c->dev, "TX DMA stuck (no room), %zu left\n", len);
				btif_dma_regs(c, "stuck");
				return -ETIMEDOUT;
			}
			usleep_range(50, 100);
			continue;
		}
		n = min_t(size_t, len, left);
		if (n + wpt >= APDMA_VFF_SIZE) {		/* like the vendor: ==size wraps too */
			u32 tail = APDMA_VFF_SIZE - wpt;

			memcpy(c->vff + wpt, p, tail);
			memcpy(c->vff, p + tail, n - tail);
			wpt = n - tail;
			wrap ^= ADMA_WPT_WRAP;
		} else {
			memcpy(c->vff + wpt, p, n);
			wpt += n;
		}
		wmb();
		writel(wpt | wrap, d + ADMA_VFF_WPT);
		valid = readl(d + ADMA_VFF_VALID) & 0xffff;
		if (valid > 0 && valid < 8)		/* the DMA does not pick up a tail of <8 bytes by itself */
			writel(1, d + ADMA_FLUSH);
		p += n;
		len -= n;
		deadline = jiffies + msecs_to_jiffies(1000);
	}
	/* wait until the vFIFO and the DMA internal buffer are empty, then BTIF TEMT */
	for (;;) {
		u32 valid = readl(d + ADMA_VFF_VALID) & 0xffff;
		u32 ibuf = readl(d + ADMA_INT_BUF_SIZE) & 0xffff;

		if (!valid && !ibuf && (readl(c->btif + BTIF_LSR) & BTIF_LSR_TEMT))
			return 0;
		if (valid > 0 && valid < 8)
			writel(1, d + ADMA_FLUSH);
		if (time_after(jiffies, deadline)) {
			dev_err(c->dev, "TX DMA drain timeout valid=%u ibuf=%u\n", valid, ibuf);
			btif_dma_regs(c, "drain");
			return -ETIMEDOUT;
		}
		usleep_range(50, 100);
	}
}

/*
 * RX through the AP_DMA vFIFO. Polling the PIO registers (2 MMIO reads per byte) cannot keep up with the BTIF line at full speed:
 * the receive FIFO overflows and bytes are lost (the GNSS stream: 512 B frames get "shortened" by ~20 bytes, the CRC does not match).
 * The DMA moves the FIFO into a ring in RAM by itself; the host reads from its RPT up to WPT (protocol as in hal_rx_dma_irq_handler).
 */
#define RX_VFF_SIZE		APDMA_VFF_SIZE
static bool rx_dma = true;
module_param(rx_dma, bool, 0644);
MODULE_PARM_DESC(rx_dma, "receive STP/BTIF data through AP_DMA RX vFIFO while the rx thread runs (PIO polling loses bytes at line rate)");

static int btif_rxdma_start(struct consys *c, u8 *drain, int *drained, int drain_max)
{
	void __iomem *d = c->rxdma;
	int i, n = 0;

	if (!c->rvff) {
		dma_set_mask_and_coherent(c->dev, DMA_BIT_MASK(32));
		c->rvff = dma_alloc_coherent(c->dev, RX_VFF_SIZE, &c->rvff_phys, GFP_KERNEL);
		if (!c->rvff)
			return -ENOMEM;
	}
	memset(c->rvff, 0, RX_VFF_SIZE);
	/* pick up the leftovers in the PIO FIFO (e.g. the start of a frame) before switching */
	while ((readl(c->btif + BTIF_LSR) & BTIF_LSR_DR) && n < drain_max)
		drain[n++] = readb(c->btif + BTIF_RBR);
	*drained = n;

	writel(1, d + ADMA_RST);
	for (i = 0; i < 10 && (readl(d + ADMA_EN) & 1); i++)
		udelay(100);
	if (i >= 10) {
		writel(2, d + ADMA_RST);
		writel(0, d + ADMA_RST);
	}
	writel(lower_32_bits(c->rvff_phys), d + ADMA_VFF_ADDR);
	writel(upper_32_bits(c->rvff_phys), d + ADMA_VFF_ADDR_H);
	writel(RX_VFF_SIZE, d + ADMA_VFF_LEN);
	writel(0, d + ADMA_VFF_WPT);
	writel(0, d + ADMA_VFF_RPT);
	writel(1, d + ADMA_VFF_THRE);			/* DMA_RX_THRE = 1 */
	writel(ADMA_RX_INT_THRE_DONE, d + ADMA_INT_FLAG);	/* clear the flags (W1C) */
	writel(ADMA_RX_INT_THRE_DONE, d + ADMA_INT_EN);		/* like the vendor; the IRQ line is not requested (polling) */
	c->rx_rpt = 0;
	writel(1, d + ADMA_EN);
	writel(readl(c->btif + BTIF_DMA_EN) | BTIF_DMA_EN_RX, c->btif + BTIF_DMA_EN);
	c->rxdma_on = true;
	return 0;
}

static void btif_rxdma_stop(struct consys *c)
{
	void __iomem *d = c->rxdma;

	c->rxdma_on = false;
	writel(readl(c->btif + BTIF_DMA_EN) & ~BTIF_DMA_EN_RX, c->btif + BTIF_DMA_EN);
	writel(1, d + ADMA_STOP);
	writel(1, d + ADMA_RST);
	writel(0, d + ADMA_EN);
	writel(0, d + ADMA_INT_EN);
}

/* take up to max bytes from the ring; 0 - no data */
static int btif_rxdma_read(struct consys *c, u8 *buf, int max)
{
	void __iomem *d = c->rxdma;
	u32 wreg = readl(d + ADMA_VFF_WPT), rpt = c->rx_rpt;
	u32 wpt = wreg & ADMA_WPT_MASK, rp = rpt & ADMA_WPT_MASK;
	u32 wrap_r = rpt & ADMA_WPT_WRAP, avail, n, tail;

	dma_rmb();
	avail = ((wreg & ADMA_WPT_WRAP) != wrap_r) ? wpt + RX_VFF_SIZE - rp : wpt - rp;
	if (!avail)
		return 0;
	n = min_t(u32, avail, max);
	tail = RX_VFF_SIZE - rp;
	if (n <= tail) {
		memcpy(buf, c->rvff + rp, n);
	} else {
		memcpy(buf, c->rvff + rp, tail);
		memcpy(buf + tail, c->rvff, n - tail);
	}
	rp += n;
	if (rp >= RX_VFF_SIZE) {
		rp -= RX_VFF_SIZE;
		wrap_r ^= ADMA_WPT_WRAP;
	}
	c->rx_rpt = rp | wrap_r;
	writel(c->rx_rpt, d + ADMA_VFF_RPT);
	return n;
}

/* wait for the first byte (sleeping), then read tightly until the line is quiet for idle_us */
static int btif_rx(struct consys *c, u8 *buf, int max, unsigned int first_ms, unsigned int idle_us)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(first_ms);
	unsigned int idle = 0;
	int n = 0;

	while (!(readl(c->btif + BTIF_LSR) & BTIF_LSR_DR)) {
		if (time_after(jiffies, deadline))
			return 0;
		usleep_range(100, 200);
	}
	for (;;) {
		if (readl(c->btif + BTIF_LSR) & BTIF_LSR_DR) {
			u8 b = readb(c->btif + BTIF_RBR);

			if (n < max)
				buf[n++] = b;
			idle = 0;
		} else {
			if (++idle > idle_us)
				break;
			udelay(1);
		}
	}
	return n;
}

/* STP mandatory frame: 80 | type<<4|len>>8 | len | 00 | payload | 00 00 */
static int wmt_cmd(struct consys *c, const char *name, const u8 *cmd, int clen,
		   const u8 *evt, int elen, unsigned int tmo_ms)
{
	u8 tx[4 + 64 + 2], rx[160];
	unsigned long deadline = jiffies + msecs_to_jiffies(tmo_ms);
	int n = 0, ret, flen = -1;

	tx[0] = 0x80;
	tx[1] = (STP_TASK_WMT << 4) | ((clen >> 8) & 0x0f);
	tx[2] = clen & 0xff;
	tx[3] = 0x00;
	memcpy(tx + 4, cmd, clen);
	tx[4 + clen] = 0;
	tx[5 + clen] = 0;

	dev_info(c->dev, "[%s] TX %d bytes: %*ph\n", name, 6 + clen, min(6 + clen, 32), tx);
	btif_wake(c);
	ret = btif_tx(c, tx, 6 + clen);
	if (ret) {
		dev_err(c->dev, "[%s] tx failed: %d\n", name, ret);
		return ret;
	}

	while (time_before(jiffies, deadline)) {
		unsigned int left = jiffies_to_msecs(deadline - jiffies);
		int got = btif_rx(c, rx + n, sizeof(rx) - n, left, 3000);

		if (!got)
			break;
		n += got;
		if (n >= 4 && rx[0] == 0x80) {
			flen = ((rx[1] & 0x0f) << 8) | rx[2];
			if (n >= 4 + flen + 2)
				break;
		}
	}
	if (!n) {
		dev_err(c->dev, "[%s] NO RESPONSE in %u ms\n", name, tmo_ms);
		btif_regs(c, "no-resp");
		return -ETIMEDOUT;
	}
	dev_info(c->dev, "[%s] RX %d bytes: %*ph\n", name, n, min(n, 48), rx);
	if (n >= 4 && rx[0] == 0x80) {
		flen = ((rx[1] & 0x0f) << 8) | rx[2];
		dev_info(c->dev, "[%s] frame: type=%d len=%d payload=%*ph\n", name, (rx[1] >> 4) & 7, flen,
			 min(max(min(flen, n - 4), 0), 32), rx + 4);
		if (flen == elen && n >= 4 + flen && !memcmp(rx + 4, evt, elen)) {
			dev_info(c->dev, "[%s] EVENT OK\n", name);
			return 0;
		}
	}
	dev_err(c->dev, "[%s] unexpected response (want %*ph)\n", name, elen, evt);
	return -EBADMSG;
}


/* like wmt_cmd, but returns the payload of the received STP frame */
static int wmt_xfer(struct consys *c, const char *name, const u8 *cmd, int clen,
		    u8 *out, int outmax, int *outlen, unsigned int tmo_ms)
{
	u8 tx[4 + 64 + 2], rx[160];
	unsigned long deadline = jiffies + msecs_to_jiffies(tmo_ms);
	int n = 0, ret, flen = -1;

	tx[0] = 0x80;
	tx[1] = (STP_TASK_WMT << 4) | ((clen >> 8) & 0x0f);
	tx[2] = clen & 0xff;
	tx[3] = 0x00;
	memcpy(tx + 4, cmd, clen);
	tx[4 + clen] = 0;
	tx[5 + clen] = 0;

	dev_info(c->dev, "[%s] TX %d bytes: %*ph\n", name, 6 + clen, min(6 + clen, 32), tx);
	btif_wake(c);
	ret = btif_tx(c, tx, 6 + clen);
	if (ret) {
		dev_err(c->dev, "[%s] tx failed: %d\n", name, ret);
		return ret;
	}
	while (time_before(jiffies, deadline)) {
		int got = btif_rx(c, rx + n, sizeof(rx) - n, jiffies_to_msecs(deadline - jiffies), 3000);

		if (!got)
			break;
		n += got;
		if (n >= 4 && rx[0] == 0x80) {
			flen = ((rx[1] & 0x0f) << 8) | rx[2];
			if (n >= 4 + flen + 2)
				break;
		}
	}
	if (!n) {
		dev_err(c->dev, "[%s] NO RESPONSE in %u ms\n", name, tmo_ms);
		return -ETIMEDOUT;
	}
	dev_info(c->dev, "[%s] RX %d bytes: %*ph\n", name, n, min(n, 48), rx);
	if (n < 4 || rx[0] != 0x80 || n < 4 + flen)
		return -EBADMSG;
	*outlen = flen;
	memcpy(out, rx + 4, min(flen, outmax));
	return 0;
}

/* vendor wmt_core_reg_rw_raw(read): 01 08 10 00 | 02 01 00 01 | addr | value | mask */
static int wmt_reg_read(struct consys *c, u32 addr, u32 *val)
{
	u8 cmd[20] = { 0x01, 0x08, 0x10, 0x00, 0x02, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff };
	u8 out[64];
	int olen = 0, ret;

	put_unaligned_le32(addr, cmd + 8);
	ret = wmt_xfer(c, "REG-RD", cmd, sizeof(cmd), out, sizeof(out), &olen, 500);
	if (ret)
		return ret;
	if (olen < 16 || out[0] != 0x02 || out[1] != 0x08 || get_unaligned_le32(out + 8) != addr) {
		dev_err(c->dev, "[REG-RD] unexpected event for 0x%08x\n", addr);
		return -EBADMSG;
	}
	*val = get_unaligned_le32(out + 12);
	return 0;
}


/* ---------- STP full mode (minimum: stop-and-wait, one frame in flight) ---------- */
struct stp_full {
	u8 txseq;	/* 3 bits */
	u8 txack;	/* seq of the last frame received from the MCU */
	u8 rxexp;	/* expected seq of the next frame from the MCU */
	u8 rxack;	/* the last ack that came from the MCU (transmit window) */
	int rxn;	/* bytes in rxbuf (the remainder between recv calls) */
	u8 rxbuf[1024];	/* max. STP frame ~460 B (calibration event 0x1c6) + header/CRC */
};

#define STP_MAX_PAYLOAD	1100
static bool stp_quiet;	/* true while hci0 runs: do not clutter dmesg with a per-frame log */

static int stp_full_send(struct consys *c, struct stp_full *st, u8 type, const u8 *pl, int len)
{
	u8 tx[4 + STP_MAX_PAYLOAD + 2];
	u16 crc;
	int ret;

	if (len > STP_MAX_PAYLOAD)
		return -EMSGSIZE;
	crc = crc16(0, pl, len);	/* CRC-16/ARC == osal_crc16 */
	mutex_lock(&c->txlock);
	tx[0] = 0x80 | (st->txseq << 3) | st->txack;
	tx[1] = (type << 4) | ((len >> 8) & 0x0f);
	tx[2] = len & 0xff;
	tx[3] = (tx[0] + tx[1] + tx[2]) & 0xff;
	memcpy(tx + 4, pl, len);
	tx[4 + len] = crc & 0xff;
	tx[5 + len] = crc >> 8;
	if (!stp_quiet)
		dev_info(c->dev, "[STP-FULL] TX seq=%u ack=%u type=%u len=%d: %*ph\n", st->txseq, st->txack, type, len,
			 min(6 + len, 32), tx);
	st->txseq = (st->txseq + 1) & 7;
	btif_wake(c);
	ret = btif_tx(c, tx, 6 + len);
	mutex_unlock(&c->txlock);
	return ret;
}

static void stp_full_ack(struct consys *c, struct stp_full *st)
{
	u8 h[4];

	h[0] = 0x80 | st->txack;
	h[1] = 0;
	h[2] = 0;
	h[3] = (h[0] + h[1] + h[2]) & 0xff;
	mutex_lock(&c->txlock);
	btif_tx(c, h, 4);
	mutex_unlock(&c->txlock);
}

/*
 * receive one frame of the given type (WMT=4, BT=0) from the full-mode stream; header-only frames (ACK from the MCU) and frames
 * of other types are skipped (acknowledged). The st->rxbuf buffer lives between calls - the remainder is not lost.
 */
static int stp_full_recv_t(struct consys *c, struct stp_full *st, int want_type, u8 *out, int outmax, int *outlen,
			   unsigned int tmo_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(tmo_ms);
	u8 *buf = st->rxbuf;

	for (;;) {
		int pos = 0, found = 0;

		while (st->rxn - pos >= 4) {
			u8 *h = buf + pos;
			int len, seq, ack, type;
			u16 crc, want;

			if ((h[0] & 0xc0) != 0x80 || ((h[0] + h[1] + h[2]) & 0xff) != h[3]) {
				pos++;			/* not a header — resynchronize byte by byte */
				continue;
			}
			seq = (h[0] >> 3) & 7;
			ack = h[0] & 7;
			type = (h[1] >> 4) & 7;
			len = ((h[1] & 0x0f) << 8) | h[2];
			st->rxack = ack;
			if (!len) {			/* header-only: ACK of our frame */
				if (!stp_quiet)
					dev_info(c->dev, "[STP-FULL] RX ack=%d (header only)\n", ack);
				pos += 4;
				continue;
			}
			if (st->rxn - pos < 4 + len + 2)
				break;			/* wait for the rest */
			crc = h[4 + len] | (h[5 + len] << 8);
			want = crc16(0, h + 4, len);
			if (crc != want) {
				dev_err(c->dev, "[STP-FULL] CRC bad (got 0x%04x want 0x%04x), len=%d\n", crc, want, len);
				pos++;
				continue;
			}
			dev_info(c->dev, "[STP-FULL] RX seq=%d ack=%d type=%d len=%d payload: %*ph\n", seq, ack, type, len,
				 min(len, 32), h + 4);
			pos += 4 + len + 2;
			if (seq == st->rxexp) {
				st->txack = seq;
				st->rxexp = (seq + 1) & 7;
				stp_full_ack(c, st);
				if (type == want_type) {
					*outlen = len;
					memcpy(out, h + 4, min(len, outmax));
					found = 1;
					break;
				}
			} else {
				dev_warn(c->dev, "[STP-FULL] unexpected seq %d (want %d)\n", seq, st->rxexp);
				stp_full_ack(c, st);
			}
		}
		if (pos) {
			memmove(buf, buf + pos, st->rxn - pos);
			st->rxn -= pos;
		}
		if (found)
			return 0;
		if (!time_before(jiffies, deadline))
			break;
		{
			int got = btif_rx(c, buf + st->rxn, sizeof(st->rxbuf) - st->rxn,
					  jiffies_to_msecs(deadline - jiffies), 2000);

			if (!got)
				break;
			st->rxn += got;
			if (!stp_quiet)
				dev_info(c->dev, "[STP-FULL] RX +%d (total %d): %*ph\n", got, st->rxn, min(st->rxn, 40), buf);
		}
	}
	return -ETIMEDOUT;
}

static int stp_full_recv(struct consys *c, struct stp_full *st, u8 *out, int outmax, int *outlen,
			 unsigned int tmo_ms)
{
	return stp_full_recv_t(c, st, STP_TASK_WMT, out, outmax, outlen, tmo_ms);
}

static int stp_full_cmd(struct consys *c, struct stp_full *st, const char *name, const u8 *cmd, int clen,
			const u8 *evt, int elen, unsigned int tmo_ms)
{
	u8 out[64];
	int olen = 0, ret;

	ret = stp_full_send(c, st, STP_TASK_WMT, cmd, clen);
	if (ret)
		return ret;
	ret = stp_full_recv(c, st, out, sizeof(out), &olen, tmo_ms);
	if (ret) {
		dev_err(c->dev, "[%s] NO WMT EVENT in %u ms\n", name, tmo_ms);
		return ret;
	}
	if (olen == elen && !memcmp(out, evt, elen)) {
		dev_info(c->dev, "[%s] EVENT OK\n", name);
		return 0;
	}
	dev_err(c->dev, "[%s] unexpected event (len %d, want %d): %*ph\n", name, olen, elen, min(olen, 32), out);
	return -EBADMSG;
}

/* like stp_full_cmd, but the event is checked by prefix, and the full answer is printed (A-die etc.) */
static int stp_full_cmd_pfx(struct consys *c, struct stp_full *st, const char *name, const u8 *cmd, int clen,
			    const u8 *evt, int elen, unsigned int tmo_ms)
{
	u8 out[64];
	int olen = 0, ret;

	ret = stp_full_send(c, st, STP_TASK_WMT, cmd, clen);
	if (ret)
		return ret;
	ret = stp_full_recv(c, st, out, sizeof(out), &olen, tmo_ms);
	if (ret) {
		dev_err(c->dev, "[%s] NO WMT EVENT in %u ms\n", name, tmo_ms);
		return ret;
	}
	dev_info(c->dev, "[%s] event (%d B): %*ph\n", name, olen, min(olen, 32), out);
	if (olen >= elen && !memcmp(out, evt, elen))
		return 0;
	dev_err(c->dev, "[%s] unexpected event prefix (want %*ph)\n", name, elen, evt);
	return -EBADMSG;
}

/* ---------- Bluetooth: FUNC_CTRL(BT) + HCI (H4) in STP frames of type 0 (vendor bt/mt66xx/legacy/stp_chrdev_bt.c) ---------- */
static bool bt_test;
module_param(bt_test, bool, 0444);
MODULE_PARM_DESC(bt_test, "stage 7/8: after WIFI on also enable the BT function and exchange HCI Reset/Read Version/Read BD_ADDR over STP type 0");

/* send an H4 command, read BT frames until Command Complete/Status (04 0e / 04 0f); print everything */
static int bt_hci_xfer(struct consys *c, struct stp_full *st, const char *name, const u8 *cmd, int clen,
		       u8 *out, int outmax, int *olen)
{
	int ret, i;

	ret = stp_full_send(c, st, 0 /* BT_TASK_INDX */, cmd, clen);
	if (ret)
		return ret;
	for (i = 0; i < 4; i++) {
		ret = stp_full_recv_t(c, st, 0, out, outmax, olen, 2000);
		if (ret) {
			dev_err(c->dev, "[%s] no HCI event (try %d)\n", name, i);
			return ret;
		}
		dev_info(c->dev, "[%s] HCI (%d B): %*ph\n", name, *olen, min(*olen, 32), out);
		if (*olen >= 3 && out[0] == 0x04 && (out[1] == 0x0e || out[1] == 0x0f))
			return 0;
	}
	return -EBADMSG;
}

/* ---------- hci0: H4 over STP type 0, background receive ---------- */
static bool bt_hci;
module_param(bt_hci, bool, 0444);
MODULE_PARM_DESC(bt_hci, "stage 8: register hci0 (BTIF stays enabled; BT function is switched on at hci open)");

static unsigned int hci_trace;
module_param(hci_trace, uint, 0644);
MODULE_PARM_DESC(hci_trace, "HCI rx/tx tracing: 0 init only, 1 events, 2 everything (ACL, LE reports)");

static char bdaddr_str[18];
module_param_string(bdaddr, bdaddr_str, sizeof(bdaddr_str), 0644);
MODULE_PARM_DESC(bdaddr, "BD_ADDR to program with vendor cmd 0xFC1A at hci setup (XX:XX:XX:XX:XX:XX); empty = keep firmware default");

/* receive telemetry (see hci_trace) */
static int consys_h4_rx(struct hci_dev *hdev, struct sk_buff *skb)
{
	static atomic_t n = ATOMIC_INIT(0);
	int k = atomic_inc_return(&n);
	u8 t = hci_skb_pkt_type(skb);
	/* for diagnostics: the full list of Supported Commands (CC for opcode 0x1002) */
	if (t == HCI_EVENT_PKT && skb->len >= 70 && skb->data[0] == 0x0e && get_unaligned_le16(skb->data + 3) == 0x1002)
		dev_info(hdev->dev.parent, "Supported Commands (octets 0..63): %*ph\n", 64, skb->data + 6);
	/*
	 * hci_trace (on the fly, /sys/module/consys_bringup/parameters/hci_trace): 0 - only the first 150 packets (init);
	 * 1 - all events except LE advertising; 2 - absolutely everything including ACL and LE reports.
	 */
	if (k <= 150 || (hci_trace >= 1 && (t == HCI_EVENT_PKT || t == HCI_SCODATA_PKT) &&
			 !(skb->len > 2 && skb->data[0] == 0x3e)) || hci_trace >= 2)
		dev_info(hdev->dev.parent, "HCI rx #%d type=%u len=%u: %*ph\n", k, t, skb->len, min_t(int, skb->len, 32), skb->data);
	return hci_recv_frame(hdev, skb);
}

static const struct h4_recv_pkt consys_h4_pkts[] = {
	{ H4_RECV_ACL,   .recv = consys_h4_rx },
	{ H4_RECV_SCO,   .recv = consys_h4_rx },
	{ H4_RECV_EVENT, .recv = consys_h4_rx },
	{ H4_RECV_ISO,   .recv = consys_h4_rx },
};

/*
 * A raw channel for experiments without a reboot: echo "01 03 0c 00" | sudo tee /sys/module/consys_bringup/parameters/hci_raw
 * (a whole H4 packet, hex; goes into STP type 0 bypassing the HCI core; the answers are visible in the "HCI rx" log).
 */
static struct consys *g_hci_c;

static int hci_raw_set(const char *val, const struct kernel_param *kp)
{
	u8 buf[256];
	int n = 0, hi = -1;
	const char *s;

	if (!g_hci_c || !g_hci_c->stp || !g_hci_c->rx_task)
		return -ENODEV;	/* hci0 must be open (bluetoothctl power on) */
	for (s = val; *s && n < (int)sizeof(buf); s++) {
		int d = hex_to_bin(*s);

		if (d < 0)
			continue;	/* spaces, commas, 0x do not get in the way (0x prefix: x is skipped, 0 becomes a digit - do not use 0x) */
		if (hi < 0) {
			hi = d;
		} else {
			buf[n++] = (hi << 4) | d;
			hi = -1;
		}
	}
	if (!n)
		return -EINVAL;
	dev_info(g_hci_c->dev, "hci_raw: TX %d bytes: %*ph\n", n, min(n, 32), buf);
	return stp_full_send(g_hci_c, g_hci_c->stp, 0, buf, n);
}

static const struct kernel_param_ops hci_raw_ops = { .set = hci_raw_set };
module_param_cb(hci_raw, &hci_raw_ops, NULL, 0200);
MODULE_PARM_DESC(hci_raw, "write-only: hex H4 packet to send raw over STP type 0 (hci0 must be up)");

/* the same for WMT (STP type 4): echo "01 10 04 00 1a 00 00 00" > wmt_raw ; events - in the "unsolicited type=4" log */
static int hex_to_buf(const char *val, u8 *buf, int max)
{
	int n = 0, hi = -1;

	for (; *val && n < max; val++) {
		int d = hex_to_bin(*val);

		if (d < 0)
			continue;
		if (hi < 0) {
			hi = d;
		} else {
			buf[n++] = (hi << 4) | d;
			hi = -1;
		}
	}
	return n;
}

static int wmt_raw_set(const char *val, const struct kernel_param *kp)
{
	u8 buf[128];
	int n;

	if (!g_hci_c || !g_hci_c->stp || !g_hci_c->rx_task)
		return -ENODEV;
	n = hex_to_buf(val, buf, sizeof(buf));
	if (!n)
		return -EINVAL;
	dev_info(g_hci_c->dev, "wmt_raw: TX %d bytes: %*ph\n", n, min(n, 32), buf);
	return stp_full_send(g_hci_c, g_hci_c->stp, STP_TASK_WMT, buf, n);
}

static const struct kernel_param_ops wmt_raw_ops = { .set = wmt_raw_set };
module_param_cb(wmt_raw, &wmt_raw_ops, NULL, 0200);
MODULE_PARM_DESC(wmt_raw, "write-only: hex WMT command to send raw over STP type 4 (hci0 must be up)");

/* parse the STP frames accumulated in st->rxbuf; BT (type 0) -> H4 -> hci_recv_frame; log the rest */
static void stp_rx_process(struct consys *c)
{
	struct stp_full *st = c->stp;
	u8 *buf = st->rxbuf;
	int pos = 0;

	while (st->rxn - pos >= 4) {
		u8 *h = buf + pos;
		int len, seq, ack, type;

		if ((h[0] & 0xc0) != 0x80 || ((h[0] + h[1] + h[2]) & 0xff) != h[3]) {
			pos++;
			continue;
		}
		seq = (h[0] >> 3) & 7;
		ack = h[0] & 7;
		type = (h[1] >> 4) & 7;
		len = ((h[1] & 0x0f) << 8) | h[2];
		st->rxack = ack;
		if (!len) {
			pos += 4;
			continue;
		}
		if (st->rxn - pos < 4 + len + 2)
			break;
		if ((h[4 + len] | (h[5 + len] << 8)) != crc16(0, h + 4, len)) {
			dev_warn_ratelimited(c->dev, "[STP-FULL] CRC bad, len=%d\n", len);
			{
				/* diagnostics: a full dump of the first two broken frames with the received/expected CRC */
				static int dumped;

				if (dumped < 2) {
					dumped++;
					dev_warn(c->dev, "CRC bad: got 0x%04x want 0x%04x, len=%d type=%d seq=%d ack=%d, rxn=%d pos=%d\n",
						 h[4 + len] | (h[5 + len] << 8), crc16(0, h + 4, len), len, type, seq, ack, st->rxn, pos);
					print_hex_dump(KERN_WARNING, "stp-bad: ", DUMP_PREFIX_OFFSET, 16, 1, h, 4 + len + 2, false);
				}
			}
			pos++;
			continue;
		}
		pos += 4 + len + 2;
		if (seq != st->rxexp) {		/* a repeat: acknowledge, do not deliver */
			stp_full_ack(c, st);
			continue;
		}
		st->txack = seq;
		st->rxexp = (seq + 1) & 7;
		stp_full_ack(c, st);
		if (type == 0 && c->hdev) {
			c->rx_skb = h4_recv_buf(c->hdev, c->rx_skb, h + 4, len, consys_h4_pkts, ARRAY_SIZE(consys_h4_pkts));
			if (IS_ERR(c->rx_skb)) {
				dev_warn_ratelimited(c->dev, "H4 rx error %ld\n", PTR_ERR(c->rx_skb));
				c->rx_skb = NULL;
				c->hdev->stat.err_rx++;
			}
		} else if (type == STP_TASK_WMT && c->wmt_waiting) {
			/* the answer to a synchronous WMT command while the thread owns BTIF */
			c->wmt_evt_len = len;
			memcpy(c->wmt_evt, h + 4, min_t(int, len, (int)sizeof(c->wmt_evt)));
			c->wmt_waiting = false;
			complete(&c->wmt_done);
		} else if (type < 8 && c->type_cb[type]) {
			c->type_cb[type](c->type_ctx[type], h + 4, len);
		} else {
			dev_info(c->dev, "[STP-FULL] unsolicited type=%d len=%d: %*ph\n", type, len, min(len, 24), h + 4);
		}
	}
	if (pos) {
		memmove(buf, buf + pos, st->rxn - pos);
		st->rxn -= pos;
	}
}

/* simple BTIF polling: while empty - sleep rx_poll_us..2x (TODO: BTIF IRQ, SPI 138) */
static unsigned int rx_poll_us = 1000;
module_param(rx_poll_us, uint, 0644);
MODULE_PARM_DESC(rx_poll_us, "BTIF rx polling period when idle (us); lower = less FIFO overrun risk, more CPU");

static int consys_rx_thread(void *data)
{
	struct consys *c = data;
	struct stp_full *st = c->stp;

	while (!kthread_should_stop()) {
		int got;

		if (st->rxn > (int)sizeof(st->rxbuf) - 128)
			st->rxn = 0;		/* garbage/desync: reset */
		if (c->rxdma_on) {
			got = btif_rxdma_read(c, st->rxbuf + st->rxn, sizeof(st->rxbuf) - st->rxn);
		} else if (readl(c->btif + BTIF_LSR) & BTIF_LSR_DR) {
			got = btif_rx(c, st->rxbuf + st->rxn, sizeof(st->rxbuf) - st->rxn, 5, 1000);
		} else {
			got = 0;
		}
		if (got > 0) {
			st->rxn += got;
			stp_rx_process(c);
			continue;		/* while there is data - do not sleep */
		}
		{
			unsigned int us = rx_poll_us ? rx_poll_us : 50;

			usleep_range(us, us * 2);
		}
	}
	return 0;
}

/* ---------- API for other modules (consys-gps.ko): a receive thread with users, WMT commands, STP frames by type ---------- */
#define STP_TASK_GPS	2
static struct consys *g_cs;	/* the only instance, set in probe */

static int consys_rx_get_c(struct consys *c)
{
	int ret = 0;

	mutex_lock(&c->rx_lock);
	if (c->rx_users == 0) {
		if (!c->stp) {
			ret = -ENODEV;
			goto out;
		}
		stp_quiet = true;
		if (rx_dma) {
			int drained = 0, room = (int)sizeof(c->stp->rxbuf) - c->stp->rxn;

			ret = btif_rxdma_start(c, c->stp->rxbuf + c->stp->rxn, &drained, room);
			if (ret) {
				dev_warn(c->dev, "RX DMA unavailable (%d), falling back to PIO polling\n", ret);
				ret = 0;
			} else {
				c->stp->rxn += drained;
			}
		}
		c->rx_task = kthread_run(consys_rx_thread, c, "consys-rx");
		if (IS_ERR(c->rx_task)) {
			ret = PTR_ERR(c->rx_task);
			c->rx_task = NULL;
			stp_quiet = false;
			if (c->rxdma_on)
				btif_rxdma_stop(c);
			goto out;
		}
	}
	c->rx_users++;
out:
	mutex_unlock(&c->rx_lock);
	return ret;
}

static void consys_rx_put_c(struct consys *c)
{
	mutex_lock(&c->rx_lock);
	if (c->rx_users > 0 && --c->rx_users == 0 && c->rx_task) {
		kthread_stop(c->rx_task);
		c->rx_task = NULL;
		if (c->rxdma_on)
			btif_rxdma_stop(c);	/* back to PIO for the synchronous WMT/STP exchanges */
		stp_quiet = false;
	}
	mutex_unlock(&c->rx_lock);
}

/* a WMT command waiting for an event: while the receive thread is not running - read ourselves, otherwise the thread brings the event */
static int consys_wmt_cmd_c(struct consys *c, const u8 *cmd, int clen, const u8 *evt, int elen, unsigned int tmo_ms)
{
	int ret;

	if (!c->stp || !c->powered)
		return -ENODEV;
	if (!c->rx_task)
		return stp_full_cmd(c, c->stp, "WMT", cmd, clen, evt, elen, tmo_ms);
	mutex_lock(&c->wmt_lock);
	reinit_completion(&c->wmt_done);
	c->wmt_evt_len = 0;
	c->wmt_waiting = true;
	ret = stp_full_send(c, c->stp, STP_TASK_WMT, cmd, clen);
	if (!ret && !wait_for_completion_timeout(&c->wmt_done, msecs_to_jiffies(tmo_ms)))
		ret = -ETIMEDOUT;
	c->wmt_waiting = false;
	if (!ret && !(c->wmt_evt_len == elen && !memcmp(c->wmt_evt, evt, elen))) {
		dev_err(c->dev, "WMT cmd %*ph: unexpected event (len %d): %*ph\n", min(clen, 8), cmd, c->wmt_evt_len,
			min(c->wmt_evt_len, 16), c->wmt_evt);
		ret = -EBADMSG;
	}
	mutex_unlock(&c->wmt_lock);
	return ret;
}

/* sending an STP frame respecting the window (no more than 4 unacknowledged); needs a running receive thread (it sends the ACKs) */
static int consys_stp_send_c(struct consys *c, u8 type, const u8 *buf, int len)
{
	struct stp_full *st = c->stp;
	int i;

	if (!st || !c->rx_task)
		return -ENODEV;
	for (i = 0; i < 300 && ((st->txseq - ((st->rxack + 1) & 7)) & 7) >= 4; i++)
		usleep_range(1000, 2000);
	if (i == 300)
		dev_warn_ratelimited(c->dev, "STP tx window stuck (txseq=%u rxack=%u)\n", st->txseq, st->rxack);
	return stp_full_send(c, st, type, buf, len);
}

int consys_rx_get(void)
{
	return g_cs ? consys_rx_get_c(g_cs) : -ENODEV;
}
EXPORT_SYMBOL_GPL(consys_rx_get);

void consys_rx_put(void)
{
	if (g_cs)
		consys_rx_put_c(g_cs);
}
EXPORT_SYMBOL_GPL(consys_rx_put);

int consys_wmt_cmd(const u8 *cmd, int clen, const u8 *evt, int elen, unsigned int tmo_ms)
{
	return g_cs ? consys_wmt_cmd_c(g_cs, cmd, clen, evt, elen, tmo_ms) : -ENODEV;
}
EXPORT_SYMBOL_GPL(consys_wmt_cmd);

int consys_stp_send(u8 type, const u8 *buf, int len)
{
	return g_cs ? consys_stp_send_c(g_cs, type, buf, len) : -ENODEV;
}
EXPORT_SYMBOL_GPL(consys_stp_send);

int consys_stp_register(u8 type, void (*cb)(void *ctx, const u8 *buf, int len), void *ctx)
{
	if (!g_cs || type >= 8 || type == 0 || type == STP_TASK_WMT)
		return -EINVAL;
	if (g_cs->type_cb[type] && cb)
		return -EBUSY;
	g_cs->type_ctx[type] = ctx;
	g_cs->type_cb[type] = cb;
	return 0;
}
EXPORT_SYMBOL_GPL(consys_stp_register);

/*
 * A stable BD_ADDR (like the HAL: vendor cmd 0xFC1A, least significant byte first). The BT firmware resets the address to the default
 * (00:00:46:76:63:01) on EVERY BT off/on, so it is set on every open, not once in hdev->setup:
 * otherwise after a power cycle the air has a different address than BlueZ, and all saved pairing keys stop working.
 * Called synchronously, before the receive thread starts (the thread does not own BTIF yet).
 */
static void consys_apply_bdaddr(struct consys *c)
{
	u8 a[6], cmd[10], out[64];
	int olen = 0, ret, i;

	if (!bdaddr_str[0])
		return;
	if (sscanf(bdaddr_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
		dev_warn(c->dev, "bad bdaddr parameter '%s'\n", bdaddr_str);
		return;
	}
	cmd[0] = 0x01;		/* H4 command */
	cmd[1] = 0x1a;
	cmd[2] = 0xfc;
	cmd[3] = 0x06;
	for (i = 0; i < 6; i++)
		cmd[4 + i] = a[5 - i];
	if (c->rx_task) {
		/* the receive thread is already running (e.g. /dev/stpgps is open): it will take the answer and pass it to the HCI core as an extra CC */
		ret = stp_full_send(c, c->stp, 0, cmd, sizeof(cmd));
		msleep(150);
	} else {
		ret = bt_hci_xfer(c, c->stp, "SET-BDADDR", cmd, sizeof(cmd), out, sizeof(out), &olen);
	}
	dev_info(c->dev, "set BD_ADDR %02x:%02x:%02x:%02x:%02x:%02x: %d\n", a[0], a[1], a[2], a[3], a[4], a[5], ret);
}

static int consys_hci_open(struct hci_dev *hdev)
{
	static const u8 bt_on[] = { 0x01, 0x06, 0x02, 0x00, 0x00, 0x01 };
	static const u8 func_evt[] = { 0x02, 0x06, 0x01, 0x00, 0x00 };
	struct consys *c = hci_get_drvdata(hdev);
	int ret;

	if (!c->stp || !c->powered)
		return -ENODEV;
	ret = consys_wmt_cmd_c(c, bt_on, sizeof(bt_on), func_evt, sizeof(func_evt), 3000);
	if (ret)
		return ret;
	msleep(100);
	consys_apply_bdaddr(c);
	return consys_rx_get_c(c);
}

static int consys_hci_close(struct hci_dev *hdev)
{
	static const u8 bt_off[] = { 0x01, 0x06, 0x02, 0x00, 0x00, 0x00 };
	static const u8 func_evt[] = { 0x02, 0x06, 0x01, 0x00, 0x00 };
	struct consys *c = hci_get_drvdata(hdev);

	/* the receive thread is still running (we hold a reference) - BT off goes synchronously through it */
	if (c->stp && c->powered)
		consys_wmt_cmd_c(c, bt_off, sizeof(bt_off), func_evt, sizeof(func_evt), 3000);
	consys_rx_put_c(c);
	kfree_skb(c->rx_skb);
	c->rx_skb = NULL;
	return 0;
}

static int consys_hci_flush(struct hci_dev *hdev)
{
	return 0;
}

/*
 * Commands the firmware does not know (HCI status 0x01), but the HCI core considers mandatory during init:
 * they are not sent to the MCU, we return Command Complete with status 0 ourselves. The list can be edited on the fly:
 *   echo 0x0c77,0x.... > /sys/module/consys_bringup/parameters/hci_fake ; rfkill block/unblock bluetooth
 */
static unsigned int le_mask_or = 0x0f;	/* events[0]: 0x01 conn complete, 0x02 adv report, 0x04 conn update, 0x08 remote features */
module_param(le_mask_or, uint, 0644);
MODULE_PARM_DESC(le_mask_or, "bits OR-ed into the first byte of LE Set Event Mask (firmware under-reports LE commands)");

static unsigned int hci_fake[16] = { 0x0c77 };	/* 0x0c77 = Read Synchronization Train Parameters */
static int n_hci_fake = 1;
module_param_array(hci_fake, uint, &n_hci_fake, 0644);
MODULE_PARM_DESC(hci_fake, "HCI opcodes answered locally with Command Complete(status 0), not sent to the controller");

static bool consys_hci_fake_cmd(struct hci_dev *hdev, struct sk_buff *skb)
{
	u16 op;
	int i;
	struct sk_buff *ev;
	u8 *p;

	if (hci_skb_pkt_type(skb) != HCI_COMMAND_PKT || skb->len < 3)
		return false;
	op = get_unaligned_le16(skb->data);
	if (hci_trace)
		dev_info(hdev->dev.parent, "HCI cmd opcode 0x%04x plen=%u\n", op, skb->data[2]);
	/*
	 * LE Set Event Mask: the kernel enables Advertising Report/Connection Complete etc. only by the bits in Supported Commands,
	 * while the MediaTek firmware does not declare them (although the commands work) -> without this the LE scan is silent. Add the needed bits to events[0].
	 */
	if (op == 0x2001 && skb->len >= 11 && le_mask_or) {
		dev_info(hdev->dev.parent, "LE event mask 0x%02x -> 0x%02x\n", skb->data[3], skb->data[3] | le_mask_or);
		skb->data[3] |= le_mask_or;
	}
	for (i = 0; i < n_hci_fake; i++)
		if (hci_fake[i] == op)
			break;
	if (i == n_hci_fake)
		return false;
	ev = bt_skb_alloc(2 + 3 + 8, GFP_KERNEL);	/* evt hdr + ncmd,opcode + status + 7 zero parameters */
	if (!ev)
		return false;
	hci_skb_pkt_type(ev) = HCI_EVENT_PKT;
	p = skb_put(ev, 13);
	p[0] = 0x0e;		/* Command Complete */
	p[1] = 11;
	p[2] = 1;		/* ncmd */
	put_unaligned_le16(op, p + 3);
	memset(p + 5, 0, 8);	/* status 0 + zero parameters */
	dev_info(hdev->dev.parent, "HCI opcode 0x%04x answered locally (unsupported by controller)\n", op);
	hci_recv_frame(hdev, ev);
	return true;
}

static int consys_hci_send(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct consys *c = hci_get_drvdata(hdev);
	struct stp_full *st = c->stp;
	u8 *buf;
	int ret, i;

	if (consys_hci_fake_cmd(hdev, skb)) {
		hdev->stat.cmd_tx++;
		kfree_skb(skb);
		return 0;
	}

	if (skb->len + 1 > STP_MAX_PAYLOAD) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	buf = kmalloc(skb->len + 1, GFP_KERNEL);
	if (!buf) {
		kfree_skb(skb);
		return -ENOMEM;
	}
	buf[0] = hci_skb_pkt_type(skb);
	memcpy(buf + 1, skb->data, skb->len);
	/* STP window: no more than 4 unacknowledged frames (retransmit is not implemented) */
	for (i = 0; i < 300 && ((st->txseq - ((st->rxack + 1) & 7)) & 7) >= 4; i++)
		usleep_range(1000, 2000);
	if (i == 300)
		dev_warn_ratelimited(c->dev, "STP tx window stuck (txseq=%u rxack=%u)\n", st->txseq, st->rxack);
	ret = stp_full_send(c, st, 0, buf, skb->len + 1);
	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT: hdev->stat.cmd_tx++; break;
	case HCI_ACLDATA_PKT: hdev->stat.acl_tx++; break;
	case HCI_SCODATA_PKT: hdev->stat.sco_tx++; break;
	}
	kfree(buf);
	kfree_skb(skb);
	return ret;
}

static int consys_hci_register(struct consys *c)
{
	struct hci_dev *hdev = hci_alloc_dev();
	int ret;

	if (!hdev)
		return -ENOMEM;
	hci_set_drvdata(hdev, c);
	hdev->bus = HCI_UART;
	hdev->manufacturer = 70;	/* MediaTek, as in Read Local Version */
	SET_HCIDEV_DEV(hdev, c->dev);
	hdev->open = consys_hci_open;
	hdev->close = consys_hci_close;
	hdev->flush = consys_hci_flush;
	hdev->send = consys_hci_send;
	ret = hci_register_dev(hdev);
	if (ret) {
		hci_free_dev(hdev);
		return ret;
	}
	c->hdev = hdev;
	g_hci_c = c;
	dev_info(c->dev, "registered %s (H4 over STP type 0)\n", hdev->name);
	return 0;
}

static void consys_hci_unregister(struct consys *c)
{
	if (!c->hdev)
		return;
	g_hci_c = NULL;
	hci_unregister_dev(c->hdev);	/* will call close if open */
	hci_free_dev(c->hdev);
	c->hdev = NULL;
}

static void bt_probe(struct consys *c, struct stp_full *st)
{
	static const u8 bt_on[] = { 0x01, 0x06, 0x02, 0x00, 0x00, 0x01 };	/* FUNC_CTRL, WMTDRV_TYPE_BT, on */
	static const u8 bt_off[] = { 0x01, 0x06, 0x02, 0x00, 0x00, 0x00 };
	static const u8 func_evt[] = { 0x02, 0x06, 0x01, 0x00, 0x00 };
	static const u8 hci_reset[] = { 0x01, 0x03, 0x0c, 0x00 };
	static const u8 hci_rver[] = { 0x01, 0x01, 0x10, 0x00 };		/* Read Local Version Information */
	static const u8 hci_raddr[] = { 0x01, 0x09, 0x10, 0x00 };		/* Read BD_ADDR */
	u8 out[64];
	int olen = 0, ret;

	ret = stp_full_cmd(c, st, "FUNC-CTRL(BT on)", bt_on, sizeof(bt_on), func_evt, sizeof(func_evt), 3000);
	dev_info(c->dev, "CPUPCR after BT on: 0x%08x (%d)\n", readl(c->hif_on + HIF_ON_CPUPCR), ret);
	if (ret)
		return;
	msleep(100);
	ret = bt_hci_xfer(c, st, "HCI-RESET", hci_reset, sizeof(hci_reset), out, sizeof(out), &olen);
	if (!ret && olen >= 7)
		dev_info(c->dev, "HCI reset status=0x%02x\n", out[6]);
	ret = bt_hci_xfer(c, st, "HCI-READ-VERSION", hci_rver, sizeof(hci_rver), out, sizeof(out), &olen);
	if (!ret && olen >= 15)
		dev_info(c->dev, "BT: HCI ver=%u rev=0x%04x LMP ver=%u mfr=0x%04x (%u) LMP sub=0x%04x\n", out[7],
			 get_unaligned_le16(out + 8), out[10], get_unaligned_le16(out + 11),
			 get_unaligned_le16(out + 11), get_unaligned_le16(out + 13));
	ret = bt_hci_xfer(c, st, "HCI-READ-BDADDR", hci_raddr, sizeof(hci_raddr), out, sizeof(out), &olen);
	if (!ret && olen >= 13)
		dev_info(c->dev, "BT: BD_ADDR(raw, LE)=%6phC\n", out + 7);
	ret = stp_full_cmd(c, st, "FUNC-CTRL(BT off)", bt_off, sizeof(bt_off), func_evt, sizeof(func_evt), 3000);
	dev_info(c->dev, "BT off: %d\n", ret);
}

/* vendor: _wmt_soc_mode_ctrl(FULL) + SET_CHIP_ID, but WITHOUT the preceding PDA/WMT reset */
static int stp_bringup(struct consys *c, int stage)
{
	static const u8 q_cmd[] = { 0x01, 0x04, 0x01, 0x00, 0x04 };
	static const u8 q_evt_def[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x04, 0x11, 0x00, 0x00, 0x00 };
	static const u8 set_cmd[] = { 0x01, 0x04, 0x05, 0x00, 0x03, 0xdb, 0x0e, 0x68, 0x01 };
	static const u8 set_evt[] = { 0x02, 0x04, 0x02, 0x00, 0x00, 0x03 };
	static const u8 q_evt[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x04, 0xdb, 0x0e, 0x68, 0x01 };
	static const u8 cid_cmd[] = { 0x01, 0x02, 0x05, 0x00, 0x11, 0x85, 0x67, 0x00, 0x00 };	/* chip id 0x6785 */
	static const u8 cid_evt[] = { 0x02, 0x02, 0x01, 0x00, 0x00 };
	struct stp_full st = { 0 };
	int ret;

	ret = wmt_cmd(c, "QUERY-STP", q_cmd, sizeof(q_cmd), q_evt_def, sizeof(q_evt_def), 500);
	if (ret)
		return ret;
	ret = wmt_cmd(c, "SET-STP", set_cmd, sizeof(set_cmd), set_evt, sizeof(set_evt), 500);
	if (ret)
		return ret;
	dev_info(c->dev, "host -> STP FULL mode (seq/ack/CRC16)\n");
	msleep(10);	/* vendor: >=2ms for switching the mechanism in the chip */
	ret = stp_full_cmd(c, &st, "QUERY-STP(full)", q_cmd, sizeof(q_cmd), q_evt, sizeof(q_evt), 1000);
	if (ret)
		return ret;
	ret = stp_full_cmd(c, &st, "SET-CHIP-ID", cid_cmd, sizeof(cid_cmd), cid_evt, sizeof(cid_evt), 1000);
	if (ret || (stage != 7 && stage != 8))
		return ret;

	if (calib) {
		/* the rest of the vendor sw_init affecting WF: QUERY_A_DIE and the RF calibration in the MCU (before any FUNC_CTRL) */
		static const u8 adie_cmd[] = { 0x01, 0x02, 0x01, 0x00, 0x12 };
		static const u8 adie_evt[] = { 0x02, 0x02, 0x05, 0x00, 0x00 };
		static const u8 cal_cmd[] = { 0x01, 0x14, 0x01, 0x00, 0x01 };
		static const u8 cal_evt[] = { 0x02, 0x14, 0x02, 0x00, 0x00, 0x01 };

		if (calib & 1) {
			ret = stp_full_cmd_pfx(c, &st, "QUERY-A-DIE", adie_cmd, sizeof(adie_cmd), adie_evt, sizeof(adie_evt), 1000);
			if (ret)
				dev_warn(c->dev, "QUERY-A-DIE failed (%d), continuing\n", ret);
		}
		if (calib & 2) {
			dev_info(c->dev, "RF calibration start (CPUPCR 0x%08x)\n", readl(c->hif_on + HIF_ON_CPUPCR));
			{
				/* the event `02 14 <len16> 00 01 <calibration data...>`: the length depends on the firmware (ours is 0x1c2) */
				u8 out[64];
				int olen = 0;

				(void)cal_evt;
				ret = stp_full_send(c, &st, STP_TASK_WMT, cal_cmd, sizeof(cal_cmd));
				if (!ret)
					ret = stp_full_recv(c, &st, out, sizeof(out), &olen, 20000);
				if (!ret) {
					dev_info(c->dev, "[RF-CALIBRATION] event (%d B): %*ph\n", olen, min(olen, 24), out);
					if (olen < 6 || out[0] != 0x02 || out[1] != 0x14 || out[4] != 0x00 || out[5] != 0x01)
						ret = -EBADMSG;
				} else {
					dev_err(c->dev, "[RF-CALIBRATION] NO WMT EVENT in 20000 ms\n");
				}
			}
			dev_info(c->dev, "RF calibration done: %d (CPUPCR 0x%08x)\n", ret, readl(c->hif_on + HIF_ON_CPUPCR));
			if (ret)
				dev_warn(c->dev, "RF calibration failed, continuing anyway\n");
		}
	}

	{
		static const u8 wifi_on[] = { 0x01, 0x06, 0x02, 0x00, 0x03, 0x01 };	/* OPCODE_FUNC_CTRL, WMTDRV_TYPE_WIFI, on */
		static const u8 wifi_off[] = { 0x01, 0x06, 0x02, 0x00, 0x03, 0x00 };
		static const u8 func_evt[] = { 0x02, 0x06, 0x01, 0x00, 0x00 };
		int r2;

		ret = stp_full_cmd(c, &st, "FUNC-CTRL(WIFI on)", wifi_on, sizeof(wifi_on), func_evt, sizeof(func_evt), 3000);
		dev_info(c->dev, "CPUPCR after WIFI on: 0x%08x\n", readl(c->hif_on + HIF_ON_CPUPCR));
		if (!ret && stage == 8) {
			dev_info(c->dev, "WIFI function is ON in MCU firmware; leaving it ON for gen4m\n");
			msleep(100);
		} else if (!ret) {
			dev_info(c->dev, "WIFI function is ON in MCU firmware\n");
			msleep(100);
			r2 = stp_full_cmd(c, &st, "FUNC-CTRL(WIFI off)", wifi_off, sizeof(wifi_off), func_evt, sizeof(func_evt), 3000);
			dev_info(c->dev, "WIFI off: %d\n", r2);
		}
	}
	if (!ret && bt_test)
		bt_probe(c, &st);
	if (!ret && stage == 8) {		/* keep the STP state for hci0 */
		kfree(c->stp);
		c->stp = kmemdup(&st, sizeof(st), GFP_KERNEL);
		if (!c->stp)
			ret = -ENOMEM;
	}
	return ret;
}

static bool pda_pre = true;
module_param(pda_pre, bool, 0444);
MODULE_PARM_DESC(pda_pre, "stage>=6: load patch_mcu via PDA (CFG, body over TX DMA, WMT reset) before STP full, like vendor");

/* vendor mtk_wcn_soc_pda_patch_dwn() + init_table_3 (WMT reset): CFG(mand) -> raw header+body -> reset */
static int pda_load(struct consys *c, const u8 *cfg, size_t cfglen, const u8 *body, size_t bsize)
{
	static const u8 pda_evt[] = { 0x02, 0x01, 0x01, 0x00, 0x00 };
	static const u8 reset_cmd[] = { 0x01, 0x07, 0x01, 0x00, 0x04 };
	static const u8 reset_evt[] = { 0x02, 0x07, 0x01, 0x00, 0x00 };
	u8 hdr[12] = { 0 };
	size_t sent;
	int ret;

	ret = wmt_cmd(c, "PDA-CFG", cfg, cfglen, pda_evt, sizeof(pda_evt), 1000);
	if (ret)
		return ret;
	put_unaligned_le32(bsize + sizeof(hdr), hdr);
	btif_wake(c);
	if (pda_dma) {
		ret = btif_dma_start(c);
		if (ret)
			return ret;
	}
	ret = pda_dma ? btif_tx_dma(c, hdr, sizeof(hdr)) : btif_tx(c, hdr, sizeof(hdr));
	for (sent = 0; !ret && sent < bsize; sent += PDA_FRAG_SIZE) {
		size_t n1 = min_t(size_t, PDA_FRAG_SIZE, bsize - sent);

		ret = pda_dma ? btif_tx_dma(c, body + sent, n1) : btif_tx(c, body + sent, n1);
	}
	if (pda_dma)
		btif_dma_stop(c);
	dev_info(c->dev, "PDA body sent (%s): %zu/%zu bytes, ret=%d\n", pda_dma ? "DMA" : "PIO", min(sent, bsize), bsize, ret);
	if (ret)
		return ret;
	msleep(50);
	ret = wmt_cmd(c, "WMT-RESET", reset_cmd, sizeof(reset_cmd), reset_evt, sizeof(reset_evt), 800);
	dev_info(c->dev, "patch_mcu PDA: %s (%d), CPUPCR=0x%08x\n", ret ? "FAILED" : "OK", ret, readl(c->hif_on + HIF_ON_CPUPCR));
	return ret;
}

static int btif_session(struct consys *c, int stage)
{
	static const u8 pda_evt[] = { 0x02, 0x01, 0x01, 0x00, 0x00 };
	static const u8 reset_cmd[] = { 0x01, 0x07, 0x01, 0x00, 0x04 };
	static const u8 reset_evt[] = { 0x02, 0x07, 0x01, 0x00, 0x00 };
	u8 cfg[21] = { 0x01, 0x01, 0x11, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x2f, 0x02, 0x02, 0x00 };
	char name[96], date[PATCH_BUILDTIME_SIZE + 1];
	u8 *buf = NULL, rx[64];
	size_t len = 0, bsize, off, sent;
	u8 *body, pdahdr[12] = { 0 };
	u32 verf, addr;
	int ret, n;

	snprintf(name, sizeof(name), "soc1_0_patch_mcu_%s_hdr.bin", fw_sfx);
	ret = load_fw(c, name, (void **)&buf, &len);
	if (ret)
		return ret;
	if (len <= PATCH_MCU_HDR_SIZE + PATCH_CHECKSUM_SIZE) {
		ret = -EINVAL;
		goto out_fw;
	}
	memcpy(date, buf, PATCH_BUILDTIME_SIZE);
	date[PATCH_BUILDTIME_SIZE] = 0;
	verf = get_unaligned_le32(buf + 24);
	addr = c->pda_addr;	/* a HYPOTHESIS, iterated from outside (pda_addrs) */
	off = PATCH_MCU_HDR_SIZE + PATCH_CHECKSUM_SIZE;
	body = buf + off;
	bsize = len - off;
	dev_info(c->dev, "[patch_mcu] %s: build=%s hdr[24:28]=0x%08x -> PDA addr (candidate)=0x%08x, body=%zu bytes (file %zu)\n",
		 name, date, verf, addr, bsize, len);
	if (c->emi)
		memcpy_toio(c->emi + EMI_PATCH_MCU_BUILDTIME, buf, PATCH_BUILDTIME_SIZE);

	cfg[5] = addr & 0xff;
	cfg[6] = (addr >> 8) & 0xff;
	cfg[7] = (addr >> 16) & 0xff;
	cfg[8] = (addr >> 24) & 0xff;
	cfg[9] = bsize & 0xff;
	cfg[10] = (bsize >> 8) & 0xff;
	cfg[11] = (bsize >> 16) & 0xff;
	cfg[12] = (bsize >> 24) & 0xff;

	ret = btif_init(c);
	if (ret)
		goto out_fw;
	n = 0;
	if (readl(c->btif + BTIF_LSR) & BTIF_LSR_DR) {
		n = btif_rx(c, rx, sizeof(rx), 10, 3000);
		dev_info(c->dev, "BTIF: %d unsolicited byte(s) before first cmd: %*ph\n", n, min(n, 32), rx);
	}

	{
		/* like the vendor wmt_core_hw_check(): chip id / HW ver / FW ver from the MCU registers BEFORE the patch */
		u32 hcr = 0, hvr = 0, fvr = 0;
		int r1 = wmt_reg_read(c, 0x80000008, &hcr);
		int r2 = wmt_reg_read(c, 0x80000000, &hvr);
		int r3 = wmt_reg_read(c, 0x80000004, &fvr);

		dev_info(c->dev, "WMT regs: HCR(chip id)=0x%08x[%d] HVR(hw)=0x%08x[%d] FVR(fw)=0x%08x[%d]\n",
			 hcr, r1, hvr, r2, fvr, r3);
	}
	if (stage >= 6) {
		if (pda_pre) {
			ret = pda_load(c, cfg, sizeof(cfg), body, bsize);
			if (ret) {
				dev_err(c->dev, "patch_mcu PDA failed (%d), aborting\n", ret);
				goto out_btif;
			}
		}
		ret = stp_bringup(c, stage);
		dev_info(c->dev, "STP bring-up (no PDA): %s (%d)\n", ret ? "FAILED" : "OK", ret);
		goto out_btif;
	}
	ret = wmt_cmd(c, "PDA-CFG", cfg, sizeof(cfg), pda_evt, sizeof(pda_evt), 1000);
	if (ret || stage < 5)
		goto out_btif;

	dev_info(c->dev, "--- PDA mode %u ---\n", c->pda_mode);
	if (c->pda_mode != 2) {
		/* stage 5: the raw PDA header (size+12) and the body */
		size_t frag = (c->pda_mode == 1) ? 100 : PDA_FRAG_SIZE;

		pdahdr[0] = (bsize + 12) & 0xff;
		pdahdr[1] = ((bsize + 12) >> 8) & 0xff;
		pdahdr[2] = ((bsize + 12) >> 16) & 0xff;
		pdahdr[3] = ((bsize + 12) >> 24) & 0xff;
		btif_wake(c);
		if (pda_dma) {
			ret = btif_dma_start(c);
			if (ret)
				goto out_btif;
		}
		ret = pda_dma ? btif_tx_dma(c, pdahdr, sizeof(pdahdr)) : btif_tx(c, pdahdr, sizeof(pdahdr));
		for (sent = 0; !ret && sent < bsize; sent += frag) {
			size_t n1 = min_t(size_t, frag, bsize - sent);

			ret = pda_dma ? btif_tx_dma(c, body + sent, n1) : btif_tx(c, body + sent, n1);
			if (c->pda_mode == 1)
				udelay(100);
		}
		if (pda_dma) {
			btif_dma_regs(c, "body-done");
			btif_dma_stop(c);
		}
		dev_info(c->dev, "PDA body sent (%s): %zu/%zu bytes, ret=%d\n", pda_dma ? "DMA" : "PIO",
			 min(sent, bsize), bsize, ret);
		if (ret)
			goto out_btif;
		if (c->pda_mode == 3)
			msleep(1500);
		else
			msleep(50);
	}
	dev_info(c->dev, "CPUPCR before reset cmd: 0x%08x\n", readl(c->hif_on + HIF_ON_CPUPCR));
	ret = wmt_cmd(c, "WMT-RESET", reset_cmd, sizeof(reset_cmd), reset_evt, sizeof(reset_evt), 800);
	dev_info(c->dev, "CPUPCR after reset: 0x%08x\n", readl(c->hif_on + HIF_ON_CPUPCR));
	if (ret == -ETIMEDOUT) {
		/* probe: is the MCU alive and listening? (a repeated CFG must return 02 01 01 00 00) */
		int pr = wmt_cmd(c, "PROBE-CFG", cfg, sizeof(cfg), pda_evt, sizeof(pda_evt), 500);

		dev_info(c->dev, "PROBE-CFG after silent reset: %s (%d)\n", pr ? "NO REPLY" : "MCU REPLIED", pr);
	}

out_btif:
	btif_regs(c, "end");
	if (!(stage == 8 && bt_hci && !ret))	/* for hci0 the BTIF clocks must stay on */
		btif_exit(c);
	btif_dma_free(c);
out_fw:
	vfree(buf);
	return ret;
}

static int consys_power_off(struct consys *c)
{
	u32 v;

	if (!c->powered)
		return 0;

	/* keep the MCU in reset before cutting the power */
	v = readl(c->rgu + RGU_CPU_SW_RST) & 0x00ffffff;
	writel(v | RGU_CPU_SW_RST_BIT | RGU_KEY, c->rgu + RGU_CPU_SW_RST);

	regmap_update_bits(c->pericfg, PERICFG_CCU_CONFIG, PERICFG_CON_PWR_ON_MASK, 0);
	pm_runtime_put_sync(c->dev);			/* CONN domain */
	clk_disable_unprepare(c->ccif4);
	xo_wcn_set(c, false);
	regulator_disable(c->vcn33_2);
	regulator_disable(c->vcn33_1);
	udelay(300);
	pinmux_set(c, false);
	regulator_disable(c->vcn18);
	c->powered = false;
	dev_info(c->dev, "power-off done\n");
	return 0;
}

static bool post_cfg = true;
module_param(post_cfg, bool, 0444);
MODULE_PARM_DESC(post_cfg, "stage>=2: vendor polling_consys_chipid() tail: CONN2EMI slpprot, toppose_restore_done, WPLL (default 1)");

/* the tail of the vendor polling_consys_chipid() (mt6785.c): the remaining registers, without which the WF PLL/EMI access are not set up */
static void consys_post_chipid_cfg(struct consys *c)
{
	void __iomem *p;
	u32 v;

	p = ioremap(0x18004000, 0x1000);	/* CONN_HIF_PDMA: +0x160 bit4 = CONN2EMI HW slpprot control enable */
	if (p) {
		v = readl(p + 0x160);
		writel((v & 0xffffffef) | BIT(4), p + 0x160);
		dev_info(c->dev, "[post-cfg] slpprot 0x18004160: 0x%08x -> 0x%08x\n", v, readl(p + 0x160));
		iounmap(p);
	}
	p = ioremap(0x180c1000, 0x1000);	/* toppose_restore_done rollback: 0x180c1600 bit23 */
	if (p) {
		v = readl(p + 0x600);
		writel((v & 0xff7fffff) | BIT(23), p + 0x600);
		dev_info(c->dev, "[post-cfg] toppose 0x180c1600: 0x%08x -> 0x%08x\n", v, readl(p + 0x600));
		iounmap(p);
	}
	p = ioremap(0x180b3000, 0x1000);	/* WPLL: 0x180b3034 bits[20:19] = 01 */
	if (p) {
		v = readl(p + 0x34);
		writel((v & 0xffe7ffff) | 0x00080000, p + 0x34);
		dev_info(c->dev, "[post-cfg] WPLL 0x180b3034: 0x%08x -> 0x%08x\n", v, readl(p + 0x34));
		iounmap(p);
	}
}

static int consys_power_on(struct consys *c, int stage)
{
	u32 val, id = 0;
	int ret, retry;

	/* mtk_wcn_consys_hw_pwr_on(): the EMI base must be known */
	if (!c->emi_base) {
		dev_err(c->dev, "EMI base invalid, refusing to power on\n");
		return -EINVAL;
	}

	if (stage >= 2) {
		if (!c->emi) {
			dev_err(c->dev, "EMI not mapped\n");
			return -ENOMEM;
		}
		emi_prepare(c);
	}

	/* vcn18 (1.8V) */
	ret = regulator_enable(c->vcn18);
	if (ret)
		return dev_err_probe(c->dev, ret, "vcn18 enable\n");
	/* vcn33_1 (wifi, 3.3V) */
	regulator_set_voltage(c->vcn33_1, 3300000, 3300000);
	ret = regulator_enable(c->vcn33_1);
	if (ret) {
		dev_err(c->dev, "vcn33_1_wifi enable: %d\n", ret);
		goto err_vcn18;
	}

	pinmux_set(c, true);
	udelay(150);

	/* co-clock: XO_WCN on (WMT_SOC.cfg: co_clock_flag=1) */
	xo_wcn_set(c, true);

	/* efuse / co-clock: vcn33_2 (2.8V) */
	regulator_set_voltage(c->vcn33_2, 2800000, 2800000);
	ret = regulator_enable(c->vcn33_2);
	if (ret) {
		dev_err(c->dev, "vcn33_2_wifi enable: %d\n", ret);
		goto err_xo;
	}

	/* consys_hw_reset_bit_set(ENABLE): hold the MCU in reset */
	val = readl(c->rgu + RGU_CPU_SW_RST) & 0x00ffffff;
	writel(val | RGU_CPU_SW_RST_BIT | RGU_KEY, c->rgu + RGU_CPU_SW_RST);

	/* consys_hw_power_ctrl(ENABLE), branch CONSYS_PWR_ON_OFF_API_AVAILABLE */
	ret = clk_prepare_enable(c->ccif4);
	if (ret) {
		dev_err(c->dev, "ccif4 clk: %d\n", ret);
		goto err_vcn33_2;
	}
	ret = pm_runtime_resume_and_get(c->dev);	/* genpd CONN: power + bus-protect */
	if (ret) {
		dev_err(c->dev, "power domain CONN on: %d -> NOT touching CONSYS regs\n", ret);
		goto err_clk;
	}
	regmap_update_bits(c->pericfg, PERICFG_CCU_CONFIG, PERICFG_CON_PWR_ON_MASK, 1);
	udelay(10);
	c->powered = true;

	consys_snapshot(c, "after-power");

	/* polling_consys_chipid() */
	for (retry = 10; retry > 0; retry--) {
		id = readl(c->top_misc_off + CONSYS_IP_VER_OFFSET);
		if (id == CONSYS_IP_VER_ID)
			break;
		dev_warn(c->dev, "CONSYS version id=0x%08x (retry %d)\n", id, retry);
		msleep(20);
	}
	dev_info(c->dev, "RESULT: CONSYS IP version id=0x%08x (expected 0x%08x) -> %s\n",
		 id, CONSYS_IP_VER_ID, id == CONSYS_IP_VER_ID ? "OK" : "MISMATCH");
	dev_info(c->dev, "HW id=0x%x FW id=0x%x CONF id=0x%x\n",
		 readl(c->mcu + CONSYS_HW_ID_OFFSET) & 0xffff,
		 readl(c->mcu + CONSYS_FW_ID_OFFSET) & 0xffff,
		 readl(c->top_misc_off + CONSYS_CONF_ID_OFFSET) & 0xf);
	if (id != CONSYS_IP_VER_ID)
		return -ENODEV;
	if (stage >= 2) {
		ret = rom_patch_download(c);
		if (ret) {
			dev_err(c->dev, "ROM patch download failed: %d (MCU stays in reset)\n", ret);
			return ret;
		}
		if (post_cfg)
			consys_post_chipid_cfg(c);
	}
	if (stage >= 3) {
		ret = mcu_start(c);
		if (ret)
			return ret;
	}
	if (stage >= 4)
		return btif_session(c, stage);
	return 0;

err_clk:
	clk_disable_unprepare(c->ccif4);
err_vcn33_2:
	regulator_disable(c->vcn33_2);
err_xo:
	xo_wcn_set(c, false);
	regulator_disable(c->vcn33_1);
err_vcn18:
	pinmux_set(c, false);
	regulator_disable(c->vcn18);
	return ret;
}


/* ======================= WMT shim for gen4m (wlan_mt6632_axi.ko) =======================
 * Instead of the vendor WMT stack: gen4m registers the wlan callbacks; when CONSYS is up (stage=8),
 * the shim calls wlan_probe_cb() (in a workqueue, since the registration comes from the platform probe).
 */
#include <linux/export.h>
#include <linux/sched.h>

/* in the vendor stack these variables are defined by WMT; gen4m (CFG_MTK_ANDROID_EMI) only reads them */
phys_addr_t gConEmiPhyBase;
EXPORT_SYMBOL(gConEmiPhyBase);
unsigned long long gConEmiSize;
EXPORT_SYMBOL(gConEmiSize);

static struct consys *g_consys;
static MTK_WCN_WMT_WLAN_CB_INFO g_wlan_cb;
static bool g_wlan_cb_set, g_consys_up, g_probed;
static PF_WMT_CB g_msgcb[WMTDRV_TYPE_MAX];
static DEFINE_MUTEX(g_shim_lock);

wmt_wlan_probe_cb mtk_wcn_wlan_probe;
wmt_wlan_remove_cb mtk_wcn_wlan_remove;
wmt_wlan_bus_cnt_get_cb mtk_wcn_wlan_bus_tx_cnt;
wmt_wlan_bus_cnt_clr_cb mtk_wcn_wlan_bus_tx_cnt_clr;
wmt_wlan_emi_mpu_set_protection_cb mtk_wcn_wlan_emi_mpu_set_protection;
wmt_wlan_is_wifi_drv_own_cb mtk_wcn_wlan_is_wifi_drv_own;

static void wmt_shim_probe_fn(struct work_struct *w)
{
	int ret;

	mutex_lock(&g_shim_lock);
	if (!g_consys_up || !g_wlan_cb_set || g_probed || !g_wlan_cb.wlan_probe_cb) {
		mutex_unlock(&g_shim_lock);
		return;
	}
	g_probed = true;
	mutex_unlock(&g_shim_lock);
	pr_info("consys-shim: calling gen4m wlan_probe_cb()\n");
	ret = g_wlan_cb.wlan_probe_cb();
	pr_info("consys-shim: wlan_probe_cb() returned %d\n", ret);
	if (ret)
		g_probed = false;
}
static DECLARE_WORK(g_probe_work, wmt_shim_probe_fn);

static void wmt_shim_up(struct consys *c)
{
	g_consys = c;
	gConEmiPhyBase = c->emi_base;
	gConEmiSize = c->emi_size;
	mutex_lock(&g_shim_lock);
	g_consys_up = true;
	mutex_unlock(&g_shim_lock);
	if (g_wlan_cb_set)
		schedule_work(&g_probe_work);
}

static void wmt_shim_down(void)
{
	cancel_work_sync(&g_probe_work);
	mutex_lock(&g_shim_lock);
	g_consys_up = false;
	mutex_unlock(&g_shim_lock);
	if (g_probed && g_wlan_cb.wlan_remove_cb)
		g_wlan_cb.wlan_remove_cb();
	g_probed = false;
	g_consys = NULL;
}

INT32 mtk_wcn_wmt_wlan_reg(P_MTK_WCN_WMT_WLAN_CB_INFO info)
{
	if (!info)
		return -EINVAL;
	mutex_lock(&g_shim_lock);
	g_wlan_cb = *info;
	g_wlan_cb_set = true;
	mtk_wcn_wlan_probe = info->wlan_probe_cb;
	mtk_wcn_wlan_remove = info->wlan_remove_cb;
	mtk_wcn_wlan_bus_tx_cnt = info->wlan_bus_cnt_get_cb;
	mtk_wcn_wlan_bus_tx_cnt_clr = info->wlan_bus_cnt_clr_cb;
	mtk_wcn_wlan_emi_mpu_set_protection = info->wlan_emi_mpu_set_protection_cb;
	mtk_wcn_wlan_is_wifi_drv_own = info->wlan_is_wifi_drv_own_cb;
	mutex_unlock(&g_shim_lock);
	pr_info("consys-shim: gen4m registered wlan callbacks (consys_up=%d)\n", g_consys_up);
	if (g_consys_up)
		schedule_work(&g_probe_work);
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_wlan_reg);

INT32 mtk_wcn_wmt_wlan_unreg(VOID)
{
	mutex_lock(&g_shim_lock);
	g_wlan_cb_set = false;
	g_probed = false;	/* gen4m unloaded: the next insmod of wlan must get wlan_probe_cb again */
	memset(&g_wlan_cb, 0, sizeof(g_wlan_cb));
	mutex_unlock(&g_shim_lock);
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_wlan_unreg);

INT32 mtk_wcn_wmt_msgcb_reg(ENUM_WMTDRV_TYPE_T type, PF_WMT_CB cb)
{
	if (type >= WMTDRV_TYPE_MAX)
		return -EINVAL;
	g_msgcb[type] = cb;
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_reg);

INT32 mtk_wcn_wmt_msgcb_unreg(ENUM_WMTDRV_TYPE_T type)
{
	if (type >= WMTDRV_TYPE_MAX)
		return -EINVAL;
	g_msgcb[type] = NULL;
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_unreg);

UINT32 mtk_wcn_wmt_ic_info_get(ENUM_WMT_CHIPINFO_TYPE_T type)
{
	switch (type) {
	case WMTCHIN_CHIPID:
		return 0x6785;
	case WMTCHIN_HWVER:
	case WMTCHIN_MAPPINGHWVER:
	case WMTCHIN_FWVER:
		return 0x8a00;		/* HVR/FVR, read from the MCU */
	case WMTCHIN_IPVER:
		return CONSYS_IP_VER_ID;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(mtk_wcn_wmt_ic_info_get);

MTK_WCN_BOOL mtk_wcn_wmt_assert_timeout(ENUM_WMTDRV_TYPE_T type, UINT32 reason, INT32 timeout)
{
	pr_err("consys-shim: assert requested by type %d reason 0x%x (ignored)\n", type, reason);
	return MTK_WCN_BOOL_FALSE;
}
EXPORT_SYMBOL(mtk_wcn_wmt_assert_timeout);

MTK_WCN_BOOL mtk_wcn_wmt_assert_keyword(ENUM_WMTDRV_TYPE_T type, PUINT8 keyword)
{
	pr_err("consys-shim: assert keyword '%s' type %d (ignored)\n", keyword ? (char *)keyword : "?", type);
	return MTK_WCN_BOOL_FALSE;
}
EXPORT_SYMBOL(mtk_wcn_wmt_assert_keyword);

MTK_WCN_BOOL mtk_wcn_wmt_func_on(ENUM_WMTDRV_TYPE_T type) { return MTK_WCN_BOOL_TRUE; }
EXPORT_SYMBOL(mtk_wcn_wmt_func_on);
MTK_WCN_BOOL mtk_wcn_wmt_func_off(ENUM_WMTDRV_TYPE_T type) { return MTK_WCN_BOOL_TRUE; }
EXPORT_SYMBOL(mtk_wcn_wmt_func_off);
int mtk_wcn_wmt_mpu_lock_aquire(void) { return 0; }
EXPORT_SYMBOL(mtk_wcn_wmt_mpu_lock_aquire);
void mtk_wcn_wmt_mpu_lock_release(void) { }
EXPORT_SYMBOL(mtk_wcn_wmt_mpu_lock_release);
int mtk_wcn_stp_coredump_start_get(void) { return 0; }
EXPORT_SYMBOL(mtk_wcn_stp_coredump_start_get);
void mtk_wcn_set_connsys_power_off_flag(MTK_WCN_BOOL enable) { }
EXPORT_SYMBOL(mtk_wcn_set_connsys_power_off_flag);
/* gen4m takes from the shim the physical address and size of the CONSYS EMI reserve (part of the WiFi firmware lives there) */
int consys_shim_get_emi(phys_addr_t *base, unsigned long long *size)
{
	if (!g_consys || !g_consys->emi_base)
		return -ENODEV;
	*base = g_consys->emi_base;
	*size = g_consys->emi_size;
	return 0;
}
EXPORT_SYMBOL(consys_shim_get_emi);
int mtk_wcn_consys_hw_wifi_paldo_ctrl(unsigned int enable) { return 0; }
EXPORT_SYMBOL(mtk_wcn_consys_hw_wifi_paldo_ctrl);
int mtk_wcn_wmt_wifi_fem_cfg_report(void *info) { return 0; }
EXPORT_SYMBOL(mtk_wcn_wmt_wifi_fem_cfg_report);

/* Android adaptor hooks (vendor connectivity adaptor) — stubs for gen4m without Android */
void connectivity_export_show_stack(struct task_struct *tsk, unsigned long *sp) { }
EXPORT_SYMBOL(connectivity_export_show_stack);
unsigned int get_low_latency_mode(void) { return 0; }
EXPORT_SYMBOL(get_low_latency_mode);
void register_file_buf_handler(void *handler, void *ctx, unsigned char type) { }
EXPORT_SYMBOL(register_file_buf_handler);
void register_set_p2p_mode_handler(void *handler) { }
EXPORT_SYMBOL(register_set_p2p_mode_handler);
void update_driver_loaded_status(unsigned char loaded) { pr_info("consys-shim: wifi driver loaded status=%u\n", loaded); }
EXPORT_SYMBOL(update_driver_loaded_status);
int wifi_reset_start(void) { return 0; }
EXPORT_SYMBOL(wifi_reset_start);
int wifi_reset_end(int status) { return 0; }
EXPORT_SYMBOL(wifi_reset_end);
/* wext events (needed only by the gen4m P2P code); cfg80211 on this kernel is without CONFIG_CFG80211_WEXT */
void wireless_send_event(struct net_device *dev, unsigned int cmd, void *wrqu, const char *extra) { }
EXPORT_SYMBOL(wireless_send_event);

static int consys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *n;
	struct platform_device *pwrap_pdev;
	struct reserved_mem *rmem;
	struct resource *res;
	struct consys *c;
	int ret;

	c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	mutex_init(&c->txlock);
	mutex_init(&c->rx_lock);
	mutex_init(&c->wmt_lock);
	init_completion(&c->wmt_done);
	g_cs = c;
	c->dev = dev;
	platform_set_drvdata(pdev, c);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mcu_cfg");
	if (!res)
		return -EINVAL;
	c->mcu = devm_ioremap(dev, res->start, resource_size(res));
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "top_misc_off");
	if (!res)
		return -EINVAL;
	c->top_misc_off = devm_ioremap(dev, res->start, resource_size(res));
	/* regions belonging to other drivers: ioremap without request_mem_region */
	c->rgu = devm_ioremap(dev, RGU_BASE, 0x100);
	c->pwrap = devm_ioremap(dev, PWRAP_BASE, 0x1000);
	c->pinmux = devm_ioremap(dev, PINMUX_BASE, 0x1000);
	c->pindrv = devm_ioremap(dev, PINDRV_BASE, 0x100);
	c->hif_on = devm_ioremap(dev, HIF_ON_BASE, 0x1000);
	c->btif = devm_ioremap(dev, BTIF_BASE, 0x1000);
	c->txdma = devm_ioremap(dev, APDMA_TX_BASE, 0x80);
	c->rxdma = devm_ioremap(dev, APDMA_RX_BASE, 0x80);
	if (!c->mcu || !c->top_misc_off || !c->rgu || !c->pwrap || !c->pinmux || !c->pindrv || !c->hif_on || !c->btif ||
	    !c->txdma || !c->rxdma)
		return -ENOMEM;

	c->pericfg = syscon_regmap_lookup_by_phandle(np, "mediatek,pericfg");
	if (IS_ERR(c->pericfg))
		return PTR_ERR(c->pericfg);
	/* infracfg_ao: DT has it as the syscon "mediatek,mt6785-infracfg"; a separate property is not needed -> no DT reflash */
	c->infracfg = syscon_regmap_lookup_by_compatible("mediatek,mt6785-infracfg");
	if (IS_ERR(c->infracfg))
		return PTR_ERR(c->infracfg);

	/* PMIC regmap == the regmap of the pwrap device (mt6397-core does it this way) */
	n = of_parse_phandle(np, "mediatek,pwrap", 0);
	if (!n)
		return -EINVAL;
	pwrap_pdev = of_find_device_by_node(n);
	of_node_put(n);
	if (!pwrap_pdev)
		return -EPROBE_DEFER;
	c->pmic = dev_get_regmap(&pwrap_pdev->dev, NULL);
	put_device(&pwrap_pdev->dev);
	if (!c->pmic)
		return -EPROBE_DEFER;

	c->ccif4 = devm_clk_get(dev, "ccif4");
	if (IS_ERR(c->ccif4))
		return dev_err_probe(dev, PTR_ERR(c->ccif4), "clk ccif4\n");
	c->vcn18 = devm_regulator_get(dev, "vcn18");
	c->vcn33_1 = devm_regulator_get(dev, "vcn33_1_wifi");
	c->vcn33_2 = devm_regulator_get(dev, "vcn33_2_wifi");
	if (IS_ERR(c->vcn18) || IS_ERR(c->vcn33_1) || IS_ERR(c->vcn33_2))
		return dev_err_probe(dev, -EPROBE_DEFER, "regulators\n");

	n = of_parse_phandle(np, "memory-region", 0);
	if (n) {
		rmem = of_reserved_mem_lookup(n);
		of_node_put(n);
		if (rmem) {
			c->emi_base = rmem->base;
			c->emi_size = rmem->size;
		}
	}

	if (c->emi_base && c->emi_size)
		c->emi = devm_ioremap(dev, c->emi_base, c->emi_size);

	/* genpd (power-domains in DT) is already attached by the platform bus */
	pm_runtime_enable(dev);

	if (restore_xo) {
		dev_warn(dev, "restore_xo: DCXO_ENABLE.CONN <- 0x%x, CW00(EXTBUF2) <- 0x%x\n", dcxo_en_orig, cw00_orig);
		xo_wcn_set(c, false);
	}

	consys_snapshot(c, "probe");

	if (stage >= 1) {
		dev_warn(dev, "stage=%d: powering CONSYS on (experimental!)\n", stage);
		if (stage == 5) {
			int i;

			for (i = 0; i < n_pda; i++) {
				c->pda_addr = pda_addrs[i];
				c->pda_mode = pda_modes[i];
				dev_info(dev, "=== PDA attempt %d/%d: addr=0x%08x mode=%u ===\n", i + 1, n_pda, c->pda_addr, c->pda_mode);
				ret = consys_power_on(c, stage);
				if (!ret) {
					dev_info(dev, "=== PDA attempt %d SUCCEEDED: addr=0x%08x ===\n", i + 1, c->pda_addr);
					break;
				}
				consys_power_off(c);	/* a clean start for the next attempt */
				msleep(200);
			}
			if (!keep_on || ret)
				consys_power_off(c);
		} else {
			c->pda_addr = pda_addrs[0];
			ret = consys_power_on(c, stage);
			if (stage == 8 && !ret) {
				dev_info(dev, "stage=8: CONSYS stays ON, handing over to gen4m (wlan_probe_cb)\n");
				wmt_shim_up(c);
				if (bt_hci) {
					int r = consys_hci_register(c);

					if (r)
						dev_err(dev, "hci register failed: %d\n", r);
				}
			} else if (!keep_on) {
				consys_power_off(c);
			}
		}
		/* do not fail the probe on an error: so that dmesg can be read and rmmod done */
		if (ret)
			dev_err(dev, "power-on sequence failed: %d\n", ret);
	}
	return 0;
}

static void consys_remove(struct platform_device *pdev)
{
	struct consys *c = platform_get_drvdata(pdev);

	g_cs = NULL;
	consys_hci_unregister(c);
	wmt_shim_down();
	btif_exit(c);
	kfree(c->stp);
	c->stp = NULL;
	consys_power_off(c);
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id consys_of_match[] = {
	{ .compatible = "xiaomi,begonia-consys-bringup" },
	{ }
};
MODULE_DEVICE_TABLE(of, consys_of_match);

static struct platform_driver consys_driver = {
	.probe = consys_probe,
	.remove = consys_remove,
	.driver = {
		.name = "consys-bringup",
		.of_match_table = consys_of_match,
	},
};
module_platform_driver(consys_driver);

MODULE_DESCRIPTION("MT6785 CONNSYS bring-up experiment (begonia)");
MODULE_LICENSE("GPL");
