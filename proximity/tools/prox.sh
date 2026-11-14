#!/bin/sh
# proximity/light check: no sudo needed; finds the stk3310 iio device by name (the number changes between boots)
D=
for d in /sys/bus/iio/devices/iio:device*; do [ "$(cat $d/name)" = stk3310 ] && D=$d; done
[ -n "$D" ] || { echo "stk3310 iio device not found"; exit 1; }
echo "using $D"
read_all() { echo "prox=$(cat $D/in_proximity_raw) light=$(cat $D/in_illuminance_raw) nearlevel=$(cat $D/in_proximity_nearlevel)"; }
echo "idle (point the top of the phone at a lamp/window):";  for i in 1 2 3 4; do read_all; sleep 1; done
echo ">>> in 4 s: cover the sensor (top of the screen, next to the earpiece) with your hand/finger and keep it there for 6 s"
sleep 4
for i in 1 2 3 4 5 6; do read_all; sleep 1; done
echo ">>> take the hand away"
sleep 2
for i in 1 2 3 4; do read_all; sleep 1; done
