import wave, struct, math
fs=48000; freqs=[100,200,400,800,1600,3200]; dur=2.5; amp=32767*10**(-20/20)
w=wave.open('/mnt/c/Users/1342c/workspace/Begonia/wifi/audio/clips/tones.wav','wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(fs)
buf=bytearray()
for f in freqs:
    n=int(fs*dur)
    for i in range(n):
        t=i/fs; g=min(1.0,t/0.2,(dur-t)/0.2); g=max(g,0.0)
        s=int(amp*g*math.sin(2*math.pi*f*t)); buf+=struct.pack('<hh',s,s)
w.writeframes(bytes(buf)); w.close()
