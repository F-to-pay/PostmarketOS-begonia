echo "=== downstream LCM sources (csot nt36672a)"
find ~/miker -ipath "*lcm*" \( -iname "*csot*nt36672a*" -o -iname "*nt36672a*csot*" -o -iname "*nt36672a*" \) -type f 2>/dev/null | head -10
echo "=== mainline panel driver(s)"
find ~/ntsrc/drivers/gpu/drm/panel -iname "*nt36672a*" 2>/dev/null
echo "=== DT panel node in pmOS DTS"
grep -n -B2 -A22 "panel@0" ~/ntsrc/arch/arm64/boot/dts/mediatek/mt6785-xiaomi-begonia.dts | head -50
