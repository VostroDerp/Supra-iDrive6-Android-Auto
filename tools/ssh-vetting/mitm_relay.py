#!/usr/bin/env python3
"""MITM relay: client -> ws://127.0.0.1:8443 -> wss://pretzel.bavarian-aa.uk:8443
Logs every frame both directions with timestamps."""
import asyncio, time, ssl, sys

LOG = "/work/mitm.log"
UPSTREAM = "wss://pretzel.bavarian-aa.uk:8443"
LOCAL = ("0.0.0.0", 8443)

def log(direction, msg):
    line = f"[{time.strftime('%H:%M:%S')}.{int(time.time()*1000)%1000:03d}] {direction} {msg}"
    print(line, flush=True)
    with open(LOG, "a") as f:
        f.write(line + "\n")

async def pump(reader, writer, tag):
    try:
        async for m in reader:
            if isinstance(m, bytes):
                log(tag, f"bin({len(m)}): {m[:400].hex()}")
            else:
                log(tag, f"txt: {m[:400]}")
            await writer.send(m)
    except Exception as e:
        log(tag, f"END: {e}")

async def handle(client_ws):
    import websockets
    peer = client_ws.remote_address
    log("client", f"connected {peer} -> upstream {UPSTREAM}")
    ctx = ssl.create_default_context()
    try:
        async with websockets.connect(UPSTREAM, ssl=ctx) as up:
            log("server", "upstream connected")
            c2s = asyncio.create_task(pump(client_ws, up, "C>S"))
            s2c = asyncio.create_task(pump(up, client_ws, "S>C"))
            done, pending = await asyncio.wait([c2s, s2c], return_when=asyncio.FIRST_COMPLETED)
            for t in pending:
                t.cancel()
    except Exception as e:
        log("server", f"upstream failed: {e}")

async def main():
    import websockets
    log("mitm", f"listening on {LOCAL[0]}:{LOCAL[1]}")
    async with websockets.serve(handle, *LOCAL):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
