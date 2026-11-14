#!/bin/sh
# irqmode=1 (rising) + irqdelay/irqprobe: INT line level and delayed re-reads inside the first irqs of a touch
DELAY=${1:-0}
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "NVTRUN begin IRQDBG delay=$DELAY" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko eng_reset=0 phys=0 irqmode=1 irqdelay=$DELAY irqprobe=40
insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=0 poison=0 nvtdump=0
sleep 6
echo ">>> NOW TOUCH THE SCREEN for 20 seconds (swipes, taps, hold) <<<"
echo "NVTRUN touch window" > /dev/kmsg
sleep 20
echo "NVTRUN end IRQDBG" > /dev/kmsg
echo done
