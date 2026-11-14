#!/usr/bin/env python3
"""One chip-ID shot per bus (works around the MCH channel getting stuck after a NACK): sudo python3 oneshot.py <slot> <unit> <addr> <reg16> [repeats]"""
import ctypes, fcntl, glob, os, sys, time
class M(ctypes.Structure): _fields_ = [('a', ctypes.c_uint16), ('f', ctypes.c_uint16), ('l', ctypes.c_uint16), ('b', ctypes.c_void_p)]
class D(ctypes.Structure): _fields_ = [('m', ctypes.POINTER(M)), ('n', ctypes.c_uint32)]
slot, unit, addr, reg = sys.argv[1], sys.argv[2], int(sys.argv[3], 0), int(sys.argv[4], 0)
rep = int(sys.argv[5]) if len(sys.argv) > 5 else 2
bus = [p for p in glob.glob('/sys/class/i2c-dev/i2c-*') if os.path.realpath(p + '/device/of_node').endswith('i2c@' + unit)][0]
fd = os.open('/dev/' + bus.rsplit('/', 1)[1], os.O_RDWR)
for k in range(rep):
    w = ctypes.create_string_buffer(bytes([reg >> 8, reg & 0xff]), 2); r = ctypes.create_string_buffer(2)
    m = (M * 2)(M(addr, 0, 2, ctypes.addressof(w)), M(addr, 1, 2, ctypes.addressof(r))); t = time.time()
    try:
        fcntl.ioctl(fd, 0x0707, D(m, 2)); res = 'ACK id=0x%04x' % int.from_bytes(r.raw, 'big')
    except OSError as e:
        res = 'errno %d' % e.errno
    print('slot %s i2c@%s 0x%02x reg 0x%04x #%d: %s (%.0f ms)' % (slot, unit, addr, reg, k + 1, res, (time.time() - t) * 1000))
