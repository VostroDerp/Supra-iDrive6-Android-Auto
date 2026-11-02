# Next car session — ordered checklist

Goal: replace the BM3 dongle. USB is preferred (charges phone); the uAP
(wireless) path is the fallback ladder. SSH independence is FIRST — it gates all future work; everything
else proceeds once it is verified. Work items top-down, mark [x] done /
[f] failed + one line why. All commands assume `tools/ssh-vetting/hu.sh` access
from that dir; HU usually `192.168.0.11`.

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
  1a. KNOCK DAEMON — small boot-time listener on our own port: issues a
      challenge, laptop signs it (ed25519, private key in PM), daemon
      verifies against baked-in PUBLIC key -> spawns dropbear for the
      session. SSH stays off until we knock. Stock trust chain 100%
      untouched -> bavarian + BMW paths all remain as fallback. Focus:
      fast startup, lightweight, robust, secure (constant-time verify,
      fresh nonce, no replayable state).
  1b. ADD our CA ALONGSIDE stock (if NbtDiagHuHighApp shows a multi-root
      cert store) -> real FD2F self-signing while bavarian stays intact.
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
- [ ] if 1a lands: knock client tool for laptop (sign challenge via PM key)

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

## 2. [ ] USB AA (PR #42 — binary built, ready)

Pre-recon (2 min, phone unplugged then plugged):
```
hu.sh sh 'ls /dev/android* /dev/oap* 2>/dev/null; pidin fds | grep -i oap; pidin ar | grep -i aoap'
hu.sh sh 'sloginfo | grep -i "aoap\|oap\|android" | tail -20'
```
- [ ] `/dev/android*` appears when phone plugs in
- [ ] `/dev/oap*` appears (stock driver switching works on ID5)
- [ ] Note whether NBTMediaMainApp holds the oap node

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
- [ ] `ls /dev/android*` node appears after plug with daemon running
- [ ] log shows `switch to accessory mode sent` / `claimed devc-serusb-aoap`
- [ ] `/dev/oap*` node, daemon reads it (`pidin fds | grep oap` → nbt-aad only)
- [ ] AA session projects over USB; phone charges
- [ ] unplug/replug behavior noted (known-buggy per PR TODO)
- [ ] audio works (untested upstream — check iDrive master volume first)
Expected rough edges: reconnect handling, possible NBTMediaMainApp contention.
If NBT_USB_DIRECT claims devc-serusb-aoap and breaks stock media: daemon exit
releases the claim (path-space claim dies with the process).

## 3. [ ] Official AA leftovers recon (Cinemo/NmeSDK) — cheap, same session

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
