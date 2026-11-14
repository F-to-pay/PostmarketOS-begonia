import wave, struct, math, sys, os
fs=48000
def goertzel(x,f):
    n=len(x); k=2*math.cos(2*math.pi*f/fs); s1=s2=0.0
    for v in x:
        s0=v+k*s1-s2; s2=s1; s1=s0
    return math.sqrt(max(s1*s1+s2*s2-k*s1*s2,0))*2/n
freqs=[100,200,400,800,1600,3200]
for p in sys.argv[1:]:
    w=wave.open(p); n=w.getnframes(); ch=w.getnchannels()
    d=struct.unpack('<%dh'%(n*ch), w.readframes(n)); x=[v/32768 for v in d[1::ch]]
    print(os.path.basename(p))
    # rough synchronization: find the start from the envelope
    win=2400; rm=[math.sqrt(sum(v*v for v in x[i:i+win])/win) for i in range(0,len(x)-win,win)]
    base=sorted(rm)[len(rm)//10]; start=next(i for i,r in enumerate(rm) if r>base*3)*win
    for j,f in enumerate(freqs):
        c=start+int((j*2.5+1.25)*fs)           # center of the tone
        best=None
        for off in range(-6000,6001,2000):     # ±125 ms search
            seg=x[c+off-12000:c+off+12000]
            if len(seg)<24000: continue
            a=goertzel(seg,f)
            if best is None or a>best[0]: best=(a,seg)
        a,seg=best
        # spectrum in the window: peaks other than the fundamental
        bins=[(g,goertzel(seg,g)) for g in range(40,9000,10)]
        rel=lambda v:20*math.log10((v+1e-12)/(a+1e-12))
        others=[(g,v) for g,v in bins if abs(g-f)>25]
        top=sorted(others,key=lambda b:-b[1])[:6]
        rms=math.sqrt(sum(v*v for v in seg)/len(seg))
        print('  %4d Hz: fundamental %6.1f dBFS | top extraneous: %s'%(f,20*math.log10(a+1e-12),
              ', '.join('%d Hz(%.0f dB)'%(g,rel(v)) for g,v in sorted(top))))
