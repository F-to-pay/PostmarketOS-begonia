P=~/pmwork/cache_git/pmaports/device/testing/firmware-xiaomi-begonia
echo "==== APKBUILD"; cat $P/APKBUILD; echo "==== dir"; ls -la $P
echo "==== firmware blobs in rootfs: size, sha256"
cd ~/pmwork/chroot_rootfs_xiaomi-begonia/lib/firmware/novatek
ls -la; sha256sum *; (command -v zstd >/dev/null && for f in *.zst; do echo "$f raw size: $(zstd -dc $f | wc -c)  sha256: $(zstd -dc $f | sha256sum | cut -c1-16)"; done)
echo "==== files inside the apk"
tar -tzvf ~/pmwork/cache_apk_aarch64/firmware-xiaomi-begonia-touchscreen-20250810-r0.c7116f82.apk 2>/dev/null | head
