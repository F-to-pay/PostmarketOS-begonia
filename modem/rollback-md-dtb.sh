#!/bin/sh
# Restores the DTB from before md-bringup and rebuilds the initramfs.
set -eu
B=/boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb
sudo sh -eu -c "
  [ -s /boot/mt6785-xiaomi-begonia.dtb.pre-md ] || { echo 'no .pre-md backup'; exit 1; }
  cp /boot/mt6785-xiaomi-begonia.dtb.pre-md '$B'
  md5sum '$B'
  mkinitfs
"
echo "rollback done, reboot"
