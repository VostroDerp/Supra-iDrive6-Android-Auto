#!/bin/bash
# recon-knock.sh — one-shot knock-daemon recon for the first car wake.
# Uses car.sh (knockd gate); output pulls go to id5/local/pulled/:
#     ./recon-knock.sh | tee recon-knock-$(date +%m%d).txt
# Collects everything the knockd deploy needs: firewall posture, dropbear
# details, alternate access (qconn), USB mount naming, verifier binary.
HERE="$(cd "$(dirname "$0")" && pwd)"
CAR="$HERE/../car/car.sh"; PULLED="$HERE/../../local/pulled"; mkdir -p "$PULLED"

r() { echo "=== $*"; "$CAR" sh "$*" 2>&1; echo; }

echo "##### knockd recon $(date) #####"; echo
r 'ifconfig -a | grep -A3 "dwc0\|uap0"'
r 'pfctl -s rules 2>/dev/null | head -40'
r 'cat /etc/pf.conf 2>/dev/null | head -40'
r 'netstat -an 2>/dev/null | grep -i listen | head -40 || sockstat 2>/dev/null | head -40'
r 'ls -l /fs/sda0/usr/sbin/*dropbear* /usr/sbin/*dropbear* /sbin/*dropbear* /fs/sda0/usr/bin/*ssh* 2>/dev/null'
r 'pidin args | grep -i "dropbear\|sshd\|inetd"'
r 'pidin env  | grep -iA4 dropbear'
r 'ls /etc/ssh* /etc/dropbear* /root/.ssh* /home/*/.ssh* /var/opt/*/ssh* 2>/dev/null'
r 'pidin | grep -i "qconn\|inetd"'
r 'pidin arg | grep -i "hsfz\|diag\|6801"'
r 'ls -l /opt/sys/bin/NbtDiagHuHighApp /fs/sda0/opt/sys/bin/NbtDiagHuHighApp 2>/dev/null'
r 'ls /fs/usb* 2>/dev/null'                 # USB mount naming — plug a stick!
r 'mount | grep -i "usb\|/fs/"'
r 'pidin arg | grep -i starter'             # what launches what (syset flags?)
r 'ls /var/opt/sys* /fs/sda0/opt/sys/*syset* 2>/dev/null'
r 'ls -l /var/opt/aa/ 2>/dev/null'          # current deploy state

echo "##### pulling verifier binary #####"
"$CAR" pull /opt/sys/bin/NbtDiagHuHighApp "$PULLED/NbtDiagHuHighApp" 2>&1 || \
"$CAR" pull /fs/sda0/opt/sys/bin/NbtDiagHuHighApp "$PULLED/NbtDiagHuHighApp" 2>&1 || \
echo "NbtDiagHuHighApp not at expected path — find it via pidin line above"
