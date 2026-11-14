#!/usr/bin/env python3
"""Holds the GPIO of the external GNSS antenna amplifier (LNA, GPIO74 on begonia) at 1 or 0 while the process lives.
In the vendor WMT the host raises this pin when GPS is turned on (wmt_ctrl_gps_lna_set); consys-gps.ko does not do that.
Uses /dev/gpiochip0 (uapi v2, ioctl GPIO_V2_GET_LINE), libgpiod is not needed.
usage: lna.py [value 1|0, default 1] [line number, default 74]   (exit - SIGTERM/Ctrl-C)
"""
import fcntl
import os
import signal
import struct
import sys

VAL = int(sys.argv[1]) if len(sys.argv) > 1 else 1
LINE = int(sys.argv[2]) if len(sys.argv) > 2 else 74

GPIO_V2_GET_LINE_IOCTL = (3 << 30) | (592 << 16) | (0xB4 << 8) | 0x07
FLAG_OUTPUT = 1 << 3
ATTR_OUTPUT_VALUES = 2

req = bytearray(592)
struct.pack_into("<I", req, 0, LINE)                              # offsets[0]
req[256:256 + 8] = b"gps-lna\0"                                    # consumer
# config @288: flags u64, num_attrs u32, padding[5], attrs[10] {attr{id u32, pad u32, value u64}, mask u64}
struct.pack_into("<QI", req, 288, FLAG_OUTPUT, 1)
struct.pack_into("<IIQQ", req, 288 + 32, ATTR_OUTPUT_VALUES, 0, VAL, 1)
struct.pack_into("<I", req, 288 + 272, 1)                         # num_lines

fd = os.open("/dev/gpiochip0", os.O_RDWR)
fcntl.ioctl(fd, GPIO_V2_GET_LINE_IOCTL, req, True)
lfd = struct.unpack_from("<i", req, 588)[0]
print("lna: GPIO%d = %d (fd %d)" % (LINE, VAL, lfd), flush=True)
signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))
signal.pause()
