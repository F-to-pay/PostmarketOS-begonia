# Redmi Note 8 Pro (begonia, MT6785) → postmarketOS mainline 6.16.4: port sources

Everything written/built/fixed during the port, sorted by area. Only **sources and notes** live here:
no images, `.ko` files, object files, ROMs, partition dumps or proprietary firmware (see the list below).

State as of 2026-11-13. Target system: `linux-postmarketos-mediatek-mt6785` 6.16.4 (tag `6.16.4-r0` of mt6785-mainline).

> **Authorship.** The code, kernel patches, scripts and notes in this folder were written by an AI —
> Claude (Anthropic, model Claude Opus 5.5) working through Claude Code, with shell access to the phone and the build machine.
> Testing on real hardware — listening, pressing buttons, plugging cables, waving hands at sensors,
> holding the phone to a window for GPS — was done by the human owner of the device: press F to pay respects.
> Review the patches with that in mind: they work on this one unit, but nobody has read them line by line yet.

## Map

| Folder | What is inside | Status on hardware |
|---|---|---|
| [`wifi/`](wifi/) | `consys-bringup.c` (CONSYS: power, firmware over BTIF/STP/WMT), gen4m driver port (vendor 4.14 → 6.16), DT node, autostart | works: wlan0/wlan1, scan, WPA2-Enterprise, traffic |
| [`bluetooth/`](bluetooth/) | the BT code lives in `wifi/consys-bringup.c` (HCI over STP); config, test and notes here | works: BR/EDR + LE, A2DP sink |
| [`audio/`](audio/) | MT6785 AFE port (`common/ mt6785/ codecs/`), MT6359 codec, TFA9874 amplifier, ACCDET, dual-mic noise suppression, scripts and services | works: headphones, 2 microphones, noise suppression, speaker (with protection), jack detection, earpiece (quiet) |
| [`battery/`](battery/) | patches: MT6359 fuel gauge (FGADC), MT6360 charger, battery DT | works (r3 verified on hardware) |
| [`touch/`](touch/) | NT36672A: `novatek-nvt-ts` fix, debug tools, TWRP dumps, panel notes, reports to the maintainer | works with the fix (patch 0007 + DT) |
| [`proximity/`](proximity/) | STK3310 sensor: threshold in DT, udev rule, tools | works (confirmed with a hand) |
| [`ir/`](ir/) | IR transmitter via `pwm-ir-tx` (DT + test) | works |
| [`gps/`](gps/) | `consys-gps.c` (`/dev/stpgps`, `/dev/gps_emi`), vendor `mnld` running in an Android chroot | **does not work**: the chip sends NMEA, 0 satellites |
| [`modem/`](modem/) | `md-bringup.c` (MD1: power, boot vector, CCIF, ccci-lite, RPC), `memwin.c`, Android chroot with `ccci_fsd`/`ccci_rpcd` | **does not work**: MD boots up to RF init, then asserts in `el1d_rf_error_check`; no calls/SMS/data |
| [`kernel-build/`](kernel-build/) | kernel patch series (0001–0010), pmaports overlay (APKBUILD + config), build/deploy scripts | the base of the whole image |
| [`docs/`](docs/) | full chronological log `NOTES-chronological.md`, notes on suspend/autosleep | — |

Per-area notes are in `<folder>/NOTES.md` (excerpts of the chronological log, split by topic; references in the text
to `wifi/…`, `pmos-begonia/…` refer to the old layout of the working directory). Reports to the maintainer (drafts, not sent):
`wifi/report-to-maintainer-wifi.md`, `audio/` and `battery/` — see the respective folders, `touch/report-to-maintainer*.md`.

## State as of 2026-11-13 (evening): drivers in the kernel, system reinstalled

The working drivers are no longer out of tree: WiFi/BT (`drivers/staging/mt6785-consys`, `drivers/staging/mt6785-gen4m`),
audio (`sound/soc/mediatek/mt6785`, changes to `mt6359`/`accdet`/common, `sound/soc/codecs/tfa9874.c`) — patches
`kernel-build/kernel/patches/0011…0017` (reproducible: `kernel-build/scripts/import-drivers.sh`). Patch `0018` fixes
the black screen after boot (CSOT panel: reset like the vendor does it). Kernel package `linux-postmarketos-mediatek-mt6785` r5.

The userspace part is the package `kernel-build/pmaports/device/testing/begonia-port` (WiFi/audio/idle-shutdown services,
modprobe.d, udev, PulseAudio profile, tools `hp-*`/`spk-*`/`mic-nc`/`ir-test`/`rcv-play`, subpackage `-firmware`
from `FWDIR`; it masks `start-pulseaudio-x11`, which made PulseAudio hang at session start).

Build and install from scratch (WSL, passwordless sudo):
```sh
cd port/kernel-build
sh scripts/export-patches.sh && sh scripts/apply-overlay.sh     # FWDIR=~/begonia-fw with the CONNSYS firmware
pmbootstrap build --force begonia-port linux-postmarketos-mediatek-mt6785
bash scripts/install-image.sh <backup dir>                         # pmbootstrap install + personal data into the image
# fastboot (Vol- + Power): fastboot flash userdata <sparse image>; fastboot flash boot boot.img
```
The description below (insmod, `wifi-up.sh`, `audio-up.sh`) is history from before the move into the kernel.

## How it used to fit together

1. **Kernel** (`kernel-build/`): patches `0001…0010` on tag `6.16.4-r0`. Source of truth is the `begonia-fixes` branch in `~/mt6785-linux` (WSL);
   the `<area>/patches/` folders contain copies of the relevant patches for convenience, **the canonical order is in `kernel-build/kernel/patches/`**.
   Patch 0003 is in both `touch/` and `proximity/` (it changes both the touch power and the sensor threshold).
2. **Out-of-tree modules** are built against a prepared kernel tree: `wifi/build.sh` prepares `~/mt6785-linux` (phone config
   `wifi/phone-config`, `modules_prepare`), then in each folder `make KDIR=$HOME/mt6785-linux all`:
   `wifi/` → `consys-bringup.ko`; `gps/` → `consys-gps.ko`; `modem/` → `md-bringup.ko`, `memwin.ko`; `audio/` → 6 modules (`snd-soc-*`, `mt6785-mt6359`).
   gen4m port (`wlan_6785_axi.ko`): see `wifi/gen4m-port/` and `wifi/tools/gen4m_build.sh`.
3. **On the phone**: WiFi/BT load order — `wifi/deploy/wifi-up.sh`; autostart — `wifi/deploy/install-autostart.sh`; audio — `audio/install-audio.sh`, `audio/load-audio*.sh`.

Verified when this was laid out: all wifi/gps/modem/audio modules build from this structure (WSL, clang/LLD, 6.16.4);
`port.py` on pristine vendor gen4m produces a tree byte-identical to the one the working `wlan_6785_axi.ko` was built from.
Not re-verified: a full build of gen4m and of the kernel package from this copy.

## What is NOT in this folder

* **Proprietary vendor firmware/binaries**: `connsys-fw/*.bin` (WIFI_RAM_CODE, `soc1_0_*`), TFA9874 firmware, `nt36672a_*.bin.zst`,
  `gps-chroot.tgz`, `md-chroot.tgz` (Android chroot with `mnld`, `ccci_fsd`, `ccci_rpcd` from `vendor.img`). Take them from the phone/your own backup.
* **Partitions and backups** (`begonia-backup/` — contains IMEI/NVRAM, **do not share**), MIUI/TWRP/ROM/KernelSU archives, `boot*.img` images, `xiaomi-begonia.img`.
* **Vendor and upstream trees** (a .git without a working copy): `kernel-r-oss` ← github.com/MiCode/Xiaomi_Kernel_OpenSource (`777ef5df`),
  `mtk-modules-r-oss` ← github.com/MiCode/MTK_kernel_modules (`cc4a1521`, path `connectivity/wlan/core/gen4m`),
  `mt6785-linux-6.16` ← gitlab.postmarketos.org/mt6785-mainline/linux (`203a993f`).
* Build artifacts (`*.o *.ko *.cmd *.mod.c Module.symvers`), built `*.dtb`, logs (`md-logs/*.kmsg` — 25 MB, `build.log`),
  register dumps/traces (`phone-extra/dump`, `pmos-dump`), voice and tone recordings (`audio/clips/*.wav` — these are the user's recordings, better not shared).
* Intermediate snapshots and one-off patchers: `md-bringup.c.stage*`, `consys-bringup.c.v1`, `apply_*.py`, `_obsolete/` (the old OCV battery hack).
* One-off debug scripts from the phone's home directory (`hpf-*.sh`, `gps*.sh` etc.) — they exist only on the phone.

## Caveats

* Scripts still contain hard-coded paths of the old layout: `/home/user/Begonia/wifi/…` (phone), `/mnt/c/Users/…/Begonia/…` (WSL, `gen4m_build.sh`,
  `pmos-build/scripts`). Adjust them to your environment.
* `audio/spk-tones.sh` expects `audio/clips/tones.wav` (not included); the tone is generated by `audio/tfa-fw/gen_tones.py`.
* The files in `wifi/deploy/`, `gps/deploy/`, `ir/deploy/`, `audio/deploy/`, `bluetooth/bt-test.sh` were taken from the phone (they were not on the PC).
  `md-bringup.c`, `md-begonia.dtsi`, `make-md-dtb.sh`, `module/Makefile` on the PC differ from the copy on the phone — the PC versions were taken (working copy).
* The live system on the phone, `/opt/*-chroot`, the source trees and the original directories (`wifi/`, `pmos-begonia/`, `pmos-build/`) were not touched by this layout: this is a copy.
  Patch 0010 (IR) exists only in this copy and in the WSL branch; the original `pmos-build/kernel/patches/` does not have it.
