set -e
rm -rf ~/nvtdump && mkdir -p ~/nvtdump && cd ~/nvtdump
S=~/ntsrc/drivers/input/touchscreen
cp $S/novatek-nvt-ts-core.c $S/novatek-nvt-ts-spi.c $S/novatek-nvt-ts.h .
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_dump.py novatek-nvt-ts-spi.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_irq.py novatek-nvt-ts-core.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_irqdbg.py novatek-nvt-ts-core.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_post.py novatek-nvt-ts-spi.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_matrix.py novatek-nvt-ts-spi.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_poison.py novatek-nvt-ts-spi.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_marker.py novatek-nvt-ts-spi.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_chip.py novatek-nvt-ts-spi.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_prebr.py novatek-nvt-ts-spi.c
python3 /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/patch_pgskip.py novatek-nvt-ts-spi.c
echo "obj-m += novatek-nvt-ts-core.o novatek-nvt-ts-spi.o" > Makefile
grep -n -E "NVTDUMP|nvtdump|pgdelay = " novatek-nvt-ts-spi.c | cut -c1-150
echo "--- build"
make -C ~/ntsrc M=$HOME/nvtdump ARCH=arm64 LLVM=1 KBUILD_MODPOST_WARN=1 modules 2>&1 | grep -v -E "^make" | tail -15
ls -la *.ko
for f in *.ko; do modinfo $f | grep -E "^(name|vermagic|depends|parm)"; done
cp *.ko /mnt/c/Users/1342c/workspace/pmos-begonia/nvtdump/
