set -e
cd /mnt/c/Users/1342c/workspace/pmos-begonia/nvtpanel/rd
rm -rf stage extra.cpio ramdisk_new.cpio ramdisk_new.zst
D=stage/usr/lib/modules/6.16.4/kernel/drivers/gpu/drm/panel
mkdir -p $D
zstd -q -19 -f ../panel-novatek-nt36672a.ko -o $D/panel-novatek-nt36672a.ko.zst
ls -l $D
(cd stage && find usr -type f | cpio -o -H newc --owner=0:0 2>/dev/null) > extra.cpio
ls -l extra.cpio
cat ramdisk.cpio extra.cpio > ramdisk_new.cpio
zstd -q -10 -T0 -f ramdisk_new.cpio -o ramdisk_new.zst
ls -l ramdisk.bin ramdisk_new.zst
# sanity: unpack list of the new archive shows the module twice; last one wins
cpio -t < ramdisk_new.cpio 2>/dev/null | grep -c "panel-novatek-nt36672a.ko.zst"
