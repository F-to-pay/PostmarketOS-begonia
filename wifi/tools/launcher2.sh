#!/bin/bash
cd /mnt/c/Users/1342c/AppData/Local/Temp/claude/19a7f945aec98efc/4bbf7d92-6853-4a95-9c07-94d3141d08b3/scratchpad/vendor/bin
llvm-objdump -d --no-show-raw-insn wmt_launcher > launcher.asm 2>/dev/null
wc -l launcher.asm
# addresses of the interesting strings in .rodata
for s in 'read patch info' 'patch info:0x' 'read patch_info failed' 'read patch_build_ver' 'invalid patch full name'; do
  off=$(grep -obUa "$s" wmt_launcher | head -1 | cut -d: -f1)
  echo "string '$s' file offset=$off (0x$(printf %x $off))"
done
llvm-readelf -S -W wmt_launcher | grep -E 'rodata|\.text|\.data' 
