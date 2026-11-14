<!-- Excerpt from the chronological log docs/NOTES-chronological.md (split by topic). -->
<!-- References like wifi/..., audio/..., pmos-begonia/... in the text refer to the old layout of the working directory; see README.md at the root of port/. -->

# GPS — notes

## RESULT #25 (2026-11-11): GPS - reconnaissance. Conclusion: it is NOT done the same way as WiFi/BT
Facts:
- FUNC_CTRL(GPS on) `01 06 02 00 02 01` -> `02 06 01 00 00` (OK), off the same; during 8 s of GPS on NOT A SINGLE frame of type 2 came over STP (the chip does not output NMEA by itself). WiFi/BT were not affected.
- There is no separate GPS firmware in /vendor/firmware (only patch_mcu, ram_{mcu,bt,wifi}, fm). Vendor: /bin/mnld (host daemon, 32/64-bit Android), /lib/libmnl.so (ARM 32-bit, Android 28, 4.2 MB, .text ~1.2 MB, a closed GNSS engine: it produces the $PMTK001,... answers itself), /bin/mtk_agpsd, HAL gnss@2.1-impl-mediatek, MNL_Config.xml (GnssMode GP+GL+GA+BD), assistance data EPO.DAT/MTKNAV.DAT/QEPO*.DAT/almanac.dat.
- mnld uses /dev/stpgps, /dev/gps_emi, /dev/ttygserial, ioctl COMBO_IOC_GET_GPS_LNA_PIN, COMBO_IOC_GPS_IC_FW/HW_VERSION; chip detection MT6630/MT6632 (we have HCR=0x6632). The driver of these char devices (stp_chrdev_gps / gps_emi) is missing from the available OSS sources (connectivity/gps in the repository is the new data_link scheme for conninfra/connac2, not ours).
- The navigation solution is computed by the host (libmnl); without it a fix is impossible. Options: (A) an Android container/libhybris with mnld + our own kernel driver stpgps/gps_emi (weeks, high risk); (B) reversing libmnl (the DSP/EMI protocol), an open engine is impossible without it; (C) WiFi geolocation through GeoClue (wifi enable=true in /etc/geoclue/geoclue.conf, the network is there) - a working substitute without GPS (but it sends the BSSID list to an external service).
Decision: GPS is not taken on; WiFi geolocation was suggested.

## ⏸ GPS — POSTPONED (2026-11-11). State and how to continue
The user decided: try the "hack" — the vendor `mnld`+`libmnl` (the closed GNSS engine on the host) in an Android chroot + our own kernel interface. The work stopped at the step "check RX DMA" (see below). Everything is built and on the phone, it can be continued with one command.

### Architecture (what was done)
- `module/consys-bringup.c` (shared infrastructure, 1 reboot on update): a common receive thread with a refcount (`consys_rx_get/put`), dispatching STP frames by type (`consys_stp_register`, type 2 = GPS), synchronous WMT commands while the thread runs (`consys_wmt_cmd`), `consys_stp_send` with a window of 4 frames. GPL symbol exports. Parameters: `rx_poll_us` (polling), `rx_dma` (RX through AP_DMA), `hci_trace`. HCI/BT moved to the same refcount.
- `module/consys-gps.c` -> `consys-gps.ko` (loads/unloads without a reboot, `gps-start.sh`): `/dev/stpgps` (a transparent STP type 2 channel; open = WMT FUNC_CTRL(GPS on) `01 06 02 00 02 01`, release = off; read/write/poll; ioctl 6..20 with the vendor numeric codes: 7/8 HW/FW versions=0x8a00, 16 = LNA pin (param `lna_pin`=74), 12 = assert -> -EBUSY, 17/18/19 -> error/-EINVAL, 20 blocks until close) and `/dev/gps_emi` (a stub: 1 MB vmalloc, mmap, ioctl 1..4). Vendor sources: `mtk-modules-r-oss/connectivity/gps/stp_chrdev_gps.c`, `gps_emi.c` (for mt6785 gps_emi is a dummy).
- Userspace: the Android chroot `/opt/gps-chroot` (from `wifi/gps/gps-chroot.tgz`, 7.9 MB; built by `scratchpad/mkchroot.py` from the Infinity-X vendor.img+system.img: mnld, 32-bit ARM Android 11, linker `/apex/com.android.runtime/bin/linker`, bionic, 34 libraries). Run: `gps-chroot.sh {setup|run|stop|teardown}` (setup mounts /dev,/proc,/sys,nvdata,nvcfg; chmod 755 on the linker and mnld is REQUIRED - the debugfs dump loses the x bit). The warning "failed to find /linkerconfig/ld.config.txt" is harmless.
- `wifi/gps/hal_emu.py` — an emulator of the Android GNSS HAL: AF_UNIX/SOCK_DGRAM, ABSTRACT sockets, the name PADDED with zeros to 108 bytes (`b"\0"+name` ljust 108, address 110 B — like the HAL `safe_sendto` and the mnld bind; without that ECONNREFUSED). HAL->mnld `mtk_hal2mnl`: `int32 LE 1`, `int32 LE code` (101 init, 102 start, 103 stop, 104 cleanup; put_int = 4 bytes LE). mnld->HAL `mtk_mnl2hal` (must be listened to, otherwise mnld hangs in the sendto retry). Answers: cmd 201 (ready), cmd 308 (update_gnss_name: `MTK_MNLD_6_0_0_20040301,MNL_VER_20091003ALPS05_6.10_10`).
- `gps-run.sh [sec]`: chroot -> mnld (background) -> hal_emu (init/start, receive) -> logs /tmp/emu.log, /tmp/mnld.log, /tmp/nmea.log; for the duration of the test sets `rx_poll_us=100` and `consys_gps trace=1`.

### What is confirmed on hardware
- The MCU GPS function turns on/off (`02 06 01 00 00`); without firmware the chip is silent to PMTK (rx 0 B) -> the host uploads the DSP.
- mnld starts in the chroot (13 threads), opens /dev/gps_emi (ioctl 1,4) and /dev/stpgps, answers the emulator, UPLOADS ~40 KB into the chip (`tx 39721 B`) and gets ~7-28 KB of answers. ioctls from mnld: 10,16,7,8,9, periodically 13 and 12 (assert, when the chip "does not answer").
- PROBLEM: byte loss on BTIF PIO receive — the 512 B GPS frames arrive shortened (~20 B lost, at the end the header of the next frame is visible; the streaming 8-byte records `aa 0f 32 NN 1c 00 xx yy` get out of sync) -> `[STP-FULL] CRC bad, len=512`, the MCU retransmits endlessly (period ~364 ms), mnld exits. Cause: the FIFO overflows (2 MMIO reads per byte cannot keep up with the line). A dump of the first two broken frames is printed once (`stp-bad:`).
- FIX (built and deployed, NOT VERIFIED): RX through AP_DMA (`0x11000c00`, an 8 KB ring, reading WPT/RPT like `hal_rx_dma_irq_handler`; parameter `rx_dma`=1, falls back to PIO on error). Enabled while the receive thread runs (start: drain the rest of the PIO FIFO -> DMA; stop: back to PIO).

### Next steps
1. Check RX DMA: after a reboot `sudo sh ~/gps-run.sh 90` (phone on the window sill, screen up). Check: whether `CRC bad` disappeared; mnld messages in `/tmp/emu.log` after the DSP load (cmd 20x/30x), `/tmp/nmea.log`; `journalctl -k | grep -E "consys-gps|CRC bad|RX DMA"`. Also make sure BT (hci0/headphones) and WiFi survive the change of reception.
2. If DMA does not start (no data): enable the BTIF RX IER (`BTIF_IER` bit0 RXFEN) / check INT_BUF_SIZE and FLUSH for the tail in DMA; compare with `hal_btif_dma_hw_init`.
3. Further down the chain: send `set_position_mode` etc. to the emulator (look at the calls in gps.default.so: hal2mnl_gps_set_position_mode), wait for a fix; antenna/LNA: GPIO74 (`lna_pin`), mnld pulls the pin itself through ioctl 16 — real pin control may be needed in consys-gps; mnld downloads the assistance data (EPO/MTKNAV) over the network (libcurl) into /data/vendor/gps in the chroot.
4. If it works: wrap it into a systemd unit (chroot+mnld+emulator), output NMEA into a pty/gpsd.
Estimate: after RX DMA the ioctl details and the HAL emulator setup remain; a real fix indoors is unlikely — test at a window/outdoors.

### Files
`wifi/module/consys-bringup.c`, `consys-gps.c`; `wifi/gps/{gps-chroot.tgz,hal_emu.py}`; on the phone in ~/: `consys-bringup.ko consys-gps.ko gps-start.sh gps-chroot.sh gps-run.sh hal_emu.py gps-chroot.tgz gps-probe.sh`; building the chroot: `scratchpad/{unpack_vendor.py,unpack_system.py,mkchroot.py}` (WSL: ~/vendor-work, ~/system-work, ~/gps-chroot, ~/gps-vendor).
Claude Code is installed on the phone (~/.local/bin/claude 2.1.322, context in ~/begonia-docs).

## GPS: mnld comes up, the chip outputs NMEA, but there are no satellites (2026-11-12)
- mnld in the chroot waited for two Android properties that do not exist without property_service: `vendor.service.nvram_init=Ready` (otherwise "nvram_init_val not Ready" and exit) and
  `persist.vendor.connsys.chipid=0x6785` (otherwise "chip is unknown", version 0x0). The property file is made by `gps/mkprops.py` (bionic prop_area, a single file
  /dev/__properties__ without a directory/contexts; there is no getprop in the chroot, verified only by running mnld).
- With them: open /dev/stpgps ok, mnld loads the DSP, `$PMTK010,001`, NMEA at 1 Hz (GGA/RMC/GSA without GSV), the host time is there, `chip hw/fw 8A00`.
- BUT: 0 satellites in 4 minutes under an open sky, PMTKAGC always 6. Raising GPIO74 (external LNA, `gps/lna.py` through gpiochip0 uapi v2) did not help.
- Chip state: a SECOND run of mnld in a row without a reboot -> open /dev/stpgps = EIO (FUNC_CTRL(GPS on) timeout -110), the WiFi firmware goes into a dump loop
  ("Not exist CR read"), ssh over WiFi dies. For now the only fix is a reboot (over USB 172.16.42.1). A correct GPS off / connsys power cycle needs to be figured out.
- A logd stub (`gps/logd_stub.py`) + `gps/gps-run-log.sh` (the mnld log in /tmp/logd.log; keep the ssh session open, setsid/nohup processes get killed).
- Further ideas: antenna/RF path (pin, ADC/EMI `/dev/gps_emi`, "set GPS_CLOCK_TYPE_P No such file", GET_RTC_FAIL), compare with the vendor GPS on sequence.

### GPS: TODO (postponed 2026-11-12, come back later)
The status in the wiki stays Broken: the chip is alive, NMEA flows, 0 satellites. What to check next:
1. RF/antenna: what else the vendor WMT does on GPS on (GPS_SYNC pins, `wmt_gps_lna_enable`/`wmt_gps_lna_pin` in WMT_SOC.cfg - the LNA may be on a pin of the chip itself, not on GPIO74).
2. `set GPS_CLOCK_TYPE_P: No such file` - a property/file with the clock type (TCXO) for mnld; `GET_RTC_FAIL`; EMI/ADC (`/dev/gps_emi`, IOCTL_EMI_MEMORY_INIT fails).
3. A correct GPS off/connsys power cycle, so that no reboot is needed between runs.
4. Before a test on the balcony: one run per boot, keep ssh open (`sudo sh gps-run-log.sh 300`).

