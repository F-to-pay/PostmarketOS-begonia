#!/usr/bin/env python3
"""Remove the IOMMU translation (MMU_EN, bit 0 of SMI_LARB_NONSEC_CON) from all larb6/larb7 ports - CAMSV writes to the physical address.
A temporary measure until the IOMMU is set up properly (the larb7 ports are missing in the mainline DT). sudo python3 larb-bypass.py"""
import mmap, os, struct
fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
for name, base, n in (('larb6', 0x1a001000, 31), ('larb7', 0x1a002000, 5)):
    m = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=base)
    for i in range(n):
        o = 0x380 + 4 * i
        struct.pack_into('<I', m, o, struct.unpack_from('<I', m, o)[0] & ~1)
    print(name, 'MMU_EN:', ''.join(str(struct.unpack_from('<I', m, 0x380 + 4 * i)[0] & 1) for i in range(n)))
    m.close()
