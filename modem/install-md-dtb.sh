#!/bin/sh
# Installs ~/begonia-md.dtb into /boot (backup next to it: .pre-md) and rebuilds the initramfs. Then reboot. Rollback: rollback-md-dtb.sh
set -eu
NEW=$HOME/begonia-md.dtb
B=/boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb
[ -s "$NEW" ] || { echo "no $NEW - run sh make-md-dtb.sh first"; exit 1; }
sudo sh -eu -c "
  [ -e /boot/mt6785-xiaomi-begonia.dtb.pre-md ] || cp -a '$B' /boot/mt6785-xiaomi-begonia.dtb.pre-md
  cp '$NEW' '$B'
  md5sum '$B' '$NEW'
  mkinitfs
"
echo "done, reboot; rollback: sh rollback-md-dtb.sh"
