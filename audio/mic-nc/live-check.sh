#!/bin/sh
# Live mode without a microphone: raw.raw (stereo) is played in real time into the null-sink "ts", the pipeline reads its monitor,
# the result is recorded from the remap-source. Compared with the offline result (ref.raw).
cd /tmp/ncT || exit 1
CAL=/home/user/.config/mic-nc.cal
FMT="--format=s16le --rate=48000 --raw"
pactl load-module module-null-sink sink_name=ts sink_properties=device.description=ts >/dev/null
pactl load-module module-null-sink sink_name=lv_sink sink_properties=device.description=lv >/dev/null
pactl load-module module-remap-source master=lv_sink.monitor source_name=lv_src >/dev/null
setsid sh -c "parec --device=ts.monitor $FMT --channels=2 --latency-msec=20 --client-name=lv-pipe | /usr/local/bin/mic-nc-dsp --cal $CAL | pacat --playback --device=lv_sink $FMT --channels=1 --latency-msec=20 --client-name=lv-pipe" >/dev/null 2>&1 &
sleep 1
parec --device=lv_src $FMT --channels=1 --client-name=lv-rec 2>/dev/null | head -c 1280000 > lv.raw &
sleep 0.5
pacat --playback --device=ts $FMT --channels=2 --client-name=lv-feed < raw.raw
sleep 3
kill %1 2>/dev/null
pkill -f client-name=lv-pipe 2>/dev/null
sleep 0.3
for pat in "module-remap-source.*lv_src" "module-null-sink.*lv_sink" "module-null-sink.*sink_name=ts"; do
	for m in $(pactl list short modules | grep -E "$pat" | cut -f1); do pactl unload-module "$m"; done
done
python3 - <<'PY'
import struct, math
fs = 48000
def load(p):
    d = open(p, 'rb').read(); n = len(d) // 2
    return [v / 32768 for v in struct.unpack('<%dh' % n, d[:n * 2])]
def rms(a): return math.sqrt(sum(v * v for v in a) / max(1, len(a)))
db = lambda v: 20 * math.log10(v + 1e-9)
ref = load('/tmp/ncT/ref.raw'); lv = load('/tmp/ncT/lv.raw')
print('recorded %.2f s' % (len(lv) / fs))
# find the offset from the maximum of the envelope correlation
def env(a): return [rms(a[i:i + 2400]) for i in range(0, len(a) - 2400, 2400)]
er, el = env(ref), env(lv)
best = None
for sh in range(0, 30):
    n = min(len(er), len(el) - sh)
    if n < 20: continue
    c = sum(er[i] * el[i + sh] for i in range(n))
    if best is None or c > best[0]: best = (c, sh)
sh = best[1] * 2400
print('pipeline offset: %.2f s' % (sh / fs))
lvs = lv[sh:]
for name, a, b in (('speech', 0.8, 4.8), ('noise', 6.0, 9.8)):
    r = rms(ref[int(a * fs):int(b * fs)]); l = rms(lvs[int(a * fs):int(b * fs)])
    print('%s: offline %.1f dBFS, live pipeline %.1f dBFS (difference %+.1f dB)' % (name, db(r), db(l), db(l) - db(r)))
PY
