B=/sys/firmware/devicetree/base
echo "=== ts_xsfer_state"; cat /sys/bus/spi/devices/spi5.0/ts_xsfer_state 2>&1
echo "=== proc nvt*"; ls /proc | grep -iE 'nvt|nt36|touch|tp'; for f in /proc/nvt_fw_version /proc/nvt_pid /proc/nvt_lockdown_info /proc/tp_info /proc/touchpanel/* ; do [ -f $f ] && { echo "--- $f"; cat $f 2>&1 | head -8; }; done
echo "=== xiaomi_touch dt"; for f in $(ls $B/xiaomi_touch 2>/dev/null); do [ -f $B/xiaomi_touch/$f ] && { printf "%s = " $f; od -An -tx1 -v $B/xiaomi_touch/$f | head -2 | tr -d '\n'; echo; }; done
echo "=== vtp ldo node"; P=$(cat $B/__symbols__/mt_pmic_vtp_ldo_reg | tr -d '\0'); echo $P; N=$B${P#/}; N=$(echo $P | sed "s#^#$B#"); for f in $(ls $N 2>/dev/null); do [ -f $N/$f ] && { printf "  %s = " $f; cat $N/$f | tr '\0' ' ' | head -c 100; echo; }; done
echo "=== regulators vtp/vsp/vsn/lcm/ts"; grep -iE 'vtp|vsp|vsn|lcm|panel|ts_|tp' /sys/kernel/debug/regulator/regulator_summary | head
echo "=== pinctrl pin 1,2,44,35-38 state"; for p in 1 2 44 35 36 37 38; do grep -E "pin $p \(" /sys/kernel/debug/pinctrl/*/pins 2>/dev/null | head -1; done
echo "=== pinconf for spi/ts pins"; for f in /sys/kernel/debug/pinctrl/*/pinmux-pins; do grep -E '^pin (1|2|44|35|36|37|38) ' $f; done 2>/dev/null
