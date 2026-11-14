#!/bin/sh
# clean fix modules (no debug params): DT irq trigger + no page re-select on reads
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "NVTRUN begin CLEANFIX" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko
insmod /tmp/novatek-nvt-ts-spi.ko
sleep 6
grep spi1 /proc/interrupts | cut -c1-90
a=$(grep spi1 /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; print s+0}'); sleep 8
b=$(grep spi1 /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; print s+0}')
echo "IDLE: $((b - a)) IRQs in 8 s (expect 0)"
echo ">>> NOW TOUCH THE SCREEN for 20 seconds (swipes, taps, hold) <<<"
echo "NVTRUN touch window" > /dev/kmsg
ev=$(grep -l "spi1.0" /sys/class/input/input*/name 2>/dev/null | head -1 | sed 's#.*/\(input[0-9]*\)/name#\1#')
n=$(timeout 20 cat /dev/input/event3 | wc -c)
echo "input event bytes: $n (events: $((n/24)))"
echo "NVTRUN end CLEANFIX events=$((n/24))" > /dev/kmsg
echo done
