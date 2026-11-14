cd ~/miker/drivers/input/touchscreen/NT36672A_G7
echo "######## nt36xxx.c: nvt_write_addr, clear_fw_status (around 380-460)"; sed -n '380,460p' nt36xxx.c
echo "######## fw_update.c: bld_crc_enable / fw_crc_enable / boot_ready"; 
for fn in nvt_bld_crc_enable nvt_fw_crc_enable nvt_boot_ready nvt_set_bld_crc_bank; do n=$(grep -n "^static void $fn\|^void $fn\|^static int32_t $fn" nt36xxx_fw_update.c | head -1 | cut -d: -f1); echo "--- $fn @ $n"; [ -n "$n" ] && sed -n "${n},$((n+32))p" nt36xxx_fw_update.c; done
