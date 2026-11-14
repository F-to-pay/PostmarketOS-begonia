#!/bin/bash
cd /mnt/c/Users/1342c/workspace/Begonia/wifi/module
llvm-nm -u consys-bringup.ko | sed -E 's/^ *U +//' | grep -v '^$' | sort -u > ref/undef.txt
wc -l < ref/undef.txt
