#!/bin/sh
# repeat: bottom-left, then bottom-right, one finger, lift it between the phases
for p in 3:BOTTOM-LEFT 4:BOTTOM-RIGHT 6:TOP-LEFT; do
	n=${p%%:*}; name=${p#*:}
	echo ">>> LIFT your finger. In 4 s put ONE finger on the $name corner and keep it there for 5 s <<<"
	sleep 4
	echo "GO ($name)"
	timeout 5 cat /dev/input/event3 > /tmp/ev_$n.bin
	chmod a+r /tmp/ev_$n.bin
	echo "done $name ($(( $(wc -c < /tmp/ev_$n.bin) / 24 )) events)"
done
