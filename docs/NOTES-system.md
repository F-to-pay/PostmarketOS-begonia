<!-- Excerpt from the chronological log docs/NOTES-chronological.md (split by topic). -->
<!-- References like wifi/..., audio/..., pmos-begonia/... in the text refer to the old layout of the working directory; see README.md at the root of port/. -->

# System (suspend, autosleep) — notes

## Status at the end of the session (2026-11-11)
Works: WiFi (scan, WPA2-Enterprise PEAP, DHCP, traffic), Bluetooth (BR/EDR, LE scan, pairing, AVRCP, A2DP sink in PulseAudio, Sony WH-CH720N headphones connected), autostart at boot (begonia-wifi.service + loop protection), clean rmmod/insmod of wlan (wlan-reload.sh), a unique stable BD_ADDR.
NOT verified: a real suspend/resume (the log has no PM: suspend entry/exit - the phone only turned the screen off); A2DP sound quality over long playback; the HFP microphone; LE connections; WiFi throughput; behaviour on a firmware crash.
Known weak spots: BT reception by polling (battery), no STP retransmit/resync, the noisy gen4m log, consys-bringup can be unloaded only by a reboot, a pmOS kernel update will overwrite the DTB (fix-boot-dtb.sh), the modules are built for 6.16.4, touch after screen off/on with the button (possibly a one-off).

## RESULT #23 (2026-11-11): a real suspend does not work on this kernel; the cause is NOT CONSYS/WiFi/BT
Attempts `systemctl suspend -i` (12:14, 12:18, 12:23, 12:31): the kernel enters "PM: suspend entry (deep|s2idle)" and leaves right away: `mtu3 11201000.usb: PM: failed to suspend async: error -16` (ssusb_gadget_suspend: -EBUSY while mtu->connected; with the cable out still "mtu3: ip sleep failed!!!"), suspend_stats success=0.
On EVERY sleep request: `mediatek-drm: vblank wait timed out on crtc 0` (WARNING in mtk_crtc_atomic_disable) + `mtk_cmdq: exist running task(s) in suspend`. At 12:31 worse: bmi160_spi SPI timeout, `backlight_vddpos/vddneg failed to disable -ETIMEDOUT`, `panel-tianma-nt36672a power_off failed -110` -> kwin: "Removing GPU /dev/dri/card0" -> the GUI disappeared with the backlight on.
NetworkManager turns WiFi off and on around sleep (that is the "restart"), bluetoothd sees "Controller resume"; wlan/consys/hci0 are not reloaded by this. The headphones disconnect/reconnect along with it.
CONCLUSION: do not use suspend (PowerDevil: "never sleep", only turn the screen off). Turning the screen off on timeout is safe, WiFi/BT live on. Suspend is a separate big project about the display/USB of mainline mt6785, not about CONSYS.
Prepared (NOT needed): ~/begonia-usb-sleep.sh + install-usb-sleep-hook.sh (unbinding the USB gadget for the duration of sleep).

## ✅ RESULT #24 (2026-11-11): the "drops into TUI/GUI" = PowerDevil AUTOSLEEP after every 30 s of inactivity
powerdevilrc: [AC|Battery|LowBattery][SuspendAndShutdown] AutoSuspendIdleTimeoutSec=30 (the default action is sleep). Log: `suspend requested from client ('org_kde_powerde')` at 12:39, 12:44, 12:46, 12:54 etc. Every sleep fails (mtu3 -16), and sometimes the panel does not come back (`[drm] Wait DSI IRQ Timeout`, `panel-tianma-nt36672a: failed to send DCS Init -62`) -> kwin "Removing GPU /dev/dri/card0" -> pmOS shows the console with buffyboard (TUI); ksplashqml SIGABRT when the shell restarts.
Fix: `kwriteconfig6 --file powerdevilrc --group <AC|Battery|LowBattery> --group SuspendAndShutdown --key AutoSuspendAction 0` (backup ~/.config/powerdevilrc.bak-before-autosuspend-off). Screen blanking (30 s) is kept. Takes effect after re-login/reboot.
Consequence: NM/headphones "restarted" and the GUI crashed exactly because of autosleep, not because of CONSYS/WiFi/BT.

## ⚠ Volume keys dead / PulseAudio hanging after the reinstall — missing `xwayland` (2026-11-14)
- The fresh image was built from `/etc/apk/world` of the old system; `xwayland` was only a dependency there and did not make it.
- kwin still listens on the lazy X11 socket (`@/tmp/.X11-unix/X0`) but cannot start Xwayland, so every X11 client hangs in
  `connect()` forever (`unix_wait_for_peer`): `start-pulseaudio-x11` (PulseAudio stuck at session start) and kded6 — whose
  `audioshortcutsservice` handles the volume keys. The kernel side was fine (`mt6779-keypad` sends KEY_VOLUMEUP/DOWN).
- Fix: `apk add xwayland xwaylandvideobridge` (+ added to `kernel-build/scripts/install-image.sh`). The mask of the
  `start-pulseaudio-x11` autostart in begonia-port is now redundant but harmless.
- Debug trick: `/proc/<pid>/syscall` + `/proc/<pid>/mem` give the sockaddr of a hung `connect()`.
