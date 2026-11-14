#!/bin/bash
# Clean install of pmOS for begonia with our kernel and begonia-port + moving personal data from the backup into the image.
# Run in WSL (passwordless sudo needed):  bash install-image.sh <backup dir with system.tar/home.tar>
# Result: ~/begonia-flash/{boot.img,xiaomi-begonia.img} for fastboot (boot / userdata).
set -euo pipefail
B=${1:?backup dir}
export PATH="$HOME/.local/bin:$PATH"
W=$HOME/pmwork
OUT=$HOME/begonia-flash
EXTRA=alligator,audiotube,bash,bc,bison,breeze-gtk,build-base,cava,clang,cowsay,cups,discover,dolphin,dtc,elfutils-dev,elisa,fastfetch,fish,flatpak,flex,font-droid,font-droid-nonlatin,font-twemoji,git,itinerary,kalgebra,kalk,kasts,kclock,kde-gtk-config,kdeconnect,keysmith,krecorder,kweather,ladspa,lang,linux-headers,lld,llvm,lsp-plugins-ladspa,make,musl-utils,nano,nethack,openssl-dev,pahole,perl,plasma-camera,plasma-dialer,plasma-keyboard,plasma-mobile-sounds,plasma-phonebook,postmarketos-artwork-wallpapers,python3,qmlkonsole,ripgrep,spacebar,strace,sudo-rs,ttyescape,tuned-ppd,v4l-utils,vvmplayer,xdg-user-dirs,xwayland,xwaylandvideobridge,begonia-port

[ "${2:-}" = "--no-install" ] || pmbootstrap install --password "begonia-tmp-$$" --add "$EXTRA"

SPARSE=$W/chroot_native/home/pmos/rootfs/xiaomi-begonia.img
[ -s "$SPARSE" ] || { echo "no image $SPARSE"; exit 1; }
# deviceinfo_flash_sparse=true -> the image is an Android sparse image; edit the raw one (fastboot splits it by itself when flashing)
sudo losetup -j "$SPARSE" | cut -d: -f1 | xargs -r -n1 sudo losetup -d
pmbootstrap chroot -- apk add -q android-tools
pmbootstrap chroot -- simg2img /home/pmos/rootfs/xiaomi-begonia.img /home/pmos/rootfs/xiaomi-begonia-raw.img
IMG=$W/chroot_native/home/pmos/rootfs/xiaomi-begonia-raw.img

# --- moving personal data into the image ----------------------------------------------------
T=$(mktemp -d)
tar -C "$T" -xf "$B/system.tar" etc/shadow etc/passwd etc/sudoers.d/claude \
	etc/NetworkManager/system-connections
tar -C "$T" -xf "$B/home.tar" user/.ssh/authorized_keys
# GPT with 4096-byte sectors (deviceinfo_rootfs_image_sector_size); WSL has no udev -> no loopXpN nodes,
# so the root (2nd partition) is mounted by offset
read -r START SIZE < <(sudo sfdisk -d --sector-size 4096 "$IMG" | awk -F'[=,]' '/img2 :/{gsub(/ /,"");print $2, $4}')
[ -n "$START" ] || { echo "did not find partition 2 in $IMG"; exit 1; }
M=$(mktemp -d)
sudo mount -o loop,offset=$((START*4096)),sizelimit=$((SIZE*4096)) "$IMG" "$M"
# password of the user user: the line from the old shadow (hash), the rest is not touched
H=$(grep '^user:' "$T/etc/shadow")
sudo sed -i "s|^user:.*|${H//|/\\|}|" "$M/etc/shadow"
# the shell as it was (fish)
SH=$(grep '^user:' "$T/etc/passwd" | cut -d: -f7)
sudo sed -i -E "s|^(user:[^:]*:[^:]*:[^:]*:[^:]*:[^:]*:).*|\1$SH|" "$M/etc/passwd"
sudo install -m 440 -o root -g root "$T/etc/sudoers.d/claude" "$M/etc/sudoers.d/claude"
sudo mkdir -p "$M/etc/NetworkManager/system-connections"
sudo cp "$T"/etc/NetworkManager/system-connections/*.nmconnection "$M/etc/NetworkManager/system-connections/"
sudo chmod 600 "$M"/etc/NetworkManager/system-connections/*
sudo chown root:root "$M"/etc/NetworkManager/system-connections/*
UID_=$(grep '^user:' "$M/etc/passwd" | cut -d: -f3)
# the uid of the user in the image does not exist on the host; uutils install does not accept a numeric -o -> chown separately
sudo install -d -m 700 "$M/home/user/.ssh"
sudo install -m 600 "$T/user/.ssh/authorized_keys" "$M/home/user/.ssh/authorized_keys"
sudo chown -R "$UID_:$UID_" "$M/home/user/.ssh"
sudo ls -lan "$M/home/user/.ssh"
grep '^user:' "$M/etc/passwd"; sudo grep -c '^user:\$' "$M/etc/shadow"
sync; sudo umount "$M"
# Check BY FILE, not by the mounted FS: on 2026-11-13 the changes did not reach the image, although they were visible
# on the mounted system (cause not found) - it had to be fixed over ssh with the temporary password.
sudo mount -o ro,loop,offset=$((START*4096)),sizelimit=$((SIZE*4096)) "$IMG" "$M"
sudo test -s "$M/home/user/.ssh/authorized_keys" && sudo grep -qF "$H" "$M/etc/shadow" || {
	sudo umount "$M"; echo "ERROR: the changes were not written to $IMG - do not flash!"; exit 1; }
sudo umount "$M"; rm -rf "$T" "$M"

# --- files for fastboot ------------------------------------------------------------------------
mkdir -p "$OUT"
cp "$W/chroot_rootfs_xiaomi-begonia/boot/boot.img" "$OUT/boot.img"
cp "$IMG" "$OUT/xiaomi-begonia.img"
ls -la "$OUT"
