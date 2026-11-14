cd ~/miker/drivers/input/touchscreen/NT36672A_G7
echo "=== SPI_RD_FAST / 3F310 in G7"; grep -n -i -E "RD_FAST|3F310|fast" nt36xxx.c nt36xxx.h nt36xxx_fw_update.c nt36xxx_mem_map.h | head -30
echo "=== nvt_write_addr usages around bootloader reset (lines 515-535) + nvt_bootloader_reset body"; sed -n '515,535p' nt36xxx.c
echo "=== probe: dtsi parse of fast addr"; grep -n -E "spi-rd-fast|swrst-n8|SWRST_N8_ADDR|SPI_RD" nt36xxx.c | head
