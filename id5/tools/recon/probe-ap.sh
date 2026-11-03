# probe-ap.sh — direct-drive recon: iap2 cfg, uap0 beacon state, AP bring-up tools.
# run: ssh <opts> root@192.168.0.11 'on -f /net/hu-omap /bin/sh' < probe-ap.sh
# read-only: every command is ls/cat or an info query. nothing is started or written.

echo "==IAP2-FILES=="
for f in /var/opt/conn/carplay/iap2.cfg /fs/sda1/opt/conn/carplay/iap2.cfg \
         /var/opt/conn/carplay/iap2_Wifi.cfg /fs/sda1/opt/conn/carplay/iap2_Wifi.cfg; do
  if [ -f "$f" ]; then echo "--- $f"; cat "$f"; else echo "--- $f MISSING"; fi
done

echo "==TEMPLATE-DIR=="
for d in /fs/sda0/opt/conn/data/carplay /opt/conn/data/carplay /var/opt/conn/carplay; do
  echo "--- $d"; ls -l "$d" 2>/dev/null | sed -n "1,25p"
done
for f in /fs/sda0/opt/conn/data/carplay/*.cfg /opt/conn/data/carplay/*.cfg; do
  [ -f "$f" ] && echo "--- $f" && cat "$f"
done

echo "==UAP0-STATE=="
ifconfig uap0 2>/dev/null
echo "--- wfd0"; ifconfig wfd0 2>/dev/null | sed -n "1,12p"

echo "==AP-TOOLS=="
for t in uaputl uaputl.exe hostapd wpa_cli wpa_cli.exe iwconfig iwlist iw wlanconfig mlanutl; do
  for d in /bin /sbin /usr/bin /opt/conn/bin /opt/sys/bin; do
    [ -f "$d/$t" ] && echo "FOUND $d/$t"
  done
done

echo "==WLAN-DRIVER-ARGS=="
pidin | grep -aiE "uaputl|wpa|hostap|mwud|wifidir|wlan|marvell|sd8|bcm|dhd" | grep -v grep | sed -n "1,25p"

echo "==UAPUTL-INFO=="
if [ -f /opt/conn/bin/uaputl ]; then /opt/conn/bin/uaputl sys_info 2>&1 | sed -n "1,30p"; fi
if [ -f /bin/uaputl ]; then /bin/uaputl sys_info 2>&1 | sed -n "1,30p"; fi

echo "==SCP-IDENTITY=="
pidin | grep -a "t/conn/bin/scp" | sed -n "1,4p"
ls -l /opt/conn/bin/scp 2>/dev/null

echo "==CONNECTIVITY-STRINGS=="
grep -aoE "[A-Za-z0-9_]{6,}" /opt/conn/bin/Connectivity.cfg /opt/conn/etc/Connectivity.cfg 2>/dev/null \
  | grep -aiE "carplay|acpd|uap|wlan|wfd|wifi|enable|fsc|swt" | sed -n "1,40p"

echo "==STARTER-GATE=="
for f in /etc/starter.cfg /opt/sys/etc/starter*.cfg /opt/conn/etc/*.cfg; do
  [ -f "$f" ] && echo "--- $f" && grep -aiE "carplay|acpd|uap|wlan|conn|scp" "$f" | sed -n "1,20p"
done

echo "==PROBE-AP-DONE=="
