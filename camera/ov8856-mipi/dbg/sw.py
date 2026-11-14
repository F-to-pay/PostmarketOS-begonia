import sys,ctypes,fcntl,os
class M(ctypes.Structure): _fields_=[("a",ctypes.c_uint16),("f",ctypes.c_uint16),("l",ctypes.c_uint16),("b",ctypes.c_void_p)]
class Dd(ctypes.Structure): _fields_=[("m",ctypes.POINTER(M)),("n",ctypes.c_uint32)]
fd=os.open("/dev/i2c-7",os.O_RDWR)
for a in sys.argv[1:]:
    reg,val=[int(x,16) for x in a.split("=")]
    b=ctypes.create_string_buffer(bytes([reg>>8,reg&255,val]),3)
    fcntl.ioctl(fd,0x0707,Dd((M*1)(M(0x10,0,3,ctypes.addressof(b))),1))
