#!/bin/sh
# Variant A (eng_reset=0 phys=0 pgdelay=0): load, then keep logging for 45 s while the screen is being touched.
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "NVTRUN begin TOUCH eng_reset=0 phys=0 pgdelay=0" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko eng_reset=0 phys=0
insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=0 nvtdump=3000
echo "loaded - TOUCH THE SCREEN NOW for 45 s (swipe, tap, hold)"
i=0
while [ $i -lt 9 ]; do sleep 5; i=$((i+1)); grep -E "spi1\.0|nvt|NVT" /proc/interrupts | head -3; done
echo "NVTRUN end TOUCH" > /dev/kmsg
echo done
