cd ~/miker/drivers/input/touchscreen/NT36672A_G7
n=$(grep -n "nvt_xiaomi_config_info_show\|static int32_t c_show_xiaomi\|XIAOMI_CONFIG_INFO" nt36xxx_ext_proc.c | head -3); echo "$n"
grep -n -i -E "xiaomi_config|XIAOMI_CONFIG" nt36xxx_ext_proc.c | head
f=$(grep -n -i "static int32_t nvt_xiaomi_config_info_show\|nvt_xiaomi_config_info_show" nt36xxx_ext_proc.c | head -1 | cut -d: -f1)
[ -n "$f" ] && sed -n "$((f-2)),$((f+40))p" nt36xxx_ext_proc.c
