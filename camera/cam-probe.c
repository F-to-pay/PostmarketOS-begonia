// SPDX-License-Identifier: GPL-2.0
/*
 * cam-probe: M0 - applying power/MCLK to a camera sensor of the Redmi Note 8 Pro (begonia) to read the chip ID over I2C.
 *
 * Configures nothing in the sensor, it only repeats the vendor power sequence
 * (imgsensor_cfg_table.c) and gives a control handle:
 *
 *   /sys/kernel/debug/begonia-cam/power   write "<slot> on [class]" | "<slot> off"
 *   /sys/kernel/debug/begonia-cam/state   read
 *
 * Slot = the vendor sensor index: 0 MAIN, 1 SUB, 2 MAIN2, 3 SUB2, 4 MAIN3.
 * Sequence class: A (S5KGW1), B (S5K3T1/T2), C (OV8856), D (GC5035), E (OV02A10/GC2375).
 * The ID is read by userspace (cam-id.py) over /dev/i2c-N while the slot is on.
 *
 * Parameter stage (default 1):
 *   0 = pins and vcamio only (no MCLK);  1 = + clocks CAMTG1..5 (24 MHz), without the CAM power domain;
 *   2 = + CAM domain (pm_runtime) and the camsys CAMTG clock  <- may lock up the bus if the domain is not ready.
 */
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>

#define NSLOT	5
#define NTG	5

static int stage = 1;
module_param(stage, int, 0444);
MODULE_PARM_DESC(stage, "0 pins; 1 +CAMTG clocks; 2 +CAM domain");

static bool tg_keep;
module_param(tg_keep, bool, 0444);
MODULE_PARM_DESC(tg_keep, "M1: keep CAMTG1..5 (24 MHz) on from module load, do not touch the pins "
		 "(MCLK for the mainline sensor drivers while the CMMCLKn->CAMTGm wiring is unknown)");

enum pin { P_RST, P_PDN, P_VCAMA, P_VCAMD, P_MCLK, P_DOVDD, P_SW, P_END };

struct step { enum pin pin; int level; int delay_ms; };

/* RST: pin level (1 = high). MCLK/DOVDD/AVDD/DVDD: 1 = enable. */
static const struct step seq_A[] = {	/* S5KGW1 */
	{ P_RST, 0, 0 }, { P_MCLK, 1, 0 }, { P_DOVDD, 1, 0 }, { P_VCAMA, 1, 0 },
	{ P_VCAMD, 1, 2 }, { P_PDN, 1, 1 }, { P_RST, 1, 5 }, { P_END },
};
static const struct step seq_B[] = {	/* S5K3T1/T2 */
	{ P_RST, 0, 1 }, { P_MCLK, 1, 1 }, { P_DOVDD, 1, 1 }, { P_VCAMA, 1, 1 },
	{ P_VCAMD, 1, 1 }, { P_RST, 1, 10 }, { P_END },
};
static const struct step seq_C[] = {	/* OV8856 */
	{ P_PDN, 1, 0 }, { P_MCLK, 1, 0 }, { P_DOVDD, 1, 0 }, { P_VCAMA, 1, 0 },
	{ P_VCAMD, 1, 0 }, { P_RST, 0, 0 }, { P_RST, 1, 5 }, { P_END },
};
static const struct step seq_D[] = {	/* GC5035 */
	{ P_RST, 0, 1 }, { P_DOVDD, 1, 1 }, { P_VCAMA, 1, 1 }, { P_MCLK, 1, 5 },
	{ P_RST, 1, 10 }, { P_END },
};
static const struct step seq_E[] = {	/* OV02A10 / GC2375 */
	{ P_RST, 1, 1 }, { P_DOVDD, 1, 1 }, { P_VCAMA, 1, 1 }, { P_MCLK, 1, 5 },
	{ P_RST, 0, 10 }, { P_END },
};
static const struct step *const seqs[] = { ['A'-'A'] = seq_A, ['B'-'A'] = seq_B, ['C'-'A'] = seq_C,
					   ['D'-'A'] = seq_D, ['E'-'A'] = seq_E };
static const char default_cls[NSLOT] = { 'A', 'B', 'D', 'E', 'C' };

struct cam {
	struct device *dev;
	struct pinctrl *pc;
	struct pinctrl_state *st[NSLOT][P_END][2];
	struct pinctrl_state *sw[2];
	struct clk *tg[NTG], *src, *camtg;
	struct regulator *vcamio;
	struct device *pd;
	struct mutex lock;
	struct dentry *dbg;
	bool clk_on, pd_on, io_on;
	int cur;		/* the powered slot or -1 */
	char cur_cls;
};

static const char *const pname[P_END] = { "rst", "pdn", "vcama", "vcamd", "mclk" };

static void setpin(struct cam *c, int slot, enum pin p, int lvl)
{
	struct pinctrl_state *s = c->st[slot][p][!!lvl];
	int ret;

	if (!s) {
		dev_warn(c->dev, "slot%d: no state %s%d\n", slot, pname[p], !!lvl);
		return;
	}
	ret = pinctrl_select_state(c->pc, s);
	if (ret)
		dev_warn(c->dev, "slot%d: %s=%d: %d\n", slot, pname[p], !!lvl, ret);
}

static int clocks_on(struct cam *c)
{
	int i, ret;

	if (c->clk_on)
		return 0;
	if (stage >= 2) {
		c->pd = dev_pm_domain_attach_by_name(c->dev, "cam");
		if (IS_ERR_OR_NULL(c->pd)) {
			ret = c->pd ? PTR_ERR(c->pd) : -ENODEV;
			c->pd = NULL;
			dev_err(c->dev, "attaching the cam domain: %d\n", ret);
			return ret;
		}
		ret = pm_runtime_resume_and_get(c->pd);
		dev_info(c->dev, "CAM domain: pm_runtime_resume_and_get=%d\n", ret);
		if (ret < 0)
			return ret;
		c->pd_on = true;
		msleep(20);
		if (c->camtg) {
			ret = clk_prepare_enable(c->camtg);
			if (ret)
				dev_warn(c->dev, "camsys CAMTG: %d\n", ret);
		}
	}
	for (i = 0; i < NTG; i++) {
		if (c->src && c->tg[i])
			clk_set_parent(c->tg[i], c->src);
		ret = clk_prepare_enable(c->tg[i]);
		if (ret)
			dev_warn(c->dev, "tg%d: %d\n", i, ret);
	}
	c->clk_on = true;
	dev_info(c->dev, "clocks CAMTG1..5 enabled, tg0=%lu Hz\n", clk_get_rate(c->tg[0]));
	return 0;
}

static void clocks_off(struct cam *c)
{
	int i;

	if (!c->clk_on)
		return;
	for (i = 0; i < NTG; i++)
		clk_disable_unprepare(c->tg[i]);
	if (stage >= 2 && c->pd_on) {
		if (c->camtg)
			clk_disable_unprepare(c->camtg);
		pm_runtime_put_sync(c->pd);
		c->pd_on = false;
		dev_pm_domain_detach(c->pd, true);
		c->pd = NULL;
	}
	c->clk_on = false;
}

static int slot_on(struct cam *c, int slot, char cls)
{
	const struct step *s;
	int ret;

	if (c->cur >= 0)
		return -EBUSY;
	if (cls < 'A' || cls > 'E')
		return -EINVAL;
	s = seqs[cls - 'A'];

	if (slot == 1 || slot == 4) {	/* MIPI mux: sub=0, main3=1 */
		if (c->sw[slot == 4])
			pinctrl_select_state(c->pc, c->sw[slot == 4]);
	}
	if (stage >= 1) {
		ret = clocks_on(c);
		if (ret)
			return ret;
	}
	for (; s->pin != P_END; s++) {
		switch (s->pin) {
		case P_MCLK:
			if (stage >= 1)
				setpin(c, slot, P_MCLK, s->level);
			break;
		case P_DOVDD:
			if (s->level && !c->io_on) {
				ret = regulator_enable(c->vcamio);
				if (ret)
					dev_warn(c->dev, "vcamio: %d\n", ret);
				else
					c->io_on = true;
			}
			break;
		default:
			setpin(c, slot, s->pin, s->level);
		}
		if (s->delay_ms)
			msleep(s->delay_ms);
	}
	c->cur = slot;
	c->cur_cls = cls;
	dev_info(c->dev, "slot %d on (class %c, stage %d)\n", slot, cls, stage);
	return 0;
}

static void slot_off(struct cam *c)
{
	int slot = c->cur;

	if (slot < 0)
		return;
	setpin(c, slot, P_RST, c->cur_cls == 'E' ? 1 : 0);
	setpin(c, slot, P_PDN, 0);
	setpin(c, slot, P_VCAMD, 0);
	setpin(c, slot, P_VCAMA, 0);
	setpin(c, slot, P_MCLK, 0);
	if (c->io_on) {
		regulator_disable(c->vcamio);
		c->io_on = false;
	}
	if (!tg_keep)
		clocks_off(c);
	c->cur = -1;
	dev_info(c->dev, "slot %d off\n", slot);
}

static ssize_t power_write(struct file *f, const char __user *ubuf, size_t len, loff_t *off)
{
	struct cam *c = file_inode(f)->i_private;
	char buf[32], cmd[8] = "", cls = 0;
	int slot, n, ret = 0;

	if (len >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len] = 0;
	n = sscanf(buf, "%d %7s %c", &slot, cmd, &cls);
	if (n < 2 || slot < 0 || slot >= NSLOT)
		return -EINVAL;
	mutex_lock(&c->lock);
	if (!strcmp(cmd, "on"))
		ret = slot_on(c, slot, n >= 3 ? cls : default_cls[slot]);
	else if (!strcmp(cmd, "off"))
		slot_off(c);
	else
		ret = -EINVAL;
	mutex_unlock(&c->lock);
	return ret ? ret : len;
}

static int state_show(struct seq_file *m, void *v)
{
	struct cam *c = m->private;

	seq_printf(m, "stage=%d slot=%d class=%c clk=%d pd=%d vcamio=%d\n", stage, c->cur,
		   c->cur >= 0 ? c->cur_cls : '-', c->clk_on, c->pd_on, c->io_on);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(state);

static const struct file_operations power_fops = {
	.owner = THIS_MODULE, .open = simple_open, .write = power_write, .llseek = noop_llseek,
};

static int cam_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cam *c;
	char nm[24];
	int i, p, v, found = 0;

	c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = dev;
	c->cur = -1;
	mutex_init(&c->lock);

	c->pc = devm_pinctrl_get(dev);
	if (IS_ERR(c->pc))
		return dev_err_probe(dev, PTR_ERR(c->pc), "pinctrl\n");
	for (i = 0; i < NSLOT; i++) {
		for (p = P_RST; p <= P_MCLK; p++) {
			for (v = 0; v < 2; v++) {
				if (p == P_MCLK)
					snprintf(nm, sizeof(nm), "cam%d_mclk_%s", i, v ? "on" : "off");
				else
					snprintf(nm, sizeof(nm), "cam%d_%s%d", i, pname[p], v);
				c->st[i][p][v] = pinctrl_lookup_state(c->pc, nm);
				if (IS_ERR(c->st[i][p][v]))
					c->st[i][p][v] = NULL;
				else
					found++;
			}
		}
	}
	for (v = 0; v < 2; v++) {
		snprintf(nm, sizeof(nm), "sw%d", v);
		c->sw[v] = pinctrl_lookup_state(c->pc, nm);
		if (IS_ERR(c->sw[v]))
			c->sw[v] = NULL;
	}
	dev_info(dev, "pinctrl: found %d states, sw0=%d sw1=%d\n", found, !!c->sw[0], !!c->sw[1]);

	c->vcamio = devm_regulator_get(dev, "vcamio");
	if (IS_ERR(c->vcamio))
		return dev_err_probe(dev, PTR_ERR(c->vcamio), "vcamio\n");

	for (i = 0; i < NTG; i++) {
		snprintf(nm, sizeof(nm), "tg%d", i);
		c->tg[i] = devm_clk_get(dev, nm);
		if (IS_ERR(c->tg[i]))
			return dev_err_probe(dev, PTR_ERR(c->tg[i]), "%s\n", nm);
	}
	c->src = devm_clk_get_optional(dev, "src24");
	c->camtg = devm_clk_get_optional(dev, "camtg");
	if (IS_ERR(c->src) || IS_ERR(c->camtg))
		return dev_err_probe(dev, -ENOENT, "src24/camtg\n");

	platform_set_drvdata(pdev, c);
	if (tg_keep && stage >= 1) {
		int ret = clocks_on(c);

		if (ret)
			return dev_err_probe(dev, ret, "tg_keep\n");
	}
	c->dbg = debugfs_create_dir("begonia-cam", NULL);
	debugfs_create_file("power", 0200, c->dbg, c, &power_fops);
	debugfs_create_file("state", 0444, c->dbg, c, &state_fops);
	dev_info(dev, "ready (stage %d). Power: echo '<slot> on [A-E]' > /sys/kernel/debug/begonia-cam/power\n", stage);
	return 0;
}

static void cam_remove(struct platform_device *pdev)
{
	struct cam *c = platform_get_drvdata(pdev);

	mutex_lock(&c->lock);
	slot_off(c);
	clocks_off(c);
	mutex_unlock(&c->lock);
	debugfs_remove_recursive(c->dbg);
}

static const struct of_device_id cam_of[] = { { .compatible = "xiaomi,begonia-cam-probe" }, { } };
MODULE_DEVICE_TABLE(of, cam_of);

static struct platform_driver cam_drv = {
	.probe = cam_probe, .remove = cam_remove,
	.driver = { .name = "begonia-cam-probe", .of_match_table = cam_of },
};
module_platform_driver(cam_drv);

MODULE_DESCRIPTION("Begonia camera power-up probe (M0)");
MODULE_LICENSE("GPL");
