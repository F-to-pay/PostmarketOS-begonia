#!/bin/sh
# Export of the commits of the begonia-fixes branch (the kernel working tree in WSL) into kernel/patches/.
# The source of truth for the kernel is this branch; after changes run the script and then apply-overlay.sh.
#   sh scripts/export-patches.sh
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
KDIR=${KDIR:-$HOME/mt6785-linux}
OUT="$HERE/kernel/patches"

cd "$KDIR"
git rev-parse --verify -q 6.16.4-r0 >/dev/null || { echo "no tag 6.16.4-r0 in $KDIR"; exit 1; }
rm -f "$OUT"/0*.patch
git format-patch -q --no-signature -o "$OUT" 6.16.4-r0..begonia-fixes
ls "$OUT"
echo
echo "do not forget: the patch list in pmaports/.../APKBUILD (source=) must match this list"
