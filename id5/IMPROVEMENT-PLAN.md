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
- **F9. Finish the unlock capture** (needs a factory-unlock / diag session, read-only). The sessions in
  `local/unlock/` only stored the challenge and our outbound FD2E/FD2F/FDAC frames, never the car's positive
  responses. Next unlock, save them: the FD2F accept (`71 01 FD2F ...`) and FDAC status (`71 01 FDAC <status>`),
  by running `tools/legacy/nbt-ssh-unlock.py` with its console log kept, or a pcap on the ENET leg. Also pull the
  version strings we have no record of (the recon `head` pipe dropped them): iStep from `/fs/sda0/repository/istep`
  and the bootloader banner `/etc/bootloader_banner.txt`, for BTLD/SWFL. Feeds the Path-B verifier question (does
  FD2F verify against the same Root/SigS/FscS chain as FSC). The 0x63 the unlock targets is the **jacinto** node
  (`HUJacintoStage1` binds `--diag=DIAGADR63`, diag high app `NbtEvoJacintoDiagHUHighApp`); the earlier pull got the
  **omap** variant and `/opt/sys/bin/NbtDiagHuHighApp` came back 0 bytes, so pull the jacinto diag binary and
  whatever implements the FD2E/FD2F/FDAC routines. Note the WS `0x01` metadata is client->server only and never
  reaches the car, so it cannot be part of what the car verifies (the car sees only FD2E/FD2F/FDAC). Definitive
  answer: the verifier binary and its embedded modulus are on the live HU, so pull them and read the modulus +
  algorithm straight out (no external SDP needed for that). The BMW SDP flash container (`swfl_0000271a...`) is
  BMW's distribution package, not on the car, so it would be sourced externally and only as a cleaner
  cross-reference. SDP/firmware stays in `local/`, never the repo.

  **Recon block for the next car session (collects everything the other agent asked for).** Run `ls` first where a
  path or filename is a guess; the HU ksh has no `head`/`env`/`nc`/`strings`, so pull binaries and run `strings` on
  the laptop. `C=id5/tools/car/car.sh`.
  ```
  # 1. Versions (read-only, via knockd; no unlock needed).
  $C sh 'cat /etc/bootloader_banner.txt; echo ===; ls -l /fs/sda0/repository; ls -l /fs/sda0/repository/istep'
  $C sh 'ls /etc | grep -i vers; cat /etc/*version* 2>/dev/null'
  # pull the istep/version files that look textual, then `strings` them on the laptop for SWFL/BTLD/I-Step.

  # 2. Pull the verifier + cert-chain binaries. 0x63 is the jacinto node, reachable from the omap shell via /net.
  $C sh 'ls -l /bin/NbtEvoJacintoDiagHUHighApp /bin/HUJacintoStage1 /opt/car/bin /opt/sys/bin'
  $C sh 'ls -l /net/hu-jacinto/bin/*DiagHUHighApp /net/hu-jacinto/opt/car/bin 2>/dev/null'
  $C pull /bin/NbtEvoJacintoDiagHUHighApp        # or the /net/hu-jacinto/... path if it is jacinto-only
  $C pull /bin/HUJacintoStage1
  $C pull /opt/car/bin/NBTCarHUTwo                # Root/SigS/FscS chain per the Path-B research
  # (re-pull /opt/sys/bin/NbtDiagHuHighApp too - last time it came back 0 bytes.)

  # 3. Raw car responses to FD2E/FD2F/FDAC + the WS 0x01 metadata. Needs a real unlock (bavarian server + laptop
  #    internet). Capture both directions so nothing is lost:
  #    term A - relay with a full log:
  tools/legacy/mitm_relay.py | tee id5/local/unlock/mitm-$(date +%Y%m%d-%H%M).log
  #    term B - pcap on the ENET leg to the HU and the WSS (tcpdump/wireshark -> .pcapng in id5/local/unlock/)
  #    term C - our client through the relay, console teed (it logs each car response):
  tools/legacy/nbt-ssh-unlock.py <car-ip> --server ws://127.0.0.1:8443 --insecure 2>&1 \
      | tee id5/local/unlock/unlock-full-$(date +%Y%m%d-%H%M).log
  ```

## G. Fixes from the 2026-11-05 car session (runs 16-25 in NEXT-SESSION)
- **G1. Browser audio holds the source like a real media source** (first PR candidate). Today the daemon gives the
  browser source (`0xb8`) back after `NBT_BROWSER_IDLE_S` of silence; on a pause the car's Bluetooth source then plays
  the phone and the music resumes (runs 19-21). What the owner wants:
  - A pause keeps the source, however long.
  - The source is let go only when the driver changes it: a source picked in the iDrive menu, the MODE button
    (wheel `15/02 fkt 513`), or anything else that is a deliberate source change.
  - While another source (radio) plays, pressing play in Android Auto takes the source back and holds it again until
    the next deliberate change.
  - Changing the source doesn't minimise Android Auto (today MODE takes iDrive to its source screen, hiding AA).
  - Design (2026-11-06): hold through pauses (`route_policy`), and on a source change release the source, tell the
    phone it lost audio focus (it pauses) and don't take the source back until the phone asks for focus again (play).
    MODE decode verified (15/02 fkt 513); the menu's source change has no verified signal yet: needs one browser-mode
    capture with the bus log on. New setting, off by default upstream. Raise it on an issue before a PR.
- **G2. Volume floor 25 in browser mode.** The browser source keeps its own level with a minimum of 25 (#81). The
  owner doesn't want the music cut (`NBT_MEDIA_GAIN_DB` rejected): find where the minimum lives (coding, the stock
  audio manager) or a source without one.
- **G3. Cluster now-playing in browser mode** shows the last Bluetooth song; its arrows don't reach AA (#51). Needs
  AA to be a real media source for iDrive (the AAiDrive model). Wheel decode: `local/recon/wheel-2026-11-05/`.
- **G4. Long-press voice** (`NBT_PTT_ASSISTANT=1`) also starts the phone's Bluetooth assistant (iDrive's own long
  press); the two clash. Left off.
- **G5. Black screen** (runs 20, 22; once more on a drive, 2026-11-06). Two kinds: run 20 = the picture gone with
  the volume pill showing (the pill window at z=0 in runs 19, 20, 22, z=999 in the working ones), which upstream fixed
  in v1.9.18 by giving the pill its own surface 1281; run 22 = the renderer going silent after a reboot. base14
  (v1.9.19 + #91 + browser-hold) is the candidate daily build; the drive's logs decide which kind it was.
- **G6. #40 input leak**: not reproduced with single BACK or MEDIA presses; next: bursts of hard keys, OPTION. Upstream's
  new `car/input_activity` (v1.9.18, in main) only reports that the driver touched a control, for the iDrive app's
  step-aside logic; it doesn't stop input reaching iDrive (Sq00ky on #40: "lots of work is needed"). The app's hold
  work since rhmiapp-22b (ours) may matter: updating the app is its own step.
- **G7. `car.sh` ~15.5 s per call** (fixed 2026-11-06): not the knock (none ran) but each SSH connection's setup,
  probably sshd's DNS lookup of the laptop (`UseDNS`). car.sh now shares one OpenSSH connection for 15 min (~0.05 s
  per call, `tools/car/test_car.sh`), polls with `grep -c` on the HU instead of copying the log, and pushes in one call;
  `UseDNS no` is in the sshd template, for the car after `car.sh ssh-diag` confirms.
