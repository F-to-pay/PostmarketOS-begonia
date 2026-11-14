D="/sys/kernel/debug/camsv-tm"
def w(b,o,v): open(D+"/w","w").write("%s %x %x"%(b,o,v&0xffffffff))
def r(b,o):
    open(D+"/r","w").write("%s %x 1"%(b,o)); return int(open(D+"/r").read().split(":")[1],16)
for base in (0x8000,0x9000):
    orig=r("ana",base+0x80)
    out=[]
    for sel in range(256):
        w("ana",base+0x80,(orig&~0xff00)|(sel<<8))
        vals=set(r("ana",base+0x84) for _ in range(6))
        if vals!={0}: out.append("%02x:%s"%(sel,"/".join("%x"%v for v in sorted(vals))))
    w("ana",base+0x80,orig)
    print("%x:"%base," ".join(out))
