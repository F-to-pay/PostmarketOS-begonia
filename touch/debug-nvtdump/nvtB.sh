#!/bin/sh
# Display power cycle with the touch modules already loaded: the panel-follower callback re-uploads the firmware
# right after the panel's RESX pulse (like downstream: "download starts after the display RESX sequence").
# The matrix (probematrix) runs on every upload, so the post-RESX reads are logged too.
P=/sys/module/novatek_nvt_ts_spi/parameters
if [ ! -e $P/nvtdump ]; then echo "touch module not loaded"; exit 1; fi
echo 5000 > $P/nvtdump
C=$(ls -d /sys/class/drm/card*-DSI-1 | head -1)
echo "NVTRUN begin BLANKCYCLE dpms-before=$(cat $C/dpms)" > /dev/kmsg
echo 0 > /sys/class/graphics/fb0/blank; sleep 3
echo "NVTRUN blank=4 (power down)" > /dev/kmsg
echo 4 > /sys/class/graphics/fb0/blank; sleep 4
echo "NVTRUN blank=0 (power up)" > /dev/kmsg
echo 0 > /sys/class/graphics/fb0/blank; sleep 8
echo "NVTRUN end BLANKCYCLE dpms-after=$(cat $C/dpms)" > /dev/kmsg
echo done
