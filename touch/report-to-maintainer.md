# xiaomi-begonia: NT36672A touchscreen returns a constant stream after firmware boot (one unit, works on yours?)

Hi! I run postmarketOS on a Redmi Note 8 Pro (`xiaomi-begonia`) with your `linux-postmarketos-mediatek-mt6785` 6.16.4
(also tried the `7.1` branch, tag `7.1-r0`) and `firmware-xiaomi-begonia-touchscreen-20250810`. Display, USB, UFS etc. work,
but the touchscreen does not. The wiki lists touch as working, so I think this is specific to my unit and I'd like to find out how
it differs from yours.

## My unit

- Redmi Note 8 Pro Global, MT6785, UFS, bootloader unlocked, MIUI-era firmware (bootloader `begonia-fa23f1804-20210701104236-202205`)
- `androidboot.hwversion=1.19.1`, `hwlevel=MP`, `hwc=GL`; `LCM_name=NONE` from the bootloader (lk log says `csot_fhd_nt36672a_dsi_vdo_lcm_drv`)
- Panel is CSOT: the Tianma init sequence gives a black screen, the CSOT one a normal picture
- Touch IC: NT36672A (TDDI), firmware `fw_ver=27`, `PID=5920` (same as the downstream kernel reports), SPI on `11019000.spi`

## Symptom

```
novatek-nvt-ts-spi spi1.0: supply vcc not found, using dummy regulator
novatek-nvt-ts-spi spi1.0: supply iovcc not found, using dummy regulator
novatek-nvt-ts-spi spi1.0: Unsupported touchscreen parameters: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
novatek-nvt-ts-spi spi1.0: probe with driver novatek-nvt-ts-spi failed with error -5
```

## What I found with a raw SPI frame dump (a log line per regmap read/write)

- The write path is fine: firmware upload, CRC setup and `boot_ready` all work, and the firmware boots (`R 0x21c60 -> 0xa0`).
- **Right after that poll every read returns the same stream, regardless of page and command byte.** Reading `0x21c00`, `0x21c08`,
  `0x21c50`, `0x21c60`, `0x21c78` all give `1b e4 12 24 04 38 09 24 0e 0a 00 01 01 01 00 12 00 ff 20 59 ff a1 08 a7 ...`
  (valid fw-info: width 1080, height 2340, max touches 10), the status/dummy byte is always `0xff`.
- With a local core patch that reads params relative to `event_buffer`, probe succeeds and `input` registers, the IRQ fires at ~90 Hz,
  but **2971 consecutive event reads (`0x21c00`, len 60) over 45 s of touching the screen are byte-identical** (the fw-info record).
- Re-writing `spi_rd_fast (0x3f310) = 0` 10 ms after the firmware boots changes nothing.
- The first read after every firmware boot is correct (`0xa0`); the next read of the same address already returns `0x1b`.

### Page x offset matrix (read right after the `0xa0` poll, 16 bytes each, status byte always `0xff` when the chip answers)

| page (reg>>7) | cmd (reg&0x7f) sent | data returned |
|---|---|---|
| `0x438` (event buffer) | `0x50, 0x08, 0x00, 0x60, 0x78` | always `1b e4 12 24 04 38 09 24 0e 0a 00 01 01 01 00 12` (= `EVENT_MAP_FWINFO`, offset `0x78`) |
| `0x439` | `0x00, 0x08` | always `20 59 ff a1 08 a7 e2 80 ff 7f 01 0f ff ff ff ff` (= what downstream reads for `nvt_xiaomi_config_info`: page of `0x21c9c`, cmd `0x1c`) |
| `0x7e6` (`0x3f300`/`0x3f30e`) | `0x00, 0x0e` | always `c0 ff a0 04 a7 34 cd ff 1b e4 12 24 04 38 09 24` |
| `0x43a, 0x437, 0x400, 0x001, 0x200` | `0x00` | no answer (dummy/status byte `0x00`) |

So the page select is honoured, but **the in-page offset (cmd byte) has no effect**: each page answers with a fixed "info record"
(the ones the firmware publishes at start-up), not with the requested offset. The first two reads after boot (`0x3f30e -> c0`, `0x21c60 -> a0`)
were correct, from the third one on it is always the fixed record. Caveat: the log shows the bytes the driver *meant* to send, not what is on the wire.

### Long reads and an RX-poison test

- RX buffers (status byte and data) pre-filled with `0xaa` before every read: **no `0xaa` ever comes back**, not even on failed reads (those return `0x00`).
  The controller always writes RX, so what we see is what was on MISO as sampled by the SPI controller (it does not exclude a wrong command byte on MOSI).
- 128-byte reads show one linear image with a page-dependent entry point:
  `[c0 ff a0 04 a7 34 cd ff] [1b e4 12 24 04 38 09 24 0e 0a 00 01 01 01 00 12 00 ff] [20 59 ff a1 08 a7 e2 80 ff 7f 01 0f] [ff ... ff] [ae] [00 ... 00]`
  (page `0x7e6` enters at the start, `0x438` at the fw-info, `0x439` at the config info).
- The image contains the value the driver wrote to `0x21c50` (`HOST_CMD`, `0xae` = "enable fw CRC") before these reads, so it is not a replay of earlier *replies*.
  It is **not a linear dump of event-buffer memory**, though: fw-info (`0x21c78`) and config-info (`0x21c9c`, per downstream) are 36 bytes apart in memory but
  adjacent (18 bytes) in the image, and `0xae` (`0x21c50`, i.e. *before* fw-info) shows up *after* config-info. A more likely picture is that the firmware answers from its own
  compact response buffer, picking a record by page only (`0x7e6` status, `0x438` fw-info, `0x439` config-info, unknown page: no answer) and ignoring the offset.
  Not tested yet: write a distinctive marker to another event-buffer address and see whether it appears in the image.

### Chip-ID read right after the bootloader reset (downstream does this, mainline does not) and a puzzling side effect

- Added `R 0x1f64e (page 0x3ec, cmd 0x4e), 7 bytes` as the first transaction after the reset, like `nvt_ts_check_chip_ver_trim()`:
  it returns `0a 00 00 72 66 03 ff` (NT36672A id, same as downstream). **The later stuck behaviour is unchanged.**
- But the head of the "image" changed: without that read the image starts with `c0 ff a0 04 a7 34 cd ff 1b e4 ...` (`c0` = data of the first read after reset,
  `0x3f30e`); with it, a later read of `0x1f64e` (16 B) returns `0a 00 00 72 66 03 ff c0 ff a0 04 a7 34 cd ff 1b ...`, i.e. **the chip id (the first read's data) is now in front**.
  So the beginning of the image depends on what was read *first* after the reset, which looks like a log of early replies rather than a static response record.
  I cannot reconcile this with "page selects the entry point" yet, and I cannot explain `04 a7 34 cd` (never returned by any earlier read).

### Firmware (re)download right after a real panel power cycle (downstream: "start after the display RESX sequence")

Forced the DSI connector off/on through sysfs (`/sys/class/drm/card0-DSI-1/status`: `dpms Off`, `enabled=disabled`, then back); the panel-follower callback
re-uploaded the firmware right after the panel was prepared again (so after the panel's reset sequence; I did not scope pio 44 itself).
**Identical behaviour**: same stuck stream in the page x offset matrix, silent pages stay silent, 2739 consecutive event reads with 1 unique payload.
Userspace (GUI restarted at that point) logs `slot 0 out of range, ignoring / slot 4 unknown state 0 / slot 28 out of range`: it interprets the fw-info bytes as touch slots.
Also checked offline: `04 a7 34 cd` is neither in the firmware blob (any byte order) nor one of its CRCs, and nobody ever read `0x21c9c` (config-info) before it shows up in the image,
so the image is not just a log of our own reads.

### Timing doubt about the "firmware booted" status (monotonic kernel timestamps, 4 uploads)

The first poll of `0x21c60` after the `boot_ready` write returns `0xa0` after only **311 / 782 / 622 / 1223 us**. Downstream waits `mdelay(5)` after `boot_ready` and
polls with 10 ms sleeps; a firmware start in ~0.3-1.2 ms looks implausible, so `0xa0` may not be a real "firmware is running" confirmation, and
"the first two reads are correct" may be an accident (they are also the first bytes of the stream). Not yet tested: reading `0x21c60`/`0x21c50` *before* `boot_ready`
(expected `0x00`/`0xae` if our writes landed), reading back the CRC bank registers, and a downstream-like `mdelay(5)` + polling loop.

### Read-back before boot_ready and a downstream-like poll (correction of the timing doubt above)

- The `0xa0` is real: the first poll after `boot_ready` (about 1 ms later, 6 bytes at `0x21c60`) returns `a0 04 a7 34 cd ff`, i.e. state `0xa0` followed by 4 bytes
  that are most likely the firmware CRC (these are the previously unexplained `04 a7 34 cd`). So the firmware does start, and that read was correct.
- **From the next poll on (1.4 ms later) every read is the stuck stream** (`1b e4 12 24 04 38 ...`), even though downstream reads `0x60` at the same point and gets address-dependent data.
- Before `boot_ready` the event page is already stuck: `R 0x21c60 -> 1b`, `R 0x21c50 -> 1b` right after we wrote `0x00` / `0xae` there. Pages `0x7e1/0x7e2`
  (`SWRST`, CRC banks, `boot_ready`) never answer reads (status byte `0x00`), i.e. they are write-only, so those writes cannot be read back.
- Observation: the correct reads (`0x3f30e`, `a0` after `boot_ready`, chip id after `0x3f310`) all came right after a *write* transaction; consecutive reads got the stuck stream.
  Counter-example: `R 0x21c40` right after the marker write `0x21c40=5a` returned `1b`. So "a read is only valid right after a write" is a candidate, not a result.

## What I already ruled out

- Supply: on the working downstream kernel (TWRP, 4.14) the PMIC regulator `vtp` (MT6360 LDO2) is always-on at **3.0 V**; on pmOS it is
  `disabled`, 1.8 V, no consumer, and the touch node has no `vcc-supply`/`iovcc-supply`. I added `vtp` 3.0 V always-on + both supplies in the DTB:
  `vtp enabled 3000000`, the warnings are gone, **same failure**. (That may still be worth adding upstream.)
- Timing after power-up (re-bind the driver minutes later: same), SPI clock 1 / 5 / 10 MHz (1 MHz is worse, 5 and 10 identical),
  page select as its own `spi_sync()` with CS released (Xiaomi style) and a delay, single full-duplex read, SPI mode 0, firmware blob
  (byte-identical to Xiaomi `fw01`), CSOT vs Tianma firmware, reset polarity, reading params at `event_buffer|0x78`.
- Downstream (`NT36672A_G7` + `spi-xiaomi-tp`) has the same page/cmd/dummy protocol, `SPI_MODE_0`, and only ever disables SPI_RD_FAST in `nvt_bootloader_reset()`.
- On the working downstream kernel the same unit logs `nvt_check_fw_reset_state error` and has `lockdown = ff ff ff ...` too, and touch still works.

## Hypotheses already tested / not tested (so nobody repeats them)

- "Fast read is still on": the driver already writes `0x3f310 = 0x00` after the bootloader reset (visible in the dump as
  `W reg=0x03f310 cmd=0x90 data=00`), and writing it again 10 ms after the firmware boots changes nothing (tested, 1469 identical frames).
- "Read pipelining / stale data from previous reads": does not match the dump. After `R 0x21c60 -> a0` the next `R 0x21c60` returns `1b`
  (not `a0`), and thousands of consecutive event reads are byte-identical, the stream does not advance. (Unexplained curiosity from
  an earlier debug run: a read of `0x3f300` returned `c0 ff a0 04 a7 34 cd ff 1b e4 12 24 ...`; `c0` and `a0` are the values read from
  `0x3f30e` and `0x21c60` just before.)
- NOT tested: a logic analyzer capture of one IRQ read (CS/SCK/MOSI/MISO), a trace of the same read on the downstream kernel, and
  the DSI init of the CSOT panel versus Xiaomi's LCM driver (no source for it here).

## IMPORTANT CORRECTION (reference measurements on the working downstream kernel, same unit, TWRP 4.14, via `/proc/NVTSPI`)

Everything above that calls the "stuck stream" a defect is wrong. On the **working** downstream kernel the very same requests return the very same answers:
event page, any offset -> `1b e4 12 24 04 38 ...` (fw-info record); `0x21c80/88` -> `20 59 ff a1 ...`; `0x3f300/0x3f30e` -> `c0 ff a0 04 a7 34 cd ff 1b e4 ...`; `0x1f64e` ->
`0a 00 00 72 66 03 ff c0 ff a0 ...`; silent pages (`0x21d00, 0x21b80, 0x20000, 0x80, 0x10000`) -> zeros; even the `0xae` at stream offsets 91/99. So that stream is simply the
chip's normal reply when **no touch event is pending**. While touching the screen on downstream, the same event read (`0x21c00`, 60 B) returns real touch frames
(`0a 1e 16 22 0a 0a ff ...`, `0a 15 1c 7c 0a 0a ...`) and the INT line fires (~39 Hz while touching).

The real differences between the working downstream and our pmOS are therefore:
1. **IRQ rate at idle: 0 / 5 s on downstream vs ~90 Hz continuously on pmOS** (4115 in 45 s, 3799 in 30 s, no touching).
2. **Touching the screen on pmOS never produces a touch frame** (2971 identical event reads over 45 s).
The read path is fine; the chip does not detect touches (or does not report them) on pmOS. Suspects now: TDDI sensing not running because of the display half of the IC
(panel init sequence, TP/display sync, RESX state) or a host command / mode that downstream sets and mainline does not.

## Questions

1. Which hardware revision / panel does your unit have (`androidboot.hwversion`, `LCM_name`, CSOT or Tianma)?
2. Do you see the same `a0` poll and then the constant fw-info stream if you dump the raw reads, or are your reads address-dependent right after boot?
3. Does anything in your tree (or in the panel's DSI init) configure the TDDI's SPI/touch block besides what is in `panel-novatek-nt36672a.c`?
4. Is there a patch or branch not in the published tag that touches the nt36672a SPI path (the `6.16.4-r0` tag is gone for me)?

Logs and a small out-of-tree module that dumps the frames are available if useful. Thanks!
