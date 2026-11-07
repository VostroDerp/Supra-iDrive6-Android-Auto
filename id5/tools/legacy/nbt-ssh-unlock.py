#!/usr/bin/env python3
"""nbt-ssh-unlock — auditable open client for the bavarian-aa NBT Evo SSH unlock.

Replicates bavarian-ssh-client v0.1.0's wire behaviour, with two deliberate
differences:

  1. No GUI and zero dependencies — stdlib only, runs anywhere python3 exists.
  2. Hardcoded car-facing allowlist: the ONLY bytes this tool will ever send
     to the car are UDS RoutineControl(0x31, startRoutine) frames targeting
     diag address 0x63 (HU_NBT2) with routine IDs 0xFD2E / 0xFD2F / 0xFDAC —
     the three steps of the observed debug-gate unlock. Anything else the
     server sends is refused before it reaches the car.

Protocol documentation: id5/tools/README.md

Usage:
  nbt-ssh-unlock.py <car-ip>                      # real unlock via pretzel relay
  nbt-ssh-unlock.py <car-ip> --probe              # fetch the FD2E challenge twice,
                                                  # print both, exit (no server)
  nbt-ssh-unlock.py <car-ip> --replay FILE        # offline re-unlock using a session
                                                  # saved by --save (works only if the
                                                  # car's challenge is unchanged)
  nbt-ssh-unlock.py <car-ip> --server ws://... [--insecure]

Every successful server unlock auto-saves the session to ./nbt-unlock-*.json so
it can be attempted with --replay later. Replay verifies the fresh challenge
matches the recorded one and aborts if not — it never sends anything the
allowlist wouldn't pass anyway.
"""

import argparse
import base64
import json
import os
import select
import socket
import ssl
import struct
import sys
import time

PROD_SERVER = "wss://pretzel.bavarian-aa.uk:8443"
CLIENT_VERSION = b"0.1.0"   # mimic upstream; the server may gate on it
JOB = b"ssh_enable"
HSFZ_PORT = 6801
TESTER, HU = 0xF4, 0x63
ALLOWED_RIDS = (b"\xfd\x2e", b"\xfd\x2f", b"\xfd\xac")
rid_name = {b"\xfd\x2e": "fd2e", b"\xfd\x2f": "fd2f", b"\xfd\xac": "fdac"}

# observed verbatim from the real server (hsfz: 00 00 <len> 00 01 f4 63 <uds>,
# len = src+tgt+uds)
FD2E_REQUEST = bytes.fromhex("0000000a0001f4633101fd2effffffff")


def log(tag, msg):
    ts = time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}"
    print(f"[{ts}] {tag}: {msg}", file=sys.stderr, flush=True)


def die(msg, code=1):
    log("error", msg)
    sys.exit(code)


# ---------------------------------------------------------------- WebSocket

def parse_url(url):
    scheme, _, rest = url.partition("://")
    if scheme not in ("ws", "wss"):
        die(f"bad --server scheme {scheme!r} (need ws:// or wss://)")
    hostport, _, path = rest.partition("/")
    path = "/" + path if path else "/"
    if ":" in hostport:
        host, p = hostport.rsplit(":", 1)
        port = int(p)
    else:
        host, port = hostport, 443 if scheme == "wss" else 80
    return scheme, host, port, path


class WS:
    """Minimal RFC6455 client: binary frames, masking, ping/pong, fragmentation."""

    def __init__(self, url, insecure=False, timeout=15):
        scheme, host, port, path = parse_url(url)
        self.host = f"{host}:{port}"
        raw = socket.create_connection((host, port), timeout=timeout)
        if scheme == "wss":
            ctx = (ssl._create_unverified_context() if insecure
                   else ssl.create_default_context())
            raw = ctx.wrap_socket(raw, server_hostname=host)
        self.sock = raw
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET {path} HTTP/1.1\r\nHost: {self.host}\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
        raw.sendall(req.encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = raw.recv(4096)
            if not chunk:
                die("relay closed during handshake")
            buf += chunk
        head, _, rest = buf.partition(b"\r\n\r\n")
        if b" 101" not in head.split(b"\r\n", 1)[0]:
            die(f"relay handshake rejected: {head.split(b' ')[1] if b' ' in head else head[:80]!r}")
        self.buf = bytearray(rest)
        self.frag = b""
        raw.settimeout(timeout)
        log("relay", f"connected {url} ({'INSECURE tls' if insecure else 'tls ok'})")

    def send(self, payload):
        n = len(payload)
        hdr = bytearray([0x82])                      # FIN + binary
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
        mask = os.urandom(4)
        hdr += mask
        hdr += bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(hdr)

    def _frame(self):
        """Parse one frame from self.buf; return (opcode, payload) or None."""
        b = self.buf
        if len(b) < 2:
            return None
        fin, op = b[0] & 0x80, b[0] & 0x0F
        masked, ln = b[1] & 0x80, b[1] & 0x7F
        off = 2
        if ln == 126:
            if len(b) < 4: return None
            ln, off = struct.unpack(">H", b[2:4])[0], 4
        elif ln == 127:
            if len(b) < 10: return None
            ln, off = struct.unpack(">Q", b[2:10])[0], 10
        if masked:
            if len(b) < off + 4: return None
            mask = bytes(b[off:off + 4]); off += 4
        else:
            mask = None
        if len(b) < off + ln:
            return None
        payload = bytes(b[off:off + ln])
        del b[:off + ln]
        if mask:
            payload = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
        if not fin:                                # continuation handling
            self.frag += payload
            return ("frag", None)
        if op == 0x0:
            payload, op = self.frag + payload, 0x2
            self.frag = b""
        elif self.frag:
            payload, op = self.frag + payload, op
            self.frag = b""
        return (op, payload)

    def recv(self):
        """Return next binary/text payload, or None on close."""
        while True:
            fr = self._frame()
            if fr is None:
                chunk = self.sock.recv(65536)
                if not chunk:
                    return None
                self.buf += chunk
                continue
            op, payload = fr
            if op == "frag":
                continue
            if op == 0x8:                          # close
                self.sock.sendall(b"\x88\x80" + os.urandom(4))
                return None
            if op == 0x9:                          # ping -> pong
                n = len(payload)
                mask = os.urandom(4)
                self.sock.sendall(b"\x8a" + bytes([0x80 | n]) + mask +
                                  bytes(c ^ mask[i % 4] for i, c in enumerate(payload)))
                continue
            if op == 0xA:                          # pong
                continue
            return payload


# ----------------------------------------------------------- relay messages

def msg(t, payload):
    return bytes([t]) + struct.pack(">I", len(payload)) + payload


def parse_relay(m):
    """Return (type, aux, payload)."""
    t = m[0]
    if t == 0x82:
        sev = m[1]
        ln = struct.unpack(">I", m[2:6])[0]
        return t, sev, m[6:6 + ln]
    if t == 0x83:
        return t, m[1] if len(m) > 1 else None, b""
    ln = struct.unpack(">I", m[1:5])[0]
    return t, None, m[5:5 + ln]


# --------------------------------------------------------------- allowlist

def check_car_frame(p):
    """Validate a server->car frame against the strict allowlist.

    Only RoutineControl(start) on 0x63 with RIDs FD2E/FD2F/FDAC passes.
    Returns the UDS payload. Raises ValueError otherwise."""
    if len(p) < 12 or p[0] != 0 or p[1] != 0:
        raise ValueError("bad hsfz magic/length")
    dlen = struct.unpack(">H", p[2:4])[0]
    if len(p) != dlen + 6:
        raise ValueError(f"hsfz length mismatch: declared {dlen}, frame {len(p)}")
    if p[4] != 0:
        raise ValueError("nonzero pad byte")
    ctrl, src, tgt = p[5], p[6], p[7]
    uds = p[8:]
    if src != TESTER or tgt != HU:
        raise ValueError(f"unexpected addresses 0x{src:02x}->0x{tgt:02x} "
                         f"(expected f4->63)")
    if ctrl != 0x01:
        raise ValueError(f"ctrl 0x{ctrl:02x} is not a data frame")
    if len(uds) < 4 or uds[0] != 0x31 or uds[1] != 0x01:
        raise ValueError(f"not RoutineControl/startRoutine: {uds[:4].hex()}")
    rid = uds[2:4]
    if rid not in ALLOWED_RIDS:
        raise ValueError(f"routine 0x{rid.hex()} NOT in allowlist "
                         "(fd2e/fd2f/fdac)")
    return uds


# ------------------------------------------------------------------- probe

def probe(car, rounds=2):
    """Send FD2E <rounds> times, print each 8-byte challenge."""
    challenges = []
    for i in range(rounds):
        car.sendall(FD2E_REQUEST)
        log("car", f"-> {FD2E_REQUEST.hex()}")
        buf = b""
        end = time.monotonic() + 3
        while time.monotonic() < end:
            r, _, _ = select.select([car], [], [], 0.4)
            if r:
                d = car.recv(4096)
                if not d:
                    die("car closed connection")
                buf += d
        log("car", f"<- {buf.hex() or '(no response)'}")
        i71 = buf.find(b"\x71\x01\xfd\x2e")
        ch = buf[i71 + 4:i71 + 12] if i71 >= 0 else None
        challenges.append(ch)
        print(f"challenge[{i}]: {ch.hex() if ch else 'not found in response'}")
        if i == 0:
            time.sleep(0.5)
    if all(challenges) and challenges[0] == challenges[1]:
        print("challenges IDENTICAL — captured signatures may replay")
    else:
        print("challenges differ — per-request random, no replay possible")


def extract_challenge(buf):
    """Find `71 01 FD 2E` + 8-byte challenge in a raw car byte stream."""
    i = buf.find(b"\x71\x01\xfd\x2e")
    return buf[i + 4:i + 12] if i >= 0 else None


def read_car(car, seconds=3.0):
    """Collect everything the car sends for `seconds`."""
    buf = b""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        r, _, _ = select.select([car], [], [], 0.3)
        if r:
            try:
                d = car.recv(65536)
            except BlockingIOError:
                continue
            if not d:
                die("car closed the hsfz connection")
            buf += d
    return buf


def replay(car, path):
    """Offline re-unlock from a saved session. Refuses (safely) if the
    car's fresh challenge differs from the recorded one."""
    rec = json.load(open(path))
    log("replay", f"session {path}: challenge={rec['challenge'][:16]}...")
    for step in ("fd2e", "fd2f", "fdac"):
        frame = bytes.fromhex(rec[step])
        check_car_frame(frame)                     # allowlist still applies
        log("car", f"-> {frame.hex()}")
        car.sendall(frame)
        buf = read_car(car, 5)
        log("car", f"<- {buf.hex()}")
        if step == "fd2e":
            ch = extract_challenge(buf)
            if ch != bytes.fromhex(rec["challenge"]):
                die(f"challenge changed ({(ch or b'?').hex()} vs recorded "
                    f"{rec['challenge']}) — replay impossible, needs the server")
            log("replay", "challenge matches recorded session — proceeding")
    print("replay complete — if the HU accepted it, SSH is enabled until reboot")


# -------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("car_ip")
    ap.add_argument("--server", default=PROD_SERVER)
    ap.add_argument("--insecure", action="store_true",
                    help="skip TLS verification (local test relays)")
    ap.add_argument("--probe", action="store_true",
                    help="fetch FD2E challenge twice and exit (no relay)")
    ap.add_argument("--replay", metavar="FILE",
                    help="offline re-unlock from a saved session JSON")
    ap.add_argument("--save", metavar="FILE",
                    help="where to save the unlock session (default: auto)")
    ap.add_argument("--timeout", type=float, default=90)
    a = ap.parse_args()

    print("car-facing allowlist: UDS RoutineControl(start) on 0x63, "
          "routines FD2E/FD2F/FDAC only — everything else is refused",
          file=sys.stderr)

    try:
        car = socket.create_connection((a.car_ip, HSFZ_PORT), timeout=8)
    except OSError as e:
        die(f"cannot reach car at {a.car_ip}:{HSFZ_PORT}: {e}")
    car.settimeout(8)
    log("car", f"connected {a.car_ip}:{HSFZ_PORT}")

    if a.probe:
        probe(car)
        return
    if a.replay:
        replay(car, a.replay)
        return

    ws = WS(a.server, insecure=a.insecure)
    # type 0x01 is a fixed 12-byte body, NO length prefix (targets: vin/mac/diag
    # — zero when the car IP was entered manually, matching upstream behaviour)
    for m in (msg(0x06, CLIENT_VERSION), bytes([0x01]) + b"\x00" * 12,
              msg(0x02, JOB)):
        ws.send(m)
    log("relay", "hello sent (version, targets, job=ssh_enable)")

    deadline = time.monotonic() + a.timeout
    ws.sock.setblocking(False)
    car.setblocking(False)
    cbuf = b""
    # Outbound request frames keyed by routine (fd2e/fd2f/fdac) plus the car's
    # own responses: "responses" keyed by routine (the positive 71 01 <rid>
    # replies the other agent asked for - FD2F accept, FDAC status), and
    # "car_frames" the full chronological list so nothing is lost.
    session = {"challenge": None, "fd2e": None, "fd2f": None, "fdac": None,
               "responses": {}, "car_frames": []}

    def _hsfz_frames(buf):
        """real-HU hsfz framing: len:u32be | 00 | ctrl | src | tgt | uds.
        len counts src+tgt+uds (i.e. total frame = 6 + len)."""
        out = []
        while len(buf) >= 6:
            total = 6 + int.from_bytes(buf[0:4], "big")
            if len(buf) < total:
                break
            out.append(buf[:total])
            buf = buf[total:]
        return out, buf

    def _relay_msg(m):
        t, aux, p = parse_relay(m)
        if t == 0x81:
            try:
                uds = check_car_frame(p)
            except ValueError as e:
                log("REFUSED", f"{e}: {p.hex()}")
                die("server sent a frame outside the allowlist — aborted")
            log("server->car", f"{p.hex()}  (routine 0x{uds[2:4].hex()})")
            session[rid_name[uds[2:4]]] = p.hex()
            car.sendall(p)
        elif t == 0x82:
            print(f"[server] {p.decode('utf-8', 'replace')}")
        elif t == 0x83:
            print(f"[server] job finished, status={aux}")
            if aux == 1 and session["challenge"]:
                path = a.save or f"nbt-unlock-{time.strftime('%Y%m%d-%H%M%S')}.json"
                json.dump(session, open(path, "w"), indent=1)
                print(f"[saved] session -> {path} "
                      "(usable with --replay if the challenge is static)")
            log("done", "unlock complete — ssh to the HU before it reboots")
            return False
        else:
            log("relay", f"unknown msg type 0x{t:02x} ({len(m)}B): {m[:64].hex()}")
        return True

    while True:
        if time.monotonic() > deadline:
            die(f"timeout after {a.timeout}s")
        r, _, _ = select.select([car, ws.sock], [], [], 1.0)
        for s in r:
            if s is car:
                try:
                    d = car.recv(65536)
                except BlockingIOError:
                    continue
                if not d:
                    die("car closed the hsfz connection")
                cbuf += d
                frames, cbuf = _hsfz_frames(cbuf)
                for f in frames:
                    log("car->server", f.hex())
                    session["car_frames"].append(f.hex())
                    # hsfz header is 8 bytes (len4|00|ctrl|src|tgt); uds follows.
                    uds = f[8:]
                    if len(uds) >= 4 and uds[0] == 0x71:   # positive RoutineControl reply
                        session["responses"][rid_name.get(uds[2:4], uds[2:4].hex())] = f.hex()
                    elif len(uds) >= 3 and uds[0] == 0x7f:  # negative reply, keep it too
                        session["responses"].setdefault("negatives", []).append(f.hex())
                    ch = extract_challenge(f)
                    if ch:
                        session["challenge"] = ch.hex()
                    ws.send(msg(0x03, f))
            else:
                # drain ALL buffered relay messages — the server bursts several
                # per TLS record; select won't refire for data already in ws.buf
                while True:
                    try:
                        m = ws.recv()
                    except (BlockingIOError, ssl.SSLWantReadError,
                            socket.timeout):
                        break
                    if m is None:
                        die("relay closed connection")
                    if not _relay_msg(m):
                        return


if __name__ == "__main__":
    main()
