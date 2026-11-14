#!/bin/sh
# install the v2 touch modules into the rootfs (originals are kept in /root/nvt-backup)
set -e
D=/lib/modules/6.16.4/kernel/drivers/input/touchscreen
mkdir -p /root/nvt-backup
[ -e /root/nvt-backup/novatek-nvt-ts-core.ko.zst ] || cp -a $D/novatek-nvt-ts-core.ko.zst $D/novatek-nvt-ts-spi.ko.zst /root/nvt-backup/
cp /tmp/novatek-nvt-ts-core.ko.zst /tmp/novatek-nvt-ts-spi.ko.zst $D/
depmod -a 6.16.4
ls -l $D/novatek-nvt-ts-*
md5sum $D/novatek-nvt-ts-*
echo "installed. originals: /root/nvt-backup. reboot now."
