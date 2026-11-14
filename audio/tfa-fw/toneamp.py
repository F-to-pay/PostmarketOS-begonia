import wave, struct, math, sys, os
fs=48000; freqs=[100,200,400,800,1600,3200]
def amps(p):
    w=wave.open(p); n=w.getnframes(); ch=w.getnchannels()
    d=struct.unpack('<%dh'%(n*ch), w.readframes(n)); x=[v/32768 for v in d[0::ch]]
    out=[]
    for j in range(6):
        seg=x[int((j*2.5+1.0)*fs):int((j*2.5+1.8)*fs)]
        out.append(math.sqrt(sum(v*v for v in seg)/len(seg))*math.sqrt(2))
    return out
base=amps(sys.argv[1])
print('input (dBFS peak):', ' '.join('%d:%.1f'%(f,20*math.log10(a)) for f,a in zip(freqs,base)))
for p in sys.argv[2:]:
    a=amps(p)
    print(os.path.basename(p), ' '.join('%dHz:%+.1fdB'%(f,20*math.log10(max(x,1e-9)/b)) for f,x,b in zip(freqs,a,base)))
