#!/bin/bash
# hu-watch.sh — timeline of the head unit's Wi-Fi side while we experiment: which Wi-Fi processes run (with pids,
# so a respawn shows as a new pid) and the uap0 / wfd0 interface state. Prints only what changed.
#   hu-watch.sh [INTERVAL]      default 5 s. Log: id5/local/wifi/hu-<time>.log. Stop with Ctrl-C.
# Read-only on the HU (pidin, ifconfig). Uses car.sh (knock + key login).
set -u
INT=${1:-5}
HERE="$(cd "$(dirname "$0")" && pwd)"; CAR="$HERE/../car/car.sh"; OUT="$HERE/../../local/wifi"; mkdir -p "$OUT"
LOG="$OUT/hu-$(date +%Y%m%d-%H%M%S).log"
"$CAR" knock >/dev/null || exit 1
echo "hu-watch: every ${INT}s, log $LOG"
REMOTE='pidin -f "an" 2>/dev/null | grep -E "mwud|scp|Connectivity|wifidir|acpd|wpa|hostap|dnsmasq|dhcpd|Online|WlanDSI|io-pkt" | grep -v grep; for i in uap0 wfd0; do ifconfig $i 2>/dev/null | grep -E "flags|inet |status|ssid"; done'
prev=""
while :; do
    ts=$(date +%H:%M:%S)
    cur=$("$CAR" sh "$REMOTE" 2>&1 | tr -s ' ' | sort)
    { echo "== $ts"; printf '%s\n' "$cur"; } >> "$LOG"
    if [ "$cur" != "$prev" ]; then
        echo "== $ts changed:"
        diff <(printf '%s\n' "$prev") <(printf '%s\n' "$cur") | grep '^[<>]' | sed 's/^</  gone:/; s/^>/  now: /'
        prev=$cur
    fi
    sleep "$INT"
done
