#!/bin/bash
# test_car.sh — host test for car.sh's SSH transport. Never touches the car: a throwaway sshd on 127.0.0.1 with
# throwaway keys stands in for the HU, HOME / ID5_LOCAL / XDG_RUNTIME_DIR point into a temp dir, so the real keys,
# ~/.putty and id5/local are not read or written.
# Checks: host key taken from a plink-style cache, shared OpenSSH connection (first call opens it, later calls reuse
# it and are fast), exit codes, push (size + exec bit in one call), pull, the run poll command (grep -c on the "HU"),
# CAR_SSH=plink, auto fallback to plink when OpenSSH can't connect, close.
# Needs: sshd, ssh, plink, puttygen, python3. Usage: id5/tools/car/test_car.sh
set -u
CAR="$(cd "$(dirname "$0")" && pwd)/car.sh"
T=$(mktemp -d); PORT=$((20000 + RANDOM % 20000))
cleanup() { [ -f "$T/sshd.pid" ] && kill "$(cat "$T/sshd.pid")" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT
fails=0
ok()   { echo "ok    $*"; }
bad()  { echo "FAIL  $*"; fails=$((fails + 1)); }
check() { local what=$1; shift; if "$@"; then ok "$what"; else bad "$what"; fi; }

for t in sshd ssh plink puttygen python3; do command -v $t >/dev/null || [ -x /usr/sbin/$t ] || { echo "skip: no $t"; exit 0; }; done
SSHD=$(command -v sshd || echo /usr/sbin/sshd)

# Throwaway HU: host key, client key (+ .ppk), sshd on 127.0.0.1:$PORT.
mkdir -p "$T/home/.putty" "$T/local" "$T/run"
ssh-keygen -q -t rsa -b 2048 -N '' -f "$T/hostkey"
ssh-keygen -q -t rsa -b 2048 -m PEM -N '' -f "$T/client.pem"
puttygen "$T/client.pem" -O private -o "$T/client.ppk" 2>/dev/null
cp "$T/client.pem.pub" "$T/authorized_keys"
cat > "$T/sshd_config" <<EOF
Port $PORT
ListenAddress 127.0.0.1
HostKey $T/hostkey
PidFile $T/sshd.pid
AuthorizedKeysFile $T/authorized_keys
PasswordAuthentication no
KbdInteractiveAuthentication no
StrictModes no
UsePAM no
EOF
"$SSHD" -f "$T/sshd_config" -E "$T/sshd.log"
for _ in $(seq 1 30); do timeout 1 bash -c ": </dev/tcp/127.0.0.1/$PORT" 2>/dev/null && break; sleep 0.1; done

# plink's cache format for the host key: rsa2@PORT:HOST 0xE,0xN
python3 - "$T/hostkey.pub" "$PORT" > "$T/home/.putty/sshhostkeys" <<'PY'
import base64, struct, sys
blob = base64.b64decode(open(sys.argv[1]).read().split()[1])
def rd(b, i):
    n = struct.unpack(">I", b[i:i + 4])[0]; return b[i + 4:i + 4 + n], i + 4 + n
_, i = rd(blob, 0); e, i = rd(blob, i); n, i = rd(blob, i)
print("rsa2@%s:127.0.0.1 0x%x,0x%x" % (sys.argv[2], int.from_bytes(e, "big"), int.from_bytes(n, "big")))
PY

export HOME="$T/home" ID5_LOCAL="$T/local" XDG_RUNTIME_DIR="$T/run" HU_IP=127.0.0.1 SSH_PORT=$PORT KNOCK_PORT=1
export KNOCK_KEY="$T/client.pem" KNOCK_PPK="$T/client.ppk" CAR_USER=$(id -un)
unset CAR_SSH CAR_KNOWN_HOSTS

# 1. First call opens the shared connection; known_hosts comes from the plink cache.
out=$("$CAR" sh 'echo hello' 2>"$T/err")
check "first call runs the command" [ "$out" = hello ]
check "known_hosts made from the plink cache" grep -q "^\[127.0.0.1\]:$PORT ssh-rsa " "$T/local/car_known_hosts"
check "no fallback warning" [ ! -s "$T/err" ]
check "shared connection up" bash -c "'$CAR' status | grep -q 'OpenSSH connection up'"

# 2. Reuse is fast, and the remote exit code comes back.
s=$EPOCHREALTIME; for _ in 1 2 3 4 5; do "$CAR" sh true; done; e=$EPOCHREALTIME
dt=$(echo "($e - $s) / 5" | bc -l)
check "reused calls are fast ($(printf %.3f "$dt") s each)" [ "$(echo "$dt < 0.5" | bc)" = 1 ]
"$CAR" sh 'exit 7'; check "remote exit code kept" [ $? = 7 ]

# 3. push: size check and the exec bit in one call; pull back.
head -c 300000 /dev/urandom > "$T/blob"; chmod +x "$T/blob"
check "push" "$CAR" push "$T/blob" "$T/remote-blob"
check "push kept +x" [ -x "$T/remote-blob" ]
check "pull" "$CAR" pull "$T/remote-blob" "$T/back"
check "pull is byte-identical" cmp -s "$T/blob" "$T/back"

# 4. The run poll command (same construction as car.sh run): counts on the "HU", labels without backslashes.
LOG="$T/nbt-usb99.log"
printf '%s\n' '[UsbAccessory] connected to io-usb' '[UsbDirect] accessory 18d1:2d00 at usb0' \
  '>>> TLS Handshake Complete! Sending' '[AASession] Streaming Media Frame 1' '[AASession] Streaming Media Frame 2' > "$LOG"
POLL=$(sed -n '/^        POLL=""/,/^        done/p' "$CAR")
st=$(eval "$POLL"; "$CAR" sh "$POLL" | awk "$(sed -n "s/.*awk '\(\$1 > 0 .*\)')\$/\1/p" "$CAR")")
check "poll counts milestones" [ "$st" = "1 connected to io-usb;1 [UsbDirect] accessory [0-9a-f][0-9a-f][0-9a-f][0-9a-f]:;1 TLS Handshake Complete;2 Streaming Media Frame;" ]

# 5. plink transport, forced and as the automatic fallback.
"$CAR" close >/dev/null
out=$(CAR_SSH=plink "$CAR" sh 'echo viaplink' 2>&1)
check "CAR_SSH=plink works" [ "$out" = viaplink ]
check "plink did not open a shared connection" bash -c "! '$CAR' status | grep -q 'OpenSSH connection up'"
mv "$T/local/car_known_hosts" "$T/kh.saved"; echo "[127.0.0.1]:$PORT ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAAAgQCwrong" > "$T/local/car_known_hosts"
out=$("$CAR" sh 'echo fellback' 2>"$T/err")
check "auto falls back to plink when OpenSSH fails" [ "$out" = fellback ]
check "and says so" grep -q "using plink" "$T/err"
s=$EPOCHREALTIME; out=$("$CAR" sh 'echo again' 2>&1); e=$EPOCHREALTIME
check "after a failure auto goes straight to plink" [ "$out" = again ]
out=$(CAR_SSH=openssh "$CAR" sh 'echo x' 2>&1); rc=$?
check "CAR_SSH=openssh fails loudly instead" [ $rc != 0 ]
mv "$T/kh.saved" "$T/local/car_known_hosts"

# 6. close (also clears the failure marker, so the next call opens a shared connection again).
"$CAR" close >/dev/null; "$CAR" sh true
check "after close OpenSSH is tried again" bash -c "'$CAR' status | grep -q 'OpenSSH connection up'"
"$CAR" close >/dev/null
check "close drops the shared connection" bash -c "! '$CAR' status | grep -q 'OpenSSH connection up'"

[ $fails = 0 ] && echo "test_car: all passed" || echo "test_car: $fails failure(s)"
exit $((fails > 0))
