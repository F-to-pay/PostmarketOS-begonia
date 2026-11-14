#!/bin/bash
set -u
SRC=/mnt/c/Users/1342c/workspace/Begonia/wifi/mtk-modules-r-oss/connectivity/wlan/core/gen4m
DST=$HOME/gen4m-port
rm -rf $DST && mkdir -p $DST && cp -r $SRC/. $DST/ 2>/dev/null
cd $DST
echo "files: $(find . -name '*.c' | wc -l) .c, $(find . -name '*.h' | wc -l) .h"
sed -n 1,60p Makefile | grep -nE '^(MTK_|ifeq|ifneq|ccflags|CONFIG|export|include|WLAN_CHIP_LIST|obj-)' | head -30
