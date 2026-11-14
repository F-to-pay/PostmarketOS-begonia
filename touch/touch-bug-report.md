# Redmi Note 8 Pro (begonia): NT36672A touchscreen does not work on postmarketOS (mt6785-mainline 6.16.4-r0)

Device: Xiaomi Redmi Note 8 Pro (`xiaomi-begonia`, MT6785, UFS), global (`androidboot.hwc=GL`), bootloader unlocked.
Kernel: `linux-postmarketos-mediatek-mt6785` 6.16.4 (tag `6.16.4-r0` of mt6785-mainline/linux), clang, SHADOW_CALL_STACK.
Driver: `novatek-nvt-ts-core` + `novatek-nvt-ts-spi` (compatible `novatek,nt36672a-ts`, `spi1.0`), firmware `novatek/nt36672a_begonia_csot.bin`.

## Symptom

No input device is created. Stock modules fail at probe:

```
novatek-nvt-ts-spi spi1.0: supply vcc not found, using dummy regulator
novatek-nvt-ts-spi spi1.0: supply iovcc not found, using dummy regulator
novatek-nvt-ts-spi spi1.0: Unsupported touchscreen parameters: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
novatek-nvt-ts-spi spi1.0: probe with driver novatek-nvt-ts-spi failed with error -5
```

## What was checked and ruled out

| Hypothesis | Result |
|---|---|
| SPI pinmux wrong | `GPIO35..38` are muxed `func5` to `11019000.spi`, as in the DT |
| Reset polarity (`reset-gpios` ACTIVE_HIGH vs RST_N active low) | Module parameter `rst_invert` 0/1 plus explicit reset pulse: identical data both ways |
| Wrong firmware (CSOT vs Tianma) | Stock Xiaomi `begonia-r-oss` `firmware/` has `novatek_nt36672a_g7_fw01.bin` (CSOT, `tp-vendor 0x53 / maker 0x42`) and `fw02` (Tianma, `0x46 / 0x36`). pmOS blob is byte-identical to `fw01` (sha256 `0b57dd62...`). Swapping in `fw02` (sha256 `3d7d13b4...`): identical chip responses |
| Register address endianness | `reg_buf=fe f0 03 00 -> reg=0x0003f0fe, page=0x7e1, cmd=0x7e` decoded correctly |
| SPI transaction shape | Driver's 3 separate transfers vs. one full-duplex transfer (Xiaomi style): identical results |
| Time after firmware boot | Reads at +0, 5, 20, 50, 100, 250, 500 ms: identical |
| Read length | 1..8 byte reads: same bytes |
| Dummy-byte check | Accepting any dummy byte (as Xiaomi does) changes nothing |

## Key observations

1. The very first read after `boot_ready` (poll of `0x21c60`, 1 byte) returns the correct `0xa0` (firmware booted). Right before, `0x3f30e` returns `0xc0`.
2. From the next read on, **every** address with dummy byte `0xff` returns the same block regardless of address/cmd:

   ```
   0x21c00 / 0x21c08 / 0x21c50 / 0x21c60 / 0x21c78 -> 1b e4 12 24 04 38 09 24 0e 0a 00 01 01 01 00 12 00 ff 20 59 ff a1 08 a7
   0x21c80                                          -> 20 59 ff a1 08 a7 e2 80 ff 7f 01 0f ff ff ff ff
   0x3f300                                          -> c0 ff a0 04 a7 34 cd ff 1b e4 12 24 04 38 09 24
   ```
   Other pages (`0x21b80 0x21d00 0x3f100 0x20000 0x00080 0x10000`) return dummy `0x00` and all zeros.
3. The block decodes as a valid fw-info record: width `04 38` = 1080, height `09 24` = 2340, max touches `0x0a`, irq type 1, wake type 1, chip id 0. So the chip is alive and reports sane parameters, but the SPI read address is ignored.
4. Reading the params at the bare offset `0x78` (as `novatek-nvt-ts-core.c` does) returns dummy `0x00` / zeros -> probe fails with `-EIO`; reading at `event_buffer|0x78` (`0x21c78`) returns the block above. With a core patch that falls back to `event_buffer`-relative reads, `probe` succeeds and `spi1.0` registers as an input device, but the IRQ handler (about 100 Hz, level always asserted) gets the same constant block instead of touch frames.

## Relevant DT (from Xiaomi begonia-r-oss `novatek-nt36xxx-spi_g7.dtsi`)

```
novatek,reset-gpio  = <&pio 2 0x00>;
novatek,irq-gpio    = <&pio 1 0x2001>;
novatek,reset-tddi  = <&pio 44 0x00>;   /* display RESX is also controlled by the touch driver */
spi-max-frequency   = <5000000>;
novatek,swrst-n8-addr = <0x03F0FE>; novatek,spi-rd-fast-addr = <0x03F310>;
```

Open question: Xiaomi's downstream driver controls the display reset (`reset-tddi`, `pio 44`) from the touch driver and says the boot update must start "after display reset (RESX) sequence". The mainline-style driver relies on the panel-follower API instead; whether the TDDI needs a specific RESX timing relative to the touch download is not verified.

## Not tried

- Driving `pio 44` (display RESX) from the touch driver around the firmware download as downstream does.
- Reading with a logic analyzer on SPI MOSI/MISO/CS.
- Another Linux version (branch `7.1` of the fork only drops the `wake_type` check).

Everything above was tested with out-of-tree rebuilt modules (`vermagic 6.16.4 SMP preempt mod_unload aarch64`, struct module size identical), loaded with `insmod` on a freshly rebooted phone.

## Update (later the same session)

- The postmarketOS wiki feature table lists **Touchscreen = Works** for this device, so the stock driver is known to work on the author's unit. This report is therefore about one specific unit (Redmi Note 8 Pro Global, `hwversion 1.19.1`, `LCM_name=NONE` from the bootloader, so CSOT vs Tianma panel could not be determined).
- Firmware swap, done properly (only `nt36672a_begonia_csot.bin.zst` present in `/lib/firmware/novatek/`, containing Xiaomi `fw02`, sha256 of the .zst `5bd2ba07...`): chip responses were byte-identical to the CSOT `fw01` run (`1b e4 12 24 04 38 09 24 ...`). An earlier attempt with a plain `.bin` next to the `.zst` was not conclusive; the later one is.
- Comparison with Xiaomi's downstream `NT36672A_G7` driver: for `hw_crc` chips the download flow starts with `nvt_bootloader_reset()` only (eng_reset + TP_RESX toggling is the non-hw_crc path), same register map (`0x3F0FE`, `0x3F310`, event buffer `0x21C00`), same SPI shape (cmd byte, one dummy byte, data; downstream does not check the dummy byte). No functional difference found.
- Downstream decides the firmware by `lockdown` bytes: `0x53/0x42` -> CSOT `fw01`, `0x46/0x36` -> Tianma `fw02`.

## Update 3: panel variant resolved

- Backported the Tianma panel descriptor from mt6785-mainline 7.1 (`xiaomi,begonia-tianma-nt36672a`: `on_cmds_1` minus last 3 commands, `on_cmds_2` with `FF F0 / FB 01 / A2 00` added) into the 6.16.4 panel driver, built a boot image with the Tianma DTB, the new panel module and the Tianma touch firmware inside the initramfs (the initramfs carries its own copies of the panel module, both touch modules and the firmware).
- Result: **black screen** with the Tianma init sequence, normal picture with the CSOT one. So this unit has a CSOT panel; the touch problem is not a CSOT/Tianma mix-up.
- 6.16.4 ships only one begonia DT (`mt6785-xiaomi-begonia.dts`, CSOT); the fork's `7.1` branch has `begonia-csot.dts` and `begonia-tianma.dts`.
- Remaining differences vs. the working setup on the author's unit are unknown (hardware revision / touch IC batch?).

## Update 4: kernel 7.1.0 (fork branch `7.1`, tag `7.1-r0`)

Built the whole kernel (clang 21, config from the phone's 6.16.4 + `olddefconfig`, no BTF), the 7.1 `begonia-csot` DTB, all modules, and a new initramfs with 7.1.0 modules; booted it on the same unit. Display works (panel bound, `connected`), touch modules load, but the stock driver fails exactly as on 6.16.4:

```
novatek-nvt-ts-spi spi1.0: supply vcc not found, using dummy regulator
novatek-nvt-ts-spi spi1.0: supply iovcc not found, using dummy regulator
novatek-nvt-ts-spi spi1.0: Unsupported touchscreen parameters: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
novatek-nvt-ts-spi spi1.0: probe with driver novatek-nvt-ts-spi failed with error -5
```

So the problem is not fixed by the kernel update. Side observation: on 7.1.0 the `mt6779-keypad` input device (volume keys) is missing from `/proc/bus/input/devices` (only `mtk-pmic-keys` and `regulator-haptic`), probably a config difference (module not built/loaded), not investigated.
