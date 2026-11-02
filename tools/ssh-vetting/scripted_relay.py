#!/usr/bin/env python3
"""Scripted relay for offline testing of nbt-ssh-unlock.py.

Replays the server flow captured via MITM (see README.md):
  banner -> "Opening the debug gate..." -> FD2E -> (challenge resp) ->
  FD2F signature -> "Gate: open" / "Enabling SSH..." -> FDAC ->
  "SSH: enabled" -> 0x83 done.

--evil N  injects frame N (an out-of-allowlist car-bound message) instead of
          the legit FDAC step, to verify the client refuses and aborts.
"""
import asyncio
import struct
import sys
import time

FD2E = bytes.fromhex("0000000a0001f4633101fd2effffffff")
FD2F = bytes.fromhex(
    "0000010a0001f4633101fd2f00000040b04e6b6e2c68c8305350eeab08aed612d07c09f2917790500583b4e4"
    "f821c874e81aed75aa6d84ad21a835100141124a0181b3e39cc31a4b41df6379b3711ad15bd9bf7652144c6d"
    "5ed67511422a94337c9e391bc194bc7653291d403f5cc051e3fcc96bdc552ecfd916b5f0e9e8c70c78fcce"
    "df0593b80a79b42ca9f429fde1b3a31c9075691f84b1b0e78e57a1e24faef01713907c0710f6ca46db6b56"
    "767798444ad59016f424daaeb540a32df02c37063411517d5e22a6aa09f438f499e1ea3d2a7ac012fbd98d"
    "070646ab30cdf718510264a1f249f981000bcc60c3adbd0da6fbb0138b1c6f8e326d6e8a657d4b2fdd70c"
    "a902b62031dd9efbaa9c21753")
FDAC = bytes.fromhex("000000060001f4633101fdac")
# evil injections: 0x36 TransferData (flash-class) and RoutineControl with a
# different RID — both must be refused by the client allowlist
EVIL = {
    1: bytes.fromhex("000000090001f4633600aabbccddee"),
    2: bytes.fromhex("000000060001f4633101dead"),
    3: bytes.fromhex("000000060001f4632ef19000"),  # WriteDataByIdentifier VIN
}


def status(sev, text):
    p = text.encode()
    return bytes([0x82, sev]) + struct.pack(">I", len(p)) + p


def frame(p):
    return bytes([0x81]) + struct.pack(">I", len(p)) + p


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] scripted-relay: {msg}", flush=True)


async def handle(ws, evil):
    log(f"client connected {ws.remote_address}")
    state = 0
    try:
        async for m in ws:
            if not isinstance(m, bytes) or not m:
                continue
            t = m[0]
            log(f"RX t=0x{t:02x} ({len(m)}B) {m[:24].hex()}{'...' if len(m) > 24 else ''}")
            if t == 0x02:
                await ws.send(status(4, "This is free software! ..."))
                await ws.send(status(2, "Opening the debug gate..."))
                await ws.send(frame(FD2E))
                state = 1
            elif t == 0x03:
                if state == 1 and b"\x71\x01\xfd\x2e" in m:
                    await ws.send(frame(FD2F))
                    state = 2
                elif state == 2 and b"\x71\x01\xfd\x2f" in m:
                    await ws.send(status(3, "Challenge: ff ff ff ff ff ff 00 00 00 00"))
                    await ws.send(status(3, "Gate: open"))
                    await ws.send(status(2, "Enabling SSH..."))
                    if evil:
                        await ws.send(frame(EVIL[evil]))
                        log(f"INJECTED EVIL FRAME {evil}")
                    else:
                        await ws.send(frame(FDAC))
                    state = 3
                elif state == 3 and b"\x71\x01\xfd\xac" in m:
                    await ws.send(status(3, "SSH: enabled"))
                    await ws.send(b"\x83\x01")
                    log("done, closing")
                    break
    except Exception as e:
        log(f"error: {e}")
    finally:
        log("closed")


async def main(evil):
    import websockets
    log(f"listening on 127.0.0.1:8443 (evil={evil})")
    async with websockets.serve(lambda ws: handle(ws, evil), "127.0.0.1", 8443):
        await asyncio.Future()


if __name__ == "__main__":
    evil = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    asyncio.run(main(evil))
