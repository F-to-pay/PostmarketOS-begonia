D="/sys/kernel/debug/camsv-tm"
import collections,sys
def r(b,o):
    open(D+"/r","w").write("%s %x 1"%(b,o)); return int(open(D+"/r").read().split(":")[1],16)
for o in [int(x,16) for x in sys.argv[1:]]:
    c=collections.Counter(r("ana",o) for _ in range(300))
    print("%05x"%o, " ".join("%08x:%d"%kv for kv in c.most_common(6)))
