#!/bin/sh
# Careful loading of the audio modules: unloads the old ones, takes the FRESH .ko from the build directories,
# loads them one by one, log to disk BEFORE each insmod (on a hang the last line points at the culprit).
# Run: sh /home/user/Begonia/wifi/audio/load-audio-safe.sh   (no sudo before sh)
A=/home/user/Begonia/wifi/audio
M=/home/user/audio-modules
LOG=/home/user/audio-load.log
[ -d /proc/device-tree/soc/clock-controller@11210000/audio-controller ] || { echo "no audio-controller in DT"; exit 1; }
sudo sysctl -q kernel.dmesg_restrict=0
: > "$LOG"

echo "== releasing the card from PulseAudio (otherwise the module is 'in use') =="
pactl unload-module module-alsa-card 2>/dev/null && echo "pulse: module-alsa-card unloaded" || echo "pulse: nothing to unload"
sleep 1
echo "== unloading the old ones (in reverse order) =="
for m in mt6785_mt6359 snd_soc_mt6785_afe snd_soc_mt6359 snd_soc_mtk_common; do
  if lsmod | grep -q "^$m "; then echo "rmmod $m"; sudo rmmod "$m" || { echo "did not unload: $m"; exit 1; }; fi
done

echo "== copying the fresh .ko =="
sudo cp "$A/common/snd-soc-mtk-common.ko" "$A/codecs/snd-soc-mt6359.ko" \
        "$A/mt6785/snd-soc-mt6785-afe.ko" "$A/mt6785/mt6785-mt6359.ko" "$M/"
ls -l "$M"
sudo dmesg -C 2>/dev/null || true

for mod in snd-soc-mtk-common snd-soc-mt6359 snd-soc-mt6785-afe mt6785-mt6359; do
  echo "[$(date +%T)] BEFORE insmod $mod" | tee -a "$LOG"
  sync; sleep 1
  P=""; [ "$mod" = snd-soc-mt6785-afe ] && P="${AFE_PARAMS:-}"
  if sudo insmod "$M/$mod.ko" $P; then
    echo "[$(date +%T)] OK $mod" | tee -a "$LOG"
  else
    echo "[$(date +%T)] ERROR $mod" | tee -a "$LOG"
    dmesg | grep -v "\[wlan\]" | grep -v mtk_axi_interrupt | tail -30 | tee -a "$LOG"
    exit 1
  fi
  dmesg | grep -v "\[wlan\]" | grep -v mtk_axi_interrupt | grep -iE "mt6785|mt6359|afe|snd|mtkaif|audio|asoc" | tail -15 >> "$LOG"
  sync; sleep 2
done
echo "== cards =="
cat /proc/asound/cards | tee -a "$LOG"
aplay -l 2>&1 | head -20 | tee -a "$LOG"
arecord -l 2>&1 | head -10 | tee -a "$LOG"
echo "log: $LOG"
