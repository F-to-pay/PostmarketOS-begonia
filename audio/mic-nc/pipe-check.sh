#!/bin/sh
# Checking the PulseAudio pipeline without a microphone: processed mono file -> null-sink -> remap-source -> recording; level comparison.
cd /tmp/ncT || exit 1
/usr/local/bin/mic-nc-dsp --cal /home/user/.config/mic-nc.cal < raw.raw > ref.raw
pactl load-module module-null-sink sink_name=pc_sink sink_properties=device.description=pc >/dev/null
pactl load-module module-remap-source master=pc_sink.monitor source_name=pc_src >/dev/null
echo "--- volumes before: sink $(pactl get-sink-volume pc_sink | grep -o '[0-9]*%' | head -1), source $(pactl get-source-volume pc_src | grep -o '[0-9]*%' | head -1), monitor $(pactl get-source-volume pc_sink.monitor | grep -o '[0-9]*%' | head -1)"
parec --device=pc_src --format=s16le --rate=48000 --channels=1 --raw --client-name=pc-rec 2>/dev/null | head -c 960000 > pc.raw &
sleep 1
pacat --playback --device=pc_sink --format=s16le --rate=48000 --channels=1 --raw --client-name=pc-play < ref.raw
sleep 1
kill %1 2>/dev/null; wait 2>/dev/null
for m in $(pactl list short modules | awk '/module-remap-source/ && /pc_src/{print $1}'); do pactl unload-module $m; done
for m in $(pactl list short modules | awk '/module-null-sink/ && /pc_sink/{print $1}'); do pactl unload-module $m; done
python3 - <<'PY'
import struct, math
def load(p):
    d = open(p, 'rb').read(); n = len(d) // 2
    return [v / 32768 for v in struct.unpack('<%dh' % n, d[:n * 2])]
def rms(a): return math.sqrt(sum(v * v for v in a) / max(1, len(a)))
db = lambda v: 20 * math.log10(v + 1e-9)
ref = load('/tmp/ncT/ref.raw'); pc = load('/tmp/ncT/pc.raw')
print('reference (pipeline input): rms %.1f dBFS, length %.2f s' % (db(rms(ref)), len(ref) / 48000))
print('recorded from remap-source: rms %.1f dBFS, length %.2f s' % (db(rms(pc)), len(pc) / 48000))
# per segment: speech/noise (with offset search)
best = None
for off in range(0, 24000, 1200):
    seg = pc[off:off + 48000]
    if len(seg) < 48000: break
    r = rms(seg)
    if best is None or r > best[0]: best = (r, off)
print('loudest piece in the recording: %.1f dBFS (offset %.2f s)' % (db(best[0]), best[1] / 48000))
print('loudest piece in the reference: %.1f dBFS' % db(max(rms(ref[o:o + 48000]) for o in range(0, len(ref) - 48000, 1200))))
PY
