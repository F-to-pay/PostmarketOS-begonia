import sys
# irqdelay: usleep before the first read in the irq thread; irqprobe: log INT line level + 4 extra reads (0.2/1/5/20 ms) for the first N irqs
p = sys.argv[1]
s = open(p).read()
anchor = 'static irqreturn_t nvt_ts_irq(int irq, void *dev_id)'
decl = ('static int irqdelay;\nmodule_param(irqdelay, int, 0444);\n'
        'MODULE_PARM_DESC(irqdelay, "usleep before the first read in the irq thread (us)");\n'
        'static int irqprobe;\nmodule_param(irqprobe, int, 0444);\n'
        'MODULE_PARM_DESC(irqprobe, "log line level and extra delayed reads for the first N irqs");\n\n')
assert s.count(anchor) == 1
s = s.replace(anchor, decl + anchor, 1)

old = '''	u8 *touch;

	error = regmap_raw_read(data->regmap, nvt_dbg_base(data) | NVT_TS_TOUCH_START, data->buf,
				data->max_touches * NVT_TS_TOUCH_SIZE);
	if (error)
		return IRQ_HANDLED;
'''
new = '''	u8 *touch;
	static int nprobe;
	bool dbg = irqprobe && nprobe < irqprobe;
	bool lv0 = false, lv1 = false;

	if (irqdelay)
		usleep_range(irqdelay, irqdelay + 20);
	if (dbg)
		irq_get_irqchip_state(irq, IRQCHIP_STATE_LINE_LEVEL, &lv0);

	error = regmap_raw_read(data->regmap, nvt_dbg_base(data) | NVT_TS_TOUCH_START, data->buf,
				data->max_touches * NVT_TS_TOUCH_SIZE);
	if (error)
		return IRQ_HANDLED;

	if (dbg) {
		static const int dly[] = { 200, 1000, 5000, 20000 };
		u8 xb[60];
		int k, e2;

		nprobe++;
		irq_get_irqchip_state(irq, IRQCHIP_STATE_LINE_LEVEL, &lv1);
		dev_info(dev, "IRQDBG #%d line before=%d after=%d first=%*ph@@NL@@", nprobe, lv0, lv1, 12, data->buf);
		for (k = 0; k < ARRAY_SIZE(dly); k++) {
			bool lv = false;

			usleep_range(dly[k], dly[k] + 50);
			e2 = regmap_raw_read(data->regmap, nvt_dbg_base(data) | NVT_TS_TOUCH_START, xb,
					     data->max_touches * NVT_TS_TOUCH_SIZE);
			irq_get_irqchip_state(irq, IRQCHIP_STATE_LINE_LEVEL, &lv);
			dev_info(dev, "IRQDBG #%d +%dus err=%d line=%d first=%*ph@@NL@@", nprobe, dly[k], e2, lv, 12, xb);
		}
	}
'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
open(p, 'w').write(s.replace('@@NL@@', chr(92) + 'n'))
print('core irqdbg patched')
