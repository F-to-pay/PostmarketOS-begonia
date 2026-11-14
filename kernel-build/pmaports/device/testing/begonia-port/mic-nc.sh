#!/bin/sh
# Dual-microphone noise suppression (bottom main AIN0 + top noise-cancelling AIN2).
#   mic-nc calibrate   - 6 s recording of room SILENCE (do not talk): computes the bottom/top noise ratio per frequency
#   mic-nc start       - enable: a microphone "Mic-NoiseSuppressed" appears (it becomes the default source)
#   mic-nc stop        - disable, restore everything as it was
#   mic-nc status      - state
#   mic-nc demo        - 10 s recording: raw and after noise suppression at the same time, files ~/mic-nc-demo/{raw,nc,ab}.wav to listen to
#   mic-nc test        - 10 s: talk for 5 s, stay silent for 5 s; shows how much the noise is suppressed and whether speech is preserved
# Parameters (env): NC_ALPHA (suppression strength, 1.4), NC_GMIN (minimum gain, 0.12 = -18 dB).
SRC=alsa_input.platform-sound.analog-stereo-in
FIFO=/tmp/mic_nc.fifo
CAL=$HOME/.config/mic-nc.cal
DSP=/usr/bin/mic-nc-dsp
FMT="--format=s16le --rate=48000 --raw"
C="amixer -c 0 -q"

route_top() { $C cset name='PGA_R_Mux' AIN2; }
route_main() { $C cset name='PGA_R_Mux' AIN0; }
running() { pgrep -f "client-name=micnc-fresh" >/dev/null 2>&1; }

# PulseAudio (stream-restore) remembers where an application stream went before and on the next start may move it to the wrong place
# (e.g. the pipeline read stream onto its own output mic_nc = a loop, silence on the input). Attach the streams by force.
# $1 = part of the application name (client-name), $2 = source for readers, $3 = sink for writers (may be empty)
pin_streams() {
	for id in $(pactl list short source-outputs | cut -f1); do
		if pactl list source-outputs | awk -v id="$id" '$0=="Source Output #"id{f=1;next} /^Source Output #/{f=0} f' | grep -q "$1"; then
			pactl move-source-output "$id" "$2" 2>/dev/null
		fi
	done
	if [ -n "$3" ]; then
		for id in $(pactl list short sink-inputs | cut -f1); do
			if pactl list sink-inputs | awk -v id="$id" '$0=="Sink Input #"id{f=1;next} /^Sink Input #/{f=0} f' | grep -q "$1"; then
				pactl move-sink-input "$id" "$3" 2>/dev/null
			fi
		done
	fi
}

# Make sure the suppressor output really produces sound (>= ~100 KB of the expected 192 KB in 2 s); if not - restart the pipeline.
ensure_flow() {
	n=0
	while [ $n -lt 2 ]; do
		got=$(timeout 2 parec --device=mic_nc $FMT --channels=1 --latency-msec=20 --client-name=micnc-probe 2>/dev/null | wc -c)
		[ "$got" -ge 100000 ] && return 0
		echo "    (the suppressor output is silent: $got bytes in 2 s, restarting the pipeline)"
		"$0" stop >/dev/null 2>&1
		"$0" start >/dev/null 2>&1
		sleep 1
		n=$((n + 1))
	done
	return 1
}

case "$1" in
calibrate)
	was=0; running && was=1
	echo "Calibration: 6 seconds of SILENCE. Do not talk, do not touch the phone, leave the room background noise as it is."
	route_top
	sleep 1
	mkdir -p "$(dirname "$CAL")"
	( sleep 0.7; pin_streams mic-nc-cal $SRC "" ) &
	parec --device=$SRC $FMT --channels=2 --client-name=mic-nc-cal 2>/dev/null | head -c 1152000 | $DSP --calibrate "$CAL"
	[ $was = 1 ] || route_main
	echo "Done: $CAL"
	;;
start)
	running && { echo "already running"; exit 0; }
	[ -r "$CAL" ] || echo "WARNING: no calibration ($CAL), working with c=1. Better: mic-nc calibrate"
	route_top
	rm -f "$FIFO"
	# source from a FIFO: the processor writes mono s16le 48 kHz straight into it (no null-sink: it has a fixed 2 s latency)
	pactl load-module module-pipe-source source_name=mic_nc file=$FIFO format=s16le rate=48000 channels=1 source_properties=device.description=Mic-NoiseSuppressed >/dev/null
	setsid sh -c "parec --device=$SRC $FMT --channels=2 --latency-msec=20 --client-name=micnc-fresh | NC_LIVE=1 $DSP --cal $CAL > $FIFO" >/dev/null 2>>/tmp/mic-nc.log &
	sleep 1
	pactl set-default-source mic_nc
	# attach AFTER changing the default source (otherwise PulseAudio moves the reader onto mic_nc = a loop)
	for n in 1 2 3 4; do pin_streams micnc-fresh $SRC ""; sleep 0.4; done
	echo "Noise suppression on: default source = Mic-NoiseSuppressed. Turn off: mic-nc stop"
	;;
stop)
	pkill -f "client-name=micnc-fresh" 2>/dev/null
	pactl set-default-source $SRC 2>/dev/null
	for m in $(pactl list short modules | awk '/module-pipe-source/ && /mic_nc/{print $1}'); do pactl unload-module "$m"; done
	# old modules of the previous scheme (null-sink + remap), if any are left
	for m in $(pactl list short modules | awk '/module-remap-source/ && /mic_nc/{print $1}'); do pactl unload-module "$m"; done
	for m in $(pactl list short modules | awk '/module-null-sink/ && /mic_nc_sink/{print $1}'); do pactl unload-module "$m"; done
	rm -f "$FIFO"
	route_main
	echo "Noise suppression off, the microphone is as before (bottom, AIN0)."
	;;
status)
	if running; then echo "running"; else echo "off"; fi
	echo "default: $(pactl get-default-source)"
	echo "calibration: $( [ -r "$CAL" ] && echo "$CAL" || echo "none" )"
	;;
test)
	running || { echo "first: mic-nc start"; exit 1; }
	ensure_flow || { echo "ERROR: the noise suppressor gives no sound even after a restart (see /tmp/mic-nc.log)"; exit 1; }
	mkdir -p /tmp/ncT; rm -f /tmp/ncT/raw.raw /tmp/ncT/nc.raw
	echo "Test of 10 seconds. 3.. 2.. 1.."; sleep 3
	( timeout 11 parec --device=$SRC $FMT --channels=2 --latency-msec=20 --client-name=mic-nc-test-raw > /tmp/ncT/raw.raw 2>/dev/null ) &
	( timeout 11 parec --device=mic_nc $FMT --channels=1 --latency-msec=20 --client-name=mic-nc-test-nc > /tmp/ncT/nc.raw 2>/dev/null ) &
	( sleep 0.5; pin_streams mic-nc-test-raw $SRC ""; pin_streams mic-nc-test-nc mic_nc "" ) &
	echo ">>> TALK in a normal voice (5 seconds): 'one, two, three, microphone check...'"
	sleep 5.2
	echo ">>> STAY SILENT (5 seconds), background noise as it is"
	wait
	python3 - <<'PY'
import struct, math
def load(p, ch, n):
    d = open(p, 'rb').read()
    cnt = len(d) // (2 * n)
    x = struct.unpack('<%dh' % (cnt * n), d[:cnt * n * 2])
    return [v / 32768 for v in x[ch::n]]
def rms(a): return math.sqrt(sum(v * v for v in a) / max(1, len(a)))
db = lambda v: 20 * math.log10(v + 1e-9)
raw = load('/tmp/ncT/raw.raw', 0, 2)      # bottom microphone as is
nc = load('/tmp/ncT/nc.raw', 0, 1)        # after noise suppression
def env(a):
    return [rms(a[i:i + 480]) for i in range(0, len(a) - 480, 480)]
er, en = env(raw), env(nc)
best = (-1, 0)
for lag in range(0, 150):                       # 0..1.5 s of pipeline delay
    n = min(len(er), len(en) - lag)
    if n < 100:
        break
    c = sum(er[i] * en[i + lag] for i in range(n))
    if c > best[0]:
        best = (c, lag)
lag_s = best[1] * 0.01
nc = nc[best[1] * 480:]
print('delay of the suppressor output relative to the microphone: %.2f s' % lag_s)
def seg(x, a, b): return x[int(a * 48000):int(b * 48000)]
for name, x in (('bottom microphone as is', raw), ('after suppression', nc)):
    s = rms(seg(x, 0.8, 4.8)); nz = rms(seg(x, 6.0, 9.8))
    print('%-26s speech %6.1f dBFS   noise %6.1f dBFS   speech/noise difference %5.1f dB' % (name, db(s), db(nz), db(s) - db(nz)))
sr, nr = rms(seg(raw, 0.8, 4.8)), rms(seg(raw, 6.0, 9.8))
sn, nn = rms(seg(nc, 0.8, 4.8)), rms(seg(nc, 6.0, 9.8))
print()
print('noise suppressed by %.1f dB, speech changed by %+.1f dB => speech/noise ratio gain %.1f dB' % (db(nr) - db(nn), db(sn) - db(sr), (db(sn) - db(nn)) - (db(sr) - db(nr))))
print('(if speech dropped a lot - lower NC_ALPHA; if noise is audible - raise it; redo mic-nc calibrate in the same room)')
PY
	;;
demo)
	# 10 second recording: the raw bottom microphone and the suppressor output at the same time. Files in ~/mic-nc-demo/:
	#   raw.wav (as is), nc.wav (after suppression), ab.wav (first raw, a pause, then nc: the same recording)
	# The loudness is levelled the same way for both (both are amplified so that the loud raw peak is around -3 dBFS).
	tmp_started=0
	if ! running; then "$0" start >/dev/null; tmp_started=1; sleep 1; fi
	ensure_flow || { echo "ERROR: the noise suppressor gives no sound even after a restart (see /tmp/mic-nc.log)"; exit 1; }
	mkdir -p "$HOME/mic-nc-demo"; rm -f "$HOME/mic-nc-demo"/*.raw
	echo "Recording 10 seconds. Talk and turn on/make background noise (TV, water, street). 3.. 2.. 1.."; sleep 3
	( timeout 11 parec --device=$SRC $FMT --channels=2 --latency-msec=20 --client-name=micnc-demo-raw > "$HOME/mic-nc-demo/raw.raw" 2>/dev/null ) &
	( timeout 11 parec --device=mic_nc $FMT --channels=1 --latency-msec=20 --client-name=micnc-demo-nc > "$HOME/mic-nc-demo/nc.raw" 2>/dev/null ) &
	( sleep 0.5; pin_streams micnc-demo-raw $SRC ""; pin_streams micnc-demo-nc mic_nc "" ) &
	echo ">>> TALK with the noise on (10 seconds)..."
	wait
	[ $tmp_started = 1 ] && "$0" stop >/dev/null
	python3 - <<'PY'
import os, struct, math, wave
d = os.path.expanduser('~/mic-nc-demo/')
def load(p, ch, n):
    b = open(p, 'rb').read(); cnt = len(b) // (2 * n)
    return [v for v in struct.unpack('<%dh' % (cnt * n), b[:cnt * n * 2])][ch::n]
raw = load(d + 'raw.raw', 0, 2)
nc = load(d + 'nc.raw', 0, 1)
shift = int(0.05 * 48000)                 # the suppressor pipeline lags ~50 ms: trim so that the recordings line up in time
raw = raw[:len(raw) - shift] if shift < len(raw) else raw
nc = nc[shift:]
n = min(len(raw), len(nc)); raw, nc = raw[:n], nc[:n]
def p90(a):
    w = 4800
    e = sorted(math.sqrt(sum(v * v for v in a[i:i + w]) / w) for i in range(0, len(a) - w, w))
    return e[int(len(e) * 0.9)] or 1
# the suppressor output contains the NC_GAIN_DB gain (default +18 dB): remove it so that the A/B comparison is fair in loudness
nc = [v / (10 ** (float(os.environ.get('NC_GAIN_DB', '18')) / 20)) for v in nc]
# common gain (the same for both): 90th percentile of speech loudness (per 100 ms) ~ -20 dBFS, soft limiter against clipping
g = min(300.0, 0.1 * 32767 / p90(raw))
def soft(v):
    x = v * g / 32768.0
    ax = abs(x)
    if ax > 0.7:
        ax = 0.7 + 0.3 * math.tanh((ax - 0.7) / 0.3)
    return int(math.copysign(ax, x) * 32767)
def amp(a): return [soft(v) for v in a]
def save(name, a):
    w = wave.open(d + name, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
    w.writeframes(struct.pack('<%dh' % len(a), *a)); w.close()
ra, na = amp(raw), amp(nc)
save('raw.wav', ra); save('nc.wav', na); save('ab.wav', ra + [0] * 48000 + na)
rms = lambda a: math.sqrt(sum(v * v for v in a) / len(a)) / 32768
print('gain for listening: +%.1f dB (the same for both)' % (20 * math.log10(g)))
print('done: raw.wav (as is), nc.wav (after suppression), ab.wav (first as is, then suppressed)')
PY
	rm -f "$HOME/mic-nc-demo"/*.raw
	echo "Listen on the phone:  paplay ~/mic-nc-demo/ab.wav   (or raw.wav / nc.wav separately)"
	echo "Fetch to the PC: scp user@minilin.local:mic-nc-demo/*.wav ."
	;;
*)
	sed -n '2,10p' "$0"
	;;
esac
