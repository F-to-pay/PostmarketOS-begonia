mount -t debugfs none /sys/kernel/debug 2>/dev/null
T=/sys/kernel/debug/tracing
echo 0 > $T/tracing_on
echo > $T/trace
echo 32768 > $T/buffer_size_kb 2>/dev/null
echo 0 > $T/events/enable
echo 1 > $T/events/spi/enable
echo "begin" > $T/trace_marker 2>/dev/null
echo 1 > $T/tracing_on
# power-cycle the screen: downstream nvt registers an fb notifier -> suspend/resume -> firmware download + fw info + first reads
echo 4 > /sys/class/graphics/fb0/blank
sleep 3
echo "unblank" > $T/trace_marker 2>/dev/null
echo 0 > /sys/class/graphics/fb0/blank
sleep 6
echo 0 > $T/tracing_on
echo "end" > $T/trace_marker 2>/dev/null
cat $T/trace > /tmp/spi_trace.txt
wc -l /tmp/spi_trace.txt
