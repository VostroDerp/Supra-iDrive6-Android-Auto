#!/bin/bash
# hu.sh — one-shot HU access for the NBT Evo via the bm3 adapter bridge.
#
# Proven facts (do NOT re-derive):
#   * HU nodes sit at 192.168.0.11-18 and SHUFFLE every wake. .10 = adapter gw.
#   * sshd: OpenSSH_5.2 QNX — dies in libcrypto under modern OpenSSH. USE PLINK.
#   * Auth: root / ts&SK412  (password auth; askpass stub was /tmp/ap.sh)
#   * Unlock: if no node answers :22, run nbt-ssh-unlock.py <node-with-6801>.
#     sshd then appears on HU nodes (sometimes NOT the diag-target node).
#   * Remote shell is QNX ksh: NO head/printf/od/nohup/find. Use echo/ls/cat,
#     or pull files locally for analysis.
#   * /var/opt/aa/ persists across sleep; processes may or may not.
#
# Usage:
#   hu.sh scan                    — list live HU nodes, :22 status, hostnames
#   hu.sh unlock [ip]             — run the diag unlock (auto-picks a node with 6801)
#   hu.sh sh '<remote cmd>'       — run cmd on hu-omap (auto-scan+unlock first)
#   hu.sh push <local> <remote>   — pscp file to hu-omap
#   hu.sh pull <remote> <local>   — pscp file from hu-omap
#   hu.sh ip                      — print current hu-omap bridge IP

set -u
PW='ts&SK412'
NET=192.168.0
HERE="$(cd "$(dirname "$0")" && pwd)"
PLINK="plink -ssh -batch"
PSCP="pscp -batch"

hostip() {  # echoes hostkey-accepting pipeline prefix
  echo y
}

scan_nodes() {  # prints "ip ssh?" per pingable HU node
  for i in 11 12 13 14 15 16 17; do
    (
      ping -c 1 -W 1 "$NET.$i" >/dev/null 2>&1 || exit 0
      nc -z -w2 "$NET.$i" 22 >/dev/null 2>&1 && echo "$NET.$i ssh" || echo "$NET.$i"
    ) &
  done
  wait
}

omap_ip() {  # prints IP of node answering hostname 'hu-omap' (or first ssh node)
  for ip in $(scan_nodes | awk '/ssh/{print $1}'); do
    h=$(echo y | $PLINK root@"$ip" -pw "$PW" 'hostname' 2>/dev/null | tail -1)
    [ "$h" = "hu-omap" ] && { echo "$ip"; return 0; }
    [ -n "$h" ] && echo "$ip" && return 0   # single-node units: take what answers
  done
  return 1
}

unlock() {
  local target="${1:-}"
  if [ -z "$target" ]; then
    for i in 15 14 13 12 11; do
      ping -c 1 -W 1 "$NET.$i" >/dev/null 2>&1 &&
        nc -z -w2 "$NET.$i" 6801 >/dev/null 2>&1 && { target="$NET.$i"; break; }
    done
    [ -z "$target" ] && { echo "no diag (6801) node found" >&2; return 1; }
  fi
  echo "unlock via $target" >&2
  python3 "$HERE/nbt-ssh-unlock.py" "$target" 2>&1 | tail -6 >&2
  sleep 2
}

ensure() {  # resolves OMAP ip, unlocking once if needed
  local ip
  ip=$(omap_ip) && { echo "$ip"; return 0; }
  unlock || return 1
  ip=$(omap_ip) && { echo "$ip"; return 0; }
  echo "no ssh node after unlock" >&2; return 1
}

case "${1:-}" in
  scan)
    scan_nodes
    for ip in $(scan_nodes | awk '/ssh/{print $1}'); do
      echo y | $PLINK root@"$ip" -pw "$PW" 'hostname' 2>/dev/null | tail -1 | sed "s/^/$ip -> /"
    done
    ;;
  unlock) unlock "${2:-}" ;;
  ip)     ensure ;;
  sh)
    shift; ip=$(ensure) || exit 1
    echo y | $PLINK root@"$ip" -pw "$PW" "$*"
    ;;
  push)  # HU has no scp binary — stream via cat
    ip=$(ensure) || exit 1
    echo y | $PLINK root@"$ip" -pw "$PW" "cat > '$3'" < "$2"
    ;;
  pull)
    ip=$(ensure) || exit 1
    echo y | $PLINK root@"$ip" -pw "$PW" "cat '$2'" > "$3"
    ;;
  *)
    sed -n '2,20p' "$0"; exit 2 ;;
esac
