#!/bin/bash
# car.sh — head-unit access over ENET through the knockd gate (replaces hu.sh for daily use).
#
# Path (proven 2026-11-02): laptop on ENET -> knock.py signs knockd's challenge (dwc0:851)
# -> knockd spawns sshd on :3804 (key-only; with knockd v1 until the HU reboots, with v2 for the
# knock window, OPEN_S in knock.conf) -> one OpenSSH connection, shared by every later call for 15 min
# (ControlMaster), with ~/.ssh/nbt_knock.pem; plink with ~/.ssh/nbt_knock.ppk if OpenSSH can't connect.
# The stock way in over ENET is `car.sh fallback`; legacy/hu.sh is only for the old BM3 dongle path.
#
# Speed (2026-11-06): every plink call cost ~15.5 s, even `true`, with no knock involved: the time is spent
# setting up each SSH connection (suspect: sshd's reverse DNS lookup of the laptop, UseDNS, which old OpenSSH
# does by default; `car.sh ssh-diag` shows where a fresh connection waits). The shared connection pays that once.
#
# Usage:
#   car.sh knock                    knock (only if :3804 is not already open)
#   car.sh sh '<remote cmd>'        run a command on hu-omap (QNX ksh: no head/env/nc/scp)
#   car.sh push <local> <remote>    copy a file to the HU (size-checked; +x kept)
#   car.sh pull <remote> [local]    copy a file from the HU (default: id5/local/recon/<basename>)
#   car.sh login                    interactive shell
#   car.sh status                   is the HU reachable, is knockd / sshd answering, is a shared connection up
#   car.sh close                    drop the shared OpenSSH connection
#   car.sh forward <lport> <rport>  laptop 127.0.0.1:<lport> -> the HU's 127.0.0.1:<rport>, over the shared
#                                   connection (OpenSSH only); `car.sh unforward <lport> <rport>` stops it
#   car.sh ssh-diag                 time one fresh connection step by step (where the ~15 s goes)
#   car.sh collect <N>              after test run N: id5/local/recon/run-N/ gets the daemon logs (nbt-usbN*.log),
#                                   pidin / fds / dev nodes / sloginfo, binary sizes, phone logcat (if adb
#                                   sees a device), then prints the stage summary
#   car.sh stages <log>             stage summary of a daemon log (how far a USB session got)
#   car.sh fallback status|probe|unlock|sh '<cmd>'|login
#                                   the STOCK way in, over the ENET cable (no BM3 dongle, no hu.sh): the online
#                                   FDAC unlock via the diag node (DIAG_IP, jacinto, :6801; needs laptop internet),
#                                   then the stock sshd on :22 with the generic password. Stock path only: our
#                                   knockd/sshd are not involved. Commands land on hu-omap (via QNET from jacinto).
#   car.sh run <N> <bin> [K=V ...]  one USB test run: kill leftovers, clear logcat, launch /var/opt/aa/<bin>
#                                   with the runbook env (+ K=V extras), ask for the phone, follow the log
#                                   until media frames / session end / RUN_WAIT_S (90), then collect N.
#                                   RUN_MMR_CONF=0 leaves out AOI_CONFIG + MM_RENDERER_CONFIG (PR #65 run).
#
# sh/push/pull/login knock first when :3804 is closed (not while a shared connection is up).
# Env: HU_IP (169.254.199.119), SSH_PORT (3804), KNOCK_PORT (851), KNOCK_PPK (~/.ssh/nbt_knock.ppk),
#      KNOCK_KEY (~/.ssh/nbt_knock.pem, used by knock.py and OpenSSH), PLINK (plink), ID5_LOCAL (id5/local),
#      USB_TREE (the -usb worktree, for build sizes in collect),
#      CAR_SSH (auto: OpenSSH shared connection, plink if that fails | openssh | plink), CAR_USER (root),
#      CAR_KNOWN_HOSTS (id5/local/car_known_hosts: made once from plink's saved host key for the HU).
set -u

HU_IP=${HU_IP:-169.254.199.119}
SSH_PORT=${SSH_PORT:-3804}
KNOCK_PORT=${KNOCK_PORT:-851}
PPK=${KNOCK_PPK:-$HOME/.ssh/nbt_knock.ppk}
PLINK=${PLINK:-plink}
KEY=${KNOCK_KEY:-$HOME/.ssh/nbt_knock.pem}
USER_=${CAR_USER:-root}
CAR_SSH=${CAR_SSH:-auto}
HERE="$(cd "$(dirname "$0")" && pwd)"
ID5="$(cd "$HERE/../.." && pwd)"                 # id5/ (repo root/id5)
LOCAL=${ID5_LOCAL:-$ID5/local}                     # gitignored: recon output, pulls
KNOWN=${CAR_KNOWN_HOSTS:-$LOCAL/car_known_hosts}
CTL=${XDG_RUNTIME_DIR:-/tmp}/car-sh-$USER_@$HU_IP-$SSH_PORT.sock

die() { echo "car.sh: $*" >&2; exit 1; }
# port_open [host] port (host defaults to HU_IP)
port_open() { local h=$HU_IP; [ $# -gt 1 ] && { h=$1; shift; }; timeout 3 bash -c ": </dev/tcp/$h/$1" 2>/dev/null; }

# --- SSH transport -----------------------------------------------------------------------------------------
# The HU's sshd is old: it signs with ssh-rsa (SHA-1) and may only offer SHA-1 key exchange / CBC ciphers, which
# OpenSSH 8.8+ turns off by default. "+" adds them back for this host only.
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=8 -o IdentitiesOnly=yes -i "$KEY" -p "$SSH_PORT"
          -o UserKnownHostsFile="$KNOWN" -o StrictHostKeyChecking=yes -o GlobalKnownHostsFile=/dev/null
          -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa
          -o KexAlgorithms=+diffie-hellman-group14-sha1,diffie-hellman-group-exchange-sha1,diffie-hellman-group1-sha1
          -o Ciphers=+aes128-cbc,aes256-cbc,3des-cbc -o MACs=+hmac-sha1
          -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o LogLevel=ERROR)

# known_hosts for OpenSSH from plink's cache (~/.putty/sshhostkeys: "rsa2@PORT:HOST 0xE,0xN"), so the HU's host key
# stays the one first seen with plink instead of being trusted anew.
make_known_hosts() {
    [ -s "$KNOWN" ] && return 0
    mkdir -p "$(dirname "$KNOWN")"
    python3 - "$HOME/.putty/sshhostkeys" "$HU_IP" "$SSH_PORT" "$KNOWN" <<'PY' || return 1
import base64, struct, sys
cache, host, port, out = sys.argv[1:5]
def mpint(v):
    b = v.to_bytes((v.bit_length() + 8) // 8, "big")   # one spare byte keeps the sign bit clear
    return struct.pack(">I", len(b)) + b
for line in open(cache):
    k, _, val = line.strip().partition(" ")
    if k == "rsa2@%s:%s" % (port, host):
        e, n = (int(x, 16) for x in val.split(","))
        blob = struct.pack(">I", 7) + b"ssh-rsa" + mpint(e) + mpint(n)
        open(out, "w").write("[%s]:%s ssh-rsa %s\n" % (host, port, base64.b64encode(blob).decode()))
        sys.exit(0)
sys.exit(1)
PY
}

master_up() { [ -S "$CTL" ] && ssh -o ControlPath="$CTL" -O check "$USER_@$HU_IP" >/dev/null 2>&1; }

# Start the shared connection: authenticates once, then waits in the background for 15 min of idle (ControlPersist).
# Its own stdio goes to /dev/null so a $(car.sh ...) never waits on it. Errors land in $LOCAL/car-ssh.err.
start_master() {
    make_known_hosts || { echo "car.sh: no saved host key for $HU_IP:$SSH_PORT in ~/.putty/sshhostkeys" > "$LOCAL/car-ssh.err"; return 1; }
    ssh "${SSH_OPTS[@]}" -o ControlMaster=yes -o ControlPath="$CTL" -o ControlPersist=900 -fN "$USER_@$HU_IP" \
        </dev/null >/dev/null 2>"$LOCAL/car-ssh.err" && master_up
}

# TRANSPORT = the argv that runs a remote command (set once per car.sh process, by pick_transport).
TRANSPORT=()
# After OpenSSH failed in auto mode, use plink for 15 min without trying again (a failed try can cost as much as a
# plink call). `car.sh close` clears it.
openssh_failed_recently() {
    [ "$CAR_SSH" = auto ] && [ -f "$CTL.failed" ] && [ $(( $(date +%s) - $(stat -c %Y "$CTL.failed") )) -lt 900 ]
}
pick_transport() {
    [ ${#TRANSPORT[@]} -gt 0 ] && return 0
    local tried=0
    if [ "$CAR_SSH" != plink ] && command -v ssh >/dev/null && ! openssh_failed_recently; then
        if master_up || { tried=1; start_master; }; then
            rm -f "$CTL.failed"
            TRANSPORT=(ssh "${SSH_OPTS[@]}" -o ControlMaster=no -o ControlPath="$CTL" "$USER_@$HU_IP")
            return 0
        fi
        touch "$CTL.failed"
    fi
    [ "$CAR_SSH" = openssh ] && die "OpenSSH could not connect: $(tail -n 3 "$LOCAL/car-ssh.err" 2>/dev/null)"
    [ "$CAR_SSH" = auto ] && [ $tried = 1 ] && echo "car.sh: OpenSSH could not connect" \
        "($(tail -n 1 "$LOCAL/car-ssh.err" 2>/dev/null)): using plink, ~15 s per call, for the next 15 min" \
        "(car.sh close tries OpenSSH again)" >&2
    TRANSPORT=("$PLINK" -ssh -batch -i "$PPK" -P "$SSH_PORT" "$USER_@$HU_IP")
}
run() { pick_transport && "${TRANSPORT[@]}" "$@"; }

knock() {
    master_up && return 0                          # a shared connection outlives the knock window
    port_open "$SSH_PORT" && return 0
    port_open "$KNOCK_PORT" || die "neither sshd :$SSH_PORT nor knockd :$KNOCK_PORT answers on $HU_IP" \
        "(ENET cable / laptop on 169.254.x? HU awake? knockd started by the boot wrapper?)"
    python3 "$ID5/tools/knock/knock.py" "$HU_IP" "$KNOCK_PORT" || die "knock failed"
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        port_open "$SSH_PORT" && return 0
        sleep 1
    done
    die "knock accepted but sshd :$SSH_PORT did not come up within 10 s"
}

# Remote file size in bytes from `ls -l` (the HU has no stat / wc).
remote_size() { run "ls -l '$1'" 2>/dev/null | awk 'NR==1 {print $5}'; }

# Session stages in order (PR #42 / #65 log strings). The three mm-renderer rows come from daemon start-up and are
# normally 0 (renderer already reachable). From `driver path claimed` on, the first 0 row is where the run stopped.
stages() {
    local log=$1 n label pat
    [ -f "$log" ] || die "no log $log"
    while IFS='|' read -r label pat; do
        n=$(grep -cE -- "$pat" "$log")
        printf '  %-28s %5s   %s\n' "$label" "$n" "$(grep -m1 -E -- "$pat" "$log" | cut -c1-110)"
    done <<'EOF'
mm-renderer waited for|mm-renderer not reachable yet
mm-renderer we started (#65)|\[MMR\] (Started|Adopted)
mm-renderer UNREACHABLE|mm-renderer still unreachable
driver path claimed|\[UsbDirect\] claimed /
claim NOT effective (race!)|claim not effective
io-usb connected|connected to io-usb
USB device seen|\[UsbDirect\] USB device
switch to accessory sent|switch to accessory mode sent
accessory enumerated|\[UsbDirect\] accessory [0-9a-f]{4}:
bridge open|open: starting Android Auto over USB
BT parked (vanilla)|phone on the USB cable
SPP register/deregister|SppUuid(Register|Deregister) response
VersionResponse|VersionResponse:
TLS handshake complete|TLS Handshake Complete
decrypt FAILURES|Failed to decrypt
media frames|Streaming Media Frame
phone ByeBye|ByeByeRequest from the phone
bridge closed|closed after .* bytes from the phone
EOF
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
    knock)
        knock && echo "sshd :$SSH_PORT open on $HU_IP"
        ;;
    sh)
        [ $# -ge 1 ] || die "usage: car.sh sh '<remote cmd>'"
        knock && run "$*"
        ;;
    push)
        [ $# -eq 2 ] || die "usage: car.sh push <local> <remote>"
        [ -f "$1" ] || die "no local file $1"
        knock || exit 1
        x=""; [ -x "$1" ] && x="chmod +x '$2' && "
        # One call: copy, keep +x, and report the size (cat reads stdin to EOF before the rest runs).
        out=$(run "cat > '$2' && ${x}ls -l '$2'" < "$1") || die "push failed"
        got=$(echo "$out" | awk 'NR==1 {print $5}')
        want=$(stat -c %s "$1")
        [ "$want" = "$got" ] || die "size mismatch: local $want, remote ${got:-?} ($2)"
        echo "pushed $1 -> $2 ($got bytes)"
        ;;
    pull)
        [ $# -ge 1 ] && [ $# -le 2 ] || die "usage: car.sh pull <remote> [local]"
        dst=${2:-$LOCAL/recon/$(basename "$1")}
        mkdir -p "$(dirname "$dst")"
        knock || exit 1
        want=$(remote_size "$1")
        [ -n "$want" ] || die "no remote file $1"
        run "cat '$1'" > "$dst" || die "pull failed"
        got=$(stat -c %s "$dst")
        # A log that is still being written can grow between ls and cat: only a short copy is an error.
        [ "$got" -ge "$want" ] || die "short copy: remote $want, local $got ($dst)"
        echo "pulled $1 -> $dst ($got bytes)"
        ;;
    collect)
        [ $# -eq 1 ] || die "usage: car.sh collect <run number>"
        N=$1; out=$LOCAL/recon/run-$N
        [ -e "$out" ] && die "$out exists (pick a new run number)"
        mkdir -p "$out"; knock || exit 1
        {
            echo "run $N collected $(date -Is) from $HU_IP"
            echo "HU date: $(run date 2>/dev/null)"
            echo "--- HU binaries (match sizes to the local builds below)"
            run 'ls -l /var/opt/aa/nbt-aad*' 2>&1
            echo "--- local builds"
            ls -l "${USB_TREE:-$ID5/../../Supra-iDrive6-Android-Auto-usb}"/milestone-7/nbt-aad-usb* 2>/dev/null
        } > "$out/meta.txt"
        for f in nbt-usb$N.log nbt-usb$N-stdout.log; do
            if [ -n "$(remote_size "/var/opt/aa/$f")" ]; then
                run "cat '/var/opt/aa/$f'" > "$out/$f"
            else
                echo "  (no /var/opt/aa/$f on the HU)"
            fi
        done
        run 'pidin ar' > "$out/pidin-ar.txt" 2>&1
        # pidin fds can block on a busy resource manager (run 13): cap it.
        pick_transport
        timeout 60 "${TRANSPORT[@]}" 'pidin fds' > "$out/pidin-fds.txt" 2>&1 \
            || echo "(pidin fds stopped after 60 s)" >> "$out/pidin-fds.txt"
        run 'ls -l /dev/android* /dev/oap* /dev/btspp* 2>&1' > "$out/dev-nodes.txt" 2>&1
        run 'sloginfo' > "$out/sloginfo.txt" 2>&1
        if command -v adb >/dev/null && [ "$(adb get-state 2>/dev/null)" = device ]; then
            adb logcat -d -v threadtime > "$out/logcat.txt" 2>&1 && echo "  phone logcat: $(wc -l < "$out/logcat.txt") lines"
        else
            echo "  (no adb device: phone logcat not collected)"
        fi
        echo "collected into $out:"; ls -l "$out" | tail -n +2 | awk '{printf "  %8s  %s\n", $5, $9}'
        [ -f "$out/nbt-usb$N.log" ] && { echo "stages (nbt-usb$N.log):"; stages "$out/nbt-usb$N.log"; }
        ;;
    run)
        [ $# -ge 2 ] || die "usage: car.sh run <N> <bin on HU, e.g. nbt-aad-usb5> [KEY=VALUE ...]"
        N=$1; BIN=$2; shift 2
        case "$N" in *[!0-9]*|'') die "run number must be a number" ;; esac
        case "$BIN" in */*|*' '*|'') die "bin is a file name in /var/opt/aa, e.g. nbt-aad-usb5" ;; esac
        for kv in "$@"; do case "$kv" in [A-Z_]*=*) ;; *) die "extra env must be KEY=VALUE: $kv" ;; esac; done
        [ -e "$LOCAL/recon/run-$N" ] && die "$LOCAL/recon/run-$N exists (pick a new run number)"
        knock || exit 1
        AA=/var/opt/aa; LOG=$AA/nbt-usb$N.log
        [ -n "$(remote_size "$AA/$BIN")" ] || die "$AA/$BIN is not on the HU (car.sh push it first)"
        [ -n "$(remote_size "$LOG")" ] && die "$LOG already exists on the HU (pick a new run number)"
        echo "[run $N] killing leftover nbt-aad / devc-serusb-aoap"
        run 'for p in $(pidin | sed -n "s/^ *\([0-9]*\).*nbt-aad.*/\1/p"); do kill -9 $p; done; for p in $(pidin | sed -n "s/^ *\([0-9]*\).*serusb-aoap.*/\1/p"); do kill -9 $p; done' >/dev/null 2>&1
        if command -v adb >/dev/null && [ "$(adb get-state 2>/dev/null)" = device ]; then
            adb logcat -c && echo "[run $N] phone logcat cleared"
        else
            echo "[run $N] no adb device: the phone side will not be collected"
        fi
        MMR="AOI_CONFIG=$AA/aoi/ MM_RENDERER_CONFIG=$AA/mm-renderer.conf"
        [ "${RUN_MMR_CONF:-1}" = 0 ] && MMR=""
        ENVS="NBT_USB=1 NBT_USB_DIRECT=1 NBT_SPP_REREG_S=0 NBT_LOG_FILE=$LOG NBT_VERBOSE=1 $MMR"
        ENVS="$ENVS LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib:/fs/sda0/opt/mm/lib:/fs/sda0/opt/conn/lib/carplay:/opt/sys/lib $*"
        echo "[run $N] launching $AA/$BIN"
        echo "         env: $ENVS"
        # All three fds redirected so the non-interactive session can return while the daemon keeps running.
        run "$ENVS $AA/$BIN </dev/null >$AA/nbt-usb$N-stdout.log 2>&1 &" || die "launch failed"
        sleep 3
        run "pidin ar | grep -v grep | grep -q '$AA/$BIN'" || die "$BIN is not running 3 s after launch: see $AA/nbt-usb$N-stdout.log"
        echo "[run $N] $BIN is running. PLUG THE PHONE IN NOW. Following the log for up to ${RUN_WAIT_S:-90} s (Ctrl-C stops waiting)."
        # Count the milestones on the HU (grep -c per pattern) instead of copying the whole log every 3 s.
        POLL=""
        for p in 'connected to io-usb' 'switch to accessory mode sent' '\[UsbDirect\] accessory [0-9a-f][0-9a-f][0-9a-f][0-9a-f]:' \
                 'VersionResponse:' 'TLS Handshake Complete' 'Failed to decrypt' 'Streaming Media Frame' \
                 'ByeByeRequest from the phone' 'closed after'; do
            POLL="$POLL echo \"\$(grep -c -E '$p' '$LOG' 2>/dev/null) $p\";"
        done
        t=0; last=""
        while [ "$t" -lt "${RUN_WAIT_S:-90}" ]; do
            sleep 3; t=$((t + 3))
            st=$(run "$POLL" 2>/dev/null | awk '$1 > 0 {n = $1; $1 = ""; sub(/^ /, ""); gsub(/\\/, ""); printf "%s %s;", n, $0}')
            [ "$st" != "$last" ] && { echo "  ${t}s: ${st:-nothing yet}"; last=$st; }
            case "$st" in *"Streaming Media Frame"*) echo "[run $N] media frames: give it a moment, then check the screen"; sleep 5; break ;; esac
            case "$st" in *ByeByeRequest*|*"closed after"*) echo "[run $N] the session ended"; break ;; esac
        done
        "$0" collect "$N"
        ;;
    fallback)
        DIAG=${DIAG_IP:-169.254.199.99}          # jacinto: HSFZ 6801 (the diag target, ECU 0x63)
        PW=${STOCK_PW:-'ts&SK412'}               # upstream's published generic root password (install.md)
        UNLOCK="$ID5/tools/legacy/nbt-ssh-unlock.py"
        # plink without -batch, answering 'y' once to the unknown host key (the stock sshd's key is not ours).
        sp() { local ip=$1; shift; echo y | "$PLINK" -ssh -pw "$PW" -P 22 "root@$ip" "$@"; }
        stock_ip() { local ip; for ip in "$HU_IP" "$DIAG"; do port_open "$ip" 22 && { echo "$ip"; return 0; }; done; return 1; }
        sub=${1:-status}; [ $# -gt 0 ] && shift
        case "$sub" in
            status)
                port_open "$DIAG" 6801 && echo "diag      $DIAG:6801 open (unlock possible)" || echo "diag      $DIAG:6801 closed"
                for ip in "$HU_IP" "$DIAG"; do
                    port_open "$ip" 22 && echo "stock ssh $ip:22 open" || echo "stock ssh $ip:22 closed"
                done ;;
            probe)                               # read-only: fetch the challenge twice, no server involved
                python3 "$UNLOCK" "$DIAG" --probe ;;
            unlock)
                stock_ip >/dev/null && { echo "stock ssh already open on $(stock_ip):22"; exit 0; }
                port_open "$DIAG" 6801 || die "diag node $DIAG:6801 not reachable (ENET cable? DIAG_IP?)"
                python3 "$UNLOCK" "$DIAG" || die "unlock failed (laptop internet? relay up?)"
                for _ in $(seq 1 20); do stock_ip >/dev/null && break; sleep 1; done
                ip=$(stock_ip) || die "unlock reported done but no :22 on $HU_IP or $DIAG after 20 s"
                echo "stock ssh open on $ip:22 (until the HU reboots)" ;;
            sh)
                [ $# -ge 1 ] || die "usage: car.sh fallback sh '<remote cmd>'"
                ip=$(stock_ip) || die "no stock ssh on :22 (car.sh fallback unlock first)"
                if [ "$ip" = "$HU_IP" ]; then sp "$ip" "$*"
                else
                    case "$*" in *\'*) die "commands run through QNET on jacinto must not contain single quotes" ;; esac
                    sp "$ip" "on -f /net/hu-omap /bin/sh -c '$*'"
                fi ;;
            login)
                ip=$(stock_ip) || die "no stock ssh on :22 (car.sh fallback unlock first)"
                [ "$ip" = "$HU_IP" ] || echo "landed on jacinto ($ip): run 'on -f /net/hu-omap /bin/sh' for the omap side"
                exec "$PLINK" -ssh -t -pw "$PW" -P 22 "root@$ip" ;;
            *) die "usage: car.sh fallback status|probe|unlock|sh '<cmd>'|login" ;;
        esac
        ;;
    stages)
        [ $# -eq 1 ] || die "usage: car.sh stages <daemon log>"
        stages "$1"
        ;;
    login)
        knock || exit 1
        pick_transport
        if [ "${TRANSPORT[0]}" = ssh ]; then exec ssh "${SSH_OPTS[@]}" -t -o ControlMaster=no -o ControlPath="$CTL" "$USER_@$HU_IP"
        else exec "$PLINK" -ssh -t -i "$PPK" -P "$SSH_PORT" "$USER_@$HU_IP"; fi
        ;;
    status)
        ping -c 1 -W 1 "$HU_IP" >/dev/null 2>&1 && echo "ping      $HU_IP: up" || echo "ping      $HU_IP: no answer"
        port_open "$KNOCK_PORT" && echo "knockd    :$KNOCK_PORT open" || echo "knockd    :$KNOCK_PORT closed"
        port_open "$SSH_PORT" && echo "sshd      :$SSH_PORT open" || echo "sshd      :$SSH_PORT closed (knock first)"
        master_up && echo "shared    OpenSSH connection up ($CTL)" || echo "shared    no OpenSSH connection (the next call opens one)"
        ;;
    forward|unforward)
        [ $# -eq 2 ] || die "usage: car.sh $cmd <laptop port> <HU port>"
        knock || exit 1
        pick_transport
        [ "${TRANSPORT[0]}" = ssh ] && master_up || die "forward needs the shared OpenSSH connection (plink has none)"
        op=forward; [ "$cmd" = unforward ] && op=cancel
        ssh -o ControlPath="$CTL" -O "$op" -L "127.0.0.1:$1:127.0.0.1:$2" "$USER_@$HU_IP" \
            || die "ssh -O $op failed (port $1 in use on the laptop?)"
        [ "$op" = forward ] && echo "forwarding 127.0.0.1:$1 -> HU 127.0.0.1:$2" || echo "stopped 127.0.0.1:$1"
        ;;
    close)
        master_up && ssh -o ControlPath="$CTL" -O exit "$USER_@$HU_IP" 2>/dev/null; rm -f "$CTL.failed"
        echo "shared connection closed"
        ;;
    ssh-diag)
        # A fresh connection (not the shared one), timed per debug line: a long gap shows where the HU waits.
        knock || exit 1
        make_known_hosts || die "no saved host key for $HU_IP:$SSH_PORT in ~/.putty/sshhostkeys"
        t0=$EPOCHREALTIME
        ssh "${SSH_OPTS[@]}" -o LogLevel=DEBUG1 -o ControlPath=none "$USER_@$HU_IP" true 2>&1 | while IFS= read -r l; do
            printf '%7.2f  %s\n' "$(echo "$EPOCHREALTIME - $t0" | bc)" "$l"
        done | grep -v -E 'debug1: (Reading|configuration|/etc|Local version|compat_banner|kex: |SSH2_MSG_KEX|expecting|rekey|Remote: )'
        echo "total $(echo "$EPOCHREALTIME - $t0" | bc) s (a gap of ~5/10/15 s before 'Authenticated' = the HU's DNS lookup)"
        ;;
    *)
        sed -n '2,/^set -u/p' "$0" | grep '^#' | sed 's/^# \{0,1\}//'
        [ -z "$cmd" ] || exit 1
        ;;
esac
