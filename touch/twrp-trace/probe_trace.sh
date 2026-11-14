mount -t debugfs none /sys/kernel/debug 2>/dev/null
T=/sys/kernel/debug/tracing
echo "== tracing dir:"; ls $T 2>&1 | head -20
echo "== spi events:"; ls $T/events/spi 2>&1
for e in spi_transfer_start spi_transfer_stop spi_message_start spi_message_done spi_message_submit; do
  echo "---- format $e"; cat $T/events/spi/$e/format 2>&1 | grep -E "^name|print fmt|field:" | head -12
done
echo "== kernel config bits (if exposed):"; (zcat /proc/config.gz 2>/dev/null | grep -E "CONFIG_FTRACE=|CONFIG_TRACING=|CONFIG_EVENT_TRACING=|CONFIG_SPI=|CONFIG_SPI_MT65XX|CONFIG_HZ=") || echo "no /proc/config.gz"
echo "== fb0 blank:"; cat /sys/class/graphics/fb0/blank 2>&1
