#!/bin/sh
# M1: loading OV8856 as a mainline sensor. sudo sh m1-load.sh
D=$(dirname "$(readlink -f "$0")")
sysctl -q kernel.dmesg_restrict=0
lsmod | grep -q '^i2c_mt65xx_mch' || insmod $D/i2c-mt65xx-mch.ko
lsmod | grep -q '^cam_probe' || insmod $D/cam-probe.ko stage=1 tg_keep=1
modprobe videodev; modprobe v4l2-fwnode; modprobe v4l2-async
lsmod | grep -q '^ov8856' || insmod $D/ov8856.ko
sleep 1
echo "== dmesg"; dmesg | grep -i -E "ov8856|camera@10|cam4-|cam-mipi|i2c-mt65xx-mch.*1101a000|begonia-cam" | tail -15
echo "== driver bound?"; ls -l /sys/bus/i2c/drivers/ov8856/ 2>&1 | grep -- '->' 
echo "== runtime PM"; for d in /sys/bus/i2c/drivers/ov8856/*-0010; do cat $d/power/runtime_status 2>/dev/null; done
echo "== regulators"; for r in /sys/class/regulator/regulator.*; do n=$(cat $r/name); case "$n" in cam4*|vcamio) echo "$n $(cat $r/state)";; esac; done
