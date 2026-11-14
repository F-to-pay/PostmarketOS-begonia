#!/usr/bin/env python3
"""One chip-ID shot for the 2 MP sensors (8-bit registers): sudo python3 oneshot8.py <slot> <unit> <ov|gc> [repeats]
ov = OV02A10 @0x3d: write 0xfd=0x00, then read 0x02..0x03 (expect 0x2509 alpha / 0x2508 beta / 0x250a AF)
gc = GC2375 @0x17: read 0xf0..0xf1 (expect 0x2375 / 0x2374 / 0x2376)
g5 = GC5035 @0x37: read 0xf0..0xf1 (expect 0x5035)"""
import ctypes, fcntl, glob, os, sys, time
class M(ctypes.Structure): _fields_ = [('a', ctypes.c_uint16), ('f', ctypes.c_uint16), ('l', ctypes.c_uint16), ('b', ctypes.c_void_p)]
class D(ctypes.Structure): _fields_ = [('m', ctypes.POINTER(M)), ('n', ctypes.c_uint32)]
slot, unit, kind = sys.argv[1], sys.argv[2], sys.argv[3]
rep = int(sys.argv[4]) if len(sys.argv) > 4 else 2
addr, reg = {'ov': (0x3d, 0x02), 'gc': (0x17, 0xf0), 'g5': (0x37, 0xf0)}[kind]
bus = [p for p in glob.glob('/sys/class/i2c-dev/i2c-*') if os.path.realpath(p + '/device/of_node').endswith('i2c@' + unit)][0]
fd = os.open('/dev/' + bus.rsplit('/', 1)[1], os.O_RDWR)
def x(msgs):
    fcntl.ioctl(fd, 0x0707, D((M * len(msgs))(*msgs), len(msgs)))
for k in range(rep):
    t = time.time()
    try:
        if kind == 'ov':
            pg = ctypes.create_string_buffer(b'\xfd\x00', 2); x([M(addr, 0, 2, ctypes.addressof(pg))])
        w = ctypes.create_string_buffer(bytes([reg]), 1); r = ctypes.create_string_buffer(2)
        x([M(addr, 0, 1, ctypes.addressof(w)), M(addr, 1, 2, ctypes.addressof(r))])
        res = 'ACK id=0x%04x' % int.from_bytes(r.raw, 'big')
    except OSError as e:
        res = 'errno %d' % e.errno
    print('slot %s i2c@%s %s 0x%02x #%d: %s (%.0f ms)' % (slot, unit, kind, addr, k + 1, res, (time.time() - t) * 1000))
