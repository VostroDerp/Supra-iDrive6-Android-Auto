#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#
# svk_read.py - read the head unit's SVK (the installed software list) over HSFZ, read-only, from the laptop.
# Purpose: find the exact installed BTLD (part number 00001FFD) and SWFL (0000271A) versions so the matching
# PSdZData container can be chosen. Prints e.g.  BTLD-00001FFD-007.051.002 / SWFL-0000271A-007.051.002 .
#
# SAFETY. This talks to the car, but it is strictly read-only by construction:
#   - It only ever sends UDS ReadDataByIdentifier (0x22) and TesterPresent (0x3E). With --session it also sends
#     DiagnosticSessionControl (0x10), which is non-persistent and standard (ISTA does it constantly).
#   - A hardcoded allowlist (ALLOWED_SIDS) refuses to put any other service on the wire. No write (0x2E), no
#     security access (0x27), no routine (0x31), no flash (0x34-0x37) can be sent, even by mistake.
#   - Reading an unknown DID just returns a negative response (0x7F); it cannot change ECU state.
# It needs only ENET to the car (the diag port 6801). It does NOT need the SSH debug-gate unlock or any internet.
#
# The SVK read DID is not confirmed for NBT yet (the jacinto diag binary, pulled in the same session, will give it).
# So by default this SCANS a short list of BMW identification DIDs (all read-only 0x22) and, in every response,
# locates the 4-byte part numbers and reads the 3 version bytes after them. Whichever DID carries the SVK, the
# part-number locate finds it. Once the real DID is known, pass it with --did to skip the scan.
#
#   svk_read.py                         scan the default DIDs on 169.254.199.99, print BTLD/SWFL versions
#   svk_read.py --host 169.254.199.99   the jacinto diagnosis node (default)
#   svk_read.py --did F101              read one DID only
#   svk_read.py --ids 00001FFD,0000271A,00001F40   which part numbers to report (default BTLD+SWFL)
#   svk_read.py --raw                   also dump each response as hex, for offline parsing
#   svk_read.py --session               send a (non-persistent) extended-session 10 03 first, if default-session
#                                       reads are refused
# Host-tested in test_svk_read.py (an in-process mock HU that speaks the real framing). Not car-verified.

import argparse
import socket
import struct
import sys
import time

HSFZ_PORT = 6801
TESTER, HU = 0xF4, 0x63

# The only UDS services this tool may ever send. Read-only identification reads plus liveness.
ALLOWED_SIDS = {0x22, 0x3E}
SESSION_SID = 0x10                       # added to the allowlist only when --session is given

# BMW manufacturer identification DIDs (ISO 0xF1xx vendor range). All read-only. The SVK lands in one of these on
# NBT. The part-number locate below does not depend on which - this list just decides what to ask for.
DEFAULT_DIDS = [0xF101, 0xF100, 0xF150, 0xF151, 0xF152, 0xF153, 0xF154, 0xF155, 0xF156, 0xF15A, 0xF15B,
                0xF191, 0xF193, 0xF194, 0xF195, 0xF1A0, 0xF1A1, 0xF1A2]

# Standard part numbers we want versions for.
DEFAULT_IDS = ["00001FFD", "0000271A"]   # BTLD, SWFL
ID_LABEL = {"00001FFD": "BTLD", "0000271A": "SWFL"}


def build_frame(uds, ctrl=0x01, src=TESTER, tgt=HU):
    """Real-HU HSFZ frame: len(u32be) | 00 | ctrl | src | tgt | uds, where len = len(src+tgt+uds).
    Verified against the captured FD2E request (0000000a 00 01 f4 63 3101fd2effffffff)."""
    body = bytes([src, tgt]) + uds
    return struct.pack(">I", len(body)) + bytes([0x00, ctrl]) + body


def parse_frames(buf):
    """Split a byte stream into (ctrl, src, tgt, uds) frames. Returns (frames, remainder)."""
    frames = []
    while len(buf) >= 6:
        length = struct.unpack(">I", buf[0:4])[0]
        total = 6 + length                      # 4 len + 1 pad + 1 ctrl + (length = src+tgt+uds)
        if total < 8 or len(buf) < total:
            break
        ctrl = buf[5]
        src, tgt = buf[6], buf[7]
        uds = buf[8:total]
        frames.append((ctrl, src, tgt, uds))
        buf = buf[total:]
    return frames, buf


def guard(uds, allow_session):
    """Refuse to send anything but the allowlisted read-only services."""
    if not uds:
        raise ValueError("empty uds")
    sid = uds[0]
    allowed = set(ALLOWED_SIDS) | ({SESSION_SID} if allow_session else set())
    if sid not in allowed:
        raise ValueError("blocked SID 0x%02x - this tool is read-only (allowed: %s)"
                         % (sid, " ".join("0x%02x" % s for s in sorted(allowed))))
    return uds


def send_uds(sock, uds, allow_session=False):
    sock.sendall(build_frame(guard(uds, allow_session)))


def collect(sock, seconds=3.0):
    """Gather frames for a short window (the gateway sends an echo ack then the data response)."""
    buf = b""
    frames = []
    end = time.monotonic() + seconds
    sock.settimeout(0.3)
    while time.monotonic() < end:
        try:
            d = sock.recv(65536)
        except socket.timeout:
            if frames:
                break
            continue
        if not d:
            break
        buf += d
        got, buf = parse_frames(buf)
        frames.extend(got)
    return frames


def rdbi_response(frames, did):
    """Return the data bytes of a positive RDBI (0x62 <did>) response, or ('neg', nrc), or None."""
    want = bytes([0x62, (did >> 8) & 0xFF, did & 0xFF])
    for ctrl, src, tgt, uds in frames:
        if uds[:3] == want:
            return uds[3:]
        if uds[:1] == b"\x7f" and len(uds) >= 3 and uds[1] == 0x22:
            return ("neg", uds[2])
    return None


def find_versions(data, ids):
    """Locate each 4-byte part number in the SVK block and read the 3 version bytes after it.
    Returns {id_hex: (version_str, context_hex)}; an id not found is absent."""
    found = {}
    for id_hex in ids:
        marker = bytes.fromhex(id_hex)
        i = data.find(marker)
        while i >= 0:
            after = data[i + len(marker): i + len(marker) + 3]
            if len(after) == 3:
                ver = "%03d.%03d.%03d" % (after[0], after[1], after[2])
                ctx = data[max(0, i - 2): i + len(marker) + 4].hex()
                found[id_hex] = (ver, ctx)
                break                            # first occurrence with 3 trailing bytes
            i = data.find(marker, i + 1)
    return found


def nrc_name(nrc):
    return {0x10: "generalReject", 0x11: "serviceNotSupported", 0x13: "wrongLength",
            0x22: "conditionsNotCorrect", 0x31: "requestOutOfRange", 0x33: "securityAccessDenied",
            0x7E: "serviceNotSupportedInActiveSession", 0x7F: "serviceNotSupportedInActiveSession"}.get(nrc,
            "0x%02x" % nrc)


def main():
    ap = argparse.ArgumentParser(description="read the HU SVK over HSFZ (read-only)")
    ap.add_argument("--host", default="169.254.199.99", help="diagnosis node (default: jacinto 169.254.199.99)")
    ap.add_argument("--port", type=int, default=HSFZ_PORT)
    ap.add_argument("--did", help="read one DID only (hex, e.g. F101) instead of scanning")
    ap.add_argument("--ids", default=",".join(DEFAULT_IDS), help="part numbers to report (hex, comma-separated)")
    ap.add_argument("--raw", action="store_true", help="dump each response as hex")
    ap.add_argument("--session", action="store_true", help="send a non-persistent 10 03 first")
    ap.add_argument("--timeout", type=float, default=3.0)
    a = ap.parse_args()

    ids = [x.strip().upper() for x in a.ids.split(",") if x.strip()]
    dids = [int(a.did, 16)] if a.did else DEFAULT_DIDS

    try:
        sock = socket.create_connection((a.host, a.port), timeout=8)
    except OSError as e:
        print("cannot reach %s:%d: %s" % (a.host, a.port, e), file=sys.stderr)
        return 1
    print("connected %s:%d (read-only: sends only %s%s)" %
          (a.host, a.port, " ".join("0x%02x" % s for s in sorted(ALLOWED_SIDS)),
           " 0x10" if a.session else ""))

    results = {}
    try:
        if a.session:
            send_uds(sock, bytes([0x10, 0x03]), allow_session=True)
            collect(sock, 1.5)
        for did in dids:
            send_uds(sock, bytes([0x22, (did >> 8) & 0xFF, did & 0xFF]))
            resp = rdbi_response(collect(sock, a.timeout), did)
            if resp is None:
                if a.did:
                    print("DID %04X: no response" % did)
                continue
            if isinstance(resp, tuple):          # negative
                print("DID %04X: negative (%s)" % (did, nrc_name(resp[1])))
                continue
            if a.raw:
                print("DID %04X: %d bytes: %s" % (did, len(resp), resp.hex()))
            hits = find_versions(resp, ids)
            for id_hex, (ver, ctx) in hits.items():
                if id_hex not in results:
                    results[id_hex] = (did, ver, ctx)
    finally:
        sock.close()

    print("---")
    if not results:
        print("no SVK entries found. Try --raw to inspect, --session if reads were refused, or --did <the real one>")
        print("(the real SVK DID comes from the jacinto diag binary - F9 step 2).")
        return 2
    for id_hex in ids:
        if id_hex in results:
            did, ver, ctx = results[id_hex]
            print("%s-%s-%s   (DID %04X, context %s)" % (ID_LABEL.get(id_hex, "PN"), id_hex, ver, did, ctx))
        else:
            print("%s-%s   NOT FOUND" % (ID_LABEL.get(id_hex, "PN"), id_hex))
    return 0


if __name__ == "__main__":
    sys.exit(main())
