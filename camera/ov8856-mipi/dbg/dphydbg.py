D="/sys/kernel/debug/camsv-tm"
def w(b,o,v): open(D+"/w","w").write("%s %x %x"%(b,o,v&0xffffffff))
def r(b,o):
    open(D+"/r","w").write("%s %x 1"%(b,o)); return int(open(D+"/r").read().split(":")[1],16)
B=0xa000
o280=r("ana",B+0x280); oe0=r("ana",B+0xe0)
print("orig 280=%08x e0=%08x 288=%08x"%(o280,oe0,r("ana",B+0x288)))
for grp in (0x03020100,0x1b1a1918,0x33323130,0x4b4a4948,0x63626160,0x7b7a7978):
    w("ana",B+0x280,grp); out=[]
    for sel in range(256):
        w("ana",B+0xe0,(oe0&~0xff)|sel)
        vals=set(r("ana",B+0x288) for _ in range(5))
        if vals!={0}: out.append("%02x:%s"%(sel,"/".join("%x"%v for v in sorted(vals)[:4])))
    print("%08x:"%grp," ".join(out[:40]))
w("ana",B+0x280,o280); w("ana",B+0xe0,oe0)
