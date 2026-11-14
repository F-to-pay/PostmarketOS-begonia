#!/bin/bash
cd /mnt/c/Users/1342c/workspace/Begonia/wifi/module
norm() { grep -E '^(# )?CONFIG_[A-Z0-9_]+( is not set|=)' "$1" | sed -E 's/^# (CONFIG_[A-Z0-9_]+) is not set/\1=n/' | sort; }
norm phone-config > /tmp/a.cfg
norm ~/mt6785-linux/.config > /tmp/b.cfg
echo "== options whose value differs (phone -> ours):"
join -t= -j1 <(sed 's/=/=\t/;s/=\t/\t/' /tmp/a.cfg | sort -k1,1 -t$'\t') <(sed 's/=/=\t/;s/=\t/\t/' /tmp/b.cfg | sort -k1,1 -t$'\t') >/dev/null 2>&1
diff <(cat /tmp/a.cfg) <(cat /tmp/b.cfg) | grep -E '^[<>]' | grep -vE 'CC_VERSION_TEXT|CLANG_VERSION|LD_VERSION|LLD_VERSION|GCC_VERSION|AS_VERSION|PAHOLE_VERSION|RUSTC' | head -60
