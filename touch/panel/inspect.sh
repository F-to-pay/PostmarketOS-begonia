cd /mnt/c/Users/1342c/workspace/pmos-begonia/nvtpanel/rd
head -c 8 ramdisk.bin | xxd | head -1
for c in "gzip -dc" "zstd -dc" "lz4 -dc" "xz -dc"; do
  if $c ramdisk.bin 2>/dev/null | head -c 6 | grep -q "070701"; then echo "compression: $c"; fi
done
(gzip -dc ramdisk.bin 2>/dev/null || zstd -dc ramdisk.bin 2>/dev/null || lz4 -dc ramdisk.bin 2>/dev/null) > ramdisk.cpio
ls -l ramdisk.cpio; head -c 6 ramdisk.cpio; echo
cpio -t < ramdisk.cpio 2>/dev/null | grep -E "panel-novatek|novatek-nvt|lm363|lm36274|mt6360" | head
