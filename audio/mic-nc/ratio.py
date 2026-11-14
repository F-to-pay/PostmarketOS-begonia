import struct, math
fs = 48000
d = open('/tmp/ncT/raw.raw', 'rb').read()
n = len(d) // 4
x = struct.unpack('<%dh' % (n * 2), d[:n * 4])
L = [v / 32768 for v in x[0::2]]   # bottom
R = [v / 32768 for v in x[1::2]]   # top


def goertzel(s, f):
    k = 2 * math.cos(2 * math.pi * f / fs)
    s1 = s2 = 0.0
    for v in s:
        s0 = v + k * s1 - s2
        s2 = s1
        s1 = s0
    return (s1 * s1 + s2 * s2 - k * s1 * s2) / len(s) ** 2 * 4   # power


def seg(a, t0, t1):
    return a[int(t0 * fs):int(t1 * fs)]


bands = [150, 250, 400, 600, 900, 1300, 1900, 2700, 4000, 6000]
print('frequency   speech: bottom/top(dB)   noise: bottom/top(dB)   | bottom speech dB, top speech dB')
for f in bands:
    ps_l = goertzel(seg(L, 0.8, 4.8), f)
    ps_r = goertzel(seg(R, 0.8, 4.8), f)
    pn_l = goertzel(seg(L, 6.0, 9.8), f)
    pn_r = goertzel(seg(R, 6.0, 9.8), f)
    db = lambda v: 10 * math.log10(v + 1e-18)
    print('%5d Hz   %+6.1f              %+6.1f             | %6.1f   %6.1f' % (
        f, db(ps_l) - db(ps_r), db(pn_l) - db(pn_r), db(ps_l), db(ps_r)))
rm = lambda a: math.sqrt(sum(v * v for v in a) / len(a))
db20 = lambda v: 20 * math.log10(v + 1e-9)
print()
print('rms speech: bottom %.1f, top %.1f dBFS;  noise: bottom %.1f, top %.1f dBFS' % (
    db20(rm(seg(L, 0.8, 4.8))), db20(rm(seg(R, 0.8, 4.8))), db20(rm(seg(L, 6.0, 9.8))), db20(rm(seg(R, 6.0, 9.8)))))
