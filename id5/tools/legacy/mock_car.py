#!/usr/bin/env python3
"""Mock BMW HSFZ endpoint (fake HU) on TCP 6801. v2
Header observed on the wire: 00 00 <len16> 00 <ctrl> <src> <tgt> <uds>
(there is an extra 00 pad byte between length and ctrl on this endpoint).
Replies positive to all safe UDS services so the server reveals its full
sequence; logs EVERYTHING for offline review."""
import socket, threading, time

HOST = "0.0.0.0"
PORT = 6801
LOG = "/work/car-traffic.log"
VIN = b"WBADT43452G123456"

def log(msg):
    line = f"[{time.strftime('%H:%M:%S')}.{int(time.time()*1000)%1000:03d}] {msg}"
    print(line, flush=True)
    with open(LOG, "a") as f:
        f.write(line + "\n")

def hsfz(ctrl, src, tgt, payload):
    # endpoint format: 00 00 <len> 00 <ctrl> <src> <tgt> <uds>
    # len counts ctrl+src+tgt+uds, NOT the pad byte
    data = bytes([ctrl, src, tgt]) + payload
    return b"\x00\x00" + len(data).to_bytes(2, "big") + b"\x00" + data

def uds_reply(req):
    if not req:
        return None
    sid = req[0]
    if sid == 0x3E:
        return bytes([0x7E, 0x00])
    if sid == 0x10:
        return bytes([0x50]) + req[1:2]
    if sid == 0x11:
        return bytes([0x51]) + req[1:2]
    if sid == 0x27:
        sub = req[1] if len(req) > 1 else 0
        if sub % 2 == 1:
            return bytes([0x67, sub]) + b"\x11\x22\x33\x44\x55\x66"
        return bytes([0x67, sub])
    if sid == 0x22:
        did = req[1:3]
        if did == b"\xf1\x90":
            return bytes([0x62]) + did + VIN
        return bytes([0x62]) + did + b"\x00" * 8
    if sid == 0x2E:
        log(f"!!! 0x2E WriteDataByIdentifier to DID {req[1:3].hex()}")
        return bytes([0x6E]) + req[1:3]
    if sid == 0x31:
        rid = req[2:4] if len(req) > 3 else b""
        log(f"    RoutineControl: op=0x{req[1]:02x} RID={rid.hex()} params={req[4:].hex()}")
        return bytes([0x71, req[1]]) + rid + req[4:] + b"\x00" * 4  # 12B response
    if sid in (0x34, 0x35, 0x36, 0x37):
        log(f"!!! FLASH-CLASS SID 0x{sid:02x} attempted!")
        return bytes([0x7F, sid, 0x11])
    if sid in (0x85, 0x28):
        return bytes([sid + 0x40])
    return bytes([sid + 0x40]) + req[1:4]

def handle(conn, addr):
    log(f"car: connection from {addr}")
    buf = b""
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            buf += data
            while len(buf) >= 5:
                if buf[0:2] != b"\x00\x00":
                    log(f"car: bad magic {buf[:16].hex()}")
                    idx = buf.find(b"\x00\x00", 1)
                    buf = buf[idx:] if idx >= 0 else b""
                    continue
                plen = int.from_bytes(buf[2:4], "big")
                # client writes one relay payload per sendto -> consume whole recv chunk
                frame = buf[:8 + plen] if len(buf) >= 8 + plen else buf
                buf = buf[len(frame):]
                # layout: 00 00 LL LL 00 CC SS TT <uds to end of frame>
                ctrl, src, tgt = frame[5], frame[6], frame[7]
                uds = frame[8:]
                log(f"car RX ctrl=0x{ctrl:02x} src=0x{src:02x} tgt=0x{tgt:02x} uds={uds.hex()}")
                if ctrl in (0x00, 0x01):
                    r = uds_reply(uds)
                    if r is not None:
                        time.sleep(0.1)
                        # verbatim echo ack + UDS resp + trailer byte (client needs lookahead)
                        ack = frame[:len(frame)]
                        resp = hsfz(0x01, tgt, src, r)
                        log(f"car TX ack  {ack.hex()}")
                        log(f"car TX resp {resp.hex()}")
                        conn.sendall(ack + resp + b"\x00")
                        reply = None
                elif ctrl == 0x10:
                    reply = hsfz(0x11, tgt, src, b"")
                if reply:
                    time.sleep(0.15)
                    log(f"car TX {reply.hex()}")
                    conn.sendall(reply)
    except Exception as e:
        log(f"car: conn error {e}")
    finally:
        conn.close()
        log(f"car: connection closed {addr}")

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen(5)
    log(f"mock car listening on {HOST}:{PORT}")
    while True:
        c, a = s.accept()
        threading.Thread(target=handle, args=(c, a), daemon=True).start()

if __name__ == "__main__":
    main()
