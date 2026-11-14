#!/usr/bin/env python3
"""M2: the first frame from OV8856 over MIPI CSI2 (2 lanes) -> SENINF2 -> mux2 -> cam mux2 -> CAMSV0 -> memory.

The PHY/CSI2/TOP/MUX sequence repeats the MT6785 HAL (libcam.halsensor.so, SeninfDrvImp::hal_csirx_*),
decoded in NOTES.md. Registers - through the camsv-tm debugfs (blocks seninf/camsv/camsys/ana).
Preconditions: loaded i2c-mt65xx-mch, cam-probe (stage=1 tg_keep=1), videodev/v4l2-*, ov8856, camsv-tm.

sudo python3 ov-cap.py [--testpat] [--settle N] [--out /tmp/ov.bin]
"""
import argparse, ctypes, fcntl, glob, os, time

D = '/sys/kernel/debug/camsv-tm'
HERE = os.path.dirname(os.path.realpath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument('--testpat', action='store_true', help='sensor test pattern (color bars)')
ap.add_argument('--settle', type=lambda x: int(x, 0), default=15)
ap.add_argument('--out', default='/tmp/ov.bin')
ap.add_argument('--w', type=int, default=640)
ap.add_argument('--l', type=int, default=480)
A = ap.parse_args()


# ---------------- registers through debugfs ----------------
def w(b, o, v):
    with open(D + '/w', 'w') as f:
        f.write('%s %x %x' % (b, o, v & 0xffffffff))


def r(b, o):
    with open(D + '/r', 'w') as f:
        f.write('%s %x 1' % (b, o))
    return int(open(D + '/r').read().split(':')[1], 16)


def rmw(b, o, clr=0, st=0):
    w(b, o, (r(b, o) & ~clr) | st)


def field(b, o, hi, lo, v):
    m = ((1 << (hi - lo + 1)) - 1) << lo
    rmw(b, o, m, (v << lo) & m)


# ---------------- sensor I2C (OV8856 @0x10 on i2c@1101a000) ----------------
class M(ctypes.Structure):
    _fields_ = [('a', ctypes.c_uint16), ('f', ctypes.c_uint16), ('l', ctypes.c_uint16), ('b', ctypes.c_void_p)]


class Dd(ctypes.Structure):
    _fields_ = [('m', ctypes.POINTER(M)), ('n', ctypes.c_uint32)]


bus = [p for p in glob.glob('/sys/class/i2c-dev/i2c-*') if os.path.realpath(p + '/device/of_node').endswith('i2c@1101a000')][0]
fd = os.open('/dev/' + bus.rsplit('/', 1)[1], os.O_RDWR)


def sw(reg, val):
    b = ctypes.create_string_buffer(bytes([reg >> 8, reg & 0xff, val]), 3)
    fcntl.ioctl(fd, 0x0707, Dd((M * 1)(M(0x10, 0, 3, ctypes.addressof(b))), 1))


def sr(reg):
    wb = ctypes.create_string_buffer(bytes([reg >> 8, reg & 0xff]), 2)
    rb = ctypes.create_string_buffer(1)
    fcntl.ioctl(fd, 0x0707, Dd((M * 2)(M(0x10, 0, 2, ctypes.addressof(wb)), M(0x10, 1, 1, ctypes.addressof(rb))), 2))
    return rb.raw[0]


def table(name, skip_stream_on=False):
    n = 0
    for line in open(os.path.join(HERE, name)):
        p = line.split()
        if not p:
            continue
        if p[0] == 'delay':
            time.sleep(int(p[1]) / 1000)
            continue
        reg, val = int(p[0], 16), int(p[1], 16)
        if skip_stream_on and reg == 0x0100 and val == 1:
            continue
        sw(reg, val)
        n += 1
    return n


# ---------------- 1. sensor power through the mainline ov8856 driver ----------------
dev = glob.glob('/sys/bus/i2c/drivers/ov8856/*-0010')[0]
open(dev + '/power/control', 'w').write('on')
time.sleep(0.05)
print('sensor: runtime', open(dev + '/power/runtime_status').read().strip(),
      'ID 0x%02x%02x' % (sr(0x300b), sr(0x300c)))
print('init: %d writes' % table('init.regs'))
print('vga : %d writes (without stream on)' % table('vga.regs', skip_stream_on=True))
if A.testpat:
    for reg, val in ((0x5000, 0x57), (0x5001, 0x02), (0x5e00, 0x80)):
        sw(reg, val)
    print('sensor test pattern: color bars')

# ---------------- 2. camera system power, LARB without translation ----------------
open(D + '/power', 'w').write('1')
os.system('python3 %s/../camsv/larb-bypass.py' % HERE)
if open(D + '/buf').read().split()[3] == '0x0':
    open(D + '/buf', 'w').write('0x200000')
phys = int(open(D + '/buf').read().split()[1], 16)
open(D + '/fill', 'w').write('0xa5')

# ---------------- 3. PHY CSI2 (HAL hal_csirx_*) ----------------
# !!! KEY: the 26 MHz MIPI-CSI references in APMIXEDSYS (AP_PLL_CON 0x1000C020, bit 6 = mipic0_26m,
# bit 11 = mipic1_26m, inverted gate: 1 = on). Without them ANA does not calibrate (+0x48 = 0),
# DPHY sees only LP transitions, CSI2 receives not a single byte. In DT/the driver - the clocks
# apmixed_mipic0_26m / apmixed_mipic1_26m (mainline does not have them).
import mmap, struct
_fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
_m = mmap.mmap(_fd, 0x1000, offset=0x1000c000)
_v = struct.unpack_from('<I', _m, 0x20)[0] | 0x40 | 0x800
_m[0x20:0x24] = struct.pack('<I', _v)
print('APMIXED 0x1000c020 = 0x%08x (mipic0/1 26M on)' % struct.unpack_from('<I', _m, 0x20)[0])
ANA = (0x8000, 0x9000)   # CSI2A, CSI2B
DPHY = 0xa000
for a in ANA:            # phyA_init
    for hi, lo, v in ((2, 0, 4), (6, 4, 4), (10, 8, 4), (14, 12, 4), (19, 16, 8)):
        field('ana', a + 0x4, hi, lo, v)
    for o in (0x18, 0x1c, 0x20):
        for hi, lo, v in ((5, 4, 1), (21, 20, 1), (7, 6, 1), (23, 22, 1)):
            field('ana', a + o, hi, lo, v)
        rmw('ana', a + o, 0, (1 << 2) | (1 << 18))
        field('ana', a + o, 11, 8, 3)
        field('ana', a + o, 27, 24, 3)
    for hi, lo in ((4, 0), (12, 8), (20, 16), (28, 24)):
        field('ana', a + 0x8, hi, lo, 0x10)
    for hi, lo in ((4, 0), (12, 8)):
        field('ana', a + 0xc, hi, lo, 0x10)
    for o in (0x30, 0x3c):
        for hi, lo in ((4, 0), (12, 8), (20, 16)):
            field('ana', a + o, hi, lo, 8)
    rmw('ana', a + 0x28, 0, 1)
    rmw('ana', a + 0x34, 0, 1)
    for o in (0x30, 0x3c):
        field('ana', a + o, 28, 24, 1)
for o in (0x20, 0x24, 0x28, 0x2c):  # dphy_init
    field('ana', DPHY + o, 23, 16, 0x20)
    field('ana', DPHY + o, 15, 8, A.settle or 4)
    rmw('ana', DPHY + o, 0, 1 << 29)

rmw('ana', 0xb018, 0, 0x10)         # setSeninfCsiMipi: CPHY top of the CSI2 port (0xb000) +0x18 |= 0x10

# CSI2 port -> SENINF4 (HAL getCSIInfo: {port,seninf} = (0,0)(1,2)(2,4)(3,0)(4,1)(5,2)(6,3))
CSI2 = 0x4a00            # seninf4 csi2: csi2_setting (D-PHY, 2 lanes)
rmw('seninf', CSI2 + 0x10, 0, 0x100)
rmw('seninf', CSI2 + 0x8, 0x7ff, 0)
rmw('seninf', CSI2 + 0x4, 1, 0)
rmw('seninf', CSI2 + 0xc0, 0, 0x80000000)
w('seninf', CSI2 + 0x0, 0x3)
w('seninf', CSI2 + 0xe0, 0xffffffff)  # DBG_CTRL: packet counters (0xd0..0xdc)
rmw('seninf', 0x68, 0x2, 0x1)    # top_setting: SENINF_TOP_PHY_CTRL_CSI2 (D-PHY)

for a in ANA:            # phyA_setting
    rmw('ana', a + 0x0, 1, 0)
rmw('ana', ANA[0], 0x20 | 0x100, 0x800)
rmw('ana', ANA[1], 0x20 | 0x100 | 0x800, 0)
for a in ANA:
    rmw('ana', a, 0, 0x40 | 0x200 | 0x1000)
for hi, lo, v in ((18, 16, 0), (10, 8, 1), (2, 0, 2), (14, 12, 3), (22, 20, 4)):  # dphy_setting
    field('ana', DPHY + 0x4, hi, lo, v)
rmw('ana', DPHY + 0x0, 0x2, 0x100 | 0x200 | 0x400 | 0x800 | 0x1)
rmw('ana', DPHY + 0x4, 0, 0x80000000)  # CK_DATA_MUX_EN (end of dphy_setting)
for a in ANA:            # phyA_power_on
    for o in (0x18, 0x1c, 0x20):
        rmw('ana', a + o, 1 | 0x10000, 0)
    rmw('ana', a, 4 | 8, 0)
    time.sleep(0.0003)
    rmw('ana', a, 0, 8); time.sleep(0.0001)
    rmw('ana', a, 0, 4); time.sleep(0.0001)
    for o in (0x18, 0x1c, 0x20):
        rmw('ana', a + o, 0, 1 | 0x10000)
    time.sleep(0.002)
    print('ANA %04x: +0x48 = 0x%08x (expect & 0x29 == 0x29)' % (a, r('ana', a + 0x48)))

rmw('seninf', 0x4200 + 0x10, 0, 1)   # seninf_setting: SENINF4 CSI2_CTRL, SENINF_CTRL
rmw('seninf', 0x4200 + 0x0, 0, 1)
rmw('seninf', 0x10, 0xf0000, 0x40000)  # TOP_MUX_CTRL_0: mux2 <- seninf4 (setSeninfTopMuxCtrl)
MUX = 0x2d00                         # mux2: MIPI source (8), 1 pixel, polarities 0
rmw('seninf', MUX + 0x4, 0x30f, 0x8)
rmw('seninf', MUX + 0x8, 0x30000, 0)
rmw('seninf', MUX + 0x0, 0, 0x6); rmw('seninf', MUX + 0x0, 0x6, 0)
rmw('seninf', MUX + 0x0, 0, 1)
w('seninf', MUX + 0xa8, 1)
rmw('seninf', 0x410, 0, 0x1f)

# ---------------- 4. CAMSV0 (like cap1.sh) ----------------
W, L = A.w, A.l
BPL = W * 10 // 8
for o, v in ((0x230, 0), (0x234, 0), (0x510, 0x40000000), (0x110, 0)):
    w('camsv', o, v)
for v in (4, 0, 1):
    w('camsv', 0x520, v)
time.sleep(0.05)
w('camsv', 0x520, 0)
for o, v in ((0x238, W << 16), (0x23c, L << 16), (0x20, phys), (0x30, BPL - 1), (0x34, L - 1), (0x38, BPL),
             (0x40, 0x20002), (0x44, 0x20002), (0x514, 0x101), (0x54c, 2), (0x110, 0x8000), (0x530, 0x8005),
             (0x510, 0x40000015), (0x518, 0xffffffff), (0x400, 1), (0x234, 1), (0x230, 0x10001)):
    w('camsv', o, v)

# ---------------- 5. streaming and one frame ----------------
sw(0x0100, 1)
time.sleep(0.3)
open(D + '/fill', 'w').write('0xa5')
w('camsv', 0x110, 0x8001)
time.sleep(0.3)

# ---------------- 6. report ----------------
print('CSI2 IRQ %08x LINE_FRAME %08x PKT %08x SHORT %08x PKT_CNT %08x' % tuple(
      r('seninf', CSI2 + o) for o in (0xc8, 0xd0, 0xd4, 0xd8, 0xdc)))
print('DPHY [0x00]=%08x [0x04]=%08x [0x30]=%08x' % (r('ana', DPHY), r('ana', DPHY + 4), r('ana', DPHY + 0x30)))
print('MUX2: SIZE %08x PIX_CNT %08x IRQ %08x MON H %d V %d' % (r('seninf', MUX + 0x30), r('seninf', MUX + 0xa4),
      r('seninf', MUX + 0x18), r('seninf', MUX + 0xb0), r('seninf', MUX + 0xb8)))
print('CAM_MUX2 CHK_RES %08x' % r('seninf', 0x400 + 0x128))
print('CAMSV0: INT %08x TG_FRMSIZE %08x' % (r('camsv', 0x51c), r('camsv', 0x268)))
d = open(D + '/frame', 'rb').read()
idx = [i for i, b in enumerate(d[:BPL * L + 4096]) if b != 0xa5]
print('frame: %d bytes written (full %d)' % ((max(idx) + 1) if idx else 0, BPL * L))
open(A.out, 'wb').write(d[:BPL * L])
os.chmod(A.out, 0o644)
