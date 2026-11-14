mount -t debugfs none /sys/kernel/debug 2>/dev/null
B=/sys/firmware/devicetree/base
echo "=== nodes mentioning novatek/nvt"
grep -rla -E 'novatek|nvt|NVT' $B 2>/dev/null | head -20
echo "=== gpio chips"; grep -E '^gpiochip' /sys/kernel/debug/gpio
echo "=== gpio lines with a consumer"; grep -E 'gpio-[0-9]+ \(' /sys/kernel/debug/gpio | grep -v '(                    |  ' | head -60
