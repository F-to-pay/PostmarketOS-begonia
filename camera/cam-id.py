#!/usr/bin/env python3
"""cam-id: reads the chip ID of the camera sensors over I2C (read only) while the slot is powered in cam-probe.

  sudo python3 cam-id.py <slot> [--scan]

Slot: 0 MAIN (i2c2), 1 SUB (i2c4), 2 MAIN2 (i2c7), 3 SUB2 (i2c4), 4 MAIN3 (i2c7).
Finds the bus by of_node (i2c@11009000 = i2c2, @11011000 = i2c4, @1101a000 = i2c7).
Without arguments it does not enable anything in debugfs - power: echo '<slot> on' > /sys/kernel/debug/begonia-cam/power
"""
import ctypes
import fcntl
import glob
import os
import sys

I2C_RDWR = 0x0707
I2C_M_RD = 0x0001
BUS_UNIT = {0: '11009000', 1: '11011000', 2: '1101a000', 3: '11011000', 4: '1101a000'}
SLOT_NAME = {0: 'MAIN', 1: 'SUB', 2: 'MAIN2', 3: 'SUB2', 4: 'MAIN3'}


class Msg(ctypes.Structure):
    _fields_ = [('addr', ctypes.c_uint16), ('flags', ctypes.c_uint16),
                ('len', ctypes.c_uint16), ('buf', ctypes.c_void_p)]


class Data(ctypes.Structure):
    _fields_ = [('msgs', ctypes.POINTER(Msg)), ('nmsgs', ctypes.c_uint32)]


def find_bus(unit):
    for p in glob.glob('/sys/class/i2c-dev/i2c-*'):
        try:
            if os.path.realpath(os.path.join(p, 'device', 'of_node')).endswith('i2c@' + unit):
                return int(p.rsplit('-', 1)[1])
        except OSError:
            pass
    return None


def xfer(fd, addr, wr, rdlen):
    """write wr, then read rdlen; returns bytes or None (no ACK)."""
    wb = ctypes.create_string_buffer(bytes(wr), len(wr))
    rb = ctypes.create_string_buffer(rdlen)
    msgs = (Msg * 2)(Msg(addr, 0, len(wr), ctypes.addressof(wb)),
                     Msg(addr, I2C_M_RD, rdlen, ctypes.addressof(rb)))
    d = Data(msgs, 2)
    try:
        fcntl.ioctl(fd, I2C_RDWR, d)
    except OSError:
        return None
    return rb.raw


def rd_id(fd, addr, reg, regw, n):
    r = [reg >> 8, reg & 0xff] if regw == 16 else [reg]
    b = xfer(fd, addr, r, n)
    return None if b is None else int.from_bytes(b, 'big')


# (name, slots, 7-bit addresses, register address width, ID register, number of bytes, {value: meaning})
CAND = [
    ('OV8856', (2, 4), (0x10,), 16, 0x300B, 2, {0x885A: 'OV8856'}),
    ('S5KGW1', (0,), (0x2d, 0x10), 16, 0x0000, 2, {0x0971: 'S5KGW1 ofilm', 0x0972: 'S5KGW1 sunny'}),
    ('S5K3T2/3T1', (1, 3), (0x56, 0x2d), 16, 0x0000, 2,
     {0x3142: 'S5K3T2 ofilm', 0x3143: 'S5K3T2 sunny', 0x3141: 'S5K3T1 ofilm', 0x3140: 'S5K3T1 sunny'}),
    ('GC5035', (2,), (0x37,), 8, 0xF0, 2, {0x5035: 'GC5035', 0x5036: 'GC5035 sunny'}),
    ('GC2375', (2, 3), (0x17, 0x3c, 0x37), 8, 0xF0, 2,
     {0x2375: 'GC2375 alpha', 0x2374: 'GC2375 beta', 0x2376: 'GC2375 AF'}),
    ('OV02A10', (2, 3), (0x3d,), 8, 0x02, 2,
     {0x2509: 'OV02A10 alpha', 0x2508: 'OV02A10 beta', 0x250A: 'OV02A10 AF'}),
]


def main():
    if len(sys.argv) < 2 or not sys.argv[1].isdigit() or int(sys.argv[1]) not in BUS_UNIT:
        print(__doc__)
        return 2
    slot = int(sys.argv[1])
    bus = find_bus(BUS_UNIT[slot])
    if bus is None:
        print('bus i2c@%s not found (camera DTB not installed/not loaded?)' % BUS_UNIT[slot])
        return 1
    print('slot %d (%s): bus /dev/i2c-%d (i2c@%s)' % (slot, SLOT_NAME[slot], bus, BUS_UNIT[slot]))
    fd = os.open('/dev/i2c-%d' % bus, os.O_RDWR)
    hits = 0
    for name, slots, addrs, regw, reg, n, ids in CAND:
        for a in addrs:
            v = rd_id(fd, a, reg, regw, n)
            tag = '' if slot in slots else '  (not from this slot per the config)'
            if v is None:
                continue
            hits += 1
            known = ids.get(v)
            print('  0x%02x %-10s reg 0x%04x -> 0x%0*x %s%s' % (a, name, reg, 2 * n, v,
                  ('= ' + known) if known else '(ID did not match)', tag))
    if '--scan' in sys.argv:
        print('  ACK scan 0x08..0x77 (1-byte read):')
        rbuf = ctypes.create_string_buffer(1)
        for a in range(0x08, 0x78):
            m = (Msg * 1)(Msg(a, I2C_M_RD, 1, ctypes.addressof(rbuf)))
            try:
                fcntl.ioctl(fd, I2C_RDWR, Data(m, 1))
                print('   ACK 0x%02x' % a)
                hits += 1
            except OSError:
                pass
    if not hits:
        print('  nobody answered (is the slot powered? sudo not forgotten?)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
