# Next car session — ordered checklist

## Tonight — car session 2026-11-03 (evening). Work top to bottom.

`C=id5/tools/car/car.sh` (from the repo root), `U=../Supra-iDrive6-Android-Auto-usb/milestone-7`,
`V=id5/local/deploy/v2-2026-11-03` (knockd v2 + NbtService, see `$V/MANIFEST.txt`).
**MUST** = the point of the session. **IF TIME** = only after every MUST above it is done.

### Stop rules (any time)
- iDrive unstable, or the HMI restarts on its own -> stop that block and undo its last change
  (`touch /var/opt/aa/service.disable` or `rm -r` the plugin dir). Don't debug in the car.
- Never leave the car without a known SSH path. The last thing we do is a final knock + login.
- Two USB runs failing at the same stage -> stop USB for tonight and go on to the next blocks.
- Every reboot: note the time, wait for iDrive, then `$C status` before anything else.
- Battery charger on for the whole session.

### B0. Before going to the car (desk)
- [x] PR #42 tip still d7ba352, PR #65 still 18d402b (checked 11:30): the staged binaries are current.
- [x] Staged: `$U/nbt-aad-usb5-vanilla` (556253 B), `$U/nbt-aad-usb4-patched` (557117 B),
      `$U/nbt-aad-usb6-mmr` (561950 B), and `$V` (knockd v2, its configs, NbtService). Host tests green.
- [x] Desk-rig run A (12:37): `desk_rig-vanilla` (PR #42 tip d7ba352, protocol 1, identity Android / Android Auto),
      Pixel 9 Pro XL, Android 17, AA 17.9.664044. **Video**: VersionResponse 1.7, TLS complete, 0 decrypt failures,
      all 16 channels opened (8 speech and 14 mic too), 532 frames 1280x720, the launcher renders. So phone side is
      proven for run 13: a car failure is QNX side (claim, io-usb, NbtMediaMainApp). Rig B (patched, protocol 2) skipped.
- [-] (was) USER: desk-rig run with the phone (`id5/tools/desk-rig/README.md`: udev rule once, then `make run`;
      then `make run ARGS="--protocol 2"`). It tells us before the car whether the phone accepts our identity,
      completes TLS and starts video. If it fails, run 13's prediction changes. ~15 min.
- [x] Network: the car is in range of the home Wi-Fi. Laptop and phone both on it (adb over Wi-Fi works, logcat
      for `run`), laptop internet for `car.sh fallback unlock` through it, HU over the ENET cable. No hotspot.
- [x] Phone state: AA data NOT wiped, the car is listed in AA settings (as Toyota, see B3 identity note).
      Leave it as is for run 13 (one variable).

### B1. Car setup (MUST, ~10 min)
- [ ] `$C status` -> `$C knock`. Note the HU date and uptime: `$C sh 'date; uptime 2>/dev/null; pidin info | head -3'`.
- [ ] Is the autostarted release daemon running? `$C sh 'pidin ar | grep -E "nbt-aad|knockd|mm-renderer"'`.
- [ ] Push the USB binaries (step 0b.2: usb5, usb4, usb6 under their names).
- [ ] Push knockd v2 side by side (nothing replaced yet):
      `$C push $V/knockd /var/opt/aa/knockd.v2`, `$C push $V/knock.conf.v2test /var/opt/aa/knock.conf.v2test`,
      `$C push $V/sshd_config.v2test /var/opt/aa/sshd_config.v2test`, `$C push $V/knock.conf.v2 /var/opt/aa/knock.conf.v2`,
      `$C push $V/sshd_config.v2 /var/opt/aa/sshd_config.v2`.
- [ ] Stage NbtService (no plugin dir yet): `$C sh 'mkdir -p /var/opt/aa/stage/NbtService'`, then push the five
      files from `$V/NbtService/` there (`libNbtService.so` under the name `NbtService`, as its README says).
- [ ] **Fallback drill, before any reboot** (stock path over ENET, nothing of ours involved):
      `$C fallback status` -> `$C fallback probe` (read-only) -> `$C fallback unlock` -> `$C fallback sh 'hostname'`.
      Proves the way back works over the cable. The stock SSH it opens closes again at the next reboot.
      If it fails: no reboots tonight (B5–B8 skipped), only no-reboot blocks.
- [ ] Push the Wi-Fi sender: `$C push id5/tools/wifi/mwu-send /var/opt/aa/mwu-send` (built, host-tested).
- [ ] Back up what B5/B6 change: `$C sh 'cp /var/opt/aa/knockd /var/opt/aa/knockd.v1; cp /var/opt/aa/knock.conf /var/opt/aa/knock.conf.v1; cp /var/opt/aa/sshd_config /var/opt/aa/sshd_config.v1'`.

### B2. Read-only recon (MUST, ~5 min, changes nothing)
- [ ] Step 0e (interfaces, pf, mounts, /var plugin dir, sshd user/version, knock.log, stock plugin anchors).
- [ ] Step 0b.4 (PR #65's paths and carplay.ini).
- [ ] Menu anchors for A5. For each stock plugin, its `<pluginApp ... insertStateID=...>` line from 0e. USER: say
      which iDrive menu each of those stock rows appears in (e.g. SecurityMenu / ActiveSeating). That maps
      insertStateID -> menu, so we know the Settings (or My Vehicle) value.
- [ ] Our sshd accepts the v2 config: `$C sh '/usr/sbin/sshd -t -f /var/opt/aa/sshd_config.v2test && echo OK'`
      (old OpenSSH may reject an option; fix the config before B4 if so).
- [ ] Wi-Fi state, for the wireless goal: `$C sh 'ifconfig uap0; ifconfig wfd0; pidin ar | grep -i -E "wlan|mwud|wpa|hostap|acpd|scp"'`
      and `$C sh 'ls -l /opt/conn/bin/mwud /opt/conn/bin/mwu_dev.conf /opt/conn/bin/wifidirect_defaults.conf'`.
- [ ] Firmware/I-level string for a future `requirements.md` row: `$C sh 'ls /etc/*ver* /fs/sda0/*ver* 2>/dev/null'`
      and read what's there. If nothing obvious, iDrive's software version screen (USER, photo).
- [ ] `$C pull` the 0e output into `id5/local/recon/improve-2026-11-03.txt` (or pipe it there).

### B3. USB Android Auto (MUST, the priority, ~40 min)
- [ ] Run 13: prediction in the run log first, then `$C run 13 nbt-aad-usb5`. Read the stage table with the
      decision tree (0d). Each next run changes ONE thing the tree names (usb4 = kill-bait, etc.).
- [ ] Desk-rig result (B0) vs car result: same stage? If the rig got video and the car didn't, the difference is
      the QNX side (claim, io-usb, NbtMediaMainApp), which is exactly what 0d's direct-mode rows check.
- [ ] IF SUCCESS, same session, note each one:
      - picture: sharpness and lag;
      - touch, knob (turn, push, tilt), back / MENU keys;
      - media audio (browser mode), nav prompts, the Assistant (mic), a phone call;
      - iDrive and back (MENU key, then the Android Auto entry);
      - reverse gear (camera, then AA back);
      - unplug and replug (reconnect is the PR's known weak spot);
      - time from plug to picture.
- [ ] IF SUCCESS, separate run: BMW identity. With no CarPlay FSC there is no carplay.ini logo/Manufacturer, so
      car_config.hpp falls back to Toyota (the phone lists the car as Toyota, and Devin's run showed a Toyota exit
      icon). `$C run <N> nbt-aad-usb5 NBT_VEHICLE=bmw` (extra args go into the env). It also changes the vehicle id, so the phone sees a NEW
      car (setup prompts again, the Toyota entry stays). Not before USB works.
- [ ] IF SUCCESS: the PR #65 run (0d, last bullet): `slay mm-renderer`, then `RUN_MMR_CONF=0 $C run <N> nbt-aad-usb6`.
      Afterwards restart our renderer (`id5/tools/car/start-aa.sh` step 1) if video stays black.
- [ ] Leave the release daemon as it was found (kill the test binary; the next boot autostarts the release one).

### B4. knockd v2, side by side (MUST, ~15 min, no reboot, v1 untouched)
- [ ] Start v2 on the test ports: `$C sh '/var/opt/aa/knockd.v2 /var/opt/aa/knock.conf.v2test'`.
      It listens on 8851, control 52792, sshd 3805, window 120 s, log `/var/opt/aa/knock-v2test.log`.
- [ ] pf only passes 851/3804, so reach the test ports through the v1 session (no pf change):
      `plink -ssh -i ~/.ssh/nbt_knock.ppk -P 3804 -N -L 8851:127.0.0.1:8851 -L 3805:127.0.0.1:3805 root@169.254.199.119 &`
- [ ] `python3 id5/tools/knock/knock.py 127.0.0.1 8851` -> "KNOCK ACCEPTED" (proto 2, hu nbt-id5).
- [ ] Log in through it: `plink -ssh -i ~/.ssh/nbt_knock.ppk -P 3805 root@127.0.0.1 'echo v2-sshd-ok'`.
- [ ] Open a long session on 3805 (e.g. `login` with `sleep 300`), wait >120 s: knock-v2test.log shows the window
      closing, a NEW login on 3805 is refused, and the long session is still alive.
- [ ] Wrong key / garbage: `python3 -c "import socket;s=socket.create_connection(('127.0.0.1',8851));s.recv(64);s.send(b'x'*256);print(s.recv(4))"` -> `N`, logged once.
- [ ] Clean up: kill knockd.v2 and its sshd (`pidin ar | grep v2test`), kill the tunnel. Pull knock-v2test.log.

### B5. Swap to knockd v2 + cold boot (MUST if B4 passed, ~10 min; fallback ready, B0)
- [ ] `$C sh 'cp /var/opt/aa/knockd.v2 /var/opt/aa/knockd; cp /var/opt/aa/knock.conf.v2 /var/opt/aa/knock.conf; cp /var/opt/aa/sshd_config.v2 /var/opt/aa/sshd_config; chmod +x /var/opt/aa/knockd'`
      (the wrapper still starts knockd at boot, now v2 on 851/3804).
- [ ] Reboot the HU (hold the power button ~30 s). After iDrive is up: `$C knock` -> v2 knock accepted, login works.
- [ ] `$C sh 'tail -20 /var/opt/aa/knock.log'`: v2 start line, ALLOW_NETS, sshd started, and the window closes
      after 900 s (check later in the session; `car.sh` re-knocks by itself).
- [ ] If it fails: `$C fallback unlock`, then restore the `.v1` copies from B1 and reboot.

### B6. NbtService plugin (MUST if B5 passed, ~20 min; fallback ready; riskiest, so last of the MUSTs)
- [ ] Recon (B2) confirmed `/var/opt/hmi/data/ID5/Plugins` exists/is used? If yes, install there (recovery =
      `rm -r`): `$C sh 'mkdir -p /var/opt/hmi/data/ID5/Plugins/NbtService && cp /var/opt/aa/stage/NbtService/* /var/opt/hmi/data/ID5/Plugins/NbtService/'`.
      If not, stop here and decide together (the read-only /opt path gives up the easy recovery).
- [ ] Reboot. USER watches: iDrive comes up normally, the "Service" row is in Media/Radio (below Android Auto?),
      it opens, the "SSH (15 min)" checkbox and status row render (photos of each screen, for the README's
      unverified items: row order, status line, padlock icon).
- [ ] Toggle on -> status shows "open"; from the laptop, `plink ... -P 3804` logs in **without a knock**. Toggle off ->
      new logins refused.
- [ ] Android Auto still autostarts and the AA menu entry still works (both plugins loaded).
- [ ] If anything is wrong: `touch /var/opt/aa/service.disable` (soft) or `rm -r` the plugin dir, reboot.

### B7. Prove the plugin arms the gate on its own (IF TIME, one reboot)
- [ ] `$C sh 'mv /var/opt/aa/autostart /var/opt/aa/autostart.off'` -> reboot. The AA wrapper doesn't run, so if
      `$C knock` works, NbtService started knockd (A1 proven). If not: toggle in the plugin, else `$C fallback unlock`.
- [ ] Same boot, the open hypothesis: is the AndroidAuto SPP record still registered with no AA daemon running?
      (`sdptool`-style check from the laptop or the daemon log at its next manual start: GapErrorCode=5 or not.)
- [ ] Restore: `mv /var/opt/aa/autostart.off /var/opt/aa/autostart` (takes effect next boot; start AA from the menu now).

### B8. Move the toggle to Settings (IF TIME; only if B6 passed and B2 found the Settings anchor)
- [ ] Laptop: set `insertStateID` (and `insertAfterFocusID` if needed) in `id5/tools/nbtservice/NbtService.xml`,
      `make -C id5/tools/nbtservice test car`, push the XML (and the .so only if rebuilt), reboot, check the row.
      Not found / doesn't show -> My Vehicle anchor, else back to Media/Radio.

### B9. Wi-Fi: the car's own AP (IF TIME; needs >= 30 min; method in `id5/tools/wifi/README.md`)
Goal: find out **why** the AP doesn't stay up, with evidence, rather than getting it up once more. Treat
Devin's notes as leads only.
- [ ] Two laptop terminals for the whole block: `id5/tools/wifi/beacon-watch.sh 'NBTAA|DIRECT-'` (is it on air?)
      and `id5/tools/wifi/hu-watch.sh` (HU Wi-Fi processes + uap0/wfd0, changes only).
- [ ] W1 observe stock, 2 min, no changes: mwud running? scp? uap0 flags? any beacon?
- [ ] W2 zero-code (USER at the screen): the hidden service menu (hold MENU ~10 s) for WLAN/hotspot entries;
      any Wi-Fi or hotspot menu in Settings (issue #12 mentions a hotspot option). Photos.
- [ ] W3 mwud direct (the old blocker was the BM3 dongle dropping UDP; `mwu-send` runs on the HU now):
      start the stock mwud if it isn't running (README step 4, new log file), then
      `$C sh '/var/opt/aa/mwu-send cmd=ap_init iface=uap0 ssid=NBTAA-TEST key=<8+ chars> auth=0020 encrypt=0008'`
      and `... cmd=ap_start iface=uap0`. Record both replies. Watch >= 5 min.
- [ ] W4 for each GONE: what does hu-watch show at that second (new pid, process gone, ifconfig change)? The
      next attempt changes only that (e.g. `slay scp` first, if scp's mwud replaced ours). Max 3 attempts.
- [ ] W5 if a beacon holds >= 5 min: does the phone join it by hand (Wi-Fi settings, the key from W3)? DHCP
      172.16.224.x? Note it.
- [ ] W6 if W5 joined: does the phone AUTO-join from the Bluetooth credentials? The mechanism is upstream and
      not blocked by our code; it just points at the dongle today (nbt-aad.env NBT_AP_CFG/NBT_AP_IF). So test it
      by hand, NOT through the wrapper or env:
      - write the uap0 creds in the same INI format as the current file (`$C sh 'cat /var/opt/aa/iap2.cfg'` for
        the layout; keep its SecurityType) to a NEW file `/var/opt/aa/iap2-uap0.cfg`: SSID/Passphrase from W3,
        BSSID = uap0's MAC (12 hex, no colons, from `ifconfig uap0`);
      - kill the release daemon (it holds the Bluetooth service); phone forgets the test network;
      - launch wireless, no USB flags, pointing at uap0 (no NBT_AP_IP/NBT_AP_BSSID so the daemon reads them off
        uap0): `NBT_AP_CFG=/var/opt/aa/iap2-uap0.cfg NBT_AP_IF=uap0` plus the usual AOI/MM/LD_LIBRARY_PATH, log to
        `/var/opt/aa/nbt-uap0-1.log`;
      - the log should show `[Wireless] uap0: ip='172.16.224.1' bssid='<uap0 MAC>'` and the Wi-Fi details sent
        over Bluetooth; the phone joins on its own and AA starts over the car's Wi-Fi = goal 3 working once.
      - Note: the daemon runs `pfctl -d` at start (upstream, so the phone can reach 5277), so the packet filter
        is off on every interface until reboot; knockd v2's own allowlist still applies.
- [ ] Pull `id5/local/wifi/` logs + the HU's `mwud-*.log`, `sloginfo`. Questions for tom-pryor (#12) from
      what we saw.
- [ ] A reboot puts Wi-Fi back to stock. Do it before B10 if anything was slain or started.

### B9c. Boot-hook recon for F5 (IF TIME, READ-ONLY, ~10 min; analysis is done later on the laptop)
Why (IMPROVEMENT-PLAN F5): today everything that starts knockd runs via HmiMain3D loading a plugin (NbtAad, and
now NbtService). If the HMI won't boot, knockd never arms and we're locked out exactly when we need in. A second
hook at the QNX start-up layer, below the HMI, would arm knockd even then. It must start KNOCKD (not sshd), so
SSH stays closed until a signed knock — no permanent exposure. This trip only COLLECTS files; we decide how (or
whether) to hook later, off-car.
- [ ] Characterise the process supervisor (findings.md §8: Connectivity/Online/scp respawn ~30 s on their own).
      Read-only: `$C sh 'ls -l /etc/rc* /proc/boot/*.sh 2>/dev/null; cat /etc/system/config/* 2>/dev/null | head'`
      — what it launches, from which paths. (Adjust paths once the first listing shows the real layout.)
- [ ] Pull the start-up scripts and the supervisor config to `id5/local/recon/boot/` (`$C pull ...`), so the
      grep for exec points (`spawn|system|/var|/fs/usb|/mnt|on -f|.sh`) runs on the laptop, not the car.
- [ ] Note the iStep / SW version string next to those files (what a firmware update would overwrite).
- [ ] No writes to any stock boot/start-up file this trip — a typo there boot-loops the unit. Recon only.
Deliverable (laptop, later): a short findings.md note — what the supervisor respawns and from where, which exec
points read from a writable path, and whether a no-edit hook exists (a stock entry that runs a binary by path
from a writable location, or a script that sources a drop-in directory). No install, and no restore-kit this trip.

### B10. Wrap-up (MUST, ~5 min)
- [ ] Final state: release daemon autostart restored, knockd (v1 or v2, whichever passed) answering, NbtService
      kept or disabled. Write down which.
- [ ] Final `$C knock` + `$C login` works. Pull knock.log, the NbtService log if any, and `sloginfo`.
- [ ] Laptop: run log filled in; commit the notes (`id5/`), `id5/local/` stays local.

## 0. [ ] USB-AA direct-mode test — structured (rewritten 2026-11-03 night)

Rules: ONE change per run. Write the prediction BEFORE the run. `collect` after
EVERY run, pass or fail. Two runs failing at the same stage = stop and re-plan,
don't stack another tweak. Note: usb11/usb12 never ran direct mode (no
`connected to io-usb`, sessions on stock /dev/oap013) — the root-cause fix
(direct + driver claim) has NOT been car-tested on the ID5 yet; run 13 is its first.

Shorthands:
`C=~/Desktop/Projects/BMW/Supra-iDrive6-Android-Auto/id5/tools/car/car.sh`
`U=~/Desktop/Projects/BMW/Supra-iDrive6-Android-Auto-usb/milestone-7`

### 0a. Pre-flight (laptop, before going to the car)
- [ ] PR #42 moved? `cd $U/.. && git fetch upstream pull/42/head && git log --oneline pr42..FETCH_HEAD`
      New commits -> read them, rebuild vanilla (and rebase pr42-id5), re-run host
      tests, re-stage. Same tip (d7ba352) -> keep the staged binaries.
- [ ] PR #65 (tom-pryor, mm-renderer for cars without CarPlay coded) moved?
      `git fetch upstream pull/65/head && git log --oneline 18d402b..FETCH_HEAD`.
      usb6 = pr42 @ d7ba352 + ONLY 18d402b (cherry-picked, not main's v1.8.62..67).
- [ ] Host tests green on what we deploy (2026-11-03: vanilla test_usb_accessory /
      test_session_owner / test_usb_log pass; patched passes with its protocol-2 assert).
- [ ] Phone logcat path: phone + laptop on one Wi-Fi (not the car's), Wireless
      debugging on -> `adb pair` / `adb connect <ip:port>`; `adb get-state` = device.
      Without it, collect skips logcat — the phone side is half the evidence.
- [ ] Record phone state: AA app data wiped since last run? 'BMW nnnnn' entry in AA
      settings? USB preference? (these changed results before)

### 0b. Car setup (once)
1. `$C status` -> `$C knock` (sshd :3804 stays up until the HU reboots with knockd v1; with v2
   it closes again after OPEN_S, and car.sh re-knocks on its own when :3804 is shut).
2. Deploy (push size-checks, keeps +x):
   `$C push $U/nbt-aad-usb5-vanilla /var/opt/aa/nbt-aad-usb5`  (pr42 @ d7ba352, 556253 B)
   `$C push $U/nbt-aad-usb4-patched /var/opt/aa/nbt-aad-usb4`  (pr42-id5 @ 6207f6c, 557117 B)
   `$C push $U/nbt-aad-usb6-mmr /var/opt/aa/nbt-aad-usb6`      (pr42 + PR #65 18d402b, 561950 B)
3. Note whether the boot-autostarted daemon is running: `$C sh 'pidin ar | grep nbt-aad'`.
4. PR #65 assumptions on the ID5 (read-only, save the output to the run log):
   `$C sh 'ls -l /opt/conn/bin/carplay/mm-renderer /opt/conn/etc/carplay/mm-renderer.conf /opt/conn/etc/carplay/mm-playlist.conf; ls -ld /opt/conn/lib/carplay/mm /var/opt/conn/carplay/carplay.ini /fs/sda1/opt/conn/carplay/carplay.ini; pidin ar | grep mm-renderer'`
   #65 spawns the FACTORY renderer (/opt/conn paths, factory conf with playlist engine) and
   treats "no carplay.ini" as "CarPlay not coded". Our wrapper instead runs
   /fs/sda0/opt/conn/... with our trimmed $AA/mm-renderer.conf (findings.md: the factory playlist
   engine wanted RO /etc/mm). If carplay.ini EXISTS here, #65 waits until 90 s uptime first.

### 0c. Per run (N = 13, 14, ...)
1. Prediction written in the run log below (what stage we expect to reach, why).
2. Unplug the phone. Then one command does the rest:
   `$C run $N nbt-aad-usb5`  (extra env as `KEY=VALUE` after the binary; `RUN_MMR_CONF=0 $C run ...`
   leaves out AOI_CONFIG/MM_RENDERER_CONFIG for the PR #65 run)
   It kills leftover nbt-aad / devc-serusb-aoap, clears the phone logcat (adb), launches
   `/var/opt/aa/<bin>` with the standard env below, checks it is still alive after 3 s, says
   **PLUG THE PHONE IN NOW**, follows the log (media frames / session end / 90 s, `RUN_WAIT_S`),
   then runs `collect $N` -> `id5/local/recon/run-$N/` + stage table. It refuses a run number
   that already exists locally or on the HU.
   The detached launch over non-interactive SSH is **not car-verified yet**. If run 13 says
   "not running 3 s after launch", use the manual steps below and note it here.
3. Fill in the run log.

Manual fallback (what `run` does):
   `$C sh 'for p in $(pidin | sed -n "s/^ *\([0-9]*\).*nbt-aad.*/\1/p"); do kill -9 $p; done'`
   `$C sh 'for p in $(pidin | sed -n "s/^ *\([0-9]*\).*serusb-aoap.*/\1/p"); do kill -9 $p; done'`
   `adb logcat -c`, then from `$C login`:
```
AA=/var/opt/aa; CP=/fs/sda0/opt/conn; BIN=nbt-aad-usb5; N=13
NBT_USB=1 NBT_USB_DIRECT=1 NBT_SPP_REREG_S=0 \
NBT_LOG_FILE=$AA/nbt-usb$N.log NBT_VERBOSE=1 \
AOI_CONFIG=$AA/aoi/ MM_RENDERER_CONFIG=$AA/mm-renderer.conf \
LD_LIBRARY_PATH=/fs/sda0/opt/sys/lib:/fs/sda0/opt/mm/lib:$CP/lib/carplay:/opt/sys/lib \
$AA/$BIN >$AA/nbt-usb$N-stdout.log 2>&1 &
```
   then plug the phone in, wait ~30 s, `$C collect $N`.

Never NBT_USB_CLAIM_DRIVER=0: it lets devc-serusb-aoap start and create /dev/oap*,
which NbtMediaMainApp reads from — the root cause. Identity envs not needed
(default Android / Android Auto). Audio defaults to browser mode (0xb8 mixer).

### 0d. Decision tree (read the stage table top-down; first 0 or bad row decides)
- `claim NOT effective` > 0, or no `driver path claimed` -> the race is back.
  Record it, check `pidin-fds.txt` for oap holders; do NOT debug TLS this run.
- No `io-usb connected` -> direct mode not active (NBT_USB_DIRECT typo, or
  `needs QNX io-usb` in the log). Fix setup, rerun. (libusbdi is present: the
  binary links it and would not start without it.)
- No `USB device seen` / `switch to accessory sent` -> phone not seen or not asked:
  `dev-nodes.txt`, phone USB mode, cable. Not a code problem yet.
- Switch sent but no `accessory enumerated` -> phone refused accessory mode.
  logcat: CAR.SETUP / UsbAccessory lines ("Accessory is not valid"?).
- `bridge open` but VersionResponse 0 -> phone not talking AAP on USB.
  If `BT parked (vanilla)` > 0 and logcat shows WPP VERSION_REQUEST_TIMEOUT loops ->
  the phone is doing wireless setup over BT instead -> NEXT RUN: usb4 (kill-bait),
  same everything else. Otherwise phone-side: forget car in AA, force-stop, replug.
- TLS complete but `decrypt FAILURES` > 0 in direct mode -> NEW finding (stealing
  should be impossible). Keep the log; next run unchanged once to check it repeats;
  then `slay NbtMediaMainApp` run (reboot restores) to rule the media app in/out.
- `media frames` > 0 and AA on screen -> SUCCESS. Same run: note video, touch, knob,
  audio (browser mode), call audio; then unplug/replug once (reconnect is a known
  PR weak spot). Then: post results to PR #42.
- After a SUCCESS only: the PR #65 run (one variable = who starts mm-renderer). Kill the
  wrapper's renderer (`slay mm-renderer`), launch usb6 with the SAME env but WITHOUT
  AOI_CONFIG / MM_RENDERER_CONFIG. Expect `mm-renderer we started (#65)` = 1, then video.
  `UNREACHABLE` -> #65's factory env does not work on the ID5: grab $AA/nbt-usbN.log +
  `sloginfo`, restart our renderer (start-aa.sh step 1). Either way = feedback for PR #65.
  Before that, usb5/usb6 runs use the wrapper's renderer, so #65 code stays idle (it
  finds the running /fs/sda0/.../conn/bin/carplay/mm-renderer by path tail and never spawns).

### 0e. Read-only recon for IMPROVEMENT-PLAN.md (any time; changes nothing, not a run)
Save the output to `recon/improve-$(date +%F).txt`:
```
$C sh 'ifconfig -a; pfctl -s rules; pfctl -s Anchors; mount; df -k /var /fs/sda0'
$C sh 'ls -ld /var /var/opt /var/opt/aa /var/opt/hmi/data/ID5 /var/opt/hmi/data/ID5/Plugins; ls -l /var/opt/aa/authorized_keys'
$C sh 'grep sshd /etc/passwd; /usr/sbin/sshd -V 2>&1 | sed -n 1,2p; ls -l /var/opt/aa/knock.log; tail -5 /var/opt/aa/knock.log'
$C sh 'ls /opt/hmi/ID5/data/ro/Plugins; grep -h "<pluginApp " /opt/hmi/ID5/data/ro/Plugins/*/*.xml'
```
Answers: A2 interfaces and pf; A5 which menus accept plugin rows (`insertStateID` of the
stock plugins); F3 StrictModes/privsep feasibility; F4 whether /var is its own partition
and the /var plugin dir exists.

### Run log (one line per run; prediction BEFORE, result AFTER)
| N  | binary | change vs previous | prediction | furthest stage | decision |
|----|--------|--------------------|------------|----------------|----------|
| 13 | usb5   | first direct+claim run | reaches TLS; no decrypt failures if claim holds; may stall at VersionResponse if phone does BT WPP | | |
| (after success) | usb6 | renderer: wrapper's -> #65 spawns factory one | `we started (#65)` = 1; video OK unless factory conf breaks on ID5 | | |

### Open hypotheses (test only when the tree points at them)
- First `SppUuidRegister response` GapErrorCode=5 = something registered
  AndroidAuto before us — probably the boot-autostarted release daemon (kill -9
  does not deregister). Test: boot with /var/opt/aa/autostart moved away. That
  boot also skips the wrapper's knockd + mm-renderer: knock will not answer
  (menu Start or bavarian unlock), mm-renderer must be started by hand (start-aa.sh step 1).

Root cause (findings.md §13): stock OAPReaderThread steals /dev/oap transfers; TLS
implicit seq -> 1 lost record = bad-record-mac forever. Direct mode + claim is the fix.

## 1. [x] SSH-independence — DONE 2026-11-02 via knockd (1a); 1b/1c optional, kept for reference

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

## 2. [~] USB AA (PR #42) — attempted 2026-11-02; BLOCKED phone-side (see findings.md §13)

Pre-recon — DONE:
- [x] `/dev/android*` appears when phone plugs in (vdev-medialaunch auto-fires aoap-switch -t)
- [x] `/dev/oap*` appears (stock devc-serusb-aoap binds the accessory)
- [x] NbtMediaMainApp A4A handler holds /dev/oap* concurrently — real contention
      (documented race in usb_accessory.cpp comments; observed stealing reads)
- [x] switch flow fully mapped: aoap-switch -t mounts /dev/androidNN only;
      devctl __DIOTF(5,3) Config-and-Switch + 1540B Identity drives the flip

Deploy + test:
```
hu.sh push hu-install/nbt-aad-usb /var/opt/aa/nbt-aad-usb
hu.sh sh 'chmod +x /var/opt/aa/nbt-aad-usb'
# check libusbdi exists or push fallback:
hu.sh sh 'ls /lib/libusbdi* /usr/lib/libusbdi* /opt/mm/lib/libusbdi* 2>/dev/null'
# (if absent: hu.sh push hu-install/libusbdi.so.2 /var/opt/aa/libusbdi.so.2
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

## 3. [~] Official AA leftovers recon (Cinemo/NmeSDK) — done 2026-11-02, see findings.md §14

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

User correction 2026-11-03: the HU Wi-Fi antenna IS fitted — the AP was seen on the
phone at several points but never stayed up (persistence is the problem, not RF).
Before more reversing: read upstream issue #12 (tom-pryor, also no CarPlay, has an
IPC PoC for enabling Wi-Fi without CarPlay) and ask him. With SSH on ENET, mwud is
reachable via `plink -L` port forwards — no pdebug/gdb injection needed any more.

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

`hu-install/nbt-aad-main` = upstream main @ dc4adba (built locally): adds
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
- [ ] Repo contribution candidate: adapter-method.md already written;
      +USB results → upstream PR discussion.

## Status log

(append one line per attempt)
- pending: everything below is untested on car

Pull tomorrow: NBTMediaMainApp (/fs/sda0/opt/mm/bin/, 8.7MB) + run on HU:
`grep -rl AndroidAuto /opt/mm/bin /opt/mm/lib` (find stock SPP registrar +
AA config provider). Offline goal: find the metatype provisioning path +
coding gate for the dormant official AA engine (see findings.md Cinemo section).
