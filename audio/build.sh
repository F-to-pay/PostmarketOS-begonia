#!/bin/bash
# Run in WSL:  bash build.sh
# Assumes that ../module/build.sh has already prepared the kernel tree in $KDIR
# (phone config, modules_prepare). Only the audio modules + DTB are built here.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
KDIR=${KDIR:-$HOME/mt6785-linux}
MK=(make -C "$KDIR" ARCH=arm64 LLVM=1 LOCALVERSION= -j"$(nproc)")

[ -f "$KDIR/include/config/kernel.release" ] || { echo "kernel tree not prepared: run bash ../module/build.sh first"; exit 1; }
echo "kernel.release = $(cat "$KDIR/include/config/kernel.release")   (phone: 6.16.4)"

cd "$HERE"
make KDIR="$KDIR" all 2>&1 | tee build.log
find . -name '*.ko' | sort
for k in $(find . -name '*.ko'); do printf '%-40s ' "$k"; modinfo "$k" | grep -E '^vermagic' || true; done

# DTB: audio nodes next to the consys node
cd "$KDIR"
DTS=arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dts
cp "$HERE/audio-begonia.dtsi" arch/arm64/boot/dts/mediatek/audio-begonia.dtsi
grep -q audio-begonia.dtsi "$DTS" || echo '#include "audio-begonia.dtsi"' >> "$DTS"
HOSTCC_FIX="clang -Wno-incompatible-pointer-types-discards-qualifiers"
"${MK[@]}" "HOSTCC=$HOSTCC_FIX" mediatek/mt6785-xiaomi-begonia.dtb
ls -l arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dtb
echo OK
