cd ~/ntsrc
echo "--- build state"; ls -la include/generated/autoconf.h include/generated/utsrelease.h include/config/auto.conf Module.symvers vmlinux 2>&1 | head
echo "--- built objects in touchscreen dir"; ls -la --time-style=long-iso drivers/input/touchscreen/novatek* 2>&1 | head -20
echo "--- utsrelease / kernelrelease"; cat include/config/kernel.release 2>/dev/null; cat include/generated/utsrelease.h 2>/dev/null
echo "--- vermagic of debug module and of phone's"; for f in /mnt/c/Users/1342c/workspace/pmos-begonia/nvt-debug/nvt616-spi-pgdelay.ko /mnt/c/Users/1342c/workspace/pmos-begonia/nvt-debug/nvt616-core.ko; do echo $f; modinfo $f 2>&1 | grep -E "vermagic|name|depends|srcversion"; done
echo "--- toolchain versions"; clang --version | head -1; ld.lld --version | head -1; make --version | head -1
echo "--- ntsrc spi/core debug state (how many edits vs k71)"; diff -q ~/k71/drivers/input/touchscreen/novatek-nvt-ts-core.c drivers/input/touchscreen/novatek-nvt-ts-core.c
echo "--- Makefile flags used (LLVM?)"; grep -m3 -E "^(CC|LLVM|ARCH)" .config 2>/dev/null; grep -E "CONFIG_CC_VERSION_TEXT|CONFIG_CC_IS_CLANG|CONFIG_MODVERSIONS|CONFIG_MODULE_SIG|CONFIG_SHADOW_CALL_STACK|CONFIG_LOCALVERSION" .config
