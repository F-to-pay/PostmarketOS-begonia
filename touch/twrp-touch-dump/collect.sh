echo "=== uname"; uname -a
echo "=== /proc/bus/input/devices"; cat /proc/bus/input/devices
echo "=== event2 sysfs"; ls -la /sys/class/input/event2/device/ 2>&1; readlink -f /sys/class/input/event2/device
echo "=== spi devices"; ls -la /sys/bus/spi/devices/ 2>&1
for d in /sys/bus/spi/devices/*; do echo "--- $d"; ls $d; cat $d/modalias 2>/dev/null; readlink $d/driver 2>/dev/null; done
echo "=== dmesg touch"; dmesg | grep -iE 'nvt|novatek|nt3667|touch|tp_|spi1|11019000|lcm|panel|gt9886|goodix|tddi|resx' | cut -c1-230
echo "=== lsmod"; lsmod 2>&1 | head -30
echo "=== mount debugfs"; mount -t debugfs none /sys/kernel/debug 2>&1; ls /sys/kernel/debug | head -40
