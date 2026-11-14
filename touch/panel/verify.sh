cd /mnt/c/Users/1342c/workspace/pmos-begonia/nvtpanel/rd
python3 - <<'PY'
import struct
o = open('../boot-vtp-panel.img','rb').read(); page = 2048
ks, ka, rs = struct.unpack('<3I', o[8:20])
up = lambda n: (n + page - 1) // page * page
open('check.zst','wb').write(o[page+up(ks):page+up(ks)+rs])
PY
zstd -dc check.zst > check.cpio
ls -l check.cpio ramdisk_new.cpio
cmp check.cpio ramdisk_new.cpio && echo "ramdisk in the image == ramdisk_new.cpio"
# the second archive: find its start (right after the first archive) and list it
python3 - <<'PY'
d = open('check.cpio','rb').read()
n1 = len(open('ramdisk.cpio','rb').read())
print('second archive starts at', n1, 'magic', d[n1:n1+6], 'aligned to 4:', n1 % 4 == 0)
PY
tail -c +30073465 check.cpio | cpio -t 2>/dev/null
