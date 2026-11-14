# Redmi Note 8 Pro (begonia): NT36672A SPI touch works with a 2-hunk fix to novatek-nvt-ts-core

DRAFT, not sent. Tested on mainline 6.16.4 (+ the begonia pmOS patches), Redmi Note 8 Pro (MT6785), NT36672A TDDI on SPI (`11019000.spi`), CSOT panel, firmware `fw01`.

## Result

With the patch below the touchscreen probes, is silent when idle (0 IRQs / 10 s) and delivers events while touching
(1194 input events in a 20 s test, no `read failed`, no `slot ... out of range`). Without it the probe fails (stock) or, with the params read worked around, the touch is dead with ~91 IRQs/s while idle.

## Three problems (all fixed by the patch below)

1. **IRQ trigger type.** The core takes the trigger from the firmware parameter (`irq_type`) via `nvt_ts_irq_type[]`. This firmware reports `1` -> `IRQF_TRIGGER_FALLING`, but the line is described as rising in the DT (`<&pio 1 1>`, kernel logs `irq: type mismatch`) and the downstream driver hard-codes `IRQF_TRIGGER_RISING`.
   On the falling edge the interrupt fires ~91 times per second while idle and never with touch data. Using the trigger type from DT (fallback: firmware) gives 0 IRQs idle and ~38 IRQs/s while touching, same as downstream.
2. **Parameters read selects no page.** `nvt_ts_probe()` reads the parameters with `regmap_raw_read(regmap, NVT_TS_PARAMETERS_START, ...)`, i.e. reg `0x78` = page 0, so the SPI layer sends no page select and the chip answers with its *touch event stream* (all `0xff` when idle, a touch frame when a finger is on the screen): `Unsupported touchscreen parameters: ff ff ff ...` (with a finger on the screen: `0a 05 8c a0 0a 0a ff ...`).
   Downstream reads them with the event buffer page selected (`EVENT_BUF_ADDR | 0x78`). The same read relative to `memory_map->event_buffer` returns the real block (`1b e4 12 24 04 38 09 24 0e 0a ...` = fw 0x1b, 1080x2340, 10 touches) and leaves the event buffer page selected, which `nvt_ts_irq()` (page-less read at reg 0) relies on.

3. **Probe races the power-up at boot.** `vtp` (always-on) is only configured ~0.28 s before the touch probe at boot; the first params read then fails with `status 0x00` (`read failed, status: 0x00`, probe error -5), while the same probe works a minute later. The patch retries "upload firmware + read params" up to 5 times, resetting the controller in between (on the test boot no retry was needed at all, so this is only a safety net).

Observation worth knowing: on this chip a read that is preceded by a page select returns the parameter block regardless of the command offset, a read without page select returns the touch stream. If the touch read is ever done with a page select, the chip keeps returning the parameter block and the touch looks dead (this is what misled me for a long time: my debug build read the touch events relative to the event buffer).

## Patch (against 6.16.4 `drivers/input/touchscreen/novatek-nvt-ts-core.c`)

```diff
diff --git a/drivers/input/touchscreen/novatek-nvt-ts-core.c b/drivers/input/touchscreen/novatek-nvt-ts-core.c
--- a/drivers/input/touchscreen/novatek-nvt-ts-core.c
+++ b/drivers/input/touchscreen/novatek-nvt-ts-core.c
@@ -15,6 +15,8 @@
 
 #include "novatek-nvt-ts.h"
 
+#define NVT_TS_PROBE_RETRIES	5
+
 static const int nvt_ts_irq_type[4] = {
 	IRQF_TRIGGER_RISING,
 	IRQF_TRIGGER_FALLING,
@@ -215,7 +217,8 @@
 		 const struct input_id *id,
 		 nvt_ts_upload_firmware_impl upload_firmware)
 {
-	int error, width, height, irq_type;
+	int error, width, height, irq_type, retry;
+	unsigned long irq_flags;
 	struct nvt_ts_data *data;
 	const struct nvt_ts_chip_data *chip;
 	struct input_dev *input;
@@ -265,17 +268,34 @@
 		return dev_err_probe(dev, error, "failed to request reset GPIO\n");
 	}
 
-	error = nvt_ts_upload_firmware(data);
-	if (error) {
+	/*
+	 * The controller may not be ready yet when its supply was only just
+	 * enabled (early boot), so retry the upload + params read a few times,
+	 * resetting the controller in between.
+	 */
+	for (retry = 0; retry < NVT_TS_PROBE_RETRIES; retry++) {
+		error = nvt_ts_upload_firmware(data);
+		if (!error) {
+			/* Wait for controller to come out of reset before params read */
+			msleep(100);
+			/*
+			 * Read the parameters relative to the event buffer, like downstream
+			 * does: this selects the event buffer page, which the touch event
+			 * reads in nvt_ts_irq() rely on (they use no page select of their own).
+			 */
+			error = regmap_raw_read(data->regmap,
+						data->memory_map->event_buffer | NVT_TS_PARAMETERS_START,
+						data->buf, NVT_TS_PARAMS_SIZE);
+			if (!error)
+				break;
+		}
+
+		dev_dbg(dev, "params not available (%d), retry %d\n", error, retry);
 		gpiod_set_value_cansleep(data->reset_gpio, 1);
-		regulator_bulk_disable(ARRAY_SIZE(data->regulators), data->regulators);
-		return dev_err_probe(dev, error, "failed to upload firmware\n");
+		msleep(100);
+		gpiod_set_value_cansleep(data->reset_gpio, 0);
+		msleep(10);
 	}
-
-	/* Wait for controller to come out of reset before params read */
-	msleep(100);
-	error = regmap_raw_read(data->regmap, NVT_TS_PARAMETERS_START,
-				data->buf, NVT_TS_PARAMS_SIZE);
 	gpiod_set_value_cansleep(data->reset_gpio, 1); /* Put back in reset */
 	regulator_bulk_disable(ARRAY_SIZE(data->regulators), data->regulators);
 	if (error)
@@ -321,9 +341,18 @@
 	data->input = input;
 	input_set_drvdata(input, data);
 
+	/*
+	 * The IRQ trigger type from the firmware parameters is not reliable,
+	 * e.g. the Redmi Note 8 Pro reports "falling" but the interrupt line is
+	 * wired (and described) as rising edge, which is also what the
+	 * downstream driver uses. Prefer the trigger type from firmware/DT.
+	 */
+	irq_flags = irq_get_trigger_type(irq);
+	if (!irq_flags)
+		irq_flags = nvt_ts_irq_type[irq_type];
+
 	error = devm_request_threaded_irq(dev, irq, NULL, nvt_ts_irq,
-					  IRQF_ONESHOT | IRQF_NO_AUTOEN |
-						nvt_ts_irq_type[irq_type],
+					  IRQF_ONESHOT | IRQF_NO_AUTOEN | irq_flags,
 					  dev_name(dev), data);
 	if (error)
 		return dev_err_probe(dev, error, "failed to request irq\n");
```

## Caveats

- Only tested on this device/panel (CSOT, `fw01`). The params read change follows downstream (`EVENT_BUF_ADDR | 0x78`) and should be harmless for chips whose event buffer is already the selected page, but I could not test other NT36xxx variants.
- The DT-first trigger type changes behaviour for devices where DT and firmware disagree; I think DT should win, but happy to restrict it if you prefer.
- Orientation/scaling checked with a finger held in each corner of the 1080x2340 panel: TL ~(96,64), TR ~(980,118), BL ~(139,2257), BR ~(974,2238), centre ~(508,1212). x grows to the right, y grows downwards, no swap/invert needed.
- Unloading the modules after a successful probe prints a `WARNING` in `regulator_bulk_free` (regulators still enabled while the input device is open); unrelated to this patch, not fixed here.
- Also needed on this board (outside the driver): the touch supply (`vtp`, MT6360 LDO2, 3.0 V) must be on; fixed in the DTB, not part of this patch.

Evidence logs are available (kernel log of the failing/working runs, downstream `/proc/NVTSPI` captures).
