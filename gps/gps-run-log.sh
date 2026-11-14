#!/bin/sh
# A GNSS run like gps-run.sh, but with a logd stub (we read the Android log of mnld) and recording the mnld exit code.
#   sudo sh /home/user/gps-run-log.sh [seconds, default 90]
D=$(dirname "$(readlink -f "$0")")
SEC=${1:-90}
sh $D/gps-chroot.sh setup >/dev/null 2>&1
lsmod | grep -q '^consys_gps' || sh $D/gps-start.sh >/dev/null 2>&1
# Android properties: mnld waits for vendor.service.nvram_init=Ready (on a real Android nvram_daemon sets it) and persist.vendor.connsys.chipid (set by the wmt detector)
python3 $D/mkprops.py /dev/__properties__ vendor.service.nvram_init=Ready persist.vendor.connsys.chipid=0x6785
pkill -f "/vendor/bin/mnld" 2>/dev/null; pkill -f logd_stub.py 2>/dev/null; sleep 1
( dmesg -w 2>/dev/null | grep -a --line-buffered consys-gps > /tmp/kgps.log & )
rm -f /tmp/nmea.log /tmp/logd.log /tmp/mnld.rc /tmp/mnld.log /tmp/emu.log
python3 $D/logd_stub.py /tmp/logd.log > /dev/null 2>&1 &
sleep 1
echo ${POLL:-100} > /sys/module/consys_bringup/parameters/rx_poll_us
echo 1 > /sys/module/consys_gps/parameters/trace
pkill -f lna.py 2>/dev/null; python3 $D/lna.py 1 74 > /tmp/lna.log 2>&1 &   # the external LNA of the GNSS antenna
cd /
( sh -c 'LD_LIBRARY_PATH=/vendor/lib:/system/lib ANDROID_ROOT=/system ANDROID_DATA=/data chroot /opt/gps-chroot /system/bin/linker /vendor/bin/mnld; echo "rc=$? $(date +%T)" > /tmp/mnld.rc' > /tmp/mnld.log 2>&1 & )
sleep 4
python3 $D/hal_emu.py $SEC 2>&1 | tee /tmp/emu.log
pkill -f "/vendor/bin/mnld" 2>/dev/null
pkill -f logd_stub.py 2>/dev/null; pkill -f lna.py 2>/dev/null
pkill -f "dmesg -w" 2>/dev/null
echo 0 > /sys/module/consys_gps/parameters/trace
echo 1000 > /sys/module/consys_bringup/parameters/rx_poll_us
echo "--- mnld exit code:"; cat /tmp/mnld.rc 2>/dev/null
echo "--- logd (last 60 lines):"; tail -60 /tmp/logd.log
echo "--- NMEA:"; wc -l /tmp/nmea.log 2>/dev/null; tail -8 /tmp/nmea.log 2>/dev/null
