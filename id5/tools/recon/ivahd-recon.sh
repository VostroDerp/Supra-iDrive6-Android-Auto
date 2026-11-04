#!/bin/sh
# ivahd-recon.sh: read-only recon for decoding Android Auto video on the IVA-HD directly (renderer option A,
# git show 934156c:id5/IMPROVEMENT-PLAN.md, F9; parked) instead of through mm-renderer. Runs on hu-omap (QNX ksh: no head/env/nc/scp).
#
#   car.sh push id5/tools/recon/ivahd-recon.sh /var/opt/aa/ivahd-recon.sh
#   car.sh sh 'sh /var/opt/aa/ivahd-recon.sh > /var/opt/aa/ivahd-recon.txt 2>&1'
#   car.sh pull /var/opt/aa/ivahd-recon.txt
#   then car.sh pull each file listed in section 8 (into id5/local/pulled/ivahd/).
#
# Best run while Android Auto video is on screen (section 5 then shows which libraries mm-renderer has loaded
# for decoding); otherwise right after boot, with the wrapper's idle mm-renderer.
# Changes nothing on the HU except copying the found libraries into /var/opt/aa/ivahd-pull/ for pulling.

OUT=/var/opt/aa/ivahd-pull
LIST=/var/opt/aa/ivahd-files.txt
ROOTS="/proc/boot /lib /usr/lib /opt /fs/sda0/opt /fs/sda0/lib /fs/sda0/usr /etc /fs/sda0/etc"
s() { echo; echo "=== $*"; }

s "0. date / uptime"
date; uptime 2>/dev/null

s "1. mm-renderer decoder plugins (CarPlay package)"
ls -l /fs/sda0/opt/conn/lib/carplay/mm /opt/conn/lib/carplay/mm 2>/dev/null

s "2. DCE / TILER / IPC / codec-engine libraries and IVA-HD (IPU) firmware"
: > $LIST
for r in $ROOTS; do
    find $r \( -name '*dce*' -o -name '*tiler*' -o -name '*memmgr*' -o -name '*rpmsg*' -o -name '*syslink*' \
        -o -name '*ipc*' -o -name 'libce*' -o -name '*xdc*' -o -name '*ivahd*' -o -name '*ducati*' \
        -o -name '*.xem3' -o -name '*ipu*' -o -name '*codec*' \) 2>/dev/null >> $LIST
done
sort -u $LIST | while read f; do ls -l "$f"; done

s "3. device nodes (decoder IPC, TILER, mdev, screen)"
ls -l /dev 2>/dev/null | grep -i -E 'rpmsg|dce|tiler|ipc|ivahd|syslink|ducati|mdev|screen|gf|omap|iva'
echo "--- /dev/name/local:"; ls /dev/name/local 2>/dev/null
echo "--- /dev/name/global:"; ls /dev/name/global 2>/dev/null

s "4. processes that may own the IVA-HD, IPC or the display"
pidin ar | grep -i -E 'ipc|rpmsg|dce|tiler|syslink|ducati|resmgr|mm-renderer|screen|LayerManager|devg|io-display|carplay|cinemo'

s "5. libraries mapped by mm-renderer and screen (which decoder stack is really used)"
for name in mm-renderer screen; do
    for p in $(pidin | sed -n "s/^ *\([0-9]*\) .*$name.*/\1/p"); do
        echo "--- $name pid $p"
        pidin -p $p mem 2>/dev/null | grep -E '\.so|\.xem3|dll' | sed 's/^ *//' | sort -u
    done
done

s "6. graphics / screen configuration (display count, pipelines, NV12 support)"
for r in /etc /fs/sda0/etc /opt/sys /fs/sda0/opt/sys /usr/lib/graphics; do
    find $r \( -name 'graphics*.conf' -o -name '*screen*.conf' -o -name 'display*.conf' \) 2>/dev/null
done | sort -u | while read f; do echo "--- $f"; cat "$f"; done
ls /usr/lib/graphics 2>/dev/null
echo "--- libscreen:"; find /lib /usr/lib /fs/sda0/opt -name 'libscreen*' 2>/dev/null

s "7. LayerManager screens / layers (shared with the cluster research, F10)"
/opt/sys/bin/LayerManagerControl get screens 2>&1
/opt/sys/bin/LayerManagerControl get scene 2>&1 | grep -i -E 'screen|layer|surface' | sort -u

s "8. copied for pulling ($OUT): car.sh pull $OUT/<name> id5/local/pulled/ivahd/<name>"
mkdir -p $OUT
for f in /fs/sda0/opt/conn/lib/carplay/mm/*ivahd* /fs/sda0/opt/conn/lib/carplay/mm/*screen_writer* \
         /fs/sda0/opt/conn/lib/carplay/mm/*mdev* $(sort -u $LIST | grep -E '\.so|\.xem3' | grep -i -E 'dce|tiler|memmgr|rpmsg|syslink|ipc|ivahd|ducati|xem3|libce|xdc'); do
    [ -f "$f" ] || continue
    cp "$f" $OUT/ 2>/dev/null && ls -l $OUT/$(basename "$f")
done
echo "done"
