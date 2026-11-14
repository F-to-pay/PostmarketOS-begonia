#!/bin/sh
# Full dump of SENINF (top, cam_mux, seninf/tg/mux 0..4) + CAMSV0 + camsys into a file. sudo sh dumpall.sh out.txt
D=/sys/kernel/debug/camsv-tm
OUT=${1:-/tmp/dump.txt}; : > $OUT
rd() { echo "$1 $2 $3" > $D/r; cat $D/r >> $OUT; }
rd seninf 0 40; rd seninf 400 80
for n in 0 1 2 3 4; do B=$((n * 0x1000)); rd seninf $(printf %x $((B + 0x200))) 20; rd seninf $(printf %x $((B + 0x600))) 10; rd seninf $(printf %x $((B + 0xd00))) 30; done
rd camsv 0 100; rd camsv 100 40; rd camsv 200 60; rd camsv 400 20; rd camsv 500 30
rd camsys 0 40
wc -l < $OUT
