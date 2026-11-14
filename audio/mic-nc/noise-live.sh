#!/bin/sh
# Background with the suppressor running, no speech: raw bottom channel vs the suppressor output (simultaneous 6 s recording).
FMT="--format=s16le --rate=48000 --raw"
SRC=alsa_input.platform-sound.analog-stereo-in
mic-nc stop >/dev/null 2>&1
mic-nc start >/dev/null
sleep 2
echo "routing R: $(amixer -c 0 cget name='PGA_R_Mux' | tail -1), L: $(amixer -c 0 cget name='PGA_L_Mux' | tail -1)"
rm -f /tmp/nz_raw.raw /tmp/nz_nc.raw
( parec --device=$SRC $FMT --channels=2 --client-name=nz-raw 2>/dev/null | head -c 1152000 > /tmp/nz_raw.raw ) &
( parec --device=mic_nc $FMT --channels=1 --client-name=nz-nc 2>/dev/null | head -c 576000 > /tmp/nz_nc.raw ) &
wait
mic-nc stop >/dev/null
python3 - <<'PY'
import struct, math
fs = 48000
def load(p, ch, n):
    d = open(p, 'rb').read(); cnt = len(d) // (2 * n)
    return [v / 32768 for v in struct.unpack('<%dh' % (cnt * n), d[:cnt * n * 2])][ch::n]
def rms(a): return math.sqrt(sum(v * v for v in a) / max(1, len(a)))
db = lambda v: 20 * math.log10(v + 1e-9)
raw = load('/tmp/nz_raw.raw', 0, 2); top = load('/tmp/nz_raw.raw', 1, 2); nc = load('/tmp/nz_nc.raw', 0, 1)
s = lambda a: a[fs:5 * fs]
print('bottom (raw)  %.1f dBFS   top (raw) %.1f dBFS   suppressor output %.1f dBFS' % (db(rms(s(raw))), db(rms(s(top))), db(rms(s(nc)))))
# rough check for clicks/dropouts: maximum of the 10 ms windows / median
def wins(a): return [rms(a[i:i + 480]) for i in range(0, len(a) - 480, 480)]
w = sorted(wins(s(nc)))
print('output: window median %.1f dBFS, window maximum %.1f dBFS (spread %.1f dB)' % (db(w[len(w)//2]), db(w[-1]), db(w[-1]) - db(w[len(w)//2])))
PY
