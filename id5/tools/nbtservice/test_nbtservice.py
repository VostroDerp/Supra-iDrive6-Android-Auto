#!/usr/bin/env python3
"""Host test for libNbtService.so (IMPROVEMENT-PLAN A1 + A5).

Builds the plugin and the fake framework with the host g++, then drives the plugin against a
REAL knockd (built from ../knock/knockd.c) in a temp directory, so what is tested is the actual
A1 chain — plugin starts the gate — and the actual A5 chain — checkbox opens and closes the
window over the control port.

  python3 test_nbtservice.py

Needs g++, gcc, openssl, python3. Nothing here touches the car or any path outside the temp
directory: the plugin is built with -DNBT_HOST_TEST, which makes it take its /var/opt/aa paths
from NBT_SERVICE_DIR.
"""
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
KNOCK = os.path.join(HERE, "..", "knock")
CXX = os.environ.get("CXX", "g++")
CC = os.environ.get("CC", "gcc")
STD = ["-std=gnu++98", "-Wall", "-Wextra"]


def run(*args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def build(tmp):
    lib = os.path.join(tmp, "libNbtService.so")
    exe = os.path.join(tmp, "nbtservice_host_test")
    knockd = os.path.join(tmp, "knockd-build")

    r = run(CXX, *(STD + ["-fPIC", "-shared", "-DNBT_HOST_TEST", "-o", lib,
                          os.path.join(HERE, "nbt_service_app.cpp"), "-ldl"]))
    if r.returncode:
        print(r.stderr)
        sys.exit("plugin build failed")
    plugin_warnings = [l for l in r.stderr.splitlines() if "warning:" in l]

    r = run(CXX, *(STD + ["-rdynamic", "-o", exe,
                          os.path.join(HERE, "nbtservice_host_test.cpp"),
                          os.path.join(HERE, "nbtservice_fake_appframework.cpp"), "-ldl"]))
    if r.returncode:
        print(r.stderr)
        sys.exit("host test build failed")

    r = run(CC, "-std=gnu89", "-Wall", "-O1", "-Wno-deprecated-declarations",
            "-o", knockd, os.path.join(KNOCK, "knockd.c"), "-lcrypto")
    if r.returncode:
        print(r.stderr)
        sys.exit("knockd build failed")
    return lib, exe, knockd, plugin_warnings


def make_gate_dir(tmp, knockd):
    """A directory that looks like /var/opt/aa: knockd, knock.conf, a key, a stub sshd."""
    d = os.path.join(tmp, "aa")
    os.makedirs(d)
    shutil.copy(knockd, os.path.join(d, "knockd"))
    os.chmod(os.path.join(d, "knockd"), 0o755)

    key = os.path.join(d, "k.pem")
    run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", key)
    run("openssl", "rsa", "-in", key, "-RSAPublicKey_out", "-out", key + ".pub.pem")

    sshd = os.path.join(d, "sshd")
    with open(sshd, "w") as f:
        f.write("#!/usr/bin/env python3\nimport time\ntime.sleep(600)\n")
    os.chmod(sshd, 0o755)
    with open(os.path.join(d, "sshd_config"), "w") as f:
        f.write("Port 3804\nListenAddress 0.0.0.0\n")

    ctl = free_port()
    with open(os.path.join(d, "knock.conf"), "w") as f:
        f.write("PORT=%d\n" % free_port())
        f.write("PUBKEY=%s\n" % (key + ".pub.pem"))
        f.write("SSHCMD=%s -f %s\n" % (sshd, os.path.join(d, "sshd_config")))
        f.write("SSHD_CONF_OUT=%s\n" % os.path.join(d, "sshd_config.active"))
        f.write("LOG=%s\n" % os.path.join(d, "knock.log"))
        f.write("ALLOW_NETS=127.0.0.0/8\nOPEN_S=900\nCONN_S=3\nHU_ID=nbt-test\n")
        f.write("CTL_PORT=%d\n" % ctl)
    return d, ctl


def kill_all(pattern):
    r = run("pgrep", "-f", pattern)
    for p in r.stdout.split():
        try:
            os.kill(int(p), signal.SIGKILL)
        except (OSError, ValueError):
            pass


def main():
    tmp = tempfile.mkdtemp(prefix="nbtservice-")
    try:
        lib, exe, knockd, warnings = build(tmp)
        print("  %s plugin builds with -Wall -Wextra and no warnings%s"
              % ("ok  " if not warnings else "FAIL", "" if not warnings else " — " + warnings[0]))
        bad = 1 if warnings else 0

        gate_dir, ctl = make_gate_dir(tmp, knockd)
        print("\n-- with a real knockd in the directory --")
        r = subprocess.run([exe, lib, "gate"], capture_output=True, text=True,
                           env=dict(os.environ, NBT_SERVICE_DIR=gate_dir))
        print(r.stdout)
        if r.stderr.strip():
            print(r.stderr.strip())
        bad += r.returncode != 0
        log = os.path.join(gate_dir, "knock.log")
        if os.path.exists(log):
            print("knock.log:")
            for line in open(log).read().splitlines()[-6:]:
                print("    " + line)
        kill_all(os.path.join(gate_dir, "knock.conf"))
        kill_all(os.path.join(gate_dir, "sshd"))

        print("-- with nothing to start --")
        empty = os.path.join(tmp, "empty")
        os.makedirs(empty)
        r = subprocess.run([exe, lib, "nogate"], capture_output=True, text=True,
                           env=dict(os.environ, NBT_SERVICE_DIR=empty))
        print(r.stdout)
        bad += r.returncode != 0

        print("nbtservice host test: %s" % ("FAILED" if bad else "all checks passed"))
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
