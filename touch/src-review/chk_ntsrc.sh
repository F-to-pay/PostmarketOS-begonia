cd ~
echo "--- diff k71 vs ntsrc (spi.c)"; diff <(sed -n 1,9999p k71/drivers/input/touchscreen/novatek-nvt-ts-spi.c) ntsrc/drivers/input/touchscreen/novatek-nvt-ts-spi.c | head -40; echo "(diff rc=$?)"
echo "--- ntsrc version"; head -5 ntsrc/Makefile | grep -E "VERSION|PATCHLEVEL|SUBLEVEL"; ls ntsrc/.config ntsrc/Module.symvers 2>&1
echo "--- ntsrc-debug-backup"; ls -la ntsrc-debug-backup | head
echo "--- how were nvt616-spi.ko built? bash history hints"; grep -n -E "nvt|make|insmod|clang" ~/.bash_history | tail -15
echo "--- toolchain"; command -v clang gcc make aarch64-linux-gnu-gcc ld.lld llvm-ar 2>&1 | head
echo "--- cs_change in spi-mt65xx"; grep -n -E "cs_change|PAUSE_EN|set_cs" ~/ntsrc/drivers/spi/spi-mt65xx.c | head -20
