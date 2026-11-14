#!/bin/sh
# Run slots 0..4 one by one: on -> cam-id (candidates only) -> off. Run: sudo sh sweep.sh [slots...]
D=$(dirname "$(readlink -f "$0")")
P=/sys/kernel/debug/begonia-cam/power
SLOTS=${*:-0 1 2 3 4}
for s in $SLOTS; do
	echo "=== slot $s"
	echo "$s on" > $P || { echo "on failed"; continue; }
	sleep 0.3
	timeout 60 python3 "$D/cam-id.py" "$s"
	echo "$s off" > $P
done
cat /sys/kernel/debug/begonia-cam/state
