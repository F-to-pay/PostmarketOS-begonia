#!/bin/sh
set -e
mkdir -p /etc/udev/rules.d
cp /tmp/90-stk3310.rules /etc/udev/rules.d/90-stk3310.rules
udevadm control --reload
udevadm trigger --subsystem-match=iio --action=add
sleep 1
for d in /sys/bus/iio/devices/iio:device*; do [ "$(cat $d/name)" = stk3310 ] && echo "$d ps scale=$(cat $d/in_proximity_scale) it=$(cat $d/in_proximity_integration_time)"; done
echo "installed. now reboot to the bootloader and flash the new boot image (near level 550)."
