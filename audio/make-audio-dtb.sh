#!/bin/sh
# Builds /home/user/begonia-audio.dtb = the decompiled begonia-wifi.dtb (vtp+touch+bat+prox+consys+wifi) + audio nodes.
# Does not need root. Run: sh this_file
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
KDIR=${KDIR:-$HOME/mt6785-linux}
BASE=${BASE:-$HOME/begonia-wifi.dtb}
OUT=${OUT:-$HOME/begonia-audio.dtb}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

dtc -q -I dtb -O dts "$BASE" > "$W/base.dts" 2>/dev/null
python3 -I - "$W/base.dts" "$W/labeled.dts" <<'PY'
import sys,re
src,dst=sys.argv[1:3]
lines=open(src).read().split('\n')
# node path (without /) -> label
want={'oscillator-26m':'clk26m',
      'soc/clock-controller@11210000':'audsys',
      'soc/clock-controller@10000000':'topckgen',
      'soc/clock-controller@10001000':'infracfg',
      'soc/clock-controller@1000c000':'apmixedsys',
      'soc/pinctrl@10005000':'pio',
      'soc/syscon@10006000/power-controller':'spm'}
stack=[];done=set()
for i,l in enumerate(lines):
    s=l.strip()
    if s.endswith('{') and not s.startswith('/*'):
        name=s[:-1].strip()
        if name=='/': stack=[]; stack.append('/'); continue
        stack.append(name)
        path='/'.join(stack[1:])
        if path in want:
            ind=l[:len(l)-len(l.lstrip())]
            lines[i]=f'{ind}{want[path]}: {name} {{'
            done.add(path)
    elif s=='};':
        stack.pop()
miss=set(want)-done
assert not miss,f'nodes not found: {miss}'
open(dst,'w').write('\n'.join(lines))
print('labels placed:',len(done))
PY
{ cat "$W/labeled.dts"; echo; cat "$HERE/audio-begonia.dtsi"; } > "$W/full.dts"
cpp -nostdinc -undef -x assembler-with-cpp -I "$KDIR/include" -o "$W/full.pp.dts" "$W/full.dts"
dtc -q -I dts -O dtb -o "$OUT" "$W/full.pp.dts" 2>"$W/dtc.err" || { cat "$W/dtc.err"; exit 1; }
grep -v -E "unit_address|avoid_unnecessary|simple_bus|graph_|clocks_property|gpios_property" "$W/dtc.err" | head -20 || true
ls -l "$OUT"
dtc -q -I dtb -O dts "$OUT" > "$W/check.dts"
echo "nodes: afe=$(grep -c 'mediatek,mt6785-audio' "$W/check.dts") sound=$(grep -c 'mediatek,mt6785_mt6359' "$W/check.dts") lines: before $(wc -l < "$W/base.dts"), after $(wc -l < "$W/check.dts")"
