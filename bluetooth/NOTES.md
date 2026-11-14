<!-- Excerpt from the chronological log docs/NOTES-chronological.md (split by topic). -->
<!-- References like wifi/..., audio/..., pmos-begonia/... in the text refer to the old layout of the working directory; see README.md at the root of port/. -->

# Bluetooth (WMT/STP/HCI) — notes

## Bluetooth (2026-11-11), reconnaissance: mt6785 = legacy WMT; BT = FUNC_CTRL(BT on) `01 06 02 00 00 01` + H4 in STP frames of type 0 (BT_TASK_INDX), ram_bt is already in EMI. Experiment: consys-bringup.ko `bt_test=1` (stage 7/8) sends HCI Reset/Read Version/Read BD_ADDR; run `sudo sh ~/bt-test.sh` RIGHT AFTER BOOT (without wlan).

## ✅ RESULT #13 (2026-11-11): Bluetooth answers over HCI through STP type 0
`bt_test=1` (after WiFi on): FUNC_CTRL(BT on) OK -> HCI_Reset `04 0e 04 01 03 0c 00` -> Read Local Version: HCI 11 (BT 5.2), LMP 11, mfr 0x0046 (MediaTek), rev 0x257f, LMP sub 0xffdb -> BD_ADDR `00:00:46:76:63:01` (the MTK default; a normal Android takes it from NVRAM) -> BT off OK. All with WiFi on.
Next: a real `hci0` (code in consys-bringup.c, parameter `bt_hci=1`): hci_dev (bus HCI_UART, mfr 70), open = FUNC_CTRL(BT on) + a kthread receiving from BTIF (polling 1-2 ms, TODO IRQ SPI 138), close = BT off; H4 assembly via h4_recv.h (copied to module/), STP: mutex on TX, a window of <=4 unacknowledged frames (NO retransmit), tx buffer up to 1100 B. The BTIF clocks stay on after bring-up with bt_hci. wifi-up.sh now loads bluetooth.ko and sets bt_hci=1.

## RESULT #14 (2026-11-11): hci0 is up in BlueZ (Powered), but does not hear the air
HCI core init: ~40 commands passed; the only command unknown to the firmware - 0x0c77 (Read Sync Train Params, status 0x01; the kernel considers it fatal) -> workaround `hci_fake` (opcodes the driver answers itself with Command Complete(0); module_param 0644, changeable on the fly). bluetoothctl: Controller 00:00:46:76:63:01, Manufacturer 0x46, Version 11, Powered: yes.
Scan (LE 0x200b/0x200c + Inquiry 0x0401) commands pass without errors, but there are no devices. No rx errors (H4/CRC/window) in the log.
The vendor mt6785 sw_init does more (we skip it): wmt_stp_wifi_lte_coex, COEX_CONFIG_ADJUST (WMT_SOC.cfg), bt_tssi_from_wifi - candidates for the cause of the "deaf" BT radio (coex/ant).
Debug tools without a reboot: `journalctl -k | grep "HCI rx"` (telemetry of incoming packets), `echo "<hex H4>" | sudo tee /sys/module/consys_bringup/parameters/hci_raw` (a raw packet into STP type 0).

## ✅✅ RESULT #15 (2026-11-11): Bluetooth WORKS - hci0 accepted an incoming connection from the PC, ACL/L2CAP is flowing
"HCI rx" telemetry: ACL packets (type=2, handle 0x32), L2CAP signalling + AVDTP (PSM 0x19), Number of Completed Packets (13 05 ..), Disconnection Complete (05 04 .. 16). bluetoothctl devices: the PC (Intel BE201, BT address next to the WiFi MAC). So: radio TX/RX, paging/connection, the data path through STP type 0 + BTIF PIO + AP_DMA for PDA - all alive.
Open question: the phone own Inquiry/LE scan found nothing (the test was when the PC was not discoverable, surrounding LE devices were not caught) - check in a controlled way: the PC in "Add a device" mode (discoverable) and/or a phone/headset nearby with advertising on.
TODO: IRQ instead of RX polling; STP retransmit; a unique BD_ADDR (now the MTK default 00:00:46:76:63:01); clean unload; hci_fake as a permanent setting (0x0c77); log level.

## ✅ RESULT #16 (2026-11-11): Inquiry scan works
bluetoothctl scan (3rd run) found `VW BT 9943` 9C:8D:7C:30:B9:3C: Extended Inquiry Result (evt 0x2f), class 0x340408 (car audio), RSSI -97 dBm (very weak, the device is far away; the first runs were empty - nobody was nearby, the PC was not discoverable). Sensitivity not really assessed: compare with the PC at 1 m (expect -40..-60 dBm).

## RESULT #17 (2026-11-11): LE scan is silent; cause - no vendor init of the BT radio from the Android HAL
Facts: BR/EDR (Inquiry RSSI -27 for the PC, ACL/L2CAP) OK; LE: all scan commands return status 0, but 0 events 0x3e for the whole day (with LE advertising definitely working nearby, WiFi off does not help). LE features (fd 7f ff 00..), states, buffers - normal.
HAL found: vendor.img (from the Infinity-X zip, unpacked in WSL ~/vendor-work, script scratchpad/unpack_vendor.py) -> /lib64/libbluetooth_mtk.so (+ hw/android.hardware.bluetooth@1.0-impl-mediatek.so); a copy in WSL ~/bt-hal. BT_InitDevice runs a script of vendor commands (bt_init_preload_script_{6628,6630,6632,after_6893,connac,consys}); for consys: Set_BD_Addr(FC1A), Set_Radio(FC79), Set_Radio_Total(FC2D, 255 B), Set_TX_Power_Offset(FC93), Set_Sleep_Timeout(FC7A), RESET(03 0c), Set_PIP(FCC5), SSP_Debug(1804). Also: Set_BT_FTR(FC7D), Fw_Cfg_Cmd/coex from BT_FW.cfg.
NVRAM: /dev/disk/by-partlabel/nvdata (ext4, ro,noload) -> APCFG/APRDEB/BT_Addr, 440 B, copied to wifi/phone/BT_Addr. HAL structure = 12-byte header + file. Radio=07 00 00 00 07 07 (file+0x0c), Sleep=03 04 00 00 00 00 00 (+0x12), BtFTR=80 00 (+0x19), TxPWOffset=ff ff ff (+0x1b), PIP=00 00 (+0x33), RadioTotal(254 B @+0xb8)=zeros.
Next step: send these commands through hci_raw and repeat the LE scan.

## ✅ RESULT #18 (2026-11-11): CAUSE OF "LE does not hear" FOUND - the LE event mask
Discriminating test (le-test2.sh, phases in the log): 0 LE reports with the BlueZ masks (both passive and active scan), 1189/1012 with "everything allowed" (0x0c01=ff..3f, 0x2001=ff..ff). The scan type does not matter.
Mechanism: the kernel (hci_le_set_event_mask_sync) enables "LE Advertising Report" (events[0]|=0x02) only if commands[26]&0x08 (LE Set Scan Enable) in Read Local Supported Commands; likewise 0x01 conn complete (26&0x10), 0x04 conn update (27&0x04), 0x08 remote feat (27&0x20). The MTK firmware does not declare these bits (although the commands work).
Fix in the driver: when sending LE Set Event Mask (0x2001) OR events[0] with `le_mask_or` (default 0x0f; module_param 0644). The full list of Supported Commands is also printed ("Supported Commands (octets 0..63)").
Other: WMT FUNC_CTRL(WIFI off) through wmt_raw leads to a bad STP frame (CRC bad len=121) and loss of seq synchronization - the receiver needs to be robust to errors (resync/retransmit).

## ✅✅✅ RESULT #19 (2026-11-11): Bluetooth works FULLY (BR/EDR + LE), the BlueZ scan sees 43 devices
Confirmation of the diagnosis: in Read Local Supported Commands octets 26 and 27 = 0x00 (the firmware does not declare LE Set Scan Enable/Create Conn/Conn Update/Remote Features), so the kernel builds the LE Event Mask without the needed bits (log: "LE event mask 0xd0 -> 0xdf" - before the patch bits 0x0f were missing). The le_mask_or patch in send() solves it.
BT result: hci0, Powered, BR/EDR inquiry (RSSI -27 for the PC nearby), an incoming connection from the PC (ACL/L2CAP/AVDTP), LE scan (43 devices). Not verified: outgoing connections, A2DP audio, an LE connection, BLE advertising from the phone side, throughput, sleep/wake.
TODO: BTIF IRQ instead of polling; STP retransmit/resync; a unique BD_ADDR; HAL commands Set_Radio_Total/Fw_Cfg; remove the telemetry; clean unload; autoload (systemd).

## RESULT #20 (2026-11-11): BT driver improvements
- Pairing with the PC (SSP, Paired+Bonded+Trusted) and an AVRCP connection work (the link lived until a manual disconnect from the GUI). A2DP sound not verified (headphones needed).
- A unique stable BD_ADDR: module_param `bdaddr` -> hdev->setup -> vendor cmd 0xFC1A (least significant byte first, like the HAL); wifi-up.sh computes 00:00:46:<md5(/etc/machine-id)[0:6]> => 00:00:46:11:3b:43. (Changing the address resets the BlueZ bonds.)
- hci_trace (0644): 0 init only (150 packets), 1 events without LE advertising, 2 everything. Previously the 3000 packet limit was eaten by LE advertising.
- Touch: lost after turning the screen off/on with the power button (read failed status 0x00 after 0.4 s) - on NT36672A touch and display are one TDDI; the touch driver cannot resume. Workaround: `modprobe -r novatek_nvt_ts_spi novatek_nvt_ts_core; modprobe novatek_nvt_ts_spi`; the real fix - resume handling (reset + firmware reload) in novatek-nvt-ts-core.c.
TODO BT: BTIF IRQ instead of polling, STP retransmit/resync, clean unload, autoload, A2DP test, LE connection, remove the excess gen4m log.

## ✅ RESULT #22 (2026-11-11): headphones (Sony WH-CH720N) connected, A2DP sink in PulseAudio
Cause of "br-connection-sdp-search": the BT firmware resets BD_ADDR to the default (00:00:46:76:63:01) on EVERY FUNC_CTRL(BT off/on) (log: Read BD_ADDR #5 = ours 3b:43, after power cycle #67 = 01 63 76); while the address was programmed only in hdev->setup (once). BlueZ kept the old address in memory, the air had another one -> the pairing keys did not match, SDP did not get through.
Fix: consys_apply_bdaddr() in hdev->open after FUNC_CTRL(BT on), synchronously, before the rx thread starts (hdev->setup removed). After the fix: Connected, UUID (A2DP Sink/AVRCP/HFP), bluez_card + bluez_sink.*.a2dp_sink (PulseAudio, bluez5 modules; pipewire-spa-bluez NOT needed), 0 oops, 0 CRC/window.
Note: there are no ALSA sound cards (/proc/asound/cards is empty) - the phone speaker/microphone are not set up, but Bluetooth audio does not depend on that.
TODO: real playback (check the sound in the headphones), HFP microphone, LE peripheral, suspend.

