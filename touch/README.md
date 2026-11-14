# NT36672A touchscreen (+ panel) — Redmi Note 8 Pro

Symptom: after the firmware load the touchscreen gives a constant stream of data. Result: it works with the `novatek-nvt-ts-core` fix
(reliable probe, IRQ type from DT) + the touch power (`vtp` 3.0 V) in DT.

| What | Where |
|---|---|
| Final kernel patches | `patches/0003` (`vtp` power + the proximity sensor threshold), `patches/0007` (`novatek-nvt-ts`) — canonical: `../kernel-build/kernel/patches/` |
| History of the driver changes (diffs v1/v2/v3), installing/rolling back on the phone | `fix/` |
| Reports to the maintainer (drafts, not sent) | `touch-bug-report.md` (the bug), `report-to-maintainer.md`, `report-to-maintainer-v2.md` (the fix in 2 hunks) |
| What the working downstream kernel (TWRP) shows: registers, regulators, pins | `twrp-touch-dump/` (read `FINDINGS.md`) |
| SPI exchange traces (kprobe, the reference matrix) | `twrp-trace/` |
| Debug tools (driver instrumentation: dump, IRQ, markers) | `debug-nvtdump/` |
| DT changes (SPI 1/10 MHz, vtp) as `.dts` | `vtp-patch/` |
| NT36672A panel: building the module/ramdisk, checking | `panel/` |
| LCM table (vendor `lcm_runs.json`, a candidate) | `lcm/` |
| Review of the pmOS package sources (APKBUILD, SPI) | `src-review/` |
| The touch firmware package from pmaports (`firmware-xiaomi-begonia-touchscreen`) | `pmaports-firmware/` (the firmware `nt36672a_begonia_csot.bin.zst` itself is not included) |

Not included: `.ko`, `.img`, ramdisks, `bootwork/` (unpacked initramfs), the `pmos-dump/` snapshots (≈9 MB of traces).

## Panel: black screen after boot — FIXED (2026-11-13, patch 0018)

**Symptom (since the very first pmOS boot):** after boot the screen stays black (backlight on) until the
screen is locked and unlocked once; the boot splash was never visible either.

**What was ruled out:**
- The display pipeline: `/sys/kernel/debug/dri/0/state` shows the CRTC active and plane-0 scanning out
  the kwin buffer (1080×2340); OVL interrupts tick at ~60 Hz. A full DPMS off/on (what lock/unlock does)
  leaves every *configuration* register of OVL0/OVL_2L0/RDMA0/DSI/MMSYS bit-identical — only status and
  debug counters differ. So the SoC side is configured the same before and after.
- The touch controller (NT36672A is a TDDI: touch and display in one chip): re-binding
  `novatek-nvt-ts-spi` (its probe resets the touch part several times) does not affect the picture.
- No DRM/DSI errors in `journalctl -k` (note: `dmesg` gets flooded by gen4m within a minute).

**Cause:** the bootloader leaves the CSOT panel running. `panel-novatek-nt36672a` prepares it with a
single 5 ms reset pulse and sends the init commands 5 ms later — enough for a cold panel (after DPMS off
the regulators are cut and reset is held), but not for one LK left running. The DSI interrupt count
confirms that one full init sequence *is* sent at boot; the panel just does not come back from it.

**Fix:** the downstream reset sequence of `csot_fhd_nt36672a_dsi_vdo` (vendor `lcm_init_power`):
`low 5 ms → high 5 ms → low 1 ms → high 11 ms` before the first command. In logical (active-low GPIO)
terms: `{1,5},{0,5},{1,1},{0,11}` in `csot_fhd_video_reset_seq`.
Patch: `patches/0018-drm-panel-nt36672a-Use-the-downstream-reset-sequence.patch`
(canonical: `../kernel-build/kernel/patches/`), kernel package r5.

**Verified on hardware:** GUI appears right after boot without lock/unlock, and the pmOS boot splash is
visible now too.

**Not done:** the vendor also sends `display on (0x29)` *before* `sleep out (0x11)`; mainline does it the
other way round. Not needed for the fix, left as is.
