#!/bin/sh
# Variant A + probematrix: right after the firmware boots (0xa0) read 19 (page x offset) addresses, 16 bytes each.
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "NVTRUN begin MATRIX eng_reset=0 phys=0 pgdelay=0 probematrix=1" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko eng_reset=0 phys=0
insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=0 probematrix=1 poison=1 chippre=1 nvtdump=400
sleep 6
echo "NVTRUN end MATRIX" > /dev/kmsg
echo done
