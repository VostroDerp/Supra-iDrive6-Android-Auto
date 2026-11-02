#!/usr/bin/env python3
"""knock — laptop-side client for the HU knockd SSH gate.

Connects to the HU's knock port, reads the 32-byte challenge nonce, signs
SHA256(nonce) with the RSA private key (via `openssl dgst -sign`), sends the
256-byte signature back. On 'K' the daemon has spawned SSH — connect as usual.

Usage:
  knock.py <hu-ip> [port]          # port default 46610
  KNOCK_KEY=~/path/key.pem knock.py <hu-ip>   # override key path (default ~/.ssh/nbt_knock.pem)

The private key is only ever READ by openssl — it never prints, never leaves
the laptop, and only the signature crosses the wire.
"""
import os
import socket
import subprocess
import sys

PORT = 851
KEY = os.environ.get("KNOCK_KEY", os.path.expanduser("~/.ssh/nbt_knock.pem"))


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: knock.py <hu-ip> [port]")
    ip, port = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else PORT
    if not os.path.exists(KEY):
        sys.exit(f"no private key at {KEY} — run genkey.sh first")

    try:
        s = socket.create_connection((ip, port), timeout=8)
    except OSError as e:
        sys.exit(f"cannot reach knockd at {ip}:{port}: {e} "
                 "(daemon not running? port filtered?)")
    s.settimeout(8)

    ver = s.recv(1)
    if not ver:
        sys.exit("daemon closed before challenge")
    nonce = b""
    while len(nonce) < 32:
        chunk = s.recv(32 - len(nonce))
        if not chunk:
            sys.exit("daemon closed mid-nonce")
        nonce += chunk
    print(f"challenge: {nonce.hex()[:24]}… (proto {ver[0]})")

    sig = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", KEY],
        input=nonce, capture_output=True)
    if sig.returncode != 0:
        sys.exit(f"signing failed: {sig.stderr.decode().strip()}")
    if len(sig.stdout) != 256:
        sys.exit(f"unexpected signature length {len(sig.stdout)} (want 256 — RSA-2048 key?)")
    s.sendall(sig.stdout)

    resp = s.recv(16)
    s.close()
    if resp == b"K":
        print(f"KNOCK ACCEPTED — SSH armed at {ip}:3804 (until HU reboot)")
    elif resp == b"N":
        sys.exit("knock REJECTED — signature didn't verify "
                 "(wrong key? pubkey on HU stale?)")
    else:
        sys.exit(f"unexpected daemon reply: {resp!r}")


if __name__ == "__main__":
    main()
