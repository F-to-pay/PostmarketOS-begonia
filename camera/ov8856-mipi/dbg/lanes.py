import time, itertools
D="/sys/kernel/debug/camsv-tm"
def w(b,o,v): open(D+"/w","w").write("%s %x %x"%(b,o,v&0xffffffff))
def r(b,o):
    open(D+"/r","w").write("%s %x 1"%(b,o)); return int(open(D+"/r").read().split(":")[1],16)
for o in (0x20,0x24,0x28,0x2c): w("ana",0xa000+o,(1<<29)|(0x20<<16)|(4<<8))
for o in (0x10,0x14): w("ana",0xa000+o,(1<<28))
hits=0
for lc,d0,d1 in itertools.permutations(range(6),3):
    w("ana",0xa004,0x80000000|(lc<<0)|(d0<<8)|(d1<<12)|(7<<16)|(7<<20))
    w("seninf",0x4ac8,0xffffffff); time.sleep(0.08)
    s=r("seninf",0x4ac8); c=r("seninf",0x4adc); f=r("seninf",0x4ad0)
    if s or c or f: print("lc %d d0 %d d1 %d: irq %08x cnt %08x lf %08x"%(lc,d0,d1,s,c,f)); hits+=1
print("done", hits)
w("ana",0xa004,0x80403102)
