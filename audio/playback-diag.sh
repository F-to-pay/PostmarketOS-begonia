#!/bin/sh
# Playback diagnostics: starts a stream in the background and reads the AFE registers through the debugfs regmap.
# Run: sh /home/user/Begonia/wifi/audio/playback-diag.sh   (no sudo before sh; breaks nothing, only reads)
# Result: /home/user/playback-diag.log
LOG=/home/user/playback-diag.log
C="amixer -c 0 -q"
: > "$LOG"

$C cset name='ADDA_DL_CH1 DL1_CH1' on
$C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
$C cset name='HP Mux' 'Audio Playback'
$C cset name='Headset Volume' 6,6

sudo sh -c '
  mount | grep -q debugfs || mount -t debugfs none /sys/kernel/debug
  D=$(ls -d /sys/kernel/debug/regmap/*11210000* 2>/dev/null | head -1)
  echo "regmap dir: $D"
  [ -n "$D" ] || { ls /sys/kernel/debug/regmap; exit 1; }
  PAT="^(0000|0004|0010|0048|004c|0050|0054|0058|005c|0060|0064|0108|010c|0124|08cc|0e00|0e20|0e40):"
  snap() { echo "--- t=$1"; grep -E "$PAT" "$D/registers"; }
  speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE -t sine -f 440 -l 1 >/dev/null 2>&1 &
  SP=$!
  i=0
  while [ $i -lt 8 ]; do
    snap $i
    echo "pcm status:"; cat /proc/asound/card0/pcm0p/sub0/status 2>&1 | head -8
    i=$((i+1)); sleep 0.15 2>/dev/null || sleep 1
  done
  wait $SP 2>/dev/null
  echo "=== hw_params (if still there) ==="; cat /proc/asound/card0/pcm0p/sub0/hw_params 2>&1
' 2>&1 | tee -a "$LOG"

echo "=== dmesg (without wlan/-16) ===" | tee -a "$LOG"
dmesg | grep -v "\[wlan\]" | grep -v mtk_axi | grep -v "error (-16)" | tail -12 | cut -c1-200 | tee -a "$LOG"
$C cset name='HP Mux' 'Open'
echo "log: $LOG"
