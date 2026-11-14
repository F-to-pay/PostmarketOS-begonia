#!/bin/sh
# Variants of MCH channel recovery after a NACK (PIO): one variant per bus. sudo sh mch-test.sh <act2> <act4> <act7>
P=/sys/module/i2c_mt65xx_mch/parameters
B=$(dirname "$(readlink -f "$0")")/../i2c-baseline.py
PC=/sys/kernel/debug/pinctrl/10005000.pinctrl-pinctrl_paris/pins
run() { # unit act scl sda
	echo $2 > $P/mch_nack_act; echo ${ACKMASK:-1} > $P/mch_ackmask
	echo "### i2c@$1 act=$2 ackmask=$(cat $P/mch_ackmask)"
	timeout 10 python3 $B 0x7a $1
	echo "  lines after NACK: $(grep -E "^pin ($3|$4) " $PC | awk '{print $2, $NF, $(NF-1)}' | tr '\n' ' ')"
	for k in 2 3; do timeout 10 python3 $B 0x7a $1; done
}
run 11009000 ${1:-0x19} 112 113
run 11011000 ${2:-0x21} 110 111
run 1101a000 ${3:-0x45} 198 199
