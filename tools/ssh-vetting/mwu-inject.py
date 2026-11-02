#!/usr/bin/env python3
"""mwu-inject.py — emit a gdb script that hijacks wifidir_app_p97's connected
UDP socket (fd -> 127.0.0.1:9196, mwud_pub) to send an arbitrary mwud command.

Wire format: [u32 LE body_len][u32 0][kv lines each \\n-terminated].
Runtime addrs resolved via GOT slots (non-PIE, fixed VMAs):
  writev -> 0x126708, malloc -> 0x126630   (wifidir_app)

Usage: ./mwu-inject.py <app_pid> <cmd_fd> [kv pairs...] > inject.gdb
       gdb -batch -x inject.gdb
e.g.:  ./mwu-inject.py 2200001 3 iface=uap0 cmd=ap_init ssid=NBTAA-DIRECT \
         key=1234567890 auth=0020 encrypt=0008 module=mwpamod
"""
import struct, sys

pid = int(sys.argv[1]); fd = int(sys.argv[2])
kv = [a for a in sys.argv[3:]]
if not any(a.startswith('module=') for a in kv):
    kv.insert(0, 'module=mwpamod')
body = ''.join(a if a.endswith('\n') else a + '\n' for a in kv)
pkt = struct.pack('<II', len(body), 0) + body.encode()
L = len(pkt)

# escape into a gdb string literal (\xNN for every byte)
gs = ''.join(f'\\x{b:02x}' for b in pkt)

print(f"""# gdb inject script — attach app pid {pid}, writev on fd {fd}
set architecture arm
target remote 192.168.0.17:8001
attach {pid}
set $wv  = *(unsigned int*)0x126708   # resolved writev()
set $mal = *(unsigned int*)0x126630   # resolved malloc()
call (void*)((void*(*)(unsigned int))$mal)({L + 16})
set $pkt = $1
set {{char[{L}]}}$pkt = "{gs}"
call (void*)((void*(*)(unsigned int))$mal)(8)
set $iov = $1
set {{void*}}$iov = $pkt
set {{unsigned int}}($iov+4) = {L}
call (int)((int(*)(int,void*,int))$wv)({fd}, $iov, 1)
detach
""")
