#!/bin/sh
# Probing the GPS channel: open /dev/stpgps, send PMTK commands, look at the answer. sudo sh /home/user/gps-probe.sh
[ -c /dev/stpgps ] || { echo "no /dev/stpgps (sudo sh ~/gps-start.sh)"; exit 1; }
echo 1 > /sys/module/consys_gps/parameters/trace
exec 3<>/dev/stpgps || { echo "open stpgps FAILED"; exit 1; }
echo "[open OK, GPS function ON]"
sleep 1
for c in '$PMTK605*31' '$PMTK000*32' '$PMTK605*31'; do
	printf '%s\r\n' "$c" >&3
	echo "[tx] $c"
	sleep 1
done
echo "[rx, 6 s]:"
timeout 6 cat <&3 | od -c | head -30
exec 3>&-
echo "[closed]"
echo 0 > /sys/module/consys_gps/parameters/trace
