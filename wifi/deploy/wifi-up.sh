#!/bin/sh
# Bring up the begonia WiFi after BOOT. Run: sudo sh /home/user/wifi-up.sh
# IMPORTANT: wlan_6785_axi must NOT be unloaded (rmmod crashes the kernel: wiphy_unregister NULL + hif_thread) - only a reboot.
set -u
D=$(dirname "$(readlink -f "$0")")
if lsmod | grep -q '^wlan_6785_axi'; then
	echo "wlan_6785_axi is already loaded. It cannot be unloaded (oops). Reboot the phone to restart it."
	ip -br link | grep -E "wlan|usb0"
	exit 1
fi
sysctl -q kernel.dmesg_restrict=0
rmmod consys_bringup 2>/dev/null
sleep 1
cp -n $D/connsys-fw/wifi.cfg $D/connsys-fw/txpowerctrl.cfg /lib/firmware/ 2>/dev/null
modprobe cfg80211 || { echo "modprobe cfg80211 FAILED"; exit 1; }
modprobe bluetooth || echo "modprobe bluetooth FAILED (hci0 will not appear)"
H=$(md5sum /etc/machine-id | cut -c1-6); BD="00:00:46:$(echo $H | cut -c1-2):$(echo $H | cut -c3-4):$(echo $H | cut -c5-6)"
insmod $D/consys-bringup.ko stage=8 calib=3 bt_hci=1 bdaddr=$BD || { echo "insmod consys-bringup FAILED"; exit 1; }
sleep 1
insmod $D/wlan_6785_axi.ko || { echo "insmod wlan FAILED"; dmesg | tail -15; exit 1; }
sleep 4
ip link set wlan0 up 2>&1
echo "--- interfaces:"; ip -br link | grep -E "wlan|usb0"
echo "--- hci:"; dmesg | grep -E "registered hci|hci register" | tail -2; echo "--- FW_START:"; dmesg | grep -E "FW_START EVT|Ready bit" | tail -2
