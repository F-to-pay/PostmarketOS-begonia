#!/bin/bash
# Moves the working out-of-tree drivers from port/ into the kernel tree (the begonia-fixes branch) as separate commits.
# Run in WSL: bash import-drivers.sh   (not idempotent: run once on a clean branch)
set -euo pipefail
P=$(cd "$(dirname "$0")/../.." && pwd)   # the port/ directory
K=${KDIR:-$HOME/mt6785-linux}
G=${GEN4M:-$HOME/gen4m-port}      # the gen4m tree after port.py (the working wlan_6785_axi.ko was built from it)
cd "$K"
git diff --quiet && git diff --cached --quiet || { echo "dirty tree"; exit 1; }
commit() { git add -A "$@"; git commit -q -s -m "$MSG"; echo "== $(git log -1 --format=%s)"; }

# --- 1. ASoC common: a separate AP interrupt enable bit ---------------------------------------------
cp $P/audio/common/mtk-afe-fe-dai.c $P/audio/common/mtk-afe-platform-driver.c $P/audio/common/mtk-base-afe.h sound/soc/mediatek/common/
MSG="ASoC: mediatek: common: Support a separate AP interrupt enable bit

MT6785 routes each memif interrupt to the AP only when its bit in
AFE_IRQ_MCU_EN is set in addition to the generic enable bit. Add
irq_ap_en_reg/irq_ap_en_shift to struct mtk_base_irq_data and toggle it
from the common trigger callback. Also ratelimit the hw_ptr error that
floods the log while a stream is being torn down."
commit sound/soc/mediatek/common

# --- 2. MT6359: volume scale inversion, microphone types ---------------------------------------------
cp $P/audio/codecs/mt6359.c sound/soc/codecs/mt6359.c
MSG="ASoC: codecs: mt6359: Fix inverted output gains, add mic type overrides

The hardware gain codes of the HP/LINEOUT/HS stages decrease with volume
(0 = +8 dB ... 0x12 = -10 dB), while the TLV scale grows. Declare the
controls inverted and translate the cached codes back to indices; extend
the headphone range by 2 dB downwards.

Add mic_type0..2 module parameters. On the Redmi Note 8 Pro the main
microphone only works in DCC mode, which is the new default for MIC0."
commit sound/soc/codecs/mt6359.c

# --- 3. ACCDET: driver name matching the MFD cell -----------------------------------------------------
cp $P/audio/codecs/mt6359-accdet.c sound/soc/codecs/mt6359-accdet.c
MSG="ASoC: codecs: mt6359-accdet: Match the MFD cell name

The MT6359 MFD core registers the cell as \"mt6359-accdet\", so the
driver named \"pmic-codec-accdet\" never binds. Rename it."
commit sound/soc/codecs/mt6359-accdet.c

# --- 4. TFA9874 --------------------------------------------------------------------------------------
cp $P/audio/codecs/tfa9874.c sound/soc/codecs/tfa9874.c
python3 - <<'PY'
import re
p='sound/soc/codecs/Kconfig'; s=open(p).read()
s=s.replace('config SND_SOC_TFA989X\n', '''config SND_SOC_TFA9874
	tristate "NXP TFA9874 amplifier (DSP bypass)"
	depends on I2C
	select REGMAP_I2C
	help
	  Minimal driver for the NXP TFA9874 smart amplifier as used on the
	  Xiaomi Redmi Note 8 Pro. The CoolFlux DSP is not used: the vendor
	  container has no speaker model, so the amplifier runs on its
	  hardware protections only. Includes a status watchdog.

config SND_SOC_TFA989X
''',1)
s=s.replace('\timply SND_SOC_TFA989X\n','\timply SND_SOC_TFA9874\n\timply SND_SOC_TFA989X\n',1)
open(p,'w').write(s)
p='sound/soc/codecs/Makefile'; s=open(p).read()
s=s.replace('snd-soc-tfa989x-y := tfa989x.o\n','snd-soc-tfa9874-y := tfa9874.o\nsnd-soc-tfa989x-y := tfa989x.o\n',1)
s=s.replace('obj-$(CONFIG_SND_SOC_TFA989X)\t+= snd-soc-tfa989x.o\n','obj-$(CONFIG_SND_SOC_TFA9874)\t+= snd-soc-tfa9874.o\nobj-$(CONFIG_SND_SOC_TFA989X)\t+= snd-soc-tfa989x.o\n',1)
open(p,'w').write(s)
PY
grep -q SND_SOC_TFA9874 sound/soc/codecs/Makefile
MSG="ASoC: codecs: Add a minimal NXP TFA9874 amplifier driver

Bring the amplifier up the way the vendor tfa98xx driver does, without
the DSP: reset, revision specific registers, device and profile
registers from the (decoded) vendor container, then power up and enable
the amplifier from a DAPM event once I2S provides BCK.

A 100 ms status poller turns the amplifier off on OCP/OTP/UVP, retries
after 3 s and latches after three faults in a minute
(debugfs: tfa9874-fault, 'reset' / 'inject'). The hardware gain is
capped at -18 dB: the speaker of the test device distorts above that."
commit sound/soc/codecs

# --- 5. MT6785 AFE + machine driver ------------------------------------------------------------------
mkdir -p sound/soc/mediatek/mt6785
cp $P/audio/mt6785/*.c $P/audio/mt6785/*.h sound/soc/mediatek/mt6785/
# out of tree codecs/ was next to mt6785/; in the tree the codecs are in sound/soc/codecs
sed -i 's|#include "../codecs/|#include "../../codecs/|' sound/soc/mediatek/mt6785/*.c
! grep -q '"../codecs/' sound/soc/mediatek/mt6785/*.c
cat > sound/soc/mediatek/mt6785/Makefile <<'MK'
# SPDX-License-Identifier: GPL-2.0

# platform driver
snd-soc-mt6785-afe-y := \
	mt6785-afe-pcm.o \
	mt6785-afe-clk.o \
	mt6785-afe-gpio.o \
	mt6785-dai-adda.o \
	mt6785-afe-control.o \
	mt6785-dai-i2s.o \
	mt6785-dai-pcm.o \
	mt6785-dai-tdm.o

obj-$(CONFIG_SND_SOC_MT6785) += snd-soc-mt6785-afe.o

# machine driver
obj-$(CONFIG_SND_SOC_MT6785_MT6359) += mt6785-mt6359.o
MK
python3 - <<'PY'
p='sound/soc/mediatek/Kconfig'; s=open(p).read()
s=s.replace('config SND_SOC_MT7986\n','''config SND_SOC_MT6785
	tristate "ASoC support for Mediatek MT6785 chip"
	depends on ARCH_MEDIATEK || COMPILE_TEST
	depends on COMMON_CLK
	select SND_SOC_MEDIATEK
	select MFD_SYSCON if SND_SOC_MT6359
	help
	  This adds ASoC platform driver support for Mediatek MT6785 chip
	  that can be used with other codecs.
	  Select Y if you have such device.
	  If unsure select "N".

config SND_SOC_MT6785_MT6359
	tristate "ASoC Audio driver for MT6785 with MT6359 codec"
	depends on SND_SOC_MT6785 && MTK_PMIC_WRAP
	select SND_SOC_MT6359
	select SND_SOC_MT6359_ACCDET
	imply SND_SOC_TFA9874
	help
	  This adds the ASoC machine driver for MediaTek MT6785 boards
	  with the MT6359 codec (Xiaomi Redmi Note 8 Pro): headphones,
	  earpiece, microphones, jack detection and the TFA9874 speaker.
	  Select Y if you have such device.
	  If unsure select "N".

config SND_SOC_MT7986
''',1)
open(p,'w').write(s)
p='sound/soc/mediatek/Makefile'; s=open(p).read()
s=s.replace('obj-$(CONFIG_SND_SOC_MT6797) += mt6797/\n','obj-$(CONFIG_SND_SOC_MT6785) += mt6785/\nobj-$(CONFIG_SND_SOC_MT6797) += mt6797/\n',1)
open(p,'w').write(s)
PY
grep -q SND_SOC_MT6785 sound/soc/mediatek/Makefile
MSG="ASoC: mediatek: Add MT6785 AFE and MT6359 machine driver

Port the MT6785 audio front end from the vendor 4.14 kernel to the
mainline mtk-afe framework: memif/IRQ tables, ADDA, I2S, PCM and TDM
DAIs, clocks (incl. the AUDIO_TOP_CON0 reset needed for DMA to run after
runtime resume), GPIO pinctrl states and the interconnection matrix.

The IRQ handler recovers from an interrupt storm at the end of long
captures (MISS_CLR + CLR on the first empty interrupt, line disabled
after 5000 of them).

The machine driver binds the AFE to the MT6359 codec (headphones,
earpiece, AIN0/AIN2 microphones, ACCDET jack) and the TFA9874 speaker
amplifier on I2S3."
commit sound/soc/mediatek

# --- 6. CONSYS (WiFi/BT power, STP, HCI) --------------------------------------------------------------
D=drivers/staging/mt6785-consys
mkdir -p $D
cp $P/wifi/consys-bringup.c $P/wifi/h4_recv.h $D/
cp $P/wifi/gen4m-port/shim/include/wmt_exp.h $D/
# production defaults (they used to be passed to insmod): stage=8 calib=3 bt_hci=1
sed -i -E 's/^static int stage;/static int stage = 8;/; s/^static unsigned int calib = 1;/static unsigned int calib = 3;/; s/^static bool bt_hci;/static bool bt_hci = true;/' $D/consys-bringup.c
grep -q '^static int stage = 8;' $D/consys-bringup.c && grep -q '^static unsigned int calib = 3;' $D/consys-bringup.c && grep -q '^static bool bt_hci = true;' $D/consys-bringup.c
cat > $D/Kconfig <<'KC'
# SPDX-License-Identifier: GPL-2.0
config MT6785_CONSYS
	tristate "MediaTek MT6785 CONNSYS (WiFi/BT) bring-up"
	depends on ARCH_MEDIATEK || COMPILE_TEST
	depends on BT && MFD_SYSCON && MTK_PMIC_WRAP
	select CRC16
	help
	  Powers up the MT6785 connectivity subsystem without the vendor
	  WMT stack: power domain, clocks, PMIC XO buffer, firmware download
	  over BTIF/STP and the WMT handshake. Registers an HCI device for
	  Bluetooth and exports the WMT API used by the gen4m WLAN driver.

	  Module name: consys-bringup.
KC
cat > $D/Makefile <<'MK'
# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_MT6785_CONSYS) += consys-bringup.o
MK

G4=drivers/staging/mt6785-gen4m
mkdir -p $G4
( cd "$G" && find . -type f \( -name '*.c' -o -name '*.h' -o -name Makefile \) ! -path './test/*' -print0 | \
	tar --null -cf - -T - ) | tar -C $G4 -xf -
rm -f $G4/Makefile.*
mkdir -p $G4/shim
cp $P/wifi/gen4m-port/shim/include/*.h $G4/shim/
# Build parameters that used to be passed to make (gen4m_build.sh) now live in the Makefile
python3 - "$G4/Makefile" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
head='''# In-tree build of the gen4m port for MT6785 (begonia). These used to be make
# command line variables of the out-of-tree build (port/wifi/tools/gen4m_build.sh).
CONFIG_MTK_COMBO_WIFI := $(CONFIG_MT6785_GEN4M)
CONFIG_MTK_COMBO_WIFI_HIF := axi
WLAN_CHIP_ID := 6785
CONFIG_MTK_PLATFORM := mt6785
MTK_COMBO_CHIP := CONNAC
WIFI_IP_SET := 2
MTK_ANDROID_EMI := y
ccflags-y += -I$(src)/shim -include $(src)/shim/gen4m_compat.h
ccflags-y += -I$(srctree)/drivers/staging/mt6785-consys
DRIVER_BUILD_DATE := $(KBUILD_BUILD_TIMESTAMP)
'''
s=head+s
# a build date from shell date breaks reproducibility; use the string from kbuild
s=s.replace("DRIVER_BUILD_DATE=$(shell date +%Y%m%d%H%M%S)\n","")
# the vendor forces modpost failures on warnings - in the tree this would break the build of the whole kernel
s=s.replace("ifeq ($(KBUILD_MODPOST_FAIL_ON_WARNINGS),)\n    # Force build fail on modpost warning\n    KBUILD_MODPOST_FAIL_ON_WARNINGS=y\nendif\n","")
assert "KBUILD_MODPOST_FAIL_ON_WARNINGS=y" not in s
open(p,'w').write(s)
PY
cat > $G4/Kconfig <<'KC'
# SPDX-License-Identifier: GPL-2.0
config MT6785_GEN4M
	tristate "MediaTek gen4m WLAN driver for MT6785 (CONNAC)"
	depends on MT6785_CONSYS && CFG80211
	help
	  The vendor gen4m WLAN driver (MiCode MTK_kernel_modules,
	  connectivity/wlan/core/gen4m), ported to Linux 6.16 for the
	  MT6785 CONNSYS (AXI). Needs WIFI_RAM_CODE_soc1_0_2a_1.bin and
	  soc1_0_ram_wifi_*.bin firmware. The module cannot be unloaded.

	  Module name: wlan_6785_axi.
KC
staging_add() {	# $1 = directory name in drivers/staging, $2 = Kconfig symbol
python3 - "$1" "$2" <<'PY'
import sys
d,sym=sys.argv[1],sys.argv[2]
p='drivers/staging/Kconfig'; s=open(p).read()
s=s.replace('\nendif # STAGING','\nsource "drivers/staging/%s/Kconfig"\n\nendif # STAGING'%d,1)
open(p,'w').write(s)
p='drivers/staging/Makefile'; s=open(p).read()
s+='obj-$(CONFIG_%s)\t+= %s/\n'%(sym,d)
open(p,'w').write(s)
PY
}
staging_add mt6785-consys MT6785_CONSYS
MSG="staging: Add the MT6785 CONNSYS bring-up driver

Power up the MT6785 connectivity subsystem without the vendor WMT
stack: genpd + CONN regulators, the MT6359 DCXO buffer, firmware
patches over BTIF (PIO and AP_DMA vFIFOs), STP full mode, the WMT
handshake and RF calibration.

Bluetooth is exposed as hci0 on top of STP; the BD address can be set
via the bdaddr parameter. The WMT entry points the gen4m WLAN driver
expects (mtk_wcn_wmt_*, EMI base/size) are exported from here.

Defaults are the configuration proven on the Redmi Note 8 Pro
(stage=8 calib=3 bt_hci=1)."
commit drivers/staging/Kconfig drivers/staging/Makefile $D

staging_add mt6785-gen4m MT6785_GEN4M
MSG="staging: Add the gen4m WLAN driver ported to Linux 6.16 for MT6785

Import connectivity/wlan/core/gen4m from MiCode/MTK_kernel_modules
(cc4a1521) after port/wifi/gen4m-port/port.py: cfg80211/netdev API
updates for 6.16, a WMT shim (shim/wmt_exp.h, gen4m_compat.h) backed
by mt6785-consys, AXI HIF only. The build parameters that used to be
passed on the make command line are set in the Makefile.

Vendor code, staging quality: warnings are not errors here."
commit drivers/staging/Kconfig drivers/staging/Makefile $G4

git log --oneline -8
