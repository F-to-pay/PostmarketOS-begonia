rm -f /tmp/touchcap.txt
echo 0 > /sys/class/graphics/fb0/blank
IRQ0=$(grep NVT-ts-spi /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; print s}')
sleep 4
T0=$(cut -d. -f1 /proc/uptime); echo "START $T0 irq0=$IRQ0" > /tmp/touchcap.txt
while [ $(( $(cut -d. -f1 /proc/uptime) - T0 )) -lt 30 ]; do
  /tmp/nvtspi R:21c00:60 >> /tmp/touchcap.txt
done
echo "END $(cut -d. -f1 /proc/uptime) irq1=$(grep NVT-ts-spi /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; print s}')" >> /tmp/touchcap.txt
