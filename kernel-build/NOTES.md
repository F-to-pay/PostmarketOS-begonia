<!-- Excerpt from the chronological log docs/NOTES-chronological.md (split by topic). -->
<!-- References like wifi/..., audio/..., pmos-begonia/... in the text refer to the old layout of the working directory; see README.md at the root of port/. -->

# Kernel/image build — notes

### Network (important)
Connecting to the phone over WiFi — only `10.x`. On 2026-11-12 ~17:10 the network changed: PC `10.30.255.140`, phone **`10.30.255.121`** (before `10.233.161.187`). If the address does not answer — `ssh user@minilin.local` (mDNS) → `ip -4 -o addr show wlan0`. `172.16.42.1` — USB, do not use.

### Spec for the next stage (the driver layer, NOT started)
Separately from the image: the modules `consys-bringup`/`consys-gps`/`wlan_6785_axi` (`wifi/module`, `wifi/gen4m-port`), the AFE audio modules (`wifi/audio`: `snd-soc-mtk-common`, `snd-soc-mt6785-afe`, `snd-soc-mt6359`, `mt6785-mt6359`), the services `begonia-wifi.service`/`begonia-audio.service`, udev (`90-mt6785-audio.rules`, `90-stk3310.rules`), UCM/PulseAudio configs (`mt6785-mt6359.conf`, `mt6785-mic.conf`), firmware and `wifi.cfg` in `/lib/firmware`. The modules are built against `~/mt6785-linux` (the same `.config` and vermagic `6.16.4 SMP preempt mod_unload aarch64` are needed; there is no `Module.symvers` in the tree, hence `KBUILD_MODPOST_WARN=1`). Idea: one install script/package `pmos-build/drivers/` (build + install + enable the services); remove `fix-boot-dtb.sh` and `apply-dtb.sh` (the DTB now comes from the kernel package).

