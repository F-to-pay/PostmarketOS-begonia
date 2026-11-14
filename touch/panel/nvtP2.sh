#!/bin/sh
# Replace the panel module with one using Xiaomi's display timing (vfp 12, 60.24 Hz), then load the instrumented touch modules and measure the idle IRQ rate.
C=/sys/class/drm/card0-DSI-1
irq() { grep spi1 /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; print s+0}'; }
echo "NVTRUN begin PANELTIMING" > /dev/kmsg
echo "before: status=$(cat $C/status) enabled=$(cat $C/enabled) dpms=$(cat $C/dpms) modes=$(head -1 $C/modes)"
rmmod novatek_nvt_ts_spi 2>/dev/null
rmmod novatek_nvt_ts_core 2>/dev/null
sleep 1
echo "rmmod panel: $(rmmod panel_novatek_nt36672a 2>&1)"; sleep 3
echo "insmod panel: $(insmod /tmp/panel-novatek-nt36672a.ko 2>&1)"; sleep 6
echo "after panel: status=$(cat $C/status) enabled=$(cat $C/enabled) dpms=$(cat $C/dpms) modes=$(head -1 $C/modes)"
echo "NVTRUN touch modules" > /dev/kmsg
insmod /tmp/novatek-nvt-ts-core.ko eng_reset=0 phys=0
insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=0 nvtdump=0
sleep 6
a=$(irq); sleep 10; b=$(irq)
echo "touch IRQ at idle: $((b - a)) in 10 s (pmOS before: ~900; downstream idle: 0)"
grep -E "novatek|nvt" /proc/modules | cut -d' ' -f1,3
grep -E "^N:.*spi1" /proc/bus/input/devices
echo "NVTRUN end PANELTIMING irq10s=$((b - a))" > /dev/kmsg
echo done
