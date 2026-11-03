#!/usr/bin/env python3
"""mwu.py — mwud client for the NBT Evo HU (reversed wire format).

Wire format: [u32 LE body_len][u32 seq][body = newline-separated k=v, each
line \\n-terminated]. Replies come back to our source address.

Usage:
  ./mwu.py <host> [--port 9196] <cmd...>
  ./mwu.py 192.168.0.17 ap_init iface=uap0 ssid=NBTAA-DIRECT key=1234567890 auth=0020 encrypt=0008
  ./mwu.py 192.168.0.17 ap_start iface=uap0
Pairs are sent as module=mwpamod iface=... cmd=... plus any extra k=v args.
"""
import socket, struct, sys, time

def send(host, port, body, timeout=2.0):
    if not body.endswith('\n'):
        body += '\n'
    pkt = struct.pack('<II', len(body), 0) + body.encode()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    s.connect((host, port))
    s.send(pkt)
    try:
        d = s.recv(8192)
        if len(d) >= 8:
            blen, seq = struct.unpack_from('<II', d, 0)
            return d[8:8+blen].decode('utf-8', 'replace')
        return repr(d)
    except socket.timeout:
        return None

def main():
    args = sys.argv[1:]
    host = args.pop(0)
    port = 9196
    if args[:2] == ['--port', args[1] if len(args)>1 else '']:
        port = int(args[1]); args = args[2:]
    elif '--port' in args:
        i = args.index('--port'); port = int(args[i+1]); del args[i:i+2]
    cmd = args.pop(0)
    kv = {'module': 'mwpamod', 'cmd': cmd}
    for a in args:
        k, _, v = a.partition('=')
        kv[k] = v
    body = ''.join(f"{k}={v}\n" for k, v in kv.items())
    print(f">>> {body}", end='')
    r = send(host, port, body)
    print(f"<<< {r if r is not None else '(no reply)'}")

if __name__ == '__main__':
    main()
