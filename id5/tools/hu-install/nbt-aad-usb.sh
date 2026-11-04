#!/bin/sh
# nbt-aad-usb.sh — NbtAad app entry on the ID5 for USB Android Auto. Installed as /opt/aa/nbt-aad
# (the path the iDrive app spawns, at autostart and from the menu entry). Replaces the old BM3-dongle
# wrapper (tools/legacy/nbt-aad-bm3.sh), whose dwc0 DHCP wait never ends without the dongle (2026-11-03).
#
# It runs /var/opt/aa/nbt-aad-base: a copy of whichever daemon build is the current base. Changing the
# base is one copy on /var, no remount: cp <build> nbt-aad-base.new && mv nbt-aad-base.new nbt-aad-base
#
# Order matters: knockd first, so SSH never depends on anything after it.
# The app passes the env from /opt/aa/nbt-aad.env plus the HMI's own environment; this script
# drops the parts that don't belong in USB mode and sets the ones runs 13-15 were tested with.
#
# Install (from the repo root; /opt is on /fs/sda0, read-only until remounted):
#   car.sh sh 'cp /opt/aa/nbt-aad /var/opt/aa/nbt-aad.wrapper.v1'
#   car.sh push id5/tools/hu-install/nbt-aad-usb.sh /var/opt/aa/nbt-aad-usb.sh
#   car.sh sh 'mount -uw /fs/sda0 && cp /var/opt/aa/nbt-aad-usb.sh /opt/aa/nbt-aad && chmod 755 /opt/aa/nbt-aad; mount -ur /fs/sda0'
# Back out: the same with /var/opt/aa/nbt-aad.wrapper.v1.

# The HMI's LD_PRELOAD (libEGL_X/libGLESv2/libKD_X) kills mm-renderer and the daemon at start.
unset LD_PRELOAD
AA=/var/opt/aa
CP=/fs/sda0/opt/conn
DAEMON=${NBT_DAEMON:-$AA/nbt-aad-base}
running() { pidin ar | grep -v grep | grep -q "$1"; }

# 1. knockd (SSH gate), unconditionally, unless one already runs (the app may start us twice).
#    It binds :851 before it forks, so a failed start exits non-zero and is retried.
if [ -x $AA/knockd ] && ! running "$AA/knockd "; then
  ( for i in 1 2 3 4 5; do $AA/knockd $AA/knock.conf >>$AA/knock.log 2>&1 </dev/null && break; sleep 30; done ) &
fi

# 2. Keep the daemon log bounded: /var is a 500 MB partition iDrive also uses. One old copy kept.
LOG=${NBT_LOG_FILE:-$AA/nbt.log}
if [ -f "$LOG" ]; then
  set -- $(ls -l "$LOG")              # no stat/wc/awk needed: field 5 is the size
  [ "${5:-0}" -gt 8000000 ] && mv -f "$LOG" "$LOG.1"
fi
export NBT_LOG_FILE=$LOG

# 3. mm-renderer, if the head unit didn't start one (it only does with CarPlay coded).
running mm-renderer || {
  MM_RENDERER_CONFIG=$AA/mm-renderer.conf AOI_CONFIG=$AA/aoi \
  LD_LIBRARY_PATH=$CP/lib/carplay:/opt/sys/lib \
  $CP/bin/carplay/mm-renderer -e -v -f -k </dev/null >$AA/mmr.log 2>&1 &
}

# 4. The daemon, USB as tested in runs 13-15. The BM3 hotspot settings from nbt-aad.env go:
#    with them the wireless side would aim at the dongle network (dwc0, its iap2.cfg).
unset NBT_AP_CFG NBT_AP_IF NBT_AP_IP NBT_AP_BSSID
export NBT_USB=1 NBT_USB_DIRECT=1 NBT_SPP_REREG_S=0
export NBT_VEHICLE=${NBT_VEHICLE:-bmw}
export AOI_CONFIG=$AA/aoi/ MM_RENDERER_CONFIG=$AA/mm-renderer.conf
export LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib:/fs/sda0/opt/mm/lib:$CP/lib/carplay:/opt/sys/lib
if [ ! -x "$DAEMON" ]; then
  echo "nbt-aad-usb.sh: $DAEMON missing or not executable" >>"$LOG"
  exit 1
fi
exec "$DAEMON"
