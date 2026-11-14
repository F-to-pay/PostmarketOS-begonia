#!/bin/sh
# Builds ~/begonia-md.dtb = the CURRENTLY installed DTB (/boot) + the md-bringup node (md-begonia.dtsi).
# Does not need root. On the phone: sh make-md-dtb.sh   (then install-md-dtb.sh)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
KDIR=${KDIR:-$HOME/mt6785-linux}
BASE=${BASE:-/boot/mt6785-xiaomi-begonia.dtb.pre-md}  # a clean base without the md-bringup node
OUT=${OUT:-$HOME/begonia-md.dtb}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
dtc -q -I dtb -O dts "$BASE" > "$W/base.dts" 2>/dev/null
python3 -I - "$W/base.dts" "$W/labeled.dts" <<'PY'
import sys
src, dst = sys.argv[1:3]
lines = open(src).read().split('\n')
want = {'soc/syscon@10006000/power-controller': 'spm',
        'soc/clock-controller@10001000': 'mdinfra',
        'soc/pwrap@1000d000/pmic/regulators/ldo_vrf12': 'mdvrf12',
        'soc/pwrap@1000d000/pmic/regulators/ldo_vrf18': 'mdvrf18',
        'soc/pwrap@1000d000/pmic/regulators/ldo_vrfck': 'mdvrfck',
        'soc/pwrap@1000d000/pmic/regulators/ldo_vbbck': 'mdvbbck',
        'soc/pwrap@1000d000/pmic/regulators/buck_vpa': 'mdvpa',
        'soc/pwrap@1000d000/pmic/regulators/ldo_va09': 'mdva09',
        'soc/pwrap@1000d000/pmic/regulators/buck_vmodem': 'mdvmodem',
        'soc/pwrap@1000d000/pmic/regulators/ldo_vsram_md': 'mdvsram'}
stack = []; done = set()
for i, l in enumerate(lines):
    s = l.strip()
    if s.endswith('{') and not s.startswith('/*'):
        name = s[:-1].strip()
        if name == '/':
            stack = ['/']; continue
        stack.append(name)
        path = '/'.join(stack[1:])
        if path in want:
            ind = l[:len(l) - len(l.lstrip())]
            lines[i] = f'{ind}{want[path]}: {name} {{'; done.add(path)
    elif s == '};':
        stack.pop()
miss = set(want) - done
assert not miss, f'nodes not found: {miss}'
open(dst, 'w').write('\n'.join(lines)); print('labels:', len(done))
PY
grep -q "md-bringup" "$W/labeled.dts" && { echo "the md-bringup node is already in the base"; exit 1; }
{ cat "$W/labeled.dts"; echo; cat "$HERE/md-begonia.dtsi"; } > "$W/full.dts"
cpp -nostdinc -undef -x assembler-with-cpp -I "$KDIR/include" -o "$W/full.pp.dts" "$W/full.dts"
dtc -q -I dts -O dtb -o "$OUT" "$W/full.pp.dts" 2>"$W/dtc.err" || { cat "$W/dtc.err"; exit 1; }
grep -v -E "unit_address|avoid_unnecessary|simple_bus|graph_|clocks_property|gpios_property" "$W/dtc.err" | head -10 || true
ls -l "$OUT"
dtc -q -I dtb -O dts "$OUT" | awk '/md-bringup/,/};/' | head -12
