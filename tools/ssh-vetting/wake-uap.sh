#!/bin/bash
# wake-uap.sh — one-command uap0 AP bring-up via pdebug DS-protocol injection.
# Run from laptop after ignition cycle. Usage: ./wake-uap.sh [omap_ip]
set -u
cd "$(dirname "$0")"
H="${1:-}"
[ -z "$H" ] && H=$(./hu.sh ip 2>/dev/null | tail -1)
H="${H:-192.168.0.17}"
echo "[*] OMAP: $H"

remote() { ./hu.sh sh "$1" | tail -n +1; }

# mwud_pub must be alive; pdebug:80 single-shot per target death — restart per inject
ensure() {
  remote 'pidin ar | grep -q mwud_pub || (chmod +x /var/opt/aa/mwud_pub; (/var/opt/aa/mwud_pub >/var/opt/aa/mwud_pub.log 2>&1 &)); pidin ar | grep -q "pdebug 80" || { slay lighttpd 2>/dev/null; (pdebug 80 &); }; sleep 1'
}
PID() { remote 'pidin ar' | awk '/mwud_pub/{print $1; exit}' | tr -d '\r'; }
FD()  { remote "pidin -p $1 fds" | awk '/udp/{gsub(/\r/,""); if ($0 !~ /9196/) {print $2; exit}}' | tr -d '\r'; }

inject() {
  local pid fd
  pid=$(PID); [ -z "$pid" ] && { echo "[!] mwud_pub gone"; return 1; }
  fd=$(FD "$pid"); fd="${fd:-4}"
  echo "[*] inject (pid=$pid fd=$fd): $*"
  python3 ./mwu-pdebug.py "$H" 80 "$pid" "$fd" 127.0.0.1 9196 "$@" 2>&1 \
    | grep -E "attach|mw |stop|run|after|restore|Error|error" || true
}

echo "[*] ensure mwud_pub + pdebug:80 (lighttpd paused)"
ensure; echo "    READY"
echo "[*] ap_init uap0 NBTAA-DIRECT"
inject module=mwpamod iface=uap0 cmd=ap_init ssid=NBTAA-DIRECT key=1234567890 auth=0020 encrypt=0008
sleep 1; ensure
echo "[*] ap_set_cfg"
inject module=mwpamod iface=uap0 cmd=ap_set_cfg bcast_ssid=1
sleep 1; ensure
echo "[*] ap_start"
inject module=mwpamod iface=uap0 cmd=ap_start
sleep 1
echo "[*] verify:"
remote 'ifconfig uap0 2>&1; echo ---; tail -30 /var/opt/aa/mwud_pub.log'
echo "[*] done — phone should see NBTAA-DIRECT / 1234567890"
