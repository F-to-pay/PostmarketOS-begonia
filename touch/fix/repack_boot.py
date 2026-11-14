#!/usr/bin/env python3
"""Replace the ramdisk of an Android boot.img (header v0-v2) and fix size + id.

usage: repack_boot.py <in.img> <ramdisk.new> <out.img>      (ramdisk.new is used as is, already compressed)
       repack_boot.py <in.img> --extract <ramdisk.out>      (dump the current ramdisk)
"""
import hashlib
import struct
import sys


def align(n, page):
    return (n + page - 1) // page * page


def parse(img):
    assert img[:8] == b'ANDROID!', 'not an Android boot image'
    ksz, kaddr, rsz, raddr, ssz, saddr, taddr, page, ver = struct.unpack_from('<9I', img, 8)
    rdtbo_sz = rdtbo_off = dtb_sz = 0
    if ver >= 1:
        rdtbo_sz, rdtbo_off, hsz = struct.unpack_from('<IQI', img, 1632)
    if ver >= 2:
        dtb_sz, = struct.unpack_from('<I', img, 1648)
    return dict(ksz=ksz, rsz=rsz, ssz=ssz, page=page, ver=ver, rdtbo_sz=rdtbo_sz, dtb_sz=dtb_sz)


def sections(img, h):
    page = h['page']
    off = page
    out = {}
    for name, size in (('kernel', h['ksz']), ('ramdisk', h['rsz']), ('second', h['ssz']),
                       ('rdtbo', h['rdtbo_sz']), ('dtb', h['dtb_sz'])):
        out[name] = img[off:off + size]
        off += align(size, page)
    assert off <= len(img) and len(img) - off < 4096 * 8, (off, len(img))
    return out


def main():
    src = open(sys.argv[1], 'rb').read()
    h = parse(src)
    sec = sections(src, h)
    tail_start = h['page'] + sum(align(len(sec[n]), h['page']) for n in sec)
    if sys.argv[2] == '--extract':
        open(sys.argv[3], 'wb').write(sec['ramdisk'])
        print('ramdisk', len(sec['ramdisk']), 'bytes, header v%d, page %d' % (h['ver'], h['page']))
        return

    if sys.argv[2] == '--dtb-extract':
        open(sys.argv[3], 'wb').write(sec['dtb'])
        print('dtb', len(sec['dtb']), 'bytes')
        return
    new_dtb = None
    args = sys.argv[1:]
    if '--dtb' in args:
        i = args.index('--dtb')
        new_dtb = open(args[i + 1], 'rb').read()
        del args[i:i + 2]
        sys.argv = [sys.argv[0]] + args
        sec['dtb'] = new_dtb
        h['dtb_sz'] = len(new_dtb)
    new_rd = open(sys.argv[2], 'rb').read() if sys.argv[2] != '-' else sec['ramdisk']
    page = h['page']
    ver = h['ver']
    hdr = bytearray(src[:page])
    struct.pack_into('<I', hdr, 16, len(new_rd))
    if ver >= 2:
        struct.pack_into('<I', hdr, 1648, len(sec['dtb']))

    sha = hashlib.sha1()
    for name, key in (('kernel', 'ksz'), ('ramdisk', None), ('second', 'ssz')):
        data = new_rd if name == 'ramdisk' else sec[name]
        sha.update(data)
        sha.update(struct.pack('<I', len(data)))
    if ver >= 1:
        sha.update(sec['rdtbo'])
        sha.update(struct.pack('<I', len(sec['rdtbo'])))
    if ver >= 2:
        sha.update(sec['dtb'])
        sha.update(struct.pack('<I', len(sec['dtb'])))
    hdr[576:608] = sha.digest().ljust(32, b'\0')

    out = bytearray(hdr)
    for name in ('kernel', 'ramdisk', 'second', 'rdtbo', 'dtb'):
        data = new_rd if name == 'ramdisk' else sec[name]
        out += data
        out += b'\0' * (align(len(data), page) - len(data))
    # keep any trailing bytes (e.g. AVB footer padding) of the original image after the sections
    out += src[tail_start:]
    open(sys.argv[3], 'wb').write(out)
    print('wrote', sys.argv[3], len(out), 'bytes; ramdisk', len(new_rd), 'id', sha.hexdigest())


main()
