#!/usr/bin/env python3
"""mwu-pdebug.py — QNX pdebug DS-protocol client. Attaches to mwud_pub,
hijacks a stopped thread's registers to call the internal send wrapper
@0x12b090(fd,pkt,len,flags,sa,sa_len) delivering a framed mwud command
datagram to its own listener. Restores regs and detaches cleanly.

Usage: mwu-pdebug.py <host> <port> <pid> <srcfd> <dst_ip> <dst_port> [kv...]
"""
import socket, struct, sys, time

HOST, PORT, PID, SRCFD = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
DST_IP, DST_PORT = sys.argv[5], int(sys.argv[6])
kv = [a if a.endswith('\n') else a+'\n' for a in sys.argv[7:]]
if not any(a.startswith('module=') for a in kv): kv.insert(0, 'module=mwpamod\n')
body = ''.join(kv).encode()
pkt = struct.pack('<II', len(body), 0) + body

SENDTO = 0x12b090
SCRATCH = 0x195768
PKT_ADDR = SCRATCH + 0x800
SA_ADDR  = PKT_ADDR + len(pkt) + 16
BKPT_ADDR = PKT_ADDR + len(pkt) + 48   # bkpt opcode placed in .bss (no NX on QNX ARM)
CH = 1                        # debug channel

class DS:
    def __init__(s, host, port):
        s.f = socket.create_connection((host, port), 10); s.f.settimeout(15)
        s.mid = 0; s.buf = b''
        s._raw(b'\x7e\x00\xff\x7e')          # channel reset
        s._raw(b'\x7e\x01\xfe\x7e')          # debug channel
    def _raw(s, b): s.f.sendall(b)
    def _frame(s, payload):
        csum = 0; out = bytearray(b'\x7e')
        for c in payload:
            csum += c
            if c in (0x7e, 0x7d): out += bytes((0x7d, c ^ 0x20))
            else: out.append(c)
        csum = (csum & 0xff) ^ 0xff
        if csum in (0x7e, 0x7d): out += bytes((0x7d, csum ^ 0x20))
        else: out.append(csum)
        out.append(0x7e); s._raw(bytes(out))
    def _recv_frame(s):
        # accumulate until a full 7e..7e frame arrives
        while True:
            i = s.buf.find(b'\x7e'); j = s.buf.find(b'\x7e', i + 1) if i >= 0 else -1
            if i >= 0 and j > i:
                fr = s.buf[i + 1:j]; s.buf = s.buf[j + 1:]
                data = bytearray(); mod = 0
                for c in fr:
                    if c == 0x7d: mod = 0x20; continue
                    data.append(c ^ mod); mod = 0
                if len(data): return bytes(data[:-1])  # strip csum
            c = s.f.recv(4096)
            if not c: raise IOError('eof')
            s.buf += c
    def ask(s, cmd, subcmd, payload=b''):
        s.mid = (s.mid + 1) & 0xff
        s._frame(bytes((cmd, subcmd, s.mid, CH)) + payload)
        while True:
            r = s._recv_frame()
            if len(r) < 4 or r[0] not in (32, 33, 34, 35, 64): continue  # skip junk/notify
            if r[0] == 64: print('  notify:', r[:12].hex()); continue
            return r

d = DS(HOST, PORT)
r = d.ask(0, 0, bytes((0, 3, 0, 0)))            # connect, protover 0.3
print('connect:', r[:8].hex())
r = d.ask(5, 0, struct.pack('<i', PID))         # attach
print('attach :', r[:8].hex())
r = d.ask(2, 0, struct.pack('<ii', PID, 0))     # select pid, tid=any
print('select :', r[:8].hex())
r = d.ask(8, 0)                                 # stop
print('stop   :', r[:8].hex())

# read general regs: ARM regset = 68 bytes (r0-r15 + cpsr)
r = d.ask(11, 0, struct.pack('<HH', 0, 68))     # regrd offset=0 size=68
print('regrd  :', len(r), 'bytes', r[:8].hex())
regdata = r[4:]                                # okdata payload follows 4-byte hdr
n = len(regdata) // 4
regs = [int.from_bytes(regdata[i*4:i*4+4], 'little') for i in range(n)]
# ARM regset: r0-r15, cpsr (verified live: sp~0xffcxx, pc/lr in text)
pc_i, sp_i, lr_i = 15, 13, 14
print('  pc=%08x sp=%08x lr=%08x nregs=%d' % (regs[pc_i], regs[sp_i], regs[lr_i], n))
saved = regdata

# memory writes
sa = bytes([0, 2]) + struct.pack('!H', DST_PORT) + bytes(int(x) for x in DST_IP.split('.')) + b'\x00' * 8
def mw(addr, data):
    r = d.ask(10, 0, struct.pack('<IQ', 0, addr) + data)
    return r[:4]
print('mw pkt :', mw(PKT_ADDR, pkt))
print('mw sa  :', mw(SA_ADDR, sa))
# hand-placed bkpt opcode — DStMsg_brk returns EINVAL on this pdebug
print('mw bkpt:', mw(BKPT_ADDR, struct.pack('<I', 0xE1200070)))
newsp = regs[sp_i] - 32
print('mw stk :', mw(newsp, struct.pack('<II', SA_ADDR, 16)))

# write regs: r0=fd r1=pkt r2=len r3=flags sp lr->orig pc pc->sendto
nr = regs[:]
nr[0], nr[1], nr[2], nr[3] = SRCFD, PKT_ADDR, len(pkt), 0
nr[sp_i], nr[lr_i], nr[pc_i] = newsp, regs[pc_i], SENDTO   # lr = saved pc: call returns into normal flow
wr = b''.join(struct.pack('<I', v) for v in nr)
r = d.ask(12, 0, struct.pack('<H', 0) + wr)          # regwr offset=0
print('regwr  :', r[:8].hex())

# counted run: execute the wrapper (~40 instrs) then auto-stop -> pdebug
# returns to command mode (plain run leaves it blocked in event-wait)
r = d.ask(13, 1, struct.pack('<I', 200))             # run subcmd=RUN_COUNT(1), N=200
print('runcnt :', r[:8].hex())
r = d.ask(11, 0, struct.pack('<HH', 0, 68))
regdata2 = r[4:]
r2 = [int.from_bytes(regdata2[i*4:i*4+4], 'little') for i in range(len(regdata2)//4)]
print('after  : r0=%08x pc=%08x' % (r2[0], r2[pc_i]))

# restore original register file -> thread resumes at its frozen point and
# immediately notices fd4 readable -> processes our queued command itself
r = d.ask(12, 0, struct.pack('<H', 0) + saved)
print('restore:', r[:8].hex())
r = d.ask(13, 0, struct.pack('<I', 0))
print('run2   :', r[:8].hex())
r = d.ask(6, 0, struct.pack('<i', PID))              # detach
print('detach :', r[:8].hex())
