#!/bin/bash
# car.sh — head-unit access over ENET through the knockd gate (replaces hu.sh for daily use).
#
# Path (proven 2026-11-02): laptop on ENET -> knock.py signs knockd's challenge (dwc0:851)
# -> knockd spawns sshd on :3804 (key-only; with knockd v1 until the HU reboots, with v2 for the
# knock window, OPEN_S in knock.conf) -> plink with
# ~/.ssh/nbt_knock.ppk. The stock way in over ENET is `car.sh fallback`; legacy/hu.sh is only for the old BM3 dongle path.
#
# Usage:
#   car.sh knock                    knock (only if :3804 is not already open)
#   car.sh sh '<remote cmd>'        run a command on hu-omap (QNX ksh: no head/env/nc/scp)
#   car.sh push <local> <remote>    copy a file to the HU (size-checked; +x kept)
#   car.sh pull <remote> [local]    copy a file from the HU (default: id5/local/recon/<basename>)
#   car.sh login                    interactive shell
#   car.sh status                   is the HU reachable, is knockd / sshd answering
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
# sh/push/pull/login knock first when :3804 is closed.
# Env: HU_IP (169.254.199.119), SSH_PORT (3804), KNOCK_PORT (851), KNOCK_PPK (~/.ssh/nbt_knock.ppk),
#      KNOCK_KEY (~/.ssh/nbt_knock.pem, used by knock.py), PLINK (plink), ID5_LOCAL (id5/local),
#      USB_TREE (the -usb worktree, for build sizes in collect).
set -u

HU_IP=${HU_IP:-169.254.199.119}
SSH_PORT=${SSH_PORT:-3804}
KNOCK_PORT=${KNOCK_PORT:-851}
PPK=${KNOCK_PPK:-$HOME/.ssh/nbt_knock.ppk}
PLINK=${PLINK:-plink}
HERE="$(cd "$(dirname "$0")" && pwd)"
ID5="$(cd "$HERE/../.." && pwd)"                 # id5/ (repo root/id5)
LOCAL=${ID5_LOCAL:-$ID5/local}                     # gitignored: recon output, pulls

die() { echo "car.sh: $*" >&2; exit 1; }
# port_open [host] port (host defaults to HU_IP)
port_open() { local h=$HU_IP; [ $# -gt 1 ] && { h=$1; shift; }; timeout 3 bash -c ": </dev/tcp/$h/$1" 2>/dev/null; }
run() { "$PLINK" -ssh -batch -i "$PPK" -P "$SSH_PORT" "root@$HU_IP" "$@"; }

knock() {
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
        run "cat > '$2'" < "$1" || die "push failed"
        want=$(stat -c %s "$1"); got=$(remote_size "$2")
        [ "$want" = "$got" ] || die "size mismatch: local $want, remote ${got:-?} ($2)"
        if [ -x "$1" ]; then run "chmod +x '$2'" || die "chmod failed"; fi
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
        timeout 60 "$PLINK" -ssh -batch -i "$PPK" -P "$SSH_PORT" "root@$HU_IP" 'pidin fds' > "$out/pidin-fds.txt" 2>&1 \
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
        t=0; last=""
        while [ "$t" -lt "${RUN_WAIT_S:-90}" ]; do
            sleep 3; t=$((t + 3))
            st=$(run "cat '$LOG'" 2>/dev/null | grep -oE 'connected to io-usb|switch to accessory mode sent|\[UsbDirect\] accessory [0-9a-f]{4}:|VersionResponse:|TLS Handshake Complete|Failed to decrypt|Streaming Media Frame|ByeByeRequest from the phone|closed after' | sort | uniq -c | tr -s ' ' | tr '\n' ';')
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
        knock && exec "$PLINK" -ssh -t -i "$PPK" -P "$SSH_PORT" "root@$HU_IP"
        ;;
    status)
        ping -c 1 -W 1 "$HU_IP" >/dev/null 2>&1 && echo "ping      $HU_IP: up" || echo "ping      $HU_IP: no answer"
        port_open "$KNOCK_PORT" && echo "knockd    :$KNOCK_PORT open" || echo "knockd    :$KNOCK_PORT closed"
        port_open "$SSH_PORT" && echo "sshd      :$SSH_PORT open" || echo "sshd      :$SSH_PORT closed (knock first)"
        ;;
    *)
        sed -n '2,/^set -u/p' "$0" | grep '^#' | sed 's/^# \{0,1\}//'
        [ -z "$cmd" ] || exit 1
        ;;
esac
