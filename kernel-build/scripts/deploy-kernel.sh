#!/bin/sh
# Installs the freshly built kernel package on the phone over SSH. The apk trigger rebuilds the initramfs by itself
# and reflashes boot (deviceinfo_flash_kernel_on_update), like fix-boot-dtb.sh used to do.
#   sh scripts/deploy-kernel.sh [user@host]      (default user@10.233.161.187)
# Rollback: flash one of ../pmos-begonia/boot*.img from fastboot or `sudo apk add linux-...=<old version>`.
set -eu
HOST=${1:-user@10.233.161.187}
KEY=${SSH_KEY:-$HOME/.ssh/id_ed25519_claude}
SSH="ssh -i $KEY $HOST"

APK=$(ls -t "$HOME"/pmwork/packages/*/aarch64/linux-postmarketos-mediatek-mt6785-[0-9]*.apk | head -1)
[ -f "$APK" ] || { echo "no built apk, run build-kernel.sh first"; exit 1; }
echo "installing: $APK"

# safety net: the current boot files and DTB stay on the phone
$SSH 'sudo mkdir -p /root/boot-backup && sudo cp -a /boot/vmlinuz /boot/initramfs /boot/dtbs /boot/boot.img /root/boot-backup/ 2>/dev/null; ls /root/boot-backup'

scp -q -i "$KEY" "$APK" "$HOST:/tmp/kernel.apk"
$SSH 'sudo apk add --allow-untrusted /tmp/kernel.apk && sync && apk info -v | grep linux-postmarketos'
echo "done. reboot: ssh $HOST sudo systemctl reboot"
