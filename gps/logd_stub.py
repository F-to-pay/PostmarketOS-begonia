#!/usr/bin/env python3
"""A stub for Android logd: listens on /dev/socket/logdw (SOCK_DGRAM), prints the log lines of the vendor daemons (mnld).

Packet format of libcutils/liblog (logger_write.cpp -> logdw): android_log_header_t { u8 id; u16 tid; u32 tv_sec; u32 tv_nsec }
(11 bytes, packed), then the payload: u8 priority, tag\0, message\0. For binary buffers (events) hex is printed.
usage: logd_stub.py [log_file]  (default /tmp/logd.log; Ctrl-C/SIGTERM - stop)
"""
import os
import signal
import socket
import struct
import sys
import time

PATH = "/dev/socket/logdw"
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/logd.log"
PRIO = "?? VDIWEFS"
os.makedirs("/dev/socket", exist_ok=True)
try:
    os.unlink(PATH)
except FileNotFoundError:
    pass
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.bind(PATH)
os.chmod(PATH, 0o666)
f = open(OUT, "a", buffering=1)
signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))
print("logd_stub: listening on %s -> %s" % (PATH, OUT), flush=True)
try:
    while True:
        d = s.recv(65536)
        if len(d) < 12:
            continue
        lid, tid, sec, nsec = struct.unpack_from("<BHII", d, 0)
        body = d[11:]
        prio = body[0] if body else 0
        rest = body[1:]
        tag, _, msg = rest.partition(b"\0")
        msg = msg.split(b"\0")[0]
        txt = msg.decode("utf-8", "replace").rstrip()
        f.write("%s %c/%s(%d): %s\n" % (time.strftime("%H:%M:%S"), PRIO[prio] if prio < len(PRIO) else "?", tag.decode("utf-8", "replace"), tid, txt))
finally:
    try:
        os.unlink(PATH)
    except OSError:
        pass
