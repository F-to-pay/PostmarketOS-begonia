#!/bin/sh
# orientation check: hold a finger on each screen corner in turn, events are recorded per phase
grep -q "spi1.0" /proc/bus/input/devices || { echo "touch driver not loaded"; exit 1; }
for p in 1:TOP-LEFT 2:TOP-RIGHT 3:BOTTOM-LEFT 4:BOTTOM-RIGHT 5:CENTER; do
	n=${p%%:*}; name=${p#*:}
	echo ">>> in 3 s: put ONE finger on the $name corner of the screen and keep it there for 5 s <<<"
	sleep 3
	echo "GO ($name)"
	timeout 5 cat /dev/input/event3 > /tmp/ev_$n.bin
	chmod a+r /tmp/ev_$n.bin
	echo "done $name ($(( $(wc -c < /tmp/ev_$n.bin) / 24 )) events)"
done
