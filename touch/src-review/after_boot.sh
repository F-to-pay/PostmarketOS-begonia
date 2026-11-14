cd ~/miker/drivers/input/touchscreen/NT36672A_G7
echo "=========== nt36xxx_fw_update.c: nvt_download_firmware_hw_crc (741-800)"
sed -n '741,800p' nt36xxx_fw_update.c
echo "=========== nvt_set_bld_hw_crc (718-740)"
sed -n '718,740p' nt36xxx_fw_update.c
