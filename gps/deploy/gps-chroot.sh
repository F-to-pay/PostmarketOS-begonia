#!/bin/sh
# Android chroot with the vendor mnld (32-bit ARM, Android 11). Run: sudo sh /home/user/gps-chroot.sh {setup|run|shell|stop|teardown}
R=/opt/gps-chroot
D=$(dirname "$(readlink -f "$0")")
mnt() { mountpoint -q "$2" || mount $1 "$2"; }
case "$1" in
setup)
	mkdir -p $R && tar xzf $D/gps-chroot.tgz -C $R
	chmod 755 $R/apex/com.android.runtime/bin/linker $R/apex/com.android.runtime/bin/linkerconfig $R/vendor/bin/mnld
	mnt "--bind /dev" $R/dev
	mnt "-t proc proc" $R/proc
	mnt "-t sysfs sysfs" $R/sys
	mkdir -p $R/mnt/vendor/nvdata $R/mnt/vendor/nvcfg
	mountpoint -q $R/mnt/vendor/nvdata || mount -o ro,noload /dev/disk/by-partlabel/nvdata $R/mnt/vendor/nvdata 2>&1 | head -1
	mountpoint -q $R/mnt/vendor/nvcfg || mount -o ro,noload /dev/disk/by-partlabel/nvcfg $R/mnt/vendor/nvcfg 2>&1 | head -1
	echo "chroot ready: $R"; mount | grep $R | awk '{print $3}'
	;;
run)
	# A simple start of mnld, log in /tmp/mnld.log (inside the chroot /tmp = $R/tmp). Ctrl-C / stop to stop.
	cd /
	LD_LIBRARY_PATH=/vendor/lib:/system/lib ANDROID_ROOT=/system ANDROID_DATA=/data \
	chroot $R /system/bin/linker /vendor/bin/mnld 2>&1 | tee /tmp/mnld.log
	;;
shell)
	chroot $R /system/bin/sh 2>/dev/null || chroot $R /bin/sh
	;;
stop)
	pkill -f "/vendor/bin/mnld" 2>/dev/null; echo stopped
	;;
teardown)
	pkill -f "/vendor/bin/mnld" 2>/dev/null
	for m in $R/mnt/vendor/nvcfg $R/mnt/vendor/nvdata $R/sys $R/proc $R/dev; do mountpoint -q $m && umount $m; done
	echo "unmounted (the tree $R is kept; rm -rf $R to delete it)"
	;;
*)
	echo "usage: sudo sh $0 {setup|run|shell|stop|teardown}"
	;;
esac
