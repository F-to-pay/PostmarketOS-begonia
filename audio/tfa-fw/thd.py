import wave, struct, math, sys, glob, os
fs=48000
def goertzel(x,f):
    n=len(x); k=2*math.cos(2*math.pi*f/fs); s1=s2=0.0
    for v in x:
        s0=v+k*s1-s2; s2=s1; s1=s0
    p=s1*s1+s2*s2-k*s1*s2
    return math.sqrt(max(p,0))*2/n
def analyze(path):
    w=wave.open(path); n=w.getnframes(); ch=w.getnchannels()
    d=struct.unpack('<%dh'%(n*ch), w.readframes(n))
    x=[v/32768 for v in d[1::ch]]   # R
    # find the start: the first frame with rms>threshold
    win=2400; thr=0.0
    rm=[math.sqrt(sum(v*v for v in x[i:i+win])/win) for i in range(0,len(x)-win,win)]
    base=sorted(rm)[len(rm)//10]
    start=next(i for i,r in enumerate(rm) if r>base*4)*win
    freqs=[100,200,400,800,1600,3200]
    out=[]
    for j,f in enumerate(freqs):
        seg=x[start+int((j*2.5+0.6)*fs): start+int((j*2.5+2.0)*fs)]
        if len(seg)<fs: break
        h1=goertzel(seg,f)
        harm=[goertzel(seg,f*k) for k in range(2,7) if f*k<20000]
        thd=math.sqrt(sum(h*h for h in harm))/h1 if h1>0 else 0
        tot=math.sqrt(sum(v*v for v in seg)/len(seg))*math.sqrt(2)  # amplitude from rms
        noise=math.sqrt(max(tot*tot-h1*h1-sum(h*h for h in harm),0))/h1 if h1>0 else 0
        out.append((f,20*math.log10(h1+1e-9),100*thd,100*noise))
    return out
for p in sys.argv[1:]:
    print(os.path.basename(p))
    for f,l,thd,noise in analyze(p):
        print('  %5d Hz: fundamental %6.1f dBFS   harm.(2-6) %5.1f%%   other(noise/subbands) %5.1f%%'%(f,l,thd,noise))
