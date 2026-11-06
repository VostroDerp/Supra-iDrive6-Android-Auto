#!/bin/bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#
# a4a_probe.sh — run A4aProbe.java (see its header for the options). Fetches the two libraries it needs from JitPack
# into id5/local/a4a-libs/ on first use, checks their SHA-256, and runs the probe with the JDK's source launcher
# (Java 11 or newer, no build step). The libraries are never committed.
#   IDriveConnectKit's Etch bindings (MIT, github.com/BimmerGestalt/IDriveConnectKit), the same AAIdrive uses
#   the Etch runtime it depends on (Apache 2.0)
#
#   a4a_probe.sh 127.0.0.1 14004            after `car.sh forward 14004 <port>`
#   a4a_probe.sh --fetch                    only fetch and check the libraries
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBS="${A4A_LIBS:-${ID5_LOCAL:-$HERE/../../local}/a4a-libs}"
BASE=https://jitpack.io/io/bimmergestalt/idriveconnectkit
JARS=(
    "idriveconnectkit-etch-0.8.jar $BASE/idriveconnectkit-etch/0.8/idriveconnectkit-etch-0.8.jar b278f7054ae45092dff10472fb324aca515ce749149e187904f1a1d53bd7d4dd"
    "etch-runtime-0.8.jar $BASE/etch-runtime/0.8/etch-runtime-0.8.jar c6c01f55c7dfbe6ad43560f7dbb317cc37333aa5e63bd3d1d3115390a2977356"
)

fetch() {
    mkdir -p "$LIBS"
    local entry name url sum
    for entry in "${JARS[@]}"; do
        read -r name url sum <<<"$entry"
        if [ ! -f "$LIBS/$name" ]; then
            curl -fsSL -m 180 -o "$LIBS/$name.part" "$url"
            mv "$LIBS/$name.part" "$LIBS/$name"
        fi
        if [ "$(sha256sum "$LIBS/$name" | cut -d' ' -f1)" != "$sum" ]; then
            echo "a4a_probe: $LIBS/$name does not match its SHA-256, removed. Run again to fetch it again." >&2
            rm -f "$LIBS/$name"
            exit 4
        fi
    done
}

command -v java >/dev/null || { echo "a4a_probe: needs java (11 or newer)" >&2; exit 2; }
fetch
[ "${1:-}" = "--fetch" ] && { echo "libraries ok in $LIBS"; exit 0; }
CP="$LIBS/idriveconnectkit-etch-0.8.jar:$LIBS/etch-runtime-0.8.jar"
exec java -cp "$CP" "$HERE/A4aProbe.java" "$@"
