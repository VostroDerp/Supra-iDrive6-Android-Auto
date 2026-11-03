# NBT-AAD on iDrive 5 without CarPlay — external-AP adapter method

**Status: working and persistent on a BMW NBT Evo iDrive 5 (no CarPlay FSC).**
Verified 2026-11-01/02. Target contribution: an alternative install path for
NBT Evo head units that lack the CarPlay option the stock method piggybacks on.

The stock install assumes CarPlay because upstream never creates a Wi-Fi AP:
the daemon reads `iap2.cfg` for AP credentials and expects the *car* to be
broadcasting that AP (`uap0`, 172.16.224.1) via the CarPlay stack. On a car
with no CarPlay, nothing lights the AP — so this method substitutes any
external Wi-Fi AP that sits on the head unit's wired diagnostic network
(`dwc0`). Everything else is upstream code and stock car binaries.

---

## 1. How it works

### Topology

```
phone ──Wi-Fi──> external AP ──ENET──> car diagnostic wiring ──> HU dwc0
              (dongle, e.g.         (OBD ENET)                  192.168.0.x
               BM3 adapter .10,
               WPA2 SSID/PSK)
phone ──Bluetooth SPP + A2DP──────────────────────────────────> HU BT
```

The "dongle" is **any Wi-Fi↔Ethernet bridge on the car's OBD-ENET network** —
a Bootmod3 adapter (what we used), an ENET-capable travel router, etc. It
provides: (a) a WPA2 AP the phone joins, (b) DHCP so the HU's `dwc0` gets a
routable address. The HU itself does **no** Wi-Fi work on this path — it
plain-TCPs over ethernet.

### Session bring-up (what actually happens, in order)

1. **HU boots** → HMI loads the `NbtAad` plugin → `autostart` flag exists →
   ~15s after HMI is up the plugin `spawn()`s `/opt/aa/nbt-aad` (our wrapper).
2. **Wrapper** (`/opt/aa/nbt-aad`, a shell script):
   - `unset LD_PRELOAD` — the HMI injects graphics libs into children
     (`libEGL_X`/`libGLESv2`/`libKD_X`); fatal to `mm-renderer`. *This was the
     boot-crash root cause.*
   - Waits for a real (non-`169.254.*`) DHCP lease on `dwc0`, exports
     `NBT_AP_IP` — the dongle's DHCP can lag >2 min at boot.
   - Starts `mm-renderer` (stock CarPlay video renderer) if not running.
   - `exec`s the real daemon `/var/opt/aa/nbt-aad`.
3. **Daemon** (`nbt-aad`, upstream binary): reads `iap2.cfg` for the AP
   creds, registers the Android Auto SPP UUID
   `4DE17A00-52CB-11E6-BDF4-0800200C9A66` on the car's Bluetooth via DSI
   (`SGBluetoothGap.BSS_GAP` → `SppUuidRegister`).
4. **Phone** (BT-paired to the car as normal): sees the AA profile on SPP,
   opens the RFCOMM link, receives the AP creds over it, joins the AP's Wi-Fi
   with them, then TCP-connects to `NBT_AP_IP:5277`.
5. **Video**: daemon attaches `mm-renderer` → LayerManager surface 1280x480
   z=2000 → AA UI replaces/overlays iDrive content.
   **Input**: iDrive knob/buttons → daemon → phone.
   **Audio**: daemon runs `audioconn -b 0xc7` (eSRC_A2DP→eSNK_SPEAKERS) →
   phone A2DP-streams to the car's speakers over stock BT.
6. **Menu row** ("Android Auto" in Media/Radio): pressing it while running
   sends the daemon's 'show' request → overlay to front. Also offers
   Start / Force Stop / Restart.

Single instance enforced by the daemon: a second launch while one runs is
what produces the 'show' (return-to-AA) behavior.

### What runs where

| Component | Path on HU | Origin |
|---|---|---|
| AA daemon | `/var/opt/aa/nbt-aad` | **Upstream release binary, unmodified** |
| Menu plugin (5 files) | `/opt/hmi/ID5/data/ro/Plugins/NbtAad/` | **Upstream release `Releases.zip`, unmodified** (`libNbtAad.so`, manifest, xml, texts, images) |
| Wrapper (autostart entry) | `/opt/aa/nbt-aad` | **Ours** — shell script (see §3) |
| Env file | `/opt/aa/nbt-aad.env` | **Ours** — values only; the file-mechanism is the plugin's own `launchDaemon` (≤15 lines) |
| AP creds | `/var/opt/aa/iap2.cfg` | **Ours** — upstream INI format (`[WiFi]` SSID/Passphrase/BSSID/SecurityType), our AP's creds |
| Autostart flag | `/var/opt/aa/autostart` | empty file; mechanism is **upstream** plugin code |
| Video renderer | `/fs/sda0/opt/conn/bin/carplay/mm-renderer` | **Stock BMW binary** — present on NBT Evo even without CarPlay coding; we just launch it |
| Renderer conf | `/var/opt/aa/mm-renderer.conf` | **Ours, adapted** from stock `/opt/conn/etc/carplay/mm-renderer.conf` — keep `mmr-track-engine` + `mmr-mmf-routing`, drop the playlist engine (needs RO `/etc/mm/`) |
| AOI config | `/var/opt/aa/aoi/mmr.conf` | **Ours** — two `dir = "..."` lines pointing mmf at the carplay mm dlls + stock mmedia |
| Manual bring-up | `/var/opt/aa/start-aa.sh` | **Ours** — same steps as wrapper, for recovery/SSH use |
| DSI/mm libs | `/fs/sda0/opt/{sys,mm,conn}/lib/...` | **Stock**, referenced via `LD_LIBRARY_PATH` only |

Nothing stock is patched or replaced on the working path. (`mwud_pub` /
`wifidir_app_p9x` in `tools/` are byte-patched *experimental* binaries from
the abandoned car-hosted-AP research — not needed here.)

`/fs/sda0` mounts read-only at runtime: `mount -uw /fs/sda0` to install,
`mount -ur /fs/sda0` to restore. Everything else lives under `/var/opt/aa/`
(persistent, writable).

### Env vars the wrapper/env-file sets (all consumed by unmodified upstream code)

```
NBT_AP_CFG=/var/opt/aa/iap2.cfg        # daemon's cred source
NBT_AP_IF=dwc0                         # ethernet iface to the AP
NBT_AP_IP=<dhcp lease on dwc0>         # wrapper computes per-boot
NBT_AP_BSSID=<the AP's BSSID>          # e.g. 02:00:00:12:34:56
NBT_LOG_FILE=/var/opt/aa/nbt.log       # NBT_VERBOSE=1 for debugging
AOI_CONFIG=/var/opt/aa/aoi/
MM_RENDERER_CONFIG=/var/opt/aa/mm-renderer.conf
LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib:/fs/sda0/opt/mm/lib:/fs/sda0/opt/conn/lib/carplay:/opt/sys/lib
```

---

## 2. Reproducible install (from zero)

**Prereqs**

- NBT Evo (verified iDrive 5; upstream targets iDrive 6) — **no CarPlay FSC
  required**. `mm-renderer` ships regardless at
  `/fs/sda0/opt/conn/bin/carplay/mm-renderer`.
- A Wi-Fi AP reachable on the HU's `dwc0` diagnostic network with known
  SSID/WPA2 passphrase/BSSID (BM3 adapter, or any ENET bridge/router).
- SSH access to the HU — TurboCode-style HSFZ:6801 unlock, then **plink**
  (stock dropbear is too old for modern OpenSSH; see §4 landmines).
  root password `ts&SK412`.
- For the menu row to *render*: A4A-family coding
  (`A4A`/`REMOTE_HMI`/`TOUCH_COMMAND` — Bimmercode). **Autostart works
  without it** — you just lose the button.
- Upstream release assets: `nbt-aad` + `Releases.zip` (5 plugin files).

**Steps**

1. **Prepare the AP**: note its SSID, WPA2 passphrase, BSSID. Write
   `iap2.cfg` in upstream format:
   ```ini
   [WiFi]
   SSID=YOUR_AP_SSID
   Passphrase=your_psk
   BSSID=AABBCCDDEEFF        # 12 hex, no colons
   SecurityType=WPA2
   ```
2. **Unlock + SSH in.** Verify `dwc0` has a lease from the AP
   (`ifconfig dwc0` — a `192.168.0.x` besides the `169.254` link-local).
3. **Create `/var/opt/aa/`** and push: `nbt-aad` (chmod +x), `iap2.cfg`,
   `mm-renderer.conf` (adapted — plugin dlls `mmr-track-engine.so`,
   `mmr-mmf-routing.so` only), `aoi/mmr.conf`:
   ```
   dir = "/fs/sda0/opt/conn/lib/carplay/mm"
   dir = "/fs/sda0/opt/mm/lib/dll/mmedia"
   ```
   plus `start-aa.sh`, and empty file `autostart`.
4. **`mount -uw /fs/sda0`**, create `/opt/hmi/ID5/data/ro/Plugins/NbtAad/`
   and `/opt/aa/`. Push the 5 plugin files into the plugin dir; push
   `nbt-aad.sh` → `/opt/aa/nbt-aad` (chmod +x) and `nbt-aad.env` →
   `/opt/aa/nbt-aad.env`. Edit the env's `NBT_AP_BSSID` to your AP's.
   **`mount -ur /fs/sda0`.**
5. **Reboot the HU** (30s power/volume-knob hold). After iDrive is up:
   ~15–60s later the daemon is running (autostart), SPP is registered.
6. **Phone**: pair Bluetooth to the car as normal → AA offers itself →
   accept on phone → it joins your AP → session + projection. First time,
   raise **iDrive master volume** (audio ships at whatever it was).

**Daily use after install**: nothing to do — boot → autostart → phone
auto-connects. Add another phone = ordinary BT pair, same flow (one session
at a time). Return to AA = the menu row. Kill everything = menu Force Stop
or `slay nbt-aad` — fully reversible, no firmware touched, `mount -ur` left
the flash read-only.

**Uninstall**: `mount -uw`, delete `/opt/hmi/ID5/data/ro/Plugins/NbtAad/`
and `/opt/aa/`, `mount -ur`, delete `/var/opt/aa/`, reboot. 100% stock.

---

## 3. The wrapper (the only real code we wrote)

`/opt/aa/nbt-aad` — verbatim:

```sh
#!/bin/sh
# NbtAad app entry: bring up the AA stack on the external-AP network.
# The app inherits the HMI's LD_PRELOAD (libEGL_X/libGLESv2/libKD_X) which is
# FATAL to child processes like mm-renderer. Strip it before starting
# anything — this is what caused the boot crashes.
unset LD_PRELOAD
AA=/var/opt/aa
CP=/fs/sda0/opt/conn

# Wait for the AP's DHCP lease on dwc0 — at boot only the link-local
# 169.254 address may exist; binding that (or nothing) is a dead daemon.
# The DHCP and link-local addresses swap primary/alias order between boots,
# so take the first inet line that is NOT 169.254.*.
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
```

Why each line exists — all three were real boot failures we hit:

- `unset LD_PRELOAD`: app-spawned children inherit the HMI's GL preload →
  `mm-renderer` dies instantly → HU resets during session bring-up
  (crash-loop, exactly the readme's documented failure mode).
- The IP wait-loop: at ~15s post-boot `dwc0` often has only `169.254.x`;
  *and* the DHCP/link-local addresses swap primary/alias order between
  boots. Grab the wrong one → daemon tells the phone to connect to an
  unreachable IP → "connecting" forever. Empty → daemon falls back to its
  built-in `172.16.224.1` default — equally dead on this path.
- `mm-renderer` idempotent start: upstream assumes CarPlay already runs it.

---

## 4. Issues we hit (field guide)

| Symptom | Cause | Fix |
|---|---|---|
| HU boots → resets ~20s → loop | app-spawned daemon inherited HMI `LD_PRELOAD` → `mm-renderer` died → HU reset mid-session | `unset LD_PRELOAD` in wrapper. Plugin boot-guard latches autostart off after 3 crash-boots — that's the escape, not a second bug |
| "Android Auto — connecting" forever | daemon bound `169.254` (alias-order swap) or `172.16.224.1` (empty `NBT_AP_IP` default) | wrapper wait-loop, §3 |
| `[SppUuidRegister] failed` / DSI errors at launch | missing `LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib` | env file, §1 |
| Black AA surface, controls dead | `mm-renderer` not running / bad `AOI_CONFIG` | `pidin ar \| grep mm-renderer`; check `mmr.log`; `dir = "..."` syntax needs spaces |
| Session plays but silent | **iDrive master volume at 0** — *not* a routing fault. `audioconn -b 0xc7` grants AND activates BT source on ID5 | volume knob on the BT-audio source. (In AA, the knob sets *phone* volume, no on-screen feedback — separate level) |
| No menu row | A4A-family coding absent | Bimmercode `A4A`/`REMOTE_HMI`/`TOUCH_COMMAND`. Autostart unaffected |
| Phone won't join AP | BM3 dongle appears **single-client** — laptop must be off its Wi-Fi for the phone to associate | disconnect laptop from dongle SSID after setup |
| AA drops mid-drive | **car sleep timer** kills the OBD-powered dongle even while HU stays on — not the phone, not the daemon. Log always ends `Phone closed socket` | inherent to OBD-powered APs; the car-hosted `uap0` path (WIP) removes it |
| `ssh`/scp fails, `plink` works | dropbear too old for modern OpenSSH (DH/libcrypto) | use plink; cache host key once per node (`printf 'y\n' \| plink ...`) |
| `touch`/`head`/`printf` not found | HU ksh is minimal — no coreutils | `cat > file`, `ls -l`, `pidin ar`, `netstat -an`, `sed`, `ifconfig` all exist |
| writes fail under `/opt` | `/fs/sda0` mounted RO | `mount -uw /fs/sda0` … `mount -ur` |
| 2nd launch exits immediately | previous daemon mid-shutdown | `sleep 3` after `slay` before relaunch |
| `/dev/btspp0` missing / no SPP | HU BT channel not fully open | re-pair phone, or toggle entries in iDrive device list (upstream readme's advice — confirmed) |
| HU personalized settings lost | collateral of the crash-loop resets | one-time cost; re-set them after stabilizing |
| HU "asleep", nothing answers | accessory sleep is shallow — sshd/daemon survive; real death = ignition off + timer | wake with ignition; nodes can re-shuffle IPs (`.11`↔`.17`) |
| phantom outgoing BMW service call (once) | iDrive consumed a hardkey press during a session-drop window | not reproduced; avoid COM/telephone hardkeys while sessions flap |

---

## 5. What still separates this from a dongle-free install

Everything above works because the *dongle* provides the AP. The car's own
radio can too — `uap0` exists at `172.16.224.1` with DHCP/pf/driver staged;
only the beacon is off. That's the remaining problem upstream solves with
CarPlay. Ranked options under test (details in `findings.md` + `archive/devin-remember.md`):

1. `acpd` (stock Apple daemon) self-launch — may light `uap0` + write real
   `iap2.cfg` before the FSC license check gates anything.
2. `WlanDSICommander` — stock DSI Wi-Fi control binary.
3. `mwud` `ap_init`/`ap_start` on `uap0` — wire format already decoded
   (`id5/tools/legacy/mwu.py`), reachable via SSH-local `9096` tunnel.
4. Small QNX SDP tool calling `Wlan_MicroApCfg.WlanMicroApOnOff` via DSI.

Also tracked: known radio constraint — the single radio multiplexes
uAP↔WFD, so an *active* Miracast session can steal the AP role (dormant
Miracast is fine); and Miracast/menu coding can be reverted once `uap0` is
proven — keep the A4A-family coding for the menu row.

**Parallel avenue (repo owner's lead):** `/opt/mm/lib/libNmeSDK.so` reportedly
contains an `ICinemoAndroidAuto` API — Cinemo's CORE Projection OEM
middleware ships on the NBT Evo (`/opt/mm/bin/Cinemo`, `NmeCmdLine`
confirmed present on our iDrive 5 firmware). If a dormant-but-complete AA
engine exists there, it could offer a more native path — including *wired*
USB projection, which sidesteps the AP problem entirely. Pull list +
analysis plan in `archive/devin-remember.md`.

**USB transport — upstream PR #42 (draft):** adds `NBT_USB=1`/`NBT_USB_DIRECT=1`
driving the stock AOAP plumbing our ID5 already ships (`aoap-switch`,
`devc-serusb-aoap` → `/dev/oap*`, `io-usb` running). USB + wireless arbitrate
one session; cable wins. Needs a QNX SDP build (no prebuilt binary) and is
still rough (unplug/replug, audio untested) — but if the `/dev/android*`/
`/dev/oap*` nodes appear on plug on ID5 (recon in `archive/devin-remember.md`), it's a
credible daily-driver path that needs no AP at all and charges the phone.
Branch fetched locally as `usb-pr42`.

## 6. Files in this repo for this method

- `id5/tools/hu-install/` — the deployed `nbt-aad.sh` wrapper,
  `nbt-aad.env`, `plugin/` (5 upstream release files)
- `id5/tools/car/start-aa.sh` — manual bring-up (same logic as wrapper)
- `id5/tools/legacy/hu.sh` — plink-based HU access (push/pull/sh)
- `id5/local/captures/session-1102/` — full recon + session logs
- `findings.md` — deep technical notes (mwud protocol, AP research, DSI calls)
- `archive/devin-remember.md` — session-to-session handoff
