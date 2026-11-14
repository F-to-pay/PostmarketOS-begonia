import wave, struct, math


def load(path, ch):
    w = wave.open(path)
    n = w.getnframes()
    c = w.getnchannels()
    d = struct.unpack('<%dh' % (n * c), w.readframes(n))
    return [v / 32768 for v in d[ch::c]]


def rms_series(x, win=480):
    return [math.sqrt(sum(v * v for v in x[i:i + win]) / win) for i in range(0, len(x) - win, win)]


db = lambda v: 20 * math.log10(v + 1e-9)
names = {('A', 0): 'AIN1', ('A', 1): 'AIN2', ('B', 0): 'AIN0 (main)', ('B', 1): 'AIN3'}
print('%-17s %7s %10s %10s %14s' % ('input', 'noise', 'peak TOP', 'peak BOTTOM', 'top-bottom, dB'))
for r in 'AB':
    for ch in (0, 1):
        x = load('/tmp/ms/%s.wav' % r, ch)
        rm = rms_series(x)
        per = len(rm) / 11.0
        top = rm[int(0.8 * per):int(5.2 * per)]
        bot = rm[int(6.3 * per):int(10.7 * per)]
        floor = sorted(rm)[len(rm) // 10] + 1e-9
        e = lambda a: math.sqrt(sum(v * v for v in a) / len(a))
        print('%-17s %7.1f %10.1f %10.1f %12.1f' % (names[(r, ch)], db(floor), db(max(top)), db(max(bot)), db(e(top)) - db(e(bot))))
# correlation of the channels in a round (a similar signal = the same microphone/pickup)
for r in 'AB':
    a = rms_series(load('/tmp/ms/%s.wav' % r, 0))
    b = rms_series(load('/tmp/ms/%s.wav' % r, 1))
    n = min(len(a), len(b))
    ma = sum(a[:n]) / n
    mb = sum(b[:n]) / n
    num = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    den = math.sqrt(sum((v - ma) ** 2 for v in a[:n]) * sum((v - mb) ** 2 for v in b[:n])) + 1e-12
    print('round %s: correlation of the L/R envelopes = %.2f' % (r, num / den))
