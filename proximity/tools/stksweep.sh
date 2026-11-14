#!/bin/sh
# sweep the stk3310 proximity settings: for each combo 3 s hand AWAY, then 3 s hand ON the sensor; needs root
D=
for d in /sys/bus/iio/devices/iio:device*; do [ "$(cat $d/name)" = stk3310 ] && D=$d; done
[ -n "$D" ] || { echo "stk3310 not found"; exit 1; }
avg() { s=0; n=0; for v in "$@"; do s=$((s + v)); n=$((n + 1)); done; echo $((s / n)); }
sample3() { a=""; for i in 1 2 3; do sleep 1; a="$a $(cat $D/in_proximity_raw)"; done; echo $a; }
echo "Follow the prompts: AWAY = hand far from the phone, COVER = palm flat on the top of the screen (sensor)."
echo ">>> starting in 5 s, hand away"
sleep 5
echo "scale  it(s)      away(avg)  cover(avg)  [raw samples]"
for combo in "0.4 0.000185" "0.1 0.000185" "0.4 0.000741" "0.1 0.000741" "0.4 0.002960" "0.1 0.002960" "1.6 0.011840" "0.4 0.011840"; do
	set -- $combo
	echo $1 > $D/in_proximity_scale
	echo $2 > $D/in_proximity_integration_time
	echo "  .. AWAY"
	sleep 1
	a=$(sample3)
	echo "  .. COVER"
	sleep 1
	c=$(sample3)
	echo "$1  $2   away=$(avg $a)  cover=$(avg $c)   [$a |$c ]"
done
echo done
