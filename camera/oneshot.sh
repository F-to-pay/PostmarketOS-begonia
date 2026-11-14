#!/bin/sh
# One shot per bus with the slot powered. sudo sh oneshot.sh  (i2c-mt65xx-mch and cam-probe stage=1 loaded)
D=$(dirname "$(readlink -f "$0")"); P=/sys/kernel/debug/begonia-cam/power
shot() { echo "$1 on" > $P; sleep 0.3; timeout 15 python3 $D/oneshot.py "$@"; echo "$1 off" > $P; }
shot 4 1101a000 0x10 0x300b   # MAIN3 OV8856  (expect 0x885a)
shot 0 11009000 0x2d 0x0000   # MAIN  S5KGW1  (0x0971/0x0972)
shot 1 11011000 0x2d 0x0000   # SUB   S5K3T1@0x2d (0x3140/0x3141), S5K3T2 lives at 0x56
