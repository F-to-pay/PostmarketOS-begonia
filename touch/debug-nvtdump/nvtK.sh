#!/bin/sh
# irqmode=1 (rising) + pgskip=1: no page select in the irq read, like downstream nvt_ts_work_func
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "NVTRUN begin PGSKIP" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko eng_reset=0 phys=0 irqmode=1 irqprobe=0
insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=0 poison=0 nvtdump=0 pgskip=1
sleep 6
echo ">>> NOW TOUCH THE SCREEN for 20 seconds (swipes, taps, hold) <<<"
echo "NVTRUN touch window" > /dev/kmsg
n=$(timeout 20 cat /dev/input/event3 | wc -c); echo "input event bytes: $n (events: $((n/24)))"
echo "NVTRUN end PGSKIP skipped=$(cat /sys/module/novatek_nvt_ts_spi/parameters/pgskip_n) retried=$(cat /sys/module/novatek_nvt_ts_spi/parameters/pgretry_n)" > /dev/kmsg
echo "skipped=$(cat /sys/module/novatek_nvt_ts_spi/parameters/pgskip_n) retried=$(cat /sys/module/novatek_nvt_ts_spi/parameters/pgretry_n)"; echo done
