cd ~/miker
echo "=== kernel version"; head -4 Makefile | grep -E "VERSION|PATCHLEVEL|SUBLEVEL"
echo "=== spi trace events (include/trace/events/spi.h): transfer events and what they print"
grep -n -E "TRACE_EVENT|DEFINE_EVENT|TP_printk|__dynamic_array|__field|SPI_TRACE|tx_buf|rx_buf" include/trace/events/spi.h | head -40
echo "=== spi core uses trace_spi_transfer_start/stop?"; grep -n "trace_spi_transfer" drivers/spi/spi.c | head
