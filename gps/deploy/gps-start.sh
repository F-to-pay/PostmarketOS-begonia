#!/bin/sh
# Load /dev/stpgps and /dev/gps_emi (the consys-gps module, depends on consys-bringup). sudo sh /home/user/gps-start.sh [trace]
D=$(dirname "$(readlink -f "$0")")
lsmod | grep -q '^consys_gps' && rmmod consys_gps
insmod $D/consys-gps.ko ${1:+trace=1} || { echo "insmod consys-gps FAILED (is consys_bringup loaded? reboot after updating consys-bringup.ko)"; exit 1; }
sleep 1
ls -la /dev/stpgps /dev/gps_emi
