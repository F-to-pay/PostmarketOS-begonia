import sys,time
D="/sys/kernel/debug/camsv-tm"
def w(b,o,v): open(D+"/w","w").write("%s %x %x"%(b,o,v&0xffffffff))
def r(b,o):
    open(D+"/r","w").write("%s %x 1"%(b,o)); return int(open(D+"/r").read().split(":")[1],16)
def rmw(b,o,c,s): w(b,o,(r(b,o)&~c)|s)
for a in [int(x,16) for x in sys.argv[1:]]:
    for o in (0x18,0x1c,0x20): rmw("ana",a+o,1|0x10000,0)
    rmw("ana",a,12,0); time.sleep(0.001)
    rmw("ana",a,0,8); time.sleep(0.001); rmw("ana",a,0,4); time.sleep(0.001)
    for o in (0x18,0x1c,0x20): rmw("ana",a+o,0,1|0x10000)
    time.sleep(0.01)
for a in (0,0x1000,0x4000,0x5000,0x8000,0x9000): print("%05x: +0=%08x +48=%08x"%(a,r("ana",a),r("ana",a+0x48)))
