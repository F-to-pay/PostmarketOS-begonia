/* SPDX-License-Identifier: GPL-2.0 */
/* Compat layer: the kernel 4.14 API that disappeared by 6.16. Included into all gen4m files via -include. */
#ifndef _GEN4M_COMPAT_H_
#define _GEN4M_COMPAT_H_

#include <linux/version.h>
#include <linux/types.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>

/* struct timeval / do_gettimeofday were removed from the kernel (y2038) */
#ifndef GEN4M_HAVE_TIMEVAL
#define GEN4M_HAVE_TIMEVAL
struct timeval {
	long tv_sec;
	long tv_usec;
};

static inline void do_gettimeofday(struct timeval *tv)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
}
#endif

/* kernel renames 4.14 -> 6.16 */
#include <linux/sched/clock.h>
#include <linux/timer.h>
#include <linux/rtc.h>
#include <linux/io.h>

#ifndef from_timer
#define from_timer(var, callback_timer, timer_fieldname) timer_container_of(var, callback_timer, timer_fieldname)
#endif
#define del_timer_sync(t) timer_delete_sync(t)
#define del_timer(t) timer_delete(t)
#define rtc_time_to_tm(t, tm) rtc_time64_to_tm((time64_t)(t), (tm))
#define ioremap_nocache(a, s) ioremap((a), (s))

#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/gro.h>
#define strlcpy(d, s, n) strscpy((d), (s), (n))
#define netif_rx_ni(skb) netif_rx(skb)

#endif /* _GEN4M_COMPAT_H_ */
