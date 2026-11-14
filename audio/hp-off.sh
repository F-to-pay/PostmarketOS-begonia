#!/bin/sh
# Disable the headphone output and reset the codec volume to minimum.
amixer -c 0 -q cset name='HP Mux' 'Open'
amixer -c 0 -q cset name='RCV Mux' 'Open'
amixer -c 0 -q cset name='Headset Volume' 0,0
echo "output disabled"
