# Our SSH access, and the backlog that is ours alone

Work that upstream does (car GPS, cluster arrows, audio, video) is not planned here: it is tested on this car once
it is merged. This file covers what only this car needs. The full decision history is in git:
`git show 934156c:id5/IMPROVEMENT-PLAN.md`.

## A. SSH access (knockd v2 + NbtService): built and host-tested, not car-tested

| Item | Today (v1) | v2 decision | Code |
|---|---|---|---|
| A1. SSH independent of Android Auto | knockd only runs if something starts it; the stock boot doesn't | The USB start-up script starts it (interim). NbtService, an iDrive plugin of its own, starts it at boot without the Android Auto app | `tools/hu-install/nbt-aad-usb.sh`, `tools/nbtservice/` |
| A2. Only from the cable and the car's Wi-Fi | knockd and sshd listen on all addresses; the car's internal network passes everything | `ALLOW_NETS` in knock.conf (link-local and the car's Wi-Fi); others are dropped before any parsing | `tools/knock/knockd.c` |
| A3. A window, not until reboot | sshd stays up from the first knock until the next boot | A knock opens sshd for `OPEN_S` (900 s), then knockd stops the listener; sessions in progress continue | `knockd.c`, `sshd_config` |
| A5. A switch in the iDrive menu | none | "SSH for 15 minutes" in NbtService opens the A3 window without a knock; being in the car is the authorisation. Placement: Media first, then Settings once the anchor is known | `tools/nbtservice/` |
| A8. Key-only root | root could log in with a password | `PermitRootLogin without-password` in our sshd_config only; the stock SSH is unaffected | `tools/hu-install/sshd_config` |

C. The knockd review findings, all fixed in v2: a slow client could hold the only slot (C1); a scanner could fill
`/var` through the log (C2); the random fallback was predictable (C3); `system()` exit codes were unreliable
(C4); the signature now binds the protocol and this head unit (C5); stale docs (C6); a host test, `test_knock.py`
(C7).

Car test: NEXT-SESSION steps 4 and 5.

## F. Ours, not scheduled

- **F1. Keys.** One key per purpose (knock signing vs login), and a key list so a lost device can be removed alone.
- **F3. sshd_config clean-up.** Several options have no recorded reason (`PermitUserEnvironment` among them).
  Moving the file to a root-owned 0700 folder would let `StrictModes` come back.
- **F4. Everything ours on `/var`.** If the iDrive plugins (via `/var/opt/hmi/data/ID5/Plugins`), knockd, sshd
  and keys all live on `/var`, a firmware update that rewrites the system partitions can't remove them.
- **F5. A second boot hook.** A stock component that runs something from `/var` or USB unsigned would keep a way
  in if the HMI hook ever breaks. Read-only search of pulled binaries and start-up scripts.
- **F6. If access is lost anyway.** One factory unlock gets it back; keep a tested `restore.sh` plus tarball that
  reinstalls everything ours in one go.
- **F7. SSH over the car's Wi-Fi.** The packet-filter anchor (`tools/knock/pf.nbtaad.sample`) ships commented out.
  If wireless runs over Wi-Fi Direct (172.16.222.0/24, not the hotspot's 172.16.224.0/24), `ALLOW_NETS` needs that
  network too. Only once wireless works.
- **F8. Idle-shell timeout.** Left out on purpose (it would cut long `car.sh` runs); keep-alives already drop dead
  sessions.
