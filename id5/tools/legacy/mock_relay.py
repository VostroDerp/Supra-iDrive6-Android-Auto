#!/usr/bin/env python3
"""Mock relay server - impersonates wss://pretzel.bavarian-aa.uk:8443 (plain ws).
Logs every message the client sends; echoes nothing back yet - we first want
to learn the protocol shape."""
import asyncio, time, sys

LOG = "/work/relay-traffic.log"

def log(msg):
    line = f"[{time.strftime('%H:%M:%S')}.{int(time.time()*1000)%1000:03d}] {msg}"
    print(line, flush=True)
    with open(LOG, "a") as f:
        f.write(line + "\n")

async def handle(ws):
    peer = ws.remote_address
    log(f"relay: connection from {peer} path={getattr(ws, 'path', '?')}")
    try:
        async for msg in ws:
            if isinstance(msg, bytes):
                log(f"relay RX bin ({len(msg)}B): {msg[:200].hex()}")
                try:
                    txt = msg.decode("utf-8")
                    log(f"relay RX as text: {txt[:300]}")
                except Exception:
                    pass
            else:
                log(f"relay RX txt: {msg[:300]}")
    except Exception as e:
        log(f"relay: error {e}")
    finally:
        log(f"relay: closed {peer}")

async def main():
    import websockets
    log("mock relay listening on 127.0.0.1:8443")
    async with websockets.serve(handle, "127.0.0.1", 8443):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
