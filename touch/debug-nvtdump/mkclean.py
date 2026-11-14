import re, shutil, sys
d = sys.argv[1]
for f in ('novatek-nvt-ts-core.c', 'novatek-nvt-ts-spi.c'):
    shutil.copy(f'{d}/{f}', f'{d}/{f}.orig')

# ---- spi
p = f'{d}/novatek-nvt-ts-spi.c'
s = open(p).read()

old_xfer = s[s.index('static int nvt_ts_spi_xfer('):s.index('static int nvt_ts_spi_reg_xfer(')]
new_xfer = '''struct nvt_ts_spi {
	struct spi_device *spi;
	/* Page currently selected in the controller, 0 if unknown */
	u32 page;
};

static int nvt_ts_spi_xfer(struct nvt_ts_spi *ts, u32 page_addr, u8 cmd,
			   struct spi_transfer *xfers, size_t num_xfers,
			   bool skip_page)
{
	struct spi_transfer cmd_xfer = { .tx_buf = &cmd, .len = 1 };
	struct spi_message msg;

	u8 page_buf[] = { 0xff, page_addr >> 8, page_addr };
	struct spi_transfer page_xfer = {
		.tx_buf = page_buf,
		.len = ARRAY_SIZE(page_buf),
		.cs_change = 1,
	};

	spi_message_init(&msg);

	if (page_addr && !(skip_page && page_addr == ts->page)) {
		spi_message_add_tail(&page_xfer, &msg);
		ts->page = page_addr;
	} else if (!page_addr) {
		ts->page = 0;
	}

	spi_message_add_tail(&cmd_xfer, &msg);

	for (int i = 0; i < num_xfers; i++)
		spi_message_add_tail(&xfers[i], &msg);

	return spi_sync(ts->spi, &msg);
}

'''
s = s.replace(old_xfer, new_xfer, 1)

old_reg = s[s.index('static int nvt_ts_spi_reg_xfer('):s.index('static int nvt_ts_spi_read(')]
new_reg = '''static int nvt_ts_spi_reg_xfer(struct nvt_ts_spi *ts, const void *reg_buf,
			       bool write, struct spi_transfer *xfers,
			       size_t num_xfers, bool skip_page)
{
	u32 reg = *(u32 *)reg_buf;
	u32 page_addr = reg >> 7;
	u8 cmd = reg & GENMASK(6, 0);

	if (write)
		cmd |= BIT(7);

	return nvt_ts_spi_xfer(ts, page_addr, cmd, xfers, num_xfers, skip_page);
}

'''
s = s.replace(old_reg, new_reg, 1)

old_read = s[s.index('static int nvt_ts_spi_read('):s.index('static int nvt_ts_spi_write(')]
new_read = '''static int nvt_ts_spi_read(void *context, const void *reg_buf, size_t reg_size,
			   void *val_buf, size_t val_size)
{
	struct nvt_ts_spi *ts = context;
	bool skip_page = true;
	u8 status;
	int ret;

	struct spi_transfer xfers[] = {
		{ .rx_buf = &status, .len = 1 },
		{ .rx_buf = val_buf, .len = val_size },
	};

	/*
	 * The controller keeps the selected page across transfers and the
	 * downstream driver does not re-select it for every touch report.
	 * Sending the page select in front of each read of the touch event
	 * buffer makes the controller return its idle stream instead of the
	 * touch data, so only select the page when it is not already selected.
	 * Retry with an explicit page select if the controller did not accept
	 * the read, in case the selected page was lost (e.g. after a reset).
	 */
retry:
	ret = nvt_ts_spi_reg_xfer(ts, reg_buf, false, xfers,
				  ARRAY_SIZE(xfers), skip_page);
	if (ret)
		return ret;

	if (status != 0xff && skip_page) {
		skip_page = false;
		goto retry;
	}

	if (status != 0xff) {
		dev_err(&ts->spi->dev, "read failed, status: 0x%02x\n", status);
		return -EIO;
	}

	return 0;
}

'''
s = s.replace(old_read, new_read, 1)

old_write = s[s.index('static int nvt_ts_spi_write('):s.index('static const struct regmap_config')] if 'static const struct regmap_config' in s else None
assert old_write
new_write = '''static int nvt_ts_spi_write(void *context, const void *data, size_t count)
{
	struct nvt_ts_spi *ts = context;

	struct spi_transfer xfer = {
		.tx_buf = data + sizeof(u32),
		.len = count - sizeof(u32),
	};

	return nvt_ts_spi_reg_xfer(ts, data, true, &xfer, 1, false);
}

'''
# keep anything between write function end and regmap_config untouched
w_start = s.index('static int nvt_ts_spi_write(')
w_end = s.index('\n}\n', w_start) + 3
s = s[:w_start] + new_write.rstrip('\n') + '\n' + s[w_end:]

s = s.replace('''	struct regmap *regmap;

	regmap =
		devm_regmap_init(&spi->dev, NULL, spi, &nvt_ts_spi_regmap_conf);''',
'''	struct nvt_ts_spi *ts;
	struct regmap *regmap;

	ts = devm_kzalloc(&spi->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->spi = spi;

	regmap =
		devm_regmap_init(&spi->dev, NULL, ts, &nvt_ts_spi_regmap_conf);''', 1)
open(p, 'w').write(s)

# ---- core
p = f'{d}/novatek-nvt-ts-core.c'
s = open(p).read()
old = '''	error = devm_request_threaded_irq(dev, irq, NULL, nvt_ts_irq,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN |
						nvt_ts_irq_type[irq_type],
					  dev_name(dev), data);'''
assert s.count(old) == 1
new = '''	/*
	 * The IRQ trigger type from the firmware parameters is not reliable,
	 * e.g. Redmi Note 8 Pro reports "falling" but the interrupt line is
	 * wired (and described) as rising edge, which is also what the
	 * downstream driver uses. Prefer the trigger type from firmware/DT.
	 */
	irq_flags = irq_get_trigger_type(irq);
	if (!irq_flags)
		irq_flags = nvt_ts_irq_type[irq_type];

	error = devm_request_threaded_irq(dev, irq, NULL, nvt_ts_irq,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN | irq_flags,
					  dev_name(dev), data);'''
s = s.replace(old, new, 1)
s = s.replace('	int error, width, height, irq_type;', '	int error, width, height, irq_type;\n	unsigned long irq_flags;', 1)
open(p, 'w').write(s)
print('ok')
