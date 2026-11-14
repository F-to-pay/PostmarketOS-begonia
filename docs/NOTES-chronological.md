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
## Bluetooth (2026-11-11), reconnaissance: mt6785 = legacy WMT; BT = FUNC_CTRL(BT on) `01 06 02 00 00 01` + H4 in STP frames of type 0 (BT_TASK_INDX), ram_bt is already in EMI. Experiment: consys-bringup.ko `bt_test=1` (stage 7/8) sends HCI Reset/Read Version/Read BD_ADDR; run `sudo sh ~/bt-test.sh` RIGHT AFTER BOOT (without wlan).

## ✅✅✅ RESULT #12 (2026-11-11): WiFi is CONNECTED and passes traffic
wlan0 (5 GHz, channel 120 DFS, WPA2-Enterprise PEAP, connected through NetworkManager) got DHCP, the gateway pings (0% loss, 3.5-98 ms), the internet (1.1.1.1) 0% loss (14-400 ms - large jitter, not analysed: power save / the noisy gen4m log). The report to the maintainer is updated (pmos-begonia/report-to-maintainer-wifi.md).
dmesg is flooded with gen4m debug output (StatsLogPkts/ARP...) - the kernel ring overflows, old consys lines get lost; read the bt/consys logs right after the start or raise the size (log_buf_len) / lower the gen4m log level.

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

## ✅ RESULT #21 (2026-11-11): rmmod wlan_6785_axi IS NOW CLEAN, the module can be reloaded without a reboot
Cause of the earlier oopses: exitWlan() with CFG_SUPPORT_PERSIST_NETDEV removed the netdevs (wlanClearDevIdx -> u4WlanDevNum=0) BEFORE glUnregisterBus(); wlanRemove() saw u4WlanDevNum==0 and returned right away ("u4WlanDevNum = 0"), the threads (hif_thread) stayed alive, the adapter was not freed -> wiphy_unregister on an already dismantled device (klist_del NULL) + hif_thread into an already unmapped CSR.
Fix (gen4m-port/port.py): as the first step exitWlan() calls hifAxiRemove() (this is what WMT does on Android through wlan_remove_cb before unloading: wlanRemove, stopping the threads, IRQ, WF power), then the regular removal of netdev/wiphy. Plus the shim: mtk_wcn_wmt_wlan_unreg() resets g_probed, otherwise a repeated load of wlan would not get wlan_probe_cb.
Check (wlan-reload.sh): rmmod OK, 0 oops, insmod again -> FW_START EVT success, Ready bit, wlan0/wlan1, NetworkManager reconnected. The WiFi function in the MCU does not need to be switched off for a reload.
Debugging WiFi now: sudo sh ~/wlan-reload.sh (no reboot). consys-bringup can still be updated only with a reboot (wlan and hci0 hold it).

## ✅ RESULT #22 (2026-11-11): headphones (Sony WH-CH720N) connected, A2DP sink in PulseAudio
Cause of "br-connection-sdp-search": the BT firmware resets BD_ADDR to the default (00:00:46:76:63:01) on EVERY FUNC_CTRL(BT off/on) (log: Read BD_ADDR #5 = ours 3b:43, after power cycle #67 = 01 63 76); while the address was programmed only in hdev->setup (once). BlueZ kept the old address in memory, the air had another one -> the pairing keys did not match, SDP did not get through.
Fix: consys_apply_bdaddr() in hdev->open after FUNC_CTRL(BT on), synchronously, before the rx thread starts (hdev->setup removed). After the fix: Connected, UUID (A2DP Sink/AVRCP/HFP), bluez_card + bluez_sink.*.a2dp_sink (PulseAudio, bluez5 modules; pipewire-spa-bluez NOT needed), 0 oops, 0 CRC/window.
Note: there are no ALSA sound cards (/proc/asound/cards is empty) - the phone speaker/microphone are not set up, but Bluetooth audio does not depend on that.
TODO: real playback (check the sound in the headphones), HFP microphone, LE peripheral, suspend.

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

## 🔊 AUDIO (sound + microphone) — start of the MT6785 AFE port (2026-11-11), STATUS: sources written, NOT built
Diagnosis: 6.16.4 has no audio driver for MT6785 (`SND_SOC_MT6359` off, no AFE node in DT, 0 ALSA cards). The phone speaker/microphone
did not work at all; Bluetooth audio (A2DP) does not depend on this.
Finding: the MT6785 AFE is the same generation as mainline **mt8192** (of 2450 register defines 2441 are identical; only
`AFE_ASRC_2CH_CON12/13` and a couple of gain masks differ; the MTKAIF/`CKSYS_AUD_TOP_CFG/MON` calibration is identical). So we do NOT drag in the 4.14 vendor driver
(`kernel-r-oss/sound/soc/mediatek/mt6785`, 194 KB + ION/SCP/audio_dsp), but fork mainline mt8192.
The `audio/` directory (out of tree, like consys): `common/` (mtk-afe-*, soundcard) · `mt6785/` (mt8192 fork + our own machine mt6785-mt6359.c) ·
`codecs/` (mt6359.c, mainline, disabled in our config) · `audio-begonia.dtsi` · `build.sh` (WSL, after `module/build.sh`).
Changes relative to mt8192: clocks `top_apll{1,2}_d4` -> `_d8` (in the MT6785 top-clk the ENG1/2 muxes have parents d2/d4/d8, the vendor uses d8);
the `audiosys` reset became optional; `AFE_MAX_REGISTER=AFE_DL8_END`; ASRC_2CH_CON12/13 from the vendor; the machine driver is cut down to a
"MT6359 only" card (FE: DL1/DL12/DL2/DL3, UL1/UL2/UL3/UL_mono1; BE: ADDA, ADDA_CH34). DL9 and I2S6-9 stay in the code as dead weight.
DT: `&audsys` + power-domain AUDIO + `afe: audio-controller` (compat `mediatek,mt6785-audio`, SPI 184 LEVEL_LOW, 33 clocks per
`aud_clks[]`; the mainline top clocks already follow the right scheme `top_audio1/2 -> top_apll_i2sN -> top_apll12_divN`), MTKAIF pins (GPIO143/146/189-194)
and the `sound` node (`mediatek,mt6785_mt6359`, `mediatek,platform=<&afe>`, no `audio-routing` -> legacy probe).
Not verified / possible pitfalls at the first build: (1) compilation — there is no compiler on the phone, edited blind; (2) DT has no
`top_mainpll_d4_d4` (not critical in the code, NULL); (3) AINx -> the real board microphones (`Main Mic`->AIN0, `Headset Mic`->AIN1 — a GUESS);
(4) `power-domains` on audsys: if the clk-mt6785-aud probe hangs after this, remove it and move it to afe;
(5) MTKAIF calibration on real hardware (`phase` 0..42) may fail -> check dmesg `mtkaif_calib`.
Stage 2 (speaker): SmartPA TFA9874 — `i2c6@0x34`, reset GPIO24, irq GPIO69, audio over I2S3 (GPIO67 LRCK / 68 DO / 71 BCK);
mainline has no tfa9874 driver (tfa989x knows 9890/91/95/97) — needs a port or the vendor tfa98xx.
Check after building: `insmod` in order snd-soc-mtk-common, snd-soc-mt6359, snd-soc-mt6785-afe, mt6785-mt6359 (`dmesg | grep -i -E "afe|mt6785|mt6359|snd"`);
`aplay -l`; microphone: `arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 5 t.wav`.

### 🔊 Audio — debugging on hardware (2026-11-12), STATUS: card exists, stream runs, no sound
Modules are built on the phone itself: `audio/build-on-phone.sh` (clang, tree `/home/user/mt6785-linux`, not as root). DTB: `apply-dtb.sh`. Loading: `load-audio-safe.sh`.
Solved:
- The `insmod snd-soc-mt6785-afe` hang = ASRC gates switched off (`AUDIO_TOP_CON1` bits 12–14 = conn_i2s/general1/general2); reading the `0x0c80..` block with them off hangs the bus. Fix: clear `0x7000` in `enable_clock` and `runtime_resume` (a hack, TODO replace with CCF clocks). No logs during the hang → bisection with `bisect-afe.sh` + an O_SYNC file.
- Added clocks `aud_dac/dac_predis/3rd_dac/3rd_dac_predis/adc/adda6_adc` to DT (otherwise DAPM `Failed to add route`). Only `aud_tdm_clk` is missing.
- **runtime PM was dead because of my own bug**: `devm_pm_runtime_enable()` disables on unbind by itself, and `remove()` also had `pm_runtime_disable()` → every rmmod accumulated `disable_depth` (it was 4–5), PM off, `cache_only` never cleared, `memif enable` = -EBUSY (-16). Removed the manual disable. After that `depth=1`, resume/suspend work, trigger does not fail. **Lesson: reboot the phone between tests** (the counter lives in the device; once a repeated insmod of afe after rmmod hung the system).
- PulseAudio grabs the card by itself after boot (speaker-test then gives `Resource busy`); the scripts do `pactl unload-module module-alsa-card`.
Current state: speaker-test opens hw:0,0, state RUNNING, but `Write error -5 (EIO)` = ALSA waits for an interrupt, DMA does not move (`hw_ptr/CUR=0`, base=0xfe140000, `AFE_DAC_CON0(0x10)=0x11`: AFE_ON+DL1 enabled). A click at start/end — the codec turns on, no stream.
Live registers (while streaming): `0x2c AFE_CONN3 = 0`, `0x108 AFE_ADDA_DL_SRC2_CON0 = 0x83001802` (bit0=0 → ADDA DL SRC NOT enabled), `0x10c=f74f0000`. DAPM dump: DL1 `stream inactive`, ADDA_DL_CH1/2 On, DACL/R On, AIF_RX On, "ADDA Playback Enable" not visible. Suspicion: the ADDA BE/route does not start, or an autodisable mixer resets CONN3, or the APLL (apll1/apll2 enable_count=0) is not enabled for 48 kHz. The debugfs regmap shows `XXXXXXXX` for volatile registers (useless), measure only from the kernel.
Debug leftovers in the code (remove later): `PMDBG` dev_info and `disable_depth` in `mt6785-afe-pcm.c`; parameters `stage/ungate0/ungate1/keep_intbus`; `REGDBG`/`PTRDBG` in `common/mtk-afe-platform-driver.c`; the delayed `REGDBG2/IRQDBG` dump 300 ms after start in `common/mtk-afe-fe-dai.c` (`afe_dbg_fn`).
Next step: after a REBOOT `sh audio/trace2.sh` → `/home/user/regdbg.log` (REGDBG2/IRQDBG). Look at: `0x108` bit0 (SRC), `0x2c` (CONN3 DL1→ADDA), IRQ en/cnt/fs, whether the apll turned on; check whether `mtk_adda_dl_event`/prepare of the ADDA BE is called (add dev_info); compare with the vendor `kernel-r-oss` (`mt6785-dai-adda.c`, `mt6785-afe-clk.c` apll enable on DL).
Then: the loud TFA9874 speaker (stage 2), module autoload, UCM/Pulse profile, `aud_tdm_clk`, the `ASoC error (-16)` noise on 0x052c/0xe40.

### 🔊 Audio — BREAKTHROUGH: DMA runs (2026-11-12 09:30), STATUS: AFE pushes data, not verified by ear
**Cause of "stream runs, no sound":** the AFE core gate `AUDIO_TOP_CON0` **bit2 (`aud_afe`)** stays 1 (clock gated), although CCF considers `aud_afe`
enabled (`clk_summary`: enable_count=1). While the bit=1 the engine does not tick: `AFE_DL1_CUR` stands still, `AFE_IRQ_MCU_STATUS`=0, IRQ 184 is silent,
speaker-test → `Write error -5`. An explicit forced write `regmap_update_bits_base(AUDIO_TOP_CON0, BIT(2), 0, NULL, false, true)` in
`mt6785_afe_runtime_resume` (after `regcache_sync`) fixes it: `CUR` runs, IRQs arrive. This is a permanent fix in `mt6785-afe-pcm.c` (next to the CON1 bit12–14 hack).
Why CCF does not clear the bit — not found out (suspicion: the CCF write does not land/regmap cache). The gate polarity of this clk driver is **not inverted**: bit=1 ⇒ clock off.
How it was found (bisection): `dbg_gate_clr` (an snd-soc-mtk-common parameter) cleared the `AUDIO_TOP_CON0` bits one by one during a stream; as long as bit2 remained — CUR=0, once cleared — it ran.
Ruled out (not the cause): `AFE_IRQ_MCU_EN` (already 0xffffffff), the 26 MHz intbus (`keep_intbus`), APLL (`force_apll`), DAPM power (with a full route everything gets powered, `aud_dac/predis` turn on),
`mtkaif_26m_clk` (a dummy in the vendor code), `spm_resource_req` (only for deep idle).
Also: added `irq_ap_en_reg/shift` to `mtk_base_irq_data` and enabling it in trigger (like the vendor; not critical, EN is 0xffffffff anyway); the IRQ error is rate-limited (when AFE stops the IRQ line bounces, `status 0` — a storm of up to 12k interrupts).
Debug leftovers (remove): `CON0DBG` in `mtk-afe-fe-dai.c`, the `REGDBG2` loop (8 rounds), parameters `dbg_gate_set/dbg_gate_clr`, `force_apll`, `PMDBG`, `PTRDBG`.
Tools on the phone (`~`): `safe.sh`/`safe2.sh` — a **silent** test (aplay of zeros, `Headset Volume 0,0`, `HP Mux` then `Open`), `bisect.sh`, `tr.sh` (DAPM trace). `load-audio-safe.sh` now accepts `AFE_PARAMS="..."` for the afe module.
**SAFETY:** `sound-test.sh hp` plays a 440 Hz sine at `Headset Volume 6,6` (scale −10 dB, step 1 dB ⇒ close to the top) — that is LOUD. The user got a loud sound in the headphones. No tones into headphones without an explicit request; debug with zeros; keep the output at `HP Mux`=Open between tests.
Next: check real sound QUIETLY (tone −40 dBFS, volume 0, headphones not on the ears at first), then the TFA9874 speaker (stage 2), autoload, remove debugging, find out why CCF does not clear bit2.
**SOUND IN THE HEADPHONES CONFIRMED (2026-11-12 09:36):** quiet test `~/quiet.sh` (440 Hz, −40 dBFS, 1 s fade-in/0.5 s fade-out, `Headset Volume 0,0`, `HP Mux`=Audio Playback → then Open) — the user hears a clean tone, the fade is smooth. The AFE → MTKAIF → MT6359 → HP path works. Remaining: debug cleanup, autoload (dtb + modules-load), UCM/profile for PulseAudio/PipeWire, the TFA9874 speaker, microphones (`arecord`), `aud_tdm_clk`, the `ASoC error (-16)` noise on 0x052c/0xe40.

### 🔊 Audio — autoload and cleanup (2026-11-12 09:40)
**Autoload done:** `begonia-audio.service` (`/etc/systemd/system`, enabled; copy in `audio/begonia-audio.service`) runs `audio/audio-up.sh`:
insmod in the order mtk-common → mt6359 → mt6785-afe → mt6785-mt6359 from the build directories `audio/*/`, waits for the card, sets the AFE route (DL1→ADDA, `DAC In Mux`=Normal Path).
Loop protection like for WiFi: markers `/var/lib/begonia/audio-starting` / `audio-disabled` (clear: `sudo rm /var/lib/begonia/audio-disabled`). Runs after `begonia-wifi.service`.
**Safe default:** the headphone output is DISABLED (`HP Mux`=Open, `Headset Volume` 0,0). Enable: `hp-on [0..4]` (ceiling 4 = −6 dB on purpose), disable: `hp-off` (symlinks in `/usr/local/bin` → `audio/hp-on.sh`, `hp-off.sh`).
Verified: after a clean reboot the service is `active`, card `mt6785_mt6359`, PulseAudio sees the sink `alsa_output.platform-sound.stereo-fallback` (RUNNING while streaming, IRQ 184 counts); the user plays music.
**Debug cleanup done** (no `CON0DBG/REGDBG/PTRDBG/PMDBG/force_apll/dbg_gate_*` in the sources or the built .ko). Kept: hang-debugging parameters `stage/ungate0/ungate1` and `keep_intbus` (probe bisection), the `AUDIO_TOP_CON0` bit2 fix in `runtime_resume`.
Details: at probe the first `runtime_resume` runs with `pm_runtime_bypass_reg_ctl` (the regmap is not touched), so the bit2 reset takes effect at the first real stream; the PulseAudio sink is announced as 44100 Hz, the kernel accepts only 48000 (resampling in PA).
Next: microphone (`arecord`), UCM/volume (codec mixer → PA), the TFA9874 speaker, `aud_tdm_clk`, the `ASoC error (-16)` noise, find out why CCF does not clear bit2.
**Volume (2026-11-12 09:50):** the headphone output is "hot" — the comfortable level was at ~25–30% of the cubic mixer scale (≈ −31…−34 dB) with the codec at minimum (`Headset Volume` 0 = −10 dB, scale −10…+8 dB, 18 steps). Now `hw_params` does not overwrite the digital gain: on the first stream start after boot `dl_gain` is written (an snd-soc-mt6785-afe parameter, default 1308 = −34 dB, linear /65536), after that the value is taken from the register, i.e. it is controlled by the `ADDA_DL_GAIN` mixer (raw 0..65535) and survives stream starts. Before the first stream the register shows the vendor 63311 (0xf74f). Tuning on the fly: `amixer -c 0 cset name='ADDA_DL_GAIN' <raw>` (raw = 65536·10^(dB/20)). Suspicion: the codec analog path is hotter than normal — no way to check without measurements.

### 🔊 Volume: CODEC SCALE INVERSION — found and fixed (2026-11-12 10:16)
**Port bug in `codecs/mt6359.c`:** the `Headset/Lineout/Handset Volume` controls were declared with `invert=0`, while the hardware gain code decreases: `DL_GAIN_8DB=0 … DL_GAIN_N_10DB=18 … 0x1f=-40dB`. The ALSA index (TLV -10..+8 dB, increasing) mapped to the code directly ⇒ **index 0 = +8 dB (MAX), index 18 = -10 dB (min)**.
Consequences: the "minimum" (`Headset Volume 0,0`) in all my "quiet" tests was the MAXIMUM analog gain (hence the deafening tone and the "hot" output); the PulseAudio slider (HW_VOLUME_CTRL, drives Headset Volume on the -10..+8 scale) **reduced** the volume above ~50% — "peak at 50%, then it drops"; my `hp-on 0..4` ceiling ended up on the loudest values.
**Fix:** `invert=1` on the three controls + `mt6359_gain_to_idx()` in `mt6359_get_playback_volsw` (index = max − code; codes outside the scale → 0). Verified on PMIC register `0x260c` (debugfs `1000d000.pwrap`): index 0 → `0x0912` (code 18, -10 dB), index 18 → `0x0000` (code 0, +8 dB).
**Second finding:** pmOS has `alsa-restore.service`/`alsa-state.service` enabled — after every boot they restored the saved mixer (`HP Mux=Audio Playback`, `Headset Volume 0,0`, `ADDA_DL_GAIN=63311`) ⇒ the output turned itself on at maximum gain and my `dl_gain` got overwritten. The services are **masked** (`systemctl mask alsa-restore alsa-state`; to revert: `unmask`). Defaults are set by `audio-up.sh`.
**dl_gain recalculated:** 10876 (-15.6 dB). Calculation: the user's comfortable level was at a slider of ~27.5% (-33.6 dB) → PA computed sw=-23.6 dB at "hw -10", in reality hw was +8 ⇒ total -15.6 dB from full scale; now 100% of the slider = hw +8 dB, sw ≈ -8 dB, and the digital -15.6 gives the same comfortable maximum.
Lesson for all tests: before ANY sound check the register/dB value, not the name; index 0 is now -10 dB.

### 🔊 Volume: final tuning (2026-11-12 10:30)
The PulseAudio slider is bound **linearly** to the codec hardware steps: `/etc/pulse/default.pa` → `load-module module-udev-detect ignore_dB=1` (backup `default.pa.orig`), the sink has no DECIBEL_VOLUME flag, 0..100% ↔ `Headset Volume` 0..20. There is no software "stitching" ⇒ the rise is even (1 dB per step), the lower bound = the codec hardware minimum.
The `Headset Volume` range was extended downwards: `max=0x14`, TLV −12..+8 dB (`hp_playback_tlv`); index 0 → code 20 = −12 dB (reg. 0x260c = 0x0a14), index 20 → code 0 = +8 dB (0x0000). The bottom is 2 dB (~20% in amplitude) quieter, the top is unchanged.
`dl_gain` = **1740** (−31.6 dB): 100% of the slider = hw +8 dB without software attenuation, the digital gain is cut so that the peak stays at the "×0.4" level (4350 in the previous scheme).
History of the tuning: comfortable peak ≈ −23.5 dB from full scale at hw=0 dB equivalent; tried ×0.7 (7613) and ×0.4 (4350) of the 10876 base.
Live tuning: `amixer -c 0 cset name=ADDA_DL_GAIN <raw>` (raw = 65536·10^(dB/20)); the top moves together with the bottom (digital shift), the shape of the range (20 dB) is set by the `Headset Volume` range.
`hp-on [0..4]` now = −12..−8 dB in hardware; the PulseAudio slider then moves this control by itself.
**Volume result (2026-11-12 10:50):** the peak was chosen as ×0.5 of the 10876 base ⇒ `dl_gain` = **2165** (fixed in `mt6785-dai-adda.c`; with the scheme "slider = hardware steps 0..20, no software part" this is 10876·0.5·10^(−8/20)). Temporary logging in `audio-up.sh` removed (original restored; once the script got damaged during cleanup — fixed, `sh -n` ok). Diagnosis of "the output turned on by itself at boot": HP Mux is always Open after a reboot (does not persist), 40 s of observation — it does not change by itself; Headset Volume at start is set by PulseAudio (it restores the slider).

### 🎤 Microphone: works at the hardware level (2026-11-12 11:45)
**Path:** `Capture_1` = `hw:0,4` (UL1 → memif VUL12) ← `ADDA_UL_CH1/2` ← MTKAIF ← MT6359 ADC ← PGA ← **AIN0 (main microphone)**. The MTKAIF calibration passes: `MTKAIFCAL ok=1 chosen_phase=2/2/-1 phase_cycle=3/3/0` (now logged via dev_info; the debug line is kept).
**How the input was found:** `~/micscan.sh` — 5 segments of 3 s with different inputs, the user claps: only `AIN0` reacts (R: peak 6877 with noise 33; L: peak 10085). `AIN1`, `AIN2`, `AIN3` — silence/noise. In complete silence the left channel gives a flat 0 (not dead — it responds to claps), the right one — ADC noise around −60 dBFS.
**Route fixed in `audio-up.sh`:** `UL1_CH1/2 ← ADDA_UL_CH1/2`, `ADDA_UL_Mux=MTKAIF`, `UL_SRC_MUX=AMIC`, `ADC_L/R_Mux=Left/Right Preamplifier`, `PGA_L/R_Mux=AIN0`, `PGA1/2 Volume=2` (12 dB).
**PulseAudio:** the standard "stereo-fallback" profile is output only (looks at hw:0,0). Added: `audio/mt6785-mt6359.conf` (profile set: output hw:X,0 + input hw:X,4), `audio/mt6785-mic.conf` (minimal path), udev rule `/etc/udev/rules.d/90-mt6785-audio.rules` (copy of `audio/90-mt6785-audio.rules`, sets `PULSE_PROFILE_SET` by `ATTR{id}=="mt6785mt6359"`). Now there is `alsa_input.platform-sound.analog-stereo-in` (48 kHz) and the profile "Headphones + Internal Microphone".
Remaining: a voice check through PulseAudio, recording level/quality (PGA, noise, left channel = 0 in silence), the headset microphone (headset mic — a different input), the noise-cancelling microphone at the top (AIN1?).
**Verified through PulseAudio (11:50):** `parecord -d alsa_input.platform-sound.analog-stereo-in` for 20 s with speech/claps — events on R up to ~527 (rms/100 ms) with noise ~27 (−62 dBFS), on L up to 469, in pauses L = 0. Moderate level, there is PGA headroom (12 dB now; maximum 24 dB). Recordings deleted.
Microphone details: in silence the left channel is a flat 0 (not ADC noise but a gate/DC subtraction), the right one — noise at −62 dBFS; at the start of any recording a click of ~−16 dBFS for ~100 ms (MTKAIF start, cut it in applications/add a delay). Not verified: the headset microphone (a different input), the top noise-cancelling microphone.

### 🎤 Microphone: tuning finished (2026-11-12 11:55)
**Problem "very quiet / dead":** with PGA 12 dB in `IDLE` mode the path gave almost zero in silence, with PGA 24 dB it disappeared completely (noise dropped to 0 instead of rising) — a sign of the preamplifier saturating on a DC offset. Read the PMIC registers (`0x2508/0x250a` PGA, `0x2526` bias 1.9 V): the PGA values arrive, the bias is on, input AIN0 is selected — the electrics are correct.
**Solution:** microphone type **DCC** (DC offset calibration): `mic_type0=3` — now the default in `codecs/mt6359.c` (module_param, `-1` = from DT/idle). Result: a normal noise floor of ~−60 dBFS, L/R channels identical, a clap reaches full scale, speech is audible (peak ~−26…−28 dBFS at 12 dB PGA). After listening the user said: "no amplifier needed, there is noise (the phone is 8 years old), it will not get better".
**Final capture parameters:** PGA 12 dB (`PGA1/2 Volume=2`), PulseAudio source 100%, DCC. The experimental software +12 dB and PGA 24 dB were reverted.
`audio-up.sh` can pick up module parameters from `/var/lib/begonia/params.<module-name>` (e.g. `params.snd-soc-mt6359` with `mic_type0=1` to experiment with another mode); currently there is no such file.
The test recordings were in `Begonia/wifi/audio/clips/` on the PC (delete if you like). Not verified: the headset microphone, the top noise-cancelling microphone; the ~100 ms click at the start of recording.

### 🔊 Speaker (SmartPA TFA9874) — POSTPONED (2026-11-12 12:10)
The user decided: not now (speaker tests are audible to everyone around, not possible at school/in public). Do it when making noise is fine, preferably alone; before any sound from the speaker — warn, minimum volume, digital silence first.
**Plan (not started):**
1. Vendor kernel (`kernel-r-oss`, `git show HEAD:...`): how TFA9874 is brought up — the `tfa98xx` driver (i2c6@0x34), power/reset GPIO24, irq GPIO69, audio over I2S3 (GPIO67 LRCK / 68 DO / 71 BCK), whether firmware is needed (`.cnt`/`tfa98xx.cnt`/config), the power-up order (PA after I2S).
2. Decide: port the vendor `tfa98xx` or extend mainline `tfa989x` (knows 9890/91/95/97, not 9874) — estimate the size, whether speaker calibration/protection exists (important: without protection the speaker can burn).
3. Add a BE link I2S3 → TFA9874 to `mt6785-mt6359.c` (the card now has only ADDA/ADDA_CH34), DT: i2c node, reset/irq, pinctrl for I2S3, `aud_tdm_clk` (not there yet), MCLK/BCLK clock for I2S3 (`top_apll_i2s3`, `apll12_div3` are already in DT).
4. Route: DL1 → I2S3 (output mux), UCM/profile for PulseAudio (speaker as port `analog-output-speaker`), a volume limiter in the driver/profile.
5. Tests: first `aplay /dev/zero`, then a quiet −40 dBFS tone at minimum volume, keep the phone away from the ear.
Other audio leftovers: headset microphone/top noise suppression, `aud_tdm_clk`, `ASoC error (-16)` on 0x052c/0xe40, 44100 vs 48000 in PulseAudio, the ~100 ms click at recording start, the reason the `aud_afe` bit is not taken through CCF.

## 🔋 BATTERY + BUILDING THE IMAGE FROM SOURCES (2026-11-12, end of day) — STATUS: works, installed on the phone
**Before:** `adc-ocv-battery.ko` (voltage-based estimate) — an inherently inaccurate hack: the MT6360 IBAT ADC sees only the charge current, not the discharge current, the IR compensation lied (capacity jumped 19% → 59% at the same 3.92 V); a race at probe (the charger was looked up once → status forever Discharging). Plus the charger input current limit of 500 mA with a 5 V/2 A PD source (`input_current_limit=500000`, BC1.2 for a PD cable gives SDP) → the phone discharged under load while charging.
**Now:** a real fuel gauge on the MT6359 hardware FGADC. Everything is built from sources, no more manual binary DTB edits or substituted `.ko` files.

### Where things are
- `Begonia/pmos-build/` — **the main place**: `README.md`, `kernel/patches/0001..0007` (series on linux `6.16.4-r0`), `pmaports/device/testing/linux-postmarketos-mediatek-mt6785/{APKBUILD,config}` (overlay, `pkgrel=3`), `scripts/{export-patches,apply-overlay,build-kernel,deploy-kernel}.sh` + `gen-source-list.py`.
- Source of truth for the kernel: the **`begonia-fixes`** branch in `~/mt6785-linux` (WSL), base — tag `6.16.4-r0`. Backup of the first (failed, with a DT cycle) series — branch `backup-v1`. Make changes as commits in `begonia-fixes` → `export-patches.sh` → `apply-overlay.sh`, do **not** patch the DTB by hand.
- Old stuff: `pmos-begonia/battery/_obsolete/` (`adc-ocv-battery.*`, `mt6359-gauge-out-of-tree/` — an intermediate out-of-tree version). `battery/patch_dts_battery.py`, `bat*.dts` — also history.

### Series contents
| # | What |
|---|---|
| 0001 | `drivers/power/supply/mt6359_fgadc.c` + Kconfig/Makefile + MFD cell `mt6359-fgadc` in `mt6397-core.c` + binding `mediatek,mt6359-fgadc.yaml`. Regmap from the parent (`struct mt6397_chip`), the OCV and R tables come from `simple-battery` in DT, nothing is hard-coded. |
| 0002 | `mt6360_charger.c`: a psy notifier — takes `CURRENT_MAX` of another USB psy (`tcpm-source-psy-*`) as AICR, returns 500 mA on `ONLINE=0`; ICHG and CV from `monitored-battery`. |
| 0003 | DTS: `vtp` (MT6360 ldo2) always-on 3.0 V + `vcc-supply`/`iovcc-supply` of the touchscreen; `proximity-near-level` 70→550. |
| 0004 | DTS: `battery` (simple-battery, OCV table in 2% steps from the vendor `battery0_profile_t1`, 25 °C, R=125 mOhm, ICHG max 2.5 A, CV 4.35 V), node `fuel-gauge` under `&pmic`, labels `mt6360_adc`/`mt6360_charger`. |
| 0005 | DTS: `consys-begonia.dtsi` (+ `wifi@18000000`, `wifi-reserve-memory`, which were not in the original dtsi — they lived only in the binary DTB). |
| 0006 | DTS: `audio-begonia.dtsi` (AFE + sound). The labels are in `mt6785.dtsi`, `make-audio-dtb.sh` with after-the-fact labelling is no longer needed. |
| 0007 | `novatek-nvt-ts-core.c` (v3 of the touch fix: probe retry, reading the parameters relative to the event buffer, IRQ type from DT). |

Config: `CONFIG_BATTERY_MT6359_FGADC=y` (built in, not a module).
Equality check: the DTB from sources against the live DTB (with reference properties masked) differs **only** in the intentional battery/charger nodes and phandle renumbering.

### How the gauge works (MT6359 registers, useful for debugging)
- `FGADC_CON0 0x0d08` bit0 FG_ON (LK already enables it). Reading uses a latch via `FGADC_CON1 0x0d0a`: SW_READ_PRE (bit0) → wait for LATCHDATA_ST (bit15) → read `CUR_CON0 0x0d8a` (s16, **+ = charging**), `CAR_CON0/1 0x0d16/0x0d18` (32 bits) → SW_CLEAR (bit3) → clear SW_READ_PRE → wait for LATCHDATA_ST=0 → clear SW_CLEAR.
- Current LSB 610.352 µA (5 mOhm shunt), charge LSB 0.085 µAh. Source: vendor `drivers/misc/mediatek/pmic/mt6359/v1/mt6359_gauge.c`, constants `UNIT_FGCURRENT`/`UNIT_CHARGE`.
- Check: FG current against the MT6360 IBAT ADC ±3%; ΔCAR against ∫I over 42 s: 3969 vs 3946 µAh (0.6%).
- Estimation: `discharged_uah` is tracked from ΔCAR, the starting point and slow correction come from OCV (OCV = V − I·R), correction only at |I| < 150 mA (stronger at < 50 mA), FULL status from the charger → 100%. Voltage — IIO channel of the MT6360 ADC (channel 4, VBAT). Usable capacity down to 3.4 V per the vendor profile is 4386 mAh; DT has `charge-full-design=4400000`.
- IIO MT6359 AUXADC `batt_v` gives garbage (raw 55025), and `bat_adc` (type IIO_RESISTANCE!) = VBAT·32768/6300 — in mainline the channels are named oddly, so MT6360 is used.

### Pitfalls (in the order I stepped on them)
1. **Probe cycle because of `power-supplies` in the charger DT.** I set `power-supplies = <&mt6360_tcpc>`. `power_supply_check_supplies()` defers the psy registration until the supplier is registered; the psy `tcpm-source-psy` appears only after the tcpc probe, and tcpc/USB waits for the vbus regulator **of this very charger** → `mt6360-chg: Failed to register power supply dev` (deferred), no battery, USB does not come up. Fix: remove the link from DT, take the limit with a psy notifier (0002). The phone was temporarily patched with `fdtput -d`; not needed now.
2. `$VAR` inside `wsl -d Ubuntu -- bash -lc '...'` is expanded **before** bash (wsl.exe wraps it in a shell) → empty variables. Pass scripts through stdin: `wsl -d Ubuntu -- bash < script.sh`.
3. On Windows python is only available as `python` (not `python3`); a `%` character in a comment inside a string with `%` formatting breaks the substitution (escape it as `%%`).
4. `pmbootstrap` (even `checksum`) requires `sudo` with a password → the user runs `sh scripts/build-kernel.sh` himself; the kernel tarball is ~250 MB, it downloaded at ~0.4 MB/s.
5. Patch names from `format-patch` depend on the commit subjects → the list in `source=` is generated (`gen-source-list.py`), do not maintain it by hand. `apk add` of the same version will not reinstall → bump `pkgrel`.
6. A wrapper script with `set -e` and `! grep` does not stop on a build error (that is how I committed code with a non-existent `POWER_SUPPLY_NAME_MAX`) — check with `if grep ...; then exit 1; fi`.
7. `git checkout <file>` with unsaved user changes is blocked by the auto classifier — back up first (`~/dts-backup/`), then `git show HEAD:path > path`.

### Build and deploy (how to repeat)
```
# WSL, from Begonia/pmos-build:
sh scripts/export-patches.sh     # if the kernel was touched: begonia-fixes commits -> kernel/patches
sh scripts/build-kernel.sh       # apply-overlay (+checksum) and pmbootstrap build; asks for sudo
sh scripts/deploy-kernel.sh      # apk to the phone; the trigger reflashes boot by itself (deviceinfo_flash_kernel_on_update)
```
I deployed by hand: `scp` apk → `sudo apk add --allow-untrusted /tmp/kernel.apk` → `sudo systemctl reboot`. Backup before installing: `/root/boot-backup` on the phone (`boot.img`, `initramfs`, `vmlinuz`, `dtbs`, `*.dtb.stock`, `*.dtb.hotfix`, `*.dtb.r2-package`). Rollback: fastboot images `pmos-begonia/boot*.img` or `apk add linux-...=6.16.4-r1`.

### Verified on hardware (r3, 17:25–17:35)
Boot without patches; `battery` (OF `/soc/pwrap@1000d000/pmic/fuel-gauge`); ICHG=2.5 A applied; the sign of the current on discharge is correct (−0.6…−1.1 A under load); on connecting PD charging `INPUT_CURRENT_LIMIT` became 2 A by itself (tcpm CURRENT_MAX), `current_now=+1.3 A`, status Charging. Touch, WiFi (`wlan_6785_axi`), audio modules in place.

**NOT verified:** AICR going back to 500 mA when the cable is pulled out; booting with the cable already plugged in (the notifier may miss a tcpm event that came before the charger probe — there is no initial scan, since the kernel has no `power_supply_for_each_device`); temperature compensation (the table has only 25 °C; the MT6359 AUXADC has a `batt_temp` channel); keeping the capacity across reboots (after a reboot SOC comes from OCV again, ±3%); Qmax learning; FULL status/end of charge; behaviour at low charge (shutdown).

### Network (important)
Connecting to the phone over WiFi — only `10.x`. On 2026-11-12 ~17:10 the network changed: PC `10.30.255.140`, phone **`10.30.255.121`** (before `10.233.161.187`). If the address does not answer — `ssh user@minilin.local` (mDNS) → `ip -4 -o addr show wlan0`. `172.16.42.1` — USB, do not use.

### Spec for the next stage (the driver layer, NOT started)
Separately from the image: the modules `consys-bringup`/`consys-gps`/`wlan_6785_axi` (`wifi/module`, `wifi/gen4m-port`), the AFE audio modules (`wifi/audio`: `snd-soc-mtk-common`, `snd-soc-mt6785-afe`, `snd-soc-mt6359`, `mt6785-mt6359`), the services `begonia-wifi.service`/`begonia-audio.service`, udev (`90-mt6785-audio.rules`, `90-stk3310.rules`), UCM/PulseAudio configs (`mt6785-mt6359.conf`, `mt6785-mic.conf`), firmware and `wifi.cfg` in `/lib/firmware`. The modules are built against `~/mt6785-linux` (the same `.config` and vermagic `6.16.4 SMP preempt mod_unload aarch64` are needed; there is no `Module.symvers` in the tree, hence `KBUILD_MODPOST_WARN=1`). Idea: one install script/package `pmos-build/drivers/` (build + install + enable the services); remove `fix-boot-dtb.sh` and `apply-dtb.sh` (the DTB now comes from the kernel package).

### 🔊 TFA9874 speaker: reconnaissance (2026-11-12, evening, NO sound)
- Vendor DT (`mt6785.dts`, `&i2c6`): `tfa98xx@34`, compatible `mediatek,speaker_amp`, reset GPIO24, irq GPIO69; in `sound`: `mtk_spk_i2s_out=<3>`, `mtk_spk_i2s_in=<0>`, `speaker-codec -> tfa98xx`.
- Vendor driver: `sound/soc/codecs/tfa98xx/` (tfa98xx.c 4149 lines + tfa_dsp.c 4285 + tfa_container.c 2603 + tfa_init.c 1732). The firmware `tfa98xx.cnt` is loaded through request_firmware.
- **`tfa98xx.cnt` was extracted from vendor.img (`/firmware/tfa98xx.cnt`, 646 bytes) → `audio/tfa-fw/`** (+ `mono.cnt` 12535 bytes, a different file, not for the speaker). Container `PM3_01`, device TFA9874, patch `9874_dummy.patch` (a stub), one profile `speaker`, inside ~80 register/value pairs + a list for the profile. There is NO DSP firmware and no speaker model ⇒ the port boils down to register initialization (in the spirit of mainline `tfa989x`), without a blob.
- Next: decode the register list using `tfa9874_tfafieldnames.h`, decide: extend `tfa989x` (+9874) vs. a separate minimal codec driver; the I2S3 BE link in `mt6785-mt6359.c`; DT/pinctrl; protection (volume limit, boost/current limit from the profile).

### 🔊 TFA9874 speaker: driver written and probed, THE AMPLIFIER HAS NOT BEEN ENABLED YET (2026-11-12 18:05)
**What was done (no sound):**
- The `tfa98xx.cnt` container is decoded (`audio/tfa-fw/decode2.py`, `check_table.py` checks the table in C against the container: 48/48 fields). Conclusion: there is no DSP speaker model, the amplifier lives on hardware protection; start-up = registers from the container + `tfa9874_specific()` by revision.
- New driver `audio/codecs/tfa9874.c` (`snd-soc-tfa9874.ko`, ~500 lines, not a port of the vendor one): I2C regmap (8-bit address, 16-bit BE value), reset GPIO24 (active HIGH, 1→10 ms→0→10 ms), REV check. **The hardware answers: `TFA9874 found, REV=0x0c74` (V1.16, i2c6@0x34).** Start/stop — the DAPM widget `Speaker Amp` (POST_PMU: config→INIT_CF→wait for CLKS→AMPE=1; PRE_PMD: AMPE=0, PWDN=1). If there is no BCK the amplifier stays off, the stream does not fail.
- **Safety fuse:** parameter `allow_amp` (default 0). While it is 0 the amplifier does not turn on for any stream. Enable temporarily: `echo 1 | sudo tee /sys/module/snd_soc_tfa9874/parameters/allow_amp`. Change the default in the code after the check.
- Machine driver: BE link `Speaker` (I2S3 ↔ `nxp,tfa9874`, bound by DT node — the i2c bus number floats, no aliases), BE fixup → S32_LE/48000/2 (the container expects a 32-bit slot), widgets/routes/controls with `Speaker` (`Speaker Switch`). If `tfa9874` did not load — the link is cut off (`num_links--`) and the card lives without the speaker.
- DT (`audio-begonia.dtsi`, a commit in the `begonia-fixes` branch, patch `0008-...TFA9874...`): i2c6 (GPIO45/46), node `audio-codec@34`, reset GPIO24, irq GPIO69 (not used yet), pinctrl states `aud_gpio_i2s3_on/off` (GPIO67 LRCK / 68 DO / 71 BCK). The DTB is installed in /boot + `mkinitfs` (boot.img and the boot partition updated, backup: `/root/boot-backup/*.r3-pre-speaker`). The kernel package (`pmos-build`) is NOT rebuilt yet — patch 0008 will be picked up by the next build-kernel.sh.
- `audio-up.sh` loads `snd-soc-tfa9874` as an optional module between the AFE and the machine driver.

**Pitfalls found on the way (important):**
1. **`BUG: scheduling while atomic`**: `mute_stream` with `mute_unmute_on_trigger` is called from trigger (atomic context), while I2C sleeps. For an I2C codec in a DPCM BE, power must go only through DAPM events (process context), not through trigger.
2. **`AFE_TINY_CONN5` (0x52c) after reset = DL1 for I2S1/I2S3** (`NONE` = 0x1f): `I2S3_TINYCONN_CH1/2_MUX` pull DL1 by default, i.e. ANY playback (to the headphones) got attached to the speaker BE and broke the PulseAudio stream start. Fixed in `mt6785_mt6359_late_probe()` (DAPM put NONE: updates both the register and the paths) + writing the cache in `afe_dev_probe` + a safety net in `audio-up.sh`. Reading 0x52c at card registration bypasses the cache (-16, that very "noise"), which is why DAPM otherwise considered the mux connected.
3. vermagic: `include/generated/utsrelease.h` in the WSL tree can get stuck at `6.16.4+` (dirty git tree) → `insmod` will refuse. Regenerate: `make ARCH=arm64 LLVM=1 LOCALVERSION= include/generated/utsrelease.h`, then `make clean && make` in audio/.
4. Turning the speaker on MANUALLY: `amixer -c 0 cset name='I2S3_CH1 DL1_CH1' on` (+ `I2S3_CH2 DL1_CH2`), off by default. The mono speaker takes slot 0 (TDMSPKS=0, left channel).

**State at 18:05:** the phone rebooted with the new modules/DTB; the headphones work as before (the PulseAudio sink is alive), no speaker events at boot, `allow_amp=N`.
**Next step (permission to make noise needed!):** 1) allow_amp=1 + route + `aplay /dev/zero` (digital silence; a quiet click is possible when the amplifier turns on) → check `dmesg` (CLKS, STATUS 0x10/0x11, TDMERR/NOCLK, OCDS/OTDS/UVDS); 2) a −40…−50 dBFS tone, phone away from the ear; 3) PulseAudio profile `analog-output-speaker` + scripts `spk-on/spk-off`; 4) default `allow_amp`, mixing L+R into slot 0, protection via irq GPIO69.

### 🔊 TFA9874 speaker: FIRST AMPLIFIER START (2026-11-12 18:15)
- Test route: `Speaker Switch`=on (off by default!) + `I2S3_CH1 DL1_CH1`/`I2S3_CH2 DL1_CH2`=on; `allow_amp=1`. Scripts on the phone: `~/spk-zero.sh` (digital zeros), `~/spk-quiet.sh` (quiet.wav, 440 Hz −40 dBFS, 6 s); both roll back the route/allow_amp/Speaker Switch by themselves and suspend the PulseAudio sink for the duration of the test.
- **Cause of NOCLK:** the AFE did not open the BCLK gates of I2S1..I2S5 (`AUDIO_TOP_CON1` bits 4..8, clock `aud_i2s3` in audsys). Fix: mask `0x71f0` instead of `0x7000` in `mt6785_afe_enable_clock` and `runtime_resume` (the same trick as for ASRC: CCF does not clear these bits). After that `CLKS=1`, `MANSTATE=9`, `AMPS/SWS/AREFS=1`.
- **Status bits 0x10 in the tfa2 family: 1 = NORMAL** (vendor: `if (!UVDS || !OTDS || !CLKS || !PLLS ...) error`). My labels "OVERHEAT/UVP" in the driver log are inverted — fix at the next build. Real state: power ok, temperature ok, no OCP, no TDMERR.
- A −40 dBFS tone was played through the speaker, the amplifier was stable for the whole stream (0x10=0016, 0x11=e280). **CONFIRMED by the user (18:16): "the sound was smooth"** — the AFE → I2S3 → TFA9874 → speaker path works. Louder than −40 dBFS not tried yet.

### 🔊 Speaker: system integration (2026-11-12 18:27) — STATUS: works, switchable from PulseAudio
- The user confirmed: a −30 dBFS tone is "quite even and without noise" (−40 and −30 listened to; louder than −30 dBFS not tried; −20 steps not needed, "enough").
- **PulseAudio:** two ports in one sink: `analog-output-headphones` (priority 9900, default, Headset Volume 0..20, kills the speaker route) and `analog-output-speaker` (priority 100, software volume). Path files `audio/analog-output-headphones`/`-speaker` (.conf; in `paths-output` the names go WITHOUT `.conf` — PulseAudio appends `.conf` itself, with the extension it became `.conf.conf` and the paths were empty), profile `mt6785-mt6359.conf`. Commands: `spk-on [%]` (default 30, ceiling 60) and `spk-off` (symlinks in /usr/local/bin → audio/spk-on.sh, spk-off.sh).
- **Mono:** input `DL1_CH2` added to the `I2S3_CH1` mixer → slot 0 gets L+R (+6 dB on mono content, accounted for in the volume ceiling).
- **Main pitfall:** while the `Speaker` pin is connected, DPCM attaches the speaker BE to ANY DL1 stream (the DAPM flags of the mixer paths at card registration are stale: register reads bypass the cache). It was not visible before because the stock PulseAudio path switched `Speaker` off. Now the card `late_probe` disables the `Speaker` pin by default; only the speaker path enables it. The amplifier is on only during a stream (DAPM), then it goes down (`before stop` in dmesg).
- `allow_amp` defaults to 1 (the safety fuse stays as a module parameter). Status labels fixed (1 = normal for OTDS/UVDS).
- Ceiling for tests: −30 dBFS; `spk-on` is limited to 60% (≈ −13 dB in software ⇒ with L+R about −7 dBFS at full-scale content — this has BARELY been tested, raise in steps).
- Not done: irq GPIO69 (protection on alarm), headset microphone, noise suppression, a kernel package with patch 0008 (the DTB is still installed by hand; `build-kernel.sh` will pick it up).

### 🔊 Speaker: volume, reverberation, distortion (2026-11-12 18:40)
- The user: at ≥60% PulseAudio there is an unpleasant "reverberation" (none at 50% and below), the speaker "rasps from the power", take care of it.
- Amplifier monitoring (debugfs `/sys/kernel/debug/tfa9874-status`, script `~/mon2.sh`, 3 min of music, 310 snapshots): **the amplifier did not complain** — reg10=0016 for the whole stream (power/temperature normal, no NOCLK/TDMERR), `CLIPS=1` only in 2 snapshots of 310, the DC-DC (reg11) switches normally e280..e2bb. The OVDS/OCPO flags in reg13 are constant (0x850f), not related to alarms. Conclusion: the reverberation/rasp is the **mechanical limit of the speaker itself** (a small phone speaker at high power + possible rattling of the case), not an amplifier fault.
- Decision: `spk-on` ceiling = **50%** (in the script); do not push the slider higher by hand. There is no hard ceiling in the system itself (the KDE slider can go to 100%) — possible options: a software limiter/port volume ceiling in PulseAudio, or reducing the amplifier AMPGAIN (the scale is not studied, needs experiments).
- Driver: added a debugfs snapshot of the amplifier registers (`tfa9874-status`).
- **Volume result (18:45):** the user by ear: peak without distortion "just below 50%", the speaker is old. `spk-on` ceiling = **45%**, default 30%. Do not raise the system slider higher.

### 🔊 Speaker: hardware volume ceiling (2026-11-12 18:52)
- On the user's request "pin the old 50% as 100%": the speaker volume is now **in hardware** — the ALSA control `Speaker Volume` of the tfa9874 driver (AMPGAIN), PulseAudio drives it with the slider (port `analog-output-speaker`, `volume = merge`).
- **Calibration** (a tone through the speaker, recorded with the built-in microphone, `~/cal.sh`, `audio/tfa-fw/rms.py`, recordings in `audio/clips/cal/`): the output amplitude is **strictly proportional** to the AMPGAIN value (≈1.14·G in microphone rms units: G=190→217, 150→171, 110→125, 90→103, 70→80, 50→57, 10→12). The scale is linear in voltage, NOT 0.25 dB/step. The vendor 0xbe=190 is full scale.
- The control is logarithmic: index 0 = silence, 1..56 = −45.5…−18 dB from the vendor maximum, 0.5 dB step (table `tfa9874_vol_gain[]`, G=1…24). **100% of the slider = index 56 = −18 dB = G=24 = the old PulseAudio "50%"; the system will not go higher**. The default start is index 32 (−30 dB).
- `spk-on [%]` (default 70, 100% = the safe maximum), `spk-off`. PulseAudio paths: speaker has `[Element Speaker] switch=on volume=merge`, headphones have `switch=off volume=ignore` (the former `volume = off` would reset the control to 0).
- Kept: the coefficients and the ceiling were chosen by the user's ear (the speaker is old, it rasps above ~−18…−21 dB).

### ⚠️ KDE crash / hangs: AFE INTERRUPT STORM (2026-11-12 19:05–19:22) — found and fixed
- **Symptom:** when playing tones (`spk-tones`, 15 s) the whole KDE session crashed at the very END of playback (plasmashell SIGBUS, kded6/powerdevil/... SIGABRT, `systemd-journald` ABRT), the phone "froze" for minutes. Log: `rcu: INFO: rcu_preempt detected stalls on CPU 0` and `mt6785_afe_irq_handler(): irq status err, status 0x0 ... 1637977 callbacks suppressed`. `/proc/interrupts`: `asys-isr` (IRQ 184) collected 68 million (later 125 million) interrupts on CPU0.
- **Cause:** at the end of a stream (after a preceding RECORDING in the session, sporadic, ~2 of 5 cycles) the AFE IRQ line stays asserted, while `AFE_IRQ_MCU_STATUS` reads as 0 (dump: status=0, en=0xffffdffe, dsp/scp/dsp2_en=0, dac_con0=1). The handler returned IRQ_HANDLED without clearing the cause → the level-triggered line fires forever. **The line is held by the "missed" interrupt flag: fixed by writing `AFE_IRQ_MCU_MISS_CLR=0xffffffff` + `AFE_IRQ_MCU_CLR=0xffffffff`.** (Before, this was visible only as the "-16 noise" and rare storms of up to 12k — now, with long streams on I2S3, it grew into a disaster.)
- **Fix** (`mt6785-afe-pcm.c`, `mt6785_afe_irq_handler`): on every empty interrupt write MISS_CLR+CLR right away; an `IRQSTORM:` register dump on the 100th in a row; safety fuse: on the 5000th in a row the line is disabled (`disable_irq_nosync`) and comes back in `runtime_resume` after clearing the statuses. Fields `irq/irq_spurious/irq_off` in `mt6785_afe_private`. Verification: 5 cycles of "recording + 15 s playback" under a watchdog — no storm, ~226 interrupts/cycle.
- Other plasmashell crashes (11.11 17:55, 18:21; 12.11 11:34 elisa, 14:52, 15:26, 18:37) — old, before/independent of the speaker; a separate cause (not investigated).
- **Lesson:** any long sound test — only under a watchdog (script `~/repro.sh`: reads the `asys-isr` rate and kills `aplay` at >100000/s). I myself ran a test that could hang the phone while the user was working on it.

### 🎛 TUI visualizer (2026-11-12 19:30)
- Installed `cava` 1.0.0 (`sudo apk add cava`, + sdl3/iniparser as dependencies). Config `~/.config/cava/config`: input `pulse`, `source = auto` (monitor of the current sink: shows both headphones and speaker), output `noncurses` (spectrum right in the terminal), 30 fps, green→red gradient. Run: `cava`, quit: `q`. Microphone instead of output: `source = alsa_input.platform-sound.analog-stereo-in` in the config.
- No equalizer was installed (a mix-up: the user meant a visualizer); the 100–200 Hz bass rasp is still without a HPF.

### 🔊 High-pass filter for the speaker (2026-11-12 19:50) — STATUS: works, measured
- Problem: the 100–200 Hz bass rasps at any volume (confirmed by ear on different tracks; measurement: at 400 Hz the 2nd/3rd harmonics are −26/−22 dB). The speaker has no excursion protection (the vendor DSP is not used).
- Solution: a HPF in the PulseAudio chain. Installed `ladspa` and `lsp-plugins-ladspa` (+9 MB). `spk-on` loads `module-ladspa-sink` (`sink_name=speaker_hpf`, `sink_master=<card>`, plugin `http://lsp-plug.in/plugins/ladspa/filter_stereo`) and makes it the default sink; `spk-off` removes it. The volume slider drives the common (flat) volume: the hardware `Speaker Volume`, the −18 dB ceiling is kept.
- Plugin parameters (order of the control inputs): bypass, in_gain, out_gain, fmode(global), fft_react, shift, zoom, 4×fft_enable, balance, **type(1 = high-pass; 0 = low-pass!)**, **mode(2 = Linkwitz-Riley)**, **slope(2)**, **freq**, width, gain, Q, decramp → `control=0,1,1,0,0.2,1,0.0266,0,0,0,0,0,1,2,2,<Hz>,0,1,0,0`. Tuning: offline `applyplugin` on tones (scripts `~/sweep.py`, `~/sweep2.py`).
- **Measurement on hardware** (−20 dBFS tones through the speaker, recorded with the microphone, cutoff 250 Hz, same amplifier level): 100 Hz −15 dB (further it hits the microphone noise), 200 Hz −10 dB, 400 Hz ±0, 800–3200 Hz ±0. The 400 Hz harmonics (−27/−23 dB) are the physics of the speaker, they stay.
- Commands: `spk-on [%]`, `spk-off`, `spk-hpf <Hz 20..600 | off>` (reloads the filter on the fly). Default cutoff 250 Hz (env `SPK_HPF`).
- **Another pitfall:** `SOC_DAPM_SINGLE_AUTODISABLE` on the `I2S3_CH1/CH2` mixers — they switch themselves off after the first stream, the speaker route disappeared; made them plain `SOC_DAPM_SINGLE` (the route is permanent; the speaker is gated by the `Speaker` pin).
- **Confirmed by the user (19:55):** with the 250 Hz HPF "everything became fine", the bass rasp is gone.

### 🛡 Speaker amplifier protection (2026-11-12 20:05) — STATUS: done and verified by simulation
- Decision: **a watchdog polling the status** (not the GPIO69 IRQ): the semantics of the interrupt registers 0x40..0x4c are unknown without a datasheet, and a mistake there = a storm/silent protection (we already had an interrupt storm). Status 0x10/0x11 is reliably known (in the tfa2 family 1 = NORMAL for DCOCPOK b1 / OTDS b2 / UVDS b4; fault = 1 for OCDS b3 / MANALARM b5 / TDMERR b6 / NOCLK b7; 0x11: b7 CLKS). The chip's hardware protection (OCP/OTP/UVP) works by itself, the watchdog is supervision on top.
- What it does (`tfa9874.c`): while the amplifier is on, every 100 ms it reads 0x10/0x11. **Fault** (2 bad polls in a row: overheating, supply drop, OCP, DC-DC, MANALARM, TDMERR, 3 i2c errors): AMPE=0 + PWDN=1, `PROTECTION: FAULT #n …` in dmesg, a 3 s pause, then an **automatic retry** of the start (as long as DAPM holds the stream); **3 faults within 60 s → latch**: the amplifier does not turn on until reset. **I2S clock lost** (3 bad polls): a soft shutdown without counting faults.
- debugfs: `/sys/kernel/debug/tfa9874-status` (first line: faults, latch), `/sys/kernel/debug/tfa9874-fault`: `echo reset` — clear the latch and counters, `echo inject` — simulate a fault (protection check). A reboot/module reload also resets it.
- **Verified** (simulation, a real fault cannot be provoked): no false triggers over 15 s of tones; inject at 1/6/11 s of one stream: shutdown → after 3.1 s "trying to start again", `up+5ms` → faults #1, #2, #3 → LATCH (`latch=1`), the next start is blocked ("amplifier blocked by protection…"), after `reset` the amplifier starts again.
- Side observation: with `spk-on` the PulseAudio→ladspa-sink→card chain keeps the hardware stream open for another ~15 s after silence, so the amplifier (in silence, noise at the level of being off) stays on for up to ~15 s after the last sound.

### 🎤 Top microphone (noise suppression) found and connected (2026-11-12 20:30)
- **The top (noise-cancelling) microphone = AIN2.** Directional `mic-scan` test (tapping near the top for 5 s / near the bottom for 5 s, energy top−bottom): AIN0 (main) −20 dB (louder at the bottom), **AIN2 +18.2 dB (louder at the top)**, AIN1 and AIN3 are silent (peaks −86/−80 dBFS; the earlier "responses" of all inputs were pickup from claps). Noise floor of AIN2 −78 dBFS (AIN0 −85).
- For this the codec got parameters `mic_type1` and `mic_type2` (default −1 = do not touch); `/var/lib/begonia/params.snd-soc-mt6359` on the phone has `mic_type0=3 mic_type1=3 mic_type2=3` (DCC, micbias2 offset). Route: `PGA_L_Mux=AIN0` (bottom), `PGA_R_Mux=AIN2` (top); `PGA_R_Mux` has no AIN1 (only None/AIN2/AIN3/AIN0), `PGA_L_Mux` — AIN0/AIN1.
- **Dual-microphone noise suppression** (`audio/mic-nc/`): `mic-nc-dsp.c` — spectral subtraction in the frequency domain (STFT sqrt-Hann 512/256), the noise of the bottom channel is estimated as `N_k = c_k · P2_k` from the top one, the coefficients `c_k` are calibrated on room silence; `G_k = clamp(1 − α·N_k/P1_k, GMIN, 1)` with smoothing; stdin stereo (L bottom, R top) → stdout mono. Control: `mic-nc calibrate | start | stop | status | test` (script `mic-nc.sh`, the processor is built on the phone with `gcc -O2`, installed in `/usr/local/bin`). While running, `start` switches `PGA_R_Mux` to AIN2, creates the "noise-suppressed microphone" source (`mic_nc`, default) via `module-null-sink` + `module-remap-source` and the pipeline `parec | mic-nc-dsp | pacat`; `stop` restores everything (AIN0 route, default source).
- Verified without speech: calibration (c_k smoothed over ±4 bins), start, a 3 s recording from `mic_nc`, stop; in silence the background is suppressed by ~22 dB (−85 → −107 dBFS). Speech and real noise NOT yet verified (`mic-nc test`: talk for 5 s, stay silent for 5 s, it shows the noise suppression and speech preservation).

### 🎤 Noise suppression: debugging and result (2026-11-12 21:30) — STATUS: works, verified on synthetic signals
- The user's first live test (`mic-nc test`) showed "speech −18 dB, noise went up": it was NOT the algorithm but the PulseAudio chain. Found and fixed:
  1. **Output delay of 2–7 s.** The `module-null-sink`+`remap-source` source has a fixed "configured latency" of 2 s; replaced with `module-pipe-source` (FIFO `/tmp/mic_nc.fifo`, the processor writes straight into it). On a synthetic input the chain delay is 0.01 s.
  2. **FIFO stall:** while the source has no reader, PulseAudio stops reading the FIFO, the write blocks, the queue fills up, and a reader that connects gets stale data (~7 s). Fix in `mic-nc-dsp`: mode `NC_LIVE=1` (set by the script) — non-blocking output, pipe size 4 KB (`F_SETPIPE_SZ`), a frame is dropped when the pipe is full. **Final delay with the real microphone 0.06 s** (8 bursts, the same offset).
  3. **Loop:** PulseAudio `stream-restore` moved the pipeline read stream back to `mic_nc` (it read itself → silence), plus `set-default-source mic_nc` moved it away on change. The script attaches the streams (`pactl move-source-output`) AFTER changing the default source.
  4. **`spk-on`/`spk-off` dragged the noise suppressor output stream** to the speaker (the "move all streams" loop); now they skip streams on `mic_nc_sink` (after switching to pipe-source this case disappeared, the check is kept).
- **Check on a synthetic "voice"** (bursts through the speaker at the bottom of the phone, the bottom microphone ~30 dB louder than the top one, noise suppression on, live pipeline): speech −25.0 → −25.0 dBFS (no loss), background −83.5 → −97.6 dBFS (−14 dB), the output is aligned in time with the microphone.
- Diagnostics: `NC_LOG=1` prints "wall clock/audio processed" to `/tmp/mic-nc.log`; measurement scripts in `audio/mic-nc/` (`lag-check.sh` — delay from bursts; `speech-live.sh` — synthetic voice; `lag-synth.sh` — the chain without a microphone).
- By mistake I played the user's voice recording from the first test through the speaker (live check); now the tests use a synthetic signal, the user's recordings are deleted.
- Not verified on the user's live speech after the fixes: `mic-nc start` → `mic-nc test`. Calibration (`mic-nc calibrate`) is worth repeating in the room where it will be used.
- **The user's live test (21:40, quiet room):** delay 0.05 s; bottom as is: speech −56.0 / noise −84.7 dBFS (28.7 dB); after noise suppression: speech −60.6 / noise −101.8 dBFS (41.2 dB). Noise −17.1 dB, speech −4.6 dB, speech/noise gain +12.4 dB. Works; for less speech loss — NC_ALPHA=1.0 (restart start). Not verified in a noisy environment.
- **Volume (21:50):** the user: after noise suppression "nicer, but quiet". Speech is recorded quietly on the bottom microphone (−56 dBFS). `mic-nc-dsp` got an output gain after noise suppression: `NC_GAIN_DB` (default +12 dB) + a soft limiter (linear up to 0.7, then tanh). Verified offline: input −40 → output −28 / −40 (NC_GAIN_DB=0) / −20 (=20) dBFS. `mic-nc demo` now normalizes by the 90th percentile of speech loudness (≈ −20 dBFS) instead of the peak (the peak caught a clap) and removes the suppressor gain from the A/B so that the comparison is fair. The user's demo files: audio/clips/ncdemo/.
- **21:55:** on the user's request the default gain `NC_GAIN_DB` was raised from +12 → **+18 dB** (verified: input −40 → output −22 dBFS); the noise suppressor was restarted with the new version.

### 🎧 3.5 mm jack detection (ACCDET) — WORKS (2026-11-12 22:00)
- **Without rebuilding the kernel:** the `mt6359-accdet` driver was taken from the kernel sources and built as an external module (`audio/codecs/mt6359-accdet.c/.h`, `snd-soc-mt6359-accdet.ko`); the MFD cell `mt6359-accdet` already exists in the kernel (`mt6397-core`). An `accdet` node is needed in DT as a child of the PMIC (`&pmic`, it is looked up by NAME at the parent; `compatible = "mediatek,mt6359-accdet"` is needed for the MFD) and `mediatek,accdet = <&accdet>` in the `sound` node. The DTB was installed by hand (`/boot` + `mkinitfs`, backup `/root/boot-backup/*.pre-accdet`); commit in `begonia-fixes` (`1f93af1f5`), the patch for the kernel package is not exported yet.
- **Trap 1:** in the original kernel the driver has `.name = "pmic-codec-accdet"`, while the MFD creates the device `mt6359-accdet` → the driver never binds. In our copy the name is fixed to `mt6359-accdet`.
- **Trap 2:** without `CONFIG_SND_SOC_MT6359_ACCDET` the header substitutes an empty stub `mt6359_accdet_enable_jack_detect` → the machine driver would not enable anything, and the .c was not compiled. Fixed with `subdir-ccflags-y += -DCONFIG_SND_SOC_MT6359_ACCDET_MODULE=1` in `audio/Makefile`.
- Machine driver (`mt6785-mt6359.c`): creates the jack `Headset Jack` (pins `Headphone Jack` ← HEADPHONE, `Headset Mic` ← MICROPHONE, buttons BTN_0..3), `num_jacks = 1`, calls `mt6359_accdet_enable_jack_detect()` from the init of the "Primary Codec" link. `audio-up.sh` loads accdet BEFORE the machine driver (otherwise the component is not found). DT parameters as on k6853/k6873/k6875: mic-vol 8, mic-mode 2, plugout-debounce 1, pwm-deb-setting …, three-key-thr 0 80 220 400; `caps=125` (PMIC EINT0, 3 buttons).
- In ALSA: `iface=CARD` controls `Headphone Jack` and `Headset Mic Jack` (read with `amixer -c 0 cget iface=CARD,name='Headphone Jack'`; without `iface=CARD` it says "Cannot find"), input device "mt6785_mt6359 Headset Jack". **Verified physically (22:00):** plugged in → HP on, after ~1 s MIC on (the test headphones apparently have a microphone = 4 contacts), pulled out → both off; a short bounce on insertion/removal.
- The DAPM pins are now bound to the jack: `Headphone Jack` (output) is connected only with headphones plugged in, `Headset Mic` — with a headset.
- Not done: PulseAudio ports with `[Jack ...]` (availability), automatic `hp-on/off` on the event, `Headset Mic` as an input (AIN1, PGA_L_Mux=AIN1), exporting the patch for the kernel package.

### 🎧 Jack automation: PulseAudio ports and automatic switching (2026-11-12 22:10) — WORKS
- `analog-output-headphones.conf`: section `[Jack Headphone]` (`state.plugged = yes`, `state.unplugged = no`) → the headphone port is available only with the jack plugged in; PulseAudio leaves it by itself.
- `audio/jack-auto.sh` + user service `begonia-jack.service` (`~/.config/systemd/user`, enabled): polls the jack control `Headphone Jack` every 0.3 s, reacts to a state stable for ~0.9 s (contact bounce). Plugged in → `hp-on 1` + `spk-off`; pulled out → `hp-off` + (by default `JACK_UNPLUG=speaker`) `spk-on 40` (speaker with the HPF). Silence only: `JACK_UNPLUG=mute`. Log: `/tmp/jack-auto.log`.
- Verified by the user by unplugging/plugging: switching works. The complaint "the ability to turn on sound fell off" turned out to be a browser bug, not our stack (the chain was checked: stream→speaker_hpf→card, the amplifier turns on).
- Not verified: the headset microphone (AIN1) — needs a real microphone; the earpiece — not tested.

### 📡 IR transmitter (IR TX) — WORKS AT THE HARDWARE LEVEL (2026-11-12 22:20), waiting for a visual check
- Hardware: the LED on **GPIO26 (function PWM_0)**, the PWM controller `pwm0` (0x11006000), channel 0 (vendor `mtk_irtx_pwm.c`, `pwm_ch=0`, memory mode). Mainline has a ready driver `pwm-ir-tx` (the kernel config already has `CONFIG_IR_PWM_TX=m`, `CONFIG_PWM_MEDIATEK=m`).
- DT: `audio/irtx-begonia.dtsi` (in the kernel tree `irtx-begonia.dtsi` + `#include` in begonia.dts; commit `982247293`): pinmux GPIO26→PWM_0, `&pwm0` status=okay, node `ir-transmitter` (`compatible = "pwm-ir-tx"`, `pwms = <&pwm0 0 26316>`, 38 kHz carrier). The DTB was installed by hand (backup `/root/boot-backup/*.pre-ir`), the modules autoload (`/etc/modules-load.d/begonia-ir.conf`: `pwm_mediatek`, `pwm-ir-tx`).
- Result: `/dev/lirc0` ("PWM IR Transmitter"), `ir-ctl --features`: send raw, scancode encoder, set carrier, set duty cycle. dmesg: "TX will not be accurate as PWM device might sleep" (a standard warning: the timings go through timers, not hardware). Installed `v4l-utils` (`ir-ctl`).
- Check: `ir-test` (blinks the IR for 6 s, look through the camera of another phone). A real remote target (air conditioner/TV) was not tested.
- IR pitfall: lirc rejects (EINVAL) a single write with a total duration of seconds (6 s); separate sends of 0.3 s and an NEC scancode pass. ir-test sends the bursts as separate calls.
- **IR TX confirmed by the user (22:30):** the IR LED glows in the camera of another phone (not noticed the first time — observer error). Status: WORKS. A real remote target was not tested.
- **Proximity confirmed (22:35):** stk3310 (iio:device3, in_proximity_raw): at rest 450–454, a hand nearby ~720 (threshold near=550 from patch 0003). Status: WORKS (checked with a hand; the near/far event for turning the screen off during a call was not checked — there are no calls).

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

### 📞 Earpiece (MT6359 HS) — the path WORKS (2026-11-12 23:55), waiting for a listening check
- Silent run (zeros, `RCV Mux`=Voice Playback, `Handset Volume` 0): during the stream `0x2588=0x0009` (DAC on), `0x2594=0x009b` (HS driver + mux on the DAC), `0x260e=0x0012` (-10 dB). On stop it is correct: `0x2594=0x0090`, `0x260e=0x001f`. A false alarm about "gain 0x1f during the stream" — my measurements over ssh lagged (~0.4 s per measurement), the stream had already ended.
- Objective test `audio/rcv-test.sh [Handset Volume] [Hz] [level -dBFS]`: a 1 kHz tone, recorded with the top microphone (AIN2, next to the earpiece), three measurements A (silence) / B (tone, RCV Open — control) / C (tone, RCV Voice Playback). Result: -40 dBFS at index 0 (-10 dB) — not heard by the microphone (below the noise floor of -81 dBFS); **-30 dBFS at index 4 (-6 dB): top microphone +20.1 dB above silence (+28 dB above control), bottom — nothing.** The tone at the microphone is -110 dBFS — weak, the earpiece is meant to be pressed to the ear (a call), on a table it is quiet by definition.
- Remaining: a listening check by the user (put the ear to the top speaker, `rcv-test 4 1000 30` — quiet; louder in steps), a UCM/PulseAudio port for the earpiece (no `analog-output-earpiece` yet), call routing — only once telephony exists (there is no modem in mainline).

### 📞 Earpiece: RESULT (2026-11-13) — WORKS, quiet, "good enough"
- **Correction to the entry above:** "+20 dB on the top microphone" at -30 dBFS is rather pickup/noise, not acoustics (the Handset Volume ladder on the microphone grew irregularly). The sound was really confirmed only by the user's ear with a louder ladder (the soft start/fade was audible, "insanely quiet").
- **Cause of the quiet sound:** the common digital `ADDA_DL_GAIN = 2165` (-29.6 dB), tuned for the headphones, also mutes HS. The analog `Handset Volume 18` (+8 dB, ceiling) does not fix that.
- **Working level (the user: "50000 will do", "livable"):** `Handset Volume 18` + `ADDA_DL_GAIN 50000` (-2.3 dB; ×23 of the default). At 17200/34000 it was "barely", at 50000 tolerable. The 65535 ceiling (0 dB) was not tried. Do not go further: the user thinks the rest is "a quirk of his old" device specifically (the earpiece grille/membrane), according to the registers the HS path is fine.
- **Click at the start:** it was in the clip file itself (the first 10 ms of the recording, peak -5 dBFS = the loudest moment of the clip, it also spoiled loudnorm). Fixed by trimming `TRIM_MS` (30 ms) before normalization. A hardware click when HS turns on was not isolated (300 ms of digital silence before the signal are clean). If it comes back — a soft gain ramp in `mt_rcv_event` (it currently writes the target straight into `ZCD_CON3`).
- **IMPORTANT:** `ADDA_DL_GAIN` is SHARED with the headphones. Any use of the earpiece with 50000 must restore 2165 (in `rcv-play.sh` that is `trap restore EXIT`), otherwise the headphones become +27.7 dB louder than normal.
- Tools: `audio/rcv-play.sh [file|speech] [Handset 0..18] [LUFS] [ADDA_DL_GAIN raw ≤65535]` (starts after 8 s, 300 ms of silence + 20 ms fade, trap restore), `rcv-level.sh` (ladder by tone level), `rcv-steps.sh` (ladder by Handset Volume), `rcv-test.sh` (tone + microphone recording, not informative at quiet levels).
- Not done: the PulseAudio port `analog-output-earpiece` (it should switch RCV Mux + Handset Volume 18 + ADDA_DL_GAIN 50000 and back to 2165), telephony in general.
- **The user's decision (2026-11-13):** the earpiece is left "for testing" for now — only the manual `rcv-play.sh`, we do NOT make the PulseAudio port `analog-output-earpiece`. Come back when telephony appears or permanent routing is needed. The system settings (`ADDA_DL_GAIN` 2165, `RCV Mux` Open) at rest are the same as before the work.

## 📶 Cellular (MD1 modem, calls/SMS/data): RECONNAISSANCE (2026-11-13)
- **The hardware/firmware is on the phone:** `/dev/disk/by-partlabel/md1img` (150 MB, MTK container, the first block `md1rom`), `scp1/2`, `spmfw`, `sspm_1/2`, `nvram`/`nvdata`/`protect1/2` (IMEI and modem calibration; copies on the PC in `Begonia/begonia-backup/*.img`). DT has only the stubs `md_attr_node` and `scp`; dmesg is silent about the modem (`vmodem: disabling`).
- **Kernel 6.16.4:** `CONFIG_WWAN` is not enabled, `MTK_SCP` is not enabled, there is no CCCI in mainline at all (`drivers/net/wwan` has only t7xx/iosm/mhi — other modems, not the MTK AP-integrated one).
- **Vendor sources exist:** `wifi/kernel-r-oss` (git without a working tree; read with `git show HEAD:path`, `git archive` fails on nouveau/aux.c — a Windows name). `drivers/misc/mediatek/eccci` = 1567 KB (hif 446, port 268, fsm 227, **mt6785 71 KB**), `ccci_util` = 186 KB. Key for enabling MD: `eccci/mt6785/md_sys1_platform.c` (`md_cd_power_on`, `md_start_platform`, `ccci_set_clk_cg`, `md_cd_bootup_cleanup`, `md_cd_power_off`), `ccci_platform.c`, `modem_secure_base.h`, `cldma_reg.h`.
- **The secure part:** power-on/MPU/clock requests go through SMC (`MTK_SIP_KERNEL_CCCI_CONTROL`, `MTK_SIP_KERNEL_CCCI_GET_INFO`, flight mode), i.e. the native Xiaomi ATF is needed (we have it in `lk`/`tee`, SMC worked for connsys too). Check: whether ATF answers CCCI SIP on 6.16.
- **Stage plan (simple first):**
  1. Check the IMEI/NV backup (there are .img files, compare `md` in nvdata from the phone), write nothing to `nvram/nvdata` until success.
  2. SMC probe: our own small module (like `consys-bringup`) that calls `CCCI_GET_INFO` and looks at the ATF answers. Without starting the modem.
  3. MD bring-up: MD clocks/power domains, loading `md1img` piece by piece (md1rom/md1dsp/...), MPU regions through SMC, start. Goal of the stage: a handshake over CCIF (shared memory), boot status without a panic.
  4. Channels: CLDMA/CCIF HIF, `ttyC*` ports (AT), `ccci_mdinit` / `md_ctrl` from vendor.img.
  5. Userspace: either the AT interface directly (ofono/ModemManager with an MTK plugin), or an Android chroot with `mtkfusionrild` like for GPS and a bridge into ofono (the Halium way).
- **Estimate:** stage 2 — an evening; stage 3 — days (missing: SSPM/SPM/EMI-MPU for 6.16, possible dependencies on the `mtk_spm`/`sspm` firmware interface, like with connsys, only bigger); stages 4–5 — weeks. The camera is a separate mountain of the same size.
- **Risks:** the modem writes to `nvdata/nvram` (IMEI, calibrations) — until success mount the partitions read-only or work with a copy; a wrong MPU/clk can hang the SoC (hard reset with the buttons, the firmware is not touched); a second start without a reboot may break the chip, like with GPS.
- **Step 1 (IMEI/NV backup) DONE (2026-11-13):** `nvram` and `proinfo` match the October backup bit for bit (the IMEI is there). `nvdata`/`nvcfg`/`protect1/2`/`persist` differ by hash, but the file trees are identical (433/20/12/12/12 files, `diff -rq` empty): only the ext4 metadata changed (mount counter 2→7, times from my mounts in the GPS chroot; `persist` has +6 free blocks, the same files). `nvdata/md/NVRAM/NVD_IMEI` is in place. Fresh images from the phone: `Begonia/begonia-backup/2026-11-13/*.img` (the October ones were not touched). Rule: until the bring-up succeeds the NV data partitions on the phone are read-only.
- **Step 2 (reconnaissance of the MD state, read-only) DONE (2026-11-13).** Correction to the plan: a "blind" SMC probe is DANGEROUS: `MTK_SIP_KERNEL_CCCI_GET_INFO` (0x82000206|AARCH) in the vendor code = `mdreg_write32(reg_id,value)`, i.e. a WRITE of modem registers through ATF, not a read. `MTK_SIP_KERNEL_CCCI_CONTROL` = 0x82000505 (the subfunctions MD_POWER_CONFIG/MD_BOOT_STATUS exist in the enum for mt6853+, the enum for mt6785 was not found; do not call without checking). Enabling MD on mt6785 is NOT through SMC, but by direct operations (`mt6785/md_sys1_platform.c`): `md1_pmic_setting_on()` (vmodem, vsram_md) → `INFRA_AO_MD_SRCCLKENA` (0x10000F0C) = 0x21 → `clk_prepare_enable(clk_table[0])` (MTCMOS MD1) → `kicker_pbm` → then the image into memory, MPU, `md_cd_let_md_go` (write 1 to MD_BOOT_VECTOR_EN).
- **What mainline sees now:** the genpd domain `md` EXISTS and is off (`off-0`), `conn` is on; the regulators `vmodem` (0.875 V) and `vsram_md` (0.99 V) exist and are off, `vmddr` is on. Registers are read via `phone-extra/rd32.py` (/dev/mem, read only; there is no `busybox devmem` on pmOS): `security_ao` (0x1001a824/828/82c/830) = **1,1,1,1 = "BROM Pass"** (LK already started the secure part of MD), `MD_SRCCLKENA` (0x10000f0c) = 0 (the modem is not clocked, it sleeps).
- **Map (native DTB, from the MIUI boot.img, `scratchpad/dtb/0.dts`):** `security_ao` 0x1001a000, `infracfg_ao` 0x10001000, `sleep` (SPM) 0x10006000, `apmixed` 0x1000c000, CCIF: `ap_ccif0..4` 0x10209000/0x1020b000/0x1023c000/0x1023e000/0x1024c000 (MD sides +0x1000), `modem_temp_share` 0x10018000. This DTB has no reserved memory regions for MD (LK/atag sets them) — find out where LK loads the image and how much it gives (cmdline/`/proc/iomem`/`md1_ccci_info`).
- **Next, step 3 (bring-up):** a `md-bringup` module after the `consys-bringup` model: (1) enable vmodem/vsram_md/SRCCLKENA/the `md` domain one at a time, reading the status after each; (2) find out the image region and MPU; (3) only then the boot vector. ONLY under supervision: a SoC hang is possible (hard reset with the buttons). nvdata/nvram — read-only.
- **SRCCLKENA address correction:** the vendor `infra_ao_base` = `infracfg_ao` @0x10001000, `INFRA_AO_MD_SRCCLKENA`=0xF0C ⇒ the real address is **0x10001F0C**, not 0x10000F0C (as in a comment of the vendor log). It reads `0x01006321`: the low byte is ALREADY 0x21 (as the vendor writes), i.e. the bootloader did this step; `0x10000F0C`=0 is a different register, not ours.
- **Step 3 (bring-up) PREPARED (2026-11-13 00:30), not run yet:** `module/md-bringup.c` (+`md-begonia.dtsi`, `make-md-dtb.sh`, `install-md-dtb.sh`, `rollback-md-dtb.sh`). A `md-bringup` node in `/soc`: `power-domains=<&spm MT6785_POWER_DOMAIN_MD>`, `vmodem-supply`/`vsram_md-supply`. The module by stages (`stage=`): 0 snapshot (changes nothing); 1 +vmodem, vsram_md; 2 +SRCCLKENA (if the low byte ≠0x21); 3 +the MD domain (`pm_runtime_resume_and_get`) and reading the boot stats (`0x1020E300`, select `0x1020E700`). The image is NOT loaded, the boot vector is NOT touched — MD does not start. `rmmod` rolls everything back in reverse order. Every stage prints a snapshot: SRCCLKENA, BROM (security_ao), the regulator state (on/µV), the domain.
- The DTB is installed in `/boot` (backup `/boot/mt6785-xiaomi-begonia.dtb.pre-md`, rollback `sh rollback-md-dtb.sh`), initramfs/boot.img rebuilt. **A reboot is needed.** Then: `sudo insmod ~/md-bringup.ko stage=0; sudo dmesg | tail`, `sudo rmmod md_bringup`, and so on for 1, 2, 3 (rmmod each time and check dmesg). If stage 3 hangs the phone — hold the power button, `.pre-md` will restore the previous DTB.
- **Step 3 (MD power) DONE (2026-11-13 00:38) — the modem MTCMOS TURNS ON and OFF in mainline.** The first version of the node (one domain in `power-domains`) failed: `genpd_dev_pm_attach` powers the domain on at driver bind, BEFORE the regulators, while `md` has an external buck (`EXT_BUCK_ISO`) — `vmodem`/`vsram_md` are not supplied, `PWR_ACK` does not come, `scpsys_power_on` waits exactly 1 s (1001 iterations of usleep_range, visible in function_graph) and returns an error (bind → `I/O error`, the device in `devices_deferred`). The sequence `scpsys_modem_pwrseq_on` MATCHES the vendor `spm_mtcmos_ctrl_md1` (ext_buck_iso→RST_B→PWR_ON→wait for ack, then bus protect), the domain code does not need changing — only the ORDER.
- **Solution:** TWO domains in the `md-bringup` node (`power-domains = <&spm MD>, <&spm CONN>; power-domain-names = "md","conn"`): with several domains the kernel does not attach them by itself (`genpd_dev_pm_attach` returns 0 when count≠1); the driver attaches `md` through `dev_pm_domain_attach_by_name` and `pm_runtime_resume_and_get(virt_dev)` at stage 3, after `vmodem`/`vsram_md`. Rollback: `pm_runtime_put_sync` + `dev_pm_domain_detach` + regulators off.
- **Stage results (insmod ~/md-bringup.ko stage=N; rmmod md_bringup):** 0 — snapshot, changes nothing (`SRCCLKENA=0x01006321`, `BROM=1,1,1,1`, vmodem/vsram_md off 0.875/0.99375 V, domain off); 1 — vmodem and vsram_md turn on at the standard voltages; 2 — no-op (the low byte of SRCCLKENA is already 0x21); 3 — `pm_runtime_resume_and_get=0`, the `md` domain ON (genpd summary), SPM registers: `PWR_STATUS(0x10006160)=0x01f7ffff` (bit0 ack), `0x10006318=0x0d` (RST_B|PWR_ON|PWR_ON_2ND), `0x100063b0=0` (ext buck iso released), `md_boot_stats0/1=0` (the modem is not started — and should not be). rmmod: domain off (`PWR_STATUS=0x01f7fffe`, `0x318=0x08`, `0x3b0=3`), regulators off, no oops/timeouts. The system and WiFi are alive.
- **Next — step 4 (really starting MD):** (a) find out where LK left `md1img`/where to load it (MD reserved-memory, `md1_ccci_info`/atag in the cmdline, the sizes of `md1rom`/`md1dsp`/`md1_dummy`), (b) EMI MPU: protecting the image region (`MTK_SIP_EMIMPU_CONTROL` 0x8200050B through ATF, the needed region numbers from `ccci_util`), (c) copying the image into memory by the header (MTK img header: `88 16 88 58` + the name `md1rom`), `MD_BOOT_VECTOR_EN` 0x20000024 (in the MD address space → a window through `md_ccif4`/`mdcfg`), (d) `md_cd_let_md_go`, wait for the CCIF handshake. This is the riskiest part (a wrong MPU/address — a SoC hang; cured with the power button).

### 📶 Step 4a: LK HAS ALREADY LOADED md1img — decoding the LK tags (2026-11-13 00:45)
- **MD memory map from DT** (reserved-memory from LK): `ccci_tag_mem` 0xBFF60000+0x10000; **`md_mem_usage` 0xAC000000+0x10000000 (256 MiB)**; `ap_md_nc_smem` 0x8E000000+0x1770000; `ap_md_c_smem` 0x8C000000+0x1780000. These regions are NOT `no-map` → readable by the AP (through the module `module/memwin.c` — a read-only window, `insmod memwin.ko phys=0x… size=0x…` → `/sys/kernel/debug/memwin/blob`; `/dev/mem` on RAM is forbidden by STRICT_DEVMEM).
- **LK tags** (`ccci_tag_mem`, format v2: name 64 B + data_offset/size/next_tag_offset; parser `phone-extra/parse_tags.py`, raw dump `phone-extra/ccci_tag.bin`): `md1img`=0x1A64B4C (image size ≈27.6 MB), `md1_chk`: `CHECK_HEADER`, `MT6785_S00`, `2021/12/08 18:27 XIAOMI_Q0MP1_K85V1_64 LWCTG_CUSTOM`, `MOLY.LR13.R1.TC8.SP.V2.P59`; `hdr_tbl_inf` (1 header): base 0xAC000000, 0xF800000, 0xC0000, 0x18B2610, 0x1A64B4C; **`retrieve0..4`** (addr/size): 0xB2E60000/0x69A0000, 0xADA70000/0x1590000, 0xAF430000/0x3D0000, 0xB2C10000/0x1F0000, 0xBB800000/0x800000, `retrieve_num=5`; `smem_dfd_size`=0x800000; `md1_bank4_cache_info` 0x8C000000/0xC000000/0x1780000; `md1_bank4_cache_layout` (4 entries: 0x8C000000/0/0x40000, 0x8C040000/0x40000/0x60000, 0x8C0A0000/0xA0000/0x60000, 0x8C100000/0x100000/0x1600000); `ccb_info` 0x8C100000/0x1600000; `udc_layout` 0xE70000/0x80000; `smem_layout` 0x8E000000 + 0x1770000; **`md_mpu_inf` (12 MPU regions, 24 B each: start, end, id, attr, …) and `md_mpu_num`=12**; `free_in_kernel`=1.
- **The image is in place:** at 0xAC000000 lies the MD start code (`67 01 06 3c 70 1b c6 24 08 00 c0 00 00 48 80 40` → MIPS: lui/addiu/jr). Reading without hardware errors (the region is not closed to the AP).
- **Next (step 4b):** find out whether LK already set up the EMI MPU for MD (read the EMI MPU registers, read-only) and how the vendor applies `md_mpu_inf` (SMC `MTK_SIP_EMIMPU_CONTROL` or `emi_mpu_set_protection`); what `retrieve*`/`free_in_kernel` are; then `MD_BOOT_VECTOR_EN` and `md_cd_let_md_go`; the CCIF handshake.

### 📶 Step 4b: reconnaissance of the vendor MD start (2026-11-13 01:00) — read-only, nothing was started
- **The kernel does NOT set the EMI MPU:** in `mt6785/ccci_config.h` `/*#define ENABLE_EMI_PROTECTION*/` is commented out ⇒ `SET_EMI_STEP_BY_STAGE`, `ccci_set_mem_access_protection_1st/2nd_stage` are not compiled. The protection (12 regions from the `md_mpu_inf` tag) is done by LK/ATF, not the kernel ⇒ the most dangerous item of the plan is gone. `MD_GENERATION`=6295.
- **The kernel does NOT load the image:** `md_cd_start` logs "modem image ready, bypass load" — the image is already in memory from LK (confirmed: 0xAC000000). `ccci_get_md_check_hdr_inf` only reads the header/version from the `md1_chk` tag.
- **The real `md_cd_start` sequence (modem_sys1.c:422), first start:** `md_cd_io_remap_md_side_register` → `md_sys1_sw_init` → `ccci_hif_late_init` → `ccci_init_security` → **`ccci_md_clear_smem`** (clearing the AP↔MD shared memory) → `md_ccif_ring_buf_init` → **`md_start_platform`** (BROM check via `security_ao`=1,1,1,1 — we already have that; "dummy md sys clk" and `md_cd_power_off` — just a test and power off). Then always: `ccci_md_hif_start(1)` → **`md_cd_power_on`** (vmodem/vsram_md, SRCCLKENA 0x21, the MD domain — `md-bringup` stage 3 ALREADY can do this) → **`md_cd_let_md_go`** (write 1 to `md_boot_slave_En`, MD_BOOT_VECTOR_EN 0x20000024 — an "MD-side" register, remapped in `md_cd_io_remap_md_side_register`) → `wdt_enable_irq` → `ccci_md_hif_start(2)`. Then the FSM waits for HS1 over CCIF (`CCCI_EVENT_HS1`), sends the runtime data, waits for HS2 ⇒ READY. `ccci_md_pre_start` = `md_cd_vcore_config` — weak, no-op.
- **Minimal "bring MD to life" experiment (stage 4 for md-bringup, NOT done yet):** after stage 3 (power on): clear the shared memory (`smem_layout`: 0x8E000000+0x1770000 and 0x8C000000+0x1780000 — per the vendor `ccci_md_clear_smem` layout), write 1 to MD_BOOT_VECTOR_EN, watch `md_boot_stats0/1` (0x1020E300, select 0x1020E700) and the CCIF registers (ap_ccif0..4 0x10209000…, MD sides +0x1000; RCHNUM/ACK), the MD watchdog IRQ. A sign of life — boot_stats growing. The handshake (HS1) cannot be completed without a CCIF/ring-buffer driver — a separate big chunk (`ccci_hif_ccif.c` 446 KB of hif in total, `fsm`, `port`).
- **Unresolved:** where `md_boot_slave_En` is remapped and how the AP sees the 0x20000000 window (read `md_cd_io_remap_md_side_register`), what `retrieve0..4`/`free_in_kernel` do (the kernel must free these regions after loading? — not critical for the start), the MD WDT/CCIF IRQs in our DT (vendor DT: the `ap_ccif*`/`md_ccif*` nodes, GIC SPI numbers).

### 📶 Step 4c: FIRST MD START — IT IS ALIVE (2026-11-13 08:01)
- `md-bringup.ko stage=4` (stage 3 + writing 1 to `MD_BOOT_VECTOR_EN` 0x20000024, the window was readable: 0 before the write, 1 after; smem was NOT cleared). No hangs, the system and WiFi are intact, `rmmod` rolled the power back without errors.
- **Boot stats moved** (0 -> ASCII codes of the MD boot phases): t=23 ms `stats0=0x53310000 stats1=0x53320000` ("S1"/"S2"), t=47 ms `stats0=0x5443000c` ("TC" + 0x0c). Then 5 s of silence: CCIF0/1 (rch/busy/ack) = 0, the head of smem did not change (sum=0xc07a194f, uncleared — DDR garbage).
- **Conclusion:** the modem core starts from the LK image, the MPU/memory are fine (the SoC did not hang). It stops at the "TC" phase — probably waiting for something from the AP: a clean smem, a HS over CCIF, clocks/resets (clk_table, `ccci_set_clk_cg`), or it crashed (a dump/WDT IRQ is needed).
- **Next:** (1) decode the boot_stats values in the vendor code (`md_cd_dump_md_bootup_status`, MOLY), (2) clearing smem by region before the start (`ccci_md_clear_smem`, flags SMF_NCLR_FIRST), (3) the vendor MD clocks (`clk_table`), (4) watch the MD WDT IRQ and CCIF with interrupts, not polling. Do not start a second time without a reboot.

### 📶 Step 4d: MD SENT HS1 (2026-11-13 08:16) — the handshake has begun
- What was missing in 4c: **the vendor preparation before the start** (`ccci_md_hif_start(md,1)` + `ccci_set_clk_cg(1)`): the clocks `infra_ccif{,1,2}_{ap,md}` + `ccif4_md` (7 of them, in DT: `clocks=<&mdinfra CLK_INFRA_CCIF_AP ...>`, the label `mdinfra` on `clock-controller@10001000` is placed by make-md-dtb.sh, the base is now `.pre-md`), `infra_ao+0x22C=1` (MD_PCCIF4_PWR_ON; before the write it is 0x55555555), the CCIF reset (`infra+0x150`/`0x154` bit 8), clearing the CCIF SRAM and **the magic "rtbn"=0x7274626E + MDSS_DBG md-view 0x40000800 + size 0x2800** in the last 12 bytes of the AP SRAM (+0x100+512-12), clearing the first MiB of nc-smem except CCB_CTRL(96-100K)/AUDIO(108-160K) with a cache flush through dma_map_single. All in `md-bringup.ko stage=5`.
- Result: the boot_stats are the same (S1/S2 -> TC in 47 ms), but after **147 ms `RCHNUM(CCIF0)=0x8000`** (channel 15 = D2H_SRAM) and in the CCIF0 SRAM (+0x100): `dl_header = ffffffff 00000000 00000000 5555ffff` (channel=0=CCCI_CONTROL_RX) and then `md_query_ap_feature`: `head_pattern=0x49434343` (MD_FEATURE_QUERY_PATTERN, "CCCI") + feature_set (bytes 03030302 03030303 ...). This is HS1: MD reports its features and waits for the AP answer.
- We did not answer: no ACK (`ACK`=0x8000 was not written), no HS2. The system/WiFi are intact, rmmod is clean.
- **Next step (HS2):** (1) decode `md_rt_data` fully (256 B feature_set + tail_pattern) — read SRAM+0x110.. completely; (2) the vendor answer path: `ccci_fsm.c` ~900 (MD_INIT_START_BOOT -> `ccci_md_send_runtime_data`), `ccci_modem.c` 1700-2000 (runtime data: the table of smem regions in `RAW_RUNTIME_DATA`, `ap_query_md_feature`: share_memory_support, ap_runtime_data_addr/size, md_runtime_data_addr/size, MPU start/total), sending = writing up_header+ap_rt_data into the SRAM (+0x100+0x110+..) and TCHNUM bit 15 (H2D_SRAM); (3) the smem layout for 6295 (`ccci_md_smem_layout_config`, ccci_modem.c:610) - it must be exact, since MD checks against it; (4) after HS2 MD should signal READY/move on to the ring buffers. A new start = only after a reboot.

### 📶 Step 4e: HS2 sent, MD hits an ASSERT in ccci_hs.c:626 (2026-11-13 08:30)
- `md-bringup.ko stage=6`: after HS1 (t≈150-170 ms) the AP answers: runtime data (704 B) in the nc-smem RUNTIME_DATA (nc+58K, md-view 0x4000E800, AP part 0x800 + MD part 0x800 at 0x4000F000) + `up_header {0, size, ch=1 (CONTROL_TX), 0x5555FFFF}` and `ap_query_md_feature` in the CCIF0 SRAM (+0x100) + TCHNUM=15/BUSY. The format/calculations are checked against the vendor `ccci_md_prepare_runtime_data`/`md_cd_send_runtime_data_v2`/`ccif_hif_fill_rt_header`.
- After ~25 ms MD raises **D2H_EXCEPTION_INIT (RCHNUM bit 16)** (the vendor ACK `&0xFFFF` does not clear bit 16; for bit 16 the AP must answer H2D_EXCEPTION_ACK = TCHNUM 16, I do that; after that RCHNUM=0x70000 = EXCEPTION_INIT + INIT_DONE + CLEARQ_DONE).
- **The exception record is in MDSS_DBG (nc+0x800)**: header `0x0800 ab 02`, "MCU", the DSP names (USIP0_USIP0.. MD32_RAKE), then `0x50, 0xDE`, the file name **`mcu/service/hif/ccci/src/ccci_hs.c`**, line number **0x272 = 626** (occurs twice), version `MOLY.LR13.R1.TC8.SP.V2.P59`, `LR13.R1.TC8.SP XIAOMI_Q0MP1_K85VLWCT G_CUSTOM`, "INITIAL". This is an assert in the MD handshake code: MD accepted HS2, but something in it did not suit it.
- Look at the dump without another start: until the phone reboots, smem is alive: `insmod memwin.ko phys=0x8e000000 size=0x10000` -> `/sys/kernel/debug/memwin/blob` (script ~/q2.sh).
- Hypotheses for ccci_hs.c:626: (A) MD requires MULTI_MD_MPU (v2.1: cached/non-cached MPU) - implemented as `mmpu=1`, checked by the next start; (B) MD requires the MUST features CCB(15)/DHL(16)/LWA(25)/CONSYS(30)/UDC(34), they need cacheable regions (CCB_info 0x8C100000+0x1600000, cached md-view 0x4C000000, `md1_bank4_cache_layout`); (C) a wrong region size/address. Going in order A -> B.

### 📶 Step 4f: the CCIF ring buffers are ready, MD goes past HS2 — and HANGS the SoC (2026-11-13 08:40)
- `mmpu=1` (MULTI_MD_MPU, ap_query_md_feature_v2_1) removed the assert in `ccci_hs.c:626`; the next assert was in `mcu/driver/ccismcore/src/ccismcore_ccci.c` — the CCISM ring buffers were not laid out. Added `md_rings_init` (16 queues, `[0xEE0000EE x2][rx/tx control][data][0xFF0000FF x2]`, normal 720K+640 B, exp 120K+640 B; CCIF_SHARE_MEMORY is announced with the real size 737920).
- With the ring buffers MD **does not assert** (no EXCEPTION within +28 ms), but ~50 ms after sending HS2 **the whole SoC hangs** (ssh/WiFi dead, the power button is needed for 10-15 s; it did not come back by itself). The log streamed with `sudo cat /dev/kmsg` -> PC (`md-logs/run-ring-1.kmsg`): the last line of the module is "HS2: sent", at +46 ms the WiFi log is still going, then a cut-off. The journald log on disk loses the last seconds on a hang — for such experiments ALWAYS stream kmsg to the PC.
- Hypothesis: it is not MD itself to blame, but my polling: the observation loop read/wrote `MD1_CFG` (0x1020E300/0x700) every 20 ms; when MD starts its normal boot and reconfigures the blocks, a read of the MD side may hang the bus. The vendor does not do this in a normal boot (only dumps after crashes). Fix: after HS1 `mdcfg` is not touched (only the AP side of CCIF + memory).

### 📶 Step 4g: BREAKTHROUGH — the cause of the hangs is found, MD passes HS2 and talks over the ring buffers (2026-11-13 08:55)
- **SoC hang = closed infra gates.** After HS2 MD goes into its normal init and reaches for resources whose clocks mainline switched off as unused (`clk_disable_unused`), while LK kept them open. The whole SoC freezes (the TOPRGU WDT reboots it by itself after ~30 s; LK then has `androidboot.bootreason=lk_crash` in the cmdline; the power button is NOT needed, the phone comes back by itself in ~1 min). Ruled out: polling MD1_CFG (`quiet=3000` — it dies anyway), the DPMAIF clock, a specific feature (bisection: without features 17/18/26/28 the HS does not pass and asserts in `ccci_hs.c:626`, with 28 — a hang).
- **Fix:** `ungate=1` — write 0xFFFFFFFF into the CLR registers of infracfg_ao (0x84/0x8c/0xa8/0xc4) before the start. State before: `cg0=1beffd5c cg1=c3094dd6 cg2=5ffd55c5 cg3=c9fc31e3` (bit=1 -> off), after: all 0. With `ungate=1` and ALL features: no hang, no EXCEPTION, MD sends `RCHNUM=0x2` (D2H_RINGQ1) after HS2 at t=287 ms.
- **Next:** (1) work out the minimal set of needed gates (suspects: infra_pmic_md, infra_auxadc_md, infra_md_tmp_share, infra_md32, ccif3_ap/md, ccif4_ap, md_msdc0) and put them into DT instead of ungate; (2) decode the ring queue (control channel: ccci_header 16 B), read HS2 from MD, answer the control messages; (3) the AP side of ccci: ring read/write, ports (ttyC*), the radio layer.
- Method for debugging hangs: `ssh ... 'sudo cat /dev/kmsg' > md-logs/run-X.kmsg &` (the stream to the PC survives the hang), the module with `quiet=N` and `fmask=` (feature mask).
- **First message from MD over a ring buffer (08:54, `ungate=1`, all features):** at t=319 ms `RCHNUM=0x2` -> ring q1 (rx rd=0 wr=72 len=81920): `aabbaabb 00000038 00000000 00000038 80000020 00000000 00004002 00000002 ...` = [marker aabbaabb][len=0x38][ccci_header: data0=0, data1=0x38, channel/seq=0x80000020 (ch 0x20, bit31 set), reserved=0][payload 40 B: 0x4002, 0x2 ...][ccddccdd tail]. Needed: read the whole packet, compare with `ccci_ringbuf_read` and the CCCI_CH enum (ch 32), answer; then a ring reader/writer in the module (update rx_control.read!).

### 📶 Step 4h: AP<->MD ARE TALKING (2026-11-13 09:00) — ccci-lite + RPC
- `md-bringup.c` got a minimal CCCI layer: reading ring packets (`rb_read`: `[AABBAABB][len][ccci_header+payload][CCDDEEFF x2]`, 8-byte alignment, updating rx.read), writing (`rb_write` into the tx ring, then BUSY+TCHNUM = queue number), seq per channel, an RPC responder. The smem for the rings is mapped write-combining via `vmap` (a cacheable mapping would race with MD: it writes the pointers in parallel).
- Channels -> MD1 queues: CONTROL/STATUS/SYSTEM = q0, RPC/IPC = q1, FS = q4 (`port_cfg.c`, table `md1_ccci_ports`). CCIF: bit q in RCHNUM = ring q from MD; our kick = TCHNUM q.
- **What MD asks the AP after HS2** (run `stage=6 mmpu=1 ungate=1`, the answer to everything is `FS_FUNC_FAIL` = -5 for now, NO exceptions):
  1. RPC op **0x4002 (GET_GPIO_NUM)**: names `GPIO_SIM1_HOT_PLUG`, `GPIO_SIM2_HOT_PLUG`, `GPIO_SIMn_SIO`, `GPIO_SIMn_SCLK`, `GPIO_SIMn_SRST` (n=1,2) + a second parameter (length 4, value = length of the name).
  2. RPC op **0x4005 (GET_EINT_ATTR)**: `MD1_SIMn_HOT_PLUG_EINT` for n=1..4, three requests per SIM (different values of the third parameter 6/0/5).
  3. **ch 14 = CCCI_FS_RX** (q4): MD asks for the AP file system: op 0x100e, a UTF-16 path `Z:\` ... — MD takes NVRAM/IMEI/calibrations through the FS channel (`ccci_fsd` on Android; on pmOS our own daemon or an Android chroot is needed, like for GPS).
- For real answers: 0x4002 -> the GPIO number from DT (the SIM interface pins: take them from the native DTB `mediatek,gpio_usage_mapping`/mdgpio in `Begonia/scratchpad/dtb`), 0x4005 -> the EINT parameters (num, debounce...), answer format: 2 parameters of 4 bytes [value][error code].
- Next: (1) honest RPC answers (GPIO/EINT SIM), (2) an FS daemon (reads/writes `nvdata`/`nvcfg` on MD request), (3) wait for control HS2/READY from MD (`CONTROL from MD`), STATUS poll (ch 67/68), (4) the `ttyC*`/AT ports.

### 📶 Step 4i: honest RPC answers work; FS is next (2026-11-13 09:10)
- The values are taken from the native dtbo (`pmos-begonia/neutrino/stock/dtbo.img`, unpacking: Android DTBO table, 2 overlays, both identical regarding SIM): `gpio_usage_mapping` (the second cell = MD-view id): SIM1_SIO/SRST/SCLK = 0x80/0x81/0x82, SIM2_SCLK/SRST/SIO = 0x83/0x84/0x85, SIM1/2_HOT_PLUG = 0x8c/0x8d; EINT: `md1_sim1/2_hot_plug_eint` interrupts=<n 8>, debounce 100000 µs (=100 ms for MD), sockettype 0, dedicated 0, src_pin 1/2. There are no SIM3/SIM4 nodes -> code -11 (ERR_SIM_HOT_PLUG_QUERY_STRING), like the vendor. Implemented in `md_rpc()` (`md_gpio_tab`, `md_eint_tab`).
- Run result (`md-logs/run-svc-2.kmsg`, 20 s): 8 answers to 0x4002 ({id,id}), 12 answers to 0x4005 ({0,val} for SIM1/2, {-11,-11} for SIM3/4), no exceptions, `rb_write=0` everywhere. Then MD freezes on an **FS request (ch14, q4)**: `op 0x100e, 2 parameters: UTF-16 "Z:\" (8 B), number 3` and waits for an answer — the boot is blocked until FS.
- **The FS channel = a "pipe" into userspace:** the kernel has no FS logic; the vendor `port_char` for CCCI_FS_RX hands the daemon `/vendor/bin/ccci_fsd` a frame `[u32 reserved(request index)][payload]` (the ccci_header is cut off, +4 B), writing back — the same way (channel 15). Daemon: 32-bit ARM Android 30 (`vendor.img`: `/bin/ccci_fsd` 129 KB, NEEDED libnvram.so, libstorage_otp.so, libcutils, libhardware_legacy), opens `/dev/ccci_fs`, `/dev/ccci_md1_sta`, `/dev/ccci_ioctl0`, `/dev/otp`; works with `/mnt/vendor/nvdata/md*` (nvdata = the IMEI/calibration partition, do NOT write until the backup is checked - it exists). The other native daemons in vendor.img: `ccci_mdinit` (starts MD through /sys/kernel/ccci/boot), `ccci_rpcd`, `md_monitor`, `mtkrild`; rc files `init.cccifsd.rc`, `init.cccimdinit.rc`, `init.cccirpcd.rc`, `init.md_apps.rc`, `mtkrild.rc`.
- A copy in WSL: `~/md-vendor/ccci_fsd` (from `~/vendor-work/vendor.img`, `debugfs dump`).
- **FS plan ("Halium-lite" method, like mnld for GPS):** (1) a misc device `/dev/ccci_fs` in the module: read() = wait for an FS_RX packet from q4 and hand out `[rsv][payload]`; write() = pack into a ccci_header(ch=15, seq, reserved=the first word) and rb_write into q4; (2) a second Android chroot (next to `/opt/gps-chroot`) with `ccci_fsd`, `libnvram.so`, `libstorage_otp.so`, the dependency chain (libfile_op, libnvram_sec?...), `/mnt/vendor/nvdata` = the mounted nvdata (read-write only after the backup check); (3) emulating `/dev/otp`, `/dev/ccci_md1_sta`, `/dev/ccci_ioctl0` with stubs. Then the same for `ccci_rpcd`/`ccci_mdinit`, then `md_monitor` and `mtkrild`.

### 📶 Step 4j: ccci_fsd SERVES MD — the FS channel works (2026-11-13 09:37)
- `md-bringup.ko` got: a kthread `md-ccci` (insmod does not block, `watch_ms=0` = run until rmmod), `/dev/ccci_fs` (miscdevice: read = a full ccci packet ch14 from q4, write = a full ccci packet, the module sets ch15/seq/data1 and puts it into q4; the vendor FS port has PORT_F_USER_HEADER), stubs `/sys/kernel/ccci/version` ("6") and `/dev/ccci_md1_sta` (broadcast events of 48 B for a 32-bit client: 9=HS1 when HS2 is sent, 10=READY on control HS2 from MD), `rb_tx_lock`.
- An Android chroot for the daemon: `md-chroot.tgz` (8 MB, `/opt/md-chroot`; built from gps-chroot + `ccci_fsd` from vendor.img + libstorage_otp/libhardware_legacy/…); **`libhardware_legacy.so` REPLACED by a stub** (`acquire_wake_lock`/`release_wake_lock` = 0, built with clang --target=armv7a-linux-androideabi21 -nostdlib; otherwise the daemon waits forever for the binder suspend service). Script `md/md-chroot.sh {setup|run|stop|teardown}`, `NVDATA_COPY=file` = nvdata/protect1/protect2 are mounted as **COPIES (loop rw)**, the real partitions are not written (the backup hash of nvdata `5d8abf02a4eb6ca5` matched before and after). The auto cycle `md/cycle.sh <name> [sec]` (reboot, copying, chroot, start, summary `md/summary.py`).
- Result: the daemon reads `/dev/ccci_md1_sta` (gets the HS1 event), opens `/dev/ccci_fs`, and through it MD reads its WHOLE NVRAM database: ~1700-1800 FS requests in ~22 s (`Z:\NVRAM\NVD_DATA\*_000`, `NVD_CORE\MT00A000/MT00B000`, protocol: ops 0x1001 open, 0x1002 read, 0x1004 write, 0x1005 close, 0x100e, 0x100f, 0x1013, 0x1014, 0x1022 ...; answer: `[ccci_header][op|0xFFFF0000][nparams][len,data]...`).
- Findings: (1) on a **ro nvdata** MD crashes in `nvram_io.c` (an attempt to create `NVD_CORE/MTCR_000`, EROFS=-18); (2) `protect1`->`/mnt/vendor/protect_f`, `protect2`->`/mnt/vendor/protect_s` are needed (the daemon looks for `.../md/`); with rw copies 1824 FS requests; (3) **my bug:** the static ring packet buffer was 2048 B, while MD sent a 3476 B FS packet (the maximum size CCCI_MTU+hdr, the daemon buffer is also 3476) - I skipped it, MD waited ~43 s for an answer and crashed on a timeout (`ccismcore_ccci.c:1898`). Fixed: a buffer of 8192.
- Next: run `md/cycle.sh run-fs-8 90` (after the buffer fix), watch for control HS2 from MD (READY), the STATUS poll, then the other ccci daemons (`ccci_mdinit`, `ccci_rpcd`, `md_monitor`) and the `ttyC*`/AT ports.

### 📶 Step 4k: MD GETS TO THE RF INITIALIZATION; HS2 from MD received (2026-11-13 10:20)
- Fixed: (1) the write limit of `/dev/ccci_fs` = CCCI_MTU+20 (=3476, the vendor has CCCI_MTU+header+4 for FS) - before, a 3476 B answer was rejected with EINVAL; (2) the ring packet buffer is 8192; (3) RPC 0x400e (DTSI_QUERY) is answered like the vendor does: `[4:0][68: 0x0f..]` (there is no md_attr_node node); (4) RPC 0x400f/0x4010 (Android properties) go to the userspace `ccci_rpcd` through `/dev/ccci_rpc` (misc, USER_HEADER, q1, ch33), the chroot got `ccci_rpcd` and `libpropshim.so` (LD_PRELOAD, `property_get` without a property service: `ro.product.name=begonia`, logs the keys into /tmp/props.log); `ccci_rpcd` answered `{7,"begonia"}`.
- Module code: a common `struct uport` (fs/rpc), `/dev/ccci_fs`, `/dev/ccci_rpc`, `/dev/ccci_md1_sta`, sysfs `ccci/version`, the `md-ccci` thread, parameters `stage=6 mmpu=1 ungate=1 rf=1 watch_ms=0`.
- **Run result (`md-logs/run-rf-1.kmsg`):** ~4590 FS requests in ~60 s (the whole NVRAM database: NVD_DATA, NVD_CORE, CALIBRAT), RPC GPIO/EINT/DTSI/property, **control HS2 from MD (`CONTROL from MD: msg=0x0`, hs2_seen=1) at ~102 s**, then MD messages on other channels (q0 ch4 and ch2, q1 ch171 the string `wostatusget=0`, q5 ch10, q6 ch57/59 = IMS), then **an assert in `mcu/l1/el1/el1d_ext/md95/el1d_rf_error_check.c:250` (parameter 0x0a)** - an RF path check. This is already the radio hardware, not CCCI.
- RF power: added `rf=1` (DT: `vrf12/vrf18/vrfck/vbbck/vpa/va09-supply`, labels mdvrf12.. in make-md-dtb.sh) - they turn on fine (1.2/1.8/1.6/1.2/0.5/0.9 V), but the assert did not change (the vendor `md1_pmic_setting_on` turns on only vmodem/vsram_md, the RF lines are controlled by MD through the PMIC wrappers; until mainline switched them off they were on from LK).
- Hypotheses for the RF assert (not verified): (a) the BSI/BPI/MIPI/RF-reset pinmux (LK set it up; compare the `pinctrl` state vs the `md_*` pins of the native dtb); (b) the 26 MHz XO/clkbuf for the RFIC (`clkbuf`/SRCLKENAI, vendor: the RPC `IPC_RPC_GET_RF_CLK_BUF_OP` 0x4008 did not arrive); (c) the LDO modes in the MT6359 PMIC (HW-op by SRCLKEN/MD vs SW enable, the mainline driver may reset them), MD access to the PMIC through `infra_pmic_md` (enabled by ungate); (d) missing RF calibrations in nvdata (the copy has them; MD read CALIBRAT\CC*, EL03/EL04); (e) the SPM/SSPM firmware (spmfw/sspm in the partitions) is not loaded - MD may wait for SPM resources.
- Not touched: the real nvdata/protect1/protect2 (everything through copies). Scripts: `md/cycle.sh` (one run ~4 min: reboot+start+summary), `md/summary.py`, `md/md-chroot.sh`, `md-chroot.tgz`; heat: during a run MD warms the phone up - after the run `q2.sh` does rmmod (MD unpowered).

## 🖥 Panel black after boot — fixed with the downstream reset sequence (2026-11-13 ~23:50)
- Symptom since the first boot: black screen until lock/unlock. Ruled out: DRM state (CRTC/plane active, OVL IRQs at 60 Hz),
  all display config registers identical before/after DPMS, the TDDI touch reset (re-binding the touch driver keeps the picture).
- Cause: LK leaves the CSOT panel running; mainline resets it with a single 5 ms pulse and starts sending commands 5 ms later.
- Fix: patch 0018 `drm/panel: nt36672a: Use the downstream reset sequence for the CSOT panel` (low 5 / high 5 / low 1 / high 11 ms,
  as vendor `lcm_init_power`). Kernel r5. Verified: GUI right after boot, the boot splash is visible now as well.

## ⚠ Volume keys dead / PulseAudio hanging after the reinstall — missing `xwayland` (2026-11-14)
- The fresh image was built from `/etc/apk/world` of the old system; `xwayland` was only a dependency there and did not make it.
- kwin still listens on the lazy X11 socket (`@/tmp/.X11-unix/X0`) but cannot start Xwayland, so every X11 client hangs in
  `connect()` forever (`unix_wait_for_peer`): `start-pulseaudio-x11` (PulseAudio stuck at session start) and kded6 — whose
  `audioshortcutsservice` handles the volume keys. The kernel side was fine (`mt6779-keypad` sends KEY_VOLUMEUP/DOWN).
- Fix: `apk add xwayland xwaylandvideobridge` (+ added to `kernel-build/scripts/install-image.sh`). The mask of the
  `start-pulseaudio-x11` autostart in begonia-port is now redundant but harmless.
- Debug trick: `/proc/<pid>/syscall` + `/proc/<pid>/mem` give the sockaddr of a hung `connect()`.
