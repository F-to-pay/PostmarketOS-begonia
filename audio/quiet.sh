#!/bin/sh
# QUIET tone: 440 Hz, -40 dBFS, 1 s fade-in, codec volume 0,0 (minimum). Output disabled afterwards.
C="amixer -c 0 -q"
pactl unload-module module-alsa-card 2>/dev/null
$C cset name='ADDA_DL_CH1 DL1_CH1' on
$C cset name='ADDA_DL_CH2 DL1_CH2' on
$C cset name='DAC In Mux' 'Normal Path'
$C cset name='Headset Volume' 0,0
$C cset name='HP Mux' 'Audio Playback'
sleep 1
aplay -D hw:0,0 /home/user/quiet.wav 2>&1 | tail -1
$C cset name='HP Mux' 'Open'
$C cset name='RCV Mux' 'Open'
echo "done, output disabled"
