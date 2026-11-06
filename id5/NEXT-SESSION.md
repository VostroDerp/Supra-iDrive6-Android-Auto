# Next car session

**Goal: a daily build that doesn't go black, the pause fix tested in the car, and everything the now-playing PR
(#51, option A) needs from the car.** The base moves to v1.9.19, which
moves the volume pill off the Android Auto picture's surface (the black screen with the pill showing, run 20, and
again on a drive on 2026-11-06). The pause fix (#110, branch `browser-hold`) gets its first car test, with the data
#110 still needs: what the bus shows when a source is picked in the iDrive menu.

Steps run in order, one change each. A step that fails is undone and the session moves on to the next step that
doesn't depend on it. Note the clock at the start of each step. Results go into the run log at the bottom.

`C=id5/tools/car/car.sh` (from the repo root), `U=../Supra-iDrive6-Android-Auto-usb/milestone-7` (staged builds).

## Builds

| On the head unit | Build | What it is | Status |
|---|---|---|---|
| `nbt-aad-base` (there now) | v1.9.09-most-base13-pr91 | upstream main e279e55 + Tom's #91 | daily build since run 25; black screen seen once on a drive |
| `nbt-aad-base13-pr91` (there) | the same | the way back | |
| `nbt-aad-base14` (push `$U/nbt-aad-base14`, md5 bf0b789d) | v1.9.19-safety-base14 | upstream main a9fac36 + #91 (0b38f62) + `browser-hold` (a55c82f, off unless `NBT_BROWSER_HOLD=1`) + `aa-media` (f2d93df: now playing published in `/dev/shmem/nbt-aad-media`, `[Media]` log lines; `NBT_MEDIA_INFO=0` = off) | host tests pass, QNX build, not car-tested |
| iDrive app (there now) | rhmiapp-22b | | works with v1.9.19 |
| iDrive app, step 5b (`$U/rhmiapp-38-np/`, `libNbtAad.so` md5 6e2f3844) | rhmiapp-38-nowplaying | upstream rhmiapp-37 + `aa-media` 7ca97a1: `NbtAad_Media.xml` (Now playing screen, cluster list, iDrive now playing line), off unless `/var/opt/aa/now-playing` exists | host tests pass, QNX build, not car-tested |

The iDrive app stays rhmiapp-22b unless step 5b goes ahead.

## Before going to the car
- [ ] Upstream, right before leaving: new PRs, issues and replies on #110 and #91. Merged work that overlaps a step
      changes that step or the base build.
- [ ] Battery charger packed. Laptop and phone on the home Wi-Fi (fallback unlock; adb for logcat).
- [ ] `id5/tools/a4a/a4a_probe.sh --fetch` (the A4A probe's libraries, for step 5c).

## Getting in
1. ENET cable in, charger on. `$C status`, then `$C knock`.
2. The first `$C sh` opens one shared SSH connection (one wait of ~15 s); every call after it takes well under a
   second for 15 min of idle. `$C status` shows it. If OpenSSH can't connect, car.sh says so and uses plink (~15 s per
   call): note the message, carry on, and look at `id5/local/car-ssh.err` at the desk.
3. If the knock port is closed: `$C fallback status`, `$C fallback unlock`, start knockd through it
   (`$C fallback sh '/var/opt/aa/knockd /var/opt/aa/knock.conf >>/var/opt/aa/knock.log 2>&1 </dev/null'`), `$C knock`.

## 1. Read-only first (~5 min)
- [ ] **The drive's black screen, before anything writes the logs:** `$C pull /var/opt/aa/nbt.log
      id5/local/recon/drive-2026-11-06/nbt.log`, the same for `nbt.log.1` (if there) and `/var/opt/aa/mmr.log`, and
      `$C sh sloginfo > id5/local/recon/drive-2026-11-06/sloginfo.txt`. At the desk: the session that went black,
      `[VolOsd] first frame ... got z=0` (runs 19, 20, 22: z=0; working runs: z=999) or the renderer log going silent
      (run 22).
- [ ] **Where SSH's 15 s go:** `$C ssh-diag`. A gap of ~5/10/15 s just before `Authenticated` is sshd's DNS lookup
      of the laptop (step 2 fixes it); a gap anywhere else: note the lines, skip step 2.
- [ ] `$C sh 'cat /var/opt/aa/sshd_config; pidin ar | grep -i sshd'` (the live config and how sshd runs).

## 2. sshd without the DNS lookup (no reboot, ~10 min; only if step 1 showed the gap)
The shared connection from "Getting in" stays open the whole step: it is the way back if the new sshd doesn't start.
- [ ] `$C sh 'cd /var/opt/aa && cp sshd_config sshd_config.v1 && cp sshd_config sshd_config.new && echo "UseDNS no" >> sshd_config.new && /usr/sbin/sshd -t -f sshd_config.new && echo SYNTAX OK'`.
      No `SYNTAX OK`: stop here (the live file is unchanged).
- [ ] `$C sh 'cd /var/opt/aa && mv sshd_config.new sshd_config && kill $(cat sshd.pid)'` (the listener only; the
      shared connection is a separate process), then `python3 id5/tools/knock/knock.py 169.254.199.119` (knockd starts
      sshd with the new file), then `$C ssh-diag`. Expect: total ~1 s.
- [ ] Way back: `cp sshd_config.v1 sshd_config`, kill and knock the same way; or the fallback unlock.

## 3. base14, hold off (no reboot, ~15 min)
- [ ] `$C push $U/nbt-aad-base14 /var/opt/aa/nbt-aad-base14`.
- [ ] `$C run 26 nbt-aad-base14 NBT_VEHICLE=bmw NBT_AUDIO_MODE=browser NBT_MOST_LOG=1 NBT_MEDIA_DUMP=3`. Check:
      - picture; `[VolOsd]` lines name surface 1281 (first time on an ID5: the pill was moved for the Supra);
      - volume up/down on the wheel and the knob: the pill shows, the picture stays;
      - reverse with the parking sensors, then back to drive: Android Auto steps aside and comes back;
      - a quick controller round (turn, push, BACK, MENU);
      - the iDrive app's `hold:` lines in `sloginfo` behave as with base13 (22b with v1.9.19);
      - with music playing and a couple of skips on the phone: `[Media] track: ...` and `[Media] album art N: ...`
        lines, `$C sh 'cat /dev/shmem/nbt-aad-media'` shows the song, and `$C pull /var/opt/aa/aa-art-1.png` (or `.jpg`,
        the log line names it) into `id5/local/recon/run-26/` (step 5 uses these).
      Prediction: all as base13, the pill above the picture. A black screen: note the time, `$C collect` anyway, and
      restart the renderer (`pidin ar | grep mm-renderer`, kill it, the start-up script's step 3 line).

## 4. Pause fix (#110): `NBT_BROWSER_HOLD=1` (no reboot, ~25 min)
`$C run 27 nbt-aad-base14 NBT_VEHICLE=bmw NBT_AUDIO_MODE=browser NBT_BROWSER_HOLD=1 NBT_MOST_LOG=1`. Before each
part: `$C sh "echo '==== MARK 4x <what> ====' >>/var/opt/aa/nbt-usb27.log"`, and tell me what the car did.
- [ ] a. Music playing, pause for 60 s. Prediction: stays paused; no `releasing the browser audio source` line.
- [ ] b. Pause and play quickly, 5+ times in 30 s. Prediction: never resumes by itself (run 19 did, after 0.3-2.8 s).
- [ ] c. MODE once. Prediction: the radio (or the next source) sounds, the phone pauses: `the driver changed the
      audio source (MODE)`, `audio focus -> phone: LOSS`. Note whether AA stays on screen (it went to iDrive's source
      screen before).
- [ ] d. Play in Android Auto. Prediction: the phone asks for focus, AA takes the source, the radio stops.
- [ ] e. Start a Maps route, so a prompt plays while the radio is on (after c again). Prediction: the prompt cuts in,
      then the radio comes back.
- [ ] f. **For #110, the menu source change:** one MARK each, ~5 s apart: iDrive menu -> Radio, -> Bluetooth audio,
      -> Android Auto. Prediction: no release (not decoded yet), so the radio stays silent: the capture is the point.
- [ ] g. A call (ring the phone from another phone), answer, hang up. Note: does the music come back by itself.
- [ ] h. Unplug, replug, and pause within 10 s of the music starting (run 20's quick resume at session start).
- [ ] Then `$C collect 27`. At the desk: the menu source messages, then the menu trigger on `browser-hold`.

## Then the base (if 3 passes; 4 only decides the env setting)
- [ ] `$C sh 'cd /var/opt/aa && cp nbt-aad-base14 nbt-aad-base.new && mv nbt-aad-base.new nbt-aad-base'`
      (`nbt-aad-base13-pr91` is the way back). The hold goes into `/opt/aa/nbt-aad.env` only if 4 a-e passed: that
      needs the `/fs/sda0` remount (the install lines at the top of `tools/hu-install/nbt-aad-usb.sh` show how).
- [ ] Reboot with the phone plugged in. Check: picture without a replug (#91), knockd up.

## 5. Now playing (#51, option A): what the iDrive side needs (~30 min, after 3; no reboot)
Goal: everything the iDrive app work needs, so the PR can be written at the desk. Step 3's run already gave the
phone's side (`[Media]` lines, album art files: format, size).
- [ ] **Media keys reach the phone** (one run, keys remapped for the test only):
      `$C run 28 nbt-aad-base14 NBT_VEHICLE=bmw NBT_AUDIO_MODE=browser NBT_KEY_OPTIONS=87 NBT_KEY_COM=88`.
      Music playing: OPTION = next track, COM = previous track (MARK before each, 3 presses ~3 s apart). Prediction:
      the track changes each time; `[AASession] key -> phone: key=87 down/up (sent)`.
- [ ] **Where iDrive's screens live** (read-only):
      `$C sh 'ls -l /opt/hmi/bin /opt/hmi/bin/zipped /opt/mm/bin/NBTMediaMainApp; ls /opt/hmi/ID5/data/ro/Plugins; ls /opt/hmi/lib /opt/mm/lib 2>/dev/null | grep -i -E "a4a|rhmi|hmi|av|etch|connector|plugin"'`,
      then `$C pull` into `id5/local/pulled/hmi/`: HmiMain3D, NBTMediaMainApp, the `*a4a*` libraries and the plugin
      connector (manifest: `pluginAppConnector` Etch, id 210; whatever library holds it). Right away at the laptop
      (5b f needs it): the `bmw::Variant` type codes for int and bool, next to the known 0x22 text / 0x28 TextID /
      0x2b ImageID. At the desk: the XML parser's tags (`instrumentCluster`, `audioHmiState`, `multimediaInfoEvent`,
      `EntICPlaylist`) and how an app becomes the entertainment source.
- [ ] **Any stock screen that already uses those tags**:
      `$C sh 'for f in $(find /opt/hmi /var/opt/hmi -name "*.xml" 2>/dev/null); do grep -l -i -E "instrumentCluster|audioHmiState|EntICPlaylist" "$f"; done'`;
      pull what it lists.
- [ ] **The car's audio sources** (read-only): `$C sh '/fs/sda0/opt/mm/bin/audioconn 2>&1 | tail -n 30'` (its
      options), then its list of all connections and sources, once with Bluetooth audio playing and once in browser
      mode. Looking for the source a connected app (A4A) plays through, next to `0xb8` and `0xc7`.
- [ ] **The cluster list in Bluetooth mode** (base14, `NBT_AUDIO_MODE=bt`, music playing), unfiltered:
      `test_controls -h` first, then `test_controls -t 120 -r` (as for the wheel, without `-k` if that limits it to
      one FBlock), with marks: skip on the phone, scroll the cluster list 3 down / 3 up, pick "next" on the list. Plus
      `sloginfo` grepped for `EntIC|InstrumentCluster|av_|A4A|Multimedia`.
- [ ] AAIdrive over Bluetooth is now its own step, 5d.

## 5b. The iDrive app with now playing (one reboot, ~30 min; after 3 and 5; needs a go on the day)
The car's app goes from rhmiapp-22b to rhmiapp-38-nowplaying with `NbtAad_Media.xml`. That also brings every upstream
app change since 22b, so check a is the plain menu working as before. The daemon is base14 (it publishes the record
and takes `media next|prev|play`). If iDrive misbehaves, SSH still works (sshd doesn't depend on the HMI).
- [ ] Where the app lives, read-only: `$C sh 'ls -l /opt/hmi/ID5/data/ro/Plugins/NbtAad /var/opt/hmi/data/ID5/Plugins 2>&1; df /opt/hmi/ID5/data/ro/Plugins/NbtAad'`.
      If it is not on `/fs/sda0`, stop and work out the install line at the laptop.
- [ ] Backup: `$C sh 'mkdir -p /var/opt/aa/app-22b && cp /opt/hmi/ID5/data/ro/Plugins/NbtAad/* /var/opt/aa/app-22b/ && ls -l /var/opt/aa/app-22b'`.
- [ ] Push: `$C sh 'mkdir -p /var/opt/aa/app-38'`, then `for f in $U/rhmiapp-38-np/*; do $C push $f /var/opt/aa/app-38/$(basename $f); done`,
      md5 of `libNbtAad.so` = 6e2f3844. `$C sh 'echo 1 > /var/opt/aa/now-playing'`.
      (`NbtAad.manifest` points at `NbtAad_Media.xml`. `NbtAad.manifest.plain` is the same app with the plain menu.)
- [ ] Install: `$C sh 'mount -uw /fs/sda0 && cp /var/opt/aa/app-38/* /opt/hmi/ID5/data/ro/Plugins/NbtAad/; mount -ur /fs/sda0'`.
      Reboot with the phone plugged in, base14 as the daemon, music playing.
- [ ] Checks, in order, stop at the first that fails (photos of every screen):
  - a. The Android Auto entry and our menu as before, Android Auto starts. Entry missing = iDrive rejected
       `NbtAad_Media.xml`: `cp NbtAad.manifest.plain` over `NbtAad.manifest` (remount), reboot, and save
       `sloginfo | grep -i -E "NbtAad|xml|pars"`. That alone answers "a native app can't declare these".
  - b. App log (`sloginfo | grep "now playing"`, or the USB app log): `now playing: on`, then
       `now playing: "<song>" - "<artist>" (<app>, playing)` and `album art N -> cover`.
  - c. Our menu, Now playing: what shows (title, artist, album, cover, progress, times). Skip a track on the phone.
  - d. iDrive home screen and the Media side panel: does the song show (`multimediaInfoEvent`)?
  - e. The cluster: anything at all, before the list is on.
  - f. The cluster list: with the int / bool codes from step 5,
       `$C sh 'rm /var/opt/aa/now-playing; sleep 2; printf "int-type N\nbool-type M\n" > /var/opt/aa/now-playing'`
       (the app reads the options when the flag comes back). Log: `cluster list model 221 <- 3 rows`. Does the list
       show? Scroll to "Next >" and pick it: app log `cluster setTrack, Parcel ...`, daemon log
       `[Media] 'next' from the iDrive app`, the track changes. Same for "< Previous".
- [ ] Also in the app log and `sloginfo`: `grep -i -E "ignored|isMyAppActive|A4A"`. Tom found the cluster logs track
      updates as ignored without an A4A audio session (#51): expect that line once the list is on.
- [ ] Way back: copy `/var/opt/aa/app-22b/*` back (remount) and reboot. If a and b passed and only the media screens
      misbehave, the plain manifest keeps rhmiapp-38 with the old menu: decide on the day.

## 5c. A4A from inside the car (option B, the owner's pick): what it needs (no reboot, ~25 min; after step 5's pull)
Option B: our daemon on the head unit logs in to the car's own connected-apps (A4A) server, the way AAIdrive does
through MyBMW's tunnel, and registers an entertainment source, so the cluster follows it. Two separate locks:
- **the login:** a BMW-signed app certificate plus a challenge signed with a key only BMW's phone app holds.
  AAIdrive borrows MyBMW's security service for it. We never take that key out of the app.
- **the transport check** Tom found: `A4AAudioSessionRegistered` needs a real AOA device (USB), `mIsSPPConnected`
  (Bluetooth) or `carPlayState = 1` (CarPlay).

Everything here is read-only except the last item.
- [ ] Processes and listeners: `$C sh 'pidin arg | grep -i -E "a4a|bcl|etch|aoap|connectiv|iap"'`, then
      `$C sh 'ls -l /usr/bin/netstat /fs/sda0/usr/bin/netstat 2>&1; netstat -an | grep -i listen'` (no `head` on
      the HU: an earlier recon lost this output that way). Looking for 4004-4008 (AAIdrive's ports: USB, Bluetooth,
      Ethernet) or whatever the A4A process listens on.
- [ ] At the laptop, from step 5's pull: strings and code around `A4AAudioSessionRegistered`, `isMyAppActive`,
      `modePlayerActive`, `mIsSPPConnected`, `carPlayState`, `DConnBMWAuxInDipo`. Which DSI names and versions feed
      the check (what tells A4A "real AOA device"), and whether that name is free in USB mode, where the stock
      `devc-serusb-aoap` doesn't run. Also `sas_certificate`, `sas_login`, challenge, and any exemption for local
      or internal clients.
- [ ] Are those DSI names taken? Push `$U/dsi_probe` (md5 9e7be8b5) to `/var/opt/aa/dsi_probe` (chmod 755). It opens
      each name as a client only (it never registers anything).
      Control first: `$C sh '/var/opt/aa/dsi_probe 5 MOSTSubSystem.DSYSMOSTServer:4.5'` must say `ATTACHED`.
      Then the names from the strings, e.g. `$C sh '/var/opt/aa/dsi_probe 5 <Role>.<Interface>:<major>.<minor> ...'`,
      in USB mode with AA running and once with AA stopped.
- [ ] The server from the laptop, only if a listener was found and the owner says go (the one item that talks to
      A4A): `$C forward 14004 <port>`, then `id5/tools/a4a/a4a_probe.sh 127.0.0.1 14004` (Etch handshake and
      `ver_getVersion`). Then `--caps` (what it allows before a login). A port that is not Etch:
      `a4a_probe.sh --raw 5 127.0.0.1 14004` (sends nothing, prints what arrives). `--cert <file>` (a certificate
      the owner supplies) and `--try-av` (can add a source: a second go) only after the first answers.
      `$C unforward 14004 <port>` afterwards. Fetch the probe's libraries at home first: `a4a_probe.sh --fetch`.

## 5d. Reference: a real A4A session (option A, ~20 min; MyBMW + AAIdrive on the phone)
AAIdrive over Bluetooth (MyBMW holds the link) while AA runs over USB with base14. It answers whether a real A4A
session and AA coexist on this car, what the cluster shows, and what our browser source does. It is also the
reference for 5c: which port, which calls, in what order.
- [ ] Before: `$C sh 'netstat -an | grep -i listen'` and `pidin arg` (same greps as 5c). Connect MyBMW, start
      AAIdrive, play music in AA. Then again: what opened.
- [ ] Note and photograph: iDrive's apps list, the cluster, iDrive's now playing, the audio source. Skip from the
      cluster list (AAIdrive's "Next >"): does the AA track change?
- [ ] `sloginfo` grepped for `EntIC|InstrumentCluster|av_|A4A|Multimedia|isMyAppActive`, the daemon log
      (`[BrowserAudio]`, `[Media]`), and the phone's logcat if adb sees it (`adb logcat -d | grep -i -E "idrive|bmw|etch"`).

## 6. Wireless over Wi-Fi Direct (no reboot, ~15-20 min)
The car runs its Wi-Fi Direct group at boot even without CarPlay, since Wi-Fi and screen-mirroring coding was switched
on with BimmerCode (findings.md, Wi-Fi; which setting matters is not tested yet). Upstream's `NBT_ALT_WIFI=DIRECT` offers that group to the phone; it was tested
on a car with CarPlay, so this is the first car without it.
- [ ] Read-only: `$C push $U/wfd_probe-main /var/opt/aa/wfd_probe`, `$C sh /var/opt/aa/wfd_probe > id5/local/recon/wfd-probe-<date>.txt`
      (it prints the decoded passphrase: the output stays in `local/`). Is the group visible on the phone always,
      or only during iDrive's screen-mirroring pairing? Which band (`grep -i -E "chan|band|freq|hw_mode"` in
      `/opt/conn/bin/wifidirect_defaults.conf`)?
- [ ] Wireless only, USB unplugged, Bluetooth connected: `$C run <N> nbt-aad-base14 NBT_USB=0 NBT_ALT_WIFI=DIRECT NBT_VEHICLE=bmw`.
      Check: the phone joins the group, the daemon accepts on 172.16.222.1:5277, picture. Check the group name the
      log shows against the real one (upstream calls its name lookup "mostly consistent"). Also watch
      `sloginfo` for the media app (`NBTMediaMainApp`) failing on `/dev/btspp0`, `audioconn -b 0xc7` failures or a
      watchdog: upstream issue #84 sees those before a reset on a CarPlay car in wireless mode.
- [ ] Then both on (`NBT_USB=1`, default `NBT_ALT_WIFI`): plugging in during a wireless session leaves it alone,
      and wireless never takes over a USB session. Making the start-up script run both is a separate change.

## 7. knockd v2 (no reboot for the side-by-side test, ~15 min)
v2 answers only on the cable and the car's Wi-Fi, opens SSH for a limited window, and fixes the review findings
(IMPROVEMENT-PLAN A2, A3, A8, C). Procedure: `tools/knock/README.md`, "Deploying v2 over v1".
- [ ] First, read-only: `$C sh 'pfctl -a filter_OBD -s rules; pfctl -s Interfaces'` (how the cable gets past the
      head unit's packet filter).
- [ ] v2 side by side on a test port with its test configs, knock and log in. Then swap it in, with v1 kept as
      `knockd.v1`, and check a cold boot through the start-up script.

## 8. NbtService (two reboots, ~20 min plus the reboots; after step 7)
An iDrive plugin that starts knockd at boot without the Android Auto app, and holds an "SSH for 15 minutes" switch
(IMPROVEMENT-PLAN A1, A5). Install: `tools/nbtservice/README.md`, to `/var/opt/hmi/data/ID5/Plugins/NbtService`.
- [ ] Read-only first: `$C sh 'ls -R /opt/hmi /var/opt/hmi 2>/dev/null | grep -i manifest'` (where stock
      plugins live; decides the menu placement later).
- [ ] Install, reboot. Check: iDrive is stable, the plugin's row appears, the switch opens and closes the knock
      port.
- [ ] The proof (one more reboot): `$C sh 'mv /var/opt/aa/autostart /var/opt/aa/autostart.off'`, reboot. The
      Android Auto app no longer starts the start-up script, so if `$C knock` works, NbtService started knockd.
      Restore with the reverse `mv` (next boot; start AA from the menu meanwhile).
- [ ] Way back: `touch /var/opt/aa/service.disable`, or remove the plugin folder.

## Later (not this session)
**Tidy-up: only what this car needs, nothing left over from the dongle and Devin days.** One change per test.
- Leftovers on the head unit (from the inventory): sort each file in `/var/opt/aa` and `/opt/aa` into in use or
  not, and move the unused ones to `/var/opt/aa/attic-<date>/` (not deleted until a few sessions run without them).
  Expected: `iap2.cfg`, `start-aa.sh`, the old test daemons (`nbt-aad-usb`..`usb8`, the old release `nbt-aad`),
  Devin's patched Wi-Fi Direct test programs and their configs, and, once #65 is in the base, `mm-renderer.conf`,
  `aoi/` and `mmr.log`. Never moved: knockd, its config, keys and logs, sshd and its config, `nbt-aad-base`,
  `autostart`.
- The settings into `nbt-aad.env`: `NBT_USB`, `NBT_USB_DIRECT` and `NBT_VEHICLE` go from the wrapper to the env
  file, the upstream way.
- Test without `NBT_SPP_REREG_S=0` (a dongle-era workaround; upstream default 5), then without the extra
  `LD_LIBRARY_PATH` (set in both the wrapper and the env file), then without `NBT_VERBOSE=1`, `AOI_CONFIG` and
  `MM_RENDERER_CONFIG` in the env file.
- #65 is merged (2026-11-04): drop the renderer start from the wrapper. Then replace its `unset LD_PRELOAD` with
  upstream's `NBT_CHILD_NO_PRELOAD=1` (#84) in `nbt-aad.env`, one change per test (UPSTREAM-WATCH has the trap).
- Update the iDrive app to the release matching the base build: the car has rhmiapp-22b (Nov 1), upstream is at
  rhmiapp-37. 22b works with v1.9.19 (it takes any non-zero overlay as "Android Auto on screen"), but the step-aside
  work since (camera / safety / warning screens, rhmiapp-35..37) is all in the app, and it may matter for G6 (#40).
  Its own step, with a remount of `/fs/sda0`; the repo's `hu-install/plugin/NbtAad.xml` also differs from upstream.
- Then decide whether the wrapper stays (knockd start, log rotation, the base copy) or `/opt/aa/nbt-aad` becomes the
  daemon itself, as upstream installs it.
- In the repo: `tools/legacy/` (the dongle, hotspot and unlock-relay tools) can go once nothing in it is needed for
  recovery; `tools/recon/probe-ap.sh` belongs to the old hotspot work too.

**Other:**
- Which coding Wi-Fi Direct needs, and which one brings the screen-mirroring pop-up. Starting from today's coding
  (BimmerCode backup first), switch one setting off per test (`ENT_MIRACAST_DISPLAY_SETTINGS`, `ENT_MIRACAST`,
  `MIRRORING`, `WLAN_AP`, then `WLAN_WIFI_DIRECT`), reboot, and check: `wfd0` up, the step 4 session, the pop-up.
  Put back any setting whose removal breaks the session. Only after step 4 works, so there is a baseline.
- Change the Wi-Fi Direct passphrase: an old encrypted copy was public in this fork's history, so the real fix is a
  new one. First check whether re-pairing screen mirroring or a reboot changes it.

## Stop rules
- iDrive unstable, or the HMI restarts on its own: stop that step and undo its last change. Don't debug in the car.
- Never leave the car without a known SSH path. The last thing done is a fresh knock and login.
- Two runs failing at the same stage: stop that step and go on to the next independent one.
- After every reboot: note the time, wait for iDrive, then `$C status` before anything else.

## Run log
One line per run: the prediction is written before, the result after. Logs: `id5/local/recon/run-<N>/`.

| N | build | change vs previous | prediction | result | decision |
|---|---|---|---|---|---|
| 13 | usb7 | first run with the driver claim + libusbdi | reaches TLS; no decrypt failures | **Video, working AA.** First session dropped when iDrive powered down for low battery; the reopen hit stale data until a replug (step 3). | USB works |
| 14 | usb7 | `NBT_VEHICLE=bmw` | phone sees a new car, BMW name + exit icon | **Video**, BMW name + icon, one clean session | Use `NBT_VEHICLE=bmw` |
| 15 | usb8 (main + #65) | factory renderer started by the daemon | renderer up, video as 14 | **Video**, same as 14 | #65 works here (no report posted) |
| 16 | usb7 via the start-up script (step 1, 2026-11-05 19:21) | script installed, env cleaned, then a reboot with the phone left plugged in | after the reboot knockd, renderer and daemon start by themselves; picture | **Step 1 passes.** From the menu: knockd once, renderer, daemon, log rotated, picture. After the reboot: knock worked with no fallback; the phone, still in accessory mode from before, stayed silent for 3 tries (the stale case); a replug gave a picture. Controller seen as the older CCS protocol (E5 fkt C20). `audioconn -b 0xc7` fine | Start-up script stays. The silent phone after a reboot is #91's start-up reset case (step 3) |
| 17 | base13 (step 2, 19:30) | upstream main e279e55 instead of usb7, `NBT_MOST_LOG=1 NBT_KNOB_RAW=1` | picture as run 16; every controller input as on usb7; volume display shows | **Passes.** Picture in a few seconds; every controller input works on the #78 code (older CCS protocol); volume display; touch, BT audio + resume, Hey Google, a call, reverse (PDC view), replug. Inputs also reach iDrive (#40): each hard key knocks iDrive off the app's blank state for ~2 s, knob turns then change radio stations. Voice long press = phone assistant over Bluetooth, not AA (`NBT_PTT_ASSISTANT` off). 2a: cluster shows song + back/forward (no queue), both skip, volume, mode work. Wheel decoded unfiltered: `local/recon/wheel-2026-11-05/` | base13 good for the base; #40 data later; run 18 tries the long press |
| 18 | base13 (20:06) | `NBT_PTT_ASSISTANT=1` | long press opens Android Auto's Assistant (dimmed screen, blue bar); the Bluetooth assistant may start too | Long press sends the Assistant key (`[PTT] Assistant key down -> phone`) and AA's Assistant opens, but the phone's Bluetooth assistant starts too (iDrive's own long press). Mic captured and sent each time, 0xc7 re-requested for the reply, yet **no spoken reply**, also for Hey Google in this run (in run 17 Hey Google was fine). No adb, so the phone side is unknown | base13 becomes the base (`nbt-aad-base.usb7` kept), PTT stays off; 2b tests Hey Google first, reply over the AA link |
| 19 | base13 (2b, 20:10) | `NBT_AUDIO_MODE=browser NBT_MOST_LOG=1` | sound over the AA link; Hey Google replies; wheel acts on the BT source underneath | **Sound works**, Hey Google replies (so run 18's silence was the Bluetooth reply path). Cluster shows the last Bluetooth song, arrows don't change the AA song (#51). Volume floor 25: the browser source keeps its own level (0x19) and won't go lower; BT source at 0-9 (#81 item 2). **Bug: pause doesn't stick**: after `NBT_BROWSER_IDLE_S` the source is released, the car goes back to its Bluetooth source, which plays the phone (AVRCP), stream restarts 0.4 s later, volume bar pops (level jump 0 -> 25) | First PR candidate: browser pause |
| 20 | base13 | `NBT_BROWSER_IDLE_S=0` | pause sticks | **Black screen**: session, video frames and decoder fine, overlay visible; replug and menu restart didn't help. The renderer (up since boot, 4 sessions) was stuck | Renderer restarted by hand |
| 21 | base13 | as 20, fresh renderer | picture; pause sticks | **Picture; pause sticks, no volume bar.** Cause confirmed. But held source = radio/other sources silent, MODE hides AA | Fix at the desk: hold through a pause, release on a source change (MODE 15/02 fkt 513, bus source status) or when AA media stops being the app; test calls on it |
| 22 | base13-pr91 (step 3, 20:50) | Tom's #91 on base13; browser settings dropped (BT, as the base) | the phone, left plugged in from run 21, is reset at start (`resetting it`, `USB reset done`) and gives a picture with no replug | **Start reset works**: `already in accessory mode when the daemon started: resetting it`, phone re-enumerates, picture, no replug. Cosmetic: `usbd_reset_device failed: No such device` right after the phone already left (the reset did work). Then the power button held = a full reboot (not the Nov 3 case): base13 came up, phone silent 3x (as run 16). Replug: **black screen** (daemon fine, renderer log silent since 44.8 s: second renderer stall tonight) | Reboot test with #91 as the base (run 23) |
| 23 | base13-pr91 as the base, via the start-up script (20:58) | reboot with the phone plugged in | at boot the daemon resets the phone and AA comes up by itself | **Passes**: `already in accessory mode when the daemon started: resetting it`, re-switch, TLS, picture at 33.7 s uptime, ~18 s after iDrive was back, no replug. knockd up by itself (3rd cold boot) | The daily case (car off/on, phone left in) needs #91: post on #91 |
| 24 | base13-pr91 (base) | daemon stopped with the picture up, then AA from the iDrive menu | ByeBye sent and answered; menu start resets the phone, picture | **Passes**: `ByeByeRequest to the phone: sent`, `ByeByeResponse`, ended by the phone in 0.89 s; phone left AA cleanly; picture back from the menu. **Step 3 passes**: base13-pr91 stays the base until #91 is merged | Post the car results on #91 (owner's OK first) |
| — | wfd_probe (step 4, read-only, 21:05) | — | group name and passphrase found | wfd0 up 172.16.222.1, dhcpd on wfd0, mwud running (`-c mwu_dev.conf -d wifidirect_defaults.conf`) but **not listening on TCP 9096** (connect refused, errno 261), so no group name from mwud/ifconfig; the WLAN persistency has the car's own name part (value in `local/`). Output: `local/recon/wfd-probe-2026-11-05.txt` (has secrets) | Step 4 deferred by the owner; next time probably `NBT_WFD_SSID` set by hand, and find where mwud listens |
| — | read-only extras (21:05-21:15) | — | — | Inventory saved (`local/recon/inventory-2026-11-05.txt`). **car.sh costs ~15.5 s per call** (even `true`; a backgrounded sleep returns at once), so `car.sh run`'s ~80 s is ~5 calls, not the daemon: skip the knock when SSH answers, batch calls. No minimum-volume setting in the usual config folders. #40: BACK + 3 left + push, and MEDIA + 4 left, did **not** leak (blank state taken back within ~1 s, all inputs to AA; the push hit AA's own exit icon). Captures: `local/recon/input40-2026-11-05/` | Leak suspects: bursts of hard keys, OPTION |
| 25 | base13-pr91 (base) via the menu | `NBT_AUDIO_MODE=browser` added to `/opt/aa/nbt-aad.env` (old file `/var/opt/aa/nbt-aad.env.v2-bt`) | browser audio from the start-up path | `audio mode: browser`, sound over USB (volume floor 25 again) | **End state of the session**: start-up script, base13-pr91, browser audio, knockd v1 |
