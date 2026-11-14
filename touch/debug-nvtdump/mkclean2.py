import shutil, sys
# minimal fix against pristine HEAD: DT irq trigger + params read from the event buffer (with page select). SPI driver untouched.
d = sys.argv[1]
shutil.copy(f'{d}/novatek-nvt-ts-core.c', f'{d}/novatek-nvt-ts-core.c.orig')
p = f'{d}/novatek-nvt-ts-core.c'
s = open(p).read()

old = '''	error = devm_request_threaded_irq(dev, irq, NULL, nvt_ts_irq,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN |
						nvt_ts_irq_type[irq_type],
					  dev_name(dev), data);'''
assert s.count(old) == 1
new = '''	/*
	 * The IRQ trigger type from the firmware parameters is not reliable,
	 * e.g. the Redmi Note 8 Pro reports "falling" but the interrupt line is
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

old = '''	error = regmap_raw_read(data->regmap, NVT_TS_PARAMETERS_START,
				data->buf, NVT_TS_PARAMS_SIZE);'''
assert s.count(old) == 1
new = '''	/*
	 * Read the parameters relative to the event buffer, like downstream
	 * does: this selects the event buffer page, which the touch event reads
	 * in nvt_ts_irq() rely on (they use no page select of their own).
	 */
	error = regmap_raw_read(data->regmap,
				data->memory_map->event_buffer | NVT_TS_PARAMETERS_START,
				data->buf, NVT_TS_PARAMS_SIZE);'''
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print('ok')
