#!/bin/sh
# Installs ~/begonia-cam.dtb into /boot (backup next to it: .pre-cam) and rebuilds the initramfs. Then reboot. Rollback: rollback-cam-dtb.sh
set -eu
NEW=$HOME/begonia-cam.dtb
B=/boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb
[ -s "$NEW" ] || { echo "no $NEW - run make-cam-dtb.sh first"; exit 1; }
sudo sh -eu -c "
  [ -e /boot/mt6785-xiaomi-begonia.dtb.pre-cam ] || cp -a '$B' /boot/mt6785-xiaomi-begonia.dtb.pre-cam
  cp '$NEW' '$B'
  md5sum '$B' '$NEW'
  mkinitfs
"
echo "done, reboot; rollback: sh rollback-cam-dtb.sh"
