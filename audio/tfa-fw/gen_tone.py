import wave, struct, math, sys
db = float(sys.argv[1]); out = sys.argv[2]
fs = 48000; dur = 4.0; fin = 1.0; fout = 0.7
amp = 32767 * 10 ** (db / 20)
w = wave.open(out, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(fs)
n = int(fs * dur); buf = bytearray()
for i in range(n):
    t = i / fs
    g = min(1.0, t / fin, (dur - t) / fout)
    g = max(0.0, g)
    s = int(amp * g * math.sin(2 * math.pi * 440 * t))
    buf += struct.pack('<hh', s, s)
w.writeframes(bytes(buf)); w.close()
