#!/bin/bash
# dis.sh <lib> <demangled-substring> <outname>  -> lift/<outname>.s + .lift
cd "$(dirname "$0")"
L=$1; P=$2; O=$3
sym=$(llvm-nm -D --defined-only $L | while read a t n; do echo "$a $n $(echo $n | llvm-cxxfilt)"; done | grep -F "$P" | head -1)
addr=$(echo $sym | awk '{print $1}'); name=$(echo $sym | awk '{print $2}')
size=$(llvm-nm -D -S --defined-only $L | grep " $name\$" | awk '{print $2}')
end=$(printf '0x%x' $((0x$addr + 0x$size)))
llvm-objdump -d --no-show-raw-insn --start-address=0x$addr --stop-address=$end $L > lift/$O.s
python3 ../../../../../../../workspace/Begonia/port/camera/ref/hal-lift.py lift/$O.s > lift/$O.lift 2>/dev/null || python3 /mnt/c/Users/1342c/workspace/Begonia/port/camera/ref/hal-lift.py lift/$O.s > lift/$O.lift
echo "$O: 0x$addr size 0x$size $(wc -l < lift/$O.s) insn"
