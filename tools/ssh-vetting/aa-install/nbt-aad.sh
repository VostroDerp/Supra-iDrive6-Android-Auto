#!/bin/sh
# NbtAad app entry: bring up the AA stack on the BM3 dongle network.
# Real daemon: /var/opt/aa/nbt-aad. Env comes from /opt/aa/nbt-aad.env
# (exported by the app); this script adds the dynamic dwc0 IP + mm-renderer.
#
# The app inherits the HMI's LD_PRELOAD (libEGL_X/libGLESv2/libKD_X) which is
# FATAL to child processes like mm-renderer ("Could not preload library").
# Strip it before starting anything — this is what caused the boot crashes.
unset LD_PRELOAD
AA=/var/opt/aa
CP=/fs/sda0/opt/conn

# knockd — cryptographically armed SSH gate (runs only if deployed+enabled:
# chmod +x to arm, -x to disarm). Listens regardless of which transport
# (BM3/ENET) is up since it binds 0.0.0.0 — needs no IP wait.
[ -x $AA/knockd ] && ( for i in 1 2 3 4 5; do $AA/knockd $AA/knock.conf >>$AA/knock.log 2>&1 && break; sleep 30; done ) &

# Wait for the dongle DHCP address on dwc0 — at boot only the link-local
# 169.254 address exists; binding that leaves the phone "connecting" forever.
# The DHCP and link-local addresses swap primary/alias order between boots,
# so take the first inet line that is NOT 169.254.*. If we launched without
# it the daemon would bind its 172.16.224.1 default — equally dead — so keep
# waiting until a real lease exists (dongle DHCP can lag past 2 min at boot).
IP=
while [ -z "$IP" ]; do
  IP=$(ifconfig dwc0 | sed -n 's/.*inet \([0-9.]*\).*/\1/p' | grep -v '^169\.254' | sed -n '1p')
  [ -n "$IP" ] || sleep 5
done
export NBT_AP_IP=$IP

pidin ar | grep -v grep | grep -q mm-renderer || {
  MM_RENDERER_CONFIG=$AA/mm-renderer.conf AOI_CONFIG=$AA/aoi \
  LD_LIBRARY_PATH=$CP/lib/carplay:/opt/sys/lib \
  $CP/bin/carplay/mm-renderer -e -v -f -k >$AA/mmr.log 2>&1 &
}
exec $AA/nbt-aad
