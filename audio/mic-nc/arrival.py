import subprocess, time, select, os, sys

lat = sys.argv[1] if len(sys.argv) > 1 else '20'
p = subprocess.Popen(['parec', '--device=alsa_input.platform-sound.analog-stereo-in', '--format=s16le', '--rate=48000',
                      '--channels=2', '--raw', '--latency-msec=' + lat, '--client-name=arr'],
                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
fd = p.stdout.fileno()
os.set_blocking(fd, False)
t0 = time.time()
last = None
events = []
total = 0
while time.time() - t0 < 6:
    r, _, _ = select.select([fd], [], [], 0.5)
    if not r:
        continue
    try:
        d = os.read(fd, 1 << 20)
    except BlockingIOError:
        continue
    if not d:
        break
    now = time.time()
    total += len(d)
    events.append((now - t0, len(d)))
p.terminate()
print('requested latency %s ms; got %d bytes in 6 s (%.2f s of audio), chunks: %d' % (lat, total, total / 192000.0, len(events)))
gaps = [events[i][0] - events[i - 1][0] for i in range(1, len(events))]
sizes = [e[1] for e in events]
if gaps:
    gaps.sort()
    print('gaps between chunks: median %.0f ms, max %.0f ms; chunk size: median %d bytes (%.0f ms), max %d bytes (%.0f ms)' % (
        gaps[len(gaps) // 2] * 1000, gaps[-1] * 1000, sorted(sizes)[len(sizes) // 2], sorted(sizes)[len(sizes) // 2] / 192.0,
        max(sizes), max(sizes) / 192.0))
