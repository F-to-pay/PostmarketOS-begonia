import struct,re,sys
d=open('tfa98xx.cnt','rb').read()
names={}
for m in re.finditer(r'TFA9874_BF_(\w+)\s*=\s*0x([0-9a-fA-F]+)',open('tfa9874_tfafieldnames.h').read()):
    names.setdefault(int(m.group(2),16),m.group(1))
def ptr(o):
    v=struct.unpack_from('<I',d,o)[0]; return v&0xffffff, v>>24
def s(off):
    e=d.index(b'\0',off); return d[off:e].decode()
def items(off,n,hdr):
    out=[]
    for i in range(n):
        po,pt=ptr(off+hdr+4*i)
        if pt==3: out.append(('str',s(po)))
        elif pt==2:
            addr=d[po]; val=struct.unpack_from('<H',d,po+1)[0]; msk=struct.unpack_from('<H',d,po+3)[0]
            out.append(('reg',addr,val,msk))
        elif pt==16:
            v,f=struct.unpack_from('<HH',d,po)
            out.append(('bf',f,v,names.get(f,'?')))
        elif pt==1: out.append(('prof',po))
        elif pt==5: out.append(('patch',po,d[po:po+16].hex()))
        else: out.append(('other',pt,po))
    return out
n,bus,dev,fn,devid=struct.unpack_from('<BBBBI',d,0x36)
print('DEVICE addr',hex(dev),'n',n)
for it in items(0x36,n,12):
    if it[0]=='reg': print('  REG 0x%02x = 0x%04x (mask 0x%04x)'%it[1:])
    elif it[0]=='bf': print('  BF  reg 0x%02x pos %d len %d = 0x%x   %s'%(it[1]>>8,(it[1]>>4)&15,(it[1]&15)+1,it[2],it[3]))
    else: print('  ',it)
for it in items(0x36,n,12):
    if it[0]=='prof':
        po=it[1]; v=struct.unpack_from('<I',d,po)[0]; ln=v&0xff
        print('PROFILE',s(ptr(po+4)[0]),'len',ln)
        for j in items(po,ln-1,8):
            if j[0]=='reg': print('  REG 0x%02x = 0x%04x (mask 0x%04x)'%j[1:])
            elif j[0]=='bf': print('  BF  reg 0x%02x pos %d len %d = 0x%x   %s'%(j[1]>>8,(j[1]>>4)&15,(j[1]&15)+1,j[2],j[3]))
            else: print('  ',j)
