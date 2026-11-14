#!/bin/sh
# Read only. Shows whether the AUDIO domain is on and which audio clocks are enabled. Loads nothing.
# Run: sh /home/user/Begonia/wifi/audio/diag-audio.sh   (output also in /home/user/audio-diag.log)
OUT=/home/user/audio-diag.log
sudo sh -c '
  mount | grep -q debugfs || mount -t debugfs none /sys/kernel/debug
  echo "== genpd summary (audio/conn/infra) =="
  cat /sys/kernel/debug/pm_genpd/pm_genpd_summary 2>&1 | head -60
  echo
  echo "== clk_summary: aud / audio / apll =="
  grep -iE "^ *(clock|---)|aud|apll" /sys/kernel/debug/clk/clk_summary 2>&1 | head -80
  echo
  echo "== devices_deferred =="
  cat /sys/kernel/debug/devices_deferred 2>&1 | head
' 2>&1 | tee "$OUT"
echo "== saved to $OUT =="
