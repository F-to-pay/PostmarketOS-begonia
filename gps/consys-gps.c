// SPDX-License-Identifier: GPL-2.0
/*
 * /dev/stpgps and /dev/gps_emi for the vendor MediaTek GNSS daemon (mnld + libmnl, Android) on mt6785/begonia.
 *
 * A replacement for the missing gps_drv.ko (see MTK_kernel_modules/connectivity/gps/stp_chrdev_gps.c, gps_emi.c): a byte channel into STP frames
 * of type 2 (GPS_TASK_INDX) on top of the STP engine of the consys-bringup module, plus COMBO_IOC_* ioctls with the same numeric codes.
 * Loads/unloads independently of consys-bringup.
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/compat.h>

/* API consys-bringup.ko */
int consys_rx_get(void);
void consys_rx_put(void);
int consys_wmt_cmd(const u8 *cmd, int clen, const u8 *evt, int elen, unsigned int tmo_ms);
int consys_stp_send(u8 type, const u8 *buf, int len);
int consys_stp_register(u8 type, void (*cb)(void *ctx, const u8 *buf, int len), void *ctx);
unsigned int mtk_wcn_wmt_ic_info_get(int type);	/* 1 = HWVER, 3 = FWVER */

#define STP_TASK_GPS			2
#define STP_PAYLOAD_MAX			1000

/* the numeric ioctl codes of the vendor stp_chrdev_gps.c */
#define COMBO_IOC_GPS_HWVER		6
#define COMBO_IOC_GPS_IC_HW_VERSION	7
#define COMBO_IOC_GPS_IC_FW_VERSION	8
#define COMBO_IOC_D1_EFUSE_GET		9
#define COMBO_IOC_RTC_FLAG		10
#define COMBO_IOC_CO_CLOCK_FLAG		11
#define COMBO_IOC_TRIGGER_WMT_ASSERT	12
#define COMBO_IOC_WMT_STATUS		13
#define COMBO_IOC_TAKE_GPS_WAKELOCK	14
#define COMBO_IOC_GIVE_GPS_WAKELOCK	15
#define COMBO_IOC_GET_GPS_LNA_PIN	16
#define COMBO_IOC_GPS_FWCTL		17
#define COMBO_IOC_GPS_HW_SUSPEND	18
#define COMBO_IOC_GPS_HW_RESUME		19
#define COMBO_IOC_GPS_LISTEN_RST_EVT	20

static unsigned int lna_pin = 74;	/* vendor DT: GPS LNA = GPIO74; 0xffff = none */
module_param(lna_pin, uint, 0644);
static bool trace;
module_param(trace, bool, 0644);
MODULE_PARM_DESC(trace, "log every read/write/ioctl (first bytes)");

static DEFINE_MUTEX(gps_lock);		/* open/close */
static DEFINE_MUTEX(gps_rd_lock);
static DEFINE_MUTEX(gps_wr_lock);
static int gps_users;
static bool gps_closing;
static DECLARE_WAIT_QUEUE_HEAD(gps_rxq);
static DECLARE_WAIT_QUEUE_HEAD(gps_rst_wq);
static DEFINE_SPINLOCK(gps_fifo_wlock);
static DEFINE_KFIFO(gps_fifo, u8, 131072);
static unsigned long gps_rx_bytes, gps_tx_bytes, gps_rx_drop;

static const u8 gps_on_cmd[]  = { 0x01, 0x06, 0x02, 0x00, 0x02, 0x01 };	/* FUNC_CTRL, WMTDRV_TYPE_GPS, on */
static const u8 gps_off_cmd[] = { 0x01, 0x06, 0x02, 0x00, 0x02, 0x00 };
static const u8 func_evt[]    = { 0x02, 0x06, 0x01, 0x00, 0x00 };

/* from the consys-bringup receive thread: STP type 2 -> FIFO */
static void gps_rx_cb(void *ctx, const u8 *buf, int len)
{
	unsigned int n;
	unsigned long flags;

	spin_lock_irqsave(&gps_fifo_wlock, flags);
	n = kfifo_in(&gps_fifo, buf, len);
	spin_unlock_irqrestore(&gps_fifo_wlock, flags);
	gps_rx_bytes += n;
	if (n != len)
		gps_rx_drop += len - n;
	if (trace)
		pr_info("consys-gps: RX %d B: %*pEhp\n", len, min(len, 32), buf);
	wake_up_interruptible(&gps_rxq);
}

static int stpgps_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	mutex_lock(&gps_lock);
	if (gps_users == 0) {
		ret = consys_rx_get();
		if (ret)
			goto out;
		ret = consys_stp_register(STP_TASK_GPS, gps_rx_cb, NULL);
		if (ret) {
			consys_rx_put();
			goto out;
		}
		kfifo_reset(&gps_fifo);
		ret = consys_wmt_cmd(gps_on_cmd, sizeof(gps_on_cmd), func_evt, sizeof(func_evt), 3000);
		if (ret) {
			pr_err("consys-gps: FUNC_CTRL(GPS on) failed: %d\n", ret);
			consys_stp_register(STP_TASK_GPS, NULL, NULL);
			consys_rx_put();
			ret = -EIO;
			goto out;
		}
		gps_closing = false;
		pr_info("consys-gps: GPS function ON\n");
	}
	gps_users++;
out:
	mutex_unlock(&gps_lock);
	return ret;
}

static int stpgps_release(struct inode *inode, struct file *file)
{
	mutex_lock(&gps_lock);
	if (gps_users > 0 && --gps_users == 0) {
		gps_closing = true;
		wake_up_interruptible(&gps_rxq);
		wake_up_interruptible(&gps_rst_wq);
		consys_wmt_cmd(gps_off_cmd, sizeof(gps_off_cmd), func_evt, sizeof(func_evt), 3000);
		consys_stp_register(STP_TASK_GPS, NULL, NULL);
		consys_rx_put();
		pr_info("consys-gps: GPS function OFF (rx %lu B, tx %lu B, dropped %lu B)\n", gps_rx_bytes, gps_tx_bytes, gps_rx_drop);
	}
	mutex_unlock(&gps_lock);
	return 0;
}

static ssize_t stpgps_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	unsigned int copied = 0;
	int ret;

	if (!count)
		return 0;
	mutex_lock(&gps_rd_lock);
	while (kfifo_is_empty(&gps_fifo)) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		}
		ret = wait_event_interruptible(gps_rxq, !kfifo_is_empty(&gps_fifo) || gps_closing);
		if (ret)
			goto out;
		if (gps_closing && kfifo_is_empty(&gps_fifo)) {
			ret = -EIO;
			goto out;
		}
	}
	ret = kfifo_to_user(&gps_fifo, ubuf, count, &copied);
	if (!ret)
		ret = copied;
out:
	mutex_unlock(&gps_rd_lock);
	return ret;
}

static ssize_t stpgps_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	u8 *buf;
	size_t done = 0;
	int ret = 0;

	if (!count)
		return -EFAULT;
	buf = kmalloc(STP_PAYLOAD_MAX, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	mutex_lock(&gps_wr_lock);
	while (done < count) {
		size_t n = min_t(size_t, count - done, STP_PAYLOAD_MAX);

		if (copy_from_user(buf, ubuf + done, n)) {
			ret = -EFAULT;
			break;
		}
		if (trace)
			pr_info("consys-gps: TX %zu B: %*pEhp\n", n, (int)min_t(size_t, n, 32), buf);
		ret = consys_stp_send(STP_TASK_GPS, buf, n);
		if (ret)
			break;
		done += n;
	}
	mutex_unlock(&gps_wr_lock);
	kfree(buf);
	gps_tx_bytes += done;
	return done ? (ssize_t)done : ret;
}

static __poll_t stpgps_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;

	poll_wait(file, &gps_rxq, wait);
	if (!kfifo_is_empty(&gps_fifo))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (gps_closing)
		mask |= EPOLLHUP;
	return mask;
}

static long stpgps_do_ioctl(struct file *file, unsigned int cmd, void __user *argp, unsigned long arg)
{
	u32 v;

	if (trace)
		pr_info("consys-gps: ioctl %u arg=0x%lx\n", cmd, arg);
	switch (cmd) {
	case COMBO_IOC_GPS_IC_HW_VERSION:
		v = mtk_wcn_wmt_ic_info_get(1);
		return copy_to_user(argp, &v, sizeof(v)) ? -EFAULT : 0;
	case COMBO_IOC_GPS_IC_FW_VERSION:
		v = mtk_wcn_wmt_ic_info_get(3);
		return copy_to_user(argp, &v, sizeof(v)) ? -EFAULT : 0;
	case COMBO_IOC_RTC_FLAG:
	case COMBO_IOC_CO_CLOCK_FLAG:
	case COMBO_IOC_WMT_STATUS:
	case COMBO_IOC_TAKE_GPS_WAKELOCK:
	case COMBO_IOC_GIVE_GPS_WAKELOCK:
		return 0;
	case COMBO_IOC_TRIGGER_WMT_ASSERT:
		return -EBUSY;
	case COMBO_IOC_GET_GPS_LNA_PIN:
		v = lna_pin;
		return copy_to_user(argp, &v, sizeof(v)) ? -EFAULT : 0;
	case COMBO_IOC_GPS_HW_SUSPEND:
	case COMBO_IOC_GPS_HW_RESUME:
		return -EINVAL;		/* "not supported" in mnld terms */
	case COMBO_IOC_GPS_LISTEN_RST_EVT:
		/* blocks until a chip reset or until the device is closed (we do not signal a chip reset) */
		return wait_event_interruptible(gps_rst_wq, gps_closing);
	default:
		return -EFAULT;		/* like the vendor: unknown command */
	}
}

static long stpgps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return stpgps_do_ioctl(file, cmd, (void __user *)arg, arg);
}

static long stpgps_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return stpgps_do_ioctl(file, cmd, compat_ptr(arg), arg);
}

static const struct file_operations stpgps_fops = {
	.owner		= THIS_MODULE,
	.open		= stpgps_open,
	.release	= stpgps_release,
	.read		= stpgps_read,
	.write		= stpgps_write,
	.poll		= stpgps_poll,
	.unlocked_ioctl	= stpgps_ioctl,
	.compat_ioctl	= stpgps_compat_ioctl,
};

static struct miscdevice stpgps_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "stpgps",
	.fops	= &stpgps_fops,
	.mode	= 0660,
};

/* ---------- /dev/gps_emi: for mt6785 the vendor driver has no GPS EMI region -> a plain 1 MB buffer, mmap + ioctl stubs ---------- */
#define GPS_EMI_BUF_SIZE	SZ_1M
#define IOCTL_EMI_MEMORY_INIT		1
#define IOCTL_MNL_NVRAM_FILE_TO_MEM	2
#define IOCTL_MNL_NVRAM_MEM_TO_FILE	3
#define IOCTL_ADC_CAPTURE_ADDR_GET	4

static void *emi_buf;

static int gpsemi_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int gpsemi_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (!emi_buf)
		return -ENOMEM;
	return remap_vmalloc_range(vma, emi_buf, vma->vm_pgoff);
}

static long gpsemi_do_ioctl(unsigned int cmd, void __user *argp)
{
	u32 v = 0;

	if (trace)
		pr_info("consys-gps: gps_emi ioctl %u\n", cmd);
	switch (cmd) {
	case IOCTL_EMI_MEMORY_INIT:
	case IOCTL_MNL_NVRAM_FILE_TO_MEM:
	case IOCTL_MNL_NVRAM_MEM_TO_FILE:
		return 0;
	case IOCTL_ADC_CAPTURE_ADDR_GET:
		return copy_to_user(argp, &v, sizeof(v)) ? -EFAULT : 0;
	default:
		return -EINVAL;
	}
}

static long gpsemi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return gpsemi_do_ioctl(cmd, (void __user *)arg);
}

static long gpsemi_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return gpsemi_do_ioctl(cmd, compat_ptr(arg));
}

static const struct file_operations gpsemi_fops = {
	.owner		= THIS_MODULE,
	.open		= gpsemi_open,
	.mmap		= gpsemi_mmap,
	.unlocked_ioctl	= gpsemi_ioctl,
	.compat_ioctl	= gpsemi_compat_ioctl,
};

static struct miscdevice gpsemi_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "gps_emi",
	.fops	= &gpsemi_fops,
	.mode	= 0660,
};

static int __init consys_gps_init(void)
{
	int ret;

	emi_buf = vmalloc_user(GPS_EMI_BUF_SIZE);
	if (!emi_buf)
		return -ENOMEM;
	ret = misc_register(&stpgps_misc);
	if (ret)
		goto err_buf;
	ret = misc_register(&gpsemi_misc);
	if (ret)
		goto err_stp;
	pr_info("consys-gps: /dev/stpgps and /dev/gps_emi registered\n");
	return 0;
err_stp:
	misc_deregister(&stpgps_misc);
err_buf:
	vfree(emi_buf);
	return ret;
}

static void __exit consys_gps_exit(void)
{
	misc_deregister(&gpsemi_misc);
	misc_deregister(&stpgps_misc);
	vfree(emi_buf);
}

module_init(consys_gps_init);
module_exit(consys_gps_exit);

MODULE_DESCRIPTION("/dev/stpgps and /dev/gps_emi over consys-bringup STP (begonia)");
MODULE_LICENSE("GPL");
