cd ~/nvtdump
grep -n -E "eng_reset|phys|module_param|DBG" novatek-nvt-ts-core.c | cut -c1-190
echo "---- sed context around eng reset"
n=$(grep -n "eng reset write" novatek-nvt-ts-core.c | head -1 | cut -d: -f1); sed -n "$((n-25)),$((n+12))p" novatek-nvt-ts-core.c
