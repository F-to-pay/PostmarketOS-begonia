set -e
rm -rf ~/nvtdump && mkdir -p ~/nvtdump && cd ~/nvtdump
S=~/ntsrc/drivers/input/touchscreen
cp $S/novatek-nvt-ts-core.c $S/novatek-nvt-ts-spi.c $S/novatek-nvt-ts.h .
grep -n "pgdelay" novatek-nvt-ts-spi.c | head -3
# 1) original single-message behaviour by default (stock shape); toggle with pgdelay=200
sed -i 's/static int pgdelay = 200;/static int pgdelay = 0;/' novatek-nvt-ts-spi.c
# 2) dump parameter + counter
python3 - <<'PY'
import re
p='novatek-nvt-ts-spi.c'
s=open(p).read()
s=s.replace('static int pgdelay = 0;', 'static int nvtdump = 1;\nmodule_param(nvtdump, int, 0644);\nMODULE_PARM_DESC(nvtdump, "log raw SPI frames (max count)");\nstatic atomic_t nvt_dump_cnt = ATOMIC_INIT(0);\n#define NVT_DUMP_OK() (nvtdump && atomic_inc_return(&nvt_dump_cnt) <= 600)\nstatic int pgdelay = 0;',1)
# read: log after transfer
old='''	if (status != 0xff) {
		dev_err(&spi->dev, "read failed, status: 0x%02x\n", status);
		return -EIO;
	}'''
new='''	if (NVT_DUMP_OK()) {
		u32 r = *(u32 *)reg_buf;
		dev_info(&spi->dev, "NVTDUMP R reg=0x%06x page=0x%03x cmd=0x%02x len=%zu status=0x%02x data=%*ph\n",
			 r, r >> 7, r & 0x7f, val_size, status, (int)min_t(size_t, val_size, 24), val_buf);
	}
	if (status != 0xff) {
		dev_err(&spi->dev, "read failed, status: 0x%02x\n", status);
		return -EIO;
	}'''
assert old in s; s=s.replace(old,new,1)
# write: log small writes only
old='''	struct spi_transfer xfer = {
		.tx_buf = data + sizeof(u32),
		.len = count - sizeof(u32),
	};
'''
new='''	struct spi_transfer xfer = {
		.tx_buf = data + sizeof(u32),
		.len = count - sizeof(u32),
	};

	if (count - sizeof(u32) <= 8 && NVT_DUMP_OK()) {
		u32 r = *(u32 *)data;
		dev_info(&spi->dev, "NVTDUMP W reg=0x%06x page=0x%03x cmd=0x%02x len=%zu data=%*ph\n",
			 r, r >> 7, (r & 0x7f) | 0x80, count - sizeof(u32),
			 (int)(count - sizeof(u32)), data + sizeof(u32));
	}
'''
assert old in s; s=s.replace(old,new,1)
open(p,'w').write(s)
PY
cat > Makefile <<'MK'
obj-m += novatek-nvt-ts-core.o novatek-nvt-ts-spi.o
MK
grep -n "NVTDUMP\|nvtdump\|pgdelay = " novatek-nvt-ts-spi.c | head
echo "--- build"
make -C ~/ntsrc M=$HOME/nvtdump ARCH=arm64 LLVM=1 modules 2>&1 | tail -15
ls -la *.ko
for f in *.ko; do modinfo $f | grep -E "^(name|vermagic|depends|parm)"; done
