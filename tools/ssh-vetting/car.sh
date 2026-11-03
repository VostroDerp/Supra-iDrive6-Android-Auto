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
#   car.sh pull <remote> [local]    copy a file from the HU (default: recon/<basename>)
#   car.sh login                    interactive shell
#   car.sh status                   is the HU reachable, is knockd / sshd answering
#
# sh/push/pull/login knock first when :3804 is closed.
# Env: HU_IP (169.254.199.119), SSH_PORT (3804), KNOCK_PORT (851), KNOCK_PPK (~/.ssh/nbt_knock.ppk),
#      KNOCK_KEY (~/.ssh/nbt_knock.pem, used by knock.py), PLINK (plink).
set -u

HU_IP=${HU_IP:-169.254.199.119}
SSH_PORT=${SSH_PORT:-3804}
KNOCK_PORT=${KNOCK_PORT:-851}
PPK=${KNOCK_PPK:-$HOME/.ssh/nbt_knock.ppk}
PLINK=${PLINK:-plink}
HERE="$(cd "$(dirname "$0")" && pwd)"

die() { echo "car.sh: $*" >&2; exit 1; }
port_open() { timeout 3 bash -c ": </dev/tcp/$HU_IP/$1" 2>/dev/null; }
run() { "$PLINK" -ssh -batch -i "$PPK" -P "$SSH_PORT" "root@$HU_IP" "$@"; }

knock() {
    port_open "$SSH_PORT" && return 0
    port_open "$KNOCK_PORT" || die "neither sshd :$SSH_PORT nor knockd :$KNOCK_PORT answers on $HU_IP" \
        "(ENET cable / laptop on 169.254.x? HU awake? knockd started by the boot wrapper?)"
    python3 "$HERE/knock/knock.py" "$HU_IP" "$KNOCK_PORT" || die "knock failed"
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        port_open "$SSH_PORT" && return 0
        sleep 1
    done
    die "knock accepted but sshd :$SSH_PORT did not come up within 10 s"
}

# Remote file size in bytes from `ls -l` (the HU has no stat / wc).
remote_size() { run "ls -l '$1'" 2>/dev/null | awk 'NR==1 {print $5}'; }

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
        dst=${2:-$HERE/recon/$(basename "$1")}
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
    login)
        knock && exec "$PLINK" -ssh -t -i "$PPK" -P "$SSH_PORT" "root@$HU_IP"
        ;;
    status)
        ping -c 1 -W 1 "$HU_IP" >/dev/null 2>&1 && echo "ping      $HU_IP: up" || echo "ping      $HU_IP: no answer"
        port_open "$KNOCK_PORT" && echo "knockd    :$KNOCK_PORT open" || echo "knockd    :$KNOCK_PORT closed"
        port_open "$SSH_PORT" && echo "sshd      :$SSH_PORT open" || echo "sshd      :$SSH_PORT closed (knock first)"
        ;;
    *)
        sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
        [ -z "$cmd" ] || exit 1
        ;;
esac
