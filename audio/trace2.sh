#!/bin/sh
# Loads fresh modules, plays a tone, collects REGDBG. Run: sh .../trace2.sh
A=/home/user/Begonia/wifi/audio
sh "$A/load-audio-safe.sh" >/dev/null 2>&1 || { echo "loading failed"; exit 1; }
pactl unload-module module-alsa-card 2>/dev/null; sleep 1
sudo dmesg -C
sh "$A/sound-test.sh" hp >/dev/null 2>&1
tail -8 /home/user/sound-test.log
dmesg | grep -E "REGDBG2|IRQDBG" | head -30 | cut -c1-700 | tee /home/user/regdbg.log
amixer -c 0 cget name='ADDA_DL_CH1 DL1_CH1' | tee -a /home/user/regdbg.log
