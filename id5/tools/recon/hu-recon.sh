#!/bin/sh
# NBT Evo read-only recon — runs on the head unit, writes nothing to the HU.
#
# Run it twice, once per node:
#   ssh root@169.254.199.99  -o HostKeyAlgorithms=+ssh-rsa -m hmac-sha1 'sh -s' < hu-recon.sh
#   (for the omap side: first enter `on -f /net/hu-omap/ /bin/sh` then `sh`)
# or copy to USB and:  sh /mnt/<usb>/hu-recon.sh | tee /mnt/<usb>/recon.txt
#
# Everything here is inspection only: ls/cat/find/ifconfig/pidin.

out() { echo; echo "===== $* ====="; }
try() { echo "--- $*"; "$@" 2>/dev/null || echo "(n/a)"; }

out "IDENTITY"
uname -a
date
echo "node check: /opt/hmi exists? $( [ -d /opt/hmi ] && echo yes || echo no )  (yes=omap namespace)"

out "ISTEP / SW VERSION CANDIDATES"
for f in /etc/version* /etc/system/version* /HBpersistence/*version* /var/opt/*version*; do
  [ -f "$f" ] && { echo "--- $f"; cat "$f"; }
done 2>/dev/null

out "FILESYSTEMS / FREE SPACE"
df -h 2>/dev/null
mount 2>/dev/null | grep -iE "sda|sd[0-9]|/opt|/var" | head -20

out "NETWORK INTERFACES (wlan AP check)"
ifconfig -a 2>/dev/null
ls /dev/wlan* 2>/dev/null
ls /net/ 2>/dev/null

out "PROCESSES — carplay / wifi / bt / AA-relevant"
pidin ar 2>/dev/null | grep -iE "carplay|iap|aas_|wifi|wlan|hotspot|bt|blue|siri|telephony|connm" 
pidin fm 2>/dev/null | grep -iE "carplay|hmi|app" | head -30

out "CARPLAY CONFIG"
ls -la /var/opt/conn/ 2>/dev/null
ls -la /var/opt/conn/carplay/ 2>/dev/null
for f in /var/opt/conn/carplay/*.cfg /var/opt/conn/*.cfg; do
  [ -f "$f" ] && { echo "--- $f"; cat "$f"; }
done 2>/dev/null
ls /dev/btspp* 2>/dev/null

out "FSC / FEATURE CODES (is CarPlay licensed?)"
find / -xdev \( -iname "*fsc*" -o -iname "*.cert" -o -iname "*sig*" \) 2>/dev/null | grep -viE "^/proc|lib.*\.so|\.h$|include" | head -40
ls -la /var/fsc /var/opt/fsc /HBpersistence/ 2>/dev/null | head -30

out "VO / FA FILES"
find / -xdev \( -iname "*fa*" -o -iname "*.fp*" -o -iname "*.afp" \) 2>/dev/null | grep -iE "fa|fp" | grep -viE "^/proc|^/usr/lib|\.so|\.h$" | head -30

out "PLUGIN FRAMEWORK"
ls /opt/hmi/ 2>/dev/null
find /opt/hmi -maxdepth 4 -type d 2>/dev/null | head -40
ls /opt/hmi/ID5/data/ro/Plugins/ 2>/dev/null
ls /opt/hmi/ID6/data/ro/Plugins/ 2>/dev/null
find /opt/hmi -name "*.xml" 2>/dev/null | head -20
find / -xdev -name "libhmi*" -o -name "libplugin*" -o -name "libeb*.so*" 2>/dev/null | head -20

out "EXISTING /opt CONTENT"
ls -la /opt/ /opt/aa 2>/dev/null

out "USB MOUNTS"
ls /mnt/ 2>/dev/null

echo; echo "===== RECON COMPLETE ====="
