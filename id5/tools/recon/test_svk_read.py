#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#
# Host test for svk_read.py. Never touches the car: an in-process mock HU on 127.0.0.1 speaks the REAL HSFZ framing
# (verified against the captured FD2E frame) and returns a sample SVK block. Checks frame build/parse, the
# part-number locate + version decode, the read-only allowlist, a negative response, and the end-to-end read.
import importlib.util
import os
import socket
import struct
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("svk_read", os.path.join(HERE, "svk_read.py"))
svk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(svk)

fails = 0


def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        fails += 1


# --- unit: frame build matches the real captured FD2E framing ---
fd2e = svk.build_frame(bytes.fromhex("3101fd2effffffff"))
check("build_frame matches captured FD2E", fd2e.hex() == "0000000a0001f4633101fd2effffffff")

# --- unit: parse round-trip ---
frames, rest = svk.parse_frames(fd2e)
check("parse_frames one frame", len(frames) == 1 and rest == b"")
ctrl, src, tgt, uds = frames[0]
check("parse_frames fields", ctrl == 0x01 and src == 0xF4 and tgt == 0x63 and uds.hex() == "3101fd2effffffff")
# split across two recv chunks
frames, rest = svk.parse_frames(fd2e[:5])
check("parse_frames partial waits", frames == [] and rest == fd2e[:5])

# --- unit: version locate + decode (07 33 02 -> 007.051.002) ---
svk_block = bytes.fromhex(
    "01" + "00001ffd" + "073302" +      # BTLD-00001FFD-007.051.002
    "01" + "0000271a" + "073302" +      # SWFL-0000271A-007.051.002
    "01" + "0000dead" + "010203")       # a decoy entry
got = svk.find_versions(svk_block, ["00001FFD", "0000271A"])
check("BTLD version decoded", got.get("00001FFD", ("",))[0] == "007.051.002")
check("SWFL version decoded", got.get("0000271A", ("",))[0] == "007.051.002")
check("missing id absent", "0000BEEF" not in svk.find_versions(svk_block, ["0000BEEF"]))

# --- unit: the read-only allowlist ---
check("0x22 allowed", svk.guard(bytes([0x22, 0xF1, 0x01]), False) is not None)
check("0x3E allowed", svk.guard(bytes([0x3E, 0x00]), False) is not None)
for bad in (0x2E, 0x27, 0x31, 0x34, 0x36, 0x10):
    try:
        svk.guard(bytes([bad, 0x00]), False)
        check("SID 0x%02x blocked" % bad, False)
    except ValueError:
        check("SID 0x%02x blocked" % bad, True)
# 0x10 only with --session
try:
    svk.guard(bytes([0x10, 0x03]), True); check("0x10 allowed with session", True)
except ValueError:
    check("0x10 allowed with session", False)


# --- integration: a mock HU speaking the real framing ---
SVK_DID = 0xF101


def mock_hu(sock, ready):
    srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0)); srv.listen(1)
    ready["port"] = srv.getsockname()[1]; ready["up"] = True
    srv.settimeout(5)
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        return
    buf = b""
    conn.settimeout(3)
    end = time.time() + 4
    while time.time() < end:
        try:
            d = conn.recv(4096)
        except socket.timeout:
            break
        if not d:
            break
        buf += d
        got, buf = svk.parse_frames(buf)
        for ctrl, src, tgt, uds in got:
            sid = uds[0]
            if sid == 0x3E:
                resp = bytes([0x7E, 0x00])
            elif sid == 0x10:
                resp = bytes([0x50]) + uds[1:2]
            elif sid == 0x22 and uds[1:3] == bytes([(SVK_DID >> 8) & 0xFF, SVK_DID & 0xFF]):
                resp = bytes([0x62]) + uds[1:3] + svk_block
            elif sid == 0x22:
                resp = bytes([0x7F, 0x22, 0x31])      # requestOutOfRange
            else:
                resp = bytes([0x7F, sid, 0x11])
            # gateway echo ack, then the swapped-address data response
            conn.sendall(svk.build_frame(uds, ctrl=ctrl, src=src, tgt=tgt))
            conn.sendall(svk.build_frame(resp, ctrl=0x01, src=tgt, tgt=src))
    conn.close()


ready = {"up": False}
t = threading.Thread(target=mock_hu, args=(None, ready), daemon=True)
t.start()
for _ in range(100):
    if ready["up"]:
        break
    time.sleep(0.02)
check("mock HU up", ready["up"])

sock = socket.create_connection(("127.0.0.1", ready["port"]), timeout=3)
# default session read of the known DID
svk.send_uds(sock, bytes([0x22, (SVK_DID >> 8) & 0xFF, SVK_DID & 0xFF]))
resp = svk.rdbi_response(svk.collect(sock, 2.0), SVK_DID)
check("end-to-end: positive RDBI", isinstance(resp, bytes))
hits = svk.find_versions(resp, ["00001FFD", "0000271A"]) if isinstance(resp, bytes) else {}
check("end-to-end: BTLD 007.051.002", hits.get("00001FFD", ("",))[0] == "007.051.002")
check("end-to-end: SWFL 007.051.002", hits.get("0000271A", ("",))[0] == "007.051.002")
# an unknown DID returns a clean negative, not a crash
svk.send_uds(sock, bytes([0x22, 0xF1, 0x99]))
neg = svk.rdbi_response(svk.collect(sock, 2.0), 0xF199)
check("end-to-end: negative is reported", isinstance(neg, tuple) and neg[0] == "neg" and neg[1] == 0x31)
sock.close()

print("test_svk_read: %d failure(s)" % fails if fails else "test_svk_read OK")
raise SystemExit(1 if fails else 0)
