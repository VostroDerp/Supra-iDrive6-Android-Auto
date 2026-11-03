#!/usr/bin/env python3
"""knock — laptop-side client for the HU knockd SSH gate.

Connects to the HU's knock port, reads the challenge, signs it with the RSA
private key (via `openssl dgst -sign`), sends the 256-byte signature back. On
'K' the daemon has opened its SSH window — connect as usual.

Protocols (knockd picks; the daemon's hello says which):
  v1  hello 0x01 | nonce[32]                        sign SHA256(nonce)
  v2  hello 0x02 | nonce[32] | idlen | hu_id        sign SHA256("nbt-knock-v2" + hu_id + nonce)
v2 binds the signature to this protocol and this head unit (IMPROVEMENT-PLAN C5).
The hu_id comes from the daemon so the two sides cannot drift apart; it is only
a domain-separation label, never a secret, and never VIN-derived.

Usage:
  knock.py <hu-ip> [port]          # port default 851 (knock.conf PORT on the HU)
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
V2_CONTEXT = b"nbt-knock-v2"


def recv_exact(s, n, what):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            sys.exit("daemon closed mid-%s" % what)
        buf += chunk
    return buf


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
                 "(daemon not running? port filtered? knocking from an "
                 "off-allowlist network — see ALLOW_NETS in knock.conf?)")
    s.settimeout(8)

    ver = s.recv(1)
    if not ver:
        sys.exit("daemon closed before the challenge (off-allowlist network? "
                 "knockd drops those without sending a nonce)")
    nonce = recv_exact(s, 32, "nonce")

    if ver[0] == 1:
        message, hu_id = nonce, None
    elif ver[0] == 2:
        idlen = recv_exact(s, 1, "hu-id length")[0]
        hu_id = recv_exact(s, idlen, "hu-id") if idlen else b""
        message = V2_CONTEXT + hu_id + nonce
    else:
        sys.exit(f"unknown knock protocol {ver[0]} — update knock.py")
    print(f"challenge: {nonce.hex()[:24]}… (proto {ver[0]}"
          + (f", hu {hu_id.decode('ascii', 'replace')}" if hu_id else "") + ")")

    sig = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", KEY],
        input=message, capture_output=True)
    if sig.returncode != 0:
        sys.exit(f"signing failed: {sig.stderr.decode().strip()}")
    if len(sig.stdout) != 256:
        sys.exit(f"unexpected signature length {len(sig.stdout)} (want 256 — RSA-2048 key?)")
    s.sendall(sig.stdout)

    resp = s.recv(16)
    s.close()
    if resp == b"K":
        print(f"KNOCK ACCEPTED — SSH armed at {ip}:3804 for the knock window "
              "(OPEN_S in knock.conf, default 15 min; knock again to extend)")
    elif resp == b"N":
        sys.exit("knock REJECTED — signature didn't verify "
                 "(wrong key? pubkey on HU stale? HU_ID changed with ACCEPT_V1=0?)")
    elif resp == b"E":
        sys.exit("knock accepted but sshd did NOT start — see knock.log on the HU "
                 "(no allowed local address up? sshd config error?)")
    else:
        sys.exit(f"unexpected daemon reply: {resp!r}")


if __name__ == "__main__":
    main()
