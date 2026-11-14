// SPDX-License-Identifier: GPL-2.0
/*
 * camsv-tm: M2 step 2 - MT6785 CAMSV (begonia): SENINF test model -> CAM_MUX -> CAMSV0 -> IMGO DMA into memory.
 * No IOMMU: a dma_alloc_coherent buffer (CMA, physically contiguous), the LARB port without translation.
 *
 * A debug remote-control module: the whole sequence is set from userspace through debugfs, so as not to
 * rebuild the module for every hypothesis.
 *   /sys/kernel/debug/camsv-tm/power   "1"/"0"   CAM domain, clocks (camsys/top), larb6 runtime PM
 *   /sys/kernel/debug/camsv-tm/buf     "<bytes>" allocate a buffer (0 - free); cat -> phys/size
 *   /sys/kernel/debug/camsv-tm/w       "<blk> <off> <val>"   blk: seninf|camsv|camsys
 *   /sys/kernel/debug/camsv-tm/r       "<blk> <off> [n]" ; cat -> values
 *   /sys/kernel/debug/camsv-tm/bufdump cat -> the first 256 bytes + number of non-zero bytes
 */
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static const char *const clk_names[] = {
	"cam-main", "cam-gals", "cam-tg", "cam-seninf", "cam-sv0", "cam-sv1", "cam-larb6", "cam-larb7",
	"top-seninf0", "top-seninf1", "top-seninf2",
};
enum { B_SENINF, B_CAMSV, B_CAMSYS, B_ANA, B_NR };
static const char *const blk_names[B_NR] = { "seninf", "camsv", "camsys", "ana" };

struct ctm {
	struct device *dev;
	void __iomem *base[B_NR];
	resource_size_t size[B_NR];
	struct clk_bulk_data clks[ARRAY_SIZE(clk_names)];
	struct device *pd, *larb;
	bool on;
	void *buf;
	dma_addr_t buf_dma;
	size_t buf_size;
	u32 rd_blk, rd_off, rd_n;
	struct mutex lock;
	struct dentry *dbg;
};

static int ctm_power(struct ctm *c, bool on)
{
	int ret;

	if (on == c->on)
		return 0;
	if (on) {
		c->pd = dev_pm_domain_attach_by_name(c->dev, "cam");
		if (IS_ERR_OR_NULL(c->pd)) {
			ret = c->pd ? PTR_ERR(c->pd) : -ENODEV;
			c->pd = NULL;
			return ret;
		}
		ret = pm_runtime_resume_and_get(c->pd);
		if (ret < 0)
			goto detach;
		ret = clk_bulk_prepare_enable(ARRAY_SIZE(clk_names), c->clks);
		if (ret)
			goto put;
		if (c->larb) {
			ret = pm_runtime_resume_and_get(c->larb);
			dev_info(c->dev, "larb6 runtime PM: %d\n", ret);
		}
		c->on = true;
		dev_info(c->dev, "power on\n");
		return 0;
	}
	if (c->larb)
		pm_runtime_put_sync(c->larb);
	clk_bulk_disable_unprepare(ARRAY_SIZE(clk_names), c->clks);
	ret = 0;
put:
	pm_runtime_put_sync(c->pd);
detach:
	dev_pm_domain_detach(c->pd, true);
	c->pd = NULL;
	c->on = false;
	return ret;
}

static int parse_blk(const char *s)
{
	int i;

	for (i = 0; i < B_NR; i++)
		if (!strcmp(s, blk_names[i]))
			return i;
	return -EINVAL;
}

static ssize_t user_str(const char __user *ub, size_t len, char *buf, size_t bl)
{
	if (len >= bl)
		return -EINVAL;
	if (copy_from_user(buf, ub, len))
		return -EFAULT;
	buf[len] = 0;
	return len;
}

static ssize_t power_write(struct file *f, const char __user *ub, size_t len, loff_t *off)
{
	struct ctm *c = file_inode(f)->i_private;
	bool on;
	int ret = kstrtobool_from_user(ub, len, &on);

	if (ret)
		return ret;
	mutex_lock(&c->lock);
	ret = ctm_power(c, on);
	mutex_unlock(&c->lock);
	return ret ? ret : len;
}

static ssize_t w_write(struct file *f, const char __user *ub, size_t len, loff_t *off)
{
	struct ctm *c = file_inode(f)->i_private;
	char buf[64], blk[16];
	u32 o, v;
	int b, ret = 0;

	if (user_str(ub, len, buf, sizeof(buf)) < 0)
		return -EINVAL;
	if (sscanf(buf, "%15s %x %x", blk, &o, &v) != 3)
		return -EINVAL;
	b = parse_blk(blk);
	if (b < 0 || o & 3 || o >= c->size[b])
		return -EINVAL;
	mutex_lock(&c->lock);
	if (c->on)
		writel(v, c->base[b] + o);
	else
		ret = -EIO;
	mutex_unlock(&c->lock);
	return ret ? ret : len;
}

static ssize_t r_write(struct file *f, const char __user *ub, size_t len, loff_t *off)
{
	struct ctm *c = file_inode(f)->i_private;
	char buf[64], blk[16];
	u32 o, n = 1;
	int b;

	if (user_str(ub, len, buf, sizeof(buf)) < 0)
		return -EINVAL;
	if (sscanf(buf, "%15s %x %x", blk, &o, &n) < 2)
		return -EINVAL;
	b = parse_blk(blk);
	if (b < 0 || o & 3 || o >= c->size[b] || !n || n > 256)
		return -EINVAL;
	c->rd_blk = b;
	c->rd_off = o;
	c->rd_n = min_t(u32, n, (c->size[b] - o) / 4);
	return len;
}

static int r_show(struct seq_file *m, void *v)
{
	struct ctm *c = m->private;
	u32 i;

	mutex_lock(&c->lock);
	if (!c->on) {
		seq_puts(m, "power is off\n");
	} else {
		for (i = 0; i < c->rd_n; i++) {
			u32 o = c->rd_off + 4 * i;

			seq_printf(m, "%s+0x%03x: 0x%08x\n", blk_names[c->rd_blk], o,
				   readl(c->base[c->rd_blk] + o));
		}
	}
	mutex_unlock(&c->lock);
	return 0;
}

static int r_open(struct inode *inode, struct file *f)
{
	return single_open(f, r_show, inode->i_private);
}

static ssize_t buf_write(struct file *f, const char __user *ub, size_t len, loff_t *off)
{
	struct ctm *c = file_inode(f)->i_private;
	unsigned long sz;
	int ret = kstrtoul_from_user(ub, len, 0, &sz);

	if (ret)
		return ret;
	mutex_lock(&c->lock);
	if (c->buf) {
		dma_free_coherent(c->dev, c->buf_size, c->buf, c->buf_dma);
		c->buf = NULL;
		c->buf_size = 0;
	}
	if (sz) {
		c->buf = dma_alloc_coherent(c->dev, PAGE_ALIGN(sz), &c->buf_dma, GFP_KERNEL);
		if (c->buf) {
			c->buf_size = PAGE_ALIGN(sz);
			memset(c->buf, 0, c->buf_size);
		} else {
			ret = -ENOMEM;
		}
	}
	mutex_unlock(&c->lock);
	return ret ? ret : len;
}

static int buf_show(struct seq_file *m, void *v)
{
	struct ctm *c = m->private;

	seq_printf(m, "phys 0x%llx size 0x%zx\n", (u64)c->buf_dma, c->buf_size);
	return 0;
}

static int buf_open(struct inode *inode, struct file *f)
{
	return single_open(f, buf_show, inode->i_private);
}

static ssize_t fill_write(struct file *f, const char __user *ub, size_t len, loff_t *off)
{
	struct ctm *c = file_inode(f)->i_private;
	unsigned int v;
	int ret = kstrtouint_from_user(ub, len, 0, &v);

	if (ret)
		return ret;
	mutex_lock(&c->lock);
	if (c->buf) {
		memset(c->buf, v & 0xff, c->buf_size);
		dma_sync_single_for_device(c->dev, c->buf_dma, c->buf_size, DMA_TO_DEVICE);
	} else {
		ret = -ENODATA;
	}
	mutex_unlock(&c->lock);
	return ret ? ret : len;
}

static ssize_t frame_read(struct file *f, char __user *ub, size_t len, loff_t *ppos)
{
	struct ctm *c = file_inode(f)->i_private;
	ssize_t ret;

	mutex_lock(&c->lock);
	if (!c->buf) {
		ret = -ENODATA;
	} else {
		dma_sync_single_for_cpu(c->dev, c->buf_dma, c->buf_size, DMA_FROM_DEVICE);
		ret = simple_read_from_buffer(ub, len, ppos, c->buf, c->buf_size);
	}
	mutex_unlock(&c->lock);
	return ret;
}

static int bufdump_show(struct seq_file *m, void *v)
{
	struct ctm *c = m->private;
	size_t i, nz = 0, first = SIZE_MAX, last = 0;
	const u8 *p;

	mutex_lock(&c->lock);
	if (!c->buf) {
		seq_puts(m, "no buffer\n");
		goto out;
	}
	p = c->buf;
	dma_sync_single_for_cpu(c->dev, c->buf_dma, c->buf_size, DMA_FROM_DEVICE);
	for (i = 0; i < c->buf_size; i++) {
		if (p[i]) {
			nz++;
			if (first == SIZE_MAX)
				first = i;
			last = i;
		}
	}
	seq_printf(m, "non-zero bytes: %zu of %zu (first 0x%zx, last 0x%zx)\n", nz, c->buf_size,
		   first == SIZE_MAX ? 0 : first, last);
	for (i = 0; i < 256 && i < c->buf_size; i += 16)
		seq_printf(m, "%04zx: %*ph\n", i, 16, p + i);
out:
	mutex_unlock(&c->lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(bufdump);

static const struct file_operations power_fops = {
	.owner = THIS_MODULE, .open = simple_open, .write = power_write, .llseek = noop_llseek,
};
static const struct file_operations w_fops = {
	.owner = THIS_MODULE, .open = simple_open, .write = w_write, .llseek = noop_llseek,
};
static const struct file_operations r_fops = {
	.owner = THIS_MODULE, .open = r_open, .read = seq_read, .write = r_write,
	.llseek = seq_lseek, .release = single_release,
};
static const struct file_operations fill_fops = {
	.owner = THIS_MODULE, .open = simple_open, .write = fill_write, .llseek = noop_llseek,
};
static const struct file_operations frame_fops = {
	.owner = THIS_MODULE, .open = simple_open, .read = frame_read, .llseek = default_llseek,
};
static const struct file_operations buf_fops = {
	.owner = THIS_MODULE, .open = buf_open, .read = seq_read, .write = buf_write,
	.llseek = seq_lseek, .release = single_release,
};

static int ctm_probe(struct platform_device *pdev)
{
	struct ctm *c;
	struct device_node *np;
	struct platform_device *lp;
	struct resource *res;
	int i, ret;

	c = devm_kzalloc(&pdev->dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = &pdev->dev;
	mutex_init(&c->lock);
	/* no request_mem_region: SENINF shares the window with seninf-tm, camsys - with the clock driver */
	for (i = 0; i < B_NR; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			return -EINVAL;
		c->size[i] = resource_size(res);
		c->base[i] = devm_ioremap(c->dev, res->start, c->size[i]);
		if (!c->base[i])
			return -ENOMEM;
	}
	for (i = 0; i < ARRAY_SIZE(clk_names); i++)
		c->clks[i].id = clk_names[i];
	ret = devm_clk_bulk_get(c->dev, ARRAY_SIZE(clk_names), c->clks);
	if (ret)
		return dev_err_probe(c->dev, ret, "clocks\n");
	ret = dma_set_mask_and_coherent(c->dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(c->dev, "dma mask: %d\n", ret);
	np = of_parse_phandle(c->dev->of_node, "mediatek,larb", 0);
	if (np) {
		lp = of_find_device_by_node(np);
		of_node_put(np);
		if (lp)
			c->larb = &lp->dev;
	}
	platform_set_drvdata(pdev, c);
	c->dbg = debugfs_create_dir("camsv-tm", NULL);
	debugfs_create_file("power", 0200, c->dbg, c, &power_fops);
	debugfs_create_file("w", 0200, c->dbg, c, &w_fops);
	debugfs_create_file("r", 0600, c->dbg, c, &r_fops);
	debugfs_create_file("buf", 0600, c->dbg, c, &buf_fops);
	debugfs_create_file("bufdump", 0400, c->dbg, c, &bufdump_fops);
	debugfs_create_file("frame", 0400, c->dbg, c, &frame_fops);
	debugfs_create_file("fill", 0200, c->dbg, c, &fill_fops);
	dev_info(c->dev, "ready (larb6 %s)\n", c->larb ? "found" : "NOT found");
	return 0;
}

static void ctm_remove(struct platform_device *pdev)
{
	struct ctm *c = platform_get_drvdata(pdev);

	debugfs_remove_recursive(c->dbg);
	mutex_lock(&c->lock);
	if (c->on) {
		writel(0, c->base[B_CAMSV] + 0x510);	/* MODULE_EN */
		writel(0, c->base[B_CAMSV] + 0x230);	/* TG_SEN_MODE */
		ctm_power(c, false);
	}
	if (c->buf)
		dma_free_coherent(c->dev, c->buf_size, c->buf, c->buf_dma);
	mutex_unlock(&c->lock);
	if (c->larb)
		put_device(c->larb);
}

static const struct of_device_id ctm_of[] = { { .compatible = "xiaomi,begonia-camsv-tm" }, { } };
MODULE_DEVICE_TABLE(of, ctm_of);

static struct platform_driver ctm_drv = {
	.probe = ctm_probe, .remove = ctm_remove,
	.driver = { .name = "begonia-camsv-tm", .of_match_table = ctm_of },
};
module_platform_driver(ctm_drv);
MODULE_DESCRIPTION("MT6785 CAMSV capture test (M2 step 2)");
MODULE_LICENSE("GPL");
