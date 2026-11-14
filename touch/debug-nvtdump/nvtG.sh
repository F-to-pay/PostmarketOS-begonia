#!/bin/sh
# prebr: read back what was written before boot_ready; dspoll: downstream-like mdelay(5) + 10 polls of 6 bytes; matrix as before.
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "NVTRUN begin PGD200 eng_reset=0 phys=0 pgdelay=200 prebr=1 dspoll=1 probematrix=1" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko eng_reset=0 phys=0
insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=200 prebr=1 dspoll=1 probematrix=1 poison=1 nvtdump=3000
sleep 8
echo "NVTRUN end PGD200" > /dev/kmsg
echo done
