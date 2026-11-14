#!/usr/bin/env python3
"""Emulator of the Android GNSS HAL for the vendor mnld (MediaTek): AF_UNIX/SOCK_DGRAM sockets in the abstract namespace.

HAL -> mnld : abstract "mtk_hal2mnl", message = int32 LE 1, int32 LE command code (101 init, 102 start, 103 stop, 104 cleanup) [+ arguments]
mnld -> HAL : abstract "mtk_mnl2hal" (NMEA, status, SV, location arrive here)
usage: hal_emu.py [seconds]    (default 60)
"""
import os
import re
import select
import socket
import struct
import sys
import time

HAL2MNL = "mtk_hal2mnl"
MNL2HAL = "mtk_mnl2hal"
CMD_INIT, CMD_START, CMD_STOP, CMD_CLEANUP = 101, 102, 103, 104
DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
NMEA_LOG = "/tmp/nmea.log"


def abstract(name):
    """like the vendor safe_sendto/mnld: a full-length sockaddr_un (110 B), sun_path[0]=0, the name padded with zeros to 108 bytes"""
    return (b"\x00" + name.encode()).ljust(108, b"\x00")


rx = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
rx.bind(abstract(MNL2HAL))
tx = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)


def send(cmd, *ints):
    pkt = struct.pack("<ii", 1, cmd) + b"".join(struct.pack("<i", v) for v in ints)
    for i in range(50):
        try:
            tx.sendto(pkt, abstract(HAL2MNL))
            print("[emu] -> mnld cmd %d (%d B)" % (cmd, len(pkt)), flush=True)
            return True
        except OSError as e:
            if i == 0:
                print("[emu] waiting for the mnld socket (%s)..." % e, flush=True)
            time.sleep(0.2)
    print("[emu] failed to send cmd %d" % cmd, flush=True)
    return False


def dump(pkt):
    ints = struct.unpack_from("<%di" % min(len(pkt) // 4, 4), pkt)
    txt = [m.group().decode("ascii", "replace") for m in re.finditer(rb"[ -~]{6,}", pkt)]
    return "len=%d ints=%s text=%s" % (len(pkt), list(ints), txt[:4])


print("[emu] listening on @%s, %.0f s" % (MNL2HAL, DURATION), flush=True)
nm = open(NMEA_LOG, "a")
if not send(CMD_INIT):
    sys.exit(1)
time.sleep(1.0)
send(CMD_START)
t0 = time.time()
n = 0
try:
    while time.time() - t0 < DURATION:
        r, _, _ = select.select([rx], [], [], 1.0)
        if not r:
            continue
        pkt = rx.recv(65536)
        n += 1
        d = dump(pkt)
        print("[emu] <- #%d %s" % (n, d), flush=True)
        for m in re.finditer(rb"\$[A-Z]{2}[A-Z0-9]{3},[ -~]*", pkt):
            line = m.group().decode("ascii", "replace")
            print("[nmea] " + line, flush=True)
            nm.write(line + "\n")
            nm.flush()
finally:
    send(CMD_STOP)
    time.sleep(0.5)
    send(CMD_CLEANUP)
    print("[emu] done, messages received: %d" % n, flush=True)
