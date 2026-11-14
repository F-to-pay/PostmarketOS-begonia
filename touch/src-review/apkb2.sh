P=~/pmwork/cache_git/pmaports/device
for f in testing/linux-postmarketos-mediatek-mt6785 testing/device-xiaomi-begonia; do echo "================ $f/APKBUILD"; cat $P/$f/APKBUILD; echo "--- files:"; ls -la $P/$f; done
echo "================ git log (mt6785 kernel pkg)"; cd ~/pmwork/cache_git/pmaports && git log --oneline -8 -- device/testing/linux-postmarketos-mediatek-mt6785 device/testing/device-xiaomi-begonia 2>/dev/null | head
