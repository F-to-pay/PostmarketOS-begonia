#!/bin/sh
# Objective earpiece test: a quiet 1 kHz tone (-40 dBFS, smooth fade-in/out),
# recorded with the top microphone (AIN2, next to the earpiece), comparing the energy at 1 kHz:
#   A - silence, B - tone with RCV Mux=Open (control for pickup/leakage), C - tone with RCV Mux=Voice Playback.
# Usage: rcv-test [Handset Volume index 0..18, default 0 = -10 dB] [frequency Hz, 1000] [tone level, dB below FS, 40]
# After the test: RCV Mux=Open, everything restored. The sound is quiet; do not put the speaker to the ear.
G=${1:-0}
F=${2:-1000}
LV=${3:-40}
case "$G$F$LV" in ''|*[!0-9]*) echo "numbers needed"; exit 1;; esac
[ "$G" -le 18 ] || G=18
C="amixer -c 0 -q"
W=/tmp/rcv; mkdir -p $W; rm -f $W/*.wav

# tone
python3 - "$F" $W/tone.wav "$LV" <<'EOF'
import sys, math, struct, wave
f = float(sys.argv[1]); sr = 48000; dur = 3.0
amp = 32767 * 10 ** (-float(sys.argv[3]) / 20)
w = wave.open(sys.argv[2], 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
n = int(sr * dur); fi = int(sr * 0.5); fo = int(sr * 0.5)
out = bytearray()
for i in range(n):
    g = min(1.0, i / fi, (n - 1 - i) / fo)
    v = int(amp * g * math.sin(2 * math.pi * f * i / sr))
    out += struct.pack('<hh', v, v)
w.writeframes(bytes(out)); w.close()
EOF

for s in $(pactl list short sinks | awk '{print $2}'); do pactl suspend-sink "$s" 1; done
for s in $(pactl list short sources | awk '!/monitor/{print $2}'); do pactl suspend-source "$s" 1; done

$C cset name='Speaker Switch' off 2>/dev/null
$C cset name='HP Mux' 'Open'
$C cset name='ADDA_DL_CH1 DL1_CH1' on; $C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
$C cset name='Handset Volume' "$G"
OLD_R=$(amixer -c 0 cget name='PGA_R_Mux' | sed -n 's/.*: values=\([0-9]*\)/\1/p')
$C cset name='PGA_R_Mux' AIN2

rec() { # $1 = name, $2 = whether to play the tone (0/1)
	arecord -q -D hw:0,4 -f S16_LE -r 48000 -c 2 -d 4 "$W/$1.wav" &
	AR=$!
	sleep 0.3
	if [ "$2" = 1 ]; then aplay -q -D hw:0,0 $W/tone.wav; else sleep 3; fi
	wait $AR
}

echo "A: silence"; $C cset name='RCV Mux' 'Open'; rec A 0
echo "B: tone, RCV Open (control)"; $C cset name='RCV Mux' 'Open'; rec B 1
echo "C: tone, RCV Voice Playback, Handset Volume=$G"; $C cset name='RCV Mux' 'Voice Playback'; rec C 1
$C cset name='RCV Mux' 'Open'
$C cset name='PGA_R_Mux' "${OLD_R:-0}"
for s in $(pactl list short sinks | awk '{print $2}'); do pactl suspend-sink "$s" 0; done
for s in $(pactl list short sources | awk '!/monitor/{print $2}'); do pactl suspend-source "$s" 0; done

python3 - "$F" $W <<'EOF'
import sys, math, wave, struct
f = float(sys.argv[1]); d = sys.argv[2]

def goertzel(x, sr, f):
    k = 2 * math.cos(2 * math.pi * f / sr); s1 = s2 = 0.0
    for v in x:
        s0 = v + k * s1 - s2; s2 = s1; s1 = s0
    p = s1 * s1 + s2 * s2 - k * s1 * s2
    return 20 * math.log10(max(1e-9, 2 * math.sqrt(max(p, 0)) / len(x)) / 32768)

def chan(path, c):
    w = wave.open(path); n = w.getnframes(); raw = w.readframes(n)
    x = struct.unpack('<%dh' % (n * w.getnchannels()), raw)
    return x[c::w.getnchannels()], w.getframerate()

res = {}
for name in 'ABC':
    for c, nm in ((0, 'bottom L'), (1, 'top R')):
        x, sr = chan('%s/%s.wav' % (d, name), c)
        x = x[sr // 2: sr * 7 // 2]  # the middle
        rms = 20 * math.log10(max(1e-9, math.sqrt(sum(v * v for v in x) / len(x))) / 32768)
        tone = goertzel(x, sr, f)
        res[(name, c)] = (rms, tone)
        print('%s %-10s RMS %6.1f dBFS | %4.0f Hz %6.1f dBFS' % (name, nm, rms, f, tone))
for c, nm in ((1, 'top R'), (0, 'bottom L')):
    a = res[('A', c)][1]; b = res[('B', c)][1]; cc = res[('C', c)][1]
    print('%s: tone C−A = %+.1f dB, C−B = %+.1f dB' % (nm, cc - a, cc - b))
EOF
echo "done, RCV disabled"
