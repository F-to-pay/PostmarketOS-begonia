# Redmi Note 8 Pro (begonia): a real battery gauge (MT6359 FGADC) and USB-C input current limit on mainline 6.16.4, plus a probe deadlock worth knowing

DRAFT, not sent. Tested on mainline 6.16.4 (`linux-postmarketos-mediatek-mt6785-6.16.4-r3`, built from tag `6.16.4-r0` + the patches below), Redmi Note 8 Pro (MT6785, PMICs MT6359 + MT6360), postmarketOS edge. Vendor reference: MiCode `Xiaomi_Kernel_OpenSource`, branch `begonia-r-oss` (`drivers/misc/mediatek/pmic/mt6359/v1/mt6359_gauge.c`).

## Result

The phone had a battery power supply only through an out-of-tree "voltage + OCV" module, which was inaccurate by design, and it charged at 500 mA from a 5 V / 2 A USB-C source. Now, with seven patches on top of `6.16.4-r0` (everything in-tree, no out-of-tree module, no hand-patched DTB):

- **`battery` power supply from the PMIC fuel gauge.** Current (signed, both directions) and charge flow come from the MT6359 FGADC (hardware coulomb counter + current ADC over the 5 mOhm shunt). The capacity starts from the open circuit voltage and is then driven by the coulomb counter, with a slow OCV re-synchronisation only while the battery is nearly idle. Verified on the device: FGADC current vs. the MT6360 charger IBAT ADC within about 3 %; coulomb counter delta vs. the integral of the current: 3969 vs. 3946 uAh over 42 s (0.6 %); the sign is right while discharging (-0.6 ... -1.1 A under load, cable unplugged) and while charging (+1.3 A).
- **USB-C input current limit follows the source.** Plugging a PD source (5 V / 2 A) now sets `INPUT_CURRENT_LIMIT` to 2 A by itself (it used to stay at 500 mA, so the phone kept discharging under load while plugged in); `CONSTANT_CHARGE_CURRENT` is taken from the battery node (2.5 A).
- Everything is described in DT (`simple-battery` with an OCV table, a `fuel-gauge` child of the PMIC).

**Not done / not tested:** return to 500 mA when the cable is removed (the code does it, I did not test it), boot with the cable already plugged in (see "Known gap"), temperature compensation (only the 25 degC curve is used; `batt_temp` of the MT6359 AUXADC exists), keeping the state of charge across reboots (after a reboot it is derived from the OCV again, about +-3 %), Qmax learning, end-of-charge/FULL handling, low-battery shutdown behaviour.

This is *not* upstreamable as is, mostly because of the DT table (vendor battery profile) and because the capacity algorithm is a simple one; the two driver patches are meant as a base for discussion. I'm writing it up mainly because of the **probe deadlock** (trap 1), which will hit anyone who links the MT6360 charger to the Type-C port controller through `power-supplies`.

## Patches (on top of `6.16.4-r0`)

| # | What |
|---|---|
| 1 | `power: supply: Add MediaTek MT6359 PMIC fuel gauge driver`. New `drivers/power/supply/mt6359_fgadc.c`, an MFD cell `mt6359-fgadc` in `mt6397-core.c` (`of_compatible = "mediatek,mt6359-fgadc"`), Kconfig, DT binding. The regmap comes from the parent (`struct mt6397_chip`); the OCV table, the internal resistance and the design capacity come from the `simple-battery` node via `monitored-battery`, nothing is hard coded. |
| 2 | `power: supply: mt6360_charger: Follow the USB source current and battery limits`. A power supply notifier picks up the maximum current of another `POWER_SUPPLY_TYPE_USB` supply (the TCPM source supply) and uses it as the input current limit while it is online (500 mA again when it goes offline); `constant-charge-current-max-microamp` and `constant-charge-voltage-max-microvolt` of the monitored battery are applied at probe. |
| 3-4 | `arm64: dts: ... begonia`: the touch supply (`vtp`, MT6360 ldo2, always-on 3.0 V) and the proximity near level; the battery node (OCV table resampled from the vendor `battery0_profile_t1`), the `fuel-gauge` node. |
| 5-6 | `arm64: dts: ... begonia`: CONNSYS/WiFi and the audio front end (these only describe hardware for the out-of-tree drivers from my other reports). |
| 7 | `Input: novatek-nvt-ts - Make probing reliable ...` (retry, event-buffer relative parameter read, IRQ trigger type from DT), see the touchscreen reports. |

`CONFIG_BATTERY_MT6359_FGADC=y`.

## What it took (the traps, in the order I hit them)

### 1. `power-supplies` from the charger to the Type-C controller deadlocks the probe

The obvious way to let the charger follow the USB-C source is `power-supplies = <&tcpc>;` on the charger node (it also makes the supply core call `external_power_changed()`). With that, the boot ends with no battery and no USB:

```
platform 11201000.usb: deferred probe pending: wait for supplier /soc/i2c@11016000/pmic@34/charger/usb-otg-vbus-regulator
platform mt6359-fgadc: deferred probe pending: supplier mt6360-chg.1.auto not ready
platform mt6360-chg.1.auto: deferred probe pending: mt6360-chg: Failed to register power supply dev
```

`power_supply_check_supplies()` returns the "supply not found yet" error (`-EPROBE_DEFER`) from `power_supply_register()` until every `power-supplies` phandle resolves to a registered supply. The TCPM source supply (`tcpm-source-psy-mt6360-tcpc.4.auto`) is registered by the TCPC probe, and on this board the TCPC/USB side waits for `usb-otg-vbus-regulator`, which is provided by **the same charger**. So the charger waits for the TCPM supply and the TCPM side waits for the charger. Solution: no DT link at all; patch 2 listens to power supply changes and reads `ONLINE`/`CURRENT_MAX` of the other USB supply in a work item (the notifier chain is atomic, the regmap write sleeps).

### 2. BC1.2 says SDP for a PD source

The charger driver sets the input current limit only when somebody writes `INPUT_CURRENT_LIMIT`; nothing does, and the BC1.2 result for a Type-C PD charger is "SDP" (`usb_type = Unknown [SDP] DCP CDP`), so the limit stays at the hardware default of 500 mA although the TCPM supply reports `CURRENT_MAX = 2000000`. With the screen on and WiFi running, the system draws more than 500 mA and the battery discharges while "Charging". Writing 2000000 to `input_current_limit` and `constant_charge_current` through sysfs gives about +1.3 A into the battery, that is what patch 2 automates.

### 3. A charger-ADC based gauge cannot work

The first (out-of-tree) attempt took the voltage and the current from the MT6360 ADC and estimated the capacity from the OCV. Its IBAT channel only measures the charge current, so while discharging there is no current at all and the IR compensation is wrong; the "capacity" jumped from 19 % to 59 % at an unchanged battery voltage of about 3.92 V. The only place that measures the discharge current is the MT6359 FGADC, which mainline has no driver for.

### 4. FGADC: latched reads, signs and LSBs

The registers are only updated through a latch, otherwise you read a stale snapshot:

1. `FGADC_CON1` (`0x0d0a`) `SW_READ_PRE` (bit 0) = 1, wait for `LATCHDATA_ST` (bit 15) = 1;
2. read `FGADC_CUR_CON0` (`0x0d8a`, signed 16 bit, **positive = charging**) and `FGADC_CAR_CON0/1` (`0x0d16`/`0x0d18`, 32 bit coulomb counter);
3. `SW_CLEAR` (bit 3) = 1, `SW_READ_PRE` = 0, wait for `LATCHDATA_ST` = 0, `SW_CLEAR` = 0.

Current LSB 610.352 uA, charge LSB 0.085 uAh (both for the 5 mOhm shunt; `shunt-resistor-micro-ohms` scales them). `FGADC_CON0` bit 0 (`FG_ON`) was already set by the bootloader. A debugfs dump of the regmap only shows the stale latched values, which fooled me at first.

### 5. Vendor profile is not 100 points of "percent"

The vendor table is `{discharged charge in 0.1 mAh, OCV in 0.1 mV, resistance in 0.1 mOhm}` per row, 100 rows per temperature. I converted the 25 degC table into `ocv-capacity-table-0` (OCV in uV, capacity in %, 2 % steps: the power supply core limits the table to 100 entries) with 100 % = the first row and 0 % = the point where the open circuit voltage crosses 3.4 V (4386 mAh used). The design capacity in the node is 4400000 uAh (typical rating 4500 mAh, minimum 4420 mAh).

### 6. The MT6359 AUXADC channels are named oddly

For the battery voltage I used the MT6360 ADC (channel 4, VBAT). On the MT6359 AUXADC `batt_v` returned nonsense (raw 55025) while `bat_adc`, registered with type `IIO_RESISTANCE`, reads VBAT as `raw * 6300 / 32768` mV and matches the MT6360 value (4277 mV vs 4277 mV in one test). I did not dig further.

## Known gap

The notifier only reacts to changes. If the PD contract is negotiated before the charger probes (cable plugged in at boot), the event is missed and the limit stays at 500 mA until the next change of the TCPM supply. There is no `power_supply_for_each_device()` in this tree to scan at probe. A cleaner design would be for TCPM to tell the charger directly (e.g. a charger-supply phandle on the connector node, resolved lazily without `-EPROBE_DEFER`), I would like your opinion before I try that.

## Reproduce

Kernel tag `6.16.4-r0` from `mt6785-mainline/linux`, the seven patches on top, `CONFIG_BATTERY_MT6359_FGADC=y`; built as the `linux-postmarketos-mediatek-mt6785` package with the patches added to `source=` (`pkgrel=3`). Check on the device:

```
grep -E 'STATUS|CAPACITY=|VOLTAGE|CURRENT_NOW|CHARGE_NOW|CHARGE_FULL=' /sys/class/power_supply/battery/uevent
grep -E 'INPUT_CURRENT_LIMIT|CONSTANT_CHARGE_CURRENT=' /sys/class/power_supply/mt6360-chg.1.auto/uevent
grep -E 'ONLINE|CURRENT_MAX' /sys/class/power_supply/tcpm-source-psy-*/uevent
```

Plug a PD charger; `INPUT_CURRENT_LIMIT` goes from 500000 to 2000000 and `current_now` becomes positive. Patches, the package overlay and the scripts to build and deploy them are available.
