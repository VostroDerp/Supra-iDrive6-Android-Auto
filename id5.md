# iDrive 5 (NBT Evo) runbook — SSH unlock, recon, install, rollback

Target unit: BMW NBT Evo, iDrive 5 HMI (`HU_NBT2`, diag address `0x63`).
Car: F36 4GC. Goal: native wireless Android Auto via `nbt-aad` + NbtAad
plugin, staged and reversible. **No flashing anywhere.**

## Recon verdicts (verified on the real HU, 2026-11-01)

- iStep **`NBTevo_W20204I`** (`/opt/sys/etc/nbt_version.txt` on omap) — W-series,
  post-18-03 → debug-gate unlock required; `nbt-ssh-unlock.py` proven end-to-end.
- HU = `hu-jacinto` (QNX 6.5.0 J5_Evo) + `hu-omap` (TI-OMAP5432) over QNET.
  Reachable via bm3 adapter bridge at `192.168.0.11` (jacinto) / `192.168.0.12` (omap);
  HSFZ TCP `6801` routes fine through the adapter — **no ENET cable needed**.
- **Physical Wi-Fi: present and up** — `uap0` @ `172.16.224.1`, `wfd0` @
  `172.16.222.1` on omap; `wpa_supplicant`, `mwud`, `DnsMasq`, `WlanDSICommander`
  all on the unit.
- **CarPlay stack installed but dormant** — `/opt/conn/bin/carplay/`
  (`acpd`, `iAP2ProtocolMgmt`, `mdnsd`, `mm-ipod`, `mm-renderer`) and
  `/opt/conn/etc/carplay/` exist; none run → license-gated (FSC `0x143`/`6CP`),
  consistent with no menu entry.
- **Plugin framework confirmed**: `/opt/hmi/ID5/data/ro/Plugins/` on omap with
  `ActiveSeating`, `SecurityMenu` — exactly where `NbtAad` installs.
- AOAP USB support exists (`devc-serusb-aoap`, `aoap-switch`, `aoap.cfg`) —
  a future wired-AA option, not needed now.
- HU shell is minimal: no `head`, `date`, `find`, `uniq`, `sqlite3`.

### nbt-aad dependency analysis (source-read)

The daemon consumes CarPlay *infrastructure*, never the CarPlay *license*:

| Need | Source on HU | This unit |
|---|---|---|
| BT serial channel | `SppUuidRegister` via DSI/BSS_GAP → `/dev/btspp0` | generic BT infra, runs already |
| AP SSID + passphrase | `iap2.cfg`/`iap2_Wifi.cfg` (or `NBT_AP_CFG`) | unknown — `probe-ap.sh` |
| Wi-Fi AP beacon | `uap0` broadcasting | up w/ IP; beacon state unknown |
| Packet filter off | `pfctl -d` (daemon does it) | root can |
| Menu row (Stage B) | `/opt/hmi/ID5/data/ro/Plugins/` + A4A/REMOTE coding | plugin dir exists; coding TBD |

**Primary path — drive the pairing directly** (no FSC). Recon + pulled-binary
analysis (Connectivity/mwud/WlanDSICommander in `tools/ssh-vetting/pulled/`):

- Live `iap2.cfg`: **absent everywhere** (Connectivity never wrote one —
  CarPlay never ran). Factory templates exist at
  `/fs/sda0/opt/conn/data/carplay/` (incl. `carplay.ini` panel data).
- `uap0` is up with an IP but **not beaconing** (confirmed by phone scan).
- The stock AP-bring-up chain is in `Connectivity`:
  `WIFI_steuernActivation` (DSI diag job, `WlanDiagFacade`) → checks
  **`WifiAPCodingValue`** → `CWlanMicroAPCfg` → `NWS_IEEE80211` service →
  AP on + **live `iap2.cfg` written** (daemon then reads it stock).
  Same jobs exist for `steuernMode`/`steuernPairable`/`steuernResetToBasicState`
  and `WIFI_status*`/`AP_statusNetworkParameters` reads.
- `pss_config.cfg` coded line already lists `wifi` — coding may already be OK.
- DHCP for `172.16.224.x` is pre-provisioned (`/var/opt/conn/dhcpd.uap0.conf`).
- `mwud` has a full uAP API (`ap_init`/`ap_set_cfg`/`ap_start`) — driver-level
  fallback if the sanctioned path is blocked.

**Invocation options (in order of preference):**
1. iDrive hidden service menu (hold MENU ~10 s) — WiFi/hotspot toggles, zero code.
2. Diag job `WIFI_STEUERN_*` over our existing HSFZ path (Ediabas client).
3. Small DSI tool built with the daemon (milestone-7 DSI client exists —
   `include/dhu_sdp.h`, `dsi_controller.h`) calling `WlanMicroApOnOff`.

**Open checks next session:** hidden-menu WLAN entries; whether
`WifiAPCodingValue` is already set (else Bimmercode FDL `6NW`/hotspot —
free, no FSC); after activation verify `iap2.cfg` appears + SSID broadcast.

**Fallback:** CarPlay FSC activation via ENET coding service (~£40-100).


## 0. SSH unlock methods by iStep

| iStep | Method | Notes |
|---|---|---|
| pre 18-03 | USB `UPD*.bin` | patched/closed after 18-03 |
| any | UART + `sysetshellevo` → `setk SYS_DEBUG=true` | needs physical access to board edge connector (pins 3-4, 1.8V TTL, 115200 8N1); also the emergency recovery path |
| post 18-03 | **bavarian-ssh-client / nbt-ssh-unlock.py** | debug-gate routines, non-persistent (until reboot); needs internet for the signing relay |
| post 18-03 | TurboCode unlocker | paid alternative doing the same class of unlock |

Our path: `tools/ssh-vetting/nbt-ssh-unlock.py` — zero-dep Python client, only
ever sends the three allowlisted debug-gate routines (`FD2E`/`FD2F`/`FDAC`)
to ECU `0x63`. Equivalent: upstream `bavarian-ssh-client` GUI pointed through
`mitm_relay.py` for live frame inspection. See `tools/ssh-vetting/README.md`.

## 1. Network layout

- bm3 adapter (verified): laptop Wi-Fi → adapter `192.168.0.10`, HU reachable
  at `192.168.0.11` (jacinto, alias of `169.254.199.99`) and `192.168.0.12`
  (omap, alias of `169.254.199.119`). HSFZ `6801` + SSH `22` both route.
- ENET cable: only needed if the adapter path ever fails.
- SSH is old dropbear on port 22 — Fedora needs legacy flags + crypto override:
  ```
  OPENSSL_ENABLE_SHA1_SIGNATURES=1 ssh -o HostKeyAlgorithms=ssh-rsa \
    -o PubkeyAcceptedAlgorithms=+ssh-rsa \
    -o KexAlgorithms=+diffie-hellman-group1-sha1,diffie-hellman-group14-sha1 \
    -m hmac-sha1 root@192.168.0.11          # password: ts&SK412
  ```
  (`SSH_ASKPASS` + `SSH_ASKPASS_REQUIRE=force` for scripts; port 6969 is closed.)
- HMI-side filesystem lives on the **omap** node: after SSH to .99, remount
  via QNET then enter its namespace:
  ```
  mount -o remount rw /net/hu-omap/fs/sda0
  on -f /net/hu-omap/ /bin/sh
  ```
  Remount ro when done; reboot also reverts SSH itself.

## 2. Unlock runbook (laptop on ENET)

```
# optional: observe the challenge randomness once (read-only, no server)
python3 tools/ssh-vetting/nbt-ssh-unlock.py 169.254.199.99 --probe

python3 tools/ssh-vetting/nbt-ssh-unlock.py 169.254.199.99
# expected: "Gate: open" -> "Enabling SSH..." -> "SSH: enabled"
ssh root@169.254.199.99 -o HostKeyAlgorithms=+ssh-rsa -m hmac-sha1   # ts&SK412
```

If the server sends anything outside the three routines, the client aborts
before the frame reaches the car — that is the point of using ours.

## 3. Read-only recon (before ANY write)

On the HU, omap namespace where relevant:

- `uname -a`, CPU/board; confirm QNX.
- iStep/SW version files: `ls /etc/version* /var/opt/* 2>/dev/null`, `cat /etc/system/...` — cross-check against the iDrive-menu value from Phase 0.
- Plugin framework: `ls /opt/hmi/` — confirm `ID5` tree exists;
  `ls /opt/hmi/ID5/data/ro/Plugins/` — list existing plugins, grab one
  descriptor XML for ID5 state/focus ID reference.
- Framework libs the plugin links against: locate `libhmi*`/`libeb`/`libplugin*`
  under `/opt/hmi/ID5/` and `/usr/lib/`; tar them off for an exact-ABI rebuild.
- CarPlay plumbing: `ls /var/opt/conn/carplay/`, `grep -i pass /var/opt/conn/carplay/*.cfg`
  (Wi-Fi creds the daemon reuses); `cat /etc/carplay.ini`-ish files; `pidin ar | grep -i carplay`.
- `ls /mnt/` layout + `/fs/sda0/opt/` free space (`df /fs/sda0`).
- Backup now: `tar czf /mnt/<usb>/hmi-backup.tgz /opt/hmi/ID5 /var/opt/conn`
  plus coding backup already done in Bimmercode.

## 4. Stage A — daemon only (the real milestone)

```
mount -o remount rw /net/hu-omap/fs/sda0
on -f /net/hu-omap/ /bin/sh
mkdir -p /opt/aa && cp /mnt/<usb>/nbt-aad /opt/aa/ && chmod +x /opt/aa/nbt-aad
/opt/aa/nbt-aad          # manual foreground run, no autostart yet
```

Then on the phone: pair as a *new* BT device → wait for Wi-Fi handoff →
Android Auto should start. Driven by the iDrive knob; audio via car BT.
If it works: **AA is functional** — everything after this is convenience.

Common failures: `/dev/btspp0` missing → re-pair the phone; Wi-Fi info seen but
no connect → disable phone hotspot, compare `/var/opt/conn/carplay/*.cfg`
passwords, reboot phone.

## 5. Stage B — iDrive menu entry

Plugin dir (already ID5-scoped upstream): `/opt/hmi/ID5/data/ro/Plugins/NbtAad/`.

- Route A: adapt `NbtAadPlugin.xml` state/focus IDs to ID5 values found in
  recon + rebuild plugin `.so` against the unit's own libs (exact ABI).
- Route B (reversible fallback): code `HMI_ID_VERSION = id6_light` via
  Bimmercode → upstream ID6 files drop in unmodified. Revert by restoring
  saved coding.
- Apply: `OnOffDSICommander appreset`, then Media/Radio → Android Auto row.

## 6. Rollback

```
mount -o remount rw /net/hu-omap/fs/sda0
on -f /net/hu-omap/ /bin/sh
rm -rf /opt/aa /opt/hmi/ID5/data/ro/Plugins/NbtAad
OnOffDSICommander appreset     # or reboot; SSH closes on reboot anyway
```

Updates keep `.old` copies (`updating.md`). Coding changes revert via the
Bimmercode CAFD backup. HMI crash-loops still leave SSH/diag intact; worst
case UART serial remains as physical recovery.

## 7. PROVEN LIVE — full AA session on iDrive 5 (2026-11-01 late session)

**Result: Android Auto projected on the iDrive 5 screen, navigation worked, iDrive
knob input worked.** Audio still routed to the phone speaker (A2DP not yet
negotiated — likely just "Media audio" toggle in phone BT settings).

### What ran (all reversible, lives in `/var/opt/aa/` — RAM-backed? no, persistent)

- Daemon: prebuilt `nbt-aad` from upstream release `neutral-fix` (byte-verified),
  at `/var/opt/aa/nbt-aad`. `liba2i_client.so` served from
  `/fs/sda0/opt/mm/lib/` via `LD_LIBRARY_PATH`.
- mm-renderer (REQUIRED for video — without it the AA surface shows black):
  `/fs/sda0/opt/conn/bin/carplay/mm-renderer` with:
  - `MM_RENDERER_CONFIG=/var/opt/aa/mm-renderer.conf` (track-engine + mmf-routing
    plugins only; drop playlist engine — it needs `/etc/mm/` which is RO)
  - `AOI_CONFIG=/var/opt/aa/aoi/` containing `mmr.conf`:
    ```
    dir = "/fs/sda0/opt/conn/lib/carplay/mm"      # has screen_writer + ivahd_video_decoder-omap5430-dce3 + mdev_streamer
    dir = "/fs/sda0/opt/mm/lib/dll/mmedia"        # stock audio filters/parsers
    ```
    (syntax is `dir = "path"` WITH spaces; AOI_CONFIG is the DIRECTORY, it loads
    `<component>.conf` inside — the mmf plugin asks for `mmr.conf`)
  - `LD_LIBRARY_PATH=/fs/sda0/opt/conn/lib/carplay:/opt/sys/lib`
  - flags: `-e -v -f -k` (stderr, verbose, foreground, clean stale PPS)
  - Confirmed init: plugins + filters all loaded; publishes
    `/pps/services/multimedia/renderer/{component,context,control}`.
- Creds file `/var/opt/aa/iap2.cfg` — INI `[WiFi]` section, keys
  `SSID`/`Passphrase`/`BSSID` (12 hex, no colons)/`SecurityType`.
- Launch env (adapter test): `NBT_AP_CFG`, `NBT_AP_IF=dwc0`,
  `NBT_AP_IP=192.168.0.13`, `NBT_AP_BSSID=02:00:00:12:34:56`, `NBT_VERBOSE=1`,
  `NBT_LOG_FILE=/var/opt/aa/nbt.log`. Daemon listens lazily on `AP_IP:5277`
  after SPP handshake; earlier failed attempts just recreate the listener.
- HU sleep is SHALLOW: sshd+daemon+mm-renderer all SURVIVE it. Unlock also
  survives reboots (sshd stayed up across 2 Bimmercode reboots).

### Verified facts this session

- `WLAN_AP` coding alone does NOT hold the micro-AP: slog shows
  `MLAN_EVENT_ID_UAP_FW_BSS_START` at boot then a WFD role-switch — one radio
  multiplexes uAP↔WFD; something must hold the uAP role.
- Miracast coding (`MIRRORING`, `ENT_MIRACAST`, `ENT_MIRACAST_DISPLAY_SETTINGS`)
  = aktiv → "Add new device → Screen mirroring" menu appears → opens a real WFD
  pairing window; phone gets lease `172.16.222.100` on `wfd0`. GO SSID
  `DIRECT-xx-BMWnnnnn`, GO BSSID `<wifi-direct-bssid>`, passphrase UNVERIFIED
  (candidate `[redacted]` from `WFDPass.conf`; WPS-negotiated PSK
  probable). ConnMngr.rnf (sqlite) stores group SSID/BSSID, no passphrase.
- **WFD GO is unusable as AA transport**: phone takes AA creds but never opens
  TCP — Android AA wants its own WPA2 association (WifiNetworkSuggestion), the
  WFD-managed link doesn't count, and the GO only lives while Miracast engages.
- **bm3 adapter as test AP works end-to-end**: iap2.cfg pointed at
  `BM3_NET_xxxxxx`/`bootmod3`, daemon at `dwc0:192.168.0.13`; phone joined the
  adapter Wi-Fi → `TcpServer: 2 accepted` → session + projection on screen.
  bm3 dongle appears single-client: phone can only join while laptop is off it.
- Screen path: daemon owns LayerManager surface `1280` (1280x480, z-order 2000)
  — visibility toggles in LMGR log; black = surface up, no frames until
  mm-renderer+decoder ran.
- DSI works: daemon's own SPP register/deregister via `SGBluetoothGap.BSS_GAP`
  35.0 — `TX request id=0x2e/0x2f` responses GapErrorCode=0.

### Held-AP options for the real transport (NO SDK needed for 1-2)

1. **`acpd`** — `/fs/sda0/opt/conn/bin/carplay/acpd` + full iAP2 stack
   (`ipod-*-iap2` transports, `libconn_carplay*` devctrls, mdnsd) is dormant on
   box. On a CarPlay car acpd+Connectivity drive the whole chain: iAP2 BT →
   facade → micro-AP on uap0 + live `iap2.cfg`. License check sits in the iAP2
   session layer; AP bring-up may fire anyway. Test: launch with
   `LD_LIBRARY_PATH=/fs/sda0/opt/conn/lib/carplay`, watch `uap0` + iap2.cfg +
   slog ~30 s, slay. Cleanest path if it works — the car does it all itself.
2. **`mwud` UDP 9096** — its MRVL_CMD/uAP socket binds 127.0.0.1 only, but
   `ssh -L 9096:127.0.0.1:9096` forwards it to the laptop; Python sends
   `AP_API_EVENT_SET_CFG`+`START` with an SSID/PSK we choose. No compiling;
   needs protocol reversing from pulled `mwud` binary.
3. **QNX SDK** — still the polished path (`nbt-wlan` DSI tool hitting
   `Wlan_MicroApCfg.WlanMicroApOnOff.requestStart`, startup scripts). Eval
   license pending; official download via get.qnx.com group `programid=64559`
   (SDP 6.5.0 Linux all-targets ~542 MB; avoid x86-only SP1 packages).

### Remaining for production

- **Audio**: verify phone BT "Media audio" toggle + car BT audio source.
- **Transport**: held-AP option above (acpd probe first — one command).
- **Stage B**: plugin files exist in release zip (`libNbtAad.so`,
  `NbtAad.xml`, image/text packs); ID5 XML state/focus adaptation still needed.
- **Boot persistence**: launch script for mm-renderer+daemon; survives sleeps
  already, needs boot hooks later.

## Access recipe (VERIFIED — use `tools/ssh-vetting/hu.sh`, don't re-derive)

- **`hu.sh sh '<cmd>'` / `hu.sh push/pull` / `hu.sh scan` / `hu.sh unlock`** —
  auto-scans nodes, unlocks if needed, runs on hu-omap.
- Nodes `192.168.0.11-17` shuffle every wake; `.10`=adapter, `.18`=laptop.
  Which node is hu-omap varies (`.15`,`.13`,`.11` all observed); unlock via the
  node answering :6801, sshd may then appear on DIFFERENT nodes.
- **sshd = OpenSSH_5.2 QNX** → modern ssh dies `error in libcrypto` mid-kex
  (DH pub key rejected by OpenSSL 3.x). **Use plink/pscp** (`plink -ssh -batch`).
- **root password `ts&SK412`** — password auth, no key needed.
- QNX ksh lacks `head`,`printf`,`od`,`nohup` — never pipe remote output through
  them; use `echo`/`ls`/`cat`, pull binaries to laptop for analysis.
- CORRECTION: HU sleep does NOT reliably preserve processes — nbt-aad &
  mm-renderer were dead after the last sleep (must restart each session until
  boot-persistence exists). `/var/opt/aa/` files persist.

## 8. mwud / wifidir_app session decode (VERIFIED 2026-11-01 late)

- mwud control = text `key=value` UDP: cmds→127.0.0.1:9096, client events bind :9097.
  Single-client: `scp` (wicome_config svc, args: `-f /var/opt/conn/pss_config.cfg
  -s wicome_config -r /opt/conn/data,/var/opt/conn -k HostAgent.Port=2020
  MonitorService.Port=2021 VerboseLevel=5`) holds the slot — must `slay scp`,
  then RESTART mwud clean (`mwud -c /opt/conn/bin/mwu_dev.conf
  -d /opt/conn/bin/wifidirect_defaults.conf` — verbose to stdout, not slog)
  before wifidir_app can bind+init. Connectivity/Online/scp all watchdog-respawn
  on their own (~30s), so the slot re-locks — work fast.
- `wifidir_app wfd0 -a -p -c <conf>` (wfdX ifaces only; conf keys:
  SSID_SUFFIX→`go_ssid=DIRECT-xx-SUFFIX`, INTENT, OP_CHANNEL_NUMBER,
  DEVICE_NAME, PERSISTENT_RECORD_BACKUP_FILE). Init msg observed on wire:
  `module=wifidirect iface=wfd0 cmd=init name=.. intent=15 op_chan=1
  listen_chan=1 methods=0088 go_ssid=DIRECT-94-NBTAAD manuf=.. num_sec_devices=4`.
  mwud sets custom P2P/WPS IEs (`PRIV_CMD_CUSTOMIE` dumps visible in verbose),
  generates UUID, then `Role Received: 1 → WARNING Expected STA mode, proceeding`.
- Group creds land in `./wps.conf` after GO negotiation (never got that far —
  phone saw nothing on Wi-Fi Direct scan before HU slept).
- OPEN: does the inited device become discoverable on its own, or does the menu
  need a choice (e.g. [6] toggle listen/find)? -a didn't obviously start GO.
- mwpamod cmds for a real uAP (NOT p2p): `ap_init ssid=X key=Y auth=z encrypt=w`,
  `ap_set_cfg bcast_ssid filter_mode mac_filter bridge_interface`, `ap_start`,
  `ap_deinit`, `ap_deauth mac=..`, `ap_sta_list` — same socket, iface=uap0.
  Untried: wifidir_app only sends these for its GO flow; a manual sender needs
  an on-HU UDP tool (none found — no nc/perl/python) OR write one via SDK.

## 9. Coexistence session (2026-11-01 wake #3 — scp alive, OMAP ssh on .17)

- **Ignition cycle put sshd on OMAP** (`192.168.0.17`, also telnet:23+qconn:8000
  there). Jacinto had sshd too (.11). `hu.sh scan` finds it.
- **mwud is NOT a boot daemon.** scp (`-s wicome_config`) is its only client and
  spawns it on demand (Miracast). mwud stayed dead for a whole ignition-on wake
  while scp ran. Socket state with scp alive + mwud dead: `127.0.0.1.9097` bound
  (scp's own event bind) + `65527→9096` lingering connected-client socket.
- **STALE LOG TRAP:** `/var/opt/aa/mwud_run.log` persists across boots —
  "MWPAMOD: Started AP / ssid=DIRECT-Y8-NBTAAD" seen this wake was the PREVIOUS
  session's manual mwud run. Always `ls -l` mtime before trusting the log.
- `wifidir_app_p98` (stock app, event port patched 9097→9098) **binds fine while
  scp owns 9097** — socket coexistence confirmed. Its "Failed to initialize
  wifi direct" was traced to **mwud being dead**, not (yet) proven to be scp
  refusing a 2nd wfd client. That question is OPEN.
- `wifidir_app` accepts **wfdX ifaces only** — rejects uap0; no stock path to
  mwpamod commands on uap0.
- start-aa.sh updated: step 0 starts mwud if `pidin` finds none, then the
  patched app, mm-renderer, nbt-aad (all no-kill).
- Real HU reboot (volume knob ~30s) restores BT + stock `DIRECT-*-BMWnnnnn` GO —
  earlier BT loss was our scp kills, self-heals on cold start.
- FALLBACK if scp blocks the wfd session: **Bimmercode WLAN-hotspot coding**
  (6NW / WifiAPCodingValue) — stock `Connectivity` brings up uap0 micro-AP with
  SSID/PSK shown in iDrive menu; daemon then runs on 172.16.224.1. No mwud
  fight at all — the car's own feature.
- nbt-aad + mm-renderer currently RUNNING on wfd0:5277 (daemon bound before
  AP existed — harmless, it just waits for the handshake).

## 10. mwud wire format REVERSED (disassembly, 2026-11-01)

- Datagram = **8-byte header + newline-separated kv body**.
  hdr[0] = u32 LE body_len; code allocs len+8, MSG_PEEKs 8, then recvs len+8.
  hdr[1] = u32 echoed into the response (seq/type; 0 works).
  Raw text without header => first 4 bytes read as length => alloc fails
  ("MWU: Failed to alloc space for msg body" — observed live).
- Body rules: each `k=v` line MUST end in `\n`; `module=<name>` required or
  "Failed to find module=" / "No registered module named %s".
- Replies are sent to the datagram's source address — a remote client gets
  real responses (status= lines).
- Working recipe (VERIFIED mechanically, pending live AP test):
  - `mwud_pub` = mwud patched `127.0.0.1:9096 -> 0.0.0.0:9196` (2-byte-shorter
    string + NUL pad, fits). Events still -> 127.0.0.1:9197.
  - Laptop sends UDP to <omap-bridge-ip>:9196 — NO on-HU sender needed.
  - `uap0` is a SEPARATE BSS from wfd0 — our AP can live there without
    fighting scp's Miracast GO (radio already in GO role on wfd0 blocks the
    wifidir GO flow at "Expected STA mode").
  - send: `module=mwpamod\niface=uap0\ncmd=ap_init\nssid=NBTAA-DIRECT\n`
    `key=1234567890\nauth=0020\nencrypt=0008\n` then `cmd=ap_start`.
- `wifidir_app_mc` = app patched cmd dest -> `239.0.0.1:9196` (multicast;
  exact 14-char fit) for wire sniffing if needed — join group on laptop.
- Why wifidir_app_p98 stalled vs scp: patched event port only affects ITS
  bind; mwud probably routes per-session state through its single control
  socket client — second client never becomes session owner. Shadow mwud
  (9196/9197) accepted the same app instantly (full init logged).

## 11. Laptop-UDP dead; gdb-injection path (2026-11-01 wake #4)

- **BM3 adapter drops laptop→HU UDP** (TCP fine). pf rule
  `pass in quick on dwc0 udp 192.168.0.0/24 -> (self) port 9196` loaded into
  empty evaluated anchor `filter_BT1` (`echo rule | pfctl -a filter_BT1 -f -`;
  flush = `pfctl -a filter_BT1 -F r`) — rule Evaluations rose but Packets: 0
  → packets die before pf, i.e. in the adapter itself.
- Multicast test confirmed wrong-wire egress: `wifidir_app_mc` (cmd dest
  239.0.0.1:9196) sourced its socket from **160.48.199.119 — the wired ENET
  iface**, not the BM3 bridge. HU routes mcast/egress via wired net.
- pf ruleset: `block drop all` default; uap0 wide open FOR AP CLIENTS
  (udp 49512:65535 + 53/5353 + CarPlay service ports) — once uap0 is lit
  clients reach HU fine. dwc0 has no inbound UDP passes.
- **Socket reality on QNX**: mwud/wifidir_app import NO socket calls
  (sendto/recvfrom absent from relocs) — transport = MsgSendv/devctl +
  write/writev on CONNECTED udp fds. Injection = `writev(fd, iov, 1)` on the
  app's already-connected socket.
- **pdebug IS on the HU** (`/bin/pdebug <port>` — TCP gdb-remote, adapter
  passes TCP). Plan: attach gdb → app's cmd fd → call writev() with our
  packet → mwud routes by `module=` → ap_init on uap0.
- GOT slots (wifidir_app, non-PIE): writev=`0x126708`, malloc=`0x126630`,
  read=`0x126674`, free=`0x126758`. Resolve at runtime: `x/wx <slot>`.
- Ready: `tools/ssh-vetting/mwu-inject.py <pid> <fd> kv...` emits the gdb
  script. Steps: relaunch wifidir_app_p97 (fd→9196, stalls at role check —
  harmless), `pdebug 8001 &`, find fd via `sockstat|grep wifidir_app`,
  run generated gdb script → watch mwud_pub.log for "Started AP".

## 12. gdb-injection recipe v2 (2026-11-01)

- Adapter TCP whitelist observed: 22 (sshd), 23 (telnetd), 80 (FREE), 8000
  (qconn). Everything else filtered. **pdebug must listen on :80**.
- pdebug = GDB-remote agent: `/bin/pdebug 80 &` on HU; gdb `target remote
  <omap-ip>:80`. Needs `set remote <feature>-packet off` lines (inject3.gdb
  has them) or modern probes hang.
- **Injection = mwud self-send.** mwud has static sendto-equivalent at
  `0x12b090(fd, buf, len, flags, sa_ptr, sa_len)` (MsgSendv marshal to io-pkt;
  sa = sockaddr_in in caller mem). Call it on **fd5** (bound ephemeral socket,
  not the :9196 listener) with dest=127.0.0.1:9196 — datagram comes back
  through its own recv → executes as client cmd. Reply goes to dead port, no
  loop. malloc resolved via GOT slot 0x195148.
- Scratch buffers: gdb `call malloc` via GOT-resolved addr, `set {char[N]}`
  writes bytes into inferior.
- Backup donor: `wifidir_app_live` = app with caps-fail made nonfatal
  (file 0x4792: e0->a0, mvn->mov r4,#0) + cmd/events on 9196/9197 — stays
  alive holding a connected socket for writev() injection if self-send
  misbehaves.
- Pending injection cmds: ap_init (uap0, NBTAA-DIRECT/1234567890) then
  ap_start, then ifconfig uap0 + phone scan, then daemon retarget uap0.

## DS-protocol injection — WORKING (2026, wake N+4)

**pdebug does NOT speak GDB RSP** — it speaks the binary QNX DS protocol
(radare2 `shlr/qnx/dsmsgs.h` has the full spec). Wire format:
`0x7e <escaped payload> <csum=sum^0xff> 0x7e`, escape 0x7e/0x7d → 0x7d (b^0x20).
Channel setup: `7e 00 ff 7e` (reset) then `7e 01 fe 7e` (debug channel).
DS msgs: `{cmd,subcmd,mid,channel}` + payload; replies cmd 0x21 ok / 0x22 okstatus
/ 0x23 okdata / 0x20 err. Verified live: connect/attach/select/stop/regrd.

Adapter TCP whitelist is ONLY {22,23,80,8000} — all owned by stock services
(inetd, lighttpd, qconn). Workaround: `slay lighttpd; pdebug 80` —
reversible, relaunch `/opt/conn/bin/lighttpd -f /opt/conn/etc/lighttpd-nbt.conf
-m /opt/conn/lib` after.

ARM regset = 68B (r0..r15,cpsr; r[13]=sp r[14]=lr r[15]=pc).
Injection recipe (tools/ssh-vetting/mwu-pdebug.py): attach mwud_pub →
stop → memwr pkt+sa into .bss (0x195768+0x800) → exec-brk at main 0x104024
→ regwr r0=fd r1=pkt r2=len r3=0 sp-=32 [sp]=sa_ptr [sp+4]=16 lr=bkpt
pc=0x12b090 → run → bp hit → regrd (r0=ret) → restore regs → run → detach.
Send-fd = mwud_pub's ephemeral UDP socket (pidin -p PID fds) — reply then
goes to a dead port; fd4 (the *.9196 listener) also works but loops the reply.

Orchestrator: tools/ssh-vetting/wake-uap.sh — whole bring-up in one shot.

## 13. USB AA attempt #1 (2026-11-02, nbt-aad-usb / PR#42) — phone never streams

Setup that WORKS: `vdev-medialaunch` auto-detects OAP-capable phones on plug-in,
spawns `aoap-switch -vvv -b <bus> -d <dev> -c /opt/mm/etc/aoap.cfg -t` which only
mounts `/dev/android<bus><dev>` and waits (start_in_oap_mode: 0). The switch is
then driven by a devctl: `__DIOTF(5,3)` Config-and-Switch carrying a 1540-byte
Identity {protocol, manufacturer, model, description, version, url, serial}.
Stock issuer: NbtMediaMainApp's A4A handler (config seen: 'BMW Group'/'BMW',
url market://details?id=com.bmwgroup.connected.bmw, serial 'VIN', mode 2 +
SET_AUDIO_MODE). nbt-aad-usb also self-issues the devctl when it sees android*
nodes (default 'Android'/'Android Auto', protocol 1).

Observed flow: switch issued -> phone re-enumerates 18d1:0x2d00 -> vdev spawns
`devc-serusb-aoap -vvvvv -b B -d D -l 16384` -> /dev/oap<bus><dev> resmgr
(read=bulk-in, write=bulk-out; one read() = one whole transfer, >= -l buffer).

FAILURE MODE: every session = daemon sends VersionRequest (ch0, 12B) -> phone
sends ZERO bytes -> 2s timeout. Once (first-ever session, stock BMW-identity
switch + media app's OAPReader holding /dev/oap01) the phone sent a 22136-byte
ch18 frame in 4-byte dribbles, then stopped. URB writes to the phone also
time out (OAP-DRV io_oapwrite "Connection timed out" ustatus 6000000) — the
phone's bulk-out endpoint is never drained => the phone-side AA service never
claims the accessory, matching the phone's permanent "connecting" spinner.

Tried, all identical 0-byte result: NBT_USB_DIRECT 1 and 0 (stock driver path),
identities 'Android'/'Android Auto' and 'BMW Group'/'BMW' (patched build
protocol=2 + SET_AUDIO_MODE, staged as /var/opt/aa/nbt-aad-usb2), My BMW app
disabled (accessory-url app-association theory), BT delete+re-pair to HU,
phone reboot. BT re-pair DID make the phone finally attempt AA ("connected"
notification flash) — suggests phone-side AA is BT-gated, not accessory-gated.

PR #42 upstream notes (author): experimental, "connecting your phone is a bit
buggy, doesn't handle reconnecting"; only ever tested NBT_USB_DIRECT=1.

NEXT LEVER: phone-side logcat (CAR.SERVICE tag — the daemon names it itself)
to see why AA service won't claim/stream on the accessory. Needs ADB — grab
over WiFi-ADB at home (car USB is in accessory mode, adb iface absent).
Suspects: accessory-app association never fires with either identity; AA
service wants the SPP setup FIRST then told to use USB (ordering); the
'BMW Group' url pointing at a disabled app leaves the accessory unclaimed.

## 14. Cinemo/NME official-AA recon (2026-11-02, pulled binaries)

libNmeSDK.so (630KB, /opt/mm/lib) exports a COMPLETE ICinemoAndroidAuto*
interface family — 11 interface groups / 12 iids:
  AudioSource, Bluetooth (OnReadyForPairing, RequestDelayedPairing,
  SendAuthData), Input (ReportKey/Absolute/Relative/Touch/TouchPad/
  SendKeyEvent/SendTouchscreenEvent), MediaBrowser, MediaPlayback,
  Navigation, Notification, Phone, Radio, Sensor, VendorExtension.
Cinemo (14.7MB, /opt/mm/bin) embeds ICinemoShellAndroidAuto + a full AA UI:
  res://androidauto.png, Xui/player/player_androidauto.xml,
  Xui/player/menu/androidauto.png + androidauto_disabled.png.
NmeCmdLine (113KB) exists as the SDK CLI — next: dump its command help to see
whether an AndroidAuto session can be started/inspected directly.
io-media-cinemo + io-media-generic run stock; Cinemo binary itself was NOT in
pidin — likely started on demand (or dormant entirely).

Meaning: the ID5 unit carries a complete, dormant official AA engine —
UI included. Unknown yet: activation path (DSI service? NmeCmdLine command?
FSC flag?), whether it's license/FSC-gated, and whether it projects over the
car's own BT+WiFi or the AOAP USB path (its presence would explain why the
A4A handler exists and owns /dev/oap* first).

EAF configs pulled (eaf_default/mini.cfg): [eaf] section registers
protocol=com.bmw.a4a .. com.bmw.a4a15 — BMW's Android-Auto external-accessory
protocol handlers declared in the media app's config (file header notes it's
the iPhone-app-communication mechanism). io-media-cinemo.cfg, Media.cfg,
cinemo_options.xml, aoap.cfg all pulled to pulled/. Activation path still
open: likely NbtMediaMainApp A4A -> Cinemo projection, gated by coding/FSC —
check vehicle coding (A4A option) and whether NbtMediaMainApp registers a DSI
service for AA when coded.

Addendum: setting phone USB preference to "connected device" (host-controlled)
made the phone ACTIVELY attempt AA over USB for the first time (notification
"connected to android auto" ~seconds, HU overlay VISIBLE ~15s, menu entry
"connected") — previously AA only engaged on BT connect. Session still ends:
VersionRequest out, 0 bytes in, 2s dead. So phone-side AA service DOES run on
USB once USB-mode is host-controlled — the failure is inside the transport
negotiation (phone never writes), not accessory claiming. Logcat will say why.

LOGCAT BREAKTHROUGH (phone-side, 2026-11-02 late): the phone is NOT waiting on
the USB accessory at all — it runs the WIRELESS-AA setup over the car BT:
GH.WPP.RFCOMM connects to car BT <car-bt-mac> on our 'AndroidAuto' SPP
uuid (4de17a00-52cb-11e6-bdf4-0800200c9a66), then waits 12 s for the head
unit's first WPP message -> WIRELESS_SETUP_WPP_VERSION_REQUEST_TIMEOUT,
restart loop (attempt #128+). So: phone insists on the BT SPP WPP handshake
first; that handshake is what tells it which transport to use.

Root cause in daemon (main.cpp ~line 899): when NBT_USB=1 AND usb->phonePresent(),
the wireless loop does `usleep(500000); continue;` — parks the whole BT/SPP
accept path ("Bluetooth links are left alone"). The designed toUsb path
("releasing the Bluetooth link, Android Auto goes over USB" ~line 1048) lives
INSIDE the per-link handler that never gets invoked while parked => the phone's
RFCOMM connects (kernel accepts) but nobody answers the WPP handshake =>
0-byte sessions on the accessory channel + infinite phone-side WPP timeouts.

Also explains the 4-byte dribble on the one early session: phone probably
wrote its WPP first message bytes.

FIX CANDIDATES (not yet applied):
a) In USB mode, still ACCEPT the SPP link and let the toUsb release path run
   (close the link -> phone gives up wireless -> commits to accessory).
   Minimal: replace the `if (usbPhone) continue` park with accept+release.
b) In USB mode, don't register the 'AndroidAuto' SPP service at all (kill the
   RFCOMM bait) -> phone can't attempt wireless setup, must use accessory.
   Which the phone prefers is an empirical question; (a) is closer to
   upstream's own intent.

DEEPER CAUSE: the CAR'S OWN stock BT advertises wireless-AA — phone's AA
settings list "BMW nnnnn wireless android auto available" even with our SPP
service unregistered. The stock 'AndroidAuto' SPP SDP record exists from boot
(our daemon's first register hit GapErrorCode=5 already-registered, then
stole it via Deregister+Register). Phone ALWAYS prefers wireless-AA while
that bait exists => never claims the USB accessory channel.

IN PROGRESS (patch step 2): kill-bait build — make USB-mode daemon deregister
the stock 'AndroidAuto' SPP service at startup so the phone has no wireless
path and must commit to the accessory. Patch plan (usb-pr42 worktree):
- bluetooth_manager.cpp: add `volatile bool g_sppKillOnly = false;`
- onGapAttached (~line 177): if killOnly -> setState(SPP_DEREG_SENT) +
  sendDeregister(c, "usb kill bait") instead of SPP_REG_SENT + sendRegister
- DEREG response handler (~line 256): if killOnly -> setState(SPP_DONE)
  (never sendRegister)
- new method killAndroidAutoSdp() (class in include/bluetooth_manager.hpp:28,
  mirror registerAndroidAutoSdp but set g_sppKillOnly before g_gap.open)
- main.cpp USB branch (~line 1653): call bluetooth.killAndroidAutoSdp()
Build: docker run --rm --platform linux/386 -v $PWD/milestone-7:/src -w /src
  andreluizpd/qnx-qcc-docker:6.5.0 make nbt-aad
Deploy: plink -i ~/.ssh/nbt_knock.ppk -P 3804 root@169.254.199.119
  'cat > /var/opt/aa/nbt-aad-usb3' < milestone-7/nbt-aad; chmod +x; launch
  with NBT_USB=1 env block; user replugs phone; watch nbt-usb*.log for bytes.
NOTE: killing the stock SPP record also removes the wireless-AA bait for the
BM3 path while usb3 runs — restart/reboot restores it (re-registration on
next boot). Reversible by design.

KILL-BAIT DEPLOYED (usb3 + NBT_SPP_REREG_S=0): daemon deregisters stock
'AndroidAuto' SPP at startup (GapErrorCode=0 first run; =10 already-gone
after). STILL "BMW nnnnn wireless android auto available" on the phone even
with a brand-new BT pairing + force-close => the wireless-AA advertisement
is NOT (only) the 'AndroidAuto' SPP record — either another SDP service
advertises it, BLE adv, or the phone caches per-BT-MAC capability
(GH.CarSvcDataStorage). NEXT: enumerate the car's SDP table (sdptool browse
<car-bt-mac> — needs sdptool/bluez on laptop, or bluetoothctl
discover+info), or wipe AA app data on the phone to clear the cached
association. usb3 daemon currently running on HU with bait-kill + REREG=0.

POST-WIPE TEST: with AA app data wiped + no wireless bait anywhere (stock SPP
deregistered, ours not registered, REREG=0), sessions still die at 2s with
0 bytes — the phone now never engages AA on the accessory either (no
"connecting" spinner, no AA entry in settings = phone sees no head unit
association at all). Working theory now: a wiped-AA phone needs the
accessory's own app-association to launch AA (FirstActivity via
USB_ACCESSORY_ATTACHED only fired when identity had a matching intent/url?).
Suspect: the generic 'Android'/'Android Auto' identity + github URL has no
installed handler; the phone needs EITHER a head-unit app association or a
fresh pairing flow that our accessory identity doesn't trigger. Next: try
identity variants (url=market://details?id=com.google.android.projection.gearhead
i.e. point AT the AA app itself!) — accessory url that resolves to gearhead
should make Android hand the accessory to AA directly. OR accept the BT
handshake and see if toUsb release pushes it to the accessory.

BYTEFLOW BREAKTHROUGH (nbt-usb11.log, phone AA data wiped + all bait killed +
Android/AndroidAuto identity): session carried real AAP — 3602B in / 2801B
out, ch 3/10/11/13 frames, 'Streaming Media Frame 1 on Channel 11'. Phone-side
logcat: USB_ACCESSORY_ATTACHED -> CarSetupService -> handoff complete
(success). Session dies at ~3s on: 'Failed to decrypt encrypted frame (35
bytes): SSL3_GET_RECORD: decryption failed or bad record mac'.

WINNING CONFIG: NBT_USB=1 NBT_SPP_REREG_S=0 + kill-bait daemon (deregisters
stock AndroidAuto SPP) + NBT_USB_MANUFACTURER=Android NBT_USB_MODEL="Android
Auto" NBT_USB_URL=https://github.com/Sq00ky/Supra-iDrive6-Android-Auto.
Identity matters: 'BMW Group'/'BMW' => phone logs 'Accessory is not valid'
(CAR.SETUP.LITE) and rejects; generic Android identity accepted.

REMAINING BUG: every encrypted RX frame fails SSL_read bad record mac =>
cipher/handshake mismatch (probably aasdk SSL channel init or USB framing
feeds SSL wrongly — wireless TLS-over-TCP vs USB framing differ). Debuggable
offline in aa_session.cpp / aa_cryptor.cpp.

SSL ANALYSIS: handshake completes (VersionResponse 1.7 -> ClientHello 2376B ->
our 1245B -> phone 51B -> 'TLS Handshake Complete!'). First RX record =
VALID TLS1.2 record (17 03 03 00 1E + 8B explicit nonce = AES-GCM) but
bad-record-mac on EVERY frame => traffic keys differ from phone's even though
handshake reported complete. Suspects: (a) handshake bytes corrupted by USB
transport fragmenting (earlier 4B-dribble bug) -> wrong master secret yet
Finished still verified? unlikely but check; (b) daemon forces TLS1.2 client
mode while phone negotiated something else; (c) cryptor re-init mid-session.
NEXT DEBUG (offline): dump full handshake hex (2376B in / 1245B out) from
nbt-usb11.log, diff vs openauto/aasdk's TLS path; check aa_cryptor.cpp TLS
version pinning + whether records on ch>0 share one SSL or need per-channel
contexts. aa-session decrypt at aa_session.cpp ~line 901.

CBC-CIPHER TEST (nbt-usb12.log): pinning cipher list to AES128-SHA etc
(NBT_SSL_CIPHERS env added to aa_cryptor.cpp) did NOT fix bad-mac — phone
still logs SSLV3_ALERT_BAD_RECORD_MAC at 23:40:42. NOT a GCM impl bug.

SHARP PATTERN from frame trace: ch0 (control) encrypted frames with flags
0x0B decrypt FINE (937B + 57B records OK). ALL failures are data-channel
frames with flags 0x0F (bit 0x04 extra). Phone negotiated AAP v1.7 (we
requested 1.6) — bit 0x04 may mark newer first-frame length semantics or a
newer frame type our parser mishandles, OR data-channel TLS records arrive
MISORDERED in the shared SSL stream (USB transport interleaving — TLS record
seq is implicit, reorder = permanent MAC failure; symmetric error on the
phone side = our writes also misordered/misframed: phone log says 'Did not
decrypt all the bytes in the input buffer!' = partial/extra record bytes).

ALSO: multiple /dev/oapN sessions churn (oap01/010/012/014/015) — verify only
the LIVE node's session runs; stale-node sessions could feed records to the
wrong SSL object.

NEXT (offline): read aasdk FrameHeader flag semantics for bit 0x04 (v1.7+);
check whether encrypt output packs >1 record per frame; verify USB reader
delivers frames strictly in wire order (no per-channel reorder); pin
VersionRequest to an older minor (env?) to test if 0x0F frames disappear.

ROOT CAUSE FOUND (offline analysis of nbt-usb12.log): NOT SSL/framing — the
stock media app's OAPReaderThread (NbtMediaMainApp) opens the same /dev/oap*
node and STEALS queued bulk-in transfers. usb_accessory.cpp comment confirms:
'car log 2026-10-31: 12 TLS records in a row missing in a burst of channel
opens'. TLS record seq is implicit -> one stolen record = bad-record-mac on
every subsequent record, and our outbound records get eaten too (phone logs
'Did not decrypt all the bytes in the input buffer!'). ch0 records decrypted
because they arrived before the burst; ch1/ch2 opens also passed, ch12+ died
in the burst. Sequence-jump diagnostics (9736...->10891...) were a red
herring — checkRxRecord reads GCM explicit-nonce bytes; with CBC ciphers the
field is ciphertext IV, not seq.

FIX = NBT_USB_DIRECT=1 (already in the build): daemon claims AOAP driver via
UsbDirect.claimAoapDriver(), stock devc-serusb-aoap stopped (stopAoapDriver),
no /dev/oap node exists for the media app to steal from, io-usb delivers
transfers strictly in order.

NEXT CAR TEST (one command, usb3 already on HU):
  slay nbt-aad + serusb-aoap, then:
  NBT_USB=1 NBT_USB_DIRECT=1 NBT_USB_CLAIM_DRIVER=0 NBT_SPP_REREG_S=0 \
  NBT_USB_MANUFACTURER="Android" NBT_USB_MODEL="Android Auto" \
  NBT_USB_URL="https://github.com/Sq00ky/Supra-iDrive6-Android-Auto" \
  NBT_LOG_FILE=/var/opt/aa/nbt-usb13.log NBT_VERBOSE=1 \
  AOI_CONFIG=/var/opt/aa/aoi/ MM_RENDERER_CONFIG=/var/opt/aa/mm-renderer.conf \
  LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib:/fs/sda0/opt/mm/lib:/fs/sda0/opt/conn/lib/carplay:/opt/sys/lib \
  /var/opt/aa/nbt-aad-usb3 &
  -> replug phone; expect records past ~#6 to keep decrypting + video frames.

CINEMO/NME ACTIVATION ANALYSIS (offline, pulled binaries): the dormant engine
needs per-unit provisioning as NME metatypes (libNmeSDK.so strings):
ANDROIDAUTO_MAKE/MODEL/YEAR/ID/ROOT_CERT/CLIENT_CERT/PRIVATE_KEY/
DRIVERPOSITION. No pulled binary registers the stock 'AndroidAuto' SPP SDP
name, none calls CinemoCreateProjection => the activator is NBTMediaMainApp
(/fs/sda0/opt/mm/bin/NBTMediaMainApp, 8.7MB, NOT pulled). eaf_default.cfg
a4a* entries are Apple iAP protocols (bundleseedID) — not AA.
Connectivity binary has zero AndroidAuto strings (its SPP work is IAP2/CarPlay).

NEXT-SESSION PULLS: /fs/sda0/opt/mm/bin/NBTMediaMainApp; also grep -rl
"AndroidAuto" /opt/mm/bin /opt/mm/lib on the HU to find the SPP registrar +
any AA config file populating the metatypes (the cert/key could be FSC-bound
=> official AA may be unlockable purely by config/coding, or the certs may be
provisioned but gated by a coding flag).
