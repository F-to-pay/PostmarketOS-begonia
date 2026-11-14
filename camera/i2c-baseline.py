#!/usr/bin/env python3
"""An empty address on all I2C buses: a working bus = errno 6 (NACK) in ~0 ms, a broken one = errno 110 (2 s timeout)."""
import ctypes, fcntl, glob, os, sys, time
class M(ctypes.Structure): _fields_ = [('a', ctypes.c_uint16), ('f', ctypes.c_uint16), ('l', ctypes.c_uint16), ('b', ctypes.c_void_p)]
class D(ctypes.Structure): _fields_ = [('m', ctypes.POINTER(M)), ('n', ctypes.c_uint32)]
addr = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x7a
only = sys.argv[2] if len(sys.argv) > 2 else None   # filter by the bus unit address
for p in sorted(glob.glob('/sys/class/i2c-dev/i2c-*')):
    unit = os.path.realpath(p + '/device/of_node').rsplit('/', 1)[1]
    if only and not unit.endswith(only):
        continue
    fd = os.open('/dev/' + p.rsplit('/', 1)[1], os.O_RDWR)
    b = ctypes.create_string_buffer(1); m = (M * 1)(M(addr, 1, 1, ctypes.addressof(b))); t = time.time()
    try:
        fcntl.ioctl(fd, 0x0707, D(m, 1)); r = 'ACK'
    except OSError as e:
        r = 'errno %d' % e.errno
    print('%-6s %-16s 0x%02x %-9s %5.0f ms' % (p.rsplit('/', 1)[1], unit, addr, r, (time.time() - t) * 1000))
