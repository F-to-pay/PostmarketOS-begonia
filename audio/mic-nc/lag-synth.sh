#!/bin/sh
# Delay of the chain "virtual input -> mic-nc-dsp -> output" without a microphone. Variant $1: nullsink | pipe
V=${1:-nullsink}
FMT="--format=s16le --rate=48000 --raw"
CAL=/home/user/.config/mic-nc.cal
cd /tmp/ncT || exit 1
python3 - <<'PY'
import struct, wave
w = wave.open('/tmp/ncT/bursts.wav'); n = w.getnframes(); d = w.readframes(n)
x = struct.unpack('<%dh' % n, d)
st = struct.pack('<%dh' % (2 * n), *[v for s in x for v in (s, s // 40)])   # bottom = loud, top = quiet
open('/tmp/ncT/bursts_st.raw', 'wb').write(st)
PY
pactl load-module module-null-sink sink_name=ts sink_properties=device.description=ts >/dev/null
if [ "$V" = pipe ]; then
	rm -f /tmp/ls.fifo
	pactl load-module module-pipe-source source_name=ls_src file=/tmp/ls.fifo format=s16le rate=48000 channels=1 >/dev/null
	OUT=/tmp/ls.fifo; REC=ls_src
	setsid sh -c "parec --device=ts.monitor $FMT --channels=2 --latency-msec=20 --client-name=ls-pipe | /usr/local/bin/mic-nc-dsp --cal $CAL > $OUT" >/dev/null 2>&1 &
else
	pactl load-module module-null-sink sink_name=ls_sink >/dev/null
	pactl load-module module-remap-source master=ls_sink.monitor source_name=ls_src >/dev/null
	REC=ls_src
	setsid sh -c "parec --device=ts.monitor $FMT --channels=2 --latency-msec=20 --client-name=ls-pipe | /usr/local/bin/mic-nc-dsp --cal $CAL | pacat --playback --device=ls_sink $FMT --channels=1 --latency-msec=20 --client-name=ls-pipe" >/dev/null 2>&1 &
fi
sleep 1
rm -f ls_in.raw ls_out.raw
parec --device=ts.monitor $FMT --channels=2 --latency-msec=20 --client-name=ls-in > ls_in.raw 2>/dev/null &
R1=$!
parec --device=$REC $FMT --channels=1 --latency-msec=20 --client-name=ls-out > ls_out.raw 2>/dev/null &
R2=$!
sleep 0.5
pacat --playback --device=ts $FMT --channels=2 --client-name=ls-feed < bursts_st.raw
sleep 9
kill $R1 $R2 2>/dev/null
wait $R1 $R2 2>/dev/null
pkill -f client-name=ls-pipe 2>/dev/null
sleep 0.3
for pat in "module-pipe-source.*ls_src" "module-remap-source.*ls_src" "module-null-sink.*ls_sink" "module-null-sink.*sink_name=ts"; do
	for m in $(pactl list short modules | grep -E "$pat" | cut -f1); do pactl unload-module "$m"; done
done
python3 - <<'PY'
import struct, math
fs = 48000
def load(p, ch, n):
    d = open(p, 'rb').read(); cnt = len(d) // (2 * n)
    return [v / 32768 for v in struct.unpack('<%dh' % (cnt * n), d[:cnt * n * 2])][ch::n]
def rms(a): return math.sqrt(sum(v * v for v in a) / max(1, len(a)))
def onsets(x):
    e = [rms(x[i:i + 480]) for i in range(0, len(x) - 480, 480)]
    thr = max(e) * 0.25; out = []; up = False
    for i, v in enumerate(e):
        if v > thr and not up: out.append(i * 0.01); up = True
        elif v < thr * 0.5: up = False
    return out
a = onsets(load('/tmp/ncT/ls_in.raw', 0, 2)); b = onsets(load('/tmp/ncT/ls_out.raw', 0, 1))
print('input:', ['%.2f' % v for v in a][:5]); print('output:', ['%.2f' % v for v in b][:5])
if a and b: print('offsets:', ['%.2f' % (b[i] - a[i]) for i in range(min(len(a), len(b), 5))])
PY
