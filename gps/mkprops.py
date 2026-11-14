#!/usr/bin/env python3
"""Generator of an Android property file (bionic prop_area, format 'single file', Android 8-11) for the chroot with mnld.

mnld waits for vendor.service.nvram_init == "Ready" (on a real Android nvram_daemon sets them). We have no properties:
property_service is not running, /dev/__properties__ does not exist. In this case bionic (the path is a plain file, not a directory)
reads a single prop_area (ContextsPreSplit) without property_info/contexts.

usage: mkprops.py /dev/__properties__ name=value [name=value ...]
Format (bionic/libc/system_properties/prop_area.{h,cpp}):
  a 128-byte header: bytes_used u32, serial u32, magic 'PORP' 0x504f5250, version 0xfc6ed0ab, reserved[28]
  data: the root prop_bt (20 bytes: namelen, prop, left, right, children + name), the nodes are aligned to 4;
  prop_info: serial (len<<24) u32 + value[92] + name; prop_bt.prop holds the offset of prop_info relative to data.
"""
import struct
import sys
import os

PA_SIZE = 128 * 1024
HDR = 128
MAGIC = 0x504F5250
VERSION = 0xFC6ED0AB


class Area:
    def __init__(self):
        self.data = bytearray(PA_SIZE - HDR)
        self.used = 20  # root: sizeof(prop_bt) = 5 * u32, empty name

    def alloc(self, size):
        aligned = (size + 3) & ~3
        off = self.used
        if off + aligned > len(self.data):
            raise SystemExit("prop_area overflow")
        self.used += aligned
        return off

    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def set32(self, off, v):
        struct.pack_into("<I", self.data, off, v)

    def new_bt(self, name):
        nb = name.encode()
        off = self.alloc(20 + len(nb) + 1)
        struct.pack_into("<IIIII", self.data, off, len(nb), 0, 0, 0, 0)
        self.data[off + 20:off + 20 + len(nb)] = nb
        return off

    @staticmethod
    def cmp(a, b):
        """cmp_prop_name: length first, then strncmp"""
        a, b = a.encode(), b.encode()
        if len(a) != len(b):
            return -1 if len(a) < len(b) else 1
        return (a > b) - (a < b)

    def bt_name(self, off):
        n = self.u32(off)
        return bytes(self.data[off + 20:off + 20 + n]).decode()

    def find_child(self, parent, name):
        """finds/creates the node name among the children of parent (a binary tree of siblings: left/right)"""
        cur_off_field = parent + 16  # children
        while True:
            cur = self.u32(cur_off_field)
            if cur == 0:
                new = self.new_bt(name)
                self.set32(cur_off_field, new)
                return new
            c = self.cmp(name, self.bt_name(cur))
            if c == 0:
                return cur
            cur_off_field = cur + (8 if c < 0 else 12)  # left / right

    def add(self, key, value):
        vb = value.encode()
        if len(vb) >= 92:
            raise SystemExit("value too long: " + key)
        node = 0
        for part in key.split("."):
            node = self.find_child(node, part)
        nb = key.encode()
        off = self.alloc(96 + len(nb) + 1)
        struct.pack_into("<I", self.data, off, len(vb) << 24)
        self.data[off + 4:off + 4 + len(vb)] = vb
        self.data[off + 96:off + 96 + len(nb)] = nb
        self.set32(node + 4, off)

    def dump(self):
        hdr = struct.pack("<IIII", self.used, 0, MAGIC, VERSION) + b"\0" * (28 * 4)
        return hdr + bytes(self.data)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    a = Area()
    for kv in sys.argv[2:]:
        k, _, v = kv.partition("=")
        a.add(k, v)
    out = sys.argv[1]
    if os.path.isdir(out):
        raise SystemExit(out + " - is a directory")
    with open(out, "wb") as f:
        f.write(a.dump())
    os.chmod(out, 0o444)
    print("written %s (%d properties)" % (out, len(sys.argv) - 2))


main()
