import sys
p = sys.argv[1]
s = open(p).read().split('\n')
# declare param after header include
for i, l in enumerate(s):
    if l.strip() == '#include "novatek-nvt-ts.h"':
        s[i+1:i+1] = ['', 'static int probematrix;', 'module_param(probematrix, int, 0444);',
                      'MODULE_PARM_DESC(probematrix, "after fw boot read a page x offset matrix (16 B each)");']
        break
# insert right before the postfast block (= right after the 0xa0 poll succeeded)
j = next(i for i, l in enumerate(s) if l.strip() == 'if (postfast) {')
code = [
    '\tif (probematrix) {',
    '\t\tstatic const u32 seq[] = {',
    '\t\t\t0x21c50, 0x21c08, 0x21c00, 0x21c60, 0x21c78, 0x21c50, 0x21c80, 0x21c88,',
    '\t\t\t0x21d00, 0x21b80, 0x3f300, 0x3f30e, 0x20000, 0x00080, 0x10000, 0x21c00,',
    '\t\t\t0x21c78, 0x21c50, 0x21c08 };',
    '\t\tu8 mbuf[16];',
    '\t\tint mi;',
    '\t\tfor (mi = 0; mi < ARRAY_SIZE(seq); mi++) {',
    '\t\t\tret = regmap_raw_read(data->regmap, seq[mi], mbuf, sizeof(mbuf));',
    '\t\t\tif (ret)',
    '\t\t\t\tdev_info(dev, "NVTDUMP matrix read 0x%06x err=%d", seq[mi], ret);',
    '\t\t}',
    '\t}',
]
s[j:j] = code
open(p, 'w').write('\n'.join(s).replace('err=%d"', 'err=%d' + chr(92) + 'n"'))
