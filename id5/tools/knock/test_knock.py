#!/usr/bin/env python3
"""Host test for knockd v2 (IMPROVEMENT-PLAN C7).

Builds knockd with the host gcc, makes a throwaway RSA-2048 key, and drives the
real daemon over loopback with a stub "sshd". Covers:

  good knock (v2)          K, the stub sshd runs, ListenAddress written
  good knock (v1)          K while ACCEPT_V1=1, N once it is 0
  wrong key                N
  short signature          N, connection closed
  slow client              dropped within CONN_S; a real knock still gets in (C1)
  off-allowlist address    closed with no nonce (A2)
  log cap                  log truncated at LOG_MAX with a marker (C2)
  open window              sshd SIGTERMed after OPEN_S (A3)
  second knock             window extended, no second sshd (C4)
  control port             open / status / close with no knock (A5)

  python3 test_knock.py            # run everything
  KNOCKD_CC=gcc python3 test_knock.py
Nothing here touches the car. No file outside a temp directory is written.
"""
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CC = os.environ.get("KNOCKD_CC", "gcc")
PASS, FAIL = [], []


def check(name, ok, detail=""):
    (PASS if ok else FAIL).append(name)
    print(("  ok   " if ok else "  FAIL ") + name + ((" — " + detail) if detail else ""))


def sh(*args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Daemon:
    """One knockd instance in its own temp directory."""

    def __init__(self, tmp, binary, key, **conf):
        self.dir = tempfile.mkdtemp(dir=tmp)
        self.binary = binary
        self.key = key
        self.port = free_port()
        self.ctl_port = free_port()
        self.log = os.path.join(self.dir, "knock.log")
        self.stub_log = os.path.join(self.dir, "stub.log")
        self.sshd = os.path.join(self.dir, "sshd")          # name matters: knockd adds -D
        self.template = os.path.join(self.dir, "sshd_config")
        self.active = self.template + ".active"
        # stub sshd: logs its argv, then either fails (STUB_FAIL) or stays in
        # the foreground like the real sshd -D does
        with open(self.sshd, "w") as f:
            f.write("#!/usr/bin/env python3\n"
                    "import os, sys, time\n"
                    "open(os.environ['STUB_LOG'], 'a').write(' '.join(sys.argv[1:]) + '\\n')\n"
                    "rc = int(os.environ.get('STUB_FAIL', '0'))\n"
                    "if rc:\n    sys.exit(rc)\n"
                    "time.sleep(600)\n")
        os.chmod(self.sshd, 0o755)
        with open(self.template, "w") as f:
            f.write("Port 3804\nListenAddress 0.0.0.0\nPermitRootLogin without-password\n")
        self.conf_path = os.path.join(self.dir, "knock.conf")
        self.conf = {
            "PORT": self.port,
            "PUBKEY": key + ".pub.pem",
            "SSHCMD": "%s -f %s" % (self.sshd, self.template),
            "SSHD_CONF_OUT": self.active,
            "LOG": self.log,
            "LOG_MAX": 65536,
            "ALLOW_NETS": "127.0.0.0/8",
            "OPEN_S": 900,
            "CONN_S": 3,
            "CTL_PORT": self.ctl_port,
            "HU_ID": "nbt-test",
            "ACCEPT_V1": 1,
        }
        self.conf.update(conf)
        with open(self.conf_path, "w") as f:
            for k, v in self.conf.items():
                f.write("%s=%s\n" % (k, v))

    def start(self, stub_fail=0):
        env = dict(os.environ, STUB_LOG=self.stub_log, STUB_FAIL=str(stub_fail))
        r = subprocess.run([self.binary, self.conf_path], capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise RuntimeError("knockd refused to start: rc=%d %s" % (r.returncode, self.logtext()))
        for _ in range(50):                            # wait for the listener
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                return self
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("knockd did not listen on %d: %s" % (self.port, self.logtext()))

    def pids(self):
        r = sh("pgrep", "-f", self.conf_path)
        return [int(x) for x in r.stdout.split()]

    def stub_pids(self):
        r = sh("pgrep", "-f", self.sshd)
        return [int(x) for x in r.stdout.split()]

    def stop(self):
        for p in self.pids() + self.stub_pids():
            try:
                os.kill(p, signal.SIGKILL)
            except OSError:
                pass

    def zombies(self):
        """Children of the daemon left unreaped."""
        out = []
        for p in self.pids():
            r = sh("ps", "-o", "pid=,stat=", "--ppid", str(p))
            out += [l for l in r.stdout.splitlines() if "Z" in l.split()[-1]]
        return out

    def logtext(self):
        try:
            with open(self.log) as f:
                return f.read()
        except OSError:
            return ""

    def ctl(self, cmd):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(3)
        s.connect(("127.0.0.1", self.ctl_port))
        s.send(cmd.encode())
        try:
            return s.recv(128).decode().strip()
        except socket.timeout:
            return "(no reply)"
        finally:
            s.close()

    def knock(self, key=None, proto=2, timeout=10):
        """One knock with openssl; returns the daemon's reply byte as str."""
        key = key or self.key
        s = socket.create_connection(("127.0.0.1", self.port), timeout=timeout)
        s.settimeout(timeout)
        try:
            ver, nonce, hu = recv_hello(s)
        except OSError:
            s.close()
            return "closed"
        if ver == 2 and proto == 2:
            msg = b"nbt-knock-v2" + hu + nonce
        else:
            msg = nonce
        sig = subprocess.run(["openssl", "dgst", "-sha256", "-sign", key],
                             input=msg, capture_output=True).stdout
        s.sendall(sig)
        r = s.recv(4)
        s.close()
        return r.decode() if r else "closed"


def recv_hello(s):
    """Read a whole hello: version, nonce and (v2) the hu-id."""
    ver = recv_exact(s, 1)
    nonce = recv_exact(s, 32)
    hu = b""
    if ver[0] == 2:
        hu = recv_exact(s, recv_exact(s, 1)[0])
    return ver[0], nonce, hu


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c:
            raise OSError("short read")
        buf += c
    return buf


def wait_for(fn, secs, step=0.2):
    end = time.time() + secs
    while time.time() < end:
        if fn():
            return True
        time.sleep(step)
    return fn()


def main():
    tmp = tempfile.mkdtemp(prefix="knocktest-")
    try:
        binary = os.path.join(tmp, "knockd")
        build = sh(CC, "-std=gnu89", "-Wall", "-Wextra", "-Wno-deprecated-declarations",
                   "-O1", "-o", binary, os.path.join(HERE, "knockd.c"), "-lcrypto")
        if build.returncode != 0:
            print(build.stderr)
            sys.exit("build failed")
        warn = [l for l in build.stderr.splitlines() if "warning:" in l]
        check("builds with -Wall -Wextra and no warnings", not warn, "; ".join(warn[:3]))

        key = os.path.join(tmp, "k.pem")
        sh("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
           "-out", key)
        sh("openssl", "rsa", "-in", key, "-RSAPublicKey_out", "-out", key + ".pub.pem")
        other = os.path.join(tmp, "other.pem")
        sh("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
           "-out", other)

        # ---- fail-safe start-up ------------------------------------------------
        d = Daemon(tmp, binary, key, SSHCMD="")
        r = subprocess.run([binary, d.conf_path], capture_output=True, text=True)
        check("no SSHCMD: refuses to run", r.returncode == 1 and "fail-safe" in d.logtext())
        d = Daemon(tmp, binary, key, ALLOW_NETS="169.254.0.0/44")
        r = subprocess.run([binary, d.conf_path], capture_output=True, text=True)
        check("bad ALLOW_NETS: refuses to run", r.returncode == 1 and "ALLOW_NETS" in d.logtext())
        d = Daemon(tmp, binary, key, PUBKEY=os.path.join(tmp, "nope.pem"))
        r = subprocess.run([binary, d.conf_path], capture_output=True, text=True)
        check("missing public key: refuses to run", r.returncode == 1)

        # ---- a good v2 knock ---------------------------------------------------
        d = Daemon(tmp, binary, key).start()
        try:
            reply = d.knock()
            check("v2 knock accepted", reply == "K", "reply %r" % reply)
            check("sshd stub started", wait_for(lambda: bool(d.stub_pids()), 3))
            args = open(d.stub_log).read() if os.path.exists(d.stub_log) else ""
            check("sshd got -D (listener is knockd's child)", "-D" in args, args.strip())
            check("sshd got the generated config", d.active in args, args.strip())
            active = open(d.active).read() if os.path.exists(d.active) else ""
            check("ListenAddress 127.0.0.1 written, 0.0.0.0 dropped",
                  "ListenAddress 127.0.0.1" in active and "0.0.0.0" not in active,
                  active.replace("\n", " | "))
            check("Port kept from the template", "Port 3804" in active)

            # second knock: extends, no second sshd (C4)
            n_before = len(d.stub_pids())
            reply = d.knock()
            check("second knock accepted", reply == "K")
            check("no second sshd", len(d.stub_pids()) == n_before, str(d.stub_pids()))
            check("log says the window was extended", "window extended" in d.logtext())

            # ---- wrong key, short signature ---------------------------------
            check("wrong key rejected", d.knock(key=other) == "N")
            s = socket.create_connection(("127.0.0.1", d.port), timeout=5)
            recv_hello(s)
            s.sendall(b"short")
            s.shutdown(socket.SHUT_WR)
            check("short signature rejected", s.recv(4) == b"N")
            s.close()

            # ---- the control port (A5) --------------------------------------
            st = d.ctl("status")
            check("ctl status reports the open window", st.startswith("open"), st)
            check("ctl close closes it", d.ctl("close") == "closed")
            check("sshd stopped by ctl close", wait_for(lambda: not d.stub_pids(), 3))
            op = d.ctl("open 60")
            check("ctl open re-opens with no knock", op.startswith("open"), op)
            check("sshd restarted by ctl open", wait_for(lambda: bool(d.stub_pids()), 3))
        finally:
            d.stop()

        # ---- C1: a slow client must not block real knocks ---------------------
        d = Daemon(tmp, binary, key, CONN_S=2).start()
        try:
            slow = socket.create_connection(("127.0.0.1", d.port), timeout=5)
            recv_hello(slow)
            # hold the slot: one byte, then silence
            slow.sendall(b"\x00")
            t1 = time.time()
            reply = d.knock(timeout=20)
            took = time.time() - t1
            check("knock gets in while a slow client holds the socket", reply == "K",
                  "reply %r after %.1f s" % (reply, took))
            check("the slow client was dropped within ~CONN_S", took < 2 + 3,
                  "%.1f s (CONN_S=2)" % took)
            check("slow connection is closed by the daemon",
                  wait_for(lambda: slow.recv(16) in (b"", b"N"), 5), "")
            slow.close()
            check("slow client logged as a bad knock",
                  "bad knock" in d.logtext(), d.logtext()[-200:])
            check("bad knocks are summarised, not one line each",
                  len(re.findall(r"bad knock", d.logtext())) <= 2, d.logtext()[-200:])
        finally:
            d.stop()

        # ---- A2: an off-allowlist local address gets no nonce -----------------
        d = Daemon(tmp, binary, key, ALLOW_NETS="10.199.0.0/16").start()
        try:
            s = socket.create_connection(("127.0.0.1", d.port), timeout=5)
            first = s.recv(1)
            s.close()
            check("off-allowlist: closed with no challenge", first == b"",
                  "got %r" % first)
            check("off-allowlist: logged with both addresses",
                  "not in ALLOW_NETS" in d.logtext(), d.logtext()[-200:])
            # a scanner on a network we do not control must not get one log line per connection
            for _ in range(20):
                try:
                    socket.create_connection(("127.0.0.1", d.port), timeout=2).close()
                except OSError:
                    pass
            lines = len(re.findall(r"not in ALLOW_NETS", d.logtext()))
            check("off-allowlist: one detail line per minute, not per connection", lines <= 2,
                  "%d lines for 21 connections" % lines)
            check("off-allowlist: no sshd started", not d.stub_pids())
        finally:
            d.stop()

        # ---- A3: the window closes on its own --------------------------------
        d = Daemon(tmp, binary, key, OPEN_S=10).start()
        try:
            check("knock opens a 10 s window", d.knock() == "K")
            check("sshd up", wait_for(lambda: bool(d.stub_pids()), 3))
            gone = wait_for(lambda: not d.stub_pids(), 20)
            check("sshd stopped when the window expired", gone, d.logtext()[-300:])
            check("window closure logged", "window expired" in d.logtext())
            # the SIGTERMed listener must be collected, or every window close leaks a zombie
            check("no zombie left by the closed window", not d.zombies(), str(d.zombies()))
            d.knock()
            check("the window can be opened again afterwards",
                  wait_for(lambda: bool(d.stub_pids()), 5))
            check("still no zombies after a second cycle", not d.zombies(), str(d.zombies()))
        finally:
            d.stop()

        # ---- C2: the log cannot grow without bound ---------------------------
        d = Daemon(tmp, binary, key, LOG_MAX=2048)
        with open(d.log, "w") as f:
            f.write("x" * 8192)
        d.start()
        try:
            d.knock()
            size = os.path.getsize(d.log)
            check("log truncated once over LOG_MAX", size < 4096, "%d bytes" % size)
            check("truncation left a marker", "log truncated" in d.logtext())
        finally:
            d.stop()

        # ---- C5: v1 still accepted, and can be switched off ------------------
        d = Daemon(tmp, binary, key, ACCEPT_V1=1).start()
        try:
            check("v1 signature accepted while ACCEPT_V1=1", d.knock(proto=1) == "K")
            check("log records the protocol version", "proto v1" in d.logtext())
        finally:
            d.stop()
        d = Daemon(tmp, binary, key, ACCEPT_V1=0).start()
        try:
            check("v1 signature rejected once ACCEPT_V1=0", d.knock(proto=1) == "N")
            check("v2 signature still accepted", d.knock(proto=2) == "K")
        finally:
            d.stop()

        # ---- sshd that fails to start ----------------------------------------
        d = Daemon(tmp, binary, key)
        d.start(stub_fail=3)
        try:
            reply = d.knock()
            check("sshd failure answered with 'E', not 'K'", reply == "E", "reply %r" % reply)
            check("sshd failure logged", "status 3" in d.logtext(), d.logtext()[-200:])
        finally:
            d.stop()

        # ---- knock.py speaks the same protocol -------------------------------
        d = Daemon(tmp, binary, key).start()
        try:
            r = subprocess.run([sys.executable, os.path.join(HERE, "knock.py"),
                                "127.0.0.1", str(d.port)],
                               capture_output=True, text=True, env=dict(os.environ, KNOCK_KEY=key))
            check("knock.py gets in", r.returncode == 0 and "ACCEPTED" in r.stdout,
                  (r.stdout + r.stderr).strip()[:160])
            check("knock.py used v2", "proto 2" in r.stdout and "nbt-test" in r.stdout,
                  r.stdout.strip())
        finally:
            d.stop()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\n%d passed, %d failed" % (len(PASS), len(FAIL)))
    for f in FAIL:
        print("  FAILED: " + f)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
