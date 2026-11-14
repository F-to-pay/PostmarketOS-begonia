#!/bin/sh
# registers of the stk3310 (bus 3, addr 0x47); needs root
sh -c 'cat /sys/bus/iio/devices/iio:device*/name' >/dev/null 2>&1
/tmp/i2cdump 3 47 > /tmp/stkdump.txt 2>&1
chmod a+r /tmp/stkdump.txt
cat /tmp/stkdump.txt
