# Next car session

**Goal: a car that is usable every day.** After a cold boot, Android Auto over USB and our SSH come up by
themselves, switching iDrive off and on doesn't need a replug, SSH answers only on the cable, and wireless works.
Features upstream is building (car GPS, cluster arrows, audio over the Android Auto link) wait until they are
merged, and then become one setting on our base build.

Steps run in order, one change each. The order: our own way in first, then a daily-usable Android Auto, upstream's
changes, the audio modes and steering wheel, wireless, and last the improvements to our way in. Times are
estimates of the typing and checks, not measured: note the clock at the start of each step in the run log. A step that fails is undone and the session moves on to the next step that
doesn't depend on it. Results go into the run log at the bottom.

`C=id5/tools/car/car.sh` (from the repo root), `U=../Supra-iDrive6-Android-Auto-usb/milestone-7` (staged builds).

## Builds

| On the head unit | Build | What it is | Status |
|---|---|---|---|
| `nbt-aad-usb7` (already there) | v1.8.72-main-usb | upstream main after the USB merge (#42) | **Proven**: runs 13-15 |
| `nbt-aad-base13` (push `$U/nbt-aad-usb13-base`) | v1.9.09-most-base13 (main e279e55) | current upstream main, unchanged (v1.9.09 or later: #65 renderer start, #78 MOST rewrite, #76 audio, Wi-Fi Direct, volume display) | not car-tested |
| `nbt-aad-base13-pr91` (push `$U/nbt-aad-usb13-pr91`) | v1.9.09-most-base13-pr91 (main + #91 at 0b38f62) | base13 + Tom's #91 (USB recovery) | step 3 only |
| `test_controls` (push `$U/test_controls-base13`) | built from the same main | the controller monitor | only if step 2's controller check fails |
| `nbt-aad-base` | a copy of one of the above | what the start-up script runs | `usb7` first; `base13` once step 2 passes |

base12 (main 8545fe9 + our reconnect fix) is retired: Tom's #91 does the same job and more, and upstream comes
first. #91 also acts in normal sessions (it resets a phone found in accessory mode at start and says goodbye to the
phone on exit), so it gets its own run (step 3) and stays out of the base until that passes.

## Before going to the car
- [ ] Upstream, right before leaving: read new PRs and issues since the last check. Merged work that overlaps a
      step changes that step, or the base build.
- [x] Build from that main: base13, base13-pr91 and `test_controls`, each with its own version string. Host tests
      for the USB and controller code. Staged in `$U`.
- [ ] Battery charger packed: the head unit shuts down on low voltage.
- [ ] Laptop and phone on the home Wi-Fi (laptop internet for the fallback unlock; adb over Wi-Fi for logcat).

## Getting in (every session, and after every reboot until step 1 works)
1. ENET cable in, charger on. `$C status`, then `$C knock`.
2. If the knock port is closed (nothing of ours starts at boot yet): `$C fallback status` (diag port open), `$C
   fallback unlock` (the stock path; opens the stock sshd until the next reboot), then start knockd through it:
   `$C fallback sh '/var/opt/aa/knockd /var/opt/aa/knock.conf >>/var/opt/aa/knock.log 2>&1 </dev/null'` (it
   detaches itself), then `$C knock`.
3. Before a test run without the start-up script, start the video renderer by hand (`tools/car/start-aa.sh`
   shows the line). With the script installed, it does this itself.

## 1. Start-up script (one reboot, ~15 min plus the reboot)
`tools/hu-install/nbt-aad-usb.sh` becomes `/opt/aa/nbt-aad`, the path the iDrive app starts at boot. It starts
knockd (once), keeps the daemon log under 8 MB, starts the renderer if none runs, and runs `/var/opt/aa/nbt-aad-base`
with the run 13-15 settings. The settings file the app reads, `/opt/aa/nbt-aad.env`, loses the old dongle settings in
the same remount.
- [ ] Read the settings file the app passes on: `$C sh 'cat /opt/aa/nbt-aad.env'`. The old dongle settings
      (`NBT_AP_CFG`, `NBT_AP_IF`, `NBT_AP_IP`, `NBT_AP_BSSID`) come out; note anything else before deciding on it.
- [ ] Keep the old ones and set the base: `$C sh 'cp /opt/aa/nbt-aad /var/opt/aa/nbt-aad.wrapper.v1; cp /opt/aa/nbt-aad.env /var/opt/aa/nbt-aad.env.v1; cp /var/opt/aa/nbt-aad-usb7 /var/opt/aa/nbt-aad-base'`.
- [ ] The cleaned settings file: `$C sh "grep -v -E '^(export )?NBT_AP_(CFG|IF|IP|BSSID)=' /opt/aa/nbt-aad.env >/var/opt/aa/nbt-aad.env.new; cat /var/opt/aa/nbt-aad.env.new"`.
- [ ] Install both (`/opt` is read-only until remounted): `$C push id5/tools/hu-install/nbt-aad-usb.sh /var/opt/aa/nbt-aad-usb.sh`,
      `$C sh 'mount -uw /fs/sda0 && cp /var/opt/aa/nbt-aad-usb.sh /opt/aa/nbt-aad && chmod 755 /opt/aa/nbt-aad && cp /var/opt/aa/nbt-aad.env.new /opt/aa/nbt-aad.env; mount -ur /fs/sda0; ls -l /opt/aa/'`.
- [ ] Keep the repo copy current: `$C pull /opt/aa/nbt-aad.env id5/tools/hu-install/nbt-aad.env` (commit it after the
      session).
- [ ] Without a reboot: stop the old stuck wrappers and any test daemon, choose Android Auto in the iDrive menu,
      plug in. Check: picture; `pidin ar` shows knockd once; log in `/var/opt/aa/nbt.log`.
- [ ] Reboot. Check: `$C knock` works with no fallback; plug in, picture.
- [ ] Way back: the install line with `/var/opt/aa/nbt-aad.wrapper.v1` and `/var/opt/aa/nbt-aad.env.v1`.

## 2. Newer base (no reboot, ~15 min)
#78 rewrote the code between the car's bus and the controller. Nobody has tested the controller on it in a car yet,
and ours isn't the Supra it was tuned on, so the controller gets a full check, not one turn. The two log settings
only add log lines, so a failure already has the raw data.
- [ ] `$C push $U/nbt-aad-usb13-base /var/opt/aa/nbt-aad-base13`, then
      `$C run <N> nbt-aad-base13 NBT_VEHICLE=bmw NBT_MOST_LOG=1 NBT_KNOB_RAW=1`.
- [ ] Controller, each one, noting any that do nothing or the wrong thing: rotate 3 clicks each way, push, tilt
      up/down/left/right, then MENU, BACK (a short press, and a 2.5 s hold to minimize), OPTION, COM, MEDIA, NAV,
      MAP.
- [ ] Volume knob: the new volume display shows over the picture and goes away.
- [ ] What runs 13-15 checked: picture, touch, Bluetooth audio and its resume, Assistant, a call, reverse (the
      parking-sensor view shows, then Android Auto comes back), replug. The volume display is new and on by default;
      on a camera car it upsets the reverse view (#102): if ours goes wrong, the same check with `NBT_VOL_OSD=0`.
- [ ] Steering wheel (2a) in the same run.
- [ ] Passes: make it the base, `$C sh 'cd /var/opt/aa && cp nbt-aad-base13 nbt-aad-base.new && mv nbt-aad-base.new nbt-aad-base'`,
      and check once through the start-up script (iDrive menu or a reboot). Fails: keep usb7 as the base; the
      log goes upstream as a report.

**If the controller is wrong.** usb7 worked with the default (Supra) settings, so first sort the failure:
- Worked on usb7, broken on base13: a regression in #78. The base13 log is the report; the fix belongs in the shared
  code, which every car runs.
- A key lands in another slot, or the knob runs backwards: the per-car settings for that already exist
  (`NBT_KEY_<slot>`, `NBT_KNOB_INVERT`, `NBT_KNOB_INST` in `nbt-aad.env`), so Tom's car is not affected.
- Either way, capture the car's own controller messages for the mapping (~10 min): stop the daemon,
  `$C push $U/test_controls-base13 /var/opt/aa/test_controls`, `$C login`, then
  `/var/opt/aa/test_controls -w -o /var/opt/aa/controls.env -l /var/opt/aa/controls-raw.log` (it asks for each
  control in turn, 15 s each). Pull both files to `id5/local/recon/controls-<date>/`.

## 2a. Steering wheel, Bluetooth audio (in the step 2 run, ~10 min)
This wheel has no next/previous buttons: the scroll wheel opens the cluster's now-playing list (previous above the
song, next and the coming songs below), and pressing the wheel plays the highlighted entry. The head unit fills that
list and gets the choice back, so the cluster screen matters as much as the log.

Music playing from Android Auto. One control at a time: before each, a marker line goes into the run's log
(`$C sh "echo '==== MARK <control> ====' >>/var/opt/aa/nbt-usb<N>.log"`; the daemon appends to that file, so the
lines interleave). Then that control as written, about 3 s apart, and say what the car and the phone did. Early in
the run: `NBT_MOST_LOG` keeps at most 200 lines per message type. Skip any control the wheel doesn't have.
1. Scroll down 1 notch, 3 times (one step at a time down the list), then wait for the list to close.
2. Scroll up 1 notch, 3 times.
3. Scroll to "next", press. 4. Scroll to "previous", press. 5. Scroll to a song further down the list, press.
6. Volume up 3 times, down 3 times. 7. Mode, once per source, until back to Bluetooth. 8. Voice button, a short
   press.
- [ ] The cluster list shows the phone's songs (the same as Android Auto shows), and each press plays what was
      highlighted.
- [ ] Volume changes, and the volume display shows.
- [ ] Mode goes through the sources; back on Bluetooth, the Android Auto music carries on.
- [ ] Voice button: does what iDrive normally does; the log shows `[PTT] voice button released`.

## 2b. Browser audio (no reboot, ~15 min; after step 2)
Sound over the Android Auto link instead of Bluetooth (#76). Upstream says it sounds better, but has no now-playing
and no steering-wheel integration yet (#51). That makes the steering-wheel results here data for #51, not a failure.
- [ ] `$C run <N> nbt-aad-base13 NBT_VEHICLE=bmw NBT_AUDIO_MODE=browser NBT_MOST_LOG=1`.
- [ ] Music plays through the car with Bluetooth audio not in use (log: `[Main] audio mode: browser`); a nav prompt
      lowers the music; listen for crackling (#81 reports it on an X3); after a call, the music comes back (upstream
      doesn't know yet).
- [ ] The 2a sequence twice, with markers: once with iDrive's source on Bluetooth (the same phone) before Android
      Auto plays, once with it on the radio. Each time, note what the cluster list shows and what a press does.
      The idea to check: Android Auto's music still runs on the phone, so iDrive's Bluetooth source may still list
      the songs and send next/previous to the phone, while the sound comes over the Android Auto link. On the radio,
      a press probably changes the station instead (the kind of thing #40 reports).
- [ ] Which mode the base uses is your choice afterwards (one line in `nbt-aad.env`).

## 3. iDrive off and on with the phone plugged in (no reboot, ~10 min)
The case that needed a replug on 2026-11-03: the head unit ends the session after 2 s of silence, reopens the same
USB connection, and the phone carries on with its old encrypted session. Tom's #91 gives such a session up after
16 frames it can't decrypt and resets the phone's USB connection, as a replug would (at most 2 resets in 2 minutes).
- [ ] `$C push $U/nbt-aad-usb13-pr91 /var/opt/aa/nbt-aad-base13-pr91`. The phone stays plugged in from the last
      run, so `$C run <N> nbt-aad-base13-pr91 NBT_VEHICLE=bmw` also tests the reset at start: log
      `[UsbAccessory] ...: resetting it`, `[UsbDirect] ...: USB reset done`, then a picture.
- [ ] Picture up, then iDrive off and on with its power button, phone left plugged in.
- [ ] Check the log: `none of the first ... encrypted frames could be decrypted`, `resetting it`, `USB reset done`,
      then a new session. The picture comes back without a replug.
- [ ] Stop the daemon with the picture up (`$C sh` kill of the daemon): `ByeByeRequest to the phone: sent`, and the
      phone leaves Android Auto cleanly.
- [ ] If the phone stays stuck: note what the phone shows and whether the log says `USB resets did not help`. The
      result goes on #91 either way. Passes: wait for #91 to merge, then it comes into the base with the next
      main.

## 4. Wireless over Wi-Fi Direct (no reboot, ~15-20 min)
The car runs its Wi-Fi Direct group at boot even without CarPlay, since Wi-Fi and screen-mirroring coding was switched
on with BimmerCode (findings.md, Wi-Fi; which setting matters is not tested yet). Upstream's `NBT_ALT_WIFI=DIRECT` offers that group to the phone; it was tested
on a car with CarPlay, so this is the first car without it.
- [ ] Read-only: `$C push $U/wfd_probe-main /var/opt/aa/wfd_probe`, `$C sh /var/opt/aa/wfd_probe > id5/local/recon/wfd-probe-<date>.txt`
      (it prints the decoded passphrase: the output stays in `local/`). Is the group visible on the phone always,
      or only during iDrive's screen-mirroring pairing? Which band (`grep -i -E "chan|band|freq|hw_mode"` in
      `/opt/conn/bin/wifidirect_defaults.conf`)?
- [ ] Wireless only, USB unplugged, Bluetooth connected: `$C run <N> nbt-aad-base13 NBT_USB=0 NBT_ALT_WIFI=DIRECT NBT_VEHICLE=bmw`.
      Check: the phone joins the group, the daemon accepts on 172.16.222.1:5277, picture. Check the group name the
      log shows against the real one (upstream calls its name lookup "mostly consistent"). Also watch
      `sloginfo` for the media app (`NBTMediaMainApp`) failing on `/dev/btspp0`, `audioconn -b 0xc7` failures or a
      watchdog: upstream issue #84 sees those before a reset on a CarPlay car in wireless mode.
- [ ] Then both on (`NBT_USB=1`, default `NBT_ALT_WIFI`): plugging in during a wireless session leaves it alone,
      and wireless never takes over a USB session. Making the start-up script run both is a separate change.

## 5. knockd v2 (no reboot for the side-by-side test, ~15 min)
v2 answers only on the cable and the car's Wi-Fi, opens SSH for a limited window, and fixes the review findings
(IMPROVEMENT-PLAN A2, A3, A8, C). Procedure: `tools/knock/README.md`, "Deploying v2 over v1".
- [ ] First, read-only: `$C sh 'pfctl -a filter_OBD -s rules; pfctl -s Interfaces'` (how the cable gets past the
      head unit's packet filter).
- [ ] v2 side by side on a test port with its test configs, knock and log in. Then swap it in, with v1 kept as
      `knockd.v1`, and check a cold boot through the start-up script.

## 6. NbtService (two reboots, ~20 min plus the reboots; after step 5)
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

## If time
- [ ] **The SSH switch in the Settings menu** (after step 6): from step 6's manifest listing, find the Settings
      anchor, change `insertStateID` in `NbtService.xml`, rebuild, reinstall, reboot. Way back: the step 6 build.
- [ ] **Inventory of what's on the head unit** (read-only, feeds the tidy-up below):
      `$C sh 'ls -la /opt/aa /var/opt/aa /var/opt/aa/*/ /opt/hmi/ID5/data/ro/Plugins/NbtAad' > id5/local/recon/inventory-<date>.txt`.
- [ ] **A second boot hook** (IMPROVEMENT-PLAN F5), read-only: pull the stock start-up scripts and look for one that
      runs something from `/var` or USB without a signature check. Nothing is changed tonight; a candidate becomes
      its own step next session.

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
- Update the iDrive app to the release matching the base build (the installed one is an older release, unmodified;
  the repo's `hu-install/plugin/NbtAad.xml` also differs from upstream main).
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
