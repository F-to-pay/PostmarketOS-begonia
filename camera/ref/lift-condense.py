#!/usr/bin/env python3
"""Condenses the hal-lift.py output into a list of register: operations entries, keeping the branches. usage: lift-condense.py file.lift"""
import re
import sys

cur = None
ops = []
out = []
for l in open(sys.argv[1], encoding='utf-8').read().splitlines():
    m = re.search(r'rd  \[(.+?)\]', l)
    if m:
        cur, ops = m.group(1), []
        continue
    m = re.search(r'WR  \[(.+?)\]', l)
    if m:
        out.append('%-24s %s' % (m.group(1), '; '.join(ops) or '(=)'))
        ops = []
        continue
    m = re.search(r'\s(and|orr|bfi|bfxil|movk|bic|eor) (.*)', l)
    if m and cur:
        ops.append(m.group(1) + ' ' + re.sub(r'^w\d+, w\d+, ', '', m.group(2)))
        continue
    m = re.search(r'\s(mov) (w\d+), (#\S+)', l)
    if m:
        ops.append('%s=%s' % (m.group(2), m.group(3)))
        continue
    if re.search(r'\s(b\.\w+|tbz|tbnz|cbz|cbnz|cmp|bl) ', l):
        out.append('   ' + re.sub(r'<.*?>', '', l.strip())[:80])
print('\n'.join(out))
