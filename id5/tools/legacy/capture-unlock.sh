#!/usr/bin/env sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#
# capture-unlock.sh - run the Bavarian SSH unlock once with full belt-and-braces capture, so it
# never has to be redone in the car. It produces three artifacts in id5/local/unlock/:
#   1. a raw pcap of the PLAINTEXT ENET/HSFZ leg (tcp 6801) - the ground-truth wire bytes both
#      directions, independent of our client's parsing (catches anything the client mis-frames).
#   2. the unlock session JSON (--save): challenge, our FD2E/FD2F/FDAC requests, AND the car's
#      responses (the 71 01 <rid> FD2F accept + FDAC status) and a full car_frames list.
#   3. the complete stderr relay log, teed (now both WSS directions: client->server and server->car).
#
# Read-only to the car except the unlock routines themselves (FD2E/FD2F/FDAC); nbt-ssh-unlock.py's
# own allowlist (check_car_frame) refuses anything else on the wire. The pcap needs sudo - it will
# prompt once for your password. If sudo/tcpdump is unavailable the unlock still runs without the pcap.
#
#   capture-unlock.sh                         car = 169.254.199.99 (jacinto, HSFZ 6801), prod relay
#   capture-unlock.sh 169.254.199.99          explicit car IP
#   capture-unlock.sh 169.254.199.99 ws://127.0.0.1:8443   via a local relay (adds --insecure)
set -eu

CAR="${1:-169.254.199.99}"
SERVER="${2:-}"
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../../local/unlock"
mkdir -p "$OUT"
STAMP=$(date +%Y%m%d-%H%M%S)
PCAP="$OUT/unlock-enet-$STAMP.pcapng"
JSON="$OUT/nbt-unlock-$STAMP.json"
LOG="$OUT/unlock-full-$STAMP.log"

printf 'car (HSFZ) : %s\npcap       : %s\njson       : %s\nlog        : %s\n\n' "$CAR" "$PCAP" "$JSON" "$LOG"

TD=""
if command -v tcpdump >/dev/null 2>&1; then
    echo "starting pcap on tcp port 6801 (sudo may prompt for your password)..."
    # -U flushes each packet so the capture survives an abrupt stop.
    sudo tcpdump -i any -U -w "$PCAP" "tcp port 6801" >/dev/null 2>&1 &
    TD=$!
    sleep 1
    if ! kill -0 "$TD" 2>/dev/null; then
        echo "WARNING: pcap did not start (sudo/tcpdump failed) - continuing without it." >&2
        TD=""
    fi
else
    echo "WARNING: tcpdump not found - continuing without the pcap." >&2
fi

# Always stop the pcap, however we exit.
stop_pcap() {
    [ -n "$TD" ] && sudo kill "$TD" 2>/dev/null || true
    [ -n "$TD" ] && wait "$TD" 2>/dev/null || true
}
trap stop_pcap EXIT INT TERM

SRVARG=""
[ -n "$SERVER" ] && SRVARG="--server $SERVER --insecure"

echo "running unlock..."
# shellcheck disable=SC2086
python3 "$HERE/nbt-ssh-unlock.py" "$CAR" $SRVARG --save "$JSON" 2>&1 | tee "$LOG"

echo
echo "stopping pcap, capture complete:"
[ -n "$TD" ] && echo "  pcap : $PCAP"
echo "  json : $JSON"
echo "  log  : $LOG"
