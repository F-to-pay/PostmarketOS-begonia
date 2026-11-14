#!/bin/sh
# Power-cycle the panel through DRM (force the DSI connector off/on), touch modules already loaded.
P=/sys/module/novatek_nvt_ts_spi/parameters
[ -e $P/nvtdump ] || { echo "touch module not loaded"; exit 1; }
echo 5000 > $P/nvtdump
C=$(ls -d /sys/class/drm/card*-DSI-1 | head -1)
echo "NVTRUN begin DRMCYCLE conn=$C status=$(cat $C/status) dpms=$(cat $C/dpms) enabled=$(cat $C/enabled)" > /dev/kmsg
echo off > $C/status; sleep 4
echo "NVTRUN connector off: status=$(cat $C/status) dpms=$(cat $C/dpms) enabled=$(cat $C/enabled)" > /dev/kmsg
echo on > $C/status; sleep 8
echo "NVTRUN connector on: status=$(cat $C/status) dpms=$(cat $C/dpms) enabled=$(cat $C/enabled)" > /dev/kmsg
echo detect > $C/status; sleep 3
echo "NVTRUN end DRMCYCLE status=$(cat $C/status) dpms=$(cat $C/dpms) enabled=$(cat $C/enabled)" > /dev/kmsg
echo done
