#!/usr/bin/env python3
"""Lifter for AArch64 disassembly (llvm-objdump) -> readable RMW operations on registers.

Understands typical MediaTek HAL code: ldr wN,[xB,#o]; and/orr/bfi/bfxil on wN; str wN,[xB,#o].
Base pointers (xB) are tracked if loaded from object fields: ldr xB,[x0|x19|...,#field]
or from a field table. Branches and calls are printed as they are. The goal is reading by hand, not execution.

usage: hal-lift.py <objdump.txt>  (output of `llvm-objdump -d --no-show-raw-insn` of one function)
"""
import re
import sys

BASES = {}   # xreg -> base description
VAL = {}     # wreg -> value description (last RMW)
OBJ = {'x0', 'x19', 'x20', 'x21'}

ins_re = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')


def fmt(o):
    return o.replace(' ', '')


for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = ins_re.match(line)
    if not m:
        continue
    addr, op, args = m.groups()
    args = args.split('//')[0].strip()
    a = [x.strip() for x in re.split(r',(?![^\[]*\])', args)]
    if op == 'ldr' and a[0].startswith('x') and '[' in a[1]:
        mm = re.match(r'\[(x\d+|sp)(?:,\s*#(0x[0-9a-f]+|\d+))?\]', a[1])
        if mm:
            src, off = mm.group(1), mm.group(2) or '0'
            if src in BASES:
                BASES[a[0]] = '*(%s+%s)' % (BASES[src], off)
            else:
                BASES[a[0]] = '%s.f%s' % (src, off)
            continue
    if op in ('add', 'sub') and a[0].startswith('x') and a[1] in BASES and a[2].startswith('#'):
        BASES[a[0]] = '%s%s%s' % (BASES[a[1]], '+' if op == 'add' else '-', a[2][1:])
        continue
    if op == 'ldr' and a[0].startswith('w'):
        mm = re.match(r'\[(x\d+)(?:,\s*#(0x[0-9a-f]+|\d+))?\]', a[1])
        if mm:
            b = BASES.get(mm.group(1), mm.group(1))
            VAL[a[0]] = '[%s+%s]' % (b, mm.group(2) or '0')
            print('%s   rd  %-28s -> %s' % (addr, VAL[a[0]], a[0]))
            continue
    if op == 'str' and a[0].startswith('w'):
        mm = re.match(r'\[(x\d+)(?:,\s*#(0x[0-9a-f]+|\d+))?\]', a[1])
        if mm:
            b = BASES.get(mm.group(1), mm.group(1))
            print('%s   WR  [%s+%s] <- %s' % (addr, b, mm.group(2) or '0', a[0]))
            continue
    if op in ('and', 'orr', 'bfi', 'bfxil', 'eor', 'lsl', 'lsr', 'ubfx', 'mov', 'movk', 'bic'):
        print('%s       %s %s' % (addr, op, ', '.join(a)))
        continue
    if op.startswith('b') or op in ('cbz', 'cbnz', 'tbz', 'tbnz', 'cmp', 'csel', 'cset', 'ret'):
        print('%s  %s %s' % (addr, op, ', '.join(a)))
        continue
    if op in ('ldrb', 'ldrh', 'ldrsw') or (op == 'ldr' and a[0].startswith('w')):
        print('%s   %s %s' % (addr, op, ', '.join(a)))
