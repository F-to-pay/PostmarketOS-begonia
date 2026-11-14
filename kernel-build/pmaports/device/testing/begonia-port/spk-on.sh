#!/bin/sh
# Sound to the loudspeaker through a high-pass filter (the old speaker cannot handle the bass - it rasps).
# Usage: spk-on [volume in %, default 70]     (100% = the safe maximum)
# HPF: Linkwitz-Riley, 24 dB/oct, cutoff 250 Hz (change on the fly: spk-hpf <Hz|off>; default env SPK_HPF)
V=${1:-70}
case "$V" in ''|*[!0-9]*) echo "a percentage number is needed"; exit 1;; esac
[ "$V" -le 100 ] || V=100
F=${SPK_HPF:-250}
HW=$(pactl list short sinks | awk '/platform-sound/ && !/speaker_hpf/{print $2; exit}')
[ -n "$HW" ] || { echo "no sink of the sound card"; exit 1; }
pactl set-sink-port "$HW" analog-output-speaker || exit 1
pactl set-sink-volume "$HW" 100%     # hardware ceiling (-18 dB from the vendor maximum); the slider below that is software
if ! pactl list short sinks | grep -q speaker_hpf; then
  pactl load-module module-ladspa-sink sink_name=speaker_hpf sink_master="$HW" channels=2 \
     sink_properties=device.description=Speaker-HPF plugin=lsp-plugins-ladspa \
     label=http://lsp-plug.in/plugins/ladspa/filter_stereo \
     control=0,1,1,0,0.2,1,0.0266,0,0,0,0,0,1,2,2,$F,0,1,0,0 >/dev/null || { echo "the filter did not load, playing without it"; exit 1; }
fi
pactl set-sink-volume speaker_hpf "$V%"
pactl set-default-sink speaker_hpf
# do not touch the noise suppressor streams (mic_nc_sink): their output must stay on its own sink
NC=$(pactl list short sinks | awk '$2=="mic_nc_sink"{print $1}')
for i in $(pactl list short sink-inputs | awk -v nc="$NC" '$2!=nc{print $1}'); do pactl move-sink-input "$i" speaker_hpf 2>/dev/null; done
echo "speaker on through a ${F} Hz HPF, volume $V%. Back to the headphones: spk-off"
