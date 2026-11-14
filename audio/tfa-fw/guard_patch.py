p = '/mnt/c/Users/1342c/workspace/Begonia/wifi/audio/codecs/tfa9874.c'
s = open(p, encoding='utf-8').read()


def rep(old, new):
    global s
    assert old in s, old[:70]
    s = s.replace(old, new, 1)


rep(r'''#include <linux/regmap.h>
''', r'''#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
''')

rep(r'''	unsigned int vol;	/* volume control index (0..TFA_VOL_MAX) */
	struct dentry *dbg;
};''', r'''	unsigned int vol;	/* volume control index (0..TFA_VOL_MAX) */
	struct dentry *dbg;
	struct dentry *dbg_fault;

	/* protection: status watchdog (see tfa9874_monitor) */
	struct delayed_work mon;
	unsigned int bad_hard, bad_soft, bad_i2c;	/* bad polls in a row */
	unsigned int fault_count;			/* faults since the last reset */
	unsigned long fault_t[3];			/* times of the last three faults */
	unsigned int fault_idx;
	unsigned long cooldown_until;
	bool latched;					/* latch: amplifier blocked until reset */
};''')

guard = r'''/* ---- protection: status watchdog -----------------------------------------
 * While the amplifier is on, read the STATUS flags (0x10/0x11) every 100 ms.
 * In the tfa2 family a one = NORMAL for DCOCPOK(b1)/OTDS(b2)/UVDS(b4), and faults are a one
 * for OCDS(b3)/MANALARM(b5)/TDMERR(b6)/NOCLK(b7). The chip hardware protection acts by itself;
 * the watchdog adds supervision: on a fault (2 bad polls in a row) it switches the amplifier off,
 * counts the failures, keeps a 3 s pause, and on 3 faults within 60 s it LATCHES the amplifier
 * (it will not turn on until reset: echo reset > /sys/kernel/debug/tfa9874-fault).
 * A lost clock (soft) just switches the amplifier off without counting a fault.
 */
#define TFA_MON_MS		100
#define TFA_FAULT_COOLDOWN_MS	3000
#define TFA_FAULT_WINDOW_MS	60000

static void tfa9874_power_down(struct tfa9874 *t);

/* called under t->lock */
static void tfa9874_fault(struct tfa9874 *t, bool hard, unsigned int s0, unsigned int s1,
			  const char *why)
{
	unsigned long now = jiffies;

	tfa9874_power_down(t);
	if (!hard) {
		dev_warn(t->dev, "PROTECTION: %s (STATUS 0x10=%04x 0x11=%04x), amplifier off\n",
			 why, s0, s1);
		return;
	}

	t->fault_count++;
	t->fault_t[t->fault_idx % 3] = now;
	t->fault_idx++;
	/* three faults in a row within the window - latch */
	if (t->fault_idx >= 3 &&
	    time_before(now, t->fault_t[t->fault_idx % 3] +
				     msecs_to_jiffies(TFA_FAULT_WINDOW_MS)))
		t->latched = true;
	t->cooldown_until = now + msecs_to_jiffies(TFA_FAULT_COOLDOWN_MS);

	dev_err(t->dev, "PROTECTION: FAULT #%u: %s (STATUS 0x10=%04x 0x11=%04x), amplifier off%s\n",
		t->fault_count, why, s0, s1,
		t->latched ? "; LATCH: 3 faults within 60 s, speaker sound blocked until reset" :
			     "; restart in 3 s");
}

static void tfa9874_monitor(struct work_struct *w)
{
	struct tfa9874 *t = container_of(to_delayed_work(w), struct tfa9874, mon);
	unsigned int s0 = 0, s1 = 0;
	bool hard, soft;

	mutex_lock(&t->lock);
	if (!t->powered)
		goto out;

	if (regmap_read(t->regmap, 0x10, &s0) || regmap_read(t->regmap, 0x11, &s1)) {
		if (++t->bad_i2c >= 3) {
			tfa9874_fault(t, true, 0, 0, "no i2c communication with the amplifier");
			goto out;
		}
		goto resched;
	}
	t->bad_i2c = 0;

	hard = (s0 & 0x16) != 0x16 || (s0 & (BIT(3) | BIT(5) | BIT(6)));
	soft = (s0 & BIT(7)) || !(s1 & BIT(7));

	if (hard) {
		if (++t->bad_hard >= 2) {
			tfa9874_fault(t, true, s0, s1,
				      (s0 & BIT(3)) ? "overcurrent (OCP)" :
				      !(s0 & BIT(2)) ? "OVERHEAT" :
				      !(s0 & BIT(4)) ? "supply drop" :
				      !(s0 & BIT(1)) ? "DC-DC overload" :
				      (s0 & BIT(5)) ? "manual alarm" : "TDM error");
			goto out;
		}
	} else {
		t->bad_hard = 0;
	}

	if (!hard && soft) {
		if (++t->bad_soft >= 3) {
			tfa9874_fault(t, false, s0, s1, "I2S clock lost");
			goto out;
		}
	} else {
		t->bad_soft = 0;
	}

resched:
	if (t->powered)
		schedule_delayed_work(&t->mon, msecs_to_jiffies(TFA_MON_MS));
out:
	mutex_unlock(&t->lock);
}

'''

rep(r'''static int tfa9874_power_up(struct tfa9874 *t)
{
	unsigned int clks = 0;
	int ret, i;

''', guard + r'''static int tfa9874_power_up(struct tfa9874 *t)
{
	unsigned int clks = 0;
	int ret, i;

	if (t->latched) {
		dev_err_ratelimited(t->dev, "amplifier blocked by the protection after faults (reset: echo reset > /sys/kernel/debug/tfa9874-fault)\n");
		return -EIO;
	}
	if (time_before(jiffies, t->cooldown_until)) {
		dev_warn_ratelimited(t->dev, "pause after a fault, not turning the amplifier on yet\n");
		return -EBUSY;
	}

''')

rep(r'''	t->powered = true;
	tfa9874_dump_status(t, "up+5ms");
	msleep(100);
	tfa9874_dump_status(t, "up+105ms");
	return 0;''', r'''	t->powered = true;
	t->bad_hard = t->bad_soft = t->bad_i2c = 0;
	tfa9874_dump_status(t, "up+5ms");
	msleep(100);
	tfa9874_dump_status(t, "up+105ms");
	schedule_delayed_work(&t->mon, msecs_to_jiffies(TFA_MON_MS));
	return 0;''')

rep(r'''static void tfa9874_power_down(struct tfa9874 *t)
{
''', r'''static void tfa9874_power_down(struct tfa9874 *t)
{
	cancel_delayed_work(&t->mon);
''')

rep(r'''	seq_printf(m, "powered=%d REV=0x%04x\n", t->powered, t->rev);''',
    r'''	seq_printf(m, "powered=%d REV=0x%04x vol_idx=%u faults=%u latched=%d\n", t->powered, t->rev, t->vol,
		   t->fault_count, t->latched);''')

fops = r'''
/* debugfs /sys/kernel/debug/tfa9874-fault: "reset" - clear the latch, "inject" - simulate a fault (protection check) */
static ssize_t tfa9874_fault_write(struct file *f, const char __user *ubuf, size_t len, loff_t *off)
{
	struct tfa9874 *t = f->private_data;
	char buf[16] = {};

	if (copy_from_user(buf, ubuf, min(len, sizeof(buf) - 1)))
		return -EFAULT;
	mutex_lock(&t->lock);
	if (!strncmp(buf, "reset", 5)) {
		t->latched = false;
		t->fault_count = 0;
		t->fault_idx = 0;
		t->cooldown_until = jiffies;
		dev_info(t->dev, "protection reset\n");
	} else if (!strncmp(buf, "inject", 6)) {
		if (t->powered)
			tfa9874_fault(t, true, 0xdead, 0xdead, "TEST: simulated fault");
		else
			dev_info(t->dev, "inject: the amplifier is off, nothing to bring down\n");
	}
	mutex_unlock(&t->lock);
	return len;
}

static const struct file_operations tfa9874_fault_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = tfa9874_fault_write,
};
'''
rep(r'''DEFINE_SHOW_ATTRIBUTE(tfa9874_status);
''', r'''DEFINE_SHOW_ATTRIBUTE(tfa9874_status);
''' + fops)

rep(r'''	mutex_init(&t->lock);
	t->vol = TFA_VOL_DEFAULT;''', r'''	mutex_init(&t->lock);
	INIT_DELAYED_WORK(&t->mon, tfa9874_monitor);
	t->vol = TFA_VOL_DEFAULT;''')

rep(r'''	t->dbg = debugfs_create_file("tfa9874-status", 0444, NULL, t, &tfa9874_status_fops);''',
    r'''	t->dbg = debugfs_create_file("tfa9874-status", 0444, NULL, t, &tfa9874_status_fops);
	t->dbg_fault = debugfs_create_file("tfa9874-fault", 0200, NULL, t, &tfa9874_fault_fops);''')

rep(r'''	if (t)
		debugfs_remove(t->dbg);
	if (t && t->powered)
		tfa9874_power_down(t);''', r'''	if (t) {
		debugfs_remove(t->dbg);
		debugfs_remove(t->dbg_fault);
		cancel_delayed_work_sync(&t->mon);
	}
	if (t && t->powered)
		tfa9874_power_down(t);''')

open(p, 'w', encoding='utf-8').write(s)
print('patched')
