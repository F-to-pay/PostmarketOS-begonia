#!/bin/sh
# irqmode=1: request the touch IRQ on the RISING edge like downstream (int_trigger_type=1), then measure idle and touch IRQ rates.
irq() { grep spi1 /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; print s+0}'; }
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "NVTRUN begin IRQMODE1" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko eng_reset=0 phys=0 irqmode=1
insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=0 poison=0 nvtdump=1500
sleep 6
grep spi1 /proc/interrupts | cut -c1-110
a=$(irq); sleep 10; b=$(irq)
echo "IDLE: $((b - a)) IRQs in 10 s   (before: ~900 on pmOS falling edge; downstream idle: 0)"
echo ">>> NOW TOUCH THE SCREEN for 25 seconds (swipes, taps, hold) <<<"
echo "NVTRUN touch window" > /dev/kmsg
sleep 25
c=$(irq)
echo "TOUCHING: $((c - b)) IRQs in 25 s"
echo "NVTRUN end IRQMODE1 idle=$((b - a)) touch=$((c - b))" > /dev/kmsg
echo done
