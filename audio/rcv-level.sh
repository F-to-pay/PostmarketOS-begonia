#!/bin/sh
# Ladder by tone LEVEL for the earpiece: Handset Volume is fixed (default 10 = 0 dB),
# the 1 kHz tone level rises -30, -24, -18, -12 dBFS (2 s each, 2 s pause). Default ceiling -12 dBFS.
# Usage: rcv-level [Handset Volume 0..12, 10] [LEVELS="30 12 0"] [FADE=seconds]
#   (LEVELS - dB below full scale; may also be written with a minus: LEVELS="-12"). The variables can be
#   passed both as arguments and in the environment (LEVELS=0 sh rcv-level.sh).
G=10
for a in "$@"; do
	case "$a" in
		LEVELS=*) LEVELS=${a#LEVELS=} ;;
		FADE=*) FADE=${a#FADE=} ;;
		*) G=$a ;;
	esac
done
export FADE
case "$G" in ''|*[!0-9]*) echo "Handset Volume: a number 0..12 is needed"; exit 1;; esac
[ "$G" -le 12 ] || G=12
LEVELS=$(echo "${LEVELS:-30 24 18 12}" | tr -d '+-')
case "$LEVELS" in *[!0-9\ ]*|'') echo "LEVELS: numbers separated by spaces (dB below FS)"; exit 1;; esac
C="amixer -c 0 -q"
W=/tmp/rcvl; mkdir -p $W; rm -f $W/*.wav
for L in ${LEVELS:-30 24 18 12}; do
python3 - $W/t$L.wav $L <<'PY'
import sys, math, struct, wave
sr = 48000; amp = 32767 * 10 ** (-float(sys.argv[2]) / 20)
w = wave.open(sys.argv[1], 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
n = sr * 2; f = int(sr * float(__import__("os").environ.get("FADE", "0.3"))); out = bytearray()
for i in range(n):
    g = min(1.0, i / f, (n - 1 - i) / f)
    v = int(amp * g * math.sin(2 * math.pi * 1000 * i / sr)); out += struct.pack('<hh', v, v)
w.writeframes(bytes(out)); w.close()
PY
done
for s in $(pactl list short sinks | awk '{print $2}'); do pactl suspend-sink "$s" 1; done
$C cset name='Speaker Switch' off 2>/dev/null
$C cset name='HP Mux' 'Open'
$C cset name='ADDA_DL_CH1 DL1_CH1' on; $C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
$C cset name='Handset Volume' "$G"
echo "start in 10 s (Handset Volume $G)"; sleep 10
$C cset name='RCV Mux' 'Voice Playback'
k=0
for L in ${LEVELS:-30 24 18 12}; do
	k=$((k+1)); echo "step $k: -$L dBFS"
	aplay -q -D hw:0,0 $W/t$L.wav
	sleep 2
done
$C cset name='RCV Mux' 'Open'
for s in $(pactl list short sinks | awk '{print $2}'); do pactl suspend-sink "$s" 0; done
echo "done, RCV disabled"
