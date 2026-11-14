#!/bin/sh
# Bluetooth experiment: only CONSYS + HCI over STP (without the WiFi driver). Run RIGHT AFTER BOOT: sudo sh /home/user/bt-test.sh
# If WiFi is needed after this - reboot (the consys-bringup module is inserted here with bt_test=1 and turns BT off at the end).
set -u
D=$(dirname "$(readlink -f "$0")")
if lsmod | grep -q '^wlan_6785_axi'; then echo "wlan already loaded - reboot first (rmmod wlan crashes the kernel)"; exit 1; fi
sysctl -q kernel.dmesg_restrict=0
rmmod consys_bringup 2>/dev/null
sleep 1
insmod $D/consys-bringup.ko stage=8 calib=3 bt_test=1 || { echo "insmod FAILED"; exit 1; }
sleep 2
dmesg | grep -i consys | grep -E "BT|HCI|FUNC-CTRL|CPUPCR after|RF cal|FAILED|failed" | tail -30
