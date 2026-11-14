import struct, math, subprocess, os, sys

fs = 48000
d = open('/tmp/ncT/raw.raw', 'rb').read()
n = len(d) // 4
x = struct.unpack('<%dh' % (n * 2), d[:n * 4])
L = [v / 32768 for v in x[0::2]]


def rms(a):
    return math.sqrt(sum(v * v for v in a) / len(a))


db = lambda v: 20 * math.log10(v + 1e-9)


def seg(a, t0, t1):
    return a[int(t0 * fs):int(t1 * fs)]


print('input (bottom):      speech %.1f dBFS, noise %.1f dBFS' % (db(rms(seg(L, 0.8, 4.8))), db(rms(seg(L, 6.0, 9.8)))))
cal = '/home/user/.config/mic-nc.cal'
for label, env, extra in (
        ('as in the test (cal, α=1.4)', {}, ['--cal', cal]),
        ('no calibration c=1, α=1.4', {}, []),
        ('cal, α=1.0', {'NC_ALPHA': '1.0'}, ['--cal', cal]),
        ('cal, α=0.6', {'NC_ALPHA': '0.6'}, ['--cal', cal]),
        ('c=1, α=0.6, gmin 0.3', {'NC_ALPHA': '0.6', 'NC_GMIN': '0.3'}, [])):
    e = dict(os.environ)
    e.update(env)
    p = subprocess.run(['/usr/local/bin/mic-nc-dsp'] + extra, input=d, capture_output=True, env=e)
    o = p.stdout
    m = len(o) // 2
    y = [v / 32768 for v in struct.unpack('<%dh' % m, o[:m * 2])]
    sp, nz = rms(seg(y, 0.8, 4.8)), rms(seg(y, 6.0, 9.8))
    sr, nr = rms(seg(L, 0.8, 4.8)), rms(seg(L, 6.0, 9.8))
    print('%-28s speech %6.1f (%+5.1f dB)  noise %6.1f (%+5.1f dB)   speech/noise gain %+5.1f dB' % (
        label, db(sp), db(sp) - db(sr), db(nz), db(nz) - db(nr), (db(sp) - db(nz)) - (db(sr) - db(nr))))
