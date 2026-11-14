#!/bin/sh
# Copies the overlay into the local pmaports and recalculates the checksums:
#   * linux-postmarketos-mediatek-mt6785: APKBUILD + config + kernel patches (kernel/patches)
#   * begonia-port: services/configs/tools + the -firmware subpackage (the firmware comes from FWDIR, it is not in port/)
# Run in WSL:  sh scripts/apply-overlay.sh          (checksum needs sudo)
#                   sh scripts/apply-overlay.sh --no-checksum
# FWDIR - directory with the proprietary firmware (WIFI_RAM_CODE_soc1_0_2a_1.bin, soc1_0_*_2a_1_hdr.bin,
#         nt36672a_begonia_csot.bin.zst); default ~/begonia-fw
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
PMA=${PMAPORTS:-$HOME/pmwork/cache_git/pmaports}
FWDIR=${FWDIR:-$HOME/begonia-fw}
PKG=device/testing/linux-postmarketos-mediatek-mt6785
PORT=device/testing/begonia-port
NM=device/testing/plasma-nm	# plasma-nm 6.8.3 + patch: the Wi-Fi switch of the mobile KCM no longer loops (Alpine has plasma-nm in community)
export PATH="$HOME/.local/bin:$PATH"

[ -d "$PMA/$PKG" ] || { echo "no $PMA/$PKG (set PMAPORTS=...)"; exit 1; }

# clean out the old versions of our patches so that they do not pile up
rm -f "$PMA/$PKG"/0*.patch
cp "$HERE/pmaports/$PKG/APKBUILD" "$HERE/pmaports/$PKG/config-postmarketos-mediatek-mt6785.aarch64" "$PMA/$PKG/"
cp "$HERE"/kernel/patches/*.patch "$PMA/$PKG/"

# the patch list in source= is generated from kernel/patches (the file names depend on the commit subjects)
python3 "$HERE/scripts/gen-source-list.py" "$PMA/$PKG/APKBUILD" "$HERE"/kernel/patches/*.patch

# begonia-port
rm -rf "$PMA/$PORT"
mkdir -p "$PMA/$PORT"
cp "$HERE/pmaports/$PORT"/* "$PMA/$PORT/"
for f in WIFI_RAM_CODE_soc1_0_2a_1.bin soc1_0_patch_mcu_2a_1_hdr.bin soc1_0_ram_bt_2a_1_hdr.bin \
	soc1_0_ram_mcu_2a_1_hdr.bin soc1_0_ram_wifi_2a_1_hdr.bin; do
	[ -s "$FWDIR/$f" ] || { echo "no firmware $FWDIR/$f (FWDIR=...)"; exit 1; }
	cp "$FWDIR/$f" "$PMA/$PORT/"
done

# plasma-nm: patched copy of the Alpine APKBUILD (pkgrel=1 > the one in the Alpine repo, so apk prefers ours)
rm -rf "$PMA/$NM"
mkdir -p "$PMA/$NM"
cp "$HERE/pmaports/$NM"/* "$PMA/$NM/"

if [ "${1:-}" != "--no-checksum" ]; then
	pmbootstrap checksum linux-postmarketos-mediatek-mt6785
	pmbootstrap checksum begonia-port
	pmbootstrap checksum plasma-nm
fi
echo "overlay applied: $PMA/$PKG, $PMA/$PORT"
