#!/bin/sh
# Builds the MT6785 audio modules right on the phone. Run: sh this_file
# 1) prepares the kernel tree ~/mt6785-linux (phone config, modules_prepare)
# 2) builds the audio/ modules (clang+lld, like the kernel itself)
# 3) builds the DTB with audio-begonia.dtsi
set -e
[ "$(id -u)" -ne 0 ] || { echo "Do not run as root/sudo: sh this_file (no sudo needed)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
KDIR=${KDIR:-/home/user/mt6785-linux}
J=$(nproc)
[ -f "$KDIR/Makefile" ] || { echo "no kernel tree in $KDIR (clone not finished?)"; exit 1; }

MK="make -C $KDIR ARCH=arm64 LLVM=1 LOCALVERSION= HOSTCC=clang -j$J"
cd "$KDIR"
: > .scmversion
cp "$HERE/../module/phone-config" .config
# do NOT touch the BTF options (the size of struct module depends on them)
scripts/config --file .config --disable LOCALVERSION_AUTO --disable MODULE_SIG_ALL --set-str MODULE_SIG_KEY ""
$MK olddefconfig
$MK modules_prepare
echo "kernel.release = $(cat include/config/kernel.release)   (phone: $(uname -r))"
[ "$(cat include/config/kernel.release)" = "$(uname -r)" ] || { echo "VERSION MISMATCH - stop"; exit 1; }

cd "$HERE"
make KDIR="$KDIR" all 2>&1 | tee build.log
echo "== modules =="
find . -name '*.ko' | sort
echo "== DTB =="
cd "$KDIR"
DTS=arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dts
cp "$HERE/audio-begonia.dtsi" arch/arm64/boot/dts/mediatek/audio-begonia.dtsi
grep -q audio-begonia.dtsi "$DTS" || echo '#include "audio-begonia.dtsi"' >> "$DTS"
$MK mediatek/mt6785-xiaomi-begonia.dtb
ls -l arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dtb
echo "DONE"
