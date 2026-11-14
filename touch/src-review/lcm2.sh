echo "=== downstream: files mentioning csot_fhd_nt36672a"
grep -rIl "csot_fhd_nt36672a" ~/miker --include=*.c --include=*.h 2>/dev/null | head -8
echo "=== mainline panel driver: csot/begonia init parts"
F=~/ntsrc/drivers/gpu/drm/panel/panel-novatek-nt36672a.c
wc -l $F; grep -n -E "begonia|csot|tianma|on_cmds|init_seq|struct panel_desc|\.compatible|nt36672a_.*_cmds|usleep|msleep|mipi_dsi_dcs|dsi_generic" $F | head -60
