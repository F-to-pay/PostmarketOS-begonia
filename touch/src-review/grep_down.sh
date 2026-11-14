cd ~/miker/drivers/input/touchscreen/mediatek
ls NT36xxx_HOSTDL_SPI/ | head -30
echo "---- functions"
grep -n -E "spi_read_write|CTP_SPI_READ|CTP_SPI_WRITE|nvt_set_page|SPI_READ_MASK|SPI_WRITE_MASK|spi_rd_fast|SPI_RD_FAST|nvt_bootloader_reset|nvt_check_fw_reset_state|NVT_TRANSFER_LEN" NT36xxx_HOSTDL_SPI/*.c NT36xxx_HOSTDL_SPI/*.h | head -60
