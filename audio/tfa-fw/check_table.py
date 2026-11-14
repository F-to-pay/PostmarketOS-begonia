import re,subprocess
src=open("../codecs/tfa9874.c").read()
body=src.split("cnt_dev_bfs[] = {")[1].split("\n};")[0]
mine=[(int(a,16),int(b,16)) for a,b in re.findall(r"\{ (0x[0-9a-f]{4}), (0x[0-9a-f]{2}) \}",body)]
out=subprocess.run(["python3","decode2.py"],capture_output=True,text=True).stdout.split("PROFILE")[0]
ref=[]
for l in out.splitlines():
    m=re.match(r"\s+BF  reg 0x(\w+) pos (\d+) len (\d+) = 0x(\w+)",l)
    if m:
        r,p,n,v=int(m.group(1),16),int(m.group(2)),int(m.group(3)),int(m.group(4),16)
        ref.append(((r<<8)|(p<<4)|(n-1),v))
print(len(mine),len(ref),"SAME" if mine==ref else "DIFF")
for a,b in zip(mine,ref):
    if a!=b: print("diff",hex(a[0]),hex(a[1]),hex(b[0]),hex(b[1]))
