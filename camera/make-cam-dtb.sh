#!/bin/sh
# Builds a DTB = BASE (the currently installed DTB) + camera-begonia.dtsi (camera power + I2C 2/4/7 + the cam-probe node).
# Does not need root. Works on the PC (WSL) and on the phone:
#   BASE=/path/to/current.dtb OUT=begonia-cam.dtb sh make-cam-dtb.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
KDIR=${KDIR:-$HOME/mt6785-linux}
BASE=${BASE:?set BASE=the currently installed DTB (on the phone /boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb)}
OUT=${OUT:-$HOME/begonia-cam.dtb}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
python3 "$HERE/gen-camera-dtsi.py" > "$W/camera-begonia.dtsi"
dtc -q -I dtb -O dts "$BASE" > "$W/base.dts" 2>/dev/null
python3 -I - "$W/base.dts" "$W/labeled.dts" <<'PY'
import sys
src, dst = sys.argv[1:3]
lines = open(src).read().split('\n')
want = {'soc/pinctrl@10005000': 'pio',
        'soc/clock-controller@10000000': 'topckgen',
        'soc/clock-controller@1a000000': 'camsys',
        'soc/syscon@10006000/power-controller': 'spm',
        'soc/i2c@11009000': 'i2c2',
        'soc/i2c@11011000': 'i2c4',
        'soc/i2c@1101a000': 'i2c7',
        'soc/pwrap@1000d000/pmic/regulators/ldo_vcamio': 'camvcamio',
        'soc/larb@1a001000': 'camlarb6'}
stack = []; done = set(); have = set()
for i, l in enumerate(lines):
    s = l.strip()
    if s.endswith('{') and not s.startswith('/*'):
        name = s[:-1].strip()
        if ':' in name:       # the label already exists
            have.add(name.split(':')[0].strip())
        if name == '/':
            stack = ['/']; continue
        stack.append(name.split(': ')[-1])
        path = '/'.join(stack[1:])
        if path in want and ':' not in name:
            ind = l[:len(l) - len(l.lstrip())]
            lines[i] = f'{ind}{want[path]}: {name} {{'; done.add(path)
    elif s == '};':
        stack.pop()
miss = set(want) - done
assert not miss, f'nodes not found: {miss}'
open(dst, 'w').write('\n'.join(lines)); print('labels:', len(done))
PY
grep -q "camera-probe" "$W/labeled.dts" && { echo "the camera-probe node is already in the base"; exit 1; }
{ cat "$W/labeled.dts"; echo; cat "$W/camera-begonia.dtsi"; } > "$W/full.dts"
cpp -nostdinc -undef -x assembler-with-cpp -I "$KDIR/include" -o "$W/full.pp.dts" "$W/full.dts"
dtc -q -I dts -O dtb -o "$OUT" "$W/full.pp.dts" 2>"$W/dtc.err" || { cat "$W/dtc.err"; exit 1; }
grep -v -E "unit_address|avoid_unnecessary|simple_bus|graph_|clocks_property|gpios_property" "$W/dtc.err" | head -10 || true
ls -l "$OUT"
dtc -q -I dtb -O dts "$OUT" | awk '/camera-probe/,/};/' | head -12
