# ID5 improvement plan

Living backlog for robustness, reliability, efficiency and tidiness. Not the car-test
runbook (that is NEXT-SESSION.md). Started 2026-11-03.

Priority: **P1** = lockout or safety risk, do soon. **P2** = clear win. **P3** = tidy-up.
Every head-unit change gets its own car step: never in the same run as a USB/AA test.

---

## A. Head-unit access (knockd v2)

Goal (unchanged): SSH to my own car with no third-party unlock service, and no way to lock
myself out.

### A1. P1 — Separate SSH access from the AA wrapper
Today: HMI loads the NbtAad plugin -> plugin sees `/var/opt/aa/autostart` -> launches the
wrapper `nbt-aad.sh` -> the wrapper starts knockd. Anything that breaks that chain also
removes SSH: a wrapper edit, autostart moved aside (planned for one hypothesis test), or the
plugin's boot guard latching autostart off after 3 crash-boots in 15 min.

Options:
- **a. A small dedicated PluginApp ("NbtService")** that only starts knockd when HmiMain3D
  loads it. It doesn't depend on the AA app, its autostart flag or its boot guard. It is
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

### A2. P1 — Restrict which networks can reach knockd and sshd (allowlist)
Today, knockd binds `INADDR_ANY` and `sshd_config` has `ListenAddress 0.0.0.0`. "ENET-only"
is not true: the BM3 dongle's Wi-Fi reaches dwc0, and so would `uap0` (the car's own AP)
and `wfd0` once they are up.

Decision: an **allowlist** in knock.conf, `ALLOW_NETS=169.254.0.0/16,172.16.224.0/24`
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

pf already shapes this (id5.md §11): the default is `block drop all`; dwc0 passes the
stock OBD/diag rule (knock on :851 works); uap0 passes UDP 49512–65535, DNS/mDNS and the
CarPlay ports to AP clients. So **TCP 851/3804 over uap0 also needs a pf anchor rule**
(the `filter_*` anchors can be loaded with `pfctl -a`, which is not persistent: knockd would
have to load it at start). Recon (read-only, NEXT-SESSION 0e): `ifconfig -a`,
`pfctl -s rules`, `pfctl -s Anchors`.

### A3. P1 — Open window + idle timeout
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

### A4. P2 — Separate keys and multiple keys
- **Separate keys.** `genkey.sh` uses one RSA key both to sign knocks and as the SSH login
  key. Hash and input lengths differ, so I see no practical cross-protocol attack. Still,
  one key per purpose is basic hygiene and costs nothing.
- **Multiple keys.** knockd accepts one key. Allow a key list, `PUBKEY_DIR`, with one file
  per device (laptop, phone), and log which key knocked. Then a lost phone means deleting
  one file. Pair each key with an `authorized_keys` entry.

### A5. P2 — iDrive menu toggle "SSH for 15 minutes"
Being in the car is the authorization: the toggle opens the A3 window directly, with no knock.
sshd itself stays key-only.
- Mechanism: the plugin sends `open` to knockd on a 127.0.0.1 UDP port (or writes a flag
  file that knockd polls).
- Where: in the NbtService plugin (A1a). Its own menu entry would hold an "SSH (15 min)"
  checkbox, a status line, "Restart Android Auto" and "Copy logs to USB". The plugin's XML
  declares where the entry sits in iDrive, so the ID5 options (Settings, or next to the AA
  row in Media/Radio) need recon.
- Not in upstream's NbtAad app: that would be a local patch we'd have to carry forever.

### A6. P3 — Phone tool over the car's Wi-Fi (after the internal AP works)
- Feasible with A2's allowlist (uap0) and a phone key (A4).
- A phone is a more exposed place for a key than an encrypted laptop (apps, backups,
  loss), so give the phone key a restricted entry in `authorized_keys`:
  `command="/var/opt/aa/phone-cmd.sh",no-pty,no-port-forwarding ...`. It then only runs a
  fixed set of actions (status, restart AA, fetch logs), not a root shell.
- Check that the HU's OpenSSH version supports these options (`sshd -V` / `ssh -V`).

### A7. sshd_config clean-up (current file: `tools/ssh-vetting/aa-install/sshd_config`)
None of these options has a comment or note saying why it was set.

| Option | Now | What it does | Probable reason it's set | Plan |
|---|---|---|---|---|
| `PermitUserEnvironment` | yes | Lets `~/.ssh/environment` or `environment=` in authorized_keys set session env vars (LD_PRELOAD, LD_LIBRARY_PATH...). Off by default because it can get around `command=` restrictions | Probably to give sessions the QNX `LD_LIBRARY_PATH`. Nothing we use needs it: car.sh sets env on the command line | **no**. Needed for A6's `command=` keys to mean anything |
| `StrictModes` | no | sshd refuses an authorized_keys file or directory that others can write. That stops another local user from planting a key | Very likely because QNX dirs on the path (`/var`, `/var/opt`, `/var/opt/aa`) are group/world-writable or not root-owned | Move `authorized_keys` to a root-owned 0700 dir with a 0600 file. Test with a **second sshd on another port** before switching. Turn it on only if the parent dirs pass; otherwise leave it off, since we're the only user |
| `UsePrivilegeSeparation` | no | Privsep parses pre-auth network input in an unprivileged chrooted child. Off = that code runs as root | Needs an `sshd` user and an empty root-owned chroot dir. `/etc/passwd` is read-only on the HU | Check `grep sshd /etc/passwd` and the OpenSSH version. It probably stays off. knockd + A2 + A3 are what make that acceptable |
| `HostKey ..._dsa_key` | on | DSA host key: obsolete and weak | Copied from a stock template | Remove the line |
| `ChallengeResponseAuthentication` / `KbdInteractiveAuthentication` | unset (old default = yes) | Keyboard-interactive auth, a second password-style path | Not set | Set **no** explicitly |
| `PermitRootLogin` | yes | Root login | Root is the only account | `without-password` (same effect with PasswordAuthentication off, but it states the intent) |

---

## B. Surviving firmware updates without an unlock service

Facts: the first code execution on a stock HU needs BMW's signed unlock (FD2F RSA-2048, and
the HU holds only the public key) or an exploit / hardware access. What we can control is
**not losing** our foothold.

### B1. P1 — Avoid the risk: don't let the head unit be reprogrammed
- At the dealer, ask for "no software update / no programming of the head unit".
  Recalls and other ECUs can still be done.
- Check the job sheet afterwards.
- On returning, run `car.sh status` before anything else.

### B2. P1 — Keep everything ours on the writable partition
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

### B3. P2 — Look for a second hook that doesn't depend on the HMI
A stock component that runs something from `/var` or USB without a signature check (a
start-up script, update helper or diag hook) would give a second way in if the HMI hook
breaks.
- Method: from pulled binaries and start-up scripts, grep for paths under `/var`,
  `/fs/usb*` or `/mnt` used with `sh` / `spawn` / `system`.
- This is research with no promised result.

### B4. P2 — If the foothold is lost anyway
- One unlock from any provider gets it back.
- Keep a single `restore.sh` + tarball on the laptop that reinstalls everything ours in
  one go, and test it.
- TurboCode is the project's partner, so its free unlocks are likely to stay while the
  project is alive; bavarian is a second provider.
- No hardware route has been researched (pulling the unit, eMMC, serial console). It
  carries real risk to the unit; last resort only.

---

## C. knockd.c review (2026-11-03, id5-dev `tools/ssh-vetting/knock/knockd.c`)

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
| C7 | P3 | No host test | Host build + a test with a throwaway key: good / bad / short / slow signature, off-allowlist peer, log cap |

---

## D. Other backlog (tidiness, robustness, reliability)

- P2 `pr42-id5`: squash `6a17ca7` + `6207f6c` before anything goes public. Keep only the
  patches a car run proves necessary. Update `test_usb_accessory` if protocol 2 stays.
- P2 USB mode in the boot wrapper (NEXT-SESSION item, once USB works end to end).
- P2 Rebase pr42 work onto upstream main v1.8.67 when PR #42 moves or merges.
- P2 Public fork hygiene: the BT MAC `<car-bt-mac>` and "BMW nnnnn" are in pushed id5-dev
  files. Decide whether to scrub (history rewrite) or accept.
- P3 Move the recon-*.sh scripts from hu.sh to car.sh; retire hu.sh to fallback-only docs.
- P3 Split id5.md into a short current-state page and a dated log. Several notes there were
  wrong and later corrected in place.
- P3 Drop or archive the dead-end tools (mwu-gdb / pdebug / inject, the mitm relays) once
  the wireless work has decided whether it needs them.
- P3 VM autostart is still on (`virsh -c qemu:///system autostart --disable qnx-build`).
