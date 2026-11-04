# What is verified on this car

BMW F36, NBT Evo head unit on ID5 software (iStep `NBTevo_W20204I`), Professional navigation, 6WA instrument
cluster (screen between analogue dials), no head-up display, **no CarPlay licence**. Everything below was seen on
this car unless it says otherwise. The step-by-step history is in git: `git show 934156c:id5/findings.md`.

## Head unit and access
- Two QNX 6.5 nodes joined by QNET: `hu-jacinto` at 169.254.199.99 (diagnosis, HSFZ port 6801) and `hu-omap` at
  169.254.199.119 (the HMI, the media stack and our daemon). The laptop reaches both over the ENET cable.
- **Stock path, never changed:** the factory debug-gate unlock over the diagnosis port opens the stock SSH on port
  22 until the next reboot (`car.sh fallback unlock`; it needs laptop internet). See `tools/legacy/README.md`.
- **Our path:** `knockd` on port 851 opens `sshd` on port 3804 (key only) for one signed knock (`car.sh knock`).
  It runs only if something starts it: the stock boot does not (2026-11-03), hence the start-up script and NbtService.
- `sshd` is OpenSSH 5.2. `/etc/passwd` has no `sshd` user, so privilege separation is impossible, and
  `/var/opt` is group-writable, so `StrictModes` has to stay off.
- The shell is minimal: no `head`, `env`, `nc`, `nohup`, `python` or `scp`, and `find` was missing in the first
  recon. `car.sh push/pull` copy over the SSH session.

## File systems
- `/opt` is on `/fs/sda0`, read-only until `mount -uw /fs/sda0` (and back with `mount -ur`).
- `/var` is `/fs/sda1`: about 500 MB, 400 MB free, writable, persistent. Everything of ours lives in `/var/opt/aa/`.

## How Android Auto starts
HmiMain3D loads the iDrive app (`/opt/hmi/ID5/data/ro/Plugins/NbtAad`), which runs `/opt/aa/nbt-aad` at boot and from
its menu entry, with the variables from `/opt/aa/nbt-aad.env` plus the HMI's own environment. That environment
includes an `LD_PRELOAD` that kills `mm-renderer` and the daemon, so the start-up script unsets it first. Upstream's
readme says HmiMain3D also loads plugins from `/var/opt/hmi/data/ID5/Plugins` (not yet seen here).

## Video
- The CarPlay stack is installed but dormant (licence-gated), so the head unit never starts `mm-renderer`. Either
  the start-up script starts it from `/fs/sda0/opt/conn/bin/carplay/` with our config, or upstream PR #65 lets the
  daemon start the factory one (run 15: works here; there is no `carplay.ini` on this car).
- The panel is 1280x480; upstream's crop mode fits Android Auto's picture to it.

## Android Auto over USB
- Works with upstream main since the USB merge (#42), with `NBT_USB=1 NBT_USB_DIRECT=1`: the daemon claims the
  factory accessory driver's path so it never starts, switches the phone to accessory mode itself and reads it
  through libusbdi. Runs 13-15: video, touch, controller, Bluetooth audio, Assistant, calls, reverse, replug.
- `NBT_VEHICLE=bmw` gives the BMW name and the exit icon on the phone.
- Every good session starts with the phone's 12-byte version answer on channel 0, 0.3-0.5 s after the open.
- **Known problem:** if the head unit ends a session while the phone stays plugged in (iDrive powered down for low
  battery and was switched back on), the reopened connection carries the phone's old encrypted session and nothing
  decrypts until a replug. A fix is written (NEXT-SESSION step 3).

## Audio (default `bt` mode)
- Media plays over Bluetooth A2DP; navigation prompts and Assistant go over Bluetooth too; calls over HFP; the
  microphone goes through the car's speech path to the phone over the Android Auto link.
- `NBT_AUDIO_MODE=browser` puts all of it on the Android Auto link instead, which is how Google's own head units
  work. Upstream improved it a lot in #76; not tried on this car yet.

## Network and packet filter
- The packet filter is `block drop all` with exceptions. `veom0` and `vlan80` (the car's internal network) pass
  everything, so anything listening on all addresses is reachable from inside the car: knockd v2 restricts this.
- `uap0` (the CarPlay hotspot) only passes the CarPlay and Android Auto ports and 49512-65535.
- There is no rule naming the ENET interface `dwc0`, yet the cable works: how is still open (`pfctl -a filter_OBD`).

## Wi-Fi
- `uap0` is up at 172.16.224.1 with its beacon off: it is CarPlay's hotspot and stays dark without the licence.
- `wfd0`, the Wi-Fi Direct group the car uses for screen mirroring, is up at boot at 172.16.222.1, run by the
  stock `mwud`. This needs the screen-mirroring coding, which was off on this car and was switched on with
  BimmerCode (2026-11-01): `MIRRORING`, `ENT_MIRACAST` and `ENT_MIRACAST_DISPLAY_SETTINGS` set to active
  (`WLAN_WIFI_DIRECT` and `WLAN_STATUS` were already active). No CarPlay licence was needed. Turning it off again
  would remove the wireless route below; it also brings an occasional screen-mirroring pop-up. Its passphrase is stored encrypted in `/var/opt/conn/WFDPass.conf`; upstream's `NBT_ALT_WIFI=DIRECT`
  decodes it and offers the group to the phone. This is the wireless route for this car (NEXT-SESSION step 6).

## Car data seen so far
- GPS: the receiver is on `/dev/ser3`, owned by `dev-navsens`. Nothing of ours reads it (a second reader would
  steal bytes from the navigation). Upstream PR #77 reads the navigation's fix from MOST instead; whether this car
  sends the same messages is not checked yet.
- The phone asks for sensors 1, 2, 3, 7, 8, 10, 13, 19, 21 and 22 at every session start.
- The cluster's navigation arrows: on F-series cars the head unit draws them with commands over MOST (upstream
  issue #52). Not captured on this car yet.
