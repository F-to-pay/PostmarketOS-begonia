#!/bin/sh
# Stepwise loading of snd-soc-mt6785-afe. Usage: sh bisect-afe.sh N   (N = 1,2,3,4)
#  1 = init_clock only (enables nothing)               -> must be safe
#  2 = + enabling the clocks (no register reads)
#  3 = + reading one register AUDIO_TOP_CON0
#  4 = + reading all registers 0x10..0xfb8
# With stage<99 the module refuses to load (-ECANCELED) - this is NORMAL, check dmesg.
# The log is written to disk BEFORE each step: after a hang the last line tells where it died.
set -u
N=${1:?"give the stage: sh $0 1|2|3|4"}
shift; EXTRA="$*"
HERE=$(cd "$(dirname "$0")" && pwd)
M=/home/user/audio-modules
LOG=/home/user/audio-bisect.log
sudo sysctl -q kernel.dmesg_restrict=0
echo "[$(date +%T)] === stage $N ===" >> "$LOG"
sudo rm -f /home/user/afe-progress.txt
sudo cp "$HERE/mt6785/snd-soc-mt6785-afe.ko" "$M/snd-soc-mt6785-afe.ko"
for mod in snd-soc-mtk-common snd-soc-mt6359; do
  lsmod | grep -q "^$(echo $mod | tr - _) " || sudo insmod "$M/$mod.ko"
done
if lsmod | grep -q "^snd_soc_mt6785_afe "; then
  echo "unloading the previous afe (otherwise stage cannot be changed)"; sudo rmmod snd_soc_mt6785_afe
fi
echo "[$(date +%T)] BEFORE insmod afe stage=$N" | tee -a "$LOG"
sync; sleep 1
sudo insmod "$M/snd-soc-mt6785-afe.ko" stage="$N" $EXTRA 2>&1 | tee -a "$LOG"
echo "[$(date +%T)] AFTER insmod afe stage=$N (returned without hanging)" | tee -a "$LOG"
dmesg | grep -E "STAGE|mt6785-audio|afe" | tail -40 | tee -a "$LOG"
sync
