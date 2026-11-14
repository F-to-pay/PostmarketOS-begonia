# Camera (MT6785, Redmi Note 8 Pro) — log

The working log for the camera is kept here (no longer appended to `wifi/NOTES.md`).

## 2026-11-13: reconnaissance (read only, the phone was not touched)

### What the phone has now
`/dev/video*`, `/dev/media*`, `/dev/v4l-subdev*` — none. dmesg is silent about the camera, no ISP/seninf DT nodes in the live tree.
Only `pwm_mediatek` is loaded; no v4l2/media modules.

### Which sensors are provided for (vendor `begonia_user_defconfig`)
| Slot | Variants (module supplier) | Mainline driver |
|---|---|---|
| MAIN (main 64 MP) | S5KGW1 sunny / ofilm | none |
| SUB (front) | S5K3T2, S5K3T1 sunny/ofilm | none |
| MAIN2 (depth/macro) | GC5035, GC2375 alpha/beta, OV02A10 alpha/beta | `ov02a10.c` exists; no gc5035/gc2375 |
| SUB2 | OV02A10 AF, GC2375 AF | `ov02a10.c` |
| MAIN3 (8 MP ultra-wide) | OV8856 sunny / ofilm | `ov8856.c` exists |

Which variant exactly is fitted on THIS unit is unknown; it will only become clear by querying the ID over I²C after power-up.
Calibration EEPROMs: BRCB032GWZ_3, GT24c32a, cat24c16. Autofocus: VCM at 0x0c (main, main2), 0x18 (sub2).
Flash: MT6360 flash LED (the begonia mainline DT already has a `mediatek,mt6360-led` node, the kernel has `leds-mt6360`).

### I²C topology (from the stock `dtbo.img`, vendor bus numbering i2c0..i2c9)
| Bus | Devices (7-bit addresses) |
|---|---|
| i2c2 | camera_main `0x1a`, main_af `0x0c`, main_eeprom `0x50`, ccu_main `0x33` |
| i2c4 | camera_sub `0x2d`, sub_eeprom `0x52`, camera_sub_two `0x2e`, sub_two_af `0x18`, sub_two_eeprom `0x63`, ccu_sub `0x43`, ccu_sub2 `0x12` |
| i2c5 | MT6360: subpmic `0x34`, subpmic_pmic `0x1a`, **subpmic_ldo `0x64`** (AF/camera power) |
| i2c7 | camera_main_two `0x10`, main_two_af `0x0c`, main_two_eeprom `0x50`, ccu_main2 `0x36`, ccu_main3 `0x11` |
Speed on the camera buses — `clock-frequency = 400000`, `mediatek,use-open-drain`. (The numbering in pmOS is different: on the phone the MT6360 bus = `3-0034/3-001a/3-0064`.)
Mapping "vendor bus → mainline bus" through aliases is NOT done yet — the first thing to check.

### Power and control (from `cust_mt6785_camera.dtsi`)
* `vcamio` (1.8 V) — PMIC MT6359 `ldo_vcamio` (exists in mainline).
* `vcama` / `vcamd` — on this board these are **GPIO switches**: cam0 → GPIO114, cam1 → GPIO122/123, cam2 → GPIO123, cam3/cam4 → GPIO195.
  AF power (`vcamaf`, `vcamaf_main2`, `vcamaf_sub2`) — LDOs of the external MT6360 (`subpmic_ldo`).
* RST / PDN: cam0 118/93, cam1 124/122, cam2 125/123, cam3 119/115, cam4 197/115.
* MCLK: cam0 GPIO116, cam1 120, cam2 121, cam3 117, cam4 196 (CAMTG clocks from topckgen/camsys).
* MIPI switch (`cam_mipi_switch_sel_0/1`) — pins in `cust_mt6785_camera.dtsi`.
* Power domains: `MT6785_POWER_DOMAIN_CAM` and `_ISP` exist in the mainline DT; the clocks `clk-mt6785-cam.c`/`-img.c` exist in the kernel.

### What mainline has and what it lacks
Has: cam/img clocks, cam/isp power domains, LARB5/LARB6 in DT, I²C controllers, MT6359 `vcamio`, MT6360 regulator/flash,
V4L2 VCM drivers (`dw9714`, `dw9719`, `dw9768`, `dw9807-vcm`, `ak7375`), sensors `ov8856`, `ov02a10`.
**Lacks**: any ISP/CSI receiver driver for MT6785 (`drivers/media/platform/mediatek/` has only `mdp`, `mdp3`, `jpeg`, `vcodec`, `vpu`).
Lacks: SENINF (CSI-2 PHY `mipi_rx_ana_csi0a/0b/1a/1b/2a/2b` @ `0x11c80000…0x11c85000`), CAMSV, DMA, camera IOMMU ports in DT, S5KGW1/S5K3T2/GC5035 drivers.

### The vendor stack (for reference, porting it whole makes no sense)
`drivers/misc/mediatek/imgsensor/` (camera HW, sensors v1/v1_1, seninf), ISP 6s, CCU, LENS, flashlight — a 4.14 monolith with an ioctl interface for the Android camera HAL.
Tree: `wifi/kernel-r-oss` (git, no working copy: `git show HEAD:<path>`); sensors — `drivers/misc/mediatek/imgsensor/src/common/v1_1/<name>_mipi_raw/`.

## Plan (realistic)
1. **M0 — power and ID.** DT nodes: regulator-fixed on the vcama/vcamd GPIOs, MT6360 LDO for AF, MCLK, RST/PDN, I²C buses. Power ONE sensor, read the chip ID over I²C (read-only).
   Result: which sensors are physically fitted and at which addresses.
2. **M1 — sensor in V4L2.** For OV8856/OV02A10 — hook up the mainline drivers. For S5KGW1/S5K3T2/GC5035 — write a driver, take the mode tables from the vendor `*_Sensor.c`.
3. **M2 — frame capture.** A SENINF (CSI-2) + CAMSV driver (the simplest DMA path without the full ISP) → raw Bayer in `/dev/videoN`. IOMMU/LARB in DT needed.
4. **M3 — a picture.** libcamera with the software ISP (SoftISP) on CPU/GPU, preview in KDE; then AF (VCM), flash, front camera.
   The hardware ISP (P1/DIP/MFB/…) is not touched — that is months.

Honest estimate: M0 — an evening; M1 — a day per sensor; M2 — the bulk of the work (a week+, there is not a single ready driver for this SENINF/CAMSV to start from);
M3 — depends on how well libcamera swallows our pipeline (most likely a custom pipeline handler or `simple` will be needed).

## Risks
* Applying power and MCLK to a mismatched sensor is safe for the hardware at the standard voltages (1.8 V IO, 2.8 V analog, 1.05–1.2 V digital), but wrong GPIO switches can pull the rails down → watch dmesg.
* Any DTB change on the phone = a reboot; rollback — as for md/audio: `.pre-*` copies in `/boot`.

## 2026-11-13 11:08: M0 prepared (DTB installed on the phone, NOT rebooted yet)

**What was done**
* The vendor slot scheme (`imgsensor_cfg_table.c`): 0 MAIN→I2C_DEV_0 (i2c2), 1 SUB→DEV_1 (i2c4), 2 MAIN2→DEV_2 (i2c7), 3 SUB2→DEV_1 (i2c4), 4 MAIN3→DEV_2 (i2c7).
  SUB and MAIN3 share the MIPI mux (GPIO94: sub=0, main3=1). At the vendor the I2C address is changed by the sensor driver on the fly (the DT nodes `@1a`, `@2d`, `@10` are just "clients").
* The power sequences of 5 classes (A S5KGW1, B S5K3T1/T2, C OV8856, D GC5035, E OV02A10/GC2375) were moved into `cam-probe.c` as they are. AVDD/DVDD = GPIO switches, DOVDD = `ldo_vcamio` MT6359.
* Chip IDs and addresses (from the vendor `*_Sensor.c`): OV8856 0x10, reg 0x300B/0C = 0x885A; S5KGW1 0x2d, 0x0000 = 0x0971/0x0972; S5K3T2 0x56 = 0x3142; S5K3T1 0x2d = 0x3141; GC5035 0x37, 0xF0/F1 = 0x5035; GC2375 0x17/0x3c, 0x2375; OV02A10 0x3d, reg 0x02/03 = 0x2509.
* MCLK: mainline has `CLK_TOP_CAMTG1..5` (parents: clk26m, univpll_192m_d8=24 MHz, …). The vendor keeps TG1 always on → enable all five at 24 MHz; mux the pin to `CMMCLKn` only for the selected slot.

**Files**: `gen-camera-dtsi.py` → `camera-begonia.dtsi` (45 pin states, I2C 2/4/7 `status=okay`, node `camera-probe`),
`cam-probe.c` (+`Makefile`), `cam-id.py`, `make-cam-dtb.sh`, `install-cam-dtb.sh`, `rollback-cam-dtb.sh`.
Verified: the module builds, the DTB builds, compared to the installed one only three `status = "disabled"` → `"okay"` change + additions.
On the phone: `~/camera/` (module, scripts), `/boot/...dtb` replaced, rollback `/boot/mt6785-xiaomi-begonia.dtb.pre-cam` (= DTB with the md-bringup node).

**How to run after the reboot** (on the phone)
```
sudo insmod ~/camera/cam-probe.ko stage=1          # stage 0 = no MCLK; 1 = + CAMTG clocks (recommended); 2 = + CAM domain (hang risk)
echo '4 on' | sudo tee /sys/kernel/debug/begonia-cam/power   # slot 4 = MAIN3 = OV8856 (the only one with a mainline driver)
sudo python3 ~/camera/cam-id.py 4 --scan           # reads the ID while the slot is on
echo '4 off' | sudo tee /sys/kernel/debug/begonia-cam/power
```
One slot at a time (the module does not allow two at once). A second `insmod` after `rmmod` is safe (unlike md/GPS), but when in doubt — reboot.

## 2026-11-13 11:15–11:40: first M0 run — all slots silent, cause found (MCLK was not enabled)
* Boot with the new DTB: OK (I2C `i2c@11009000` = /dev/i2c-2, `@11011000` = i2c-4, `@1101a000` = i2c-6; WiFi/sound alive).
* `insmod cam-probe.ko stage=1`: 50 pin states + sw0/sw1 found, clocks CAMTG1..5 = 24 MHz.
* `sweep.sh` (slots 0..4, known addresses only) + a scan of 0x08..0x77 on slot 4 → **not a single ACK**. A scan of 112 addresses takes >60 s (slow NACKs) — do not run it under the 60-second ssh timeout.
* In dmesg: `slotN: mclk=1: -524` — the MCLK state was REJECTED. In mainline `drive-strength` is in mA (2..16, step 2),
  while the DT had the raw vendor `<1>` → `-ENOTSUPP`, pinctrl rolls back the whole state (the CMMCLK mux too) → sensors without MCLK.
* Fixed: `drive-strength = <4>` (gen-camera-dtsi.py), DTB rebuilt and installed (11:40). A reboot is needed, then repeat the sweep.

## 2026-11-13 12:00–12:32: ✅ M0 — SENSORS IDENTIFIED (4 of 5), the camera I2C buses came alive

### Result per slot (power + 24 MHz MCLK from cam-probe stage=1, the CAM domain is not needed)
| Slot | Bus | Address | ID | Sensor |
|---|---|---|---|---|
| 0 MAIN | i2c@11009000 | 0x2d | 0x0971 | **S5KGW1 (ofilm)**, 64 MP |
| 1 SUB  | i2c@11011000 | 0x2d | 0x3141 | **S5K3T1 (ofilm)**, front |
| 3 SUB2 | i2c@11011000 | 0x17 | 0x2375 | **GC2375**, 2 MP |
| 4 MAIN3| i2c@1101a000 | 0x10 | 0x885a | **OV8856 (ofilm)**, ultra-wide |
| 2 MAIN2| i2c@1101a000 | 0x37 | f0=0x7f f1=0x3f | ??? — answers, but the ID is not GC5035 (0x5035). OV02A10@0x3d and GC2375@0x17 — NACK |
The vendor picks the "alpha/sunny" variants of the 2 MP sensors by the supplier of the main one (`isS5kgw1_ofilm_or_sunny`), here everything is ofilm.

### Camera I2C buses are multi-channel controllers (the main finding)
* i2c2/4/7 at the vendor: `ch_offset_default = <0x100>` (i2c2 also has `ch_offset_ccu = <0x200>`). Channel 0 is dead for the CPU (built-in `i2c-mt65xx`: timeout, 0 interrupts, even with MCU_INTR=1).
* Solution — an out-of-tree `i2c-mch/i2c-mt65xx-mch.ko`, generated by `i2c-mch/mch-port.py` from mainline `i2c-mt65xx.c`; in DT the bus nodes get `compatible = "xiaomi,mt6785-i2c-mch"` + `mediatek,ch-offset = <0x100>` (the built-in driver does not take them).
* What the channel needs (found by experiment, checked against the vendor `i2c-mtk.c`):
  - transaction registers — into the channel +0x100; SOFTRESET/IO_CONFIG/TIMING/LTIMING/HS — into both channel 0 and the channel (`mch_cfg_both`); DCM_EN (0xf88) — base only;
  - **short transactions (≤8 bytes) — PIO through the FIFO, without AP-DMA** (`mch_pio`, like the vendor; mainline always uses DMA);
  - the channel configuration is rewritten before every transaction; `MCU_INTR(0x40)=1`; `FIFO_ADDR_CLR |= 0x4 (MCH)`; START = only `TRANSAC_START`.
* **Open problem:** after a NACK the channel gets stuck (channel START = 0x3, the `RESUME_ARBIT` bit is not cleared) — all subsequent transactions on that bus time out until a reboot.
  Did not help: init_hw (+/- RESUME_ARBIT into channel 0/the channel), DMA reset, disabling auto-restart, re-initializing before the transaction.
  After a SUCCESSFUL transaction the channel lives on (START=0x2, repeated reads work). Workaround for probing — `oneshot*.py`: one shot at the expected address per bus per boot.
* Debugging: the module `mch_debug=1` prints irq_stat/START; a channel register dump on timeout (`mtk_i2c_mch_dump`); test `i2c-mch/mch-test.sh`, `i2c-baseline.py [addr] [unit]`.

### Other
* MCLK pins: in mainline `drive-strength` is in mA (it was `<1>` → -ENOTSUPP, MCLK was not enabled) — fixed to `<4>`.
* Files: `oneshot.py` (16-bit registers), `oneshot8.py` (`ov`/`gc`/`g5`), `oneshot.sh`, `oneshot2.sh`, `sweep.sh` (useless while a NACK jams the bus), `diag.sh`.

### Next
1. Fix the NACK in i2c-mch (without it any sensor driver that tries several addresses will hang the bus).
2. Slot 2: what the device at 0x37 is (read more registers; possibly needs its own power/MCLK order).
3. M1: OV8856 — the mainline driver (`ov8856.c`, needs XVCLK 19.2 MHz — we have 24 MHz, a patch of the tables/checks is needed) or a port of the vendor tables.

## 2026-11-13 12:40: ✅ M1 (OV8856) — the mainline ovti,ov8856 driver bound and identified the sensor
* DT (`gen-camera-dtsi.py`, section M1): `camera@10` on i2c7, `clock-frequency = 24 MHz` (CAMTG5 ← univpll_192m_d8), `reset-gpios = <&pio 197 ACTIVE_LOW>`,
  `dovdd` = vcamio, `avdd` = regulator-fixed on GPIO195, `dvdd` = regulator-fixed on GPIO115 (PDN) on top of avdd, MCLK pin `cam4_mclk_on` (CMMCLK4, 4 mA),
  endpoint: 2 lanes (`data-lanes = <1 2>`, at the vendor `SENSOR_MIPI_2_LANE`), `link-frequencies = <720000000 360000000>` (mainline lane_cfg_2).
  GPIO94 (MIPI mux) = gpio-hog "1" (MAIN3). For the front camera it will have to be switched (sub = 0).
* Modules: `ov8856/ov8856.ko` = mainline ov8856.c unchanged (disabled in the phone config); `cam-probe.ko tg_keep=1` keeps CAMTG1..5 at 24 MHz (which CAMTG goes to CMMCLK4 — not found out).
* Loading: `sudo sh ~/camera/m1-load.sh` (mch → cam-probe tg_keep → videodev/v4l2-fwnode/v4l2-async → ov8856).
* Result: `ov8856 7-0010` is bound, the only message is `external clock rate 24000000 is unsupported` (expected: the mainline PLL tables are for 19.2 MHz,
  at 24 MHz the internal frequencies will be ×1.25 — irrelevant until there is a receiver). probe in DT mode powers on and checks the chip ID → identification passed. After probe — runtime suspended, regulators off.
* No /dev/v4l-subdev* yet: the sensor is registered through v4l2-async and waits for the bridge notifier (SENINF/CAMSV = M2).
* Not done in M1: S5KGW1, S5K3T1, GC2375 (no mainline drivers), slot 2 (0x37, ID 0x7f3f), the MCH channel NACK jam, OV8856 tables for 24 MHz.

## 2026-11-13 13:20: ✅ NACK on the multi-channel I2C buses FIXED
* Cause: ACKERR/HS_NACKERR in the interrupt mask of the CHANNEL. The handler clears ACKERR in the middle of the hardware handling the NACK → the channel gets stuck (START=0x3).
  The vendor does not include these bits in the channel mask (the status is set anyway, completion is by TRANSAC_COMP). Now `mch_ackmask` defaults to 0, recovery after a NACK - only FIFO_ADDR_CLR|MCH.
* Verified: 3 buses × 3 NACKs in a row, then a full scan of 0x08..0x77 on all slots — without a single timeout.
* Rejected hypotheses (for the record): DMA (a reset helped to clear dma_en, but not the channel), auto-restart, soft reset/handshake reset of the channel, RESUME_ARBIT into the channel/channel 0, START=0.

### Map of the camera buses (scan with a slot powered)
| Bus | Slot | ACK |
|---|---|---|
| i2c@11009000 | 0 MAIN | 0x0c (VCM), 0x2d (S5KGW1), 0x51 (EEPROM) |
| i2c@11011000 | 1 SUB / 3 SUB2 | 0x0c, 0x2d (S5K3T1), 0x17 (GC2375), 0x50, 0x51 (EEPROM) |
| i2c@1101a000 | 2 MAIN2 | 0x37 (?), 0x54 (EEPROM) |
| i2c@1101a000 | 4 MAIN3 | 0x10 (OV8856), 0x21 (?), 0x54 |
Slot 2 (0x37): all registers 0x00..0xef = 0, `f0..fb = 7f 3f 7f 00 00 ff 1f 00 7f ff 00 6f` — looks like a GalaxyCore in reset (masks instead of values). Needs a different power sequence — postponed.

## 2026-11-13 13:30: M2 — reconnaissance: SENINF is programmed in the closed HAL
* The vendor kernel `imgsensor/.../seninf.c` — only mmap of the registers (SENINF, MIPI_RX, GPIO) + clocks/ioctl. There is NO SENINF sequence in the kernel.
  CAMSV: `cameraisp/src/mt6785/inc/cam_regs.h` has only some of the registers (MODULE_EN 0x510, INT_EN 0x518, INT_STATUS 0x51c, SW_CTL 0x520,
  FBC_IMGO_CTL1/2 0x110/0x114, IMGO_BASE_ADDR 0x20, TG_SEN_MODE 0x230, TG_VF_CON 0x234, TG_INTER_ST 0x26c, TG_TIME_STAMP 0x2a0); it is also programmed by the HAL.
* The HAL lives in the phone vendor partition (`/dev/disk/by-partlabel/vendor`, mount ro at /mnt/vend): `lib64/libcam.halsensor.so` (SeninfDrvImp),
  `libcamdrv_isp.so`, `libcam.iopipe.so`. Copies for analysis — in the session scratchpad (do NOT distribute, proprietary).
* The symbols are exported: `SeninfDrvImp::hal_csirx_phyA_init/phyA_setting/dphy_init/dphy_setting/seninf_top_setting/seninf_csi2_setting/seninf_setting`,
  `setSeninfTopMuxCtrl`, `setSeninfCamMuxSrc`, `enableCamMUX`, `setSeninfMuxCtrl`, `setSeninfMuxCrop`, `setTestModel` (!), `configMclk`.
  The "SENINF + CAM_MUX + CDPHY" architecture is the MT6873/MT6853 generation → compare with the open MediaTek V4L2 driver `mtk_cam-seninf` (kernel-4.19/5.10, register headers with bit fields).
* First offsets (llvm-objdump): csi2_setting writes +0x4, +0x8, +0x10, +0xc0 of its block; phyA_init: +0x4..+0x3c (0x18/0x1c/0x20 — once per lane).
* Upstream: the BayLibre series "Add Mediatek ISP3.0" (seninf + camsv for MT8365, v8 01.2025) — not in 6.16; a different generation, but it is fine as a V4L2/media-graph skeleton.
* `setTestModel` in the HAL = the built-in SENINF test pattern generator → CAMSV/DMA can be checked with no sensor at all.

### M2 plan
1. Reverse `libcam.halsensor.so`: which pointer goes to which block (mmap offsets from `init()`), then phyA/dphy/csi2/top/mux field by field. Compare with `mtk_cam-seninf` (MT6873).
2. Reverse CAMSV (`libcamdrv_isp.so` + cam_regs.h): TG, IMGO DMA, FBC, interrupts.
3. Driver skeleton (after the ISP3.0 model: a seninf subdev + a camsv video node), DT nodes (seninf @0x1a040000, mipi_rx_ana @0x11c80000.., camsv @0x1a050000..), IOMMU/LARB.
4. First frame: SENINF test pattern → CAMSV → memory; then OV8856 (2 lanes, CSI0?).

## 2026-11-13 ~14:00: the MT6785 SENINF = SENINF "phy_2_0" from the upstream MediaTek ISP7 series (confirmed)
* HAL `SeninfDrvImp::init()`: mmap of SENINF @0x1A040000 and MIPI RX @0x11C80000 (from `seninf_cfg.h`), sub-block pointers from the table `.rodata 0xe100..0xe2f8`:
  - ctrl `+0x200+0x1000·i` (i=0..4), csi2 `+0xa00+0x1000·i`, mux `+0xd00+0x1000·i` (i=0..5), cam_mux `+0x400`, more blocks `+0x600+0x1000·i` (ISP7 does not have them; tg in ISP7 is `+0xf00`);
  - MIPI: port k (CSI0/1/2) every 0x4000: ANA-A `+0`, ANA-B `+0x1000`, DPHY top `+0x2000`, CPHY top `+0x3000`.
  - EXACTLY like `mtk_cam_seninf_init_iomem` in ISP7 `mtk_csi_phy_2_0`.
* The HAL function names = the ISP7 names (`hal_csirx_phyA_init` ↔ `csirx_phyA_init`, `..._seninf_csi2_setting`, `..._dphy_setting`, ...).
  `hal_csirx_seninf_csi2_setting` is bit-for-bit = ISP7 (RESYNC_MERGE_CTRL|=0x100, HDR_MODE_0, OPT.CPHY_SEL, CSI2_EN=lane mask), plus the HAL sets `IRQ_EN |= 0x80000000`.
* Source: the series "Add MediaTek ISP7 camera system driver", v1 patch 02/10 (Shu-hsiang Yang, 2024-10-09) — taken from lkml.iu.edu
  (lore/patchwork are behind an anti-bot wall), laid out in `ref/isp7-v1/src/` (hw_phy_2_0.c + 10 register headers with bit fields).
* SENINF plan: skeleton — ISP7 hw_phy_2_0 (cut the ISP7-specific parts: cphy, deskew, multi-exposure), check each function against the HAL (llvm-objdump), differences — in comments.

## 2026-11-13 ~14:15: the MT6785 CAMSV ≈ the ISP3.0 CAMSV (MT8365, BayLibre v8) with shifted blocks
* Instances: `camsv1..6 @ 0x1a050000 + 0x1000·i`, IRQ GIC_SPI 279, 280, 281, 282, ... (vendor mt6785.dts).
* Inside an instance (cam_regs.h): IMGO DMA `+0x20` (BASE_ADDR), FBC `+0x110/+0x114` (FBC_IMGO_CTL1/2), TG `+0x220` (SEN_MODE +0x230, VF_CON +0x234, INTER_ST +0x26c, TIME_STAMP +0x2a0),
  top `+0x510` (MODULE_EN +0x510, INT_EN +0x518, INT_STATUS +0x51c, SW_CTL +0x520).
* ISP3.0: top `+0x0` (MODULE_EN/FMT_SEL/INT_EN/INT_STATUS/SW_CTL/IMGO_FBC/CLK_EN/PAK), TG: SEN_MODE `tg+0x10`, VF_CON +0x14, GRAB_PXL +0x18, GRAB_LIN +0x1c, PATH_CFG +0x20;
  IMGO: BASE_ADDR +0x0, XSIZE +0x8, YSIZE +0xc, STRIDE +0x10, CON +0x14, CON2 +0x18. The same blocks, a different layout + MT6785 has a separate FBC (CTL1/CTL2, ISP5 generation).
* The ISP3.0 v8 sources (seninf + camsv + mt8365 dtsi) are laid out in `ref/isp30/src/` (from lkml.iu.edu).
* Unknown: the exact offsets of FMT_SEL/CLK_EN/PAK/IMGO XSIZE/YSIZE/STRIDE/CON on MT6785 → reverse `libcamdrv_isp.so` (camsv registers) or the vendor camera_isp.c.

### M2 plan (refined)
1. SENINF: a driver subset of ISP7 phy_2_0 (one lane CSI0? — find the OV8856 port from the vendor DT/HAL: `getCSIInfo`), SENINF test pattern (`setTestModel` ↔ ISP7 test model) → CAM_MUX → CAMSV.
2. CAMSV: the ISP3.0 camsv skeleton (videobuf2 + video node) with the MT6785 layout; IOMMU: CAMSV ports in M4U (larb6/7?) — check in the mainline DT.
3. CAM power domain + camsys clocks (CLK_CAM_*), SENINF clocks (CLK_TOP_SENINF*).
4. First frame: test pattern → /dev/videoN → raw dump.

## 2026-11-13 19:50: ✅ M2 step 1 — the SENINF test model WORKS (CAM domain + clocks + layout confirmed)
* Module `seninf/seninf-tm.ko`, DT node `seninf-tm@1a040000` (window 0x6000, domains cam+conn, clocks CLK_CAM_MAIN/GALS/TG/SENINF + CLK_TOP_SENINF0..2).
* `echo 1 > /sys/kernel/debug/seninf-tm/power`: the CAM domain came up (0), the registers are readable, top-seninf0 = 312 MHz. No hangs.
* Before start: TOP mux0 0x03020100 (default mux i ← seninf i), CAM_MUX ctrl0 0x03020100, EN 0x7ff, CHK_EN 0x7ff.
* `echo > start` (the vendor setTestModel sequence, TG at +0x600):
  - SENINF0 ctrl=1, testmdl=1, TG ctl 0x00300161, size 0x10001f00, clk 7;
  - MUX0: ctrl0=1, **SIZE = 0x10001f00** (4096 lines × 7936 pix. — exactly the requested frame), PIX_CNT 0x01f01f00, FRAME_SIZE_MON H_VALID 7935, irq 0x0c, fifo 0x0020→0x0036;
  - CAM_MUX0: **CHK_RES = 0x10007c00** (4096 lines, width 0x7c00), err 0x10000000 (the expected size for the check is not set), irq 0x2.
* Conclusion: SENINF → MUX → CAM_MUX push frames. Next: CAMSV (receive from CAM_MUX0 → IMGO DMA into memory), IOMMU/LARB.

## 2026-11-13 ~20:00: the MT6785 CAMSV — register layout from the HAL (`/vendor/lib64/libimageio_plat_drv.so`)
HAL classes (symbols exported): `CAMSV_TOP_CTRL::_config/_enable`, `CAMSV_TG_CTRL::_config/_enable`, `DMAO_CAMSV_B::_config/_enable/setBaseAddr`,
`CAMSV_BUF_CTRL` (FBC). The registers are written through `IspDrvCamsv::writeReg(offset, value)` → the offsets are visible as constants in `w1`. The instance window is 0x1000 (camsvN @0x1a050000+0x1000·N).
* **TOP**: `SW_CTL 0x520` — reset: 4 → 0 → 1, wait for ==3, then 0 (the same in the vendor kernel `camera_isp.c`).
  `MODULE_EN 0x510`: bit0 (TG?) in TOP::_config, **bit4 = IMGO DMA** (DMAO::_enable). `0x514` (FMT_SEL?), `0x54c` — written in DMAO::_config (decode the values).
  `0x530` (_enable): |=0x1, |=0x4, |=0x8000 (probably CLK_EN / DCM). `0x400` |= 1 in DMAO::_enable (FBC/DMA enable?).
* **TG** (+0x220): `SEN_MODE 0x230`: bit0 CMOS_EN (TG::_enable), bit1, bit13 (0x2000), bits16..18 (0x10000/0x20000/0x40000);
  `VF_CON 0x234`: bit1 (and bit0 = VFDATA_EN per cam_regs/the kernel); `SEN_GRAB_PXL 0x238`, `SEN_GRAB_LIN 0x23c` (start|end<<16, like ISP3.0); `0x2a4` = v | v<<8.
* **IMGO DMA** (+0x20, the ISP5 DMA layout): `BASE_ADDR 0x20`, `OFST_ADDR 0x28`, `XSIZE 0x30`, `YSIZE 0x34`, `STRIDE 0x38` (bits 24+ — bus size/format, `|0x1000000`),
  `CON 0x3c` (=0x80000080-like, the HAL writes 0x80…), `CON2 0x40` = 0x20002, `CON3 0x44` = 0x20002, `0x48` = (bandwidth computation, ultra/pre-ultra).
* **FBC**: `FBC_IMGO_CTL1 0x110`, `CTL2 0x114` (cam_regs.h). INT_EN 0x518, INT_STATUS 0x51c.
* Next: fill in the values (full decoding of TG/DMAO/BUF_CTRL `_config`), module `camsv-tm`: SENINF test model → CAM_MUX0 → CAMSV0, a CMA buffer (physical address, no IOMMU), one frame → check the contents.

## 2026-11-13 ~20:10: CAMSV0 — frames arrive, DMA does not write yet (no IOMMU)
* Module `camsv/camsv-tm.ko` (a debugfs "remote control": power / buf / w / r / bufdump), DT `camsv-tm@1a050000` (windows SENINF, CAMSV0, camsys without request_mem_region; clocks + CAM_SV0/SV1/LARB6/LARB7; larb6 runtime PM).
  Script `camsv/cap1.sh` (test model on all 5 SENINFs, 640×480 → CAMSV0).
* CAMSV0 after reset: GRAB_PXL 0x7fff0000, GRAB_LIN 0x3fff0000, MODULE_EN 0x40000000, FMT_SEL 0x100, IMGO_CON 0x80000080, 0x53c = 0x20150120 (version?).
* HAL `convertTgEnum2CamMux(tg) = tg − 14` (tg ≥ 16) → CAMSV0 = **cam mux 2** (source mux2). CHK_RES of cam mux 2 = 0x01e00a00 (480 lines) — the frame arrives.
* ISP3.0 → MT6785 layout (top +0x510): MODULE_EN 0x510 (TG 0, PAK_EN 2, PAK_SEL 3, IMGO 4, DB 30), FMT_SEL 0x514 (TG1_FMT [2:0]: RAW10=1), INT_EN 0x518, INT_STATUS 0x51c,
  SW_CTL 0x520, CLK_EN 0x530 (TG 0, PAK 2, DMA 15 → 0x8005), PAK 0x54c (RAW10=2). HAL INT_EN = 0x001304b1. FBC_IMGO_CTL1 0x110: _config |=0x10000, _enable FBC_EN 0x8000 + 0x400000 (+FB_NUM in [31:24]?), enqueue |=1 (RCNT_INC?).
* Run: INT_STATUS = 0x0001008f (VS, TG_ST, EXPDON, SOF + **IMGO_ERR bit16**); after FBC 0x00418001 → 0x0001009f (+TG_ERR bit4), FBC_CTL2 = 0, the buffer is empty. No IOMMU fault in dmesg.
* Hypotheses: TG does not accept the format/size (decode CAMSV_TG_CTRL::_config completely: SEN_MODE bits 1/13/16-18, VF_CON bit1, 0x2a4; TG pixel mode vs SENINF mux 0x001f0001),
  FBC_NUM [31:24], INT_STATUS is not W1C (possibly clear-on-read). Next step — a full decoding of TG/DMAO/BUF_CTRL _config from the object field values.

## 2026-11-13 ~20:30: CAMSV0 — TG and PASS1 work, the IMGO DMA does not write
* TG_CTRL::_config (HAL): SEN_MODE |= 0x10000 (bit16) always; field [0x44] (subsampling) → SEN_MODE bits17/18 + 0x2a4 = v|v<<8; field [0x40] → VF_CON bit1;
  field [0x68] (pixel mode 1/2/3) → SEN_MODE bit1/bit13; GRAB_PXL = x0 | x1<<16 ([0x48]|[0x58]<<16), GRAB_LIN = y0 | y1<<16 ([0x4c]|[0x60]<<16).
* With SEN_MODE = 0x10001, without FBC: INT_STATUS = 0x0009048f — VS, TG_ST, EXPDON, SOF, **PASS1_DON (bit10)**, but **IMGO_ERR (bit16) + bit19**. With FBC 0x418001: 0x0001009f (TG_ERR).
* Dump after the run: **TG_FRMSIZE 0x268 = 0x028001e0 (640×480)** — the frame was received correctly. FBC_CTL2 0x114 = 0x60000000; 0x000 = 0x0007007e;
  0x360 = 0xffff8288; 0x3b4/0x3bc/0x3c4 = 0xffffffff (look like DMA error/status registers); the buffer (CMA 0xfe400000, no IOMMU) is empty; no IOMMU fault in dmesg.
* Hypotheses: (1) the CAMSV DMA goes through larb7 (not in the mainline DT, not initialized) — check the larb6/larb7 ports (MMU/NONSEC registers), enable larb7 by hand;
  (2) missing DMA settings from the HAL (0x48 ultra/pre-ultra, 0x000.., 0x400), decode DMAO_CAMSV_B::_config by values; (3) registers 0x3b4.. — "DMA_ERR_CTRL": decode.

## 2026-11-13 ~20:15–20:40: ✅ FIRST CAMSV DMA WRITE INTO MEMORY (once), then it does not reproduce
* **Cause of IMGO_ERR found:** all ports of larb6 (0..30) and larb7 (0..4) are in `MMU_EN=1` (SMI_LARB_NONSEC_CON 0x380+4·port, bit 0) → the CMA physical address
  is treated as an IOVA. Clearing the bit (`camsv/larb-bypass.py`, /dev/mem) → **the DMA wrote 124800 non-zero bytes** (ff ff ff… from address 0), INT_STATUS 0x0001008f, IMGO_ERR gone.
  No IOMMU fault was printed in dmesg at that (!).
* Later (after rmmod/insmod adding `frame` to debugfs and restarts of cap1.sh) it does not reproduce: INT_STATUS 0x00030097 (TG_ERR bit4 + bits16/17),
  **TG_FRMSIZE 0x268 = 0x02800000 — 640 pixels, 0 lines** (in the successful run 0x028001e0), TG_INTER_ST 0x105 (it was 0x..207).
  Did not help: stopping TG/the module before the reset, a full power cycle, damping the test model TM_DUM=0x00640064.
* Difference of the successful run: CAMSV was configured on top of a long-running test model after several previous runs (without a power cycle); FBC=0; SEN_MODE 0x10001.
* Hypotheses for the next session: the start order (SENINF TM first → wait for frames → then CAMSV TG), re-initializing SENINF mux2/cam mux2,
  TG pixel mode vs SENINF (mux ctrl1 bits[9:8]), TG_ERR config (VF_CON bit1 / SEN_MODE bit1), compare ALL CAMSV/SENINF registers with the successful state.
* Tools: `camsv/cap1.sh` (variables W/L/FMT/PAK/MEN/SEN/FBC/TMDUM), `camsv/raw2png.py` (RAW10 → PNG), `debugfs camsv-tm/frame` (the whole buffer).

## 2026-11-13 20:20: ✅✅ FIRST FRAME IN MEMORY: SENINF test pattern → CAMSV0 → IMGO DMA (no IOMMU)
* After a reboot it reproduces reliably, incl. after a power cycle of the CAM domain (the earlier "TG 0 lines" is a consequence of rmmod/insmod of camsv-tm in that boot; do not unload the module, reboot instead).
* Order: `insmod camsv-tm.ko` → `sh cap1.sh` (power, larb-bypass.py, test model on 5 SENINFs, CAMSV0 reset/setup, FBC=0, SEN_MODE 0x10001, MODULE_EN 0x40000015,
  FMT_SEL 0x101, PAK 2, STRIDE 0x01000320, CON2/3 0x20002, INT_EN all, 0x400=1) → `cp /sys/kernel/debug/camsv-tm/frame` → `raw2png.py frame 640 480 800 out.png`.
* Result: INT_STATUS 0x0001008f, TG_FRMSIZE 0x028001e0 (640×480), buffer: 124800 non-zero bytes. The picture `camsv/frames/tm-640x480.png`: vertical stripes/hatching
  of the test pattern (TM_CTL 0x00300161) in **lines 0..239**, lines 240..479 = 0 → to figure out: DMA YSIZE/pixel mode, or that is how the pattern looks.
* Register dumps (successful state): `camsv/frames/d0-boot.txt` (right after power on), `d1-run.txt` (after the capture), `d2-cycle.txt` (after a power cycle).
* Next: (1) half of the frame; (2) the real OV8856 sensor over MIPI (CSI port, ANA/DPHY PHY per ISP7 phy_2_0, vendor csi settings); (3) IOMMU instead of larb-bypass
  (the CAMSV port on larb6/7 — find it by enabling MMU_EN port by port); (4) a V4L2 driver.

## 2026-11-13 ~20:50: CAMSV — the DMA writes EXACTLY HALF of the frame
* Marker test (`FILL=0xa5`, debugfs `fill`): exactly 50% of the frame bytes are written at any size (640×200→80000, 640×120→48000, 960×240→144000, 640×300→120000,
  640×480→192000). TG_FRMSIZE (0x268) is full at that (640×480). It does not depend on YSIZE (YS=959 → the same), the test model speed (TM_CLK 7/1f/3f/ff), FBC.
* Without FBC the DMA writes only the FIRST frame (torn — CAMSV turns on in the middle of a frame). With FBC: `FBC_IMGO_CTL1 = 0x8000` (FBC_EN), then `|= 1` (RCNT_INC) — the next frame is written.
  FBC with bits 16/22 (0x418000/0x18000, like in the HAL) — does not write at all. FBC_CTL2 [31:24] — the frame counter.
* Test model patterns (TM_CTL[8:4]): 0x16 (00300161), 0x2 (00300021), 0x4 (00300041) — vertical stripes/hatching (X only), 0xC — black.
  Pictures: `camsv/frames/f-*.png`, `tm-640x480.png`.
* INT_STATUS stays 0x0009048f: PASS1_DON + **IMGO_ERR (16) + bit19** — the DMA aborts the frame on an error in the middle.
* The XSIZE=0x18f/0x31f/0x63f experiment gave a garbage result (1 byte per line, TG_ERR) — the state got corrupted, re-check after a reboot.
* Hypotheses: a bytes/line mismatch between PAK and DMA (DBL/pixel mode), IMGO_CON/CON2 (FIFO/burst), 0x48 (ultra), decode bit 19 and the registers 0x3b4… (0xffffffff),
  compare with the complete DMAO_CAMSV_B::_config sequence (CON/CON2/CON3/0x48 values, order).

## 2026-11-13 ~21:10: ✅✅ FULL 640×480 FRAME of the test pattern in memory
* Cause of "half a frame": **bit 24 in IMGO STRIDE (0x38)** — a bus flag, the HAL sets it only in one branch (tbnz on the format). With it the DMA packs 2 lines into one.
  `STRIDE = BPL` (no flags) → 384000 bytes written = the whole RAW10 640×480 frame. The picture `camsv/frames/f-stf0.png`: a horizontal gradient hatching (pattern 0x16) over the full height.
* Working sequence (cap1.sh, defaults): larb-bypass → test model → CAMSV0 reset → TG window, IMGO BASE/XSIZE=BPL-1/YSIZE=L-1/STRIDE=BPL, CON2/3 0x20002,
  FMT_SEL 0x101, PAK 2, CLK_EN 0x8005, MODULE_EN 0x40000015, INT_EN all, 0x400=1, VF_CON 1, SEN_MODE 0x10001; then FBC_CTL1 = 0x8000 and `|=1` (RCNT_INC) → one full frame.
* Remaining: TG_ERR (bit4) in INT_STATUS with STRIDE without bit 24 (0x9049f) — to figure out; IMGO_ERR bit16 + bit19 also hang around.
* Verified: pixel mode 1 (DBL → TG sees 1280), PAK DBL is not needed.

## 2026-11-13 ~21:00–22:00: M2 → the real OV8856 over MIPI CSI2 — ✅ THE DATA REACHES CAMSV
Script: `ov8856-mipi/ov-cap.py` (already with all the fixes below). Debug scripts: `ov8856-mipi/dbg/`
(`sr.py`/`sw.py` — read/write sensor registers over i2c-7; `pon.py` — phyA_power_on; `settle.py`, `lanes.py` — sweeps;
`anadbg.py`/`dphydbg.py` — ANA/DPHY debug muxes; `mem.py` — /dev/mem peek/poke; `smp.py` — register statistics).
`ref/hal-dis.sh` — disassembler of a HAL function by name (llvm-nm/objdump) + hal-lift.

### What was wrong (in the order of discovery)
1. **THE MAIN THING: the MIPI-CSI 26 MHz references in APMIXEDSYS are off.** `0x1000C020` (AP_PLL_CON8-like, inverted gate, 1 = on):
   bit 6 = `apmixed_mipic0_26m`, bit 11 = `apmixed_mipic1_26m` (bits 16/17 — mipid0/1 for DSI). After boot it was `0x20`.
   Without them: ANA +0x48 = 0 (calibration fails), the DPHY FSM sees only LP transitions, zero packets and zero errors in ALL CSI2 blocks.
   Wrote `0x860` → ANA +0x48 = 0x..3f, CSI2 IRQ 0x324, PKT_CNT 0x01e101e1 (481), MUX2 SIZE 0x01e00280 (640×480), CAMSV TG_FRMSIZE 0x028001e1.
   → For the driver: DT needs the clocks `apmixed_mipic0_26m`/`mipic1_26m` (mainline clk-mt6785 does not have them — add the gates to apmixedsys).
2. **The CSI2 port goes to SENINF4, not SENINF2.** HAL `getCSIInfo` (table {port, seninf, ?}): (0,0) (1,2) (2,4) (3,0) (4,1) (5,2) (6,3).
   I.e. CSI2 → csi2 block `0x4a00`, ctrl `0x4200`. Confirmed by the base table in `SeninfDrvImp::init()` (obj+0x140.. = 0xa00,0x1a00,0x2a00,0x3a00,0x4a00).
3. **TOP_MUX_CTRL (TOP+0x10/0x14)** = the source for mux i (a nibble per byte: mux0 [3:0], mux1 [11:8], mux2 [19:16], mux3 [27:24], mux4/5 in +0x14).
   By default mux i ← seninf i (0x03020100 / 0x504). For OV8856: mux2 ← seninf4 → TOP+0x10 = 0x03040100. (TOP+0x8 = 0x32100000 — do not touch, meaning unknown.)
4. Missed HAL writes: `setSeninfCsiMipi` between dphy_init and csi2_setting: **CPHY top of the port (CSI2: ANA 0xb000) +0x18 |= 0x10**;
   end of `dphy_setting`: **DPHY +0x4 |= 0x80000000** (CK_DATA_MUX_EN). By themselves (without item 1) they did not help.
5. The MIPI switch GPIO94 (`cam_mipi_switch_sel`): for MAIN3 (OV8856) = HIGH, the hog in DT is already set like that (gpio-606 out hi). For SUB (front S5K3T1) — LOW.

### MIPI RX map (confirmed by HAL init + a dump)
ANA = `0x11c80000`: CSI0A +0x0000, CSI0B +0x1000, DPHY0 +0x2000, CPHY0 +0x3000; CSI1A +0x4000, CSI1B +0x5000, DPHY1 +0x6000, CPHY1 +0x7000;
CSI2A +0x8000, CSI2B +0x9000, DPHY2 +0xa000, CPHY2 +0xb000. (The names `mipi_rx_ana_csi2a@11c84000` in the vendor DT are old/wrong.)
ANA +0x0: bit0 CPHY_EN, bits 2/3 LPF/CORE (power_on), CKMODE L0/L1/L2 = 5/8/11, CKSEL = 6/9/12 (like ISP7 4D1C: clock on A.L2).
ANA +0x48 — calibration/LPRX status (the HAL waits for &0x29). ANA debug: +0x80[15:8] = select, +0x84 = value. DPHY: +0x280 select, +0xe0[7:0], +0x288.
DPHY +0x30/+0x34 — FSM of the clock/data lanes (0x01 idle, 0x10 HS). CSI2 (ISP7 layout): IRQ_EN 0xc0, IRQ_STATUS 0xc8, LINE_FRAME 0xd0,
PACKET_STATUS 0xd4, SHORT 0xd8, PACKET_CNT 0xdc, DBG_CTRL 0xe0 (enable the counters).

### Sensor
OV8856 streams after init.regs + vga.regs: 0x0100=1, 0x3018=0x32 (2 lanes), 0x3808..0x380b = 640×480, 0x4800=0x44.
Switching 0x0100 0→1 after the PHY setup — DPHY sees HS right away.

### Where we stopped
* After enabling APMIXED everything up to the CAMSV TG works (TG sees 640×481), but **the frame was not written to memory**: INT 0x0009048f
  (IMGO_ERR bit16 + bit19), FBC_CTL2 = 0xf4000001, the buffer is all 0xa5. CAMSV was set up BEFORE the data started (ov-cap.py step 4),
  possibly the DMA got stuck in an error → next time: run the whole `ov-cap.py` after a fresh reboot (APMIXED is already in the script),
  if needed reset CAMSV (SW_CTL 4,0,1,0) after the stream starts, then FBC RCNT_INC. Check TG GRAB_LIN (481 lines from the sensor vs 480).
* IOMMU instead of larb-bypass, a V4L2 driver, the front camera (switch LOW), slot2 0x37 — not started.
