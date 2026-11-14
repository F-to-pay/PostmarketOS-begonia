cd ~/miker/drivers/input/touchscreen/NT36672A_G7
echo "=== xiaomi_config_info / FAE / config version reads"
grep -n -i -E "config_info|FAE ID|Reservation byte|xiaomi_config|EVENT_MAP_(FWINFO|PROJECTID|RESET_COMPLETE|HOST_CMD|HANDSHAKING)|0x1F64E|F64E" nt36xxx.c nt36xxx_ext_proc.c nt36xxx.h nt36xxx_mem_map.h | head -30
