# OMAP-namespace recon — run on hu-omap via: ssh ... 'on -f /net/hu-omap /bin/sh' < this
# bare-minimum commands only (no head/find/date on the HU)

echo "===== OMAP IDENTITY ====="
uname -a
hostname

echo "===== OMAP NETWORK INTERFACES (wlan AP?) ====="
ifconfig

echo "===== OMAP PROCESSES — carplay/wifi/bt/AA ====="
pidin | grep -iE "carplay|iap|aas_|wifi|wlan|hotspot|bt|blue|siri|telephony|connm|apple|miracast|airplay|dhcp|dnsmasq|hostap"

echo "===== ALL OMAP PROCESSES ====="
pidin

echo "===== /opt TREE ====="
ls /opt/
ls /opt/hmi/ 2>/dev/null

echo "===== HMI / PLUGIN FRAMEWORK ====="
ls /opt/hmi/ID5/ 2>/dev/null
ls /opt/hmi/ID5/data/ 2>/dev/null
ls /opt/hmi/ID5/data/ro/ 2>/dev/null
ls /opt/hmi/ID5/data/ro/Plugins/ 2>/dev/null
ls /opt/hmi/ID6/ 2>/dev/null

echo "===== FSC / FEATURE CODES (CarPlay licensed?) ====="
ls /data/sys/fsc/ 2>/dev/null
ls /var/opt/sys/fsc/ 2>/dev/null
ls /data/persistency/ 2>/dev/null
ls /var/opt/persistency/ 2>/dev/null
ls /fs/ 2>/dev/null

echo "===== ISTEP / VERSION CANDIDATES ====="
for f in /etc/*ver* /etc/*istep* /etc/*step* /opt/sys/etc/*ver* \
         /var/opt/sys/*ver* /var/opt/sys/*istep* /data/sys/*ver* \
         /data/sys/*istep* /mnt/*ver* ; do
  if [ -f "$f" ]; then echo "--- $f"; cat "$f"; fi
done

echo "===== CARPLAY / WLAN CONFIG FILES ====="
for f in /etc/*carplay* /etc/*wlan* /etc/*wifi* /opt/car/etc/* \
         /var/opt/car/* /data/car/* ; do
  if [ -f "$f" ]; then echo "--- $f"; cat "$f"; fi
done

echo "===== USB / STORAGE MOUNTS ====="
ls /fs 2>/dev/null
mount | grep -iE "fs/|mnt|usb|sda|hd" 2>/dev/null

echo "===== RECON2 COMPLETE ====="
