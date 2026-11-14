#!/bin/sh
# Loads the audio modules in dependency order and shows the result. After a reboot with begonia-audio.dtb.
M=/home/user/audio-modules
[ -d /proc/device-tree/soc/clock-controller@11210000/audio-controller ] || { echo "no audio-controller in DT: no reboot with begonia-audio.dtb yet"; exit 1; }
sudo sh -c "
  insmod $M/snd-soc-mtk-common.ko &&
  insmod $M/snd-soc-mt6359.ko &&
  insmod $M/snd-soc-mt6785-afe.ko &&
  insmod $M/mt6785-mt6359.ko
"
echo "== dmesg =="
sudo dmesg | grep -iE "mt6785|mt6359|afe|snd|mtkaif|audio" | tail -40
echo "== cards =="
cat /proc/asound/cards
aplay -l 2>&1 | head -20
arecord -l 2>&1 | head -20
