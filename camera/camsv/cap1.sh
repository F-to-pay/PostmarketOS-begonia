#!/bin/sh
# First capture: SENINF test model (all 5, like the HAL) -> CAMSV0 -> IMGO into the buffer (no IOMMU). sudo sh cap1.sh
D=/sys/kernel/debug/camsv-tm
w() { echo "$1 $2 $3" > $D/w; }
r() { echo "$1 $2 ${3:-1}" > $D/r; cat $D/r; }
W=${W:-640}; L=${L:-480}; BPL=$((W * 10 / 8))
echo 1 > $D/power
python3 $(dirname "$(readlink -f "$0")")/larb-bypass.py
[ "$(awk '{print $4}' $D/buf)" = "0x0" ] && echo 0x200000 > $D/buf
[ -n "$FILL" ] && echo $FILL > $D/fill
PH=$(awk '{print $2}' $D/buf); echo "buffer $PH"
# --- SENINF: cam_mux EN, test model on seninf0..4 (TG +0x600, mux +0xd00, ctrl +0x200)
w seninf 410 1f
for n in 0 1 2 3 4; do
	B=$((n * 0x1000))
	w seninf $(printf %x $((B + 0xd04))) 001f0001
	w seninf $(printf %x $((B + 0xd00))) 1
	w seninf $(printf %x $((B + 0x220))) 1
	w seninf $(printf %x $((B + 0x200))) 1
	w seninf $(printf %x $((B + 0x60c))) $(printf %08x $(((L << 16) | W)))
	w seninf $(printf %x $((B + 0x610))) ${TMCLK:-7}
	w seninf $(printf %x $((B + 0x618))) ${TMDUM:-00640064}
	w seninf $(printf %x $((B + 0x608))) ${TMCTL:-00300161}
done
# --- CAMSV0: reset SW_CTL 4,0,1, wait for 3, 0
w camsv 230 0; w camsv 234 0; w camsv 510 40000000; w camsv 110 0; sleep 0.1
w camsv 520 4; w camsv 520 0; w camsv 520 1; sleep 0.05; r camsv 520; w camsv 520 0
# TG window, IMGO
w camsv 238 $(printf %08x $((W << 16)))
w camsv 23c $(printf %08x $((L << 16)))
w camsv 20 ${PH#0x}
w camsv 30 $(printf %x ${XS:-$((BPL - 1))})
w camsv 34 $(printf %x ${YS:-$((L - 1))})
w camsv 38 $(printf %x $((BPL | ${STF:-0})))
w camsv 40 20002; w camsv 44 20002
w camsv 514 ${FMT:-101}
w camsv 54c ${PAK:-2}
w camsv 110 ${FBC:-8000}
# enable
w camsv 530 8005
w camsv 510 ${MEN:-40000015}
w camsv 518 ffffffff; w camsv 400 1
w camsv 234 1
w camsv 230 ${SEN:-10001}
sleep 0.3
[ -n "$FILL" ] && echo $FILL > $D/fill
w camsv 110 $(printf %x $((0x${FBC:-8000} | 1)))   # RCNT_INC: give the DMA one buffer -> the next whole frame
sleep 0.3
echo "== CAMSV0 after start"; r camsv 230 2; r camsv 26c; r camsv 510 4; r camsv 51c; r camsv 110 2
echo "== SENINF mux0 size"; r seninf d30; r seninf 508
sleep 0.3; r camsv 51c; r camsv 51c
cat $D/bufdump | head -8
