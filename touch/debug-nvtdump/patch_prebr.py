import sys

# prebr : read back the CRC bank registers and the event-buffer bytes we just wrote, right before boot_ready is written
# dspoll: downstream-like wait (mdelay(5)) + 10 polls of 6 bytes at 0x21c60, 10 ms apart, right after boot_ready is written
p = sys.argv[1]
s = open(p).read().split('\n')

for i, l in enumerate(s):
    if l.strip() == '#include "novatek-nvt-ts.h"':
        s[i+1:i+1] = ['', 'static int prebr;', 'module_param(prebr, int, 0444);',
                      'MODULE_PARM_DESC(prebr, "read back written registers before boot_ready");',
                      'static int dspoll;', 'module_param(dspoll, int, 0444);',
                      'MODULE_PARM_DESC(dspoll, "downstream-like mdelay(5) + polling after boot_ready");']
        break

k = next(i for i, l in enumerate(s) if l.strip().startswith('ret = regmap_write(data->regmap, mem_map->boot_ready, 0x01);'))
assert s[k+1].strip() == 'if (ret) {' and s[k+4].strip() == '}', s[k+1:k+5]

# 2nd insertion first (later in the file)
s[k+5:k+5] = [
    '\tif (dspoll) {',
    '\t\tu8 sbuf_[8];',
    '\t\tint si_;',
    '\t\tmsleep(5);',
    '\t\tfor (si_ = 0; si_ < 10; si_++) {',
    '\t\t\tregmap_raw_read(data->regmap, 0x21c60, sbuf_, 6);',
    '\t\t\tdev_info(dev, "NVTDUMP dspoll %d done@@NL@@", si_);',
    '\t\t\tmsleep(10);',
    '\t\t}',
    '\t}',
]
s[k:k] = [
    '\tif (prebr) {',
    '\t\tstatic const u32 pb_regs_[] = { 0x3f128, 0x3f118, 0x3f100, 0x3f12c, 0x3f130, 0x3f104,',
    '\t\t\t\t\t\t0x21c60, 0x21c50, 0x3f30e, 0x3f10d, 0x3f0fe, 0x3f310 };',
    '\t\tstatic const u8 pb_len_[] = { 3, 3, 4, 3, 3, 4, 1, 1, 1, 1, 1, 1 };',
    '\t\tu8 pbuf_[8];',
    '\t\tint pi_;',
    '\t\tdev_info(dev, "NVTDUMP --- read-back before boot_ready (written: 3f128=00 00 00 3f118=7b e8 00 3f100=6c 58 9e 0b 3f12c=00 77 02 3f130=17 03 00 3f104=92 92 6c 52 21c60=00 21c50=ae)@@NL@@");',
    '\t\tfor (pi_ = 0; pi_ < ARRAY_SIZE(pb_regs_); pi_++)',
    '\t\t\tregmap_raw_read(data->regmap, pb_regs_[pi_], pbuf_, pb_len_[pi_]);',
    '\t\tdev_info(dev, "NVTDUMP --- read-back done@@NL@@");',
    '\t}',
]
open(p, 'w').write('\n'.join(s).replace('@@NL@@', chr(92) + 'n'))
