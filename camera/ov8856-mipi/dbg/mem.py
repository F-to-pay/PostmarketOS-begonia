import mmap,os,struct,sys
a=int(sys.argv[1],16); fd=os.open("/dev/mem",os.O_RDWR|os.O_SYNC)
m=mmap.mmap(fd,0x1000,offset=a&~0xfff); o=a&0xfff
if len(sys.argv)>2: m[o:o+4]=struct.pack("<I",int(sys.argv[2],16))
print("%08x: %08x"%(a,struct.unpack("<I",m[o:o+4])[0]))
