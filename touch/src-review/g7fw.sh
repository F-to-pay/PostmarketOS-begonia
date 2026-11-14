cd ~/miker/drivers/input/touchscreen/NT36672A_G7
echo "=== BOOT_RDY / SPI_RD_FAST / fw reset state in fw_update.c"; grep -n -E "BOOT_RDY|SPI_RD_FAST|nvt_check_fw_reset_state|nvt_bootloader_reset|BLD_CRC_EN|msleep|mdelay|Download_Firmware_HW_CRC|hw_crc|EVENT_MAP_RESET_COMPLETE" nt36xxx_fw_update.c | head -50
echo "=== tail of Download_Firmware_HW_CRC"
n=$(grep -n "BOOT_RDY_ADDR" nt36xxx_fw_update.c | head -1 | cut -d: -f1); [ -n "$n" ] && sed -n "$((n-30)),$((n+25))p" nt36xxx_fw_update.c
echo "=== nvt_check_fw_reset_state"
sed -n '618,692p' nt36xxx.c
