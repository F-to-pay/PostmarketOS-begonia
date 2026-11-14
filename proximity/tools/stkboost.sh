#!/bin/sh
# raise the sensitivity of the stk3310 (default: gain x1, shortest integration) and repeat the cover test; needs root
D=
for d in /sys/bus/iio/devices/iio:device*; do [ "$(cat $d/name)" = stk3310 ] && D=$d; done
[ -n "$D" ] || { echo "stk3310 not found"; exit 1; }
echo 0.094720 > $D/in_proximity_integration_time
echo 0.1 > $D/in_proximity_scale
echo 0.094720 > $D/in_illuminance_integration_time
echo 0.1 > $D/in_illuminance_scale
echo "ps it=$(cat $D/in_proximity_integration_time) scale=$(cat $D/in_proximity_scale); als it=$(cat $D/in_illuminance_integration_time) scale=$(cat $D/in_illuminance_scale)"
read_all() { echo "prox=$(cat $D/in_proximity_raw) light=$(cat $D/in_illuminance_raw)"; }
echo "idle (point the top of the phone at a lamp/window):"; for i in 1 2 3 4; do read_all; sleep 1; done
echo ">>> in 4 s: cover the sensor (top of the screen, next to the earpiece) with your hand and keep it there for 6 s"
sleep 4
for i in 1 2 3 4 5 6; do read_all; sleep 1; done
echo ">>> take the hand away"
sleep 2
for i in 1 2 3 4; do read_all; sleep 1; done
