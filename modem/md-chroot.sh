#!/bin/sh
# Android chroot for the vendor ccci_fsd (32-bit ARM, Android 11). sudo sh md-chroot.sh {setup|run|shell|stop|teardown}
# nvdata is read-only by default: NVDATA_RW=1 sudo sh md-chroot.sh setup  -> read-write (ccci_fsd writes to /mnt/vendor/nvdata/md)
R=/opt/md-chroot
D=$(dirname "$(readlink -f "$0")")
mnt() { mountpoint -q "$2" || mount $1 "$2"; }
case "$1" in
setup)
	mkdir -p $R && tar xzf $D/md-chroot.tgz -C $R
	chmod 755 $R/apex/com.android.runtime/bin/linker $R/apex/com.android.runtime/bin/linkerconfig $R/vendor/bin/ccci_fsd $R/vendor/bin/ccci_rpcd
	mkdir -p $R/dev $R/proc $R/sys $R/mnt/vendor/nvdata $R/mnt/vendor/nvcfg $R/data
	mnt "--bind /dev" $R/dev
	mnt "-t proc proc" $R/proc
	mnt "-t sysfs sysfs" $R/sys
	O=ro,noload; [ "$NVDATA_RW" = 1 ] && O=rw
	if [ -n "$NVDATA_COPY" ]; then
		# safe mode: work with a COPY of the nvdata partition (loop, rw); the real partition is not touched
		[ -f "$NVDATA_COPY" ] || dd if=/dev/disk/by-partlabel/nvdata of="$NVDATA_COPY" bs=1M 2>&1 | tail -1
		LOOP=$(losetup -j "$NVDATA_COPY" | cut -d: -f1 | head -1); [ -n "$LOOP" ] || LOOP=$(losetup -f --show "$NVDATA_COPY")
		mountpoint -q $R/mnt/vendor/nvdata || mount -o rw $LOOP $R/mnt/vendor/nvdata 2>&1 | head -1
		O="rw (COPY $NVDATA_COPY on $LOOP)"
	else
		mountpoint -q $R/mnt/vendor/nvdata || mount -o $O /dev/disk/by-partlabel/nvdata $R/mnt/vendor/nvdata 2>&1 | head -1
	fi
	mountpoint -q $R/mnt/vendor/nvcfg || mount -o ro,noload /dev/disk/by-partlabel/nvcfg $R/mnt/vendor/nvcfg 2>&1 | head -1
	# protect1/protect2 = the MD backup NVRAM (/mnt/vendor/protect_f, protect_s); with NVDATA_COPY - working rw copies too
	for pair in protect1:protect_f protect2:protect_s; do
		part=${pair%%:*}; dir=${pair##*:}
		mkdir -p $R/mnt/vendor/$dir
		if [ -n "$NVDATA_COPY" ]; then
			img=$(dirname "$NVDATA_COPY")/$part-copy.img
			[ -f "$img" ] || dd if=/dev/disk/by-partlabel/$part of="$img" bs=1M 2>&1 | tail -1
			lp=$(losetup -j "$img" | cut -d: -f1 | head -1); [ -n "$lp" ] || lp=$(losetup -f --show "$img")
			mountpoint -q $R/mnt/vendor/$dir || mount -o rw $lp $R/mnt/vendor/$dir 2>&1 | head -1
		else
			mountpoint -q $R/mnt/vendor/$dir || mount -o ro,noload /dev/disk/by-partlabel/$part $R/mnt/vendor/$dir 2>&1 | head -1
		fi
	done
	echo "chroot ready: $R (nvdata: $O)"; mount | grep $R | awk '{print $3, $6}'
	;;
run)
	# ccci_fsd 0 = MD1. Log (if strace is available - a trace of the file accesses) in /tmp/ccci_fsd.log
	cd /
	export LD_LIBRARY_PATH=/vendor/lib:/system/lib ANDROID_ROOT=/system ANDROID_DATA=/data
	if command -v strace >/dev/null 2>&1; then
		strace -f -tt -s 200 -e trace=openat,open,read,write,ioctl,mkdir,stat,fstat,access,unlink,rename,lseek,close,mount \
			-o /tmp/ccci_fsd.strace chroot $R /system/bin/linker /vendor/bin/ccci_fsd 0 >/tmp/ccci_fsd.log 2>&1
	else
		chroot $R /system/bin/linker /vendor/bin/ccci_fsd 0 >/tmp/ccci_fsd.log 2>&1
	fi
	;;
run-rpc)
	# ccci_rpcd: the MD userspace RPC (Android properties), property_get replaced by an LD_PRELOAD stub
	cd /
	export LD_LIBRARY_PATH=/vendor/lib:/system/lib ANDROID_ROOT=/system ANDROID_DATA=/data LD_PRELOAD=/vendor/lib/libpropshim.so
	chroot $R /system/bin/linker /vendor/bin/ccci_rpcd >/tmp/ccci_rpcd.log 2>&1
	;;
shell)
	chroot $R /system/bin/sh 2>/dev/null || chroot $R /bin/sh
	;;
stop)
	pkill -f "[v]endor/bin/ccci_" 2>/dev/null; echo stopped
	;;
teardown)
	pkill -f "[v]endor/bin/ccci_" 2>/dev/null
	for m in $R/mnt/vendor/protect_f $R/mnt/vendor/protect_s $R/mnt/vendor/nvcfg $R/mnt/vendor/nvdata $R/sys $R/proc $R/dev; do mountpoint -q $m && umount $m; done
	[ -n "$NVDATA_COPY" ] && for f in "$NVDATA_COPY" $(dirname "$NVDATA_COPY")/protect1-copy.img $(dirname "$NVDATA_COPY")/protect2-copy.img; do for l in $(losetup -j "$f" | cut -d: -f1); do losetup -d $l; done; done
	echo "unmounted (the tree $R is kept)"
	;;
*)
	echo "usage: sudo sh $0 {setup|run|shell|stop|teardown}"
	;;
esac
