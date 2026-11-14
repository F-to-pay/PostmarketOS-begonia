#!/bin/bash
# Build of gen4m (MT6632/AXI) for the 6.16 kernel (mt6785-mainline) with -k: error statistics.
# The shim (wmt_exp.h, gen4m_compat.h) is in wifi/gen4m-port/shim/include; changes to the vendor sources — in wifi/gen4m-port/patches.
W=/mnt/c/Users/1342c/workspace/Begonia/wifi
SRC=$W/mtk-modules-r-oss/connectivity/wlan/core/gen4m
DST=$HOME/gen4m-port
SHIM=$HOME/gen4m-shim
KDIR=$HOME/mt6785-linux
rm -rf "$SHIM" && mkdir -p "$SHIM" && cp -r $W/gen4m-port/shim/. "$SHIM"/
rm -rf "$DST" && mkdir -p "$DST" && cp -r "$SRC"/. "$DST"/
[ -d $W/gen4m-port/patches ] && cp -r $W/gen4m-port/patches/. "$DST"/ 2>/dev/null
cd "$DST" || exit 1
python3 $W/gen4m-port/port.py "$DST" || { echo "PORT.PY FAILED"; exit 3; }
sed -i '/^ccflags-y += -Werror$/d' Makefile      # the vendor forces -Werror; on 6.16 that is thousands of style errors
printf '
ccflags-y += -Wno-error -Wno-missing-prototypes -Wno-macro-redefined -Wno-null-pointer-subtraction -Wno-cast-function-type-strict -Wno-implicit-fallthrough -Wno-array-bounds -ferror-limit=0
' >> Makefile
make -C "$KDIR" M="$PWD" ARCH=arm64 LLVM=1 LOCALVERSION= KBUILD_MODPOST_WARN=1 \
  CONFIG_MTK_COMBO_WIFI=m CONFIG_MTK_COMBO_WIFI_HIF=axi WLAN_CHIP_ID=6785 CONFIG_MTK_PLATFORM=mt6785 MTK_COMBO_CHIP=CONNAC WIFI_IP_SET=2 MTK_ANDROID_EMI=y \
  KCFLAGS="-I$SHIM/include -include $SHIM/include/gen4m_compat.h -Wno-error -ferror-limit=0" \
  -k -j14 modules > build.out 2>&1
echo "make exit=$?"
echo "errors: $(grep -c 'error:' build.out)  warnings: $(grep -c 'warning:' build.out)"
echo "== files with errors (top)"
grep 'error:' build.out | sed -E 's/:[0-9]+:[0-9]+:.*//' | sort | uniq -c | sort -rn | head -12
grep -E "error:" build.out | sort -u | cut -c1-230 > errors.txt
echo "unique error lines: $(wc -l < errors.txt)  -> $DST/errors.txt"
cp errors.txt /mnt/c/Users/1342c/workspace/Begonia/wifi/gen4m-port/errors.txt
echo "== error kinds"
grep 'error:' build.out | sed -E 's/^[^:]+:[0-9]+:[0-9]+: //' | sed -E "s/'[^']*'/'X'/g" | sort | uniq -c | sort -rn | head -16
