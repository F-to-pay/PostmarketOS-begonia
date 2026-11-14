import sys,ctypes,fcntl,os,glob
class M(ctypes.Structure): _fields_=[("a",ctypes.c_uint16),("f",ctypes.c_uint16),("l",ctypes.c_uint16),("b",ctypes.c_void_p)]
class Dd(ctypes.Structure): _fields_=[("m",ctypes.POINTER(M)),("n",ctypes.c_uint32)]
fd=os.open("/dev/i2c-7",os.O_RDWR)
for a in sys.argv[1:]:
    reg=int(a,16); wb=ctypes.create_string_buffer(bytes([reg>>8,reg&255]),2); rb=ctypes.create_string_buffer(1)
    fcntl.ioctl(fd,0x0707,Dd((M*2)(M(0x10,0,2,ctypes.addressof(wb)),M(0x10,1,1,ctypes.addressof(rb))),2))
    print("%04x=%02x"%(reg,rb.raw[0]),end=" ")
print()
