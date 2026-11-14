#!/bin/sh
# Restores the DTB from before the camera install (/boot/...pre-cam) and rebuilds the initramfs.
set -eu
B=/boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb
[ -e /boot/mt6785-xiaomi-begonia.dtb.pre-cam ] || { echo "no .pre-cam backup"; exit 1; }
sudo sh -eu -c "cp /boot/mt6785-xiaomi-begonia.dtb.pre-cam '$B'; md5sum '$B'; mkinitfs"
echo "rollback done, reboot"
