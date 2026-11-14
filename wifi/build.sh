#!/bin/bash
# Run in WSL:  bash build.sh
# 1) prepares the kernel tree (~/mt6785-linux) with the phone config
# 2) builds consys-bringup.ko (clang/LLD - like the kernel on the phone)
# 3) builds the phone DTB with the consys node (for checking; flashing boot.img - separately)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
KDIR=${KDIR:-$HOME/mt6785-linux}
J=$(nproc)
# HOSTCC: resolve_btfids/libbpf (needed with DEBUG_INFO_BTF) does not build with a fresh clang+glibc because of -Werror
# in third-party code (discards-qualifiers); the warning itself is silenced on the host compiler.
HOSTCC_FIX="clang -Wno-incompatible-pointer-types-discards-qualifiers"
MK=(make -C "$KDIR" ARCH=arm64 LLVM=1 LOCALVERSION= "HOSTCC=$HOSTCC_FIX" -j"$J")

cd "$KDIR"
[ -f Makefile ] || { echo "no kernel tree in $KDIR (clone not checked out?)"; exit 1; }

# an empty .scmversion: otherwise setlocalversion appends "+" (the tree is in git) and the vermagic will not match the phone
: > .scmversion
# the phone config; LOCALVERSION_AUTO off => release == "6.16.<sublevel>" without a suffix
cp "$HERE/phone-config" .config
# IMPORTANT: do NOT touch DEBUG_INFO_BTF(+_MODULES) - the size of struct module depends on it (btf_data*),
# otherwise insmod gives "Invalid module format". Only things that do not affect the layout are changed.
scripts/config --file .config --disable LOCALVERSION_AUTO \
	--disable MODULE_SIG_ALL --set-str MODULE_SIG_KEY ""
"${MK[@]}" olddefconfig
"${MK[@]}" modules_prepare
echo "kernel.release = $(cat include/config/kernel.release)   (phone: 6.16.4)"

# module
cd "$HERE"
make KDIR="$KDIR" LOCALVERSION= all
file consys-bringup.ko | cut -c1-120
modinfo consys-bringup.ko | grep -E 'vermagic|license|alias'
echo "this_module size: $(llvm-readelf -S -W consys-bringup.ko | awk '/ .gnu.linkonce.this_module/ {print $6}')  (phone: 000500)"

# DTB to check the syntax and references (phandles, constants)
cd "$KDIR"
DTS=arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dts
cp "$HERE/consys-begonia.dtsi" arch/arm64/boot/dts/mediatek/consys-begonia.dtsi
grep -q consys-begonia.dtsi "$DTS" || echo '#include "consys-begonia.dtsi"' >> "$DTS"
"${MK[@]}" mediatek/mt6785-xiaomi-begonia.dtb
ls -l arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dtb
echo "OK"
