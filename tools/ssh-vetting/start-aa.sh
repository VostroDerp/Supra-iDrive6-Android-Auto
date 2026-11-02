#!/bin/sh
# start-aa.sh — bring up the AA stack on hu-omap after a wake (BM3 adapter path).
# Lives at /var/opt/aa/start-aa.sh on the HU. Reversible: only starts processes,
# kills nothing, touches no stock services (scp/mwud/BT untouched).
#
# Network: BM3 dongle BM3_NET_xxxxxx (iap2.cfg), HU on dwc0 (IP from arg, .11 typical).
# Audio: daemon grants eSRC_A2DP->SPEAKERS via audioconn -b 0xc7; BT audio must have
# been picked once in iDrive Media (sticky across sessions).
AA=/var/opt/aa
CP=/fs/sda0/opt/conn
IP=${1:-192.168.0.11}

# --- 1. mm-renderer (video pipeline) -------------------------------------
pidin ar | grep -v grep | grep -q mm-renderer || {
  echo "starting mm-renderer"
  MM_RENDERER_CONFIG=$AA/mm-renderer.conf \
  AOI_CONFIG=$AA/aoi \
  LD_LIBRARY_PATH=$CP/lib/carplay:/opt/sys/lib \
  $CP/bin/carplay/mm-renderer -e -v -f -k >$AA/mmr.log 2>&1 &
  sleep 3
}

# --- 2. nbt-aad on dwc0 --------------------------------------------------
pidin ar | grep -v grep | grep -q nbt-aad || {
  echo "starting nbt-aad on dwc0 ($IP:5277)"
  NBT_AP_CFG=$AA/iap2.cfg \
  NBT_AP_IF=dwc0 NBT_AP_IP=$IP NBT_AP_BSSID=02:00:00:12:34:56 \
  NBT_VERBOSE=1 NBT_LOG_FILE=$AA/nbt.log \
  AOI_CONFIG=$AA/aoi \
  MM_RENDERER_CONFIG=$AA/mm-renderer.conf \
  LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib:/fs/sda0/opt/mm/lib:$CP/lib/carplay:/opt/sys/lib \
  $AA/nbt-aad >$AA/nbt-stdout.log 2>&1 &
}

sleep 4
grep "GapErrorCode" $AA/nbt.log | tail -2
pidin ar | grep -E "nbt-aad|mm-renderer" | grep -v grep
