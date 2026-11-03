# Handoff — iDrive 5 Android Auto (updated 2026-11-02 end of day)

**START HERE NEXT SESSION: `NEXT-SESSION.md` at repo root — ordered checklist,
authoritative. ITEM 1 = SSH independence (USB-stick armed, key-only dropbear
from the boot hook — design + recon commands all in the checklist). DONE (1a knockd): it removes the bavarian-aa.uk single point of failure. Then USB → Cinemo → uAP →
Miracast → browser-audio binary. FUTURE.md (gitignored, local-only) holds
the bigger ideas log.**

## SSH-independence — checklist item 1 (PRIORITY ONE)
**DEPLOYED + PROVEN 2026-11-02 (cold boot, zero bavarian).** knockd listens
on dwc0:851 (already passed by stock pf anchor filter_OBD — ZERO firewall
changes; uap0 can't reach it). Valid RSA-2048 sig -> spawns sshd -f
/var/opt/aa/sshd_config on :3804, key-only, AuthorizedKeysFile=/var/opt/aa/
authorized_keys. Boot launch: start-aa.sh guarded hook (5x bounded retry).
Proven live: ignition cycle -> knockd auto-armed -> knock.py -> 'K' ->
sshd:3804 -> plink -i ~/.ssh/nbt_knock.ppk root login on hu-omap. Rejects:
garbage + wrong-key sigs logged 'bad knock'. SIGPIPE-on-RST kill fixed
(signal(SIGPIPE,SIG_IGN)). Priv key: ~/.ssh/nbt_knock.pem + .ppk + PM copy.
Pubkey format on HU: raw hex modulus (SDP6.5 PEM parser chokes on new PEM —
knockd reads hex or PEM). Stock untouched: /etc is RO anyway; bavarian FDAC
path fully intact = fallback. Lost priv key -> bavarian -> rotate pair.

Problem: SSH unlock depends on wss://pretzel.bavarian-aa.uk:8443 signing an
RSA-2048 response to the HU's random FD2E challenge (verified: ~41 captured
sessions, all different challenges, ~256B signatures — no replay, no
self-signing possible). If that server dies we are locked out.
Chosen fix (user decided): USB-stick arming. Stick carries an authorized_keys
PUBLIC key file only; boot hook polls /fs/usb*/aa-ssh/authorized_keys (stock
media auto-mount), copies key in, starts dropbear -s (password auth off).
No stick -> no SSH -> bavarian unlock remains the fallback. Private key lives
in user's password manager. Full recon command list in NEXT-SESSION.md item 1.

## State — AA WORKING + PERSISTENT (BM3 dongle path)
- Video + iDrive knob input + audio through speakers all confirmed working.
- Menu entry VERIFIED: Media/Radio -> "Android Auto" row (A4A coding present).
  Press = overlay to front; has Start/Force Stop/Restart.
- Autostart: `/var/opt/aa/autostart` flag -> HMI plugin launches
  `/opt/aa/nbt-aad` ~15s after boot (boot-guard: 3 crash-boots/15min latches off).
- No laptop/SSH needed for daily use. Second phone = just BT-pair; same flow.
- Audio: daemon `audioconn -b 0xc7` grants AND activates BT source on this HU.
  Silent audio = iDrive MASTER VOLUME (separate from phone volume; knob in AA
  adjusts phone only, no on-screen feedback). NBT_BT_PLAY=1 = no-op on ID5.

## On-HU deployed layout
- `/opt/hmi/ID5/data/ro/Plugins/NbtAad/` = 5 release files (menu plugin).
- `/opt/aa/nbt-aad` = WRAPPER SCRIPT: `unset LD_PRELOAD` (HMI preload kills
  children incl mm-renderer -> boot crashes), wait-forever for first non-
  169.254 inet on dwc0 (alias/primary order flips per boot!), start
  mm-renderer, exec `/var/opt/aa/nbt-aad`. Kernel #! works via app spawn().
- `/opt/aa/nbt-aad.env` = app-read env (<=15 lines): iap2.cfg, NBT_AP_IF=dwc0,
  BM3 BSSID, log, AOI/MMR cfg, LD_LIBRARY_PATH incl /fs/sda0/opt/sys/lib
  (REQUIRED for DSI/SppUuidRegister).
- `/var/opt/aa/` = daemon binary, iap2.cfg (BM3 creds), confs, mwud/wfd tools.
- `/fs/sda0` is RO: `mount -uw /fs/sda0` to write, `mount -ur` to restore.

## uAP ladder detail — checklist item 4 (dongle-free wireless)
uap0 is UP at 172.16.224.1 (daemon's default!), DHCP (dnsmasq.conf) + pf
rules + driver (devnp-mv8787) all pre-staged. Only the SSID beacon is off.
Upstream never lights it — reads creds from CarPlay's iap2.cfg (why "CarPlay
required"). Test order next wake:
1. **acpd probe** — `/fs/sda0/opt/conn/bin/carplay/` has the Apple daemon;
   `LD_LIBRARY_PATH=/fs/sda0/opt/conn/lib/carplay ./acpd &`, watch uap0 +
   /var/opt/conn/carplay/iap2.cfg + slog ~30s, slay. AP bring-up may fire
   before the FSC license check (license sits in iAP2 session layer).
   CAVEAT: real-iPhone path is CLOSED (FSC gates BT advertisement); running
   the binary ourselves is untested — may still be dead.
2. **WlanDSICommander** — stock binary in /opt/conn/bin; check usage/strings
   for WlanMicroApOnOff / ap commands.
3. **mwud ap_init via SSH tunnel** — PROVEN tooling: stock mwud listens
   127.0.0.1:9096; `plink -L 9096:127.0.0.1:9096 root@<hu>` then
   `id5/tools/legacy/mwu.py 127.0.0.1 ap_init iface=uap0 ssid=NBTAA-DIRECT key=1234567890 auth=0020 encrypt=0008`
   + `ap_start iface=uap0`. Wire format: [u32 body_len][u32 seq][k=v\n body].
   Fallback: mwud_pub on HU binds 9196; DS-injection last resort.
4. QNX SDK DSI tool (Wlan_MicroApCfg.WlanMicroApOnOff) — BUILDABLE NOW (toolchain solved via docker image).
- If beacon works: write iap2.cfg = our SSID/PSK + uap0 BSSID (uap0 MAC,
  read live: `ifconfig uap0`), daemon binds 172.16.224.1 (NBT_AP_IP can stay
  via wrapper or set explicitly) -> phone joins car AP -> AA, no dongle.
- Known risk: one radio multiplexes uAP<->WFD (Miracast session can steal
  it; dormant = fine). AP-lighting needs automation after proof (check
  WlanDSICommander/sendcall args, or tiny ARM bin when SDK lands).
- After uap0 proven: user reverts Miracast coding (annoying popup); keep
  A4A-family (menu) + 6NW/WLAN-hotspot if set.

## Cinemo/NmeSDK recon detail — checklist item 3
Repo owner reports ICinemoAndroidAuto API in /opt/mm/lib/libNmeSDK.so —
Cinemo's CORE Projection OEM middleware (AA/CarPlay/CarLife, USB+wireless).
CONFIRMED on our firmware: /opt/mm/bin/Cinemo (14.7MB) + NmeCmdLine exist;
Connectivity/scp have zero Android refs (remnants live in mm media stack).
Upstream daemon bypasses it entirely (DSI + mm-renderer).
NEXT WAKE pull list: ls /opt/mm/lib | grep -i nme; pull libNmeSDK.so,
NmeCmdLine, Cinemo; pidin mem (who maps the lib); NmeCmdLine usage probe.
Offline then: readelf -Ws / nm -D (mangled ICinemoAndroidAuto surface),
strings for service/PPS/DSI names, DT_NEEDED importers.
Note: Cinemo supports USB projection — wired AA could sidestep the uap0 AP
problem entirely. FlashXCode sells software-only AA on NBT Evo = proof this
avenue can work. Unknown: completeness/license-gating vs the AP work — keep
both tracks.

## USB path detail — checklist item 2 (PR #42, branch usb-pr42)
+2794/-22 on milestone-7: NBT_USB=1 switches phone to AOAP itself (HU asks
only once at plug-in; PR re-asks via io-usb, never vendor 0424); 
NBT_USB_DIRECT=1 reads via libusbdi + CLAIMS /opt/mm/bin/devc-serusb-aoap
path so stock driver can't start (fixes lost TLS vs NBTMediaMainApp also
opening /dev/oap*). USB+wireless arbitrate one session (cable wins).
ID5 CONFIRMED has full AOAP plumbing: /opt/mm/bin/aoap-switch,
devc-serusb-aoap, io-usb (omap5-xhci+ehci) running, srm -e /fs/usb0.
Draft TODOs: buggy unplug/replug, audio-over-USB untested, full flow needs
first clean car test. NO prebuilt binary (no CI, fork has no releases) —
SOLVED via licensed docker image (see build-vm.md): BUILT nbt-aad-usb
(430KB, armv7le, libusbdi NEEDED) staged at id5/tools/hu-install/
next to libusbdi.so.2 fallback. DEPLOY next wake as /var/opt/aa/nbt-aad-usb
(KEEP release daemon untouched!) with NBT_USB=1 NBT_USB_DIRECT=1 in a test
launch — verify USB recon items first, then plug phone.
CAR RECON next wake (no build): ls /dev/android* /dev/oap* with phone
plugged; pidin fds|grep -i oap; sloginfo|grep -i aoap. If nodes appear,
ID5 matches the PR model.

## Reference (in repo, persisted)
- `id5/tools/hu-install/` = exact deployed wrapper+env+plugin files.
- `id5/local/captures/session-1102/` = recon-1102.txt (941-line HU
  dump: ifconfig -a, /dev, netstat, pidin, conn/sys/mm bin listings, iap cfgs,
  uap0-grep), nbt-session.log (9.4MB), mmr/mwud/wfd logs.
- `findings.md` = deep technical notes: mwud wire format §10, coexistence §9,
  AP bring-up chain (WIFI_steuernActivation->WifiAPCodingValue->NWS_IEEE80211).
- `id5/tools/legacy/hu.sh sh|push|pull` (run FROM id5/tools/ dir!).
  plink only; root/`ts&SK412`; HU ip usually .11 on BM3 net.
- Watch item: phantom BMW-service call once — likely hardkey->iDrive during
  a session-drop window; not reproduced. Session drops = "Phone closed
  socket" every time = CAR SLEEP kills dongle (uap0 path fixes this too).
- HU lacks: head/env/printf/od/nc/perl/python/scp/touch. Has: cat sed ls -l
  ifconfig pidin netstat slay mount.
