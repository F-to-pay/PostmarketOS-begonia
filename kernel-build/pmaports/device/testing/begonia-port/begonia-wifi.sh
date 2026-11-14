#!/bin/sh
# Brings up the begonia CONNSYS (WiFi + Bluetooth): the drivers are in the kernel now (mt6785-consys, mt6785-gen4m),
# but autoloading consys_bringup from DT is blacklisted (modprobe.d), so that the reboot loop protection works:
# if the previous run did not finish, autostart is disabled until a manual `sudo rm /var/lib/begonia/disabled`.
# wlan_6785_axi must not be unloaded (oops) - restart only with a reboot.
S=/var/lib/begonia
mkdir -p "$S"
if [ -e "$S/disabled" ]; then
	echo "begonia-wifi: autostart disabled by the protection ($S/disabled). Enable: sudo rm $S/disabled"
	exit 0
fi
if [ -e "$S/starting" ]; then
	echo "begonia-wifi: the previous run did not finish -> disabling autostart ($S/disabled)"
	touch "$S/disabled"; rm -f "$S/starting"; sync
	exit 0
fi
if grep -q '^wlan_6785_axi ' /proc/modules; then
	echo "begonia-wifi: already loaded"; exit 0
fi
touch "$S/starting"; sync

# a stable BD address from machine-id (locally administered prefix as before)
H=$(md5sum /etc/machine-id | cut -c1-6)
BD="00:00:46:$(echo "$H" | cut -c1-2):$(echo "$H" | cut -c3-4):$(echo "$H" | cut -c5-6)"

rc=0
modprobe consys_bringup bdaddr="$BD" || { echo "begonia-wifi: consys_bringup did not load"; rc=1; }
[ $rc = 0 ] && { modprobe wlan_6785_axi || { echo "begonia-wifi: wlan_6785_axi did not load"; rc=1; }; }

rm -f "$S/starting"; sync
exit $rc
