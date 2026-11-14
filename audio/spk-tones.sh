#!/bin/sh
# Listening test of the speaker: 6 pure tones of 2.5 s in a row (100, 200, 400, 800, 1600, 3200 Hz), moderate volume.
# Say which of them rasp/rattle. Usage: spk-tones [Speaker Volume index 0..56, default 40 (-26 dB)]
# Turns the speaker on for the test by itself and restores everything as it was (headphones).
G=${1:-40}
case "$G" in ''|*[!0-9]*) echo "an index 0..56 is needed"; exit 1;; esac
[ "$G" -le 56 ] || G=56
C="amixer -c 0 -q"
SINK=$(pactl list short sinks | awk '/platform-sound/{print $2; exit}')
pactl suspend-sink "$SINK" 1
$C cset name='HP Mux' 'Open'; $C cset name='RCV Mux' 'Open'
$C cset name='Speaker Switch' on; $C cset name='I2S3_CH1 DL1_CH1' on; $C cset name='I2S3_CH1 DL1_CH2' on
$C cset name='Speaker Volume' "$G"
echo "playing: 100 -> 200 -> 400 -> 800 -> 1600 -> 3200 Hz (2.5 s each). Listen for which ones rasp."
aplay -q -D hw:0,0 /home/user/Begonia/wifi/audio/clips/tones.wav
$C cset name='Speaker Switch' off; $C cset name='I2S3_CH1 DL1_CH1' off; $C cset name='I2S3_CH1 DL1_CH2' off
pactl suspend-sink "$SINK" 0
echo "done, speaker off."
