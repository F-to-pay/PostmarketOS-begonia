#!/bin/sh
# Change the speaker HPF cutoff: spk-hpf <Hz>  (80..600)  |  spk-hpf off  (no filter: the bass rasps!)
A=${1:-250}
V=$(pactl get-sink-volume speaker_hpf 2>/dev/null | grep -o '[0-9]*%' | head -1 | tr -d '%'); V=${V:-70}
if [ "$A" = off ]; then A=20; fi
case "$A" in ''|*[!0-9]*) echo "a number (Hz) or off is needed"; exit 1;; esac
[ "$A" -ge 20 ] && [ "$A" -le 600 ] || { echo "20..600 Hz"; exit 1; }
SPK_HPF=$A
export SPK_HPF
M=$(pactl list short modules | awk '/module-ladspa-sink/ && /speaker_hpf/{print $1}')
for m in $M; do pactl unload-module "$m"; done
exec /usr/bin/spk-on "$V"
