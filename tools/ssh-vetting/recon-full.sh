#!/bin/bash
# recon-full.sh — full HU state dump for offline analysis, via OUR ssh gate
# (knockd -> sshd:3804, key-only). No bavarian/unlock involvement.
# Output: tools/ssh-vetting/recon/recon-<date>.txt (gitignored).
cd "$(dirname "$0")" || exit 1
HU=${HU_IP:-169.254.199.119}
OUT=recon/recon-$(date +%Y%m%d-%H%M).txt
mkdir -p recon
SSH="plink -ssh -batch -i $HOME/.ssh/nbt_knock.ppk -P 3804 root@$HU"
r() { echo "===== $*"; $SSH "$*" 2>&1 | sed -n '1,60p'; echo; }
{
echo "##### recon-full $(date) via our knockd gate #####"; echo
r 'hostname; uname -a'
r 'ifconfig -a'
r 'pfctl -s rules'
r 'pfctl -a filter_OBD -s rules'
r 'netstat -an | grep LISTEN'
r 'pidin'
r 'pidin ar'
r 'mount'
r 'ls /fs/'
r 'ls /fs/usb* 2>/dev/null'
r 'ls /dev/'
r 'ls -l /dev/serusb* /dev/usb* /dev/android* /dev/oap* 2>/dev/null'
r 'pidin ar | grep -iE "usb|aoap|diag|qconn|telnet|inetd|cinemo|nme|starter|syset"'
r 'ls /opt/sys/bin | grep -iE "diag|nbt|ssh"'
r 'ls /fs/sda0/opt/sys/bin 2>/dev/null | grep -iE "diag|nbt"'
r 'ls /opt/mm/bin | grep -iE "cinemo|nme"; ls /opt/mm/lib | grep -iE "cinemo|nme"'
r 'cat /etc/ssh/sshd_config'
r 'cat /etc/inetd.conf'
r 'sloginfo | grep -iE "usb|aoap|android|oap" | sed -n "1,40p"'
# pidin fds dropped: too slow on HU, no socket-port mapping anyway
} > "$OUT" 2>&1
echo "wrote $OUT ($(wc -l <"$OUT") lines)"
