#!/bin/sh
# Diagnostics of a powered slot: pins, vcamio, clocks, I2C error. sudo sh diag.sh <slot>
S=${1:-4}
P=/sys/kernel/debug/begonia-cam/power
echo "$S on" > $P; sleep 0.3
cat /sys/kernel/debug/begonia-cam/state
PC=$(ls -d /sys/kernel/debug/pinctrl/*10005000* 2>/dev/null | head -1)
echo "== pins ($PC)"
for g in 93 94 114 115 116 117 118 119 120 121 122 123 124 125 195 196 197 198 199 110 111 112 113; do
	grep -E "^pin $g |^ *$g[: ]|^\s*0*$g:" "$PC/pins" 2>/dev/null | head -1
done
echo "== pinconf-pins head"; head -3 "$PC/pins" 2>/dev/null
echo "== vcamio"; for r in /sys/class/regulator/regulator.*; do n=$(cat $r/name 2>/dev/null); case "$n" in *vcamio*) echo "$n state=$(cat $r/state) uV=$(cat $r/microvolts 2>/dev/null)";; esac; done
echo "== clk"; grep -E "camtg|univpll_192m_d8 " /sys/kernel/debug/clk/clk_summary | head -8
echo "== i2c errno"; python3 - <<'PY'
import os,fcntl,ctypes,glob
class M(ctypes.Structure): _fields_=[('a',ctypes.c_uint16),('f',ctypes.c_uint16),('l',ctypes.c_uint16),('b',ctypes.c_void_p)]
class D(ctypes.Structure): _fields_=[('m',ctypes.POINTER(M)),('n',ctypes.c_uint32)]
import time
for unit in ('11009000','11011000','1101a000'):
    bus=[p for p in glob.glob('/sys/class/i2c-dev/i2c-*') if os.path.realpath(p+'/device/of_node').endswith(unit)][0]
    fd=os.open('/dev/'+bus.rsplit('/',1)[1],os.O_RDWR)
    for a in (0x10,0x2d,0x37,0x3d):
        b=ctypes.create_string_buffer(1); m=(M*1)(M(a,1,1,ctypes.addressof(b))); t=time.time()
        try: fcntl.ioctl(fd,0x0707,D(m,1)); r='ACK'
        except OSError as e: r='errno %d %s'%(e.errno,os.strerror(e.errno))
        print(unit,hex(a),r,'%.0f ms'%((time.time()-t)*1000))
PY
echo "$S off" > $P
