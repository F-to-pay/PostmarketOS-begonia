import sys
# v3 = v2 + retry the firmware upload / params read (the controller is not ready right after the supply comes up at boot)
p = sys.argv[1] + '/novatek-nvt-ts-core.c'
s = open(p).read()

old = s[s.index('\terror = nvt_ts_upload_firmware(data);\n\tif (error) {\n\t\tgpiod_set_value_cansleep(data->reset_gpio, 1);'):
        s.index('\tgpiod_set_value_cansleep(data->reset_gpio, 1); /* Put back in reset */')]
new = '''	/*
	 * The controller may not be ready yet when its supply was only just
	 * enabled (early boot), so retry the upload + params read a few times,
	 * resetting the controller in between.
	 */
	for (retry = 0; retry < NVT_TS_PROBE_RETRIES; retry++) {
		error = nvt_ts_upload_firmware(data);
		if (!error) {
			/* Wait for controller to come out of reset before params read */
			msleep(100);
			/*
			 * Read the parameters relative to the event buffer, like downstream
			 * does: this selects the event buffer page, which the touch event
			 * reads in nvt_ts_irq() rely on (they use no page select of their own).
			 */
			error = regmap_raw_read(data->regmap,
						data->memory_map->event_buffer | NVT_TS_PARAMETERS_START,
						data->buf, NVT_TS_PARAMS_SIZE);
			if (!error)
				break;
		}

		dev_dbg(dev, "params not available (%d), retry %d\\n", error, retry);
		gpiod_set_value_cansleep(data->reset_gpio, 1);
		msleep(100);
		gpiod_set_value_cansleep(data->reset_gpio, 0);
		msleep(10);
	}
'''
s = s.replace(old, new, 1)
s = s.replace('\tint error, width, height, irq_type;\n', '\tint error, width, height, irq_type, retry;\n', 1)
s = s.replace('#include "novatek-nvt-ts.h"\n', '#include "novatek-nvt-ts.h"\n\n#define NVT_TS_PROBE_RETRIES\t5\n', 1)
open(p, 'w').write(s)
print('v3 ok')
