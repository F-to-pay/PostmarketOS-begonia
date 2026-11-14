import sys
p = sys.argv[1]
s = open(p).read().split('\n')
# 1) declare param right after the header include
for i, l in enumerate(s):
    if l.strip() == '#include "novatek-nvt-ts.h"':
        s[i+1:i+1] = ['', 'static int postfast;', 'module_param(postfast, int, 0444);',
                      'MODULE_PARM_DESC(postfast, "disable SPI_RD_FAST again N ms after fw boot (0 = off)");']
        break
# 2) last "return 0;" before the nvtdump marker (= end of nvt_ts_spi_upload_firmware)
marker = next(i for i, l in enumerate(s) if l.strip().startswith('static int nvtdump'))
j = max(i for i in range(marker) if s[i].strip() == 'return 0;')
s[j:j+1] = [
    '\tif (postfast) {',
    '\t\tmsleep(postfast);',
    '\t\tret = regmap_write(data->regmap, mem_map->spi_rd_fast, 0x00);',
    '\t\tdev_info(dev, "NVTDUMP post-boot spi_rd_fast=0 write err=%d", ret);',
    '\t\tmsleep(5);',
    '\t}',
    '\treturn 0;']
open(p, 'w').write('\n'.join(s).replace('write err=%d"', 'write err=%d' + chr(92) + 'n"'))
