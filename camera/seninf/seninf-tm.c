// SPDX-License-Identifier: GPL-2.0
/*
 * seninf-tm: M2 step 1 - MT6785 SENINF test model (Redmi Note 8 Pro / begonia), without a sensor and without DMA.
 *
 * The MT6785 SENINF = SENINF phy_2_0 of the upstream MediaTek ISP7 series (block layout checked against the HAL
 * libcam.halsensor.so: SeninfDrvImp::init/setTestModel). Difference: the TG block (generator) is at +0x600, not +0xf00.
 *
 * Window 0x1a040000 (SENINF):
 *   seninfN ctrl  = +0x200 + 0x1000*N   (SENINF_CTRL 0x0, SENINF_TESTMDL_CTRL 0x20)
 *   seninfN tg    = +0x600 + 0x1000*N   (TM_CTL 0x8, TM_SIZE 0xc, TM_CLK 0x10, TM_DUM 0x18)
 *   seninfN csi2  = +0xa00 + 0x1000*N
 *   muxN          = +0xd00 + 0x1000*N   (CTRL_0 0x0, CTRL_1 0x4, IRQ_STATUS 0x18, IMG_SIZE 0x20, SIZE 0x30,
 *                                        PIX_CNT 0xa4, FRAME_SIZE_MON_CTRL 0xa8, H_VALID 0xb0, V_VALID 0xb8)
 *   cam_mux       = +0x400             (EN 0x10, CHK_EN 0x18, IRQ_STATUS 0xa8, CAM_MUX0_CHK_RES 0x108)
 *
 * debugfs /sys/kernel/debug/seninf-tm/: power (1/0), start (any write), dump (read).
 * HAL setTestModel writes: cam_mux+0x10 |= 0x1f; mux+0x4 = 0x001f0001 | pixmode<<8; mux+0x0 = 1;
 * ctrl+0x20 = 1; ctrl+0x0 = 1; tg+0xc = 0x10001f00 (4096x7936); tg+0x10 = 7; tg+0x8 = 0x00300161.
 */
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>

#define SENINF_CTRL(n)	(0x200 + 0x1000 * (n))
#define SENINF_TG(n)	(0x600 + 0x1000 * (n))
#define SENINF_CSI2(n)	(0xa00 + 0x1000 * (n))
#define SENINF_MUX(n)	(0xd00 + 0x1000 * (n))
#define SENINF_CAMMUX	0x400
#define SENINF_TOP	0x000

static unsigned int tm_size = 0x10001f00;	/* HAL: 4096 lines x 7936 pixels */
module_param(tm_size, uint, 0644);
static unsigned int tm_ctl = 0x00300161;	/* HAL: EN + pattern */
module_param(tm_ctl, uint, 0644);
static unsigned int tm_clk = 7;
module_param(tm_clk, uint, 0644);

static const char *const clk_names[] = {
	"cam-main", "cam-gals", "cam-tg", "cam-seninf", "top-seninf0", "top-seninf1", "top-seninf2",
};

struct stm {
	struct device *dev;
	void __iomem *base;
	struct clk_bulk_data clks[ARRAY_SIZE(clk_names)];
	struct device *pd;
	bool on;
	struct mutex lock;
	struct dentry *dbg;
};

static u32 rd(struct stm *s, u32 off) { return readl(s->base + off); }
static void wr(struct stm *s, u32 off, u32 v) { writel(v, s->base + off); }

static int stm_power(struct stm *s, bool on)
{
	int ret;

	if (on == s->on)
		return 0;
	if (on) {
		s->pd = dev_pm_domain_attach_by_name(s->dev, "cam");
		if (IS_ERR_OR_NULL(s->pd)) {
			ret = s->pd ? PTR_ERR(s->pd) : -ENODEV;
			s->pd = NULL;
			dev_err(s->dev, "attaching the cam domain: %d\n", ret);
			return ret;
		}
		ret = pm_runtime_resume_and_get(s->pd);
		dev_info(s->dev, "CAM domain: %d\n", ret);
		if (ret < 0)
			goto detach;
		ret = clk_bulk_prepare_enable(ARRAY_SIZE(clk_names), s->clks);
		if (ret) {
			dev_err(s->dev, "clocks: %d\n", ret);
			goto put;
		}
		s->on = true;
		dev_info(s->dev, "power on, SENINF_TOP_CTRL=0x%08x, top-seninf0 %lu Hz\n",
			 rd(s, SENINF_TOP), clk_get_rate(s->clks[4].clk));
		return 0;
	}
	clk_bulk_disable_unprepare(ARRAY_SIZE(clk_names), s->clks);
	ret = 0;
put:
	pm_runtime_put_sync(s->pd);
detach:
	dev_pm_domain_detach(s->pd, true);
	s->pd = NULL;
	s->on = false;
	return ret;
}

/* the vendor test model sequence for SENINF0 -> MUX0 -> CAM_MUX0, pixel mode 0 */
static void stm_start(struct stm *s)
{
	wr(s, SENINF_CAMMUX + 0x10, rd(s, SENINF_CAMMUX + 0x10) | 0x1f);	/* CAM_MUX_EN */
	wr(s, SENINF_CAMMUX + 0x18, rd(s, SENINF_CAMMUX + 0x18) | 0x1);	/* CAM_MUX_CHK_EN, mux0 */
	wr(s, SENINF_MUX(0) + 0x4, 0x001f0001);					/* MUX_CTRL_1 */
	wr(s, SENINF_MUX(0) + 0x0, 0x1);					/* MUX_CTRL_0: EN */
	wr(s, SENINF_MUX(0) + 0xa8, 0x1);					/* FRAME_SIZE_MON_CTRL */
	wr(s, SENINF_CTRL(0) + 0x20, 0x1);					/* TESTMDL_CTRL */
	wr(s, SENINF_CTRL(0) + 0x0, 0x1);					/* SENINF_CTRL: EN */
	wr(s, SENINF_TG(0) + 0xc, tm_size);					/* TM_SIZE */
	wr(s, SENINF_TG(0) + 0x10, tm_clk);					/* TM_CLK */
	wr(s, SENINF_TG(0) + 0x8, tm_ctl);					/* TM_CTL */
	dev_info(s->dev, "test model started: size 0x%08x clk %u ctl 0x%08x\n", tm_size, tm_clk, tm_ctl);
}

static int dump_show(struct seq_file *m, void *v)
{
	struct stm *s = m->private;
	int i;

	mutex_lock(&s->lock);
	if (!s->on) {
		seq_puts(m, "power is off (echo 1 > power)\n");
		goto out;
	}
	seq_printf(m, "TOP: ctrl 0x%08x mux0 0x%08x mux1 0x%08x\n",
		   rd(s, SENINF_TOP), rd(s, SENINF_TOP + 0x10), rd(s, SENINF_TOP + 0x14));
	seq_printf(m, "SENINF0: ctrl 0x%08x testmdl 0x%08x | TG: ctl 0x%08x size 0x%08x clk 0x%08x dum 0x%08x\n",
		   rd(s, SENINF_CTRL(0)), rd(s, SENINF_CTRL(0) + 0x20),
		   rd(s, SENINF_TG(0) + 0x8), rd(s, SENINF_TG(0) + 0xc), rd(s, SENINF_TG(0) + 0x10),
		   rd(s, SENINF_TG(0) + 0x18));
	for (i = 0; i < 2; i++) {
		u32 mx = SENINF_MUX(0);

		seq_printf(m, "MUX0 #%d: ctrl0 0x%08x ctrl1 0x%08x irq 0x%08x img_size 0x%08x size 0x%08x "
			   "err_size 0x%08x fifo 0x%08x pix_cnt 0x%08x mon H %u V %u (blank %u/%u)\n", i,
			   rd(s, mx), rd(s, mx + 0x4), rd(s, mx + 0x18), rd(s, mx + 0x20), rd(s, mx + 0x30),
			   rd(s, mx + 0x34), rd(s, mx + 0x40), rd(s, mx + 0xa4), rd(s, mx + 0xb0),
			   rd(s, mx + 0xb8), rd(s, mx + 0xb4), rd(s, mx + 0xbc));
		msleep(100);
	}
	seq_printf(m, "CAM_MUX: ctrl0 0x%08x en 0x%08x chk_en 0x%08x irq 0x%08x | mux0 chk_res 0x%08x err 0x%08x\n",
		   rd(s, SENINF_CAMMUX), rd(s, SENINF_CAMMUX + 0x10), rd(s, SENINF_CAMMUX + 0x18),
		   rd(s, SENINF_CAMMUX + 0xa8), rd(s, SENINF_CAMMUX + 0x108), rd(s, SENINF_CAMMUX + 0x10c));
out:
	mutex_unlock(&s->lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dump);

static ssize_t power_write(struct file *f, const char __user *ub, size_t len, loff_t *off)
{
	struct stm *s = file_inode(f)->i_private;
	bool on;
	int ret = kstrtobool_from_user(ub, len, &on);

	if (ret)
		return ret;
	mutex_lock(&s->lock);
	ret = stm_power(s, on);
	mutex_unlock(&s->lock);
	return ret ? ret : len;
}

static ssize_t start_write(struct file *f, const char __user *ub, size_t len, loff_t *off)
{
	struct stm *s = file_inode(f)->i_private;
	int ret = 0;

	mutex_lock(&s->lock);
	if (s->on)
		stm_start(s);
	else
		ret = -EIO;
	mutex_unlock(&s->lock);
	return ret ? ret : len;
}

static const struct file_operations power_fops = {
	.owner = THIS_MODULE, .open = simple_open, .write = power_write, .llseek = noop_llseek,
};
static const struct file_operations start_fops = {
	.owner = THIS_MODULE, .open = simple_open, .write = start_write, .llseek = noop_llseek,
};

static int stm_probe(struct platform_device *pdev)
{
	struct stm *s;
	int i, ret;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->dev = &pdev->dev;
	mutex_init(&s->lock);
	s->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(s->base))
		return PTR_ERR(s->base);
	for (i = 0; i < ARRAY_SIZE(clk_names); i++)
		s->clks[i].id = clk_names[i];
	ret = devm_clk_bulk_get(s->dev, ARRAY_SIZE(clk_names), s->clks);
	if (ret)
		return dev_err_probe(s->dev, ret, "clocks\n");
	platform_set_drvdata(pdev, s);
	s->dbg = debugfs_create_dir("seninf-tm", NULL);
	debugfs_create_file("power", 0200, s->dbg, s, &power_fops);
	debugfs_create_file("start", 0200, s->dbg, s, &start_fops);
	debugfs_create_file("dump", 0444, s->dbg, s, &dump_fops);
	dev_info(s->dev, "ready: echo 1 > /sys/kernel/debug/seninf-tm/power; echo > start; cat dump\n");
	return 0;
}

static void stm_remove(struct platform_device *pdev)
{
	struct stm *s = platform_get_drvdata(pdev);

	debugfs_remove_recursive(s->dbg);
	mutex_lock(&s->lock);
	if (s->on) {
		wr(s, SENINF_TG(0) + 0x8, 0);
		wr(s, SENINF_CTRL(0), 0);
		wr(s, SENINF_MUX(0), 0);
		stm_power(s, false);
	}
	mutex_unlock(&s->lock);
}

static const struct of_device_id stm_of[] = { { .compatible = "xiaomi,begonia-seninf-tm" }, { } };
MODULE_DEVICE_TABLE(of, stm_of);

static struct platform_driver stm_drv = {
	.probe = stm_probe, .remove = stm_remove,
	.driver = { .name = "begonia-seninf-tm", .of_match_table = stm_of },
};
module_platform_driver(stm_drv);
MODULE_DESCRIPTION("MT6785 SENINF test model (M2 step 1)");
MODULE_LICENSE("GPL");
