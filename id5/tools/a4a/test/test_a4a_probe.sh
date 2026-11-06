#!/bin/bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#
# test_a4a_probe.sh — host test for a4a_probe.sh. Never touches the car: MockA4aServer (same Etch bindings, listening
# side) and small python servers on 127.0.0.1 stand in for the head unit.
# Checks: version, caps refused, cert refused / challenge, av refused without a cert, av accepted then disposed,
# a closed port, a silent non-Etch port, --raw on a port that talks, bad arguments.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROBE="$HERE/../a4a_probe.sh"
TMP="$(mktemp -d)"
PIDS=()
cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; rm -rf "$TMP"; }
trap cleanup EXIT

pass=0 fail=0
check() {   # check <name> <command...>
    local name="$1"; shift
    if "$@"; then pass=$((pass + 1)); echo "ok   $name"; else fail=$((fail + 1)); echo "FAIL $name"; fi
}
has() { grep -q -- "$2" "$1"; }

freeport() { python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])'; }

"$PROBE" --fetch >/dev/null || { echo "FAIL libraries not available"; exit 1; }
LIBS="${A4A_LIBS:-${ID5_LOCAL:-$HERE/../../../local}/a4a-libs}"
CP="$LIBS/idriveconnectkit-etch-0.8.jar:$LIBS/etch-runtime-0.8.jar"

# 1. The mock A4A server.
MP=$(freeport)
java -cp "$CP" "$HERE/MockA4aServer.java" "$MP" >"$TMP/mock.log" 2>&1 &
PIDS+=($!)
for _ in $(seq 1 100); do has "$TMP/mock.log" "mock listening" && break; sleep 0.1; done
check "mock server up" has "$TMP/mock.log" "mock listening $MP"

"$PROBE" 127.0.0.1 "$MP" >"$TMP/a.out" 2>&1; rc=$?
check "default: exit 0" [ "$rc" = 0 ]
check "default: version 9.8.7" has "$TMP/a.out" "^version  OK 9.8.7"
check "default: no caps/cert/av" bash -c "! grep -qE '^(caps|cert|av) ' '$TMP/a.out'"
check "default: verdict A4A" has "$TMP/a.out" "this port is an A4A server"

printf 'NOPE' >"$TMP/bad.cert"
"$PROBE" 127.0.0.1 "$MP" --caps --cert "$TMP/bad.cert" --try-av >"$TMP/b.out" 2>&1; rc=$?
check "refused: exit 0" [ "$rc" = 0 ]
check "refused: caps SecurityException 1" has "$TMP/b.out" "^caps     REFUSED SecurityException errorId=1 errorMsg=not logged in"
check "refused: cert IllegalArgument 2" has "$TMP/b.out" "^cert     REFUSED IllegalArgumentException errorId=2"
check "refused: av SecurityException 3" has "$TMP/b.out" "^av       REFUSED SecurityException errorId=3"

printf 'CERT' >"$TMP/good.cert"
"$PROBE" 127.0.0.1 "$MP" --cert "$TMP/good.cert" --try-av >"$TMP/c.out" 2>&1; rc=$?
check "accepted: exit 0" [ "$rc" = 0 ]
check "accepted: challenge 01020304" has "$TMP/c.out" "^cert     OK 4 byte challenge 01020304 (not signed, no login)"
check "accepted: av handle 7 disposed" has "$TMP/c.out" "^av       OK av_create accepted without a login: handle 7, disposed"
sleep 0.5
check "accepted: mock saw av_dispose 7" has "$TMP/mock.log" "mock av_dispose 7"

# 2. Nothing listening.
CP2=$(freeport)
"$PROBE" 127.0.0.1 "$CP2" >"$TMP/d.out" 2>&1; rc=$?
check "closed: exit 1" [ "$rc" = 1 ]
check "closed: tcp FAILED" has "$TMP/d.out" "^tcp      FAILED"

# 3. A port that accepts and never answers (not Etch).
SP=$(freeport)
python3 -c '
import socket, sys, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", int(sys.argv[1]))); s.listen(8)
conns = []
while True:
    c, _ = s.accept(); conns.append(c)
' "$SP" &
PIDS+=($!)
sleep 0.3
start=$(date +%s)
"$PROBE" 127.0.0.1 "$SP" >"$TMP/e.out" 2>&1; rc=$?
took=$(( $(date +%s) - start ))
check "silent: exit 3" [ "$rc" = 3 ]
check "silent: version FAILED" bash -c "grep -qE '^(version  FAILED|etch     FAILED)' '$TMP/e.out'"
check "silent: no A4A verdict" bash -c "! grep -q 'this port is an A4A server' '$TMP/e.out'"
check "silent: done in < 20 s ($took s)" [ "$took" -lt 20 ]

# 4. --raw on a port that talks first.
RP=$(freeport)
python3 -c '
import socket, sys
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", int(sys.argv[1]))); s.listen(1)
c, _ = s.accept(); c.sendall(b"\x00\x01BCL?"); c.close()
' "$RP" &
PIDS+=($!)
sleep 0.3
"$PROBE" --raw 2 127.0.0.1 "$RP" >"$TMP/f.out" 2>&1; rc=$?
check "raw: exit 0" [ "$rc" = 0 ]
check "raw: hex" has "$TMP/f.out" "^raw      6 bytes, then the server closed: 000142434c3f"
check "raw: ascii" has "$TMP/f.out" '^ascii    ..BCL?'

# 5. Bad arguments.
"$PROBE" >/dev/null 2>&1; rc=$?
check "usage: no args exit 2" [ "$rc" = 2 ]
"$PROBE" 127.0.0.1 "$MP" --bogus >/dev/null 2>&1; rc=$?
check "usage: unknown option exit 2" [ "$rc" = 2 ]

echo "test_a4a_probe: $pass passed, $fail failed"
[ "$fail" = 0 ]
