cd /tmp && rm -f nvfw.bin && zstd -dc ~/pmwork/chroot_rootfs_xiaomi-begonia/lib/firmware/novatek/nt36672a_begonia_csot.bin.zst > nvfw.bin && ls -l nvfw.bin
python3 - <<'PY'
d = open('/tmp/nvfw.bin','rb').read()
import struct
print('size', len(d))
for name, pat in (('04 a7 34 cd', bytes.fromhex('04a734cd')), ('cd 34 a7 04 (LE of 0x04a734cd)', bytes.fromhex('cd34a704'))):
    offs = []; i = d.find(pat)
    while i != -1 and len(offs) < 10: offs.append(i); i = d.find(pat, i+1)
    print(name, '->', [hex(o) for o in offs])
# header fields used by the mainline driver (struct firmware_header)
ilm_bin, ilm_sram, ilm_size, dlm_bin, dlm_sram, dlm_size, crc_ilm, crc_dlm = struct.unpack('<8I', d[:32])
print('ilm bin/sram/size', hex(ilm_bin), hex(ilm_sram), hex(ilm_size), '| dlm', hex(dlm_bin), hex(dlm_sram), hex(dlm_size))
print('crc ilm', hex(crc_ilm), 'crc dlm', hex(crc_dlm))
# partition headers at 0x30: 16 bytes each: sram, size, bin, crc
n = (ilm_bin - 0x30) // 16
print('info partitions:', n)
for k in range(n):
    sram, size, binaddr, crc = struct.unpack('<4I', d[0x30 + 16*k: 0x30 + 16*k + 16])
    mark = '  <== 0x04a734cd / cd34a704' if crc in (0x04a734cd, 0xcd34a704) else ''
    print(f'  part{k}: sram={sram:#x} size={size:#x} bin={binaddr:#x} crc={crc:#010x}{mark}')
PY
