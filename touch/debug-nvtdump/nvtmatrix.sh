#!/bin/sh
# Loads the instrumented touch modules with several parameter combinations; marks each run in the kernel log.
# core: eng_reset (0/1), phys (0/1)   spi: pgdelay (0 = stock single message, 200 = Xiaomi style split page select)
run() {
	tag=$1; e=$2; p=$3; d=$4
	rmmod novatek_nvt_ts_spi 2>/dev/null
	rmmod novatek_nvt_ts_core 2>/dev/null
	sleep 1
	echo "NVTRUN begin $tag eng_reset=$e phys=$p pgdelay=$d" > /dev/kmsg
	insmod /tmp/novatek-nvt-ts-core.ko eng_reset=$e phys=$p
	insmod /tmp/novatek-nvt-ts-spi.ko pgdelay=$d
	sleep 5
	echo "NVTRUN end $tag" > /dev/kmsg
}
run A 0 0 0
run B 0 1 0
run C 1 1 200
run D 0 0 200
echo "matrix done"
