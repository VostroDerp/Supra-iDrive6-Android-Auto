#!/bin/bash
# beacon-watch.sh — is the head unit's AP on air? Objective answer from the laptop's own Wi-Fi radio.
#
# Rescans every INTERVAL s (default 5) and logs every SSID/BSSID it sees, then prints a line whenever a network
# matching PATTERN appears or disappears. Run it for the whole Wi-Fi block; correlate with hu-watch.sh.
#   beacon-watch.sh [PATTERN] [INTERVAL]        PATTERN = extended regex on SSID or BSSID (default: NBTAA|DIRECT-)
# Log: id5/local/wifi/beacon-<time>.log (all networks, every scan). Stop with Ctrl-C.
# The laptop can stay connected to another network (e.g. the phone hotspot) while it scans.
set -u
PAT=${1:-NBTAA|DIRECT-}; INT=${2:-5}
HERE="$(cd "$(dirname "$0")" && pwd)"; OUT="$HERE/../../local/wifi"; mkdir -p "$OUT"
LOG="$OUT/beacon-$(date +%Y%m%d-%H%M%S).log"
command -v nmcli >/dev/null || { echo "needs nmcli (NetworkManager)"; exit 1; }
echo "beacon-watch: pattern '$PAT', every ${INT}s, log $LOG"
prev=""
while :; do
    ts=$(date +%H:%M:%S)
    scan=$(nmcli -t -f SSID,BSSID,CHAN,FREQ,SIGNAL,SECURITY dev wifi list --rescan yes 2>/dev/null)
    printf '%s\n' "$scan" | sed "s/^/$ts /" >> "$LOG"
    hit=$(printf '%s\n' "$scan" | grep -E -- "$PAT" | sort)
    if [ "$hit" != "$prev" ]; then
        if [ -n "$hit" ]; then printf '%s\n' "$hit" | sed "s/^/$ts SEEN  /"; else echo "$ts GONE  (no network matching '$PAT')"; fi
        prev=$hit
    fi
    sleep "$INT"
done
