#!/bin/bash
cd /mnt/c/Users/1342c/AppData/Local/Temp/claude/19a7f945aec98efc/4bbf7d92-6853-4a95-9c07-94d3141d08b3/scratchpad/vendor
mkdir -p bin && debugfs -R 'dump /bin/wmt_launcher bin/wmt_launcher' vendor.img 2>/dev/null
debugfs -R 'dump /bin/wmt_loader bin/wmt_loader' vendor.img 2>/dev/null
ls -l bin
file bin/wmt_launcher | cut -c1-140
echo "=== strings (patch/addr/rom related)"
strings -n 6 bin/wmt_launcher | grep -iE 'patch|rom|addr|hdr|soc1|firmware|mt6785|srh|0x' | head -60
