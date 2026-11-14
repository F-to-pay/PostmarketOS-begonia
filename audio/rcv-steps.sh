#!/bin/sh
# Earpiece volume ladder: 8 steps of 2 s (1 kHz tone, -30 dBFS), 1.5 s pause between them.
# Handset Volume goes 4,6,8,10,12,14,16,18 (-6 ... +8 dB). Ceiling ~ -22 dBFS (quieter than a normal call level).
# Put the ear to the top speaker and count from which step SOMETHING is audible.
# At the same time records with the top microphone (AIN2) and prints the 1 kHz level per step.
C="amixer -c 0 -q"
W=/tmp/rcvs; mkdir -p $W; rm -f $W/*.wav
python3 - $W/tone.wav <<'EOF'
import sys, math, struct, wave
sr = 48000; dur = 2.0; amp = 32767 * 10 ** (-30 / 20)
w = wave.open(sys.argv[1], 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
n = int(sr * dur); f = int(sr * 0.3); out = bytearray()
for i in range(n):
    g = min(1.0, i / f, (n - 1 - i) / f)
    v = int(amp * g * math.sin(2 * math.pi * 1000 * i / sr)); out += struct.pack('<hh', v, v)
w.writeframes(bytes(out)); w.close()
EOF
for s in $(pactl list short sinks | awk '{print $2}'); do pactl suspend-sink "$s" 1; done
for s in $(pactl list short sources | awk '!/monitor/{print $2}'); do pactl suspend-source "$s" 1; done
$C cset name='Speaker Switch' off 2>/dev/null
$C cset name='HP Mux' 'Open'
$C cset name='ADDA_DL_CH1 DL1_CH1' on; $C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
OLD_R=$(amixer -c 0 cget name='PGA_R_Mux' | sed -n 's/.*: values=\([0-9]*\)/\1/p')
$C cset name='PGA_R_Mux' AIN2
$C cset name='Handset Volume' 4
echo "start in 15 s: ear to the top speaker, then 8 steps of ~3.5 s"; sleep 15
arecord -q -D hw:0,4 -f S16_LE -r 48000 -c 2 -d 30 $W/all.wav &
AR=$!
sleep 0.5
$C cset name='RCV Mux' 'Voice Playback'
k=0
for g in 4 6 8 10 12 14 16 18; do
	k=$((k+1))
	$C cset name='Handset Volume' $g
	echo "step $k (Handset Volume $g)"
	aplay -q -D hw:0,0 $W/tone.wav
	sleep 1.5
done
$C cset name='RCV Mux' 'Open'
wait $AR
$C cset name='PGA_R_Mux' "${OLD_R:-0}"
for s in $(pactl list short sinks | awk '{print $2}'); do pactl suspend-sink "$s" 0; done
for s in $(pactl list short sources | awk '!/monitor/{print $2}'); do pactl suspend-source "$s" 0; done
python3 - $W/all.wav <<'EOF'
import sys, math, wave, struct
w = wave.open(sys.argv[1]); sr = w.getframerate(); n = w.getnframes()
x = struct.unpack('<%dh' % (n * 2), w.readframes(n))[1::2]
def g(seg, f=1000):
    k = 2 * math.cos(2 * math.pi * f / sr); s1 = s2 = 0.0
    for v in seg:
        s0 = v + k * s1 - s2; s2 = s1; s1 = s0
    p = max(s1 * s1 + s2 * s2 - k * s1 * s2, 1e-12)
    return 20 * math.log10(2 * math.sqrt(p) / len(seg) / 32768)
t0 = 0.5  # start of the first step relative to the recording start (s), accounting for delays ~
base = g(x[int(sr * 0.1):int(sr * 0.45)])
print('background (before the tone) 1 kHz: %.1f dBFS' % base)
for i in range(8):
    a = t0 + i * 3.5 + 0.5; seg = x[int(sr * a):int(sr * (a + 1.0))]
    if len(seg) > sr // 2:
        print('step %d: %.1f dBFS' % (i + 1, g(seg)))
EOF
echo "done, RCV disabled"
