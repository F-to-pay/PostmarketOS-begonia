mount -t debugfs none /sys/kernel/debug 2>/dev/null
B=/sys/firmware/devicetree/base
dumpnode() { n=$1; for f in $(ls $n); do [ -f $n/$f ] || continue; sz=$(wc -c < $n/$f); if [ $sz -lt 300 ]; then printf "  %s = " $f; if [ $sz -gt 0 ] && cat $n/$f | tr -d '\0' | grep -qaE '^[ -~]+$'; then cat $n/$f | tr '\0' ' '; else od -An -tx1 -v $n/$f | tr -d '\n'; fi; echo; fi; done; }
echo "=== spi5@11019000"; dumpnode $B/spi5@11019000
echo "=== spi5@11019000 children"; ls $B/spi5@11019000
echo "=== __symbols__ pmx_ts / spi5 pinctrl"; for s in pmx_ts_active pmx_ts_suspend ts_int_active ts_reset_active ts_spi_active; do [ -e $B/__symbols__/$s ] && echo "$s -> $(cat $B/__symbols__/$s | tr '\0' ' ')"; done
ls $B/__symbols__ | grep -iE 'ts_|spi5|touch|nvt|tp_' 
echo "=== pinctrl-maps (ts/spi5/nvt)"; grep -iE 'ts|spi5|nvt|touch' /sys/kernel/debug/pinctrl/pinctrl-maps | head -30
