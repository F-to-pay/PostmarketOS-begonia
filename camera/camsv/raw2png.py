#!/usr/bin/env python3
"""RAW10 (CAMSV MIPI packing: 4 pixels in 5 bytes) -> 8-bit grayscale PNG (no dependencies).
usage: raw2png.py frame.bin W H [stride] out.png"""
import struct, sys, zlib
f, W, H = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
stride = int(sys.argv[4]) if len(sys.argv) > 5 else W * 10 // 8
out = sys.argv[-1]
d = open(f, 'rb').read()
rows = []
for y in range(H):
    line = d[y * stride: y * stride + W * 10 // 8]
    px = bytearray()
    for i in range(0, len(line) - 4, 5):
        px += line[i:i + 4]          # the upper 8 bits of four pixels
    rows.append(b'\x00' + bytes(px[:W]).ljust(W, b'\x00'))
def chunk(t, b):
    return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 0, 0, 0, 0)) + \
      chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b'')
open(out, 'wb').write(png)
print('ok', out, W, H, 'stride', stride)
