#!/bin/bash
cd /mnt/c/Users/1342c/workspace/Begonia/wifi/module
zstd -dqf ref/snd-aloop.ko.zst -o ref/snd-aloop.ko
for f in ref/snd-aloop.ko consys-bringup.ko; do
  printf "%-22s " $f; llvm-readelf -S -W $f | awk '/gnu.linkonce.this_module/ {print "this_module size(hex)=" $6 "  name=" $2}'
done
