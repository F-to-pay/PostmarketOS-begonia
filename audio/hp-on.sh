#!/bin/sh
# Enable the headphone output. Usage: hp-on [volume 0..4]   (0 = minimum -10 dB, 4 = -6 dB; default 1)
# Headset Volume scale (after fixing the inversion in the codec driver): index 0..18 = -10..+8 dB, 1 dB step.
# Ceiling 4 on purpose: above it is loud. The system volume (slider) then moves this control by itself.
V=${1:-1}
case "$V" in 0|1|2|3|4) ;; *) echo "volume 0..4 (I will not allow more - protecting the ears)"; exit 1;; esac
amixer -c 0 -q cset name='Headset Volume' "$V,$V"
amixer -c 0 -q cset name='HP Mux' 'Audio Playback'
echo "headphones on, Headset Volume=$V,$V (adjust the volume in the system/player; start low)"
