mount -t debugfs none /sys/kernel/debug 2>/dev/null
echo "=== of_node spi5.0"; 
N=$(readlink -f /sys/bus/spi/devices/spi5.0/of_node); echo $N
for f in $(ls $N); do if [ -f $N/$f ]; then printf "%s = " $f; (cat $N/$f | tr '\0' ' ' | head -c 160; echo; echo -n "   hex: "; od -An -tx1 -v $N/$f | head -3 | tr -d '\n'; echo); fi; done
echo "=== of_node subdirs"; ls -la $N
echo "=== spi5 master of_node"; M=$(readlink -f /sys/bus/spi/devices/spi5.0/../../of_node 2>/dev/null); ls $M 2>&1 | head -30
echo "=== regulator summary"; cat /sys/kernel/debug/regulator/regulator_summary 2>&1 | head -80
echo "=== gpio"; cat /sys/kernel/debug/gpio 2>&1 | grep -E '(gpio-(35|36|37|38|44|2|1) |tp|touch|nvt|rst|irq)' | head -30
echo "=== pinctrl"; ls /sys/kernel/debug/pinctrl 2>&1 | head
echo "=== vendor fw"; mkdir -p /tmp/venm; mount -o ro /dev/block/platform/bootdevice/by-name/vendor /tmp/venm 2>&1; ls -la /tmp/venm/firmware 2>&1 | grep -iE 'nvt|novatek|nt36|ts_' ; md5sum /tmp/venm/firmware/novatek_ts_fw.bin 2>&1; sha256sum /tmp/venm/firmware/novatek_ts_fw.bin 2>&1
