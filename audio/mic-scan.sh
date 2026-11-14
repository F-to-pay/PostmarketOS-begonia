#!/bin/sh
# Finding the top (noise-cancelling) microphone. Two rounds of 11 s. In each: for the first ~5 s TAP WITH A FINGER at the TOP edge
# (next to the front camera/earpiece), then ~5 s at the BOTTOM one (next to the USB-C port). An even tempo, like a metronome.
# From the "top/bottom" energy difference the script shows which input is where. The codec parameters mic_type1/2=3 are needed
# (/var/lib/begonia/params.snd-soc-mt6359). Usage: mic-scan
C="amixer -c 0 -q"
SRC=alsa_input.platform-sound.analog-stereo-in
pactl suspend-source "$SRC" 1 2>/dev/null
mkdir -p /tmp/ms; rm -f /tmp/ms/*.wav

round() {
	# $1 = left ADC input, $2 = right one, $3 = round name
	$C cset name='PGA_L_Mux' "$1"
	$C cset name='PGA_R_Mux' "$2"
	echo
	echo ">>> round $3: left = $1, right = $2"
	for n in 3 2 1; do printf "    start in %s...\r" "$n"; sleep 1; done
	arecord -q -D hw:0,4 -f S16_LE -r 48000 -c 2 -d 11 "/tmp/ms/$3.wav" &
	AR=$!
	echo "    >>> TOP: tap at the TOP edge (5 seconds)       "
	sleep 5.5
	echo "    >>> BOTTOM: tap at the BOTTOM edge (5 seconds) "
	wait $AR
}

round AIN1 AIN2 A
round AIN0 AIN3 B

amixer -c 0 -q cset name='PGA_L_Mux' AIN0
amixer -c 0 -q cset name='PGA_R_Mux' AIN0
pactl suspend-source "$SRC" 0 2>/dev/null

python3 - <<'EOF'
import wave, struct, math


def halves(path, ch):
    w = wave.open(path)
    n = w.getnframes()
    c = w.getnchannels()
    d = struct.unpack('<%dh' % (n * c), w.readframes(n))
    x = [v / 32768 for v in d[ch::c]]
    win = 480
    rm = [math.sqrt(sum(v * v for v in x[i:i + win]) / win) for i in range(0, len(x) - win, win)]
    per = len(rm) / 11.0
    top = rm[int(0.8 * per):int(5.2 * per)]       # 0.8..5.2 s - "top"
    bot = rm[int(6.3 * per):int(10.7 * per)]      # 6.3..10.7 s - "bottom"
    f = sorted(rm)[len(rm) // 10] + 1e-9
    e = lambda a: math.sqrt(sum(v * v for v in a) / len(a))
    db = lambda v: 20 * math.log10(v + 1e-9)
    return db(f), db(max(top)), db(max(bot)), db(e(top)), db(e(bot))


names = {('A', 0): 'AIN1', ('A', 1): 'AIN2', ('B', 0): 'AIN0 (main)', ('B', 1): 'AIN3'}
print()
print('%-18s %8s %12s %12s %14s   where the microphone is' % ('input', 'noise', 'peak TOP', 'peak BOTTOM', 'top-bottom (energy)'))
for r in 'AB':
    for ch in (0, 1):
        f, pt, pb, et, eb = halves('/tmp/ms/%s.wav' % r, ch)
        diff = et - eb
        where = 'at the TOP' if diff > 4 else ('at the BOTTOM' if diff < -4 else 'unclear (same sound)')
        print('%-18s %8.1f %12.1f %12.1f %12.1f dB   %s' % (names[(r, ch)], f, pt, pb, diff, where))
print()
print('recordings: /tmp/ms/A.wav /tmp/ms/B.wav (left = 1st input, right = 2nd)')
EOF
