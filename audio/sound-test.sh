#!/bin/sh
# Sound test through the MT6359 codec. Usage:
#   sh sound-test.sh hp    - headphones (plug into the 3.5 mm jack, moderate volume)
#   sh sound-test.sh rcv   - earpiece (put the ear to the top speaker, quietly FIRST)
# Writes details to /home/user/sound-test.log
set -u
MODE=${1:-}
LOG=/home/user/sound-test.log
C="amixer -c 0 -q"
case "$MODE" in hp|rcv) ;; *) echo "give hp or rcv"; exit 1;; esac
: > "$LOG"
say() { echo "$*" | tee -a "$LOG"; }

say "== AFE route: DL1 -> ADDA_DL =="
$C cset name='ADDA_DL_CH1 DL1_CH1' on  2>&1 | tee -a "$LOG"
$C cset name='ADDA_DL_CH2 DL1_CH2' on  2>&1 | tee -a "$LOG"
$C cset name='DAC In Mux' 'Normal Path' 2>&1 | tee -a "$LOG"

if [ "$MODE" = hp ]; then
  say "== codec: headphones =="
  $C cset name='HP Mux' 'Audio Playback' 2>&1 | tee -a "$LOG"
  $C cset name='Headset Volume' 6,6       2>&1 | tee -a "$LOG"
else
  say "== codec: earpiece =="
  $C cset name='RCV Mux' 'Voice Playback' 2>&1 | tee -a "$LOG"
  $C cset name='Handset Volume' 5         2>&1 | tee -a "$LOG"
fi

say "== playing a 440 Hz tone, ~3 s (hw:0,0) =="
if command -v speaker-test >/dev/null 2>&1; then
  timeout 8 speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE -t sine -f 440 -l 1 2>&1 | tee -a "$LOG"
else
  say "no speaker-test; trying aplay -D hw:0,0 with /dev/zero (silence) - just a check that the stream opens"
  timeout 3 aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 2 /dev/zero 2>&1 | tee -a "$LOG"
fi
say "== dmesg (tail, without wlan) =="
dmesg | grep -v "\[wlan\]" | grep -v mtk_axi | grep -v "error (-16)" | tail -15 | cut -c1-200 | tee -a "$LOG"
say "== rolling back the route =="
$C cset name='HP Mux' 'Open'  2>/dev/null; $C cset name='RCV Mux' 'Open' 2>/dev/null
say "log: $LOG"
