#!/bin/sh
# Measuring the real delay of the noise suppressor pipeline: a series of short "voice" bursts is played through the speaker (bottom),
# record the raw bottom channel and the suppressor output at the same time, compute the offset for each burst.
FMT="--format=s16le --rate=48000 --raw"
SRC=alsa_input.platform-sound.analog-stereo-in
pin() { # $1 client name, $2 source
	for id in $(pactl list short source-outputs | cut -f1); do
		if pactl list source-outputs | awk -v id="$id" '$0=="Source Output #"id{f=1;next} /^Source Output #/{f=0} f' | grep -q "$1"; then
			pactl move-source-output "$id" "$2" 2>/dev/null
		fi
	done
}
python3 - <<'PY'
import math, struct, wave
fs = 48000
y = []
for k in range(8):                       # 8 bursts of 0.4 s every 1.5 s
    for i in range(int(1.5 * fs)):
        t = i / fs
        if t < 0.4:
            v = 0.0
            for h in range(1, 40):
                f = 130 * h
                if f > 5000: break
                g = sum(math.exp(-((f - fc) / bw) ** 2) for fc, bw in ((700, 250), (1200, 300), (2600, 500)))
                v += g * math.sin(2 * math.pi * f * t) / h ** 0.4
            y.append(int(v * 6000 * min(1, t / 0.02, (0.4 - t) / 0.02)))
        else:
            y.append(0)
w = wave.open('/tmp/ncT/bursts.wav', 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(fs)
w.writeframes(struct.pack('<%dh' % len(y), *y)); w.close()
PY
mic-nc stop >/dev/null 2>&1
mic-nc start >/dev/null
sleep 3
# without the LSP filter (it loads the CPU): only the speaker route
HWS=$(pactl list short sinks | awk '/platform-sound/{print $2; exit}')
pactl set-sink-port "$HWS" analog-output-speaker; pactl set-sink-volume "$HWS" 100%
amixer -c 0 -q cset name='Speaker Switch' on; amixer -c 0 -q cset name='I2S3_CH1 DL1_CH1' on; amixer -c 0 -q cset name='I2S3_CH1 DL1_CH2' on; amixer -c 0 -q cset name='I2S3_CH2 DL1_CH2' on
sleep 1
HW=$(pactl list short sinks | awk '/platform-sound/ && !/hpf/ && !/mic/{print $2; exit}')
rm -f /tmp/ncT/b_raw.raw /tmp/ncT/b_nc.raw
( parec --device=$SRC $FMT --channels=2 --latency-msec=20 --client-name=bl-raw 2>/dev/null | head -c 2688000 > /tmp/ncT/b_raw.raw ) &
( parec --device=mic_nc $FMT --channels=1 --latency-msec=20 --client-name=bl-nc 2>/dev/null | head -c 1344000 > /tmp/ncT/b_nc.raw ) &
( sleep 0.4; pin bl-raw $SRC; pin bl-nc mic_nc ) &
sleep 0.8
paplay --device=$HW /tmp/ncT/bursts.wav 2>/dev/null
sleep 2.5
wait
amixer -c 0 -q cset name='Speaker Switch' off; amixer -c 0 -q cset name='I2S3_CH1 DL1_CH1' off; amixer -c 0 -q cset name='I2S3_CH1 DL1_CH2' off; amixer -c 0 -q cset name='I2S3_CH2 DL1_CH2' off
pactl set-sink-port "$HWS" analog-output-headphones
mic-nc stop >/dev/null
python3 - <<'PY'
import struct, math
fs = 48000
def load(p, ch, n):
    d = open(p, 'rb').read(); cnt = len(d) // (2 * n)
    return [v / 32768 for v in struct.unpack('<%dh' % (cnt * n), d[:cnt * n * 2])][ch::n]
def rms(a): return math.sqrt(sum(v * v for v in a) / max(1, len(a)))
db = lambda v: 20 * math.log10(v + 1e-9)
low = load('/tmp/ncT/b_raw.raw', 0, 2); nc = load('/tmp/ncT/b_nc.raw', 0, 1)
def onsets(x):
    win = 480
    e = [rms(x[i:i + win]) for i in range(0, len(x) - win, win)]
    thr = max(e) * 0.25
    out = []; up = False
    for i, v in enumerate(e):
        if v > thr and not up:
            out.append(i * 0.01); up = True
        elif v < thr * 0.5:
            up = False
    return out
a, b = onsets(low), onsets(nc)
print('bursts in the raw bottom: ', ['%.2f' % v for v in a])
print('bursts at the suppressor output: ', ['%.2f' % v for v in b])
if a and b:
    lags = [b[i] - a[i] for i in range(min(len(a), len(b)))]
    print('offsets per burst: ', ['%.2f' % v for v in lags])
