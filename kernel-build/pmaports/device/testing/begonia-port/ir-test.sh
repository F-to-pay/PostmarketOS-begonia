#!/bin/sh
# IR transmitter check: the IR LED is INVISIBLE TO THE EYE, look at it through the camera of ANOTHER phone/a webcam
# (the LED is on the top edge of the phone; in a camera it glows purple/white). It blinks for 6 seconds: 0.3 s on, 0.3 s off.
# Every burst is a separate ir-ctl call: the kernel (lirc) rejects (EINVAL) a single write with a total duration of seconds.
# Usage: ir-test [carrier_Hz, default 38000]
C=${1:-38000}
echo "IR transmitter: carrier $C Hz. Point the camera of another phone at the TOP edge. Starting in 3 s..."
sleep 3
F=/tmp/ir-test.txt
echo "pulse 300000" > "$F"
echo "blinking for 6 seconds (10 bursts of 0.3 s)..."
i=0
ok=0
while [ $i -lt 10 ]; do
	sudo ir-ctl -d /dev/lirc0 --carrier=$C --duty-cycle=33 --send="$F" 2>/dev/null && ok=$((ok + 1))
	sleep 0.3
	i=$((i + 1))
done
echo "done: $ok of 10 bursts sent (if it glowed in the camera: the IR transmitter works)"
