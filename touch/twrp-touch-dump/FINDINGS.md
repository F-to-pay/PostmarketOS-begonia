# NT36672A touch on Redmi Note 8 Pro: what the working downstream kernel (TWRP) shows

Source: TWRP 3.6.0_11-2, kernel `4.14.247-Neutrino-Stock` (downstream, touch works), same unit as the pmOS bug report
(`LCM csot_fhd_nt36672a`, `androidboot.hwversion=1.19.1`, CSOT). All values read live over adb (read-only). Raw dumps: `collect1..7.txt`.

## Same as the pmOS DT (so the DT port is right)

| item | downstream (live DT) | pmOS bug report |
|---|---|---|
| touch SPI controller | `spi5@11019000`, `mediatek,mt6765-spi`, `pad-select=0` | `11019000.spi` |
| SPI pins | GPIO35..38 muxed `func5` (read from `pinctrl .../pins`) | same |
| irq | `<&pio 1 0x2001>` -> gpio-302 `NVT-int`, idle **high** | same |
| reset | `<&pio 2 0>` -> gpio-303 `NVT-tp-rst`, idle **out high** | same |
| `reset-tddi` | `<&pio 44 0>` is **not claimed by any driver** (`pin 44 MUX UNCLAIMED`) | listed as "not tried": it can be skipped |
| swrst / rd-fast addr | `0x3F0FE` / `0x3F310` | same |
| SPI max freq | 5 MHz (`ts_novatek`), 10 MHz on the xsfer node `spi-for-tp` | 5 MHz |
| firmware | `novatek_ts_fw.bin`, `fw_need_write_size=110592`, built in or in the ramdisk, **not** in /vendor/firmware | CSOT `fw01` blob |

## Differences worth testing on pmOS

1. **Touch supply: PMIC MT6360 `ldo2`, named `vtp`.** In the live DT: `/mt6360_ldo_dts/ldo2`, `regulator-name = "vtp"`,
   `regulator-always-on`, `regulator-default-on`, range 1.2..3.6 V, currently **3000 mV, enabled**
   (`regulator_summary`: `vtp 0 0 0 3000mV`). The other MT6360 LDOs (`VFP`, `VMC`, `VMCH`) are not always-on.
   pmOS says `supply vcc not found, using dummy regulator`. Nobody there asks for `vtp`, so the touch part may
   rely on an LDO nobody enables (it only works if the bootloader already left it on).
   Check on pmOS: `for r in /sys/class/regulator/regulator.*; do echo $r $(cat $r/name) $(cat $r/state) $(cat $r/microvolts); done`
   Test: in the begonia DT make `mt6360` `ldo2` `regulator-always-on; regulator-boot-on;` min=max=3000000 and point the
   touch `vcc-supply` at it.
2. The downstream driver downloads the firmware **on every resume, after the panel is unblanked**
   (`nvt_fb_notifier_callback event=9 blank=0` -> `nvt_ts_resume` -> `Update firmware success! <144 ms>`).
   The mainline panel-follower flow has to do the same after the panel is up (reset released, display powered).

## Things that look broken in pmOS but are NOT in downstream

- Lockdown bytes `ff ff ff ff ff ff ff ff` (`/proc/tp_lockdown_info`): same on the working downstream kernel.
- `nvt_check_fw_reset_state error, retry=51, buf=0A 1F 4F 07 0A` happens downstream too, right after the firmware download, and touch still works.
- The "constant block" `... 20 59 ff a1 08 a7 e2 80 ...` that pmOS reads is the normal config-info record:
  `/proc/nvt_xiaomi_config_info` -> `FW/config version 0x20`, `FAE ID 0x59`, `Reservation byte 0xFFA108A7E280`.
  So the chip answers correctly; what still needs an explanation is why event-frame reads return the same record.
  Downstream also has `fw_ver=27, x_num=18, y_num=36, PID=5920` (`/proc/nvt_fw_version`).

## Confirmed on the running pmOS (6.16.4), read-only via sysfs/DT (`../pmos-dump/probe1.txt`, `probe2.txt`)

- `regulator.53 | vtp | disabled | 1800000 | users=0` -> the touch supply is **off, set to 1.8 V, no consumer**
  (downstream: always-on at **3.0 V**). `vfp` (ldo1) likewise off, no users.
- DT `soc/i2c@11016000/pmic@34/regulator/ldo2`: `regulator-name = "vtp"`, 1.2..3.6 V, **no always-on / boot-on**, referenced by nobody.
- DT `soc/spi@11019000/touchscreen@0`: properties are only `compatible`, `firmware-name`, `interrupts-extended`, `panel`,
  `pinctrl-0`, `reset-gpios`, `spi-cs-setup-delay-ns` (20), `spi-max-frequency` (5 MHz). **No `vcc-supply` / `iovcc-supply`.**
- `vio18` (1.8 V) is enabled with 4 users, `backlight_vddpos/vddneg` 5.5 V are enabled, so only `vtp` (3.0 V) is missing.

### Suggested DT change (not applied, kernel rebuild needed)

```dts
/* mt6360 regulator node */
ldo2 {                       /* "vtp": touch/TDDI VCC, always-on 3.0 V downstream */
	regulator-min-microvolt = <3000000>;
	regulator-max-microvolt = <3000000>;
	regulator-always-on;
	regulator-boot-on;
};

touchscreen@0 {
	vcc-supply   = <&mt6360_ldo2>;   /* 3.0 V */
	iovcc-supply = <&mt6359_vio18>;  /* 1.8 V, label may differ in the .dtsi */
};
```

## Result of the `vtp` patch (`../vtp-patch/boot-vtp.img`, DTB only) on the real unit

- `vtp: Bringing 1800000uV into 3000000-3000000uV`, `vtp enabled 3000000 users=1`, `supply vcc not found` is gone.
- Probe still fails: `Unsupported touchscreen parameters: ff ff ... / error -5`, both at boot and on a manual unbind/bind
  ~3 minutes later (`journalctl -k`, `dmesg`). So **supply voltage and power-on timing are ruled out** as the cause.
- Left: the read protocol itself (the chip answers, but the address/page of the read does not take effect).
  Next step is a line-by-line diff of the mainline driver's SPI read/page-select code against Xiaomi's
  `NT36xxx_SPI` `nvt_set_page()` / `CTP_SPI_READ` (needs the two sources, or a logic analyzer on SPI MOSI/MISO/CS).

## SPI clock sweep (DTB-only patches on top of the `vtp` patch, same unit)

| `spi-max-frequency` | result |
|---|---|
| 1 MHz (`boot-vtp-1mhz.img`) | `read failed, status: 0x00` -> `-5` (worse: dummy byte is not 0xff) |
| 5 MHz (`boot-vtp.img`, stock value) | `Unsupported touchscreen parameters: ff ff ...` -> `-5` |
| 10 MHz (`boot-vtp-10mhz.img`, = `touch_spi@0` in the downstream DT) | identical to 5 MHz |

So clock speed is not the knob (above ~5 MHz the answer does not change).

## Source review (mainline `novatek-nvt-ts-spi.c` vs Xiaomi `NT36672A_G7/nt36xxx.c`)

- Page select bytes, command byte (`addr & 0x7f`), 1 dummy byte, SPI mode 0, 8 bit: identical.
- Page select as its own `spi_sync()` (CS released) is already implemented in `~/ntsrc` (`pgdelay`), and was reported "identical".
- `xiaomi-spi-tp` (`touch_xsfer`) is only an ownership/mutex layer for the SPI device, no hardware setup.
- Observed signature: reads at `0x21c00/08/50/60/78` all return the same fw-info record, i.e. the command byte seems ignored.

## Raw SPI frame dump (instrumented modules, `../nvtdump/`, logs in `../pmos-dump/`)

Out-of-tree build of `novatek-nvt-ts-{core,spi}` from `~/ntsrc` (6.16.4, vermagic matches), `NVTDUMP` log line per read/write.
Variant "A" = `eng_reset=0 phys=0 pgdelay=0` (the only combination that registers the input device; B/C/D failed with
`irq: type mismatch ... -22`, a reload artefact after a successful probe - needs a reboot between runs).

- Firmware upload is clean: `R 0x3f30e -> c0` (bootloader CRC state), `R 0x21c60 -> a0` (fw booted) - the same as downstream.
- Writes always work (fw upload, CRC setup, `boot_ready`), the firmware boots, `input3 "spi1.0"` registers, IRQ fires ~90 Hz (4115 in 45 s).
- **Right after the `a0` poll every read returns the same stream**: the next `R 0x21c60 -> 1b`, `R 0x21c78 (len 15) -> 1b e4 12 24 04 38 ...`,
  and all **2971** event reads `R 0x21c00 len 60` over 45 s of touching the screen are byte-identical
  (`1b e4 12 24 04 38 09 24 0e 0a 00 01 01 01 00 12 00 ff 20 59 ff a1 08 a7 ...`). dummy/status byte is always `0xff`.
  Reads of page 0 without a page-select message return status `0x00` (no answer).
- Re-writing `0x3f310 = 0` (SPI_RD_FAST off) 10 ms after the firmware boots (`postfast=10`): write ok, **no change**
  (1469 identical event frames again). Downstream only ever disables fast read in `nvt_bootloader_reset()`, same as mainline.

Page x offset matrix (`probematrix=1`, right after the `0xa0` poll): page `0x438` returns the fw-info record (downstream `EVENT_MAP_FWINFO` = `+0x78`)
for cmd `0x50/0x08/0x00/0x60/0x78`; page `0x439` returns `20 59 ff a1 08 a7 e2 80 ...` (downstream `nvt_xiaomi_config_info`, read at `0x21c9c` = page `0x439`
cmd `0x1c`) for cmd `0x00/0x08`; page `0x7e6` returns `c0 ff a0 04 a7 34 cd ff ...`; pages `0x43a/0x437/0x400/0x001/0x200` do not answer (status `0x00`).
=> page select works, in-page offset (cmd byte) is ignored, each page answers with a fixed info record. It is NOT "stuck at 0x78" (page `0x439` is `+0x1c`).

RX poison (`poison=1`: status and data pre-filled with `0xaa`): never visible in the log (failed reads return `0x00`) -> the MTK controller always writes RX.
Long reads (128 B from `0x3f300` / `0x21c00`, 64 B from `0x21c80`): one linear image `[8 B][fw-info 18 B][config-info 12 B][0xff ...][0xae][0x00 ...]`, entry point set by
the page. The image contains `0xae`, the value the driver wrote to `0x21c50` (HOST_CMD). It is NOT a linear memory dump (fw-info `0x21c78` and config-info `0x21c9c` are
36 B apart in memory but 18 B apart in the image; `0x21c50` is before fw-info but `ae` comes after config-info). Likely the firmware answers from its own compact
per-page response buffer and ignores the offset. Marker-write test (write `0x5a` elsewhere in the event buffer, see if it shows up) pending.

Marker writes (`0x5a` -> `0x21c40`, `0xa5` -> `0x21c10`, `err=0`): not visible anywhere (direct reads `-> 1b`, long reads unchanged, `ae` still there) -> not plain memory.
Chip-id read after reset (`chippre=1`, `R 0x1f64e` 7 B, like downstream's `nvt_ts_check_chip_ver_trim()`): returns `0a 00 00 72 66 03 ff` (correct), later behaviour unchanged;
but the head of the image now starts with the chip id (`0a 00 00 72 66 03 ff c0 ff a0 04 a7 34 cd ff 1b ...`) instead of `c0 ff a0 ...` -> it depends on the first read after reset.

Display power cycle (DSI connector forced off/on, panel-follower re-uploads firmware after the panel's prepare/reset): identical stuck stream, 2739 identical event reads.
Offline: `04 a7 34 cd` is not in the firmware blob (any byte order) and not a partition CRC; nobody ever read `0x21c9c` before config-info shows up in the image.
Read-back before `boot_ready` (`prebr`) + downstream-like poll (`dspoll`, 6 B at `0x21c60`): pages `0x7e1/0x7e2` (SWRST, CRC banks, boot_ready) never answer reads (write-only);
`0x21c60/0x21c50` already read `1b` (stuck) before `boot_ready`; the FIRST poll after `boot_ready` returns the correct `a0 04 a7 34 cd ff`, every later poll is `1b e4 12 24 04 38`.
Same with `pgdelay=200` (page select as its own `spi_sync` + 200 us). Caveat: in the `dspoll` runs the timestamps show only ~1 ms between the `boot_ready` write and poll 0 although the code does
`msleep(5)` first; unexplained, do not rely on that gap (the 311-1223 us gaps measured with the stock poll are fine).

## REFERENCE MEASUREMENTS ON THE WORKING DOWNSTREAM CHIP (`../twrp-trace/nvtspi.c` over `/proc/NVTSPI`, `twrp_reference_matrix.txt`, `twrp_long_reads.txt`, `touchcap.txt`) - READ THIS FIRST

(Earlier statements in this file that treat the "stuck stream" as a defect are superseded by this section.)
- Bug in my first helper: bit 7 of the request byte is **1 = READ** (`NVTWRITE = 0`, `NVTREAD = 1`), I had it inverted, so my first "reads" were actually writes of zeros to
  `0x1f64e.., 0x21c78.., 0x21c60, 0x21c00.., 0x21c9c, 0x3f30e, 0x21c50` on the running chip (RAM only; the firmware is reloaded on the next screen resume, which I did before measuring).
- The `/proc/NVTSPI` interface is `read()`-based in this kernel (`nvt_flash_read` exists, there is no `nvt_flash_write` in kallsyms).
- Same page x offset matrix as on pmOS gives IDENTICAL answers on the working chip (fw-info record for any offset of the event page, `20 59 ...` on `0x21c80`, `c0 ff a0 ...` on `0x3f300`,
  chip id `0a 00 00 72 66 03 ff c0 ff a0 ...` on `0x1f64e`, silent pages -> zeros, `0xae` at stream offsets 91 / 99). The "stuck stream" is the normal reply when no event is pending.
- With touching, the same event read returns real frames (`0a 1e 16 22 0a 0a ff ...`); IRQ count 4012 -> 5172 in 30 s while touching, **0 IRQs per 5 s at idle**.
- pmOS: ~90 Hz IRQ at idle and never a touch frame. => the open problem is NOT the read protocol; it is that the chip on pmOS does not detect/report touches (sensing/mode/TDDI-sync).

### Panel (DSI) init: downstream vs mainline (from the Neutrino R-OSS Stock binary kernel in `begonia-twrp-boot-source`, `../neutrino/`)

The three downstream LCM drivers are in the kernel (`csot_fhd_nt36672a_dsi_vdo_lcm_drv`, `tianma_fhd_nt36672a_...`, `ebbg_fhd_ft8719_...`); the CSOT init table found in `.rodata`
(72-byte records `{u32 cmd, u8 count, u8 para[64]}`) is `ff 25 | fb 01 | 05=04 | 13=04 | ff 24 | fb 01 | c3=00 | c4=20 | c2=8e | ff 10 | 35=00 | 29 | 11`, which is exactly mainline's
`csot_fhd_video_on_cmds_1` (plus its `51/53/55` backlight writes). The suspend tables (`ff 24 | fb 01 | c3=00 | ff 10 | 28 | delay 20 | 10 | delay 60`) match `csot_fhd_video_off_cmds`.
=> the DSI command lists are the same; a different panel init is NOT what separates working from non-working touch (timing/reset sequence not compared).

### Display swap (two display units, A = original, B = a spare of the same model)

- A: TWRP touch works (fw_ver 27, lockdown `ff x8`), pmOS touch does not. B: TWRP touch works too (user report; downstream even logs `FW info is broken! fw_ver=0x12 ~0x24, 0xFF/0xFF, 0x09/0x24`,
  `PID=FFFF`, lockdown `00 x8`, 0 IRQ at idle), pmOS touch does not. => **the display hardware is not the difference; it is something in pmOS.**
- On B the SPI replies are shifted by a few bytes (pmOS: `R 0x3f30e -> status 0x04 data a7` instead of `ff c0`, i.e. the same stream, 3 bytes late; downstream: shifted fw-info), so the instrumented driver
  cannot even finish probe on B. Display units differ in SPI reply latency.
- Firmware: the blob embedded in the TWRP kernel (`Image` offset `0x168d990`) is byte-identical to pmOS's `nt36672a_begonia_csot.bin` (sha256 `0b57dd62...`); the second embedded one (`3d7d13b4...`) is fw02 (Tianma).
- Regulators (sysfs `state`): only `vtp` differs for the touch (the others are modem/audio rails: `vmodem`, `vsram_md`, `vrf18`, `va09`, `vaud18`, `vbif28`; `VFP` is the fingerprint rail with 1 user).
- **Interrupt edge**: downstream requests the IRQ with `int_trigger_type = 1` (= `IRQF_TRIGGER_RISING`, hard-coded `INT_TRIGGER_TYPE`); the mainline core maps the firmware param (`irq_type = 1`) through
  `nvt_ts_irq_type[]` to `IRQF_TRIGGER_FALLING` (and DT says rising, hence the `irq: type mismatch` message). Test with the `irqmode` parameter of the instrumented core (display A, `pmos-dump/irqmode1_A.txt`):
  **`irqmode=1` (RISING) gives idle = 0 IRQs / 10 s (was ~900 on falling), and IRQs only while touching (~80 Hz burst, 2146 total, stops by itself after the touch).** The IRQ now behaves exactly like downstream.
  BUT: all 1471 logged frames at 0x21c00 are still the "stuck stream" (`1b e4 12 24 04 38 ...`), no real touch frame (`0a 1e 16 22 ...`), so the driver prints `slot 0 out of range / slot 4 unknown state 0 / slot 28 out of range`
  and no input events. => the falling-edge setting was one real bug (IRQ storm), but not the one that blocks the frame data.
- **IRQ timing probe** (`irqdelay` / `irqprobe`, display A, `pmos-dump/irqdbg_A.txt`): in the first 40 touch IRQs the frame at 0x21c00 was re-read 0 / 0.2 / 1 / 5 / 20 ms after the IRQ:
  199 of 200 reads are the stuck stream (`1b e4 12 24 04 38 09 24 0e 0a 00 01`), one transient glitch (`... 09 24 ff ff 0a 24`, #37 at +5 ms), never a touch frame. IRQ period while touching ~27 ms.
  `irq_get_irqchip_state(LINE_LEVEL)` returned 0 everywhere (before/after/all re-reads) - not trustworthy as the physical level on this eint. => read delay after the IRQ is NOT the cause.

### Pad config and display timing (pmOS vs working downstream)

- Pad config of the touch lines is identical: pmOS `pin 1 (INT) 001: 0001001111`, `pin 2 (RST) 002: 0111001100` = TWRP's values (`../pmos-dump/gpio_pmos.txt`).
- Display timing from the bootloader log (`pl_lk`, CSOT, what Xiaomi really drives): `vsa 2, vbp 10, vfp 12, vact_line 2340, hsa 16, hbp 56, hfp 64`, `PLL_CLOCK 553` (~1106 Mbit/s per lane),
  `cont_clock 0`, `fps=6024` (60.24 Hz, pixel clock ~173.2 MHz). Mainline `csot_fhd_video_panel_default_mode` (copied from another phone): `vfp 14`, 60.00 Hz, 172.6 MHz.
  -> different vertical front porch (12 vs 14) and refresh/clock; TDDI touch scanning is time-shared with the display timing, so this is the one remaining difference of the display half. Untested.

## Capturing the same transactions on the working downstream kernel (TWRP, 4.14.247-Neutrino): attempted, not possible with the tools at hand (`../twrp-trace/`)

- `spi:*` tracepoints exist (`CONFIG_EVENT_TRACING=y`) but in this 4.14 `spi_transfer_start/stop` print only `spi5.0 <ptr> len=N` (no tx/rx data): lengths/timing only.
- No `kprobe_events` (only `uprobe_events`), so memory arguments cannot be fetched. The `function` tracer on `nvt_flash_read/CTP_SPI_READ` wedged ftrace (reads of `current_tracer` hung); do not use it on this kernel.
- `/proc/NVTSPI` (downstream's debug interface, `nt36xxx.c`: request in the user buffer `[rw<<7|len>>8][len][cmd][data]`) was driven by a small static aarch64 helper (`nvtspi.c`):
  it returns **all zeros for every address**, including ones the driver reads fine itself (`0x21c9c` -> `20 59 ff a1 ...` via `/proc/nvt_xiaomi_config_info`), with no error in `dmesg`.
  Reason unknown (the handler in this kernel may differ from the sources in `~/miker`, or the helper's request format is off). Not resolved.
- Needs: the source of the actual TWRP kernel, a logic analyzer, or a kernel built with SPI data tracing.

Conclusion so far: wiring, power (3.0 V `vtp`), reset, clock (1/5/10 MHz), page-select shape, firmware and the write path are fine;
what is wrong is the **read data path after the firmware has started** (chip answers with a fixed stream regardless of page/cmd).
Next real step: logic analyzer on CS/SCK/MOSI/MISO during one IRQ read, or compare against a capture of the same read in the downstream kernel.

## Not captured (needs source or a logic analyzer)

- Exact SPI wire sequence of the IRQ read (cmd byte / fast-read at `0x3F310` / dummy byte).
- Per-pin bias/drive strength of `pmx_ts_active` (the `pinctrl-0` phandles `0x180/0x181`).


## Re-reading the downstream capture (touchcap.txt) + downstream source (drivers/input/touchscreen/mediatek/NT36xxx_HOSTDL_SPI)

- Even on the WORKING downstream, 2050 of ~2076 `/proc/NVTSPI` reads of 0x21c00 *while touching* return the stuck stream; only ~1% show a real frame (`0a 1e 16 22 0a 0a ff ...`) or a half-overwritten
  one (`... 00 12 ff ff 0a 15 1c 7c 0a 0a ff ...`). So a touch frame is only visible in a SHORT WINDOW after the INT edge; the stuck stream (= fw-info block from 0x21c78, returned for any cmd offset in page 0x438) is the background.
  The IRQ rate is the same on both (~38 Hz while touching: downstream 1160 IRQs / 30 s, pmOS ~37 Hz).
- Downstream `nvt_ts_work_func`: ONE `CTP_SPI_READ(point_data, 65+1)` (cmd 0x00), **no page select** (page 0x21c00 stays selected since `nvt_get_fw_info`/`nvt_read_pid` end with `nvt_set_page(EVENT_BUF_ADDR)`).
  Mainline sends the page select with EVERY read (extra SPI message + CS toggle before the data), i.e. the data read starts later. Hypothesis: the frame window is shorter than the extra latency. Test: `pgskip=1`.
- Downstream resume/boot also does `nvt_check_fw_reset_state(RESET_STATE_REK)` (wait until 0x21c60 in 0xA1..0xAF) after the download; mainline only needs INIT (0xA0). Not yet tested.
- Downstream boot update runs from a work 14 s after probe ("make sure boot update starts after display reset (RESX) sequence"); the fw is downloaded after the DSI init, hw_crc=1 for NT36672A (same path as mainline).

### BREAKTHROUGH: pgskip (no page select before the IRQ read) returns real touch frames on pmOS (display A, `pmos-dump/pgskip_A.txt`)

- `irqmode=1` + `pgskip=1`: in the first 30 IRQs the first read had a REAL touch frame 27 times (`0a 2b 1e bc 0a 0a ff ...` = id 1, update, x~699 y~492; next ones `0a 29 1c 04`, `0a 21 15 ee`, `0a 1c 11 64`:
  smoothly moving coordinates), 3 times all-ff (no event / release). Never the stuck stream. No `slot ... out of range` warnings any more.
- So the extra page-select message in front of every read DESTROYS/skips the frame (mainline regmap sends `ff page_hi page_lo` + CS toggle before every read; downstream does not).
- Caveats seen in the same run: reads that follow a successful read within ~5 ms (the debug re-reads) and, for ~6 s at the start of the touch window, the handler's own reads returned status 0x00 (`read failed, status: 0x00`)
  when the page select was skipped -> the skip must fall back to a page select when status != 0xff. Implemented as `pgskip=1` + retry-with-page-select (module params `pgskip_n`, `pgretry_n` count both). Next: verify input events.

### ROOT CAUSE + FIX (clean v2 patch tested on display A)

- Chip behaviour: a read PRECEDED by a page select returns the parameter block (`1b e4 12 24 04 38 09 24 0e 0a ...`, the old "stuck stream") whatever the cmd offset; a read WITHOUT page select returns the touch stream (ff idle / touch frames).
  The debug build read touches relative to the event buffer (page select every time) -> always the parameter block. Upstream's `nvt_ts_irq()` reads reg 0 page-less and is fine.
- Upstream fails here only because the params read at reg 0x78 is page-less (reads the touch stream: all ff, or a touch frame if a finger is on the screen -> `0a 05 8c a0 0a 0a ff ...` seen in the boot log).
- Fix v2 (`touch-fix/nvt-touch-fix-v2.diff`, core only): params read at `event_buffer | 0x78` + IRQ trigger from DT. Result: probe OK, idle 0 IRQs / 10 s, 1194 input events in 20 s of touching.
- Draft report: `report-to-maintainer-v2.md` (not sent).
- Orientation check (fingers held in corners, v2 driver): TL ~(96,64), TR ~(980,118), BL ~(139,2257), BR ~(974,2238), centre ~(508,1212); panel 1080x2340; x right, y down, no swap needed. (First BL attempt was a misplaced finger.)

### Permanent install (boot image)
- The touch modules are loaded from the initramfs inside boot.img (probe at ~12.7 s, before rootfs is mounted), so replacing `/lib/modules/...` has no effect at boot.
- At boot the first params read failed (status 0x00; vtp configured only 0.28 s before) -> v3 patch (`touch-fix/nvt-touch-fix-v3.diff`) retries the upload + params read.
- New image: `touch-fix/bootwork/boot-vtp-touch.img` (= boot-vtp.img + appended cpio overriding novatek-nvt-ts-{core,spi}.ko.zst; repack tool `touch-fix/repack_boot.py`, identity-tested). Flashed with `fastboot flash boot`; rollback = flash `vtp-patch/boot-vtp.img`.
- First boot with it: touch input device registered by itself at 13.0 s, no read errors.

### Battery (power_supply) - voltage/OCV estimate, no fuel gauge
- Hardware: mt6360-chg (charger, psy type USB) + mt6360-adc (vbat/ibat IIO channels 4 and 6). No gauge driver in mainline for the MT6359 FG, so no Battery psy -> UI had no battery.
- `battery/adc-ocv-battery.c` (out-of-tree module, installed in /lib/modules/6.16.4/extra) + DT nodes `battery` / `battery-info` (simple-battery, 4500 mAh, generic OCV table) added to the DTB in `touch-fix/bootwork/boot-vtp-touch-bat.img` (`battery/patch_dts_battery.py`).
- Result after boot: type=Battery, status=Charging, capacity=99, voltage_now=4227500, current_now=40000, technology=Li-ion. Capacity is an OCV estimate (not coulomb counting).

#### UPDATE 2026-11-12: OCV crutch replaced by a real gauge driver (`battery/mt6359-gauge/`)
- `adc-ocv-battery` was inaccurate by design (MT6360 IBAT ADC sees only charge current; no discharge current -> wrong IR compensation; capacity jumped 19% -> 59% at constant voltage) and had a race (charger looked up once at probe -> status stuck at Discharging). Moved to `battery/_obsolete/`.
- New `mt6359-gauge.ko` reads the MT6359 FGADC through the pwrap regmap (`1000d000.pwrap`): FGADC_CON1 latch protocol (SW_READ_PRE bit0 -> wait LATCHDATA_ST bit15 -> read CUR_CON0 0xd8a, CAR_CON0/1 0xd16/0xd18 -> SW_CLEAR bit3), current LSB 610.352 uA (5 mOhm shunt, signed, + = charging), coulomb LSB 0.085 uAh. Source: downstream `drivers/misc/mediatek/pmic/mt6359/v1/mt6359_gauge.c`.
- Verified: FG current vs MT6360 IBAT ADC within ~3%; coulomb counter delta vs integral of current 3969 vs 3946 uAh over 42 s (0.6%).
- Estimator: discharged charge tracked from the hardware CAR on top of an OCV start point (vendor battery0_profile_t1, 25 degC, R from the same table + 10 mOhm PCB), slow OCV resync when |I| < 150 mA, FULL from the charger -> 100%. Usable capacity down to 3.4 V = 4386 mAh. No temperature compensation, state not saved across reboots (re-derived from OCV at boot, +-3%).
- Binds to the existing `/battery` DT node (`begonia,adc-ocv-battery` alias kept, `begonia,mt6359-gauge` also accepted) -> no DTB change needed. `battery/mt6359-gauge/install.sh` installs it to `/lib/modules/6.16.4/extra`.
- Charging was limited to 500 mA: `mt6360-chg` `input_current_limit` and `constant_charge_current` default to 500000 although the PD source offers 5 V / 2 A. Writing 2000000 to both (sysfs, root) gives ~1.3 A into the battery. Not persistent.
