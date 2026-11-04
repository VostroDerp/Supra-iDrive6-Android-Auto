# Next car session

**Goal: a car that is usable every day.** After a cold boot, Android Auto over USB and our SSH come up by
themselves, switching iDrive off and on doesn't need a replug, SSH answers only on the cable, and wireless works.
Features upstream is building (car GPS, cluster arrows, audio over the Android Auto link) wait until they are
merged, and then become one setting on our base build.

Steps run in order, one change each. A step that fails is undone and the session moves on to the next step that
doesn't depend on it. Results go into the run log at the bottom.

`C=id5/tools/car/car.sh` (from the repo root), `U=../Supra-iDrive6-Android-Auto-usb/milestone-7` (staged builds).

## Builds

| On the head unit | Build | What it is | Status |
|---|---|---|---|
| `nbt-aad-usb7` (already there) | v1.8.72-main-usb | upstream main after the USB merge (#42) | **Proven**: runs 13-15 |
| `nbt-aad-base12` (push `$U/nbt-aad-usb12-base`) | v1.8.80-base12-main8545-usbstale | upstream main 8545fe9 (Wi-Fi Direct, #76 audio) + our reconnect fix | QNX-built, host-tested, not car-tested |
| `nbt-aad-base` | a copy of one of the above | what the start-up script runs | `usb7` first; `base12` once step 2 passes |

The reconnect fix only acts when a reopened phone starts with stale data, so in a normal session base12 behaves as
upstream main. Step 2 tests the newer main; step 3 tests the fix.

## Before going to the car
- [ ] Upstream, right before leaving: read new PRs and issues since the last check. Merged work that overlaps a
      step changes that step, or the base build.
- [ ] Builds staged in `$U` (table above). Battery charger packed: the head unit shuts down on low voltage.
- [ ] Laptop and phone on the home Wi-Fi (laptop internet for the fallback unlock; adb over Wi-Fi for logcat).

## Getting in (every session, and after every reboot until step 1 works)
1. ENET cable in, charger on. `$C status`, then `$C knock`.
2. If the knock port is closed (nothing of ours starts at boot yet): `$C fallback status` (diag port open), `$C
   fallback unlock` (the stock path; opens the stock sshd until the next reboot), then start knockd through it:
   `$C fallback sh '/var/opt/aa/knockd /var/opt/aa/knock.conf >>/var/opt/aa/knock.log 2>&1 </dev/null'` (it
   detaches itself), then `$C knock`.
3. Before a test run without the start-up script, start the video renderer by hand (`tools/car/start-aa.sh`
   shows the line). With the script installed, it does this itself.

## 1. Start-up script (one reboot, ~25 min)
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
- [ ] Without a reboot: stop the old stuck wrappers and any test daemon, choose Android Auto in the iDrive menu,
      plug in. Check: picture; `pidin ar` shows knockd once; log in `/var/opt/aa/nbt.log`.
- [ ] Reboot. Check: `$C knock` works with no fallback; plug in, picture.
- [ ] Way back: the install line with `/var/opt/aa/nbt-aad.wrapper.v1` and `/var/opt/aa/nbt-aad.env.v1`.

## 2. Newer base (no reboot, ~15 min)
- [ ] `$C push $U/nbt-aad-usb12-base /var/opt/aa/nbt-aad-base12`, then `$C run <N> nbt-aad-base12 NBT_VEHICLE=bmw`.
- [ ] Check what runs 13-15 checked: picture, touch, controller, Bluetooth audio and its resume, Assistant, a call,
      reverse and back, replug.
- [ ] Passes: make it the base, `$C sh 'cd /var/opt/aa && cp nbt-aad-base12 nbt-aad-base.new && mv nbt-aad-base.new nbt-aad-base'`,
      and check once through the start-up script (iDrive menu or a reboot). Fails: keep usb7 as the base; the
      log goes upstream as a report.

## 3. iDrive off and on with the phone plugged in (no reboot, ~10 min)
The case that needed a replug on 2026-11-03: the head unit ends the session after 2 s of silence, reopens the same
USB connection, and the phone carries on with its old encrypted session. The fix ends such an opening at once and
resets the phone's USB connection, as a replug would.
- [ ] Picture up on base12, then iDrive off and on with its power button, phone left plugged in.
- [ ] Check the log: `the phone is still in an earlier session`, `USB reset done`, then either `accessory ...
      removed` and a new switch, or a reopen whose first transfer is `[ch 0 0x03 12]`. The picture comes back
      without a replug.
- [ ] If the phone stays stuck after the reset: the opening still ends at once instead of two minutes of decrypt
      failures. Note what the phone shows. The upstream PR waits for this step.

## 4. knockd v2 (no reboot for the side-by-side test, ~25 min)
v2 answers only on the cable and the car's Wi-Fi, opens SSH for a limited window, and fixes the review findings
(IMPROVEMENT-PLAN A2, A3, A8, C). Procedure: `tools/knock/README.md`, "Deploying v2 over v1".
- [ ] First, read-only: `$C sh 'pfctl -a filter_OBD -s rules; pfctl -s Interfaces'` (how the cable gets past the
      head unit's packet filter).
- [ ] v2 side by side on a test port with its test configs, knock and log in. Then swap it in, with v1 kept as
      `knockd.v1`, and check a cold boot through the start-up script.

## 5. NbtService (one reboot, ~25 min; after step 4)
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

## 6. Wireless over Wi-Fi Direct (no reboot, ~25 min)
The car runs its Wi-Fi Direct group at boot even without CarPlay, since Wi-Fi and screen-mirroring coding was switched
on with BimmerCode (findings.md, Wi-Fi; which setting matters is not tested yet). Upstream's `NBT_ALT_WIFI=DIRECT` offers that group to the phone; it was tested
on a car with CarPlay, so this is the first car without it.
- [ ] Read-only: `$C push $U/wfd_probe-main /var/opt/aa/wfd_probe`, `$C sh /var/opt/aa/wfd_probe > id5/local/recon/wfd-probe-<date>.txt`
      (it prints the decoded passphrase: the output stays in `local/`). Is the group visible on the phone always,
      or only during iDrive's screen-mirroring pairing? Which band (`grep -i -E "chan|band|freq|hw_mode"` in
      `/opt/conn/bin/wifidirect_defaults.conf`)?
- [ ] Wireless only, USB unplugged, Bluetooth connected: `$C run <N> nbt-aad-base12 NBT_USB=0 NBT_ALT_WIFI=DIRECT NBT_VEHICLE=bmw`.
      Check: the phone joins the group, the daemon accepts on 172.16.222.1:5277, picture. Check the group name the
      log shows against the real one (upstream calls its name lookup "mostly consistent").
- [ ] Then both on (`NBT_USB=1`, default `NBT_ALT_WIFI`): plugging in during a wireless session leaves it alone,
      and wireless never takes over a USB session. Making the start-up script run both is a separate change.

## If time
- [ ] **Audio over the Android Auto link** (#76, merged): one run on base12 with `NBT_AUDIO_MODE=browser`. Better
      sound than Bluetooth by upstream's account, but no now-playing or steering-wheel integration yet. Your choice
      afterwards.
- [ ] **The SSH switch in the Settings menu** (after step 5): from step 5's manifest listing, find the Settings
      anchor, change `insertStateID` in `NbtService.xml`, rebuild, reinstall, reboot. Way back: the step 5 build.
- [ ] **A second boot hook** (IMPROVEMENT-PLAN F5), read-only: pull the stock start-up scripts and look for one that
      runs something from `/var` or USB without a signature check. Nothing is changed tonight; a candidate becomes
      its own step next session.

## Later (not this session)
- Which coding Wi-Fi Direct needs, and which one brings the screen-mirroring pop-up. Starting from today's coding
  (BimmerCode backup first), switch one setting off per test (`ENT_MIRACAST_DISPLAY_SETTINGS`, `ENT_MIRACAST`,
  `MIRRORING`, `WLAN_AP`, then `WLAN_WIFI_DIRECT`), reboot, and check: `wfd0` up, the step 6 session, the pop-up.
  Put back any setting whose removal breaks the session. Only after step 6 works, so there is a baseline.
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
