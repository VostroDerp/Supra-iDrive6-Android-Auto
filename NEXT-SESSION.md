# Next car session — ordered checklist

## 0. [ ] TOMORROW: USB-AA direct-mode test (read first — the whole session is one test)

Overnight = clean stock boot: stock SPP bait re-registered at boot, usb3
binary persists at /var/opt/aa/nbt-aad-usb3 (kill-bait + NBT_SSL_CIPHERS env +
NBT_USB_{MANUFACTURER,MODEL,URL} envs). Phone AA data is wiped (clean slate).

SEQUENCE:
1. knock: `cd tools/ssh-vetting && python3 knock/knock.py 169.254.199.119`
   (ENET up already? else `HU_IP=169.254.199.119` on hu.sh calls)
2. SSH: `plink -ssh -i ~/.ssh/nbt_knock.ppk -P 3804 root@169.254.199.119`
3. Kill leftovers:
   `for p in $(pidin | sed -n "s/^ *\([0-9]*\).*nbt-aad.*/\1/p"); do kill -9 $p; done`
   `for p in $(pidin | sed -n "s/^ *\([0-9]*\).*serusb-aoap.*/\1/p"); do kill -9 $p; done`
4. Deploy BOTH builds from ~/Desktop/Projects/BMW/Supra-iDrive6-Android-Auto-usb:
   `cat > /var/opt/aa/nbt-aad-usb4` < milestone-7/nbt-aad-usb4-patched  (PR42+patches)
   `cat > /var/opt/aa/nbt-aad-usb5` < milestone-7/nbt-aad-usb5-vanilla (pure upstream)
   chmod +x both.
   ORDER: try usb5 (vanilla) FIRST — upstream's own flow may suffice now;
   if phone still chases wireless bait, switch to usb4 (kill-bait deregisters
   our stale 'AndroidAuto' SPP record).
5. Launch DIRECT mode (no shared /dev/oap — media app can't steal records):
```
AA=/var/opt/aa; CP=/fs/sda0/opt/conn
NBT_USB=1 NBT_USB_DIRECT=1 NBT_USB_CLAIM_DRIVER=0 NBT_SPP_REREG_S=0 \
NBT_LOG_FILE=$AA/nbt-usb13.log NBT_VERBOSE=1 \
AOI_CONFIG=$AA/aoi/ MM_RENDERER_CONFIG=$AA/mm-renderer.conf \
LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib:/fs/sda0/opt/mm/lib:$CP/lib/carplay:/opt/sys/lib \
$AA/nbt-aad-usb4 >$AA/nbt-usb13-stdout.log 2>&1 &
```
   (identity envs no longer needed — defaults are Android/AndroidAuto+github url;
   audio defaults to NBT_AUDIO_MODE=browser -> ConnectionMaster 0xb8 mixer path,
   which bypasses the audioconn 0xc7 CMCommand failure we kept hitting)
6. Replug phone. WATCH: `grep -nE "TLS record|decrypt|Frame RX|Streaming" $AA/nbt-usb13.log`

SUCCESS = TLS records >6 keep decrypting (ch12/13/14 opens respond),
video frames arrive ('Streaming Media Frame'), HU screen shows AA.
FAILURE A (records still drop) = slay NbtMediaMainApp for the test window
(reversible, reboot restores): `slay NbtMediaMainApp` then re-run step 4-5.
FAILURE B (0 bytes again) = phone-side: delete 'BMW nnnnn' AA vehicle entry
AGAIN (it re-caches), force-stop AA app, replug.

DIAGNOSTIC GRAB if it fails: pull nbt-usb13.log + `adb logcat -d -v time >
~/aa-logcat3.txt` on the phone (WiFi-ADB over hotspot; wireless debugging may
need re-enabling). Compare record counts both sides.

Root cause banked in id5.md: stock OAPReaderThread steals /dev/oap transfers;
TLS implicit seq -> 1 lost record = bad-record-mac forever, both directions.
DIRECT mode is the designed fix (io-usb hands transfers in order, no node).

## 1. [ ] SSH-independence recon (while unlocked — HIGH, gates all future work)

## 1. [ ] SSH-independence recon (while unlocked — HIGH, gates all future work)

Context: unlock = UDS FD2E challenge (random per request — ~41 saved sessions
all differ, replay dead) → FD2F RSA-2048 signature (asymmetric — HU only holds
the public key, self-signing impossible) → FDAC enables SSH until reboot. The
bavarian-aa.uk server dependency can NOT be defeated cryptographically — but it
doesn't need to be: FDAC only does something trivial locally (spawn dropbear /
open port 22), and we ALREADY have persistent boot-time code execution via the
nbt-aad autostart. Replicate FDAC's effect in the boot hook → SSH every boot,
server never needed again.

PRIORITY ORDER (user-decided, latest):
  1a. KNOCK DAEMON — DONE 2026-11-02 (deployed + proven 2x cold boots, zero
      bavarian). knockd on dwc0:851 (stock filter_OBD pass, ENET-only) ->
      RSA-2048 sig -> sshd :3804 key-only via /var/opt/aa/authorized_keys.
      Boot hook in start-aa.sh (5x retry). Client: knock/knock.py; keys at
      ~/.ssh/nbt_knock.{pem,ppk}. Original description below, kept for ref:
      small boot-time listener on our own port: issues a
      challenge, laptop signs it (ed25519, private key in PM), daemon
      verifies against baked-in PUBLIC key -> spawns dropbear for the
      session. SSH stays off until we knock. Stock trust chain 100%
      untouched -> bavarian + BMW paths all remain as fallback. Focus:
      fast startup, lightweight, robust, secure (constant-time verify,
      fresh nonce, no replayable state).
  1b. ADD our CA ALONGSIDE stock — NOT DONE (NbtEvoOmapDiagHUHighApp pulled
      to pulled/, not yet analyzed; 1a already covers daily need so this is
      optional/future-cars). (if cert store shows multi-root) -> real FD2F
      self-signing while bavarian stays intact.
      REPLACE-only if no alternative AND a proven one-command restore
      (backup modulus + dd-back script tested BEFORE overwriting).
  1c. USB-STICK ARM — kept as fallback design if 1a/1b stall.

KNOCK DAEMON recon (answers feasibility):
```
hu.sh sh 'pfctl -s rules 2>/dev/null | head -30; cat /etc/pf.conf 2>/dev/null | head -30'
        # is inbound on dwc0/ENET default-denied? pick allowed port or add rule
hu.sh sh 'ls /fs/sda0/usr/sbin/*dropbear* /usr/sbin/*dropbear* /sbin/*dropbear* 2>/dev/null'
hu.sh sh 'pidin args | grep -i dropbear'        # stock spawn args when unlocked
hu.sh sh 'ls /etc/ssh* /etc/dropbear* /root/.ssh* /home/*/.ssh* 2>/dev/null'
hu.sh sh 'pidin arg | grep -i "hsfz\|diag\|6801"'  # find NbtDiagHuHighApp live
hu.sh pull /opt/sys/bin/NbtDiagHuHighApp pulls/   # FD2F verifier -> offline:
                                                # cert store = multi-root? addable?
```
- [ ] pf inbound posture on dwc0 known; knock port chosen (or rule added)
- [ ] dropbear path/args/host-keys known
- [ ] knock daemon written (QNX build: ~200 lines, ed25519 static verify)
      + launched from autostart hook
- [ ] TEST: knock -> SSH opens; reboot without knock -> no SSH (stock);
      bavarian unlock still works alongside
- [ ] verifier binary pulled; cert store = multi-root? -> 1b feasibility
- [x] if 1a lands: knock client tool for laptop (sign challenge via PM key)

1c design detail — USB-STICK ARM (fallback, unchanged):
USB stick carries ONLY an authorized_keys public-key file (safe to lose);
private key lives in user's password manager (the real secret, PM-backed-up).
Boot hook polls for /fs/usb*/aa-ssh/authorized_keys (auto-mounted by stock
io-usb/srm media path, FAT32), copies it into place, starts dropbear with
password auth disabled (-s). No stick → no SSH, stock-like; bavarian unlock
stays the untouched fallback. Public password ts&SK412 becomes dead weight.
Optionally also bind dropbear to dwc0/ENET side only.

Recon (~15 min, needs unlocked SSH which we'll have anyway):
```
hu.sh sh 'pidin | grep -i "ssh\|drop\|inetd\|qconn"'
hu.sh sh 'ls /fs/sda0/usr/sbin/*dropbear* /fs/sda0/usr/sbin/*ssh* /usr/sbin/*dropbear* /sbin/*dropbear* 2>/dev/null'
hu.sh sh 'pidin args | grep -i dropbear'        # how it was launched, by whom
hu.sh sh 'pidin env  | grep -i dropbear -A5'    # env of the spawned process
hu.sh sh 'sockstat 2>/dev/null || netstat -an 2>/dev/null | grep LISTEN'
hu.sh sh 'ls /etc/ssh* /etc/dropbear* /var/opt/*/ssh* /root/.ssh* /home/*/.ssh* 2>/dev/null'  # where authorized_keys + host keys live
hu.sh sh 'pidin | grep -i qconn'               # QNX Momentics agent = alt access
hu.sh sh 'ls /fs/usb* 2>/dev/null'             # USB mount naming (plug a test stick!)
hu.sh sh 'pidin arg | grep -i "hsfz\|diag\|6801"'
```
- [ ] dropbear path + args + authorized_keys location + host-key handling
- [ ] USB mount point name confirmed with a real stick (/fs/usb0? usb1?)
- [ ] hook polls for the stick (media mount may lag boot — same wait pattern
      as the dwc0 loop), copies key, starts dropbear -s
- [ ] TEST: stick in → reboot → SSH works with private key only, password
      login refused; stick out → reboot → no SSH; bavarian still works
- [ ] note whether dropbear can bind dwc0-only (extra hardening, optional)
- [ ] qconn status recorded (port 8000 = Momentics fallback channel)
- [ ] fallback: if stock dropbear can't do key auth, ship our own QNX ARM
      dropbear/sshd build — toolchain available
- [ ] risk window recorded: until this lands, a dead server = locked out

## 2. [~] USB AA (PR #42) — attempted 2026-11-02; BLOCKED phone-side (see id5.md §13)

Pre-recon — DONE:
- [x] `/dev/android*` appears when phone plugs in (vdev-medialaunch auto-fires aoap-switch -t)
- [x] `/dev/oap*` appears (stock devc-serusb-aoap binds the accessory)
- [x] NbtMediaMainApp A4A handler holds /dev/oap* concurrently — real contention
      (documented race in usb_accessory.cpp comments; observed stealing reads)
- [x] switch flow fully mapped: aoap-switch -t mounts /dev/androidNN only;
      devctl __DIOTF(5,3) Config-and-Switch + 1540B Identity drives the flip

Deploy + test:
```
hu.sh push aa-install/nbt-aad-usb /var/opt/aa/nbt-aad-usb
hu.sh sh 'chmod +x /var/opt/aa/nbt-aad-usb'
# check libusbdi exists or push fallback:
hu.sh sh 'ls /lib/libusbdi* /usr/lib/libusbdi* /opt/mm/lib/libusbdi* 2>/dev/null'
# (if absent: hu.sh push aa-install/libusbdi.so.2 /var/opt/aa/libusbdi.so.2
#  and add /var/opt/aa to LD_LIBRARY_PATH in the launch)
hu.sh sh 'cd /var/opt/aa && unset LD_PRELOAD; NBT_USB=1 NBT_USB_DIRECT=1 \
  NBT_VERBOSE=1 NBT_LOG_FILE=/var/opt/aa/nbt-usb.log \
  AOI_CONFIG=/var/opt/aa/aoi MM_RENDERER_CONFIG=/var/opt/aa/mm-renderer.conf \
  LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib:/fs/sda0/opt/mm/lib:/fs/sda0/opt/conn/lib/carplay:/opt/sys/lib:/var/opt/aa \
  ./nbt-aad-usb > nbt-usb-stdout.log 2>&1 &'
```
- [x] `ls /dev/android*` node appears after plug with daemon running
- [x] log shows `switch to accessory mode sent` / `claimed devc-serusb-aoap`
      (daemon self-issues the devctl; phone re-enumerates 0x2d00)
- [x] `/dev/oap*` node + daemon opens it (oap01/03/04 all sessioned)
- [ ] AA session projects over USB; phone charges — BLOCKED: VersionRequest
      sent, phone answers 0 bytes every session, 2s timeout, phone stuck
      "connecting" (writes to phone URB-timeout too => phone never claims
      the accessory channel). Tried: DIRECT on/off, 'Android/AA' vs
      'BMW Group'/'BMW' identities, protocol 2+SET_AUDIO_MODE (patched build
      = /var/opt/aa/nbt-aad-usb2), My BMW disabled, BT re-pair, phone reboot.
- [x] unplug/replug behavior noted (re-enum gives new dev number; vdev refires
      aoap-switch -t but nothing issues the devctl unless our daemon is up)
- [ ] audio works — never reached
NEXT LEVER: phone-side logcat (tag CAR.SERVICE) via WiFi-ADB at home —
car USB can't host adb in accessory mode. PR notes: experimental, buggy
reconnect, only ever tested NBT_USB_DIRECT=1.

## 3. [~] Official AA leftovers recon (Cinemo/NmeSDK) — done 2026-11-02, see id5.md §14

```
hu.sh sh 'ls -l /fs/sda0/opt/mm/lib/ | grep -i "nme\|cinemo\|auto"'
hu.sh pull /opt/mm/lib/libNmeSDK.so pulls/         # if present
hu.sh pull /opt/mm/bin/NmeCmdLine pulls/
hu.sh pull /opt/mm/bin/Cinemo pulls/               # 14.7MB, still worth it
hu.sh sh 'pidin mem | grep -i "nme\|cinemo" | head -20'
hu.sh sh './NmeCmdLine 2>&1 | head -20'            # usage probe
```
- [ ] libNmeSDK.so exists on ID5 firmware (owner saw ID6 — may differ)
- [ ] pulled for offline `readelf -Ws`/`nm -D`/`strings` → ICinemoAndroidAuto
      method surface, service/PPS names, who imports it
- [ ] pidin shows which processes map it (engine live or dormant)

## 4. uAP path — wireless without dongle (fallback ladder)

Only if USB fails or for the wireless endgoal. Order = least custom code first.

- [ ] 3a. `acpd` self-launch (pure stock if it works):
  ```
  hu.sh sh 'LD_LIBRARY_PATH=/fs/sda0/opt/conn/lib/carplay /fs/sda0/opt/conn/bin/carplay/acpd &'
  # watch 30s: ifconfig uap0 / cat /var/opt/conn/carplay/iap2.cfg / sloginfo
  ```
  CAVEAT: real-iPhone route is CLOSED (FSC gates BT advertisement); self-launch
  untested — may still die on license check. Slay after probe.
- [ ] 3b. `WlanDSICommander` — inspect usage/strings for a micro-AP verb:
  ```
  hu.sh pull /opt/conn/bin/WlanDSICommander pulls/  # strings offline
  hu.sh sh 'WlanDSICommander 2>&1 | head -30'
  ```
- [ ] 3c. `mwud` uAP via SSH tunnel (PROVEN tooling):
  ```
  plink -L 9096:127.0.0.1:9096 root@<hu>      # or tunnel to mwud_pub:9196
  ./mwu.py 127.0.0.1 ap_init iface=uap0 ssid=NBTAA-DIRECT key=1234567890 auth=0020 encrypt=0008
  ./mwu.py 127.0.0.1 ap_start iface=uap0
  ```
  Expect `status=0` + `MWPAMOD: Started AP`. Then phone scans → SSID visible.
- [ ] 3d. SDK DSI tool — NOW BUILDABLE (toolchain solved): small C tool calling
  `Wlan_MicroApCfg.WlanMicroApOnOff.requestStart` via DSI. Write when needed;
  sources for DSI client pattern in `milestone-7/src/dsi_client.cpp`.

If uAP lit: write iap2.cfg = our SSID/PSK + uap0 MAC (read live
`ifconfig uap0`), daemon binds 172.16.224.1 → phone joins car Wi-Fi → AA.
Radio caveat: one radio multiplexes uAP↔WFD — dormant Miracast is fine, an
active WFD session steals it.

## 5. [ ] Miracast coding decision (after uAP result known)

- Revert: Miracast/screen-mirroring coding — kills the popup glitch, removes
  wfd0 radio contention. AA needs none of it.
- Keep: A4A-family (`A4A`/`REMOTE_HMI`/`TOUCH_COMMAND`) — menu row depends on it.
- Keep (probably): 6NW/WLAN-hotspot if set — may feed stock AP path.
- FA backup before any coding revert.

## 6. [ ] Optional upgrade: `nbt-aad-main` (latest upstream, self-built)

`aa-install/nbt-aad-main` = upstream main @ dc4adba (built locally): adds
`NBT_AUDIO_MODE=browser` (daemon mixes AA's 3 native channels → car browser
source; ducking + call-priority + no BT-source dependency) AND kombi_nav
(HUD/cluster turn arrows + lane guidance). Deploy as separate binary like
nbt-aad-usb; test browser audio after USB result is known.

## Deferred / watch items

- [ ] Phantom BMW-service call (once) — avoid COM/telephone hardkey during
      session drops; only investigate if it recurs.
- [ ] Car-sleep kills OBD dongle mid-drive — inherent; uAP/USB fixes by design.
- [ ] If USB works end-to-end: autostart wrapper variant for USB mode
      (env flags; skip the dwc0 wait-loop — no IP needed).
- [ ] Repo contribution candidate: id5-adapter-method.md already written;
      +USB results → upstream PR discussion.

## Status log

(append one line per attempt)
- pending: everything below is untested on car

Pull tomorrow: NBTMediaMainApp (/fs/sda0/opt/mm/bin/, 8.7MB) + run on HU:
`grep -rl AndroidAuto /opt/mm/bin /opt/mm/lib` (find stock SPP registrar +
AA config provider). Offline goal: find the metatype provisioning path +
coding gate for the dormant official AA engine (see id5.md Cinemo section).
