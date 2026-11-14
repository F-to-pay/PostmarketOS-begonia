#!/bin/sh
# Variant A + "postfast": disable SPI_RD_FAST again 10 ms after the firmware boots. Touch the screen while it runs.
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "NVTRUN begin POSTFAST eng_reset=0 phys=0 pgdelay=0 postfast=10" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko eng_reset=0 phys=0
insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=0 postfast=10 nvtdump=1500
echo "loaded - TOUCH THE SCREEN NOW for 30 s"
i=0
while [ $i -lt 6 ]; do sleep 5; i=$((i+1)); grep -E "spi1\.0" /proc/interrupts | head -2; done
echo "NVTRUN end POSTFAST" > /dev/kmsg
echo done
