#!/bin/sh
mic-nc stop >/dev/null 2>&1
mic-nc start
sleep 2
echo "--- processes:"; pgrep -a parec; pgrep -a mic-nc-dsp; pgrep -a pacat
echo "--- sink-inputs (id sink client driver format):"; pactl list short sink-inputs
echo "--- sinks:"; pactl list short sinks
echo "--- source-outputs:"; pactl list short source-outputs
echo "--- sources:"; pactl list short sources
echo "--- state of mic_nc_sink and its monitor:"
pactl list sinks | awk '/Name: mic_nc_sink/{f=1} f&&/State|Volume:|Mute/{print} /Name: alsa/{f=0}' | head -6
echo "--- DSP alive: $(pgrep -c mic-nc-dsp), CPU load:"
ps -o pid,pcpu,comm 2>/dev/null | grep -E "parec|pacat|mic-nc" | head
mic-nc stop
