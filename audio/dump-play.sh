#!/bin/sh
# Plays a tone for ~6 s and takes FULL regmap dumps into /home/user/dump/ . Run: sh .../dump-play.sh
O=/home/user/dump
C="amixer -c 0 -q"
rm -rf $O; mkdir -p $O
$C cset name='ADDA_DL_CH1 DL1_CH1' on
$C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
$C cset name='HP Mux' 'Audio Playback'
$C cset name='Headset Volume' 6,6
sudo sh -c "
  mount | grep -q debugfs || mount -t debugfs none /sys/kernel/debug
  ls /sys/kernel/debug/regmap > $O/regmap-list.txt
  speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE -t sine -f 440 -l 1 >/dev/null 2>&1 &
  sleep 1
  A=/sys/devices/platform/soc/11210000.clock-controller/11210000.clock-controller:audio-controller/power
  cat \$A/runtime_status > $O/rpm.txt 2>&1
  grep -E 'aud_afe|aud_dac |aud_adc |top_audio |apll1 |apll2 ' /sys/kernel/debug/clk/clk_summary > $O/clk2.txt
  cat /proc/asound/card0/pcm0p/sub0/status >> $O/rpm.txt 2>&1
  find /sys/kernel/debug/asoc -type f -path '*dapm*' 2>/dev/null | while read f; do echo \"== \$f\"; cat \$f; done > $O/dapm.txt 2>&1
  for d in /sys/kernel/debug/regmap/*; do
    n=\$(basename \$d)
    cat \$d/registers > $O/\$n.txt 2>$O/\$n.err
  done
  cat /sys/kernel/debug/clk/clk_summary 2>/dev/null | grep -iE 'aud|apll|26m' > $O/clk.txt
  wait
  chown -R $(id -u):$(id -g) $O
"
$C cset name='HP Mux' 'Open'
wc -l $O/*.txt
echo "done: $O"
