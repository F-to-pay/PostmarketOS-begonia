#!/bin/sh
# A full GNSS run: chroot -> mnld (background) -> HAL emulator (init/start, receiving NMEA). sudo sh /home/user/gps-run.sh [seconds, default 90]
D=$(dirname "$(readlink -f "$0")")
SEC=${1:-90}
sh $D/gps-chroot.sh setup >/dev/null 2>&1
lsmod | grep -q '^consys_gps' || sh $D/gps-start.sh >/dev/null 2>&1
pkill -f "/vendor/bin/mnld" 2>/dev/null; sleep 1
rm -f /tmp/nmea.log /opt/gps-chroot/tmp/mnld.log
echo ${POLL:-100} > /sys/module/consys_bringup/parameters/rx_poll_us
echo 1 > /sys/module/consys_gps/parameters/trace
cd /
( LD_LIBRARY_PATH=/vendor/lib:/system/lib ANDROID_ROOT=/system ANDROID_DATA=/data \
  chroot /opt/gps-chroot /system/bin/linker /vendor/bin/mnld > /tmp/mnld.log 2>&1 & )
sleep 4
python3 $D/hal_emu.py $SEC 2>&1 | tee /tmp/emu.log
pkill -f "/vendor/bin/mnld" 2>/dev/null
echo 0 > /sys/module/consys_gps/parameters/trace
echo 1000 > /sys/module/consys_bringup/parameters/rx_poll_us
echo "--- mnld.log (tail):"; tail -25 /tmp/mnld.log
echo "--- NMEA:"; wc -l /tmp/nmea.log 2>/dev/null; tail -8 /tmp/nmea.log 2>/dev/null
