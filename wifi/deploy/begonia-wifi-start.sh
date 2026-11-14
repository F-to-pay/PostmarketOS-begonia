#!/bin/sh
# Autostart of the CONSYS WiFi/BT at boot (called from begonia-wifi.service).
# Reboot loop protection: if the previous run did not finish (the "starting" marker remained),
# autostart is disabled ("disabled") until a manual intervention: sudo rm /var/lib/begonia/disabled
S=/var/lib/begonia
mkdir -p "$S"
if [ -e "$S/disabled" ]; then
	echo "begonia-wifi: autostart disabled by the protection ($S/disabled), skipping. Enable: sudo rm $S/disabled"
	exit 0
fi
if [ -e "$S/starting" ]; then
	echo "begonia-wifi: the previous run did not finish -> disabling autostart ($S/disabled)"
	touch "$S/disabled"; rm -f "$S/starting"; sync
	exit 0
fi
touch "$S/starting"; sync
sh /home/user/wifi-up.sh
rc=$?
rm -f "$S/starting"; sync
exit $rc
