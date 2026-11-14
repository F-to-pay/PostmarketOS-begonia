import wave, struct, math, glob, os
rows=[]
for f in sorted(glob.glob('/mnt/c/Users/1342c/workspace/Begonia/wifi/audio/clips/cal/*.wav')):
    w=wave.open(f); n=w.getnframes(); ch=w.getnchannels()
    d=struct.unpack('<%dh'%(n*ch), w.readframes(n))
    # take the middle of the recording (1..2.5 s), channel R (less noise, L = 0 in silence)
    a=d[int(48000*1.0)*ch:int(48000*2.5)*ch]
    r=a[1::ch]; l=a[0::ch]
    def rms(x): return math.sqrt(sum(v*v for v in x)/max(1,len(x)))
    rows.append((os.path.basename(f), rms(l), rms(r)))
base=None
for name,l,r in rows:
    db=lambda x: 20*math.log10(x/32768) if x>0 else -999
    print('%-12s  L rms=%7.1f (%6.1f dBFS)   R rms=%7.1f (%6.1f dBFS)'%(name,l,db(l),r,db(r)))
