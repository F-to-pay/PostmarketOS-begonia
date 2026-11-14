import wave, struct, math, sys


def stats(path, ch):
    w = wave.open(path)
    n = w.getnframes()
    c = w.getnchannels()
    d = struct.unpack('<%dh' % (n * c), w.readframes(n))
    x = [v / 32768 for v in d[ch::c]]
    win = 480
    rm = [math.sqrt(sum(v * v for v in x[i:i + win]) / win) for i in range(0, len(x) - win, win)]
    srt = sorted(rm)
    floor = srt[len(srt) // 10] + 1e-9
    peak = max(rm) + 1e-9
    # how many windows are noticeably above the noise (in seconds)
    loud = sum(1 for v in rm if v > floor * 8) * win / 48000
    db = lambda v: 20 * math.log10(v)
    return db(floor), db(peak), db(peak / floor), loud


names = {('A', 0): 'AIN1', ('A', 1): 'AIN2', ('B', 0): 'AIN0 (main, control)', ('B', 1): 'AIN3'}
print('%-28s %9s %11s %11s %10s' % ('input', 'noise dBFS', 'clap dBFS', 'clap/noise', 'loud s'))
for r in 'AB':
    for ch in (0, 1):
        f, p, ratio, loud = stats('/tmp/ms/%s.wav' % r, ch)
        print('%-28s %9.1f %11.1f %9.1f dB %9.2f' % (names[(r, ch)], f, p, ratio, loud))
