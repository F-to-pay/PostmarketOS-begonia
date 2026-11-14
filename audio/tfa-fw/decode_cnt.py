import struct,sys
d=open(sys.argv[1],'rb').read()
names="dev prof reg str file patch marker mode inSel outSel progCfg lagW gains vbat sensCal sensDel bitfield default live liveStr group cmd mbdrc filter noinit features cfmem fwk vddp".split()
def ptr(o):
    v=struct.unpack_from('<I',d,o)[0]; return v&0xffffff, v>>24
def s(off):
    e=d.index(b'\0',off); return d[off:e].decode()
nd,np_,nl=struct.unpack_from('<HHH',d,40)
print('ndev',nd,'nprof',np_,'nlive',nl)
o=46; idx=[]
while True:
    off,t=ptr(o); o+=4
    if t==6 and off==0xa5a5a5: break
    idx.append((off,t))
print('index',[(hex(a),names[b]) for a,b in idx])
def dump_list(off,n,hdrlen):
    for i in range(n):
        po,pt=ptr(off+hdrlen+4*i)
        if pt==3: print('   str',repr(s(po)))
        elif pt==2:
            a=d[po]; val,mask=struct.unpack_from('<HH',d,po+1) if False else (None,None)
            # regpatch: addr(1) value(2) mask(2) packed?
            addr=d[po]; val=(d[po+1]<<8)|d[po+2]; msk=(d[po+3]<<8)|d[po+4]
            print('   reg  0x%02x = 0x%04x mask 0x%04x'%(addr,val,msk))
        elif pt==16:
            v,f=struct.unpack_from('<HH',d,po)
            print('   bitfield value=0x%x field=0x%04x'%(v,f))
        else:
            print('   ',names[pt],hex(po),d[po:po+12].hex())
for off,t in idx:
    if t==0:
        n,bus,dev,fn,devid=struct.unpack_from('<BBBBI',d,off)
        no,nt=ptr(off+8)
        print('DEVICE',s(no),'items',n,'addr',hex(dev),'devid',hex(devid))
        dump_list(off,n,12)
    elif t==1:
        v=struct.unpack_from('<I',d,off)[0]; n=v&0xff; grp=(v>>8)&0xff; pid=v>>16
        no,nt=ptr(off+4)
        print('PROFILE',s(no),'len',n,'group',grp,'id',hex(pid))
        dump_list(off,n-1,8)
