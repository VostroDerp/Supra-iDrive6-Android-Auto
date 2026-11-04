#!/bin/sh
# cluster-recon.sh: read-only recon for Android Auto turn arrows in the F36 6WA cluster (see upstream issue #52; history: git show 934156c:id5/IMPROVEMENT-PLAN.md, F10).
# Finds which HU processes, configs and MOST function blocks carry the native navigation's arrows to the KOMBI.
# Runs on hu-omap (QNX ksh: no head/env/nc/scp). Sends nothing on MOST or CAN.
#
# Start an iDrive route first (car parked is fine) so the cluster shows an arrow, then:
#   car.sh push id5/tools/recon/cluster-recon.sh /var/opt/aa/cluster-recon.sh
#   car.sh sh 'sh /var/opt/aa/cluster-recon.sh > /var/opt/aa/cluster-recon.txt 2>&1'
#   car.sh pull /var/opt/aa/cluster-recon.txt
# Section 2 greps binaries and can take a few minutes.

PAT='kombi|routeguid|maneuver|manoeuvre|arrowguid|turnarrow|ArrowData|GuidanceInfo|KombiNav|ICNav'
s() { echo; echo "=== $*"; }

s "0. date / uptime"
date; uptime 2>/dev/null

s "1. processes (nav, HMI, MOST, KOMBI)"
pidin ar | grep -i -E 'nav|hmi|most|kombi|guid|map|dsi'

s "2. binaries and libraries naming the KOMBI / route guidance (file list only)"
for r in /opt/sys/bin /opt/sys/lib /fs/sda0/opt/sys/bin /fs/sda0/opt/sys/lib /opt/nav /fs/sda0/opt/nav \
         /opt/hmi /fs/sda0/opt/hmi /opt/mm/bin /usr/lib /lib; do
    [ -d $r ] || continue
    find $r -type f \( -name '*.so*' -o -perm -u+x \) 2>/dev/null | while read f; do
        grep -l -i -E "$PAT" "$f" 2>/dev/null
    done
done | sort -u

s "3. config files naming the KOMBI / route guidance / MOST FBlocks"
for r in /etc /opt/sys/etc /fs/sda0/opt/sys/etc /opt/hmi /fs/sda0/opt/hmi /opt/nav /fs/sda0/opt/nav /var/opt; do
    [ -d $r ] || continue
    find $r -type f \( -name '*.xml' -o -name '*.cfg' -o -name '*.conf' -o -name '*.ini' -o -name '*.txt' \) 2>/dev/null |
        while read f; do grep -l -i -E "$PAT|fblock" "$f" 2>/dev/null; done
done | sort -u

s "4. matching lines in those configs (first 20 per file)"
for r in /etc /opt/sys/etc /fs/sda0/opt/sys/etc /opt/hmi /fs/sda0/opt/hmi /opt/nav /fs/sda0/opt/nav; do
    [ -d $r ] || continue
    find $r -type f \( -name '*.xml' -o -name '*.cfg' -o -name '*.conf' -o -name '*.ini' \) 2>/dev/null |
        while read f; do
            n=$(grep -c -i -E "$PAT" "$f" 2>/dev/null)
            [ "${n:-0}" -gt 0 ] && { echo "--- $f ($n)"; grep -i -E "$PAT" "$f" | sed -n 1,20p; }
        done
done

s "5. DSI / PPS services with nav or KOMBI names"
ls /pps/services 2>/dev/null | grep -i -E 'nav|kombi|guid|route'
ls /dev/name/local 2>/dev/null | grep -i -E 'nav|kombi|guid|route|most|dsi'

s "6. system log lines during guidance"
sloginfo 2>/dev/null | grep -i -E "$PAT|most" | sed -n '1,200p'
echo "done"
