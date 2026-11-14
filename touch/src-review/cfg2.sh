cd ~/miker/drivers/input/touchscreen/NT36672A_G7
echo "=== EVENT_MAP enum"; grep -n -A18 "EVENT_MAP_HOST_CMD" nt36xxx.h | head -30
echo "=== config info proc / reader"; grep -rn -E "nvt_xiaomi_config_info|xiaomi_config_info|NVT_XIAOMI_CONFIG|config_info_read|nvt_get_xiaomi_lockdown|lockdown" *.c *.h | head -12
