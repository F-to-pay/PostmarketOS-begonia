#!/usr/bin/env python3
# Reading 32-bit registers through /dev/mem (READ ONLY). Usage: sudo python3 rd32.py 0x1001a824 [...]
import mmap, os, struct, sys
fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC)
for a in sys.argv[1:]:
    addr = int(a, 0)
    base = addr & ~0xfff
    try:
        m = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
        v = struct.unpack('<I', m[addr - base:addr - base + 4])[0]
        m.close()
        print('0x%08x = 0x%08x' % (addr, v))
    except Exception as e:
        print('0x%08x : ERROR %s' % (addr, e))
