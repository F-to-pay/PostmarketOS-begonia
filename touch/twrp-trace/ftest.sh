T=/sys/kernel/debug/tracing
echo 0 > $T/tracing_on; echo > $T/trace
cat $T/available_tracers
echo function > $T/current_tracer 2>&1
printf "nvt_flash_read\nCTP_SPI_READ\nCTP_SPI_WRITE\n" > $T/set_ftrace_filter 2>&1
echo 1 > $T/tracing_on
/tmp/nvtspi R:1f64e:7 R:21c78:16
echo 0 > $T/tracing_on
echo "---- trace:"; grep -v "^#" $T/trace | head -20
echo nop > $T/current_tracer; echo > $T/set_ftrace_filter
echo "---- dmesg tail:"; dmesg | tail -4 | cut -c1-140
