#!/bin/sh
# Return the output to the headphones: remove the speaker filter, restore the headphone port.
HW=$(pactl list short sinks | awk '/platform-sound/ && !/speaker_hpf/{print $2; exit}')
pactl set-default-sink "$HW"
# do not touch the noise suppressor streams (mic_nc_sink)
NC=$(pactl list short sinks | awk '$2=="mic_nc_sink"{print $1}')
for i in $(pactl list short sink-inputs | awk -v nc="$NC" '$2!=nc{print $1}'); do pactl move-sink-input "$i" "$HW" 2>/dev/null; done
M=$(pactl list short modules | awk '/module-ladspa-sink/ && /speaker_hpf/{print $1}')
for m in $M; do pactl unload-module "$m"; done
pactl set-sink-port "$HW" analog-output-headphones && echo "output: headphones (if the output is not enabled - hp-on)"
