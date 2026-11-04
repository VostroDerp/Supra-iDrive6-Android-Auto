#!/usr/bin/env python3
"""Host test for mwu_send: a fake mwud that checks the framing and answers like the real one (header + k=v)."""
import socket, struct, subprocess, sys
exe = sys.argv[1]
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]; s.settimeout(5)
fails = 0
def check(name, ok):
    global fails
    print(('  ok   ' if ok else '  FAIL ') + name); fails += 0 if ok else 1
p = subprocess.Popen([exe, '-p', str(port), 'cmd=ap_init', 'iface=uap0', 'ssid=T'], stdout=subprocess.PIPE, text=True)
d, peer = s.recvfrom(8192)
blen, seq = struct.unpack_from('<II', d)
body = d[8:].decode()
check('header length matches body', blen == len(d) - 8)
check('seq 0', seq == 0)
check('module=mwpamod added', body.startswith('module=mwpamod\n'))
check('every line newline-terminated', body.endswith('\n') and 'cmd=ap_init\niface=uap0\nssid=T\n' in body)
r = b'status=0\nmsg=ok\n'; s.sendto(struct.pack('<II', len(r), seq) + r, peer)
out = p.communicate(timeout=5)[0]
check('reply printed', '<<< status=0' in out and p.returncode == 0)
p = subprocess.Popen([exe, '-p', str(port), '-t', '1', 'module=other', 'cmd=x'], stdout=subprocess.PIPE, text=True)
d, peer = s.recvfrom(8192)
check('explicit module kept, not duplicated', d[8:].decode().count('module=') == 1)
out = p.communicate(timeout=5)[0]
check('no reply -> exit 1', p.returncode == 1 and 'no reply' in out)
check('bad arg -> exit 2', subprocess.run([exe, 'notkv'], capture_output=True).returncode == 2)
print('mwu_send host test:', 'all passed' if not fails else f'{fails} failed'); sys.exit(1 if fails else 0)
