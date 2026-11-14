<!-- Excerpt from the chronological log docs/NOTES-chronological.md (split by topic). -->
<!-- References like wifi/..., audio/..., pmos-begonia/... in the text refer to the old layout of the working directory; see README.md at the root of port/. -->

# begonia (MT6785) WiFi — working notes

Goal: bring up WiFi (CONNSYS) on pmOS / the mainline 6.16 kernel. It works on Android, not in mainline.

## What we have
- `connsys-fw/` — firmware from vendor.img (Project Infinity-X 3.12): `WIFI_RAM_CODE_soc1_0_2a_1.bin`,
  `soc1_0_{patch_mcu,ram_mcu,ram_wifi,ram_bt}_2a_1_hdr.bin`, `wifi.cfg`, `WMT_SOC.cfg`, `txpowerctrl.cfg`, `BT_FW.cfg`.
  Generation `soc1_0` => CONNSYS is built into the SoC (not the external MT6631; that one is apparently only FM/analog).
- `kernel-r-oss/` — `MiCode/Xiaomi_Kernel_OpenSource`, branch `begonia-r-oss`. The working tree is NOT checked out
  (the checkout failed on Windows). Read via `git show HEAD:path` / `git grep ... HEAD`, or a sparse checkout.
- On the phone (`user@172.16.42.1`, key `~/.ssh/id_ed25519_claude`): systemd, kernel 6.16.4, `cfg80211`/`mac80211` = modules,
  no CONNSYS node in DT, no firmware, `/sys/class/ieee80211` is missing.

## What the Xiaomi OSS kernel does NOT have
`drivers/misc/mediatek/connectivity/` — only the `connadp` stub (Makefile: "Do Nothing, move to standalone repo").
The actual `conninfra` + `wlan/gen4m` + `common/wmt` live in a separate repository
(`vendor/mediatek/kernel_modules/connectivity`) — it has to be found separately.

## CONNSYS hardware from the vendor DT (mt6785.dts, xiaomi-mt6785-common.dtsi)
`consys@18002000`, compatible `mediatek,mt6785-consys`, clocks: `SCP_SYS_CONN` ("conn"), `INFRACFG_AO_CCIF4_AP_CG` ("ccif").
reg:
| name | address | size |
|---|---|---|
| CONN_MCU_CONFIG_BASE | 0x18002000 | 0x1000 |
| TOP_RGU_BASE | 0x10007000 | 0x100 |
| INFRACFG_AO_BASE | 0x10001000 | 0x1000 |
| SPM_BASE | 0x10006000 | 0x1000 |
| CONN_HIF_ON_BASE | 0x18007000 | 0x1000 |
| CONN_TOP_MISC_OFF_BASE | 0x180b1000 | 0x1000 |
| CONN_MCU_CFG_ON_BASE | 0x180a3000 | 0x1000 |
| CONN_MCU_CIRQ_BASE | 0x180a5000 | 0x800 |
| CONN_TOP_MISC_ON_BASE | 0x180c1000 | 0x1000 |
| CONN_HIF_PDMA_BASE | 0x18004000 | 0x1000 |
| AP2MD_PCCIF4_BASE | 0x1024c000 | 0x40 |
| INFRA_AO_PERICFG_BASE | 0x10003000 | 0x1000 |

IRQ: BGF_EINT = SPI 321 (LEVEL_LOW), WDT_EINT = SPI 78 (LEVEL_LOW), conn2ap_sw_irq = SPI 323 (EDGE_RISING).
`wifi@18000000` (`mediatek,wifi`): reg 0x18000000/0x100000, IRQ SPI 322 (LEVEL_LOW), memory-region = wifi_mem.
Reserved memory (alloc-ranges 0x40000000..0xC0000000, align 16M, no-map):
- `consys-reserve-memory` (`mediatek,consys-reserve-memory`): 0x400000
- `wifi-reserve-memory` (`shared-dma-pool`): 0x300000
Pins: `consys_pins_default` (empty), GPS LNA = GPIO74 (init low / oh / ol).
Also `conn_peri_base = <0x18820000 ...>` (mt6785.dts:1671).

## Next steps
1. Find the standalone connectivity repository (conninfra, gen4m, wmt) for mt6785/soc1_0.
2. Compare the DT/iomem/reserved memory of the mainline kernel with the vendor DT (`sudo cat /proc/iomem`, `dmesg` from the phone).
3. Add consys/wifi + reserved-memory to the mainline DTS, check that the `wmt_loader` chain starts.
4. Port to 6.16 (cfg80211 API), MCU loading from the firmware.

## FOUND: the standalone driver repository
`mtk-modules-r-oss/` = `github.com/MiCode/MTK_kernel_modules`, branch `begonia-r-oss` (sparse checkout: `connectivity/common`,
`connectivity/wlan/core/gen4m`, `connectivity/wlan/adaptor`; read the rest via `git show HEAD:path`).

mt6785 uses the OLD WMT scheme, NOT conninfra (conninfra in the tree is only for mt6885):
- `connectivity/common/common_main/platform/mt6785.c` (+ `include/mt6785.h`, `PLATFORM_SOC_CHIP 0x6785`) — CONSYS power/clk/EMI/start
- `connectivity/common/common_main/platform/mtk_wcn_consys_hw.c:132` — `of_match`: `mediatek,mt6785-consys` (matches the vendor DT)
- `connectivity/common/common_main/core/wmt_core.c:1091` — `case 0x6785`
- `connectivity/wlan/core/gen4m/` — the WiFi driver itself; platform: `os/linux/plat/mt6785/plat_priv.c`
- build-in adapter: `connadp` in the Xiaomi kernel (`connectivity_build_in_adapter.c`, `wmt_build_in_adapter.c`) — hooks into the kernel (EMI, clkbuf, PMIC),
  in mainline they will have to be replaced/cut out.

Size of the task: common_main (WMT/STP/PSM) + gen4m (~1000+ files). Dependencies on the MediaTek 4.14 kernel API (mt-plat, clkbuf, mt_emi_api, memblock).

## [OUTDATED — see "CORRECTION" below] Analysis of the dumps from the phone (`phone/dmesg.txt`, `phone/iomem.txt`), pmOS 6.16.4
What the mainline kernel does NOT have (all bases needed by CONSYS are missing from /proc/iomem):
- 0x18000000..0x180fffff (CONN_*, wifi@18000000) — nobody occupies it, no conflicts
- 0x10001000 INFRACFG_AO, 0x10003000 PERICFG_AO, 0x10006000 SPM, 0x1024c000 PCCIF4 — not described at all
- 0x10007000 TOP_RGU — taken by `watchdog@10007000` (overlaps consys reg[1]; needs a shared/syscon, not an exclusive mmap)
- no scpsys/power domain `SCP_SYS_CONN` (there is only "genpd: Disabling unused power domains") and `INFRACFG_AO_CCIF4_AP_CG`
- no reserved-memory for consys (0x400000) and wifi (0x300000)
- the MT6359 PMIC regulators (vcn18, vcn33_1_wifi, vcn33_2_bt, vcn28) do not come up: only mt6359-rtc/keys via pwrap
- clkbuf (26 MHz, `mtk_clkbuf_ctl.h`) does not exist in mainline
What exists: GIC, pwrap+mt6359 (rtc/keys), topckgen (10000000), pinctrl, UFS/MMC/USB/DSI. The modem (ccci) is not up, only reserved-mem mblock-*.

## Key conclusion from the vendor code (common_main/platform/mt6785.c)
There are NO SMC/SIP calls into ATF. Enabling CONSYS — direct register writes (~190 accesses CONSYS_REG_*/SPM_*/INFRA*) +
regulators (vcn18, vcn33_1_wifi, vcn33_2_bt, vcn28) + clkbuf + EMI MPU/remap (`consys_emi_*`) + devapc-cb.
=> the task is doable with an ordinary driver, without reversing ATF secrets.

## Plan (updated)
1. mainline: bring up infracfg_ao + scpsys(SPM) with the CONN domain; mt6359 regulators; clkbuf. Check what mt6785-mainline GitLab already has (maybe part of it is done).
2. DT: consys@18002000, wifi@18000000, reserved-memory (consys 4M, wifi 3M), pins (GPIO74 gps_lna).
3. A new minimal "consys-power" driver: port the sequence from mt6785.c (power on → rom patch → MCU start), check `polling_consys_chipid`.
4. Only then gen4m (WMT/STP → wlan) on 6.16.

## CORRECTION (after going through mt6785-mainline, branch 6.16, `mt6785-linux-6.16/`, commit 203a993)
The conclusion "mainline has no infracfg/SPM/regulators" was a MISTAKE: /proc/iomem does not show syscon/clk regions (of_iomap without request_mem_region).
In reality the phone has: `10001000.clock-controller` (infracfg), `10003000.syscon` (pericfg), `10006000.syscon` (SPM),
the drivers `mtk-scpsys`/`mtk-power-controller`. Mainline files for comparison: `phone/mt6785.dtsi.mainline`, `phone/begonia.dts.mainline`.

Already in the mainline fork:
- the domain `MT6785_POWER_DOMAIN_CONN` in DT (spm) and in `drivers/pmdomain/mediatek/mt6785-pm-domains.h` (ctl 0x320, sta BIT(1), bus-protect TOPAXI BIT(13|18)/BIT(14), TOPAXI_1 BIT(10))
- the clock `CLK_INFRA_CCIF4_AP` (= vendor `clk_infracfg_ao_ccif4_ap_cg`) — `clk-mt6785-infra.c`
- MT6359 regulators on the phone (currently disabled): vcn18, vcn13, vcn33_1_bt, vcn33_1_wifi, vcn33_2_bt, vcn33_2_wifi, vrf12, vrf18, vrfck
- topckgen as syscon (needed for the EMI remap), pericfg as syscon, pwrap
- pinctrl, GIC

Vendor power-on (`mt6785.c: consys_hw_power_ctrl`, branch CONSYS_PWR_ON_OFF_API_AVAILABLE) = clk_prepare_enable(ccif4_ap_cg) + clk_prepare_enable(scp_conn_main = genpd CONN)
+ in pericfg (+ INFRASYS_COMMON_AP2MD_PCCIF4_AP_PERI_AP_CCU_CONFIG) set bit 1 (CON_PWR_ON). I.e. it is implemented through `power-domains`/`clocks`.

Real gaps (what has to be written):
1. **consys/wifi DT nodes** (reg map above) + reserved-memory (consys 4M, wifi 3M; vendor: align 16M, alloc 0x40000000..0xC0000000).
2. **XO_WCN clock buffer** (vendor `clkbuf-config = <2 1 1 2 0 0 1>` => buf2 (CONN) = SW_CONTROL, i.e. the AP must enable it itself). Enabling (`clk_buf_ctrl_internal(CLK_BUF_CONN, FORCE_ON)`):
   - pwrap+0x190 (`DCXO_ENABLE`): clear bit1 (`DCXO_CONN_ENABLE`)
   - MT6359 `DCXO_CW00` (PMIC reg 0x788): clear EXTBUF2_MODE (mask 0x3, shift 3), set EXTBUF2_EN_M (bit 5)
   Source file: `phone/clkbuf_hw_mt6785.c`. For mainline: through the pmic regmap + access to pwrap (the region is taken by the pwrap driver -> a syscon/helper is needed). Possibly the bootloader already left the buffer on — check by reading DCXO_CW00 on the phone (needs root: debugfs regmap).
3. **EMI**: write `gConEmiPhyBase` into topckgen+CONSYS_EMI_MAPPING_OFFSET (bits 16..20 — base>>?), PERISYS_MAPPING=0x10003000, MD mapping (optional, "MD direct path not supported" is acceptable). EMI-MPU region 27 (CONFIG_MTK_EMI) — optional, it is protection.
4. Bind the regulators vcn18/vcn13/vcn33_*/vrf* to consys (supplies in DT); there is no `vcn28` in MT6359 — check what the vendor `consys_hw_vcn28_ctrl` powers.
5. watchdog (0x10007000, TOP_RGU) overlaps consys reg[1] — take it through syscon/regmap.
6. Then: the WMT/STP core and gen4m (the main effort).

## module/ — the first experiment (CONSYS power-on -> chip id)   [built, NOT RUN ON THE PHONE]
Files: `module/consys-bringup.c` (platform driver), `consys-begonia.dtsi` (node + reserved-memory), `build.sh`, `Makefile`,
`phone-config` (= /boot/config of the phone), artifacts: `consys-bringup.ko`, `mt6785-xiaomi-begonia-consys.dtb`, `begonia-consys.dts.txt`.
Build: WSL, `~/mt6785-linux` (fork of mt6785-mainline branch 6.16, 6.16.4), `LLVM=1` (the phone kernel is built with clang 22), `bash build.sh`.
Verified: vermagic `6.16.4 SMP preempt mod_unload aarch64` == phone; all 38 external symbols are in the phone kallsyms;
DTB: all phandles resolved (power-domains CONN, clk 103=CCIF4_AP, vcn18/vcn33_1_wifi/vcn33_2_wifi, pericfg/topckgen/pwrap, memory-region).
Success criterion: `readl(0x180b1000+0x10) == 0x10020600` (CONSYS IP version), the line "RESULT: ... OK" in dmesg.
Module parameters: `stage=0` (snapshot, default), `stage=1` (power-on + chip id; then power-off unless `keep_on=1`).
IMPORTANT: the phone has no CONFIG_OF_OVERLAY/OF_DYNAMIC => the consys node will appear ONLY after flashing a boot.img with the new DTB.
Not ported (TODO after chip id): VCN18/VCN33 LP/HW modes (KERNEL_pmic_ldo_*_lp), vcn28_hw_mode, EMI remap (topckgen+0x380 = base>>16, 20 bits),
EMI MPU region 27, acr/afe, set_xo_osc_ctrl, identify_adie, ROM patch download.

## Boot image with the consys node (BUILT, NOT FLASHED)   [method — like in pmos-begonia: a DTB-only patch + repack_boot.py]
- Base = `pmos-begonia/touch-fix/bootwork/boot-vtp-touch-bat-prox.img` (on the phone: `proximity-near-level=0x226` == bat-prox; kernel/ramdisk are the same in all vtp images, only the DTB differs).
- DTS patch: `module/patch_dts_consys.py` (phandles are looked up by structure; new ones 0x1f4..0x1f8) -> `module/base/consys.dtb`
  (diff to the original: 0 removed lines, only additions; the phandles spm=0x55 infracfg=0x2a pericfg=0x46 topckgen=0x3 match the DTB from the fork).
- Repack: `python pmos-begonia/touch-fix/repack_boot.py <base.img> - <out.img> --dtb module/base/consys.dtb`
  -> `module/boot-vtp-touch-bat-prox-consys.img` (24866816 bytes == original; kernel/ramdisk/second/rdtbo byte-identical, only the dtb + id/sha in the header change).
- Flashing (as before): `fastboot flash boot <img>`; ROLLBACK: `fastboot flash boot pmos-begonia/touch-fix/bootwork/boot-vtp-touch-bat-prox.img`
  (fallback: `pmos-begonia/vtp-patch/boot-vtp.img`).
- After boot: `ls /sys/firmware/devicetree/base/soc | grep consys`; then `sudo insmod consys-bringup.ko` (stage=0) and `dmesg | grep consys`;
  only then `sudo insmod consys-bringup.ko stage=1`.

## ✅ RESULT #1 (2026-11-10): CONSYS on the mainline kernel IS ALIVE — chip id read
Phone (pmOS 6.16.4), image `boot-vtp-touch-bat-prox-consys.img` + `consys-bringup.ko stage=1`:
```
[after-power] DCXO_CW00=0x4e25 DCXO_ENABLE=0x1 RGU.SW_RST=0x1000 pericfg.CCU_CFG=0x1 ; vcn18/vcn33_1_wifi/vcn33_2_wifi=1 ccif4=1
CONSYS version id=0x00000000 (retry 10)            <- the first read right after power-on = 0 (a ~20 ms pause is needed, like the vendor retry/msleep(20))
RESULT: CONSYS IP version id=0x10020600 (expected 0x10020600) -> OK
HW id=0x8a00 FW id=0x8a00 CONF id=0x2 ; power-off done (no hangs)
```
Initial state before power-on: DCXO_CW00=0x4e1d (EXTBUF2 mode=3, EN_M=0), pwrap.DCXO_ENABLE=0x3. After: mode=0, EN_M=1, DCXO_ENABLE bit1 cleared — like the vendor.
Confirmed working: genpd CONN (mtk-pm-domains) + clk CCIF4_AP + mt6359 regulators + XO_WCN + pinmux + RGU reset-assert + the pericfg bit; reserve 0xbc000000+4M.
Debugging: `sudo sysctl kernel.dmesg_restrict=0` on the phone (until reboot) — then dmesg can be read over ssh without sudo.
Toolchain pitfalls (handled in build.sh): struct module size (DEBUG_INFO_BTF must not be disabled), release without "+" (LOCALVERSION=), HOSTCC without -Werror for libbpf.

## ✅ RESULT #2 (2026-11-10): the CONSYS MCU STARTED with our RAM images — "MCU ALIVE"
`consys-bringup.ko stage=3` (the module itself reads /home/user/connsys-fw/soc1_0_ram_{mcu,bt,wifi}_2a_1_hdr.bin):
- infracfg_ao+0x380 = emi_base>>16 (=0xbc00), +0x388 = 0x10003000>>16 (=0x1000) — reads back correctly.
- ROM patch -> EMI (reserve 0xbc000000+4M), offsets from u32@24 of the header (LE): off = u32 & 0x00ffff00:
  mcu 0x000000 (0x1dfe8), bt 0x080000 (0x8b661), wifi 0x140000 (0x3044c); readback OK. Build time (16 B) -> EMI+0x68300/310/320.
- releasing the RGU reset (bit12, key 0x88<<24) -> poll: t+20ms CPUPCR=0xf000589c (executes code from the EMI window), t+40ms 0x18002600==0x1D1E (MCU ALIVE).
- then CPUPCR=0x00000b04 stable (idle in ROM, waiting for host commands), coredump empty. power-off without hangs.
TRAPS:
- the vendor "topckgen_base" == INFRACFG_AO (0x10001000), NOT topckgen! Writing the remap to 0x10000380 is forbidden (someone else's registers).
- ID = 0 on a "cold" power-on after our previous power-off: XO_WCN must be enabled from the initial state
  (DCXO_ENABLE=0x3, CW00=0x4e1d) and given time to settle; power_off now restores the initial state. The cause (XO timing vs remap) was not separated: H1/H2 in the log.
- patch_mcu (soc1_0_patch_mcu_*_hdr.bin, 22 KB, a different header format) was NOT loaded — that is the PDA path (ems_info pda_dl_patch_flag=1), through the WMT channel.
Next: layer 2 — the host<->MCU channel (BTIF + STP + WMT commands): PDA patch, WMT power-on, "WIFI func on"; then gen4m (HIF over AXI/PDMA 0x18004000).

## RECONNAISSANCE of layer 2: the host<->MCU channel (BTIF + STP + WMT)   [read only, nothing was run]
Source: mtk-modules-r-oss/.../common_main/core/{wmt_ic_soc.c, wmt_core.c, stp_core.c}, kernel-r-oss/.../btif.
- mt6785: emi_info.pda_dl_patch_flag=1 => power-on order: [ROM patch into EMI — done on our side] -> PDA loading of patch_mcu over BTIF (before STP full) -> WMT reset (init_table_3)
  -> `_wmt_soc_mode_ctrl(BTIF_FULL)`: query STP(01 ..), set STP, enable host STP full, sleep 10ms, query STP -> SET_CHIP_ID -> (a-die, coex, ...) -> func-on.
- WMT command format: `01 <opcode> <len16 LE> <payload>`; event: `02 <opcode> <len16 LE> <status..>`.
  PDA CFG (21 bytes): `01 01 11 00 00 | addr[4] | size[4] | key[4]=0 | 2f 02 02 00` -> wait for `02 01 01 00 00`.
  Then a raw 12-byte PDA header (size+12 in the first 4 bytes LE, the rest 0) and fragments of 1000 bytes (RAW, without STP).
  WMT_RESET_CMD / SET_STP / QUERY_STP / SET_CHIP_ID — arrays in wmt_ic_soc.c (init_table_1_2/_3/_4/_5, set_chipid_script).
- STP frame (mandatory, before full): [0x80+(txseq<<3)] [type<<4 | len>>8 &0xf] [len&0xff] [0x00] payload [00 00]  (no CRC/window). type WMT = 0.
- STP frame (full): h0=0x80+(txseq<<3)+txack, h1=(type<<4)|(len>>8 &0xf), h2=len&0xff, h3=(h0+h1+h2)&0xff, payload, CRC16(payload) LE;
  window/ack/seq + retransmit timer (MTKSTP_WINSIZE) — this is the trickiest part.
- BTIF HW (vendor DT btif@1100c000): reg 0x1100c000 (+0x1000), tx-dma 0x11000b80, rx-dma 0x11000c00; IRQ GIC SPI 138 (btif), 155 (tx dma), 154 (rx dma);
  INFRACFG_AO clocks BTIF_CG + AP_DMA_CG => in mainline: CLK_INFRA_BTIF (=26), CLK_INFRA_AP_DMA (=40) EXIST. Registers: RBR/THR +0x0, IER +0x4, IIR/FIFOCTRL +0x8, FAKELCR +0xC, LSR +0x14,
  SLEEP_EN +0x48, DMA_EN +0x4C, RTOCNT +0x54, TRI_LVL +0x60, WAK +0x64, HANDSHAKE +0x6C (details in btif_priv.h). PIO without DMA is possible for the control traffic.
- Scale: the vendor WMT is ~67k lines (core 32k, linux 23k, platform 12k) — it does NOT need porting; a minimal layer is needed:
  (B) BTIF PIO tx/rx ~300 lines, (C) STP mand + PDA + full (seq/ack/CRC/retx) ~400-600, (D) 5-10 WMT commands up to "WIFI func on".
- Without reflashing the DT it is possible to: ioremap by address, IRQ via irq_fwspec(GIC SPI 138), the clock via of_clk_get_from_provider(infracfg, 26/40). Formally later — a node in DT.
- Unknown: the addressByte for PDA patch_mcu (in the header: hdr[24:28]=11 c0 01 00 => 0x0001c011?), the BTIF baud/mode, whether the MCU needs "WMT ready" before the first CFG.
First success criterion of layer 2: after PDA CFG the event `02 01 01 00 00` arrives => BTIF and the PDA protocol work.

## ✅ RESULT #3 (2026-11-10): the AP<->MCU channel over BTIF works — PDA CFG accepted
`consys-bringup.ko stage=4` (BTIF PIO/polling, no DMA and IRQ; clocks via of_clk_get_from_provider(infracfg, 26 BTIF / 40 AP_DMA); no DT changes):
```
BTIF init: FAKELCR=0, HANDSHAKE|=1, FIFOCTRL(16550 FCR) 0x3,0x1,0x5,0x1, TRI_LVL=0x18 (tx8,rx1), DMA_EN=0x4 (autorst), IER=0; before TX: WAK 0->1 (128-160us)
[PDA-CFG] TX 27: 80 40 15 00 | 01 01 11 00 00 | 00 c0 01 00 | 40 57 00 00 | 00 00 00 00 | 2f 02 02 00 | 00 00
[PDA-CFG] RX 11: 80 40 05 00 | 02 01 01 00 00 | 00 00   -> EVENT OK (~6 ms)
```
- STP mandatory frame: `80 | (4<<4)|len>>8 | len | 00 | payload | 00 00` (WMT_TASK_INDX=4), the answer is symmetric.
- patch_mcu: header 28 B + 2 B checksum (OPT_PATCH_CHECKSUM), body 22336 B; PDA addr = hdr[24:28] & ~0xff = 0x0001c000 (hypothesis: accepted at the CFG level).
- nothing arrives from the MCU before the first command.
Next: stage=5 — the raw body over PDA (12-byte header + 1000 B fragments) and WMT reset (01 07 01 00 04 -> 02 07 01 00 00), then the switch to STP full.

## INTERIM (2026-11-10): PDA patch_mcu does NOT work yet, the register channel works
Confirmed working (stage=4/5 on the phone):
- WMT_SET_REG read in STP mand: `01 08 10 00 | 02 01 00 01 | addr | 0 | ffffffff` -> `02 08 0c 00 | 00 00 00 01 | addr | value`.
  Read: 0x80000008 (HCR/chip id) = 0x6632 (the vendor then substitutes the SoC chipid 0x6785), 0x80000000 (HVR) = 0x8a00, 0x80000004 (FVR) = 0x8a00.
- PDA CFG is accepted (answer 02 01 01 00 00) with ANY address, including 0x0 and 0xd6570001 -> the address in CFG is not validated.
DOES NOT work: after PDA CFG the ROM "goes silent" (no answer to WMT reset, to a repeated CFG, or even without a body at all) — both with the full body (22336+12), with pauses,
  and with a 1.5 s reset delay. I.e. the ROM stays in "waiting for the PDA stream" mode or silently rejects it. The cause is not found.
  Hypotheses: (a) loss/corruption of body bytes over PIO (the vendor sends via DMA); (b) wrong body format/size/CRC; (c) the address — but (CFG does not check it).
  Launcher (wmt_launcher): reads the "patch info" from the file at offsets 26..33; for patch_mcu that is 01 00 57 d6 (57 d6 = checksum), the address cannot be extracted from it unambiguously.
Workaround: the vendor with patch_num==0 skips PDA+reset and goes on (QUERY_STP/SET_STP -> STP full -> SET_CHIP_ID ...). stage=6 tries exactly that.
STP full (from stp_core.c): h0=0x80|seq<<3|ack, h1=type<<4|len>>8, h2=len, h3=(h0+h1+h2)&0xff, payload, CRC16/ARC(payload) LE (== the kernel crc16());
  ACK = header-only `80|ack 00 00 sum`; the MCU sends its frames with seq (3 bits), the host acknowledges.
Commands: QUERY_STP `01 04 01 00 04` -> default `02 04 06 00 00 04 11 00 00 00`; SET_STP `01 04 05 00 03 db 0e 68 01` -> `02 04 02 00 00 03`;
  after FULL: QUERY -> `... db 0e 68 01`; SET_CHIP_ID `01 02 05 00 11 85 67 00 00` -> `02 02 01 00 00`.

## ✅ RESULT #4 (2026-11-10): STP full + WMT work; the MCU WiFi function turns on/off (stage=7)
The chain on the phone (pmOS mainline 6.16.4, module consys-bringup.ko stage=7), WITHOUT patch_mcu/PDA and without coex settings:
 power-on -> ROM patch into EMI -> MCU alive -> BTIF PIO -> WMT_SET_REG reads (HCR 0x6632 / HVR 0x8a00 / FVR 0x8a00)
 -> QUERY_STP, SET_STP (mand) -> host in STP FULL -> QUERY_STP(full) -> SET_CHIP_ID 0x6785
 -> FUNC_CTRL WIFI on: `01 06 02 00 03 01` -> `02 06 01 00 00` (CPUPCR 0x51a2 — the WiFi core runs) -> WIFI off OK -> power-off.
STP full minimum: stop-and-wait, h0=0x80|seq<<3|ack, ACK = header-only, CRC-16/ARC (the kernel crc16()). Compatible with the firmware.
patch_mcu (PDA) was NOT needed to turn the WiFi function on; PDA remains unsolved (the ROM goes silent after CFG) — possibly not needed at all.
NEXT (what separates us from a working network): gen4m — the WiFi driver:
  1) keep WiFi on, do not cut the power; HIF: AXI/PDMA (CONN_HIF_PDMA 0x18004000, wifi@18000000 1 MB window, IRQ SPI 322), reserved wifi-mem 3 MB;
  2) loading WIFI_RAM_CODE_soc1_0_2a_1.bin (631 KB) into the WiFi core through the HIF; 3) cfg80211 (mac80211 is not used, gen4m = fullmac-like);
  4) wpa_supplicant/NetworkManager on top of cfg80211. gen4m is ~1100 files, 4.14 API -> port to 6.16 (cfg80211) — the bulk of the work.

## RECONNAISSANCE of gen4m (the WiFi driver) for mt6785   [reading, nothing was run on the phone]
- Chip profile: **MT6632** (Makefile: MTK_COMBO_CHIP defaults to MT6632; the MCU returned HCR=0x6632) -> `chips/mt6632/mt6632.c` (495 lines) + `chips/common`.
- HIF: **AXI** -> `os/linux/hif/axi/axi.c` (1590 lines, platform_driver, of_match "mediatek,wifi", of_reserved_mem_device_init, of_dma_configure, ioremap of the CSR from reg[0] of the node)
  + common PDMA: `os/linux/hif/common/{hal_pdma,kal_pdma,dbg_pdma}.c` (~5000 lines). Platform: `os/linux/plat/mt6785/plat_priv.c` (126 lines).
- DT needs the node `wifi@18000000` (compatible "mediatek,wifi", reg 0x18000000/0x100000, IRQ GIC_SPI 322 LEVEL_LOW, memory-region = wifi-reserve-memory 3 MB shared-dma-pool) — our DTB does NOT have it YET (there is consys-bringup).
- Size of gen4m: 149 .c + 186 .h; lines: mgmt 83k, nic 35k, common 35k, os/linux 105k, include 136k. Port 4.14 -> 6.16: cfg80211 ops, timers, proc_ops, wext, netdev API.
- WiFi firmware: WIFI_RAM_CODE_soc1_0_2a_1.bin (631 KB) is loaded by gen4m itself through the HIF after WMT "WIFI on".
- An attempt at a trial build of gen4m for 6.16 with -k (error count) was blocked by the permission classifier; a decision by the user is required.

## MEASUREMENT: trial build of gen4m for 6.16 (WSL, ~/gen4m-port, script module/ref/gen4m_build.sh)
Required variables: CONFIG_MTK_COMBO_WIFI=m CONFIG_MTK_COMBO_WIFI_HIF=axi WLAN_CHIP_ID=MT6632 MTK_COMBO_CHIP=MT6632 CONFIG_MTK_PLATFORM=mt6785 (+ -I to the vendor headers).
The first wall (91 of 149 files): `wmt_exp.h` -> `mtk_wcn_cmb_stub.h`/`osal.h`/`wmt_plat.h` (the whole vendor WMT). Solution: our own minimal `wmt_exp.h` (shim) instead of the vendor stack.
The WMT API surface that gen4m actually calls (only ~17 functions, the shim is small):
  mtk_wcn_wmt_{assert_keyword, assert_timeout, ic_info_get, wlan_reg, wlan_unreg, wifi_fem_cfg_report, msgcb_reg, msgcb_unreg, mpu_lock_aquire, mpu_lock_release},
  mtk_wcn_wlan_{reg, unreg, gen4_init, gen4_exit}, mtk_wcn_stp_coredump_start_get, mtk_wcn_consys_hw_wifi_paldo_ctrl, mtk_wcn_set_connsys_power_off_flag.
Port plan: (1) a shim wmt_exp.h + .c that delegates to consys-bringup (power/STP/WMT func on); (2) get the 6.16 compilation through (cfg80211/proc_ops/timers);
(3) the DT node wifi@18000000 + reflashing boot.img; (4) loading WIFI_RAM_CODE through the HIF; (5) wlan0.

## PORTING gen4m TO 6.16 — start (wifi/gen4m-port/, build: module/ref/gen4m_build.sh in WSL)
Done: shim `gen4m-port/shim/include/{wmt_exp.h, gen4m_compat.h, stdarg.h}` (the vendor WMT replaced by a minimal API; compat: struct timeval/do_gettimeofday,
sched_clock, from_timer->timer_container_of, del_timer_sync->timer_delete_sync, rtc_time_to_tm->rtc_time64_to_tm, ioremap_nocache->ioremap);
the build script removes the forced -Werror (+ -Wno-* for style warnings), builds MT6632/AXI with -k and writes `gen4m-port/errors.txt`.
Number of unique compilation errors over time: 710 (with -Werror) -> 182 (no wmt_exp.h) -> 82 (shim + -Wno-error) -> 97 (after stdarg, gl_kal.c opened up).
Remaining classes (mechanical): cfg80211_ops signatures (~25: key ops now with link_id, mgmt_frame_register removed -> update_mgmt_frame_registrations,
 set_monitor_channel/start_ap/change_beacon/set_bitrate_mask/..., station_parameters -> link_sta_params, cfg80211_roam_info.bss -> links[]),
 file_operations -> proc_ops (~11), set_fs/get_fs/KERNEL_DS/mm_segment_t (gl_kal.c: reading files -> kernel_read), netif_rx_ni -> netif_rx, napi_gro_flush,
 strlcpy -> strscpy, net_device tx_dropped/rx_dropped -> dev->stats/core_stats, RPS_MAP_SIZE, platform .remove is now void, list_sort cmp type,
 wireless_dev.cac_started, gConEmiPhyBaseFinal/gConEmiSizeFinal/kalSetDrvEmiMpuProtection (platform EMI -> into the shim).
Then: implement wmt_shim.c (wlan_reg/msgcb/ic_info_get...) + the link to consys-bringup, the DT node wifi@18000000, firmware loading, wlan0.

## ✅ RESULT #5 (2026-11-10): gen4m COMPILES for 6.16 -> wlan_mt6632_axi.ko
Unique error counter: 710 -> 182 -> 82 -> 97 -> 60 -> 4 -> **0**. Build: `bash module/ref/gen4m_build.sh` (WSL), result `~/gen4m-port/wlan_mt6632_axi.ko` (50 MB with debug info).
What was done (all reproducible, the vendor sources are not edited — it works on a copy):
- `gen4m-port/shim/include/`: `wmt_exp.h` (a minimal WMT API), `gen4m_compat.h` (timeval/do_gettimeofday, sched_clock, from_timer, del_timer_sync, rtc_time_to_tm, ioremap_nocache, strlcpy, netif_rx_ni, gro), `stdarg.h`.
- `gen4m-port/port.py` (a patcher with asserts): MODULE_SUPPORTED_DEVICE, netif_napi_add(3 args), dev_addr -> eth_hw_addr_set, set_fs/get_fs (kalFileOpen/Read/Write), tx/rx_dropped,
  roam_info.links[0].bss, file_operations -> proc_ops (gl_proc.c, gl_kal.c), an RPS stub, fb notifier stubs, cfg80211_ops: link_id in add/get/del_key, default(mgmt)_key, tdls_mgmt,
  start_radar_detection, stop_ap, set_bitrate_mask, get_tx_power; change_beacon(cfg80211_ap_update); mgmt_frame_register -> update_mgmt_frame_registrations (a wrapper);
  station_parameters -> link_sta_params; axi: platform_device_id for MT6632, remove() -> void, MODULE_DEVICE_TABLE(axi) removed, the EMI-MPU call removed; list_sort cmp; P2P: the same changes.
- Makefile of the copy: the forced -Werror removed, -Wno-* added for the style warnings.
NOT DONE (next): 1) wmt_shim.c — implementations of ~17 WMT functions (wlan_reg/unreg, msgcb, ic_info_get, mpu_lock, assert*, set_connsys_power_off_flag, gConEmi* etc.) and the link to consys-bringup (keep WiFi on, hand over pwr/STP/func-on);
2) the DT node wifi@18000000 (mediatek,wifi, reg 0x18000000/0x100000, SPI 322, memory-region wifi-reserve-memory 3 MB shared-dma-pool) + reflashing boot.img;
3) checking the module symbols (247 undefined) against the phone kallsyms; vermagic/struct module like consys-bringup; 4) loading WIFI_RAM_CODE through the HIF, registering wlan0.
NOTE: at the time of writing ssh to the phone (172.16.42.1) timed out — check the USB network/the phone power.

## ✅ RESULT #6 (2026-11-10): PROFILE CORRECTION — gen4m for mt6785 is CONNAC (not MT6632); the shim and DT are ready
- CORRECTION to RESULT #5: the chip profile = **CONNAC** (`chips/connac/connac.c`; firmware `WIFI_RAM_CODE_soc1_0_<hw>_<n>.bin`, flavor 'a' => `..._2a_1.bin`), WLAN_CHIP_ID=6785, MTK_COMBO_CHIP=CONNAC.
  0x6632 from the MCU register is NOT the profile choice. With CONNAC there were 18 errors, after stubbing plat_priv.c and including wmt_exp.h in gl_rst.c — 0. Module: `gen4m-port/wlan_6785_axi.ko` (4.5 MB stripped, vermagic/struct module OK).
- plat_priv.c (mt6785) is replaced with stubs in the port (the vendor cpu_ctrl/topo_ctrl/EMI-MPU are unavailable); in the original WIFI_EMI_MEM_OFFSET=0x140000/SIZE=0x130000 — matches our EMI layout (wifi RAM @0x140000).
- consys-bringup.ko now contains the WMT shim: exports mtk_wcn_wmt_{wlan_reg,wlan_unreg,msgcb_*,ic_info_get,assert_*,mpu_lock_*}, mtk_wcn_{set_connsys_power_off_flag,consys_hw_wifi_paldo_ctrl,...},
  Android hooks (register_file_buf_handler, update_driver_loaded_status, wifi_reset_*...). A new `stage=8`: bring CONSYS up completely (up to STP full + SET_CHIP_ID), LEAVE it on and call wlan_probe_cb of the gen4m driver (workqueue).
- DT: added `wifi-reserve-memory` (shared-dma-pool, no-map, 3 MB) and `wifi@18000000` (compatible "mediatek,wifi", reg 0x18000000/0x100000, IRQ SPI 322 LEVEL_LOW, memory-region). Image: `module/boot-vtp-touch-bat-prox-consys-wifi.img`
  (kernel/ramdisk/rdtbo the same, only the DTB changes; rollback = bootwork/boot-vtp-touch-bat-prox.img).
- ON THE PHONE (~): wlan_6785_axi.ko, consys-bringup.ko (with the shim), WIFI_RAM_CODE_soc1_0_2a_1.bin (must be put into /lib/firmware), connsys-fw/.
- TURN OFF idle-shutdown on the phone (it powered the phone off in the middle of the work): `sudo systemctl disable --now idle-shutdown`.
ORDER OF THE FIRST RUN (after flashing the wifi image): modprobe cfg80211; cp WIFI_RAM_CODE... /lib/firmware; insmod consys-bringup.ko stage=8 (keeps the power);
  insmod wlan_6785_axi.ko -> AXI probe (node wifi@18000000) -> mtk_wcn_wmt_wlan_reg -> the shim calls wlan_probe_cb -> firmware loading over the HIF -> wlan0. Logs: dmesg.

## ✅ RESULT #7 (2026-11-11): gen4m LOADED on the phone, HIF/DMA and firmware loading work; FW_START gets no answer
Working gen4m build configuration: `MTK_COMBO_CHIP=CONNAC WLAN_CHIP_ID=6785 WIFI_IP_SET=2 MTK_ANDROID_EMI=y` (+port.py, shim; `module/ref/gen4m_build.sh`). Module `gen4m-port/wlan_6785_axi.ko`.
What is confirmed on hardware (dmesg):
- DT node wifi@18000000 + wifi-reserve-memory 3 MB (0xab000000): `axiCsrIoremap ... 0x100000 @ 0x18000000`, `request_irq num(299)`.
- Driver own in ~2 ms; WFDMA rings (TX FWDL/CMD, RX DATA/EVENT) in the reserve; the device takes the commands (DIDX grows); `Chip ID[0001] Version[E1] HW[0x8a00] SW[0x8a00]`, `ready_bits=0x3`.
- Firmware found: `WIFI_RAM_CODE_soc1_0_2a_1.bin` (manifest `t-neptune-mp-soc1_0e1-2015-KRUG_SOC1_0_E1_ASIC-20200924182102`, 5 regions); the regions are passed through the FWDL ring (SEQ 3..10) and the EMI sections are written into EMI (`0xbc000000+0x177000`, `+0x1fe000`).
- It took: the CONNAC profile (not MT6632), IP_SET=2 (name `..._2a_1.bin`, RX desc 20 B), enabling CFG_MTK_ANDROID_EMI and handing out gConEmiPhyBase/Size (+Final) from the shim; without the flag wlanDownloadEMISection silently copies nothing.
DOES NOT WORK: `FW_START` (wlanConfigWifiFunc, fw_dl.c:1628) — no event for 3 s, `CONN_HIF_ON_DBGCR01 = 0xb04` (the main CONSYS MCU in idle ROM). Tried: EMI sections enabled (no effect);
  WMT FUNC_CTRL(WIFI on) before loading, left ON (the MCU answers OK, PC ->0xe014, but by FW_START it is 0xb04 again; no effect on FW_START).
`mtsoc1_0_patch_e1_hdr.bin` is missing (WARN "FW[2] load error", the Android vendor has no such file — probably not needed).
HYPOTHESES (most likely first): (1) the WF CPU (N9) is not started/gets no clock: the remaining steps of the vendor sw_init are needed (A-die/wifi_config, coex tables, osc/xo, set_registers) or a different WF power sequence on the MCU side;
 (2) the EMI sections of WIFI_RAM_CODE overlap/corrupt our ROM images (ram_wifi @0x140000.. etc.) or WF sees EMI through a different remap; (3) a WF patch is needed (mtsoc1_0_patch_e1) — unlikely;
 (4) the firmware starts, but the event does not arrive (RX ring/IRQ 299/EVENT ring) — check the RX ring, IRQ and the CONNSYS registers of the WF CPU PC.
Next steps: read the WF CPU PC/WFSYS state (CONN registers, addresses from gen4m: WFSYS_ON 0x820D0000 + 0xAE000?), check the IRQ 299 handling (counter in /proc/interrupts), port the rest of sw_init.

## RESULT #8 (2026-11-11): added A-die, post-chipid config, RF calibration — FW_START is still silent
What was done in consys-bringup.ko (parameters): `calib` (mask: 1=QUERY_A_DIE, 2=RF calibration, default 1), `post_cfg` (default 1).
- QUERY_A_DIE `01 02 01 00 12` -> `02 02 05 00 00 01 8b 31 66` (the A-die answered; 0x6631?). No effect on FW_START.
- post_cfg = the tail of the vendor polling_consys_chipid(): 0x18004160 bit4 (CONN2EMI slpprot): 0->0x10; 0x180c1600 bit23 (toppose_restore_done): 0x01004706->0x01804706;
  0x180b3034 bits[20:19]=01 (WPLL): 0x07900020->0x07880020. Applied, no effect on FW_START. (co-clock for mt6785 = 0, this block is skipped.)
- RF calibration `01 14 01 00 01` (expect `02 14 02 00 00 01`): the ACK came, NO event in 20 s either with post_cfg or without. The MCU PC spins in 0xf000c9da..cd76
  (in the ram_mcu RAM image, the EMI offset == PC-0xf0000000). After the hang FUNC_CTRL does not answer either. Nothing to disassemble with: the MCU core is Andes NDS32, there is no toolchain.
  => the MCU RAM code (without patch_mcu) hangs in the calibration; the main hypothesis remains: the not loaded patch_mcu (PDA), since the vendor installs it BEFORE everything else.
- PALDO for mt6785 is a no-op (consys_hw_wifi_vcn33_ctrl return 0), ACR/AFE — "No need". Ruled out.
- The working configuration now: stage=8 calib=1 (calibration off): FUNC_CTRL(WIFI on) OK, gen4m gets to FW_START and is silent there (HIF/DMA/FWDL ok).
Next: (a) go back to PDA patch_mcu: the BTIF DMA path instead of PIO (the vendor sends the body through DMA; PIO could lose bytes/break timing), check what exactly the ROM answers to CFG ->
  body -> reset; (b) an NDS32 disassembler (binutils nds32 / Andes) to understand what the MCU waits for in the calibration; (c) logging the WF CPU/N9 PC (gen4m registers) during FW_START.

## PREPARATION #9 (2026-11-11): TX through AP_DMA for the PDA body — built, NOT RUN ON THE PHONE
- Checked against the vendor: the patch_mcu format is decoded correctly (hdr 28 + checksum 2, body 22336 B, aligned to 4; addr from hdr[24:28]=0x0001c011 -> 0x1c000 — a low MCU address, not 0xf0xxxxxx like ram_*; CFG does not check the address).
  The vendor sends the PDA header and 1000 B fragments through wmt_core_tx(raw) -> BTIF TX **DMA** (BTIF_TX_MODE/RX_MODE = DMA in mtk_btif.h). Our PIO is correct (LSR control), but "PIO loses bytes" is not proven.
- consys-bringup.c: added TX-only DMA (AP_DMA 0x11000b80, vFIFO 8 KB coherent, regs as in btif_dma_priv.h; RX and the control frames stayed PIO), parameter `pda_dma` (default 1; 0 = the old PIO behaviour).
  Log: the lines `TXDMA[start|body-done|stuck|drain]` and `PDA body sent (DMA|PIO)`.
- Run: `sudo sysctl kernel.dmesg_restrict=0; sudo insmod ~/consys-bringup.ko stage=5 pda_addrs=0x1c000; dmesg | grep -i consys | tail -80; sudo rmmod consys-bringup`
  (the old module is kept on the phone as ~/consys-bringup.ko.pio-bak). Success = the answer `02 07 01 00 00` to WMT-RESET after the body.
- sudo on the phone requires a password (no NOPASSWD) -> the user does the run.

## ✅ RESULT #9 (2026-11-11): patch_mcu LOADED over PDA through TX DMA — PIO really was losing bytes
`stage=5 pda_addrs=0x1c000`: PDA-CFG -> OK; header(12)+body 22336 B through AP_DMA TX (vFIFO 8 KB): `WPT=RPT=0x174c`, `INTBUF=0`; WMT-RESET -> `02 07 01 00 00` OK; CPUPCR 0xb04 (ROM idle) -> 0x1e664 (the patch executes).
Pitfall: in BTIF_DMA_EN bit0 = **RX** DMA, bit1 = **TX** DMA (btif_priv.h). The first attempt with BIT(0) enabled RX DMA: the channel took 12 bytes into INTBUF, and BTIF did not accept them (drain timeout -110).
The address 0x1c000 (hdr[24:28]=0x0001c011 & ~0xff) is confirmed in practice.
Built into stages 6-8: parameter `pda_pre` (default 1) = PDA-CFG -> DMA body -> WMT-RESET -> STP full -> SET_CHIP_ID -> (calib) -> FUNC_CTRL. `pda_dma=0` brings PIO back.
NEXT: stage=8 calib=3 (the RF calibration now with patch_mcu), then insmod wlan_6785_axi.ko and watch FW_START.

## ✅ RESULT #10 (2026-11-11): with patch_mcu the RF calibration ANSWERS (before it hung forever)
stage=8 calib=3, pda_pre=1: PDA OK -> STP full -> SET_CHIP_ID -> QUERY_A_DIE (`02 02 05 00 00 01 8b 31 66`) -> RF-cal: the MCU sends the event `02 14 c2 01 00 01 01 11 00 84 84 84 82 82 ...` (STP frame len=0x1c6=454 B, this is calibration data; status=00, subtype=01).
Our receiver cut the frame at 320 B (buf[320] in stp_full_recv) -> the CRC did not match -> the MCU retransmitted seq=3 forever -> "NO WMT EVENT". Fixed: buf[1024]; the calibration event is checked by opcode/status/subtype, not by the exact length (the vendor expects `02 14 02 00 00 01`, on our firmware the real event is longer).
Side effect: after the calibration was cut off, FUNC_CTRL(WIFI on) still answered `02 06 01 00 00` (seq 4 was accepted as "unexpected", since rxexp got stuck at 3).

## ✅✅ RESULT #11 (2026-11-11): WiFi IS UP — wlan0 + wlan1 created, FW_START EVT success
Sequence on the phone: `modprobe cfg80211; insmod consys-bringup.ko stage=8 calib=3` (PDA patch_mcu over TX DMA -> STP full -> chipid -> A-die -> RF-cal `done: 0` -> WIFI on) ; `insmod wlan_6785_axi.ko`.
gen4m dmesg: `FW_START CMD send` -> `FW_START EVT success!!` -> `Ready bit asserted`; interfaces wlan0 (4a:32:d2:dd:ca:2d), wlan1, cfg80211 registered (random MAC).
Root cause of the last days: patch_mcu was not loaded (PIO lost bytes of the PDA body) -> the MCU without the patch hung in the RF calibration and did not start the WF CPU. Fix: TX through AP_DMA (BTIF_DMA_EN: bit0=RX, bit1=TX!) + an STP receive buffer >=512 B.
Remaining gen4m warnings (not critical, but to fix): no /lib/firmware/{wifi.cfg, txpowerctrl.cfg} (they are in connsys-fw/), `mtsoc1_0_patch_e1_hdr.bin` (not needed, WARN), `illegle ucWifiPath 0` (nicCfgChipCapPhyCap, probably because of the missing wifi.cfg/NVRAM), `glLoadNvram fail` (random MAC).
NEXT: ip link set wlan0 up; scan (iw/wpa_supplicant/NetworkManager), connect to an AP; autoload (modules-load + the order consys-bringup -> wlan, parameters stage=8 calib=3); move to a proper DT/driver without parameters; close the holes (power-off/rmmod, suspend).

## ⚠ PITFALL (2026-11-11): `apk upgrade` on the phone REFLASHES the boot partition with the stock DTB
The mkinitfs trigger -> boot-deploy builds boot.img from /boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb (stock, 46716 B) and `dd`s it into the `boot` partition => our DTB changes are lost (vtp/touch/bat/prox + consys + wifi). In the live system the nodes stay until the next reboot.
Fix (on the phone): `sudo sh /home/user/fix-boot-dtb.sh` — puts `wifi/module/base/consys.dtb` (== ~/begonia-wifi.dtb, 48514 B) into /boot/dtbs/... (the stock one is kept as /boot/mt6785-xiaomi-begonia.dtb.stock) and runs mkinitfs. Permanently: a kernel package update will overwrite the DTB again -> repeat the script.
Rollback to stock: cp /boot/mt6785-xiaomi-begonia.dtb.stock /boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb && mkinitfs. Emergency from the PC: fastboot flash boot wifi/module/boot-vtp-touch-bat-prox-consys-wifi.img (but the ramdisk there is old).

## ⚠ PITFALL (2026-11-11): `rmmod wlan_6785_axi` CRASHES THE KERNEL (no clean unload path) - only a reboot
oops #1: exitWlan -> `wlanRemove: u4WlanDevNum = 0` (ERROR, double free of the wiphy/adapter) -> cleanup_module -> wiphy_unregister -> device_del -> klist_del NULL (+0x20).
oops #2: hif_thread (still alive) -> wlanAcquirePowerControl -> asicLowPowerOwnClear -> kalDevRegWrite into an already unmapped CSR -> page fault. rmmod hangs in D, wlan refcnt = -1, WiFi is dead, a reboot is needed.
TODO: sort out the unload order in gl_init/gl_os (wlanRemove/glUnregisterWiphy/exitWlan), stop hif_thread before the unmap. For now: wifi-up.sh/bt-test.sh refuse to run if wlan is already loaded.
Interesting: before that wlan0 was connected to the network - dmesg shows real RX (ARP/IPv6), Tput > 0.
## ✅✅✅ RESULT #12 (2026-11-11): WiFi is CONNECTED and passes traffic
wlan0 (5 GHz, channel 120 DFS, WPA2-Enterprise PEAP, connected through NetworkManager) got DHCP, the gateway pings (0% loss, 3.5-98 ms), the internet (1.1.1.1) 0% loss (14-400 ms - large jitter, not analysed: power save / the noisy gen4m log). The report to the maintainer is updated (pmos-begonia/report-to-maintainer-wifi.md).
dmesg is flooded with gen4m debug output (StatsLogPkts/ARP...) - the kernel ring overflows, old consys lines get lost; read the bt/consys logs right after the start or raise the size (log_buf_len) / lower the gen4m log level.

## ✅ RESULT #21 (2026-11-11): rmmod wlan_6785_axi IS NOW CLEAN, the module can be reloaded without a reboot
Cause of the earlier oopses: exitWlan() with CFG_SUPPORT_PERSIST_NETDEV removed the netdevs (wlanClearDevIdx -> u4WlanDevNum=0) BEFORE glUnregisterBus(); wlanRemove() saw u4WlanDevNum==0 and returned right away ("u4WlanDevNum = 0"), the threads (hif_thread) stayed alive, the adapter was not freed -> wiphy_unregister on an already dismantled device (klist_del NULL) + hif_thread into an already unmapped CSR.
Fix (gen4m-port/port.py): as the first step exitWlan() calls hifAxiRemove() (this is what WMT does on Android through wlan_remove_cb before unloading: wlanRemove, stopping the threads, IRQ, WF power), then the regular removal of netdev/wiphy. Plus the shim: mtk_wcn_wmt_wlan_unreg() resets g_probed, otherwise a repeated load of wlan would not get wlan_probe_cb.
Check (wlan-reload.sh): rmmod OK, 0 oops, insmod again -> FW_START EVT success, Ready bit, wlan0/wlan1, NetworkManager reconnected. The WiFi function in the MCU does not need to be switched off for a reload.
Debugging WiFi now: sudo sh ~/wlan-reload.sh (no reboot). consys-bringup can still be updated only with a reboot (wlan and hci0 hold it).


## ✅ 2026-11-14: BT/WiFi «ломаются» при переключении - причина и лечение (STP retransmit)
- Симптом: после выкл/вкл WiFi/BT (и даже сразу после загрузки) hci0 сыпал `Opcode 0x0c13 failed: -110`, `STP tx window stuck`, `[WMT] NO WMT EVENT`; rfkill block/unblock BT не помогал, жил только ребут. WiFi (AXI) при этом жив.
- Причина: потеря части STP-кадра при записи в BTIF (большие HCI-команды: Write Local Name 248 Б, EIR 241 Б; после загрузки - всегда первые, при переключении WiFi - случайно). Парсер MCU ждёт остаток кадра и игнорирует всё дальнейшее, MCU каждые ~360 мс повторяет свой последний кадр (`unexpected seq N`), наши кадры не принимает. Хостового retransmit не было (TODO с 2026-11-11).
- Лечение (консис-драйвер, коммит «Retransmit unacknowledged STP frames», патч 0019): очередь отправленных кадров `c->txq` (8 слотов), по таймауту 180 мс без ack - `7f 7f 7f 7f` (resync) + повтор неподтверждённых, как `stp_do_tx_timeout` у вендора; окно считается по числу неподтверждённых (`st->nunack`), а не по `rxack` (он врал сразу после старта). В dmesg: `[STP-FULL] no ack: resync + resend ...`.
- Проверка: на каждой загрузке теперь 3 retransmit-а (seq 7,0,1) и всё поднимается; стресс 17 циклов (nmcli radio wifi off/on параллельно с `bluetoothctl system-alias`) + 3 цикла `rfkill block/unblock all` - 0 ошибок.
- Не выяснено: почему теряются именно первые большие кадры (подозрение: MCU не успевает проснуться после WAK, `btif_wake()` ждёт 0.5-1 мс). Можно попробовать увеличить задержку/слать кадр порциями.
- Установлено на телефон вручную в `/lib/modules/6.16.4/.../consys-bringup.ko.zst` (оригинал: `/var/lib/begonia/backup/consys-bringup.ko.zst.orig`); пакет ядра пересобрать (r6) с патчем 0019, чтобы это закрепилось.
