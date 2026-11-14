# Redmi Note 8 Pro (begonia) — building the system

Goal: the phone is built from sources, not from manual DTB edits and substituted `.ko` files.

```
pmos-build/
  kernel/patches/     patch series on linux 6.16.4-r0 (mt6785-mainline/linux)
  pmaports/           overlay: APKBUILD + config of the linux-postmarketos-mediatek-mt6785 package
  scripts/            export-patches / apply-overlay / build-kernel / deploy-kernel
  drivers/            (stage 2) out-of-tree drivers — WiFi/BT/GPS, audio, services
```

## What is in the image (kernel + DTB)

| Patch | What it does |
|---|---|
| 0001 | the `mt6359_fgadc` driver — a real fuel gauge (the PMIC coulomb counter) instead of the OCV hack |
| 0002 | `mt6360_charger`: the input current limit is taken from the USB-C source (through psy notifications, without a DT link — otherwise a probe deadlock), the charge limits — from the battery |
| 0003 | DTS: touch power (`vtp` 3.0 V), the proximity sensor threshold |
| 0004 | DTS: battery (`simple-battery` with an OCV table), the gauge node, the charger→battery link |
| 0005 | DTS: CONNSYS + WiFi + reserved-memory |
| 0006 | DTS: audio (AFE + sound card) |
| 0007 | `novatek-nvt-ts`: reliable probe, IRQ type from DT |

Config: `CONFIG_BATTERY_MT6359_FGADC=y` (built into the kernel, no module).
The finished DTB is part of the kernel package; `fix-boot-dtb.sh` and manual binary DTB patching are no longer needed.

## Build

Source of truth for the kernel is the `begonia-fixes` branch in `~/mt6785-linux` (WSL), base — tag `6.16.4-r0`.

```sh
# in WSL
cd /mnt/c/Users/1342c/workspace/Begonia/pmos-build
sh scripts/export-patches.sh      # if the kernel was changed: commits -> kernel/patches
sh scripts/build-kernel.sh        # overlay into pmaports + pmbootstrap build (needs sudo)
sh scripts/deploy-kernel.sh       # apk to the phone; the trigger reflashes boot by itself
```

A quick DTS check without a full build: `make ARCH=arm64 LLVM=1 mediatek/mt6785-xiaomi-begonia.dtb` in `~/mt6785-linux`.

## What is still not done properly

* the battery temperature is not taken into account, the capacity is not kept across reboots;
* WiFi/BT/GPS (`consys-bringup`, `gen4m`) and the audio drivers (AFE) are out-of-tree modules, the driver layer (stage 2).
