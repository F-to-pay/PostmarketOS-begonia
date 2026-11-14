import time
D="/sys/kernel/debug/camsv-tm"
def w(b,o,v): open(D+"/w","w").write("%s %x %x"%(b,o,v&0xffffffff))
def r(b,o):
    open(D+"/r","w").write("%s %x 1"%(b,o)); return int(open(D+"/r").read().split(":")[1],16)
for st in (0x02,0x06,0x0a,0x10,0x18,0x20,0x30,0x50):
  for tr in (0x00,0x04,0x0f):
    for o in (0x20,0x24,0x28,0x2c): w("ana",0xa000+o,(1<<29)|(st<<16)|(tr<<8))
    for o in (0x10,0x14): w("ana",0xa000+o,(1<<28)|(st<<16))
    w("seninf",0x4ac8,0xffffffff); time.sleep(0.12)
    s=r("seninf",0x4ac8); c=r("seninf",0x4adc); f=r("seninf",0x4ad0)
    if s or c or f: print("settle %02x trail %02x: irq %08x cnt %08x lf %08x"%(st,tr,s,c,f))
print("done")
