#!/usr/bin/env python3
"""Prescribe the kernel patches in source= of an APKBUILD.

usage: gen-source-list.py APKBUILD patch...

Patch file names come from the commit subjects, so the list is generated from
kernel/patches instead of being kept by hand.
"""
import os
import re
import sys

apkbuild, patches = sys.argv[1], [os.path.basename(p) for p in sys.argv[2:]]
text = open(apkbuild).read()

# drop previously listed patches
text = re.sub(r'\n\t0\d{3}-[^\n]*\.patch(?=\n)', '', text)

anchor = '\n\t$_config\n'
if text.count(anchor) != 1:
    sys.exit('anchor "$_config" not found exactly once in %s' % apkbuild)
text = text.replace(anchor, anchor + ''.join('\t%s\n' % p for p in patches), 1)

open(apkbuild, 'w').write(text)
print('source= has %d patches' % len(patches))
