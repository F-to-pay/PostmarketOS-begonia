import math, struct, subprocess, os


def run(env_extra):
    fs = 48000
    # bottom: 1 kHz, -40 dBFS; top: quiet (-70 dBFS) => speech-like: the noise estimate from the top is almost zero, G ~ 1
    n = 2 * fs
    s = bytearray()
    for i in range(n):
        a = int(32767 * 10 ** (-40 / 20) * math.sin(2 * math.pi * 1000 * i / fs))
        b = int(32767 * 10 ** (-70 / 20) * math.sin(2 * math.pi * 1000 * i / fs + 1))
        s += struct.pack('<hh', a, b)
    env = dict(os.environ)
    env.update(env_extra)
    p = subprocess.run(['/usr/local/bin/mic-nc-dsp'], input=bytes(s), capture_output=True, env=env)
    o = p.stdout
    m = len(o) // 2
    y = struct.unpack('<%dh' % m, o[:m * 2])
    seg = y[fs // 2: fs]
    r = math.sqrt(sum(v * v for v in seg) / len(seg)) / 32768
    return 20 * math.log10(r * math.sqrt(2) + 1e-12)


for label, e in (('default (+12 dB)', {}), ('NC_GAIN_DB=0', {'NC_GAIN_DB': '0'}), ('NC_GAIN_DB=20', {'NC_GAIN_DB': '20'})):
    print('%-24s output (sine peak) %.1f dBFS   [input -40.0]' % (label, run(e)))
