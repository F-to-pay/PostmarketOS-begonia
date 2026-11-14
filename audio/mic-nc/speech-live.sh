#!/bin/sh
# A live test without a human: a synthetic "voice" is played through the phone speaker (bottom), 
# the noise suppressor runs live; comparison of the raw bottom channel / top / suppressor output.
FMT="--format=s16le --rate=48000 --raw"
SRC=alsa_input.platform-sound.analog-stereo-in
pin_streams() {
	for id in $(pactl list short source-outputs | cut -f1); do
		if pactl list source-outputs | awk -v id="$id" '$0=="Source Output #"id{f=1;next} /^Source Output #/{f=0} f' | grep -q "$1"; then
			pactl move-source-output "$id" "$2" 2>/dev/null
		fi
	done
}
python3 - <<'PY'
# synthetic "voice": 130 Hz harmonics with 700/1200/2600 Hz formants and 4 Hz syllable modulation, 4 s
import math, struct, wave
fs = 48000
y = []
for i in range(4 * fs):
    t = i / fs
    env = 0.55 + 0.45 * math.sin(2 * math.pi * 4 * t) ** 2
    v = 0.0
    for h in range(1, 60):
        f = 130 * h
        if f > 6000: break
        g = sum(math.exp(-((f - fc) / bw) ** 2) for fc, bw in ((700, 250), (1200, 300), (2600, 500)))
        v += g * math.sin(2 * math.pi * f * t) / h ** 0.4
    y.append(max(-32768, min(32767, int(v * env * 6000))))
w = wave.open('/tmp/ncT/speech.wav', 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(fs)
w.writeframes(struct.pack('<%dh' % len(y), *y)); w.close()
PY
mic-nc stop >/dev/null 2>&1
mic-nc start >/dev/null
sleep 2
spk-on 100 >/dev/null
sleep 1
rm -f /tmp/ncT/l_raw.raw /tmp/ncT/l_nc.raw
( parec --device=$SRC $FMT --channels=2 --latency-msec=20 --client-name=sl-raw 2>/dev/null | head -c 1920000 > /tmp/ncT/l_raw.raw ) &
( parec --device=mic_nc $FMT --channels=1 --latency-msec=20 --client-name=sl-nc 2>/dev/null | head -c 960000 > /tmp/ncT/l_nc.raw ) &
( sleep 0.4; pin_streams sl-raw $SRC; pin_streams sl-nc mic_nc ) &
sleep 0.8
paplay --device=$(pactl list short sinks | awk '/platform-sound/ && !/hpf/ && !/mic/{print $2; exit}') /tmp/ncT/speech.wav 2>/dev/null &
sleep 0.2
wait
spk-off >/dev/null
mic-nc stop >/dev/null
python3 - <<'PY'
import struct, math
fs = 48000
def load(p, ch, n):
    d = open(p, 'rb').read(); cnt = len(d) // (2 * n)
    return [v / 32768 for v in struct.unpack('<%dh' % (cnt * n), d[:cnt * n * 2])][ch::n]
def rms(a): return math.sqrt(sum(v * v for v in a) / max(1, len(a)))
db = lambda v: 20 * math.log10(v + 1e-9)
low = load('/tmp/ncT/l_raw.raw', 0, 2); top = load('/tmp/ncT/l_raw.raw', 1, 2); nc = load('/tmp/ncT/l_nc.raw', 0, 1)
seg = lambda a, t0, t1: a[int(t0 * fs):int(t1 * fs)]
for name, a in (('bottom (raw)', low), ('top (raw)', top), ('suppressor out', nc)):
    print('%-16s speech(1.5-4.5s) %6.1f dBFS    silence(6.5-9.5s) %6.1f dBFS' % (name, db(rms(seg(a, 1.5, 4.5))), db(rms(seg(a, 6.5, 9.5)))))
print('bottom-top on speech: %.1f dB' % (db(rms(seg(low, 1.5, 4.5))) - db(rms(seg(top, 1.5, 4.5)))))
PY
