import wave, struct, math, sys, os
fs=48000
def goertzel(x,f):
    n=len(x); k=2*math.cos(2*math.pi*f/fs); s1=s2=0.0
    for v in x:
        s0=v+k*s1-s2; s2=s1; s1=s0
    return math.sqrt(max(s1*s1+s2*s2-k*s1*s2,0))*2/n
for p in sys.argv[1:]:
    w=wave.open(p); n=w.getnframes(); ch=w.getnchannels()
    d=struct.unpack('<%dh'%(n*ch), w.readframes(n)); x=[v/32768 for v in d[1::ch]][fs//2:fs//2+24000]
    rms=math.sqrt(sum(v*v for v in x)/len(x))
    print('%s: rms=%.1f dBFS'%(os.path.basename(p),20*math.log10(rms+1e-9)))
    bins=[(f,goertzel(x,f)) for f in range(40,8000,20)]
    top=sorted(bins,key=lambda b:-b[1])[:10]
    print('  peaks:', ', '.join('%d Hz(%.0f)'%(f,20*math.log10(a+1e-9)) for f,a in sorted(top)))
    for lo,hi in [(40,200),(200,1000),(1000,3000),(3000,8000)]:
        seg=[a for f,a in bins if lo<=f<hi]; e=math.sqrt(sum(a*a for a in seg)/len(seg))
        print('  %4d-%4d Hz: avg. level %.1f dBFS'%(lo,hi,20*math.log10(e+1e-9)))
