#!/bin/sh
# Builds the kernel package (and DTB) with pmbootstrap. Needs sudo (pmbootstrap mounts a chroot) —
# run it in an interactive WSL session, it will ask for the password once.
#   sh scripts/build-kernel.sh            # apply-overlay + build
#   sh scripts/build-kernel.sh --boot     # additionally build boot.img (no pmbootstrap install needed)
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
export PATH="$HOME/.local/bin:$PATH"

sh "$HERE/scripts/apply-overlay.sh"
pmbootstrap build --force linux-postmarketos-mediatek-mt6785

APK=$(ls -t "$HOME"/pmwork/packages/*/aarch64/linux-postmarketos-mediatek-mt6785-[0-9]*.apk | head -1)
echo
echo "built: $APK"
echo "next:  sh scripts/deploy-kernel.sh"
