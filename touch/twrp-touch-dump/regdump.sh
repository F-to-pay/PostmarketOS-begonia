for r in /sys/class/regulator/regulator.*; do
  n=$(cat $r/name 2>/dev/null); s=$(cat $r/state 2>/dev/null); v=$(cat $r/microvolts 2>/dev/null); u=$(cat $r/num_users 2>/dev/null); t=$(cat $r/type 2>/dev/null)
  echo "$n|$s|$v|$u|$t"
done
