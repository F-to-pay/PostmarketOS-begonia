#!/bin/sh
# Play audio through the earpiece. Usage:
#   rcv-play [file | speech] [Handset Volume 0..18, 10] [LUFS, -16] [ADDA_DL_GAIN raw, do not change]
# Without a file/speech: flite speech synthesis + the Oxygen system sound. loudnorm normalization, peak -2 dBFS.
# The fourth parameter is a TEMPORARY digital gain ADDA_DL_GAIN (raw 0..65535; normally 2165 = -29.6 dB, it is SHARED
# with the headphones!). The previous value is remembered and restored on exit (incl. on Ctrl+C). Ceiling 65535 (0 dB) (+18 dB = 17200, +24 dB = 34000).
# Starts after 8 s, afterwards - RCV Mux=Open.
SRC=${1:-speech}; G=${2:-10}; LU=${3:--16}; DLG=${4:-}
case "$G" in ''|*[!0-9]*) echo "Handset Volume: a number 0..18"; exit 1;; esac
[ "$G" -le 18 ] || G=18
case "$DLG" in ''|*[!0-9]*) [ -z "$DLG" ] || { echo "ADDA_DL_GAIN: a raw number"; exit 1; };; esac
[ -z "$DLG" ] || [ "$DLG" -le 65535 ] || { echo "ADDA_DL_GAIN > 65535 is not allowed"; exit 1; }
C="amixer -c 0 -q"
OLD_DLG=$(amixer -c 0 cget name=ADDA_DL_GAIN | sed -n 's/.*: values=\([0-9]*\)/\1/p')
OLD_DLG=${OLD_DLG:-2165}
restore() {
	$C cset name='RCV Mux' 'Open'
	$C cset name=ADDA_DL_GAIN "$OLD_DLG"
	for s in $(pactl list short sinks | awk '{print $2}'); do pactl suspend-sink "$s" 0; done
}
trap restore EXIT
trap 'exit 130' INT TERM
W=/tmp/rcvp; mkdir -p $W; rm -f $W/*.wav
if [ "$SRC" = speech ]; then
	flite -t "Earpiece check. One, two, three, four, five. If you can hear this, the sound works." -o $W/s.wav 2>/dev/null \
	|| flite -t "Testing the earpiece. One, two, three, four, five. If you can hear this, the sound works." -o $W/s.wav
	ffmpeg -loglevel error -y -i $W/s.wav -i /usr/share/sounds/Oxygen-Im-Message-In.ogg \
		-filter_complex "[0:a]aresample=48000,aformat=channel_layouts=stereo[a];[1:a]aresample=48000,aformat=channel_layouts=stereo[b];[a][b]concat=n=2:v=0:a=1,loudnorm=I=$LU:TP=-2:LRA=7,afade=t=in:d=0.02,adelay=300|300" \
		-ar 48000 -ac 2 -sample_fmt s16 $W/p.wav
else
	ffmpeg -loglevel error -y -i "$SRC" -af "atrim=start=0.${TRIM_MS:-030},asetpts=PTS-STARTPTS,loudnorm=I=$LU:TP=-2:LRA=7,afade=t=in:d=0.02,adelay=300|300" -ar 48000 -ac 2 -sample_fmt s16 $W/p.wav
fi
for s in $(pactl list short sinks | awk '{print $2}'); do pactl suspend-sink "$s" 1; done
$C cset name='Speaker Switch' off 2>/dev/null
$C cset name='HP Mux' 'Open'
$C cset name='ADDA_DL_CH1 DL1_CH1' on; $C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
$C cset name='Handset Volume' "$G"
[ -z "$DLG" ] || $C cset name=ADDA_DL_GAIN "$DLG"
echo "start in 8 s (Handset Volume $G, $LU LUFS, ADDA_DL_GAIN ${DLG:-$OLD_DLG}; normally $OLD_DLG)"; sleep 8
$C cset name='RCV Mux' 'Voice Playback'
aplay -q -D hw:0,0 $W/p.wav
echo "done, RCV disabled, ADDA_DL_GAIN will return to $OLD_DLG"
