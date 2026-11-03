#!/bin/bash
# car.sh — head-unit access over ENET through the knockd gate (replaces hu.sh for daily use).
#
# Path (proven 2026-11-02): laptop on ENET -> knock.py signs knockd's challenge (dwc0:851)
# -> knockd spawns sshd on :3804 (key-only, stays up until the HU reboots) -> plink with
# ~/.ssh/nbt_knock.ppk. hu.sh remains the fallback for the bavarian / BM3 path only.
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
port_open() { timeout 3 bash -c ": </dev/tcp/$HU_IP/$1" 2>/dev/null; }
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
        run 'pidin fds' > "$out/pidin-fds.txt" 2>&1
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
