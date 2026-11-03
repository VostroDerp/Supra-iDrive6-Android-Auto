# ID5 improvement plan

Living backlog for robustness, reliability, efficiency and tidiness. Not the car-test
runbook (that is NEXT-SESSION.md). Started 2026-11-03.

Priority: **P1** = lockout or safety risk, do soon. **P2** = clear win. **P3** = tidy-up.
Every head-unit change gets its own car step: never in the same run as a USB/AA test.

---

## A. Head-unit access (knockd v2) — approved work

Goal (unchanged): SSH to my own car with no third-party unlock service, and no way to lock
myself out. **Hard rule: the stock path (FDAC unlock -> stock dropbear on :22 with the generic
password) must keep working untouched.** Ours is a separate sshd binary run with its own
config file (`-f /var/opt/aa/sshd_config`), on its own port (3804), with its own key file.
Nothing in A or F touches /etc, the stock dropbear or its settings.

Build order: C1+C2 (lockout / disk-fill bugs), then A2 + A3 + the rest of C (knockd v2,
host-tested), then A1 (own boot hook), then A5 (toggle). Each is its own car step, verified
on a cold boot before the old path is removed.

**Implementation status 2026-11-03: A1, A2, A3, A5, A8 and all of C are written and
host-tested; none of it has been on the car.** knockd v2 is `id5/tools/knock/` (42 checks in
`test_knock.py`, cross-built static for ARMv7LE); the NbtService plugin is
`id5/tools/nbtservice/` (host test drives the plugin against a real knockd; cross-built, and
every framework symbol it imports was checked against the car's own libAppFramework /
libCoreFramework). Both READMEs carry the car step and the list of what is still unverified.
Car order: knockd v2 first (one step, cold-boot check), then the plugin (one step, with the
FDAC unlock ready as the way back).

### A1. P1 — Separate SSH access from the AA wrapper — APPROVED 2026-11-03, BUILT (option a, not car-tested)
Intended: HMI loads the NbtAad plugin -> plugin sees `/var/opt/aa/autostart` -> launches the
wrapper `/opt/aa/nbt-aad` -> the wrapper starts knockd. Anything that breaks that chain also
removes SSH: a wrapper edit, autostart moved aside (planned for one hypothesis test), or the
plugin's boot guard latching autostart off after 3 crash-boots in 15 min.
**Car check 2026-11-03: the chain was never complete.** The wrapper on the car has no knockd step (the repo copy has
one but was never installed) and waits forever for a BM3 dongle DHCP lease on dwc0; every knockd start in
knock.log was manual. Interim:
the USB boot wrapper (NEXT-SESSION N1) starts knockd first (option b); NbtService stays the goal.

Options:
- **a. A small dedicated PluginApp ("NbtService")** — CHOSEN, built 2026-11-03 — that only
  starts knockd when HmiMain3D loads it. It doesn't depend on the AA app, its autostart flag or its boot guard. It is
  also the natural home for A5 (the iDrive SSH toggle). Recommended.
  Risk: a broken plugin can crash HmiMain3D at boot. Mitigations:
  - keep it minimal, with no work in the constructor;
  - test it on the host first with `rhmiapp_host_test` + `rhmiapp_fake_appframework`;
  - install it under `/var/opt/hmi/data/ID5/Plugins/` (see B2), so recovery is just `rm`
    over the still-working path.
- b. Keep the wrapper, but start knockd first and unconditionally, before anything that can
  fail. Cheap, but it still depends on the AA app's autostart and boot guard.
- c. Edit a stock start-up file (HMood/Connectivity .cfg on /fs/sda0). No: it is
  overwritten by updates, and a typo there can break boot.

Precondition: never remove the old path until the new one has been seen working on a cold
boot. For a while, both may start knockd: the second one fails to bind and exits, which is
harmless.

### A2. P1 — Restrict which networks can reach knockd and sshd (allowlist) — APPROVED 2026-11-03, BUILT (not car-tested)
Today, knockd binds `INADDR_ANY` and `sshd_config` has `ListenAddress 0.0.0.0`. "ENET-only"
is not true: the BM3 dongle's Wi-Fi reaches dwc0, and so would `uap0` (the car's own AP)
and `wfd0` once they are up.

Decision (approved): an **allowlist** in knock.conf, `ALLOW_NETS=169.254.0.0/16,172.16.224.0/24`
(ENET link-local + the car AP).
- knockd checks the **local** address of each accepted socket (`getsockname`) and drops
  non-matches before sending a nonce.
- sshd gets one `ListenAddress` per allowed address. knockd starts sshd on demand, so the
  addresses exist by then.
- This keeps the phone-tool idea (A6) on the car AP. It also keeps out the networks we
  don't control:
  - the BM3 dongle's Wi-Fi, which is bridged onto dwc0, so anyone who joins it reaches the
    knock port. This is the one exposed **today**;
  - `wfd0`;
  - anything a firmware update adds. `INADDR_ANY` would bind all of it.
- The allowlist cannot separate ENET from the BM3 dongle (same dwc0, different subnet:
  link-local vs the dongle's DHCP range), so allow `169.254.0.0/16` only, never the
  dongle subnet.

pf already shapes this (findings.md §11): the default is `block drop all`; dwc0 passes the
stock OBD/diag rule (knock on :851 works); uap0 passes UDP 49512–65535, DNS/mDNS and the
CarPlay ports to AP clients. So **TCP 851/3804 over uap0 also needs a pf anchor rule**
(the `filter_*` anchors can be loaded with `pfctl -a`, which is not persistent: knockd would
have to load it at start). Recon (read-only, NEXT-SESSION 0e): `ifconfig -a`,
`pfctl -s rules`, `pfctl -s Anchors`.

### A3. P1 — Open window + idle timeout — APPROVED 2026-11-03, BUILT (not car-tested)
Today, sshd stays up from the first knock until reboot.

New behaviour:
- A valid knock opens sshd for `OPEN_S` (default 900 s), then knockd SIGTERMs the
  *listener*.
- OpenSSH sessions are separate processes, so established sessions keep running. After
  that, nobody new can connect without knocking again.
- `car.sh knock` already re-knocks when :3804 is closed, so nothing changes on the laptop.

In sshd_config:
- `ClientAliveInterval 60` + `ClientAliveCountMax 3` drop dead sessions.
- `LoginGraceTime 30`.
- Optionally, an idle-shell timeout through `TMOUT` in the session environment.

### A5. P2 — iDrive menu toggle "SSH for 15 minutes" — APPROVED 2026-11-03, BUILT (placement still to decide)
**Next (decided 2026-11-03):** prove it in Media/Radio (the anchor the build uses), run the menu recon
(NEXT-SESSION 0e), then move it to **Settings** (fallback My Vehicle). The move is one XML attribute plus a
rebuild and a reboot. Both are queued in tonight's session (NEXT-SESSION, "Tonight" B2 + B8).
Being in the car is the authorization: the toggle opens the A3 window directly, with no knock.
sshd itself stays key-only.
- Mechanism: the plugin sends `open` to knockd on a 127.0.0.1 UDP port (or writes a flag
  file that knockd polls).
- Where: in the NbtService plugin (A1a). Its own menu entry would hold an "SSH (15 min)"
  checkbox, a status line, "Restart Android Auto" and "Copy logs to USB". The plugin's XML
  declares where the entry sits in iDrive, so the ID5 options (Settings, or next to the AA
  row in Media/Radio) need recon.
- Placement is declared by `insertStateID` (parent menu) + `insertAfterFocusID` (row it
  follows) in the plugin XML. NbtAad uses `insertStateID="20"` = Media/Radio. The stock
  plugins' XML on the HU (NEXT-SESSION 0e) shows which other menus accept plugin rows.
  Order **decided 2026-11-03**:
  1. **Settings menu** (preferred), if a stock plugin proves an ID5 anchor there;
  2. **My Vehicle / vehicle info** (same condition);
  3. **Media/Radio, below the Android Auto row**: last resort only. The anchor is known to
     work on this car.
  Build with `insertStateID` as a single constant, so the anchor can change after the
  NEXT-SESSION 0e recon.
  Inside the entry: "SSH (15 min)" checkbox + remaining time; later status, Restart AA,
  Copy logs to USB.
  Built 2026-11-03 as `id5/tools/nbtservice/`: the checkbox and a status row with the
  remaining time in four buckets (iDrive takes row text from the text DB by id, so a
  formatted string is not possible). `insertStateID="20"` (Media/Radio) is the only
  attribute to change after the recon. Restart AA and Copy logs to USB are not built.
- Not in upstream's NbtAad app: that would be a local patch we'd have to carry forever.

### A8. P2 — `PermitRootLogin without-password` in OUR sshd_config — APPROVED 2026-11-03, BUILT (not car-tested)
It applies only to our sshd (`-f /var/opt/aa/sshd_config`, :3804). The stock dropbear on :22
and the FDAC unlock with the generic password are not affected. It ships with the knockd v2
sshd_config (A3).

---

## C. knockd.c review findings — ALL APPROVED 2026-11-03, ALL FIXED in knockd v2 (not car-tested)

Sound: fresh 32-byte nonce from `/dev/urandom`, `RSA_verify` with SHA-256 and e=65537,
fail-safe exits when the config or key is missing, SIGPIPE ignored, daemonises after bind
so the wrapper's retry loop works.

| # | Sev | Finding | Fix |
|---|---|---|---|
| C1 | P1 | **A slow client can block knocking for ~20 min.** Accept is sequential. `SO_RCVTIMEO` = 6 s is per `recv()`: sending 1 byte every 5 s holds the only slot for 256 × 5 s. Anyone on the dongle Wi-Fi can hold off a knock | Overall deadline per connection (e.g. 8 s total via `select()` on the remaining time); plus A2 |
| C2 | P1 | **A scanner can fill `/var`.** Every bad knock appends a line to `/var/opt/aa/knock.log` with no cap. A full `/var` can break stock HU functions, and it wears the flash | Cap the log (e.g. 64 KB, then truncate or rotate); rate-limit "bad knock" lines (one summary per minute) |
| C3 | P2 | **The `/dev/urandom` fallback is predictable.** It seeds `rand()` with `time ^ getpid ^ &out`. pid and stack address are constant in one process, so the nonce depends only on the clock, which may sit at a reset value early in boot | No randomness, no service: fail closed |
| C4 | P2 | `system()` together with `SA_NOCLDWAIT`: `system()`'s `waitpid` can fail with ECHILD, so the `SSHCMD rc` log line is unreliable. Every valid knock also runs `sshd` again, even when it is already up | Spawn sshd as a child knockd tracks (also needed for A3); skip if its pid is alive |
| C5 | P2 | The signature covers only `SHA256(nonce)`, with no context (protocol, HU identity). Not exploitable as is. Combined with the A4 key reuse it is the kind of thing that turns into a problem later | v2 protocol: sign `"nbt-knock-v2" ‖ hu-id ‖ nonce`; keep v1 accepted until knock.py moves over |
| C6 | P3 | Stale docs: the header and the `conf_port` default say 46610 (the deployed value is 851). `RSA_FLAG_NO_CONSTTIME` on the hex path is pointless for a public key (harmless). `send()` has no timeout | Tidy along with the v2 work |
| C7 | P3 | No host test | Host build + a test with a throwaway key: good / bad / short / slow signature, off-allowlist peer, log cap. Done: `test_knock.py`, 42 checks, plus a `Makefile` (the car build line was nowhere recorded) |

---

## B. Firmware updates

### B1. Firmware updates: DECIDED 2026-11-03
The head unit will not be updated (no dealer programming of the HU). B2–B4 are kept in
the follow-up list as a precaution.

---

## E. Development speed and repo hygiene (decisions 2026-11-03)

### E1. Less car time per answer
- **E1a. Desk test rig — DONE 2026-11-03** (`id5/tools/desk-rig/`; built, not yet run against a phone: needs the udev rule in its README). `AASession::start(int fd)` takes any socket, and
  `aa_session.cpp` / `aa_cryptor.cpp` include no QNX headers. USB is already bridged to a
  socketpair (usb_accessory.hpp). A Linux host program could do the whole chain at the
  desk against the real phone:
  - libusb AOAP switch (identity strings, protocol 1/2) + bulk bridge -> socketpair ->
    the real AASession;
  - stub callbacks that dump H.264 to a file for ffplay.
  It answers the phone-side questions: accessory accepted? version? TLS with our cipher
  list? service discovery accepted? video starts? Every ID5 USB failure so far except the
  QNX-only part (driver claim, io-usb, NbtMediaMainApp stealing) was on that list. The same
  program on TCP covers wireless protocol work. A test tool upstream could use too.
- **E1b. `car.sh run <N> <bin>` — DONE 2026-11-03** (stub-tested; the detached launch is not car-verified yet). One command that kills leftovers, clears logcat, launches
  with the standard env, prints "plug the phone in", waits until a stage is reached or
  60 s pass, then runs `collect N`. Run time goes from about 5 min of pasting to about 1 min.
- **E1c. Batch each car session** (practice, no code). Prepare the run list with branches in advance (the
  decision tree already does this). Do the read-only recon (0e) in the same session.
- **E1d. Test from indoors — not now** (revisit after E1a/E1b). A small Linux box (Pi or old laptop) on the
  car's ENET, joined to home Wi-Fi when the car is parked at home. car.sh runs on it, and
  the phone in the car uses adb over home Wi-Fi.
  - You only need the car awake on the charger. You don't need to sit in it.
  - It keeps the A2 allowlist intact, because the box talks to the HU over ENET.
  - Open questions: home Wi-Fi reach to the car, and replugging the phone without a hand.
    Test whether `adb shell svc usb resetUsbGadget` is a good enough "replug"; otherwise
    use a switchable USB cable.
- **E1e. Bench head unit — not now** (costs money). A used NBT Evo + display + bench
  harness + 12 V supply, with one paid unlock, then knockd.
  - Covers USB AA, video, Bluetooth and the uAP/Wi-Fi work: everything inside the HU, so
    nearly all of goals 1–3.
  - It cannot test the controller knob (MOST), the car's audio routing or vehicle data.
  - Bench wake-up requirements need research before buying.

### E2. Repo issues spotted
- **E2a. DONE 2026-11-03. Personal identifiers on the public fork:**
  - car BT MAC `<car-bt-mac>`;
  - the car's Wi-Fi Direct SSID `DIRECT-xx-BMWnnnnn` + GO BSSID;
  - "BMW nnnnn": BMW names the HU after the last 5 VIN digits.
  History rewritten with git-filter-repo, limited to our commits after the upstream base
  `dc4adba`, so upstream hashes are kept. Then force-pushed: `b408b13` -> `c5053db`.
  Backup bundle and the list of old SHAs are in `~/Desktop/Projects/BMW/backups/`.
  Remaining step for the user: ask GitHub Support to purge the old commits. Until then
  they can still be fetched by SHA through the fork network.
  The generic root password in the docs is upstream's published one (not ours), so that is
  fine.
- **E2b. DONE 2026-11-03 (`id5/tools/git-hooks/`). The pre-push guard only checked file names and sizes.** It let the identifiers
  above through. Add content patterns (MAC addresses, `BMW[0-9]{5}`, `DIRECT-..-BMW`,
  PEM private keys).
  Also, the hook is a relative symlink into the main worktree's `tools/`. If that worktree
  checks out `main`, the link dangles and git **silently skips** the guard. Fix: a
  hook that refuses to push if the guard script is missing.
- **E2c. DROPPED 2026-11-03: not ours to change without the owner asking.** Upstream contributions with no car needed:
  - a `.gitignore` for build outputs;
  - a host-test runner (`tests/run.sh` or `make check`): 29 tests, each with its own build
    line;
  - GitHub Actions running the host tests.
  Reason: upstream deliberately commits some binaries (`tools/test_controls`, the
  rhmiapp zips), and the owner uploads through the GitHub web UI, so a `.gitignore`
  could get in their way. Each test documents its own build line, and the readme says
  WSL is fine, so the test setup works for them as it is. Revisit only if they ask.
  The first upstream contribution is USB (see §G).
- **E2d. P3 Staged-binary manifest** (part of E1b: `car.sh` / build-variant). `collect` already tells builds apart by the
  `Build: ... (date time)` line. A `milestone-7/BINARIES.txt` written at build time
  (name, size, build time, commit, cherry-picks) makes that mapping explicit. A
  `tools/build-variant.sh <ref> <name>` (scratch worktree -> build -> copy -> manifest) also
  avoids building in a worktree that has stale objects from another branch.

---

---

## G. First upstream contribution: USB (how the owners work)

Observed (2026-11-03):
- Sq00ky merges every PR. Contributors (tom-pryor, dn00, houssems) send focused PRs.
- New behaviour is opt-in and off by default (`NBT_*`), with host tests, and `readme.md`
  marks "Car-verified" or "Not car-verified".
- Contributors never bump `NBT_AAD_BUILD`: the maintainer does that in release commits,
  with credits.
- PR #26 was closed in favour of overlapping work already merged, so check open PRs
  before starting.
- USB is tom-pryor's PR #42 (with #65 for no-CarPlay cars). He asked for testers.

So, in order:
1. **Test reports on #42 and #65** from the ID5 (no CarPlay coded). Give facts per run:
   binary/commit, env, stage table, what worked. This is the most useful thing we can give,
   and it costs the owners nothing.
2. **Fixes found on the ID5 go to tom-pryor first**, as a suggestion in the PR thread or a
   small PR against his `usb-accessory` branch, not a competing PR to main. Each is reshaped
   into general behaviour: detected or `NBT_*`-gated, host-tested, readme updated, written
   in the commit-subject style of the repo. Example: hand the phone from Bluetooth to USB,
   instead of the local kill-bait deregister.
3. **After #42 merges**: follow-up PRs to main on topic branches from `upstream/main`.
   For example: a `requirements.md` row for our ID5 firmware, and USB robustness fixes.
4. Never from id5-dev, never with id5/ files, never with personal identifiers (the guard
   checks pushes to the fork; upstream PRs are opened from those topic branches).

---

## F. Follow-up list (agreed in principle, not scheduled)

### F1. (was A4) P2 — Separate keys and multiple keys
- **Separate keys.** `genkey.sh` uses one RSA key both to sign knocks and as the SSH login
  key. Hash and input lengths differ, so I see no practical cross-protocol attack. Still,
  one key per purpose is basic hygiene and costs nothing.
- **Multiple keys.** knockd accepts one key. Allow a key list, `PUBKEY_DIR`, with one file
  per device (laptop, phone), and log which key knocked. Then a lost phone means deleting
  one file. Pair each key with an `authorized_keys` entry.

### F2. (was A6) P3 — Phone tool over the car's Wi-Fi (after the internal AP works)
- Feasible with A2's allowlist (uap0) and a phone key (A4).
- A phone is a more exposed place for a key than an encrypted laptop (apps, backups,
  loss), so give the phone key a restricted entry in `authorized_keys`:
  `command="/var/opt/aa/phone-cmd.sh",no-pty,no-port-forwarding ...`. It then only runs a
  fixed set of actions (status, restart AA, fetch logs), not a root shell.
- Check that the HU's OpenSSH version supports these options (`sshd -V` / `ssh -V`).

### F3. (was A7) sshd_config clean-up (current file: `id5/tools/hu-install/sshd_config`)
None of these options has a comment or note saying why it was set.

| Option | Now | What it does | Probable reason it's set | Plan |
|---|---|---|---|---|
| `PermitUserEnvironment` | yes | Lets `~/.ssh/environment` or `environment=` in authorized_keys set session env vars (LD_PRELOAD, LD_LIBRARY_PATH...). Off by default because it can get around `command=` restrictions | Probably to give sessions the QNX `LD_LIBRARY_PATH`. Nothing we use needs it: car.sh sets env on the command line | **no**. Needed for A6's `command=` keys to mean anything |
| `StrictModes` | no | sshd refuses an authorized_keys file or directory that others can write. That stops another local user from planting a key | Very likely because QNX dirs on the path (`/var`, `/var/opt`, `/var/opt/aa`) are group/world-writable or not root-owned | Move `authorized_keys` to a root-owned 0700 dir with a 0600 file. Test with a **second sshd on another port** before switching. Turn it on only if the parent dirs pass; otherwise leave it off, since we're the only user |
| `UsePrivilegeSeparation` | no | Privsep parses pre-auth network input in an unprivileged chrooted child. Off = that code runs as root | Needs an `sshd` user and an empty root-owned chroot dir. `/etc/passwd` is read-only on the HU | Check `grep sshd /etc/passwd` and the OpenSSH version. It probably stays off. knockd + A2 + A3 are what make that acceptable |
| `HostKey ..._dsa_key` | on | DSA host key: obsolete and weak | Copied from a stock template | Remove the line |
| `ChallengeResponseAuthentication` / `KbdInteractiveAuthentication` | unset (old default = yes) | Keyboard-interactive auth, a second password-style path | Not set | Set **no** explicitly |
| `PermitRootLogin` | yes | Root login | Root is the only account | `without-password` (same effect with PasswordAuthentication off, but it states the intent). **Our sshd only**: the stock dropbear on :22 has its own settings and keeps password login |

### F4. (was B2) P1 — Keep everything ours on the writable partition
- iDrive also loads plugins from `/var/opt/hmi/data/ID5/Plugins/` (upstream readme, "The
  iDrive app").
- If the hook plugin (A1a) and knockd/sshd/keys all live under `/var`, nothing of ours is
  on the system image an update rewrites.
- Unknowns:
  - whether an update wipes `/var`. User data such as BT pairings and settings surviving
    updates would hint it is kept;
  - whether a new HMI version changes the PluginApp ABI, in which case our plugin no
    longer loads.
- Recon (read-only): `mount`, `df -k /var`, `ls -ld /var/opt/hmi/data/ID5`.

### F5. (was B3) P2 — Look for a second hook that doesn't depend on the HMI
A stock component that runs something from `/var` or USB without a signature check (a
start-up script, update helper or diag hook) would give a second way in if the HMI hook
breaks.
- Method: from pulled binaries and start-up scripts, grep for paths under `/var`,
  `/fs/usb*` or `/mnt` used with `sh` / `spawn` / `system`.
- This is research with no promised result.
- Aim is a hook that starts KNOCKD (not sshd), so there is no permanent-exposure downside.
- READ-ONLY recon queued for the 2026-11-03 session (NEXT-SESSION B9c): pull the start-up /
  supervisor config, grep on the laptop, note the SW version. No writes to stock boot files,
  no install, no restore-kit this trip. Prefer a no-edit drop-in; decide how to hook later.

### F6. (was B4) P2 — If the foothold is lost anyway
- One unlock from any provider gets it back.
- Keep a single `restore.sh` + tarball on the laptop that reinstalls everything ours in
  one go, and test it.
- TurboCode is the project's partner, so its free unlocks are likely to stay while the
  project is alive; bavarian is a second provider.
- No hardware route has been researched (pulling the unit, eMMC, serial console). It
  carries real risk to the unit; last resort only.

---

### F7. P2 — pf anchor so knockd/sshd answer on the car's Wi-Fi (uap0) (from A2, 2026-11-03)
knockd v2 already allows 172.16.224.0/24, but pf only passes TCP 851/3804 on dwc0. The `PFCMD` hook and
`id5/tools/knock/pf.nbtaad.sample` ship, commented out. Load it when the car's own AP works (wireless goal / F2),
after the `pfctl -s rules` / `-s Anchors` recon. Until then, Wi-Fi clients can't reach the gate, which is fine.

### F8. P3 — Idle-shell timeout (`TMOUT`) for our sshd sessions (from A3, 2026-11-03)
Left out on purpose: it would cut long `car.sh collect` / `run` sessions short, and the ClientAlive keep-alives
already drop dead sessions. Review later: maybe a long value (e.g. 2 h), or only for interactive `login`
shells and not `car.sh sh` commands.

### F9. P2 — Decode Android Auto video on the IVA-HD directly (renderer option A, 2026-11-03)
Today the daemon feeds H.264 through mm-renderer (CarPlay package). Option A: open the IVA-HD ourselves through
TI DCE (the car's own libdce, dlopen'ed like the mm-renderer libs today) and post the frames to our screen window.
Gains: no mm-renderer process or CarPlay files, most of video_manager's workarounds gone (cpvideoctx purge, speed
seeding, prefetch, per-session rebuild, renderer start), likely coexists with CarPlay, per-frame stats and keyframe
recovery, and a second concurrent decoder for Android Auto's CLUSTER stream (F10; but the F36 cluster has no known
video input, so that one only matters for other cars). Not a gain: picture quality (the
1280x480 panel is already shown 1:1) or much latency (mm-renderer already runs VideoDecoderLowLatencyMode=1).
Rejected: Cinemo as a player (playback-tuned delays) and Cinemo's AA engine (closed; the engine plugin wasn't in
the pulled files). Comparison page: `id5/local/docs/renderer-options.html`.
Order: after USB works, measure touch-to-picture lag; run `id5/tools/recon/ivahd-recon.sh` (B2); if the decoder
stack is reachable, a standalone test tool first (like test_display), the daemon untouched.

### F10. P2 — Content on the instrument cluster / HUD (F36) (2026-11-03)
Goal: the Google Maps map from Android Auto's CLUSTER video stream in the cluster; fallback: turn arrows, distance,
lanes, street name (what upstream does on the J29 over SOME/IP 0xB006, which the F-series doesn't have).
Research (2026-11-03, web + local recon; nothing car-checked yet):
- **F30-platform facts** (BMW F30 training doc "Displays, Indicators and Controls", same platform as F36):
  - The basic cluster (2.7" 320x120, PT-CAN only) is fitted only without 609 Professional nav or 610 HUD.
  - **6WA** (5.7" 640x160 TFT, on **MOST and PT-CAN**) is required with 609 and 610. NBT Evo is the 609 unit, so this car
    most likely has a 6WA (or the later 6WB digital cluster). To be confirmed from the FA/VCM or the KOMBI part number.
  - Nav in the cluster = **arrow display** plus distance/arrival on the computer pages. **No map in the cluster**
    on F-series is documented anywhere.
  - HUD (610): the cluster draws the HUD picture and sends it over APIX/LVDS. The HUD content is data-driven, drawn by
    the cluster, so arrows reaching the KOMBI also reach the HUD.
- **Map video (Android Auto CLUSTER stream): not feasible on the F36 as far as known.** No video input on the cluster
  (the APIX link runs cluster to HUD, an output). So F9's second-decoder benefit doesn't apply to this car.
- **Methods, ranked** (all reuse the NAVIGATION_STATUS data `kombi_nav` already parses for the J29):
  1. **MOST capture of native guidance, then replay.** The HU sends turn arrows to the KOMBI as asynchronous MOST data (BMW
     MOST training doc). `ccs_input` already observes MOST (FBlock 0xE5 via `registerDevCtrls`), so the first step is
     to observe the nav-to-KOMBI messages while an **iDrive route** runs. Then, much later, send our own guidance
     updates the same way. Confidence med (arrows over MOST), low on the bytes.
  2. **Feed the HU's own guidance service** (a DSI nav/guidance service the HMI publishes), so the stock path draws
     the arrows in the cluster and HUD. Confidence low-med. Check: DSI services with Guidance/Kombi in their names.
  3. **K-CAN / PT-CAN injection.** Rejected: 6WA nav isn't CAN-carried, F-series PT-CAN frames carry CRC + counter,
     and it's safety traffic.
  4. **Map video to the cluster.** Rejected for the F36 (no video input known).
- **Car checks (read-only):**
  - Start an iDrive route, then `sloginfo -w | grep -i -E 'kombi|guid|route|arrow'`.
  - `strings` on HmiMain3D for Kombi / RouteGuidance / ManeuverView.
  - LayerManager screens (`ivahd-recon.sh` section 7).
  - FA/VCM options via E-sys or ISTA.
- **Safety:** observe first, send nothing. Later, only guidance updates (never Check Control, warnings or lamps).
  Clear on disconnect or crash. Stand down while the native nav guides.
- **Owner answers (2026-11-03):** the cluster has the screen between analogue dials (**6WA**), **no HUD**, and iDrive
  navigation works and already shows its arrows in the cluster. So the native path exists on this car (method 1 or 2).
- **Next:** runbook B9d (`id5/tools/recon/cluster-recon.sh`, read-only, during an iDrive route) finds the processes, configs
  and MOST function blocks behind the arrows. Then, in a later session, an observe-only MOST capture with `test_controls -r
  -k <those FBlock keys>` during guidance. Caveat: `registerDevCtrls` delivers messages addressed to the HU, and the arrows
  go out of it, so the capture may only show the cluster's replies. The DSI/HMI side (method 2) may then be the better tap.
- Sources: F30 displays training PDF (archive.org BMWTechnicalTrainingDocuments ST1113), F01/E70 HUD training PDFs
  (ST811, ST605), BMW MOST bus training (ST043), bimmerpost thread 1512884 (NAVI_NBT_ENABLE coding lead; unread, 403).

---

## D. Other backlog (tidiness, robustness, reliability)

- P2 `pr42-id5`: squash `6a17ca7` + `8a8db25` (was 6207f6c) before anything goes public. Keep only the
  patches a car run proves necessary. Update `test_usb_accessory` if protocol 2 stays.
- P2 USB mode in the boot wrapper (NEXT-SESSION item, once USB works end to end).
- P2 Rebase pr42 work onto upstream main v1.8.67 when PR #42 moves or merges.
- DONE 2026-11-03: public fork hygiene (see E2a).
- DONE 2026-11-03: recon scripts use car.sh; hu.sh moved to `id5/tools/legacy/`.
- P3 Split findings.md into a short current-state page and a dated log. Several notes there were
  wrong and later corrected in place.
- P3 Drop or archive the dead-end tools (mwu-gdb / pdebug / inject, the mitm relays) once
  the wireless work has decided whether it needs them.
- P3 VM autostart is still on (`virsh -c qemu:///system autostart --disable qnx-build`).
