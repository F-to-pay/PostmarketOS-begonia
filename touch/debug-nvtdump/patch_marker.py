import sys
p = sys.argv[1]
s = open(p).read().split('\n')
# insert right before the long-read block (the line holding 'static const u32 bl[]'), i.e. before its opening '{'
k = next(i for i, l in enumerate(s) if 'static const u32 bl[]' in l)
assert s[k-1].strip() == '{', s[k-1]
code = [
    '\t\tret = regmap_write(data->regmap, 0x21c40, 0x5a);',
    '\t\tdev_info(dev, "NVTDUMP marker write 0x21c40=5a err=%d@@NL@@", ret);',
    '\t\tret = regmap_write(data->regmap, 0x21c10, 0xa5);',
    '\t\tdev_info(dev, "NVTDUMP marker write 0x21c10=a5 err=%d@@NL@@", ret);',
    '\t\tret = regmap_raw_read(data->regmap, 0x21c40, mbuf, 1);',
    '\t\tret = regmap_raw_read(data->regmap, 0x21c10, mbuf, 1);',
]
s[k-1:k-1] = code
open(p, 'w').write('\n'.join(s).replace('@@NL@@', chr(92) + 'n'))
