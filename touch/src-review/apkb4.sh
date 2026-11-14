P=~/pmwork
echo "=== firmware-xiaomi-begonia* anywhere"
find $P ~/pmbootstrap ~/bootwork -maxdepth 9 \( -name "firmware-xiaomi-begonia*" -o -name "*begonia-touchscreen*" \) 2>/dev/null | head -10
echo "=== grep in pmaports for the package name"
grep -rl "firmware-xiaomi-begonia-touchscreen" $P/cache_git/pmaports 2>/dev/null | head -5
echo "=== kernel config relevant (enabled / module)"
C=$P/cache_git/pmaports/device/testing/linux-postmarketos-mediatek-mt6785/config-postmarketos-mediatek-mt6785.aarch64
grep -n -E "^CONFIG_(TOUCHSCREEN_NOVATEK|SPI_MT65XX|SPI_MTK|REGULATOR_MT6360|MFD_MT6360|DRM_PANEL_NOVATEK|DRM_PANEL_TIANMA|REGULATOR_MT6359|REGULATOR_MT6358|MFD_MT6397|PINCTRL_MT6785|SPI_SPIDEV|SPI_MEM|SPI_GPIO|PINCTRL_MTK_PARIS)" $C
echo "=== rootfs firmware pkgs inside pmbootstrap chroot"
ls ~/pmwork/chroot_rootfs_xiaomi-begonia/usr/lib/firmware/novatek ~/pmwork/chroot_rootfs_xiaomi-begonia/lib/firmware/novatek 2>&1 | head
apk info -r -e firmware-xiaomi-begonia-touchscreen 2>&1 | head -2
grep -l "begonia" ~/pmwork/chroot_rootfs_xiaomi-begonia/lib/apk/db/installed 2>/dev/null | head -2
grep -n -B1 -A12 "P:firmware-xiaomi-begonia" ~/pmwork/chroot_rootfs_xiaomi-begonia/lib/apk/db/installed 2>/dev/null | head -40
