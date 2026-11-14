import struct, math
fs = 48000


def load(p, ch, n):
    d = open(p, 'rb').read()
    cnt = len(d) // (2 * n)
    return [v / 32768 for v in struct.unpack('<%dh' % (cnt * n), d[:cnt * n * 2])][ch::n]


def rms(a):
    return math.sqrt(sum(v * v for v in a) / max(1, len(a)))


db = lambda v: 20 * math.log10(v + 1e-9)
low = load('/tmp/ncT/l_raw.raw', 0, 2)
top = load('/tmp/ncT/l_raw.raw', 1, 2)
nc = load('/tmp/ncT/l_nc.raw', 0, 1)
print('t,s   bottom  top     suppressed')
for i in range(20):
    a, b = int(i * 0.5 * fs), int((i + 1) * 0.5 * fs)
    print('%4.1f %7.1f %7.1f %8.1f' % (i * 0.5, db(rms(low[a:b])), db(rms(top[a:b])), db(rms(nc[a:b]))))
