cd ~/miker/drivers/input/touchscreen/NT36672A_G7
ls | head -20
echo "=== spi_read_write / CTP_SPI_READ"; awk '/static inline int32_t spi_read_write|int32_t CTP_SPI_READ|int32_t nvt_set_page/{p=1} p{print NR": "$0} /^}/{if(p){c++; p=0}} ' nt36xxx.c | head -110
echo "=== bootloader reset + resume flow"; grep -n -E "nvt_bootloader_reset|nvt_update_firmware|nvt_ts_resume|nvt_get_fw_info|nvt_check_fw_reset_state|msleep|mdelay|gpio_set_value|nvt_hw_reset|gpio_direction" nt36xxx.c | head -60
