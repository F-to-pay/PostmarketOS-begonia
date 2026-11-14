#!/usr/bin/env python3
"""show.py <file> <from>-<to> [<from>-<to> ...]  — print line ranges from ~/gen4m-port (WSL)."""
import os
import sys

root = os.path.expanduser('~/gen4m-port')
f = os.path.join(root, sys.argv[1])
lines = open(f, encoding='utf-8', errors='replace').read().split('\n')
for rng in sys.argv[2:]:
    a, b = (int(x) for x in rng.split('-'))
    print('----- %s %d-%d' % (sys.argv[1], a, b))
    for i in range(a, min(b, len(lines)) + 1):
        print('%5d  %s' % (i, lines[i - 1][:160]))
