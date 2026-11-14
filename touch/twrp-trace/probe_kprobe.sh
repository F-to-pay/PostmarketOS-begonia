T=/sys/kernel/debug/tracing
echo "== kprobe/uprobe files:"; ls $T | grep -iE "kprobe|uprobe|dynamic|function|stack"
echo "== README mentions of fetch args / arrays:"; grep -n -iE "kprobe_events|fetcharg|x8\[|\[N\]|string|symbol" $T/README | head -20
echo "== kallsyms sample:"; grep -E " (spi_sync|spi_sync_locked|__spi_sync|spi_transfer_one_message|mtk_spi_transfer_one|mtk_spi_fifo_transfer|mtk_spi_setup_packet|spi_read_write|CTP_SPI_READ|nvt_set_page)$" /proc/kallsyms | head -20
echo "== kallsyms readable addresses?"; head -2 /proc/kallsyms
echo "== kptr_restrict:"; cat /proc/sys/kernel/kptr_restrict
