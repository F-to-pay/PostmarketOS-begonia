#!/usr/bin/env python3
"""Adds reserved-memory 'consys-emi' and the node consys@18002000 to a DECOMPILED dts (from boot.img).

usage: patch_dts_consys.py <in.dts> <out.dts>

A decompiled dts has no labels, so the phandles are found by structure:
  - the node already has 'phandle = <..>' -> take it;
  - no -> assign a new one (max+1) and append it to the node.
Invariants are checked (assert) - so as not to get a silently broken DTB.
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src).read().split('\n')

# ---- minimal parsing of the node nesting by lines ----
nodes = []  # dict(path, start, end, depth)
stack = []
for i, ln in enumerate(lines):
    s = ln.strip()
    if s.endswith('{') and not s.startswith('/*'):
        name = s[:-1].strip() or '/'
        stack.append((name, i))
    elif s == '};':
        name, st = stack.pop()
        path = '/'.join(n for n, _ in stack) + '/' + name
        nodes.append(dict(path=path.replace('//', '/'), name=name, start=st, end=i, depth=len(stack)))
assert not stack, 'unbalanced braces'


def find(pred, what):
    r = [n for n in nodes if pred(n)]
    assert len(r) == 1, '%s: expected 1 match, got %d' % (what, len(r))
    return r[0]


used = {int(m, 16) for m in re.findall(r'phandle = <(0x[0-9a-f]+)>', '\n'.join(lines))}
next_ph = [max(used) + 1]
inserts = {}  # line index -> list of lines to insert AFTER it


def phandle_of(node):
    """Return the phandle of a node; create it if missing."""
    for j in range(node['start'] + 1, node['end']):
        m = re.fullmatch(r'\t*phandle = <(0x[0-9a-f]+)>;', lines[j].rstrip())
        # only the properties of the node itself, not of the nested ones: the nested ones have a larger indent
        if m and len(lines[j]) - len(lines[j].lstrip('\t')) == node['depth'] + 1:
            return int(m.group(1), 16)
    ph = next_ph[0]
    next_ph[0] += 1
    ind = '\t' * (node['depth'] + 1)
    inserts.setdefault(node['start'], []).append('%sphandle = <0x%x>;' % (ind, ph))
    return ph


def by_path_end(suffix):
    return lambda n: n['path'].endswith(suffix)


soc = find(lambda n: n['path'] == '/soc', 'soc')
resmem = find(lambda n: n['path'] == '/reserved-memory', 'reserved-memory')

spm = find(by_path_end('/soc/syscon@10006000/power-controller'), 'spm power-controller')
domain_conn = find(lambda n: n['path'] == spm['path'] + '/power-domain@2', 'power-domain@2 (CONN)')
# CONN == index 2 (MT6785_POWER_DOMAIN_CONN); check it by reg
assert re.search(r'reg = <0x02>;', '\n'.join(lines[domain_conn['start']:domain_conn['end']])), 'domain index'
infracfg = find(by_path_end('/soc/clock-controller@10001000'), 'infracfg')
pericfg = find(by_path_end('/soc/syscon@10003000'), 'pericfg')
topckgen = find(by_path_end('/soc/clock-controller@10000000'), 'topckgen')
pwrap = find(by_path_end('/soc/pwrap@1000d000'), 'pwrap')
rg = pwrap['path'] + '/pmic/regulators/'
vcn18 = find(lambda n: n['path'] == rg + 'ldo_vcn18', 'ldo_vcn18')
vcn33_1w = find(lambda n: n['path'] == rg + 'ldo_vcn33_1_wifi', 'ldo_vcn33_1_wifi')
vcn33_2w = find(lambda n: n['path'] == rg + 'ldo_vcn33_2_wifi', 'ldo_vcn33_2_wifi')

assert 'consys' not in '\n'.join(lines), 'already patched?'

ph = dict(
    spm=phandle_of(spm), infracfg=phandle_of(infracfg), pericfg=phandle_of(pericfg),
    topckgen=phandle_of(topckgen), pwrap=phandle_of(pwrap), vcn18=phandle_of(vcn18),
    vcn33_1w=phandle_of(vcn33_1w), vcn33_2w=phandle_of(vcn33_2w),
)
ph['emi'] = next_ph[0]
next_ph[0] += 1
ph['wifi_mem'] = next_ph[0]
next_ph[0] += 1

CLK_INFRA_CCIF4_AP = 103
POWER_DOMAIN_CONN = 2

resmem_node = [
    '\t\tconsys-emi {',
    '\t\t\tno-map;',
    '\t\t\tsize = <0x00 0x400000>;',
    '\t\t\talignment = <0x00 0x1000000>;',
    '\t\t\talloc-ranges = <0x00 0x40000000 0x00 0x80000000>;',
    '\t\t\tphandle = <0x%x>;' % ph['emi'],
    '\t\t};',
]
consys_node = [
    '\t\tconsys@18002000 {',
    '\t\t\tcompatible = "xiaomi,begonia-consys-bringup";',
    '\t\t\treg = <0x00 0x18002000 0x00 0x1000 0x00 0x180b1000 0x00 0x1000>;',
    '\t\t\treg-names = "mcu_cfg", "top_misc_off";',
    '\t\t\tpower-domains = <0x%x 0x%02x>;' % (ph['spm'], POWER_DOMAIN_CONN),
    '\t\t\tclocks = <0x%x 0x%x>;' % (ph['infracfg'], CLK_INFRA_CCIF4_AP),
    '\t\t\tclock-names = "ccif4";',
    '\t\t\tvcn18-supply = <0x%x>;' % ph['vcn18'],
    '\t\t\tvcn33_1_wifi-supply = <0x%x>;' % ph['vcn33_1w'],
    '\t\t\tvcn33_2_wifi-supply = <0x%x>;' % ph['vcn33_2w'],
    '\t\t\tmediatek,pericfg = <0x%x>;' % ph['pericfg'],
    '\t\t\tmediatek,topckgen = <0x%x>;' % ph['topckgen'],
    '\t\t\tmediatek,pwrap = <0x%x>;' % ph['pwrap'],
    '\t\t\tmemory-region = <0x%x>;' % ph['emi'],
    '\t\t};',
]

wifi_mem_node = [
    '		wifi-reserve-memory {',
    '			compatible = "shared-dma-pool";',
    '			no-map;',
    '			size = <0x00 0x300000>;',
    '			alignment = <0x00 0x1000000>;',
    '			alloc-ranges = <0x00 0x40000000 0x00 0x80000000>;',
    '			phandle = <0x%x>;' % ph['wifi_mem'],
    '		};',
]
wifi_node = [
    '		wifi@18000000 {',
    '			compatible = "mediatek,wifi";',
    '			reg = <0x00 0x18000000 0x00 0x100000>;',
    '			interrupts = <0x00 0x142 0x08>;',
    '			memory-region = <0x%x>;' % ph['wifi_mem'],
    '		};',
]

# insertions before the closing '};' of reserved-memory and soc (the line indices do not change until applied)
inserts.setdefault(resmem['end'] - 1, []).extend(resmem_node + wifi_mem_node)
inserts.setdefault(soc['end'] - 1, []).extend(consys_node + wifi_node)

out = []
for i, ln in enumerate(lines):
    out.append(ln)
    out.extend(inserts.get(i, []))
open(dst, 'w').write('\n'.join(out))
print('phandles:', {k: hex(v) for k, v in ph.items()})
print('inserted: %d lines' % (len(out) - len(lines)))
