import sys
# pgskip: do not re-send the page select when the same page was selected by the previous access
# (downstream nvt_ts_work_func reads without it). A skipped read that fails (status != 0xff) is retried WITH the page select.
p = sys.argv[1]
s = open(p).read()

anchor = 'static int nvt_ts_spi_reg_xfer(struct spi_device *spi, const void *reg_buf,'
assert s.count(anchor) == 1
decl = '''static int pgskip;
module_param(pgskip, int, 0444);
MODULE_PARM_DESC(pgskip, "skip the page select when it is the same page as the previous access");
static u32 pgskip_last;
static bool pgskip_did;
static int pgskip_n, pgretry_n;
module_param(pgskip_n, int, 0444);
module_param(pgretry_n, int, 0444);

'''
s = s.replace(anchor, decl + anchor, 1)

old = '\tu32 page_addr = reg >> 7;\n\tu8 cmd = reg & GENMASK(6, 0);\n'
assert s.count(old) == 1, s.count(old)
new = old + '''
	pgskip_did = false;
	if (pgskip && page_addr && page_addr == pgskip_last) {
		page_addr = 0;
		pgskip_did = true;
		pgskip_n++;
	} else {
		pgskip_last = page_addr;
	}
'''
s = s.replace(old, new, 1)

anchor2 = '\tif (NVT_DUMP_OK()) {\n\t\tu32 r_ = *(u32 *)reg_buf;'
assert s.count(anchor2) == 1, s.count(anchor2)
retry = '''	if (status != 0xff && pgskip && pgskip_did) {
		pgskip_last = 0;
		pgretry_n++;
		ret = nvt_ts_spi_reg_xfer(spi, reg_buf, false, xfers, ARRAY_SIZE(xfers));
		if (ret)
			return ret;
	}

'''
s = s.replace(anchor2, retry + anchor2, 1)
open(p, 'w').write(s)
print('spi pgskip patched')
