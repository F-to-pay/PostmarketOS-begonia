#!/bin/sh
# dump the stk3310 registers via regmap debugfs (needs root)
mount -t debugfs none /sys/kernel/debug 2>/dev/null
d=$(ls /sys/kernel/debug/regmap | grep -i "3-0047" | head -1)
echo "regmap dir: $d"
[ -n "$d" ] && cat /sys/kernel/debug/regmap/$d/registers
echo "--- regulators with users / names that could be the sensor supply:"
for r in /sys/class/regulator/regulator.*; do echo "$(cat $r/name) state=$(cat $r/state 2>/dev/null) users=$(cat $r/num_users 2>/dev/null) uV=$(cat $r/microvolts 2>/dev/null)"; done | head -60
