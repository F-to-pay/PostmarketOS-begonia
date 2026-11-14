#!/bin/sh
# Installs the DTB with the audio nodes into /boot and reflashes boot (mkinitfs). Does NOT put modules into /lib/modules
# (load them by hand after a reboot: sh load-audio.sh). Rollback: sh rollback-audio.sh
# Run: sh this_file   (then reboot)
set -eu
D=/home/user
NEW=$D/begonia-audio.dtb
B=/boot/dtbs/mediatek/mt6785-xiaomi-begonia.dtb
[ -s "$NEW" ] || { echo "no $NEW - run sh make-audio-dtb.sh first"; exit 1; }
# modules are not copied here: load-audio-safe.sh takes the fresh .ko (the files in audio-modules belong to root)
echo "== root part (will ask for the sudo password) =="
sudo sh -eu -c "
  [ -e /boot/mt6785-xiaomi-begonia.dtb.pre-audio ] || cp -a '$B' /boot/mt6785-xiaomi-begonia.dtb.pre-audio
  cp '$NEW' '$B'
  md5sum '$B' '$NEW'
  mkinitfs
"
echo "--- done. Reboot, then: sh /home/user/Begonia/wifi/audio/load-audio.sh"
echo "--- rollback: sh /home/user/Begonia/wifi/audio/rollback-audio.sh"
