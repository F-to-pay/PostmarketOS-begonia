import struct, sys
b = open(sys.argv[1], 'rb').read()
cur = 0; n = 0
def cs(x): return x.split(b'\0')[0].decode('latin1')
print('%-26s %6s %6s  %s' % ('tag', 'off', 'size', 'data (u32 LE / text)'))
while cur + 76 <= len(b) and n < 200:
    name = cs(b[cur:cur+64])
    off, sz, nxt = struct.unpack('<III', b[cur+64:cur+76])
    if not name or off + sz > len(b):
        print('-- stop: cur=0x%x name=%r off=%x sz=%x nxt=%x' % (cur, name, off, sz, nxt)); break
    d = b[off:off+sz]
    if sz <= 64 and sz % 4 == 0:
        val = ' '.join('%x' % v for v in struct.unpack('<%dI' % (sz // 4), d))
    elif all(32 <= c < 127 or c == 0 for c in d):
        val = repr(cs(d))
    else:
        val = '%d bytes: ' % sz + ' '.join('%x' % v for v in struct.unpack('<%dI' % min(sz // 4, 24), d[:min(sz // 4, 24)*4]))
    print('%-26s %6x %6x  %s' % (name, off, sz, val))
    n += 1
    if nxt <= cur: print('-- next<=cur, stop'); break
    cur = nxt
print('tags:', n)
