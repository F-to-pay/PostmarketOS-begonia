import sys

# Adds: 'poison' param (rx buffers pre-filled with 0xaa before every read), long-read dump rows, and long matrix reads.
p = sys.argv[1]
s = open(p).read().split('\n')

for i, l in enumerate(s):
    if l.strip() == '#include "novatek-nvt-ts.h"':
        s[i+1:i+1] = ['', 'static int poison = 1;', 'module_param(poison, int, 0444);',
                      'MODULE_PARM_DESC(poison, "pre-fill rx buffers with 0xaa before each read");']
        break

# poison right before the read transfer
for i, l in enumerate(s):
    if l.strip().startswith('ret = nvt_ts_spi_reg_xfer(spi, reg_buf, false, xfers,'):
        s[i:i] = ['\tif (poison) {', '\t\tstatus = 0xaa;', '\t\tmemset(val_buf, 0xaa, val_size);', '\t}']
        break

# extra dump rows for long reads: right after the continuation line of the "NVTDUMP R" dev_info
k = next(i for i, l in enumerate(s) if 'NVTDUMP R reg=' in l)
assert 'val_buf);' in s[k+1], s[k+1]
s[k+2:k+2] = [
    '\t\tif (val_size > 24) {',
    '\t\t\tsize_t off_;',
    '\t\t\tfor (off_ = 24; off_ < min_t(size_t, val_size, 256); off_ += 32)',
    '\t\t\t\tdev_info(&spi->dev, "NVTDUMP R+ off=%zu data=%*ph@@NL@@", off_,',
    '\t\t\t\t\t (int)min_t(size_t, 32, val_size - off_), (u8 *)val_buf + off_);',
    '\t\t}',
]

# long reads at the end of the probematrix loop
m = next(i for i, l in enumerate(s) if 'NVTDUMP matrix read' in l)
assert s[m+1].strip() == '}', s[m+1]
s[m+2:m+2] = [
    '\t\t{',
    '\t\t\tstatic const u32 bl[] = { 0x3f300, 0x21c00, 0x21c80 };',
    '\t\t\tu8 big[128];',
    '\t\t\tfor (mi = 0; mi < ARRAY_SIZE(bl); mi++) {',
    '\t\t\t\tret = regmap_raw_read(data->regmap, bl[mi], big, mi == 2 ? 64 : 128);',
    '\t\t\t\tif (ret)',
    '\t\t\t\t\tdev_info(dev, "NVTDUMP long read 0x%06x err=%d@@NL@@", bl[mi], ret);',
    '\t\t\t}',
    '\t\t}',
]
text = '\n'.join(s).replace('@@NL@@', chr(92) + 'n')
open(p, 'w').write(text)
