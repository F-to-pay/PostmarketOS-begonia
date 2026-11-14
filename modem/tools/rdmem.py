#!/usr/bin/env python3
# Reading a block of physical memory through /dev/mem (READ ONLY). sudo python3 rdmem.py 0xADDR [length=0x100]
import mmap, os, sys
addr = int(sys.argv[1], 0); n = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x100
fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC)
base = addr & ~0xfff; off = addr - base
m = mmap.mmap(fd, (off + n + 0xfff) & ~0xfff, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
d = m[off:off + n]
for i in range(0, len(d), 16):
    row = d[i:i + 16]
    print('%010x  %-47s  %s' % (addr + i, ' '.join('%02x' % b for b in row), ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)))
