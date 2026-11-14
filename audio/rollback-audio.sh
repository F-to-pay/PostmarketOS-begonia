#!/bin/sh
# Restores the DTB from before audio (vtp+touch+bat+prox+consys+wifi) and reflashes boot.
set -eu
B=/boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb
sudo sh -eu -c "
  [ -s /boot/mt6785-xiaomi-begonia.dtb.pre-audio ] || { echo 'no .pre-audio backup'; exit 1; }
  cp /boot/mt6785-xiaomi-begonia.dtb.pre-audio '$B'
  md5sum '$B' /home/user/begonia-wifi.dtb
  mkinitfs
"
echo "rollback done, reboot"
