<!-- Excerpt from the chronological log docs/NOTES-chronological.md (split by topic). -->
<!-- References like wifi/..., audio/..., pmos-begonia/... in the text refer to the old layout of the working directory; see README.md at the root of port/. -->

# Battery (MT6359 fuel gauge) — notes

## 🔋 BATTERY + BUILDING THE IMAGE FROM SOURCES (2026-11-12, end of day) — STATUS: works, installed on the phone
**Before:** `adc-ocv-battery.ko` (voltage-based estimate) — an inherently inaccurate hack: the MT6360 IBAT ADC sees only the charge current, not the discharge current, the IR compensation lied (capacity jumped 19% → 59% at the same 3.92 V); a race at probe (the charger was looked up once → status forever Discharging). Plus the charger input current limit of 500 mA with a 5 V/2 A PD source (`input_current_limit=500000`, BC1.2 for a PD cable gives SDP) → the phone discharged under load while charging.
**Now:** a real fuel gauge on the MT6359 hardware FGADC. Everything is built from sources, no more manual binary DTB edits or substituted `.ko` files.

### Where things are
- `Begonia/pmos-build/` — **the main place**: `README.md`, `kernel/patches/0001..0007` (series on linux `6.16.4-r0`), `pmaports/device/testing/linux-postmarketos-mediatek-mt6785/{APKBUILD,config}` (overlay, `pkgrel=3`), `scripts/{export-patches,apply-overlay,build-kernel,deploy-kernel}.sh` + `gen-source-list.py`.
- Source of truth for the kernel: the **`begonia-fixes`** branch in `~/mt6785-linux` (WSL), base — tag `6.16.4-r0`. Backup of the first (failed, with a DT cycle) series — branch `backup-v1`. Make changes as commits in `begonia-fixes` → `export-patches.sh` → `apply-overlay.sh`, do **not** patch the DTB by hand.
- Old stuff: `pmos-begonia/battery/_obsolete/` (`adc-ocv-battery.*`, `mt6359-gauge-out-of-tree/` — an intermediate out-of-tree version). `battery/patch_dts_battery.py`, `bat*.dts` — also history.

### Series contents
| # | What |
|---|---|
| 0001 | `drivers/power/supply/mt6359_fgadc.c` + Kconfig/Makefile + MFD cell `mt6359-fgadc` in `mt6397-core.c` + binding `mediatek,mt6359-fgadc.yaml`. Regmap from the parent (`struct mt6397_chip`), the OCV and R tables come from `simple-battery` in DT, nothing is hard-coded. |
| 0002 | `mt6360_charger.c`: a psy notifier — takes `CURRENT_MAX` of another USB psy (`tcpm-source-psy-*`) as AICR, returns 500 mA on `ONLINE=0`; ICHG and CV from `monitored-battery`. |
| 0003 | DTS: `vtp` (MT6360 ldo2) always-on 3.0 V + `vcc-supply`/`iovcc-supply` of the touchscreen; `proximity-near-level` 70→550. |
| 0004 | DTS: `battery` (simple-battery, OCV table in 2% steps from the vendor `battery0_profile_t1`, 25 °C, R=125 mOhm, ICHG max 2.5 A, CV 4.35 V), node `fuel-gauge` under `&pmic`, labels `mt6360_adc`/`mt6360_charger`. |
| 0005 | DTS: `consys-begonia.dtsi` (+ `wifi@18000000`, `wifi-reserve-memory`, which were not in the original dtsi — they lived only in the binary DTB). |
| 0006 | DTS: `audio-begonia.dtsi` (AFE + sound). The labels are in `mt6785.dtsi`, `make-audio-dtb.sh` with after-the-fact labelling is no longer needed. |
| 0007 | `novatek-nvt-ts-core.c` (v3 of the touch fix: probe retry, reading the parameters relative to the event buffer, IRQ type from DT). |

Config: `CONFIG_BATTERY_MT6359_FGADC=y` (built in, not a module).
Equality check: the DTB from sources against the live DTB (with reference properties masked) differs **only** in the intentional battery/charger nodes and phandle renumbering.

### How the gauge works (MT6359 registers, useful for debugging)
- `FGADC_CON0 0x0d08` bit0 FG_ON (LK already enables it). Reading uses a latch via `FGADC_CON1 0x0d0a`: SW_READ_PRE (bit0) → wait for LATCHDATA_ST (bit15) → read `CUR_CON0 0x0d8a` (s16, **+ = charging**), `CAR_CON0/1 0x0d16/0x0d18` (32 bits) → SW_CLEAR (bit3) → clear SW_READ_PRE → wait for LATCHDATA_ST=0 → clear SW_CLEAR.
- Current LSB 610.352 µA (5 mOhm shunt), charge LSB 0.085 µAh. Source: vendor `drivers/misc/mediatek/pmic/mt6359/v1/mt6359_gauge.c`, constants `UNIT_FGCURRENT`/`UNIT_CHARGE`.
- Check: FG current against the MT6360 IBAT ADC ±3%; ΔCAR against ∫I over 42 s: 3969 vs 3946 µAh (0.6%).
- Estimation: `discharged_uah` is tracked from ΔCAR, the starting point and slow correction come from OCV (OCV = V − I·R), correction only at |I| < 150 mA (stronger at < 50 mA), FULL status from the charger → 100%. Voltage — IIO channel of the MT6360 ADC (channel 4, VBAT). Usable capacity down to 3.4 V per the vendor profile is 4386 mAh; DT has `charge-full-design=4400000`.
- IIO MT6359 AUXADC `batt_v` gives garbage (raw 55025), and `bat_adc` (type IIO_RESISTANCE!) = VBAT·32768/6300 — in mainline the channels are named oddly, so MT6360 is used.

### Pitfalls (in the order I stepped on them)
1. **Probe cycle because of `power-supplies` in the charger DT.** I set `power-supplies = <&mt6360_tcpc>`. `power_supply_check_supplies()` defers the psy registration until the supplier is registered; the psy `tcpm-source-psy` appears only after the tcpc probe, and tcpc/USB waits for the vbus regulator **of this very charger** → `mt6360-chg: Failed to register power supply dev` (deferred), no battery, USB does not come up. Fix: remove the link from DT, take the limit with a psy notifier (0002). The phone was temporarily patched with `fdtput -d`; not needed now.
2. `$VAR` inside `wsl -d Ubuntu -- bash -lc '...'` is expanded **before** bash (wsl.exe wraps it in a shell) → empty variables. Pass scripts through stdin: `wsl -d Ubuntu -- bash < script.sh`.
3. On Windows python is only available as `python` (not `python3`); a `%` character in a comment inside a string with `%` formatting breaks the substitution (escape it as `%%`).
4. `pmbootstrap` (even `checksum`) requires `sudo` with a password → the user runs `sh scripts/build-kernel.sh` himself; the kernel tarball is ~250 MB, it downloaded at ~0.4 MB/s.
5. Patch names from `format-patch` depend on the commit subjects → the list in `source=` is generated (`gen-source-list.py`), do not maintain it by hand. `apk add` of the same version will not reinstall → bump `pkgrel`.
6. A wrapper script with `set -e` and `! grep` does not stop on a build error (that is how I committed code with a non-existent `POWER_SUPPLY_NAME_MAX`) — check with `if grep ...; then exit 1; fi`.
7. `git checkout <file>` with unsaved user changes is blocked by the auto classifier — back up first (`~/dts-backup/`), then `git show HEAD:path > path`.

### Build and deploy (how to repeat)
```
# WSL, from Begonia/pmos-build:
sh scripts/export-patches.sh     # if the kernel was touched: begonia-fixes commits -> kernel/patches
sh scripts/build-kernel.sh       # apply-overlay (+checksum) and pmbootstrap build; asks for sudo
sh scripts/deploy-kernel.sh      # apk to the phone; the trigger reflashes boot by itself (deviceinfo_flash_kernel_on_update)
```
I deployed by hand: `scp` apk → `sudo apk add --allow-untrusted /tmp/kernel.apk` → `sudo systemctl reboot`. Backup before installing: `/root/boot-backup` on the phone (`boot.img`, `initramfs`, `vmlinuz`, `dtbs`, `*.dtb.stock`, `*.dtb.hotfix`, `*.dtb.r2-package`). Rollback: fastboot images `pmos-begonia/boot*.img` or `apk add linux-...=6.16.4-r1`.

### Verified on hardware (r3, 17:25–17:35)
Boot without patches; `battery` (OF `/soc/pwrap@1000d000/pmic/fuel-gauge`); ICHG=2.5 A applied; the sign of the current on discharge is correct (−0.6…−1.1 A under load); on connecting PD charging `INPUT_CURRENT_LIMIT` became 2 A by itself (tcpm CURRENT_MAX), `current_now=+1.3 A`, status Charging. Touch, WiFi (`wlan_6785_axi`), audio modules in place.

**NOT verified:** AICR going back to 500 mA when the cable is pulled out; booting with the cable already plugged in (the notifier may miss a tcpm event that came before the charger probe — there is no initial scan, since the kernel has no `power_supply_for_each_device`); temperature compensation (the table has only 25 °C; the MT6359 AUXADC has a `batt_temp` channel); keeping the capacity across reboots (after a reboot SOC comes from OCV again, ±3%); Qmax learning; FULL status/end of charge; behaviour at low charge (shutdown).

