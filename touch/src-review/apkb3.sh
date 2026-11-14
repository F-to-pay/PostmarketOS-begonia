P=~/pmwork/cache_git/pmaports
D=$P/device/testing/device-xiaomi-begonia
for f in deviceinfo kernel-cmdline.conf modules-initfs; do echo "==== $f"; cat $D/$f; echo; done
echo "==== firmware-xiaomi-begonia-touchscreen"
F=$(find $P -type d -name "firmware-xiaomi-begonia-touchscreen" | head -1); echo $F; cat $F/APKBUILD; ls -la $F
echo "==== kernel config: touch/spi/regulator/pinctrl options"
C=$P/device/testing/linux-postmarketos-mediatek-mt6785/config-postmarketos-mediatek-mt6785.aarch64
grep -n -E "NOVATEK|NVT|SPI_MT65XX|CONFIG_SPI=|MT6360|REGULATOR_MT6360|PINCTRL_MT6785|DRM_PANEL_NOVATEK|DRM_PANEL_TIANMA|PANEL_FOLLOWER|TOUCHSCREEN|REGULATOR_FIXED|REGULATOR_MT6359|INPUT_TOUCH" $C | head -30
