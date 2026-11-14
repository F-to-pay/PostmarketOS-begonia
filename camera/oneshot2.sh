#!/bin/sh
# 2 MP slots: one candidate per bus per boot. sudo sh oneshot2.sh <ov|gc> <ov|gc>   (slot 2, slot 3)
D=$(dirname "$(readlink -f "$0")"); P=/sys/kernel/debug/begonia-cam/power
echo "2 on ${3:-E}" > $P; sleep 0.3; timeout 15 python3 $D/oneshot8.py 2 1101a000 $1; echo "2 off" > $P
echo "3 on" > $P; sleep 0.3; timeout 15 python3 $D/oneshot8.py 3 11011000 $2; echo "3 off" > $P
