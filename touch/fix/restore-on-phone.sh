#!/bin/sh
# put the original pmOS modules back
set -e
D=/lib/modules/6.16.4/kernel/drivers/input/touchscreen
cp -a /root/nvt-backup/novatek-nvt-ts-core.ko.zst /root/nvt-backup/novatek-nvt-ts-spi.ko.zst $D/
depmod -a 6.16.4
echo restored
