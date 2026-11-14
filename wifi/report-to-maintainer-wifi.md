# Redmi Note 8 Pro (begonia): MT6785 CONNSYS WiFi brought up on mainline 6.16.4 (scan works, wlan0 registered)

DRAFT, not sent. Tested on mainline 6.16.4 (`linux-postmarketos-mediatek-mt6785-6.16.4-r1`), Redmi Note 8 Pro (MT6785), built-in CONNSYS (`soc1_0`, HW/SW id `0x8a00`), firmware taken from the vendor partition of a custom Android ROM (Project Infinity-X 3.12; driver sources: MiCode `Xiaomi_Kernel_OpenSource` / `MTK_kernel_modules`, branch `begonia-r-oss`) (`WIFI_RAM_CODE_soc1_0_2a_1.bin` + `soc1_0_{patch_mcu,ram_mcu,ram_bt,ram_wifi}_2a_1_hdr.bin`).

## Result

Out-of-tree, but reproducible: after the sequence below the vendor WiFi driver (MediaTek gen4m, ported to 6.16) loads the firmware, gets `FW_START EVT success!!` / `Ready bit asserted`, registers `wlan0` (+ a second interface `wlan1`, not investigated) and NetworkManager scans: all APs around show up on 2.4 and 5 GHz, including a DFS channel (120 / 5600 MHz), signal 80/100 next to the AP.

**Association and data path also work** (tested after the first draft of this report): `wlan0` associates through NetworkManager/wpa_supplicant to a WPA2-Enterprise (802.1X, PEAP) network on a 5 GHz DFS channel (120), gets a DHCP lease, and traffic flows: gateway ping 0 % loss (3.5-98 ms), internet ping 0 % loss (14-400 ms; the latency/jitter is high, probably power save and the very verbose gen4m debug logging, not investigated). No throughput measurement yet.

**Not tested yet:** throughput/stability over time, power management, suspend, error recovery (chip reset), regulatory behaviour, WPA3/SAE, AP mode. The driver **cannot be unloaded** (see below), so it is "boots, scans, associates, passes traffic", not "production ready".

This is *not* upstreamable as is (vendor WMT replaced by a small shim, gen4m is a ~1100-file out-of-tree driver). I'm writing it up because the **bring-up sequence and the traps** are the hard part and apply to any MT6785 / `soc1_0` (and probably other pre-conninfra MediaTek) port, and so that nobody else spends days on the same dead ends.

## What it took (chronological, with the wrong turns)

The mt6785 vendor code uses the **old WMT scheme**, not conninfra (`common_main/platform/mt6785.c`, no SMC/SIP calls into ATF at all). Everything is plain register writes + regulators, so it can be done in a normal driver without reverse engineering secrets. Layers, bottom-up:

### 1. Power-on and chip id (works with what mainline already has)

- genpd `MT6785_POWER_DOMAIN_CONN` (already in `mt6785-pm-domains.h`), clock `CLK_INFRA_CCIF4_AP`, regulators `vcn18`, `vcn33_1_wifi`, `vcn33_2_wifi` (MT6359, currently `disabled` in the board DT).
- **XO_WCN clock buffer** (vendor `clk_buf_ctrl_internal(CLK_BUF_CONN, FORCE_ON)`; board config makes buf2 SW-controlled, so the AP must switch it on): pwrap `DCXO_ENABLE` (+0x190) clear bit 1, and MT6359 `DCXO_CW00` (reg `0x788`): `EXTBUF2_MODE` (bits 4:3) = 0, `EXTBUF2_EN_M` (bit 5) = 1. Must start from the original state (`DCXO_ENABLE=0x3`, `CW00=0x4e1d`) - after a bad power-off the chip id read back `0` until the original values were restored.
- pericfg `CCU_CONFIG` (+`INFRASYS_COMMON_AP2MD_PCCIF4_AP_PERI_AP_CCU_CONFIG`) bit 1 `CON_PWR_ON`, pinmux for the CONN pins, RGU reset (`0x10007000`, shared with the watchdog -> `ioremap` without `request_mem_region`).
- Check: `readl(0x180b1000 + 0x10) == 0x10020600` (needs ~20 ms after power-on; first read right after returns 0).

### 2. EMI window + ROM patches -> "MCU ALIVE"

- **Trap:** the vendor variable `topckgen_base` is really **INFRACFG_AO (`0x10001000`)**, not topckgen. EMI remap: `infracfg_ao + 0x380 = emi_base >> 16` (20 bits), `+0x388 = 0x10003000 >> 16`. Writing `0x10000380` corrupts unrelated registers.
- 4 MiB reserved (`no-map`) at `0xbc000000` for the CONSYS EMI region, 3 MiB for WiFi (`shared-dma-pool`).
- `*_hdr.bin` layout: 48 byte header for the RAM images; EMI offset = `le32@24 & 0x00ffff00` (mcu `0x000000`, bt `0x080000`, wifi `0x140000`); write them into EMI, read back, then release the MCU reset (RGU bit 12, key `0x88 << 24`). Alive marker: `0x18002600 == 0x1d1e` ~40 ms later; idle `CPUPCR = 0xb04` (ROM idle loop, waiting for host commands).
- Tail of vendor `polling_consys_chipid()` that matters: `0x18004160` bit 4 (CONN2EMI slpprot), `0x180c1600` bit 23 (toppose_restore_done), `0x180b3034` bits [20:19] = 01 (WPLL).

### 3. Host <-> MCU channel (BTIF + STP + WMT)

- BTIF `0x1100c000`. **PIO is fine for control traffic** (small frames), polled, FIFO 16 bytes, check `LSR`.
- STP *mandatory* framing before full mode: `[0x80+(txseq<<3)] [type<<4 | len>>8] [len] [0x00] payload [00 00]`, WMT type = 4. WMT command `01 <opcode> <len16> <payload>`, event `02 <opcode> ...`.
- Switch to STP *full* (`SET_STP`), then SET_CHIP_ID `0x6785`. Full mode is stop-and-wait: header `0x80|seq<<3|ack`, header-only ACK, CRC-16/ARC (kernel `crc16()`) over the payload. Minimum that works.
- **Receive buffer must hold a whole STP frame (>= ~460 bytes, I use 1024).** I originally had 320: a long frame was cut, its CRC never matched and the MCU retransmitted the same seq forever. This looked exactly like "MCU hangs on RF calibration" (see below).

### 4. The real blocker: patch_mcu (PDA) - **PIO silently loses bytes, use the AP_DMA TX vFIFO**

Symptoms for days, all with the RAM images loaded but no `patch_mcu`:
- `RF calibration` (`01 14 01 00 01`) is ACKed and then **no event ever arrives**, `CPUPCR` stuck in the MCU's RAM code (`0xf000c9da..cd76`), subsequent `FUNC_CTRL` dead.
- gen4m runs through HIF/DMA/firmware download fine, sends `FW_START` and **gets no event for 3 s** (`CONN_HIF_ON_DBGCR01 = 0xb04`, MCU in ROM idle).

Things I proved *not* to be the cause (each cost hours): `QUERY_A_DIE`, the `polling_consys_chipid` tail registers, PALDO/ACR/AFE (no-ops on mt6785), EMI sections of the WiFi RAM code, `FUNC_CTRL(WIFI on)` before download, adding calibration with/without the WPLL setting.

Cause: the vendor loads `patch_mcu` first, via the **PDA** protocol, and I had skipped it because after the `PDA CFG` command the ROM went silent. The body was sent over BTIF **PIO** (polled `THR` writes with proper `LSR` checks - looked right, but the ROM never accepted it). The vendor sends it over **AP_DMA BTIF TX** (`tx-dma 0x11000b80`, 8 KiB vFIFO). With DMA:

```
PDA-CFG   01 01 11 00 00 | addr[4] | size[4] | key[4]=0 | 2f 02 02 00     -> 02 01 01 00 00
raw hdr   le32(size+12), 8 zero bytes
raw body  22336 bytes (patch_mcu file minus 28 byte header and 2 byte checksum), 1000-byte fragments
WMT reset 01 07 01 00 04                                                    -> 02 07 01 00 00   (CPUPCR 0xb04 -> 0x1e664)
```

- `addr` = `hdr[24:28] & ~0xff` = `0x0001c000` (a low MCU address, unlike the `0xf0xx0000` of the RAM images). The CFG command does not validate it, so it can't be guessed by trial and error.
- TX DMA programming (from `btif_dma_priv.h` / `btif_dma_plat.c`): warm reset, `VFF_ADDR/ADDR_H`, `VFF_LEN`, `WPT=RPT=0`, `THRE = size-7`, `EN=1`; per write copy into the ring, update `WPT` (wrap bit `0x10000`), flush if `0 < VALID < 8`; done when `VALID == 0 && INT_BUF_SIZE == 0 && LSR.TEMT`. Polled, no IRQ needed.
- **Trap:** in `BTIF_DMA_EN` bit 0 is **RX** DMA, bit 1 is **TX** DMA. I first set bit 0: the AP_DMA channel pulled the bytes into its internal buffer (`INT_BUF_SIZE = 12`) but BTIF never accepted them.
- After this: `STP full -> SET_CHIP_ID -> QUERY_A_DIE -> RF calibration -> FUNC_CTRL(WIFI on)` all succeed. The calibration event is `02 14 <len16=0x1c2> 00 01 <data...>` (454 byte STP frame), longer than the `02 14 02 00 00 01` the vendor code compares against - check opcode/status/subtype, not the length.

### 5. gen4m on 6.16

- Profile is **CONNAC** (not MT6632, even though the MCU register reports `0x6632`): `MTK_COMBO_CHIP=CONNAC WLAN_CHIP_ID=6785 WIFI_IP_SET=2 MTK_ANDROID_EMI=y`, HIF = AXI (`mediatek,wifi`, `reg = <0x18000000 0x100000>`, IRQ SPI 322 level-low, `memory-region` = 3 MiB pool). `WIFI_IP_SET=2` selects `..._2a_1.bin` and the 20 byte RX descriptor. Without `CFG_MTK_ANDROID_EMI` (and exporting `gConEmiPhyBase`/size) `wlanDownloadEMISection` silently copies nothing.
- Vendor WMT replaced by a ~17-function shim (`mtk_wcn_wmt_{wlan_reg,msgcb_*,ic_info_get,assert_*,mpu_lock_*}`, power-off flag, ...) exported from my CONSYS module, which also does the bring-up above and then calls `wlan_probe_cb`.
- 4.14 -> 6.16 API porting is mechanical but long (710 -> 0 compile errors): `cfg80211_ops` (link_id on key ops, `update_mgmt_frame_registrations`, `link_sta_params`, `roam_info.links[]`), `proc_ops`, `set_fs`/`KERNEL_DS` -> `kernel_read`, timers, `netif_rx`, platform `.remove` -> void, etc.

## Reproduce

Modules built against the exact phone kernel (vermagic `6.16.4 SMP preempt mod_unload aarch64`, keep `DEBUG_INFO_BTF` as in the phone config or `struct module` size differs):

```
modprobe cfg80211
insmod consys-bringup.ko stage=8 calib=3     # power, ROM patches, PDA patch_mcu (DMA), STP full, chip id, A-die, RF cal, WIFI on; stays on
insmod wlan_6785_axi.ko                      # gen4m: firmware download -> FW_START -> wlan0
```

Needs `WIFI_RAM_CODE_soc1_0_2a_1.bin`, `wifi.cfg`, `txpowerctrl.cfg` in `/lib/firmware` and the four `soc1_0_*_hdr.bin` in the module's `fw_dir` (all from the vendor image; I don't think they're redistributable). DT additions: `consys@18002000` (`reg` map as in the vendor DT, clocks `CCIF4_AP`, genpd `CONN`, regulators, syscons pericfg/pwrap, `memory-region`), `wifi@18000000` and the two reserved regions.

## Remaining (known) issues

- `wifi.cfg` / `txpowerctrl.cfg` missing on the first runs -> `nicCfgChipCapPhyCap: illegle ucWifiPath 0`, `glLoadNvram fail` (random MAC each load).
- Harmless warnings: `mtsoc1_0_patch_e1_hdr.bin` not found (not shipped in this vendor image).
- (fixed, see "Unloading gen4m" below) `rmmod wlan_6785_axi` used to oops.
- No suspend/resume, no recovery after MCU assert; RF calibration data is neither saved nor restored.
- Polled BTIF only (no IRQ), CONSYS registers mapped by physical address, EMI remap via infracfg regmap; a proper version needs a consys/WMT-lite driver with bindings.
- **pmOS packaging trap:** `apk upgrade` runs `mkinitfs` -> `boot-deploy`, which rebuilds `boot.img` from `/boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb` and flashes the `boot` partition. Any DTB customisation (here: consys/wifi nodes) lives only in that file and is silently reverted by the next upgrade.

## Questions

1. Is there interest in the CONSYS power/EMI/bring-up part as an upstream driver (and DT bindings for `mediatek,mt6785-consys`), or should it stay a downstream pmOS package?
2. Should the XO_WCN buffer control live in the MT6359 / pwrap driver (it is a DCXO_CW00 + pwrap register pair that the CONN block needs the AP to toggle)?
3. For the board DT: OK to enable `vcn18`/`vcn33_*_wifi` and add the reserved regions on begonia unconditionally?

Everything above is from `NOTES.md` in my working tree (results #1-#11); I can share the module and the patched gen4m tree.

## Bluetooth (same CONSYS, H4 over STP)

On this SoC (old WMT scheme, `bt/mt66xx/legacy` in the vendor tree) Bluetooth needs nothing beyond what WiFi already brings up: `FUNC_CTRL(BT on)` (`01 06 02 00 00 01` -> `02 06 01 00 00`), then H4 packets (including the packet-type byte) are carried in STP-full frames of **type 0** (`BT_TASK_INDX`) over the same BTIF link; the BT firmware (`soc1_0_ram_bt_2a_1_hdr.bin`) is the ROM patch already in the EMI. I register a plain `hci_dev` (`HCI_UART`) with `open` = BT on, `close` = BT off, `send` = STP frame, RX = a polling kthread feeding `h4_recv_buf()` (copied `h4_recv.h`); `hci0` comes up in BlueZ (version 11 / BT 5.2, manufacturer 0x0046, default BD_ADDR `00:00:46:76:63:01`).

Status: inquiry works (RSSI -27 dBm at ~0.5 m from a laptop), incoming connection from the laptop works with L2CAP/AVDTP traffic, LE scan sees dozens of devices. Not tested: outgoing connections, A2DP audio, LE connections, throughput.

Two firmware quirks that need host-side workarounds (both are "the HCI core believes the controller's capability bitmap"):

1. **Read Synchronization Train Parameters (`0x0c77`)** is advertised through the LMP feature bits (so `hci_read_sync_train_params_sync()` runs in `hci_init4`) but the firmware answers *Unknown HCI Command* (0x01), which the core treats as fatal for the whole init -> `hci0` is closed again. I answer this opcode locally with a Command Complete (status 0); the list is a writable module parameter.
2. **LE events are never enabled.** `hci_le_set_event_mask_sync()` only sets `LE Advertising Report` / `LE Connection Complete` / `Conn Update` / `Remote Features` when `commands[26]`/`commands[27]` of *Read Local Supported Commands* say the corresponding LE commands exist. This firmware returns zeros in octets 26 and 27 (although all those commands work and return success), so BlueZ ends up with an LE mask without `Advertising Report`: scan commands succeed, the controller reports nothing. I OR `0x0f` into the first byte of the LE Set Event Mask on the way out. Without that, LE looks "dead" (it took a full day to see that it was just the mask: with an all-ones mask the same controller delivers thousands of reports in seconds). A quirk flag for "supported-commands bitmap under-reports LE" would be a cleaner upstream solution than patching the command in the driver.

The Android vendor HAL (`libbluetooth_mtk.so`) additionally sends a script of vendor commands after opening the device (`Set_Radio 0xFC79`, `Set_Sleep_Timeout 0xFC7A`, `Set_TX_Power_Offset 0xFC93`, `Set_PIP 0xFCC5`, `Set_Radio_Total 0xFC2D`, ... with data from NVRAM `BT_Addr`). I tried the first four manually (all accepted, no effect on any of the problems above); they are not part of the current driver.

## Unloading gen4m (rmmod) - fixed

Symptom: `rmmod wlan_6785_axi` oopsed twice and left the module stuck (refcount -1), so every WiFi debugging iteration cost a reboot:

1. `exitWlan` -> `wiphy_unregister()` -> `device_del()` -> `klist_del()` NULL dereference (`dev->p == NULL`, i.e. the wiphy device was already torn down);
2. `hif_thread` (still running) -> `wlanAcquirePowerControl` -> `asicLowPowerOwnClear` -> `kalDevRegWrite()` into the already unmapped CSR window.

Cause: this is vendor code that was never exercised in the way I use it. With `CFG_SUPPORT_PERSIST_NETDEV` the netdevs/wiphy outlive `wlanRemove()` (the vendor build is built-in and never unloaded), and `exitWlan()` removes the netdevs first (`wlanClearDevIdx()` -> `u4WlanDevNum = 0`) and calls `glUnregisterBus(wlanRemove)` only afterwards. `wlanRemove()` then sees `u4WlanDevNum == 0`, logs `u4WlanDevNum = 0` and returns immediately: threads keep running, the adapter is never freed, and the wiphy is destroyed underneath a live driver. On Android the real teardown (`wlanRemove` + MCU power-off) is done by WMT through `wlan_remove_cb` = `hifAxiRemove()` before the module is unloaded.

Fix (two lines in my port, no change to the logic of the driver): call `hifAxiRemove()` first in `exitWlan()` (full teardown while netdevs and counters are still valid), then let the existing PERSIST_NETDEV code remove the netdevs/wiphy. In my WMT shim, `wlan_unreg` also has to clear the "already probed" flag, otherwise a later `insmod` never gets its `wlan_probe_cb`.

Result: `rmmod` returns cleanly (0 oops), `insmod` again gives `FW_START EVT success`, `Ready bit asserted`, `wlan0`/`wlan1`, NetworkManager reconnects and traffic flows (gateway ping 0 % loss) - without a reboot and without turning the WiFi function off in the MCU in between.

Upstream relevance: `exitWlan()` is wrong for any "module that can be unloaded" configuration with `CFG_SUPPORT_PERSIST_NETDEV=1`; the one-line reorder is probably worth sending to MediaTek / the gen4m maintainers if anyone still follows them.

## One more firmware quirk: BD_ADDR is reset on every BT function off/on

`FUNC_CTRL(BT on)` brings the controller up with its built-in default address (`00:00:46:76:63:01`); the Android HAL writes the real one right after open with the vendor command `0xFC1A` (`01 1a fc 06 <6 bytes, LSB first>`, it even generates a random address when NVRAM still holds the default). The address does **not** survive a BT function off/on, which BlueZ does routinely (power off/on, adapter re-init), so the address has to be programmed on *every* `hdev->open`, not once in `hdev->setup`: otherwise the controller silently comes back with the default address while BlueZ and the stored link keys still use the old one. Symptom: devices paired before the power cycle fail later with `br-connection-sdp-search` (authentication does not match), everything else (inquiry, LE scan, link-level connection) keeps working, which makes it look like an ACL/SDP data-path bug. With the address re-programmed on each open, a Sony WH-CH720N pairs, connects and PulseAudio creates its A2DP sink.

## Unrelated, but worth knowing: system suspend currently breaks the display stack

While testing "WiFi/BT survive sleep" I found that `systemctl suspend` is not usable on this kernel at all, independent of CONSYS (the CONSYS drivers are idle at those moments): every attempt aborts immediately with `mtu3 11201000.usb: PM: failed to suspend async: error -16` (EBUSY while the USB gadget is connected; `ip sleep failed` even with the cable removed) and, on the display side, logs `mediatek-drm: vblank wait timed out on crtc 0` (WARNING in `mtk_crtc_atomic_disable`) plus `mtk_cmdq: exist running task(s) in suspend`. Once the panel power-off also failed with bus timeouts (`backlight_vddpos/vddneg ... -ETIMEDOUT`, `panel-tianma-nt36672a: power_off failed ret = -110`, preceded by `bmi160_spi: SPI transfer timed out`) and kwin dropped `/dev/dri/card0`: the session stays up with the backlight on but no compositor. NetworkManager's sleep handling (it disconnects and re-associates WiFi on every suspend request) looks like a "WiFi restart" but it is only NM. Screen blanking by timeout is fine. I did not investigate the display/USB suspend problems further.
