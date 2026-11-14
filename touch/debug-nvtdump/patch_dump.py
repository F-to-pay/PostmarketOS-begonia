import sys

# "@@NL@@" is replaced by a backslash + n at the very end (keeps the C string escape intact).
p = sys.argv[1]
src = open(p).read().split('\n')
out = []
for line in src:
    st = line.strip()
    if st.startswith('static int pgdelay = 200;'):
        out.append('static int nvtdump = 600;')
        out.append('module_param(nvtdump, int, 0644);')
        out.append('MODULE_PARM_DESC(nvtdump, "log raw SPI frames");')
        out.append('static atomic_t nvt_dump_cnt = ATOMIC_INIT(0);')
        out.append('#define NVT_DUMP_OK() (atomic_inc_return(&nvt_dump_cnt) <= nvtdump)')
        out.append('static int pgdelay = 0;	/* stock single-message shape; load with pgdelay=200 for Xiaomi style */')
        continue
    if st == 'if (status != 0xff) {':
        out.append('\tif (NVT_DUMP_OK()) {')
        out.append('\t\tu32 r_ = *(u32 *)reg_buf;')
        out.append('\t\tdev_info(&spi->dev, "NVTDUMP R reg=0x%06x page=0x%03x cmd=0x%02x len=%zu status=0x%02x data=%*ph@@NL@@",')
        out.append('\t\t\t r_, r_ >> 7, r_ & 0x7f, val_size, status, (int)min_t(size_t, val_size, 24), val_buf);')
        out.append('\t}')
    if st == 'return nvt_ts_spi_reg_xfer(spi, data, true, &xfer, 1);':
        out.append('\tif (count - sizeof(u32) <= 8 && NVT_DUMP_OK()) {')
        out.append('\t\tu32 w_ = *(u32 *)data;')
        out.append('\t\tdev_info(&spi->dev, "NVTDUMP W reg=0x%06x page=0x%03x cmd=0x%02x len=%zu data=%*ph@@NL@@",')
        out.append('\t\t\t w_, w_ >> 7, (w_ & 0x7f) | 0x80, count - sizeof(u32), (int)(count - sizeof(u32)), (const u8 *)data + sizeof(u32));')
        out.append('\t}')
    out.append(line)
text = '\n'.join(out).replace('@@NL@@', chr(92) + 'n')
open(p, 'w').write(text)
