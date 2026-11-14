B=/sys/firmware/devicetree/base/ts_novatek
dumpnode() { n=$1; for f in $(ls $n); do [ -f $n/$f ] || continue; sz=$(wc -c < $n/$f); if [ $sz -lt 200 ] && [ $sz -gt 0 ]; then printf "%s/%s = " ${n##*ts_novatek} $f; if cat $n/$f | tr -d '\0' | grep -qaE '^[ -~]+$'; then cat $n/$f | tr '\0' ' '; else od -An -tx1 -v $n/$f | tr -d '\n'; fi; echo; fi; done; }
echo "=== ts_novatek"; dumpnode $B
for c in $B/*/; do echo "=== ${c##*/}"; dumpnode ${c%/}; done
echo "=== firmware search"
for d in /vendor /tmp/venm /system /lib/firmware /firmware /odm /system/vendor; do [ -d $d ] && find $d -maxdepth 4 \( -iname 'novatek*' -o -iname 'nt36*' -o -iname '*nvt*' \) 2>/dev/null; done | head -20
ls -la /lib/firmware 2>&1 | head
