import sys
p = sys.argv[1]
s = open(p).read().split('\n')
for i, l in enumerate(s):
    if l.strip() == '#include "novatek-nvt-ts.h"':
        s[i+1:i+1] = ['', 'static int chippre;', 'module_param(chippre, int, 0444);',
                      'MODULE_PARM_DESC(chippre, "read chip id (0x1f64e, 7 B) right after the bootloader reset, like downstream");']
        break
k = next(i for i, l in enumerate(s) if 'nvt_ts_spi_bootloader_reset(data->regmap, mem_map)' in l and 'ret =' in l)
assert s[k+1].strip().startswith('if (ret)') and s[k+4].strip() == '}', s[k+1:k+5]
s[k+5:k+5] = [
    '\tif (chippre) {',
    '\t\tu8 cid_[8];',
    '\t\tint cr_ = regmap_raw_read(data->regmap, 0x1f64e, cid_, 7);',
    '\t\tdev_info(dev, "NVTDUMP chip-id read after reset err=%d@@NL@@", cr_);',
    '\t}',
]
text = '\n'.join(s)
old = '0x21c50, 0x21c08, 0x21c00, 0x21c60, 0x21c78, 0x21c50, 0x21c80, 0x21c88,'
assert old in text
text = text.replace(old, '0x1f64e, 0x21c50, 0x21c08, 0x21c00, 0x21c60, 0x21c78, 0x21c50, 0x21c80, 0x21c88,')
open(p, 'w').write(text.replace('@@NL@@', chr(92) + 'n'))
