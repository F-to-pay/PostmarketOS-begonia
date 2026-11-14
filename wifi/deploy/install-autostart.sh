#!/bin/sh
# Installing the autostart. Run: sudo sh /home/user/install-autostart.sh
set -e
D=$(dirname "$(readlink -f "$0")")
install -m 0644 "$D/begonia-wifi.service" /etc/systemd/system/begonia-wifi.service
chmod 0755 "$D/begonia-wifi-start.sh"
systemctl daemon-reload
systemctl enable begonia-wifi.service
rm -f /var/lib/begonia/disabled /var/lib/begonia/starting
echo "installed. Check after a reboot: systemctl status begonia-wifi --no-pager"
echo "disable:    sudo systemctl disable begonia-wifi"
