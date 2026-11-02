# SSH independence — research on permanent unlock paths

Why this exists: bavarian-aa.uk's unlock server could vanish anytime → lockout.
This file collects every known path to SSH on NBT Evo, ranked for our goals:
(1) permanence on THIS car, (2) repeatability on FUTURE cars without the service.

## How the bavarian unlock works (vetted — see README.md)

UDS RoutineControl on HU_NBT2 (0x63): FD2E fetch 8B challenge → FD2F submit
~256B RSA-2048 signature → FDAC enables SSH until reboot. The server holds a
private key the HU trusts (likely a leaked BMW engineering cert or their own
key after a CA overwrite). ~41 captured sessions: all challenges differ —
no replay possible. Signature is raw high-entropy RSA output, no structure.

## Path A — persistent SYS_DEBUG / OPT_DEBUG flag (BEST for this car)

The HU startup config (`tools/ssh-vetting/pulled/starter.cfg`) already
contains `<Name>OPT_DEBUG</Name>` and `APP_DEBUG` env entries — the flag
mechanism is stock. When the debug flag is set persistently, SSH is enabled
at every boot — exactly what the bavarian FDAC routine does, but permanent.

- Forum-documented on NBT Evo: via UART console —
  `sysetshellevo` → `setk SYS_DEBUG=true` → `store` → reboot → SSH open
  permanently. (car1337 "Open SSH for BMW NBT, NBT EVO unit" thread)
- UART = 115200 8N1 on marked TX/RX/GND pads inside the HU (bench work,
  one-time, no crypto at all).
- KEY QUESTION for next car session: is the syset keystore writable from a
  normal root shell? If `setk`-equivalent persists via a config file/partition
  we can edit over SSH, ONE bavarian unlock → flip flag → permanent SSH
  forever, no USB stick, no repeat unlocks. Recon: find the keystore
  (`ls /var/opt/sys* /var/syset* /fs/sda0/opt/sys/*syset*`, grep starter.cfg
  context, look for `syset`/`setk`/`getk` binaries on the HU).

## Path B — CA modulus overwrite (vandelay method — permanent + own signing)

Public repo: github.com/vandelayautollc/nbt-evo-reversing (NBT Evo / iDrive 6).
The scene's actual technique — you can't get BMW's private key, so REPLACE
the public key on the device:

- HU verifies signatures against an embedded CA modulus (RSA-1024, e=3,
  128B little-endian) inside a binary on /fs/sda0 (their example:
  /fs/sda0/opt/car/bin/NBTCarHU @ offset 0x35A300 — OUR offset will differ,
  must locate in our firmware).
- Generate own CA: `openssl genrsa -3 -out nbt-ca.pem 1024`, dump modulus LE.
- Overwrite via `dd` onto the mounted-rw binary → HU then accepts signatures
  from OUR CA → we can sign FD2F responses ourselves → our own fully-offline
  "pretzel server" equivalent forever. Bonus: also signs FSC codes etc.
- Getting initial write access (chicken-and-egg): one bavarian unlock OR
  their nbt_optdebug_flash.py (writes OPT_DEBUG enable to the primary
  partition — flash client chooses target; signature validation only happens
  on secondary→primary promotion, so primary writes are unchecked).
  Brick risk noted; recoverable via boot mode.

## Path C — UART serial console (always works, needs opening the HU)

sysetshellevo over UART → SYS_DEBUG=true → persistent SSH. Per-unit bench
work but needs zero crypto/servers — the guaranteed fallback for FUTURE cars.
Pad locations documented in the car1337 thread (photo attached there).

## Path D — commercial/scene tools (per-VIN, third-party)

- Feature Installer + purchased activation code (FlashXCode etc.) —
  reseller-held signing, same dependency class as bavarian but paid/stable-ish.
- HuTool USB method — pre-iStep-18 firmware only (generating .bin for USB
  boot-time SSH enable).
- "NBT SSH & Storage Workspace" (binunlock) — ENET-based open/close tool.

## Path E — leaked BMW key material (theoretical, not recommended to chase)

FSC-generation tools (BimmerUtility etc.) prove real BMW private keys have
leaked, and E-Sys/ISTA ship certstores. Whether any chain to the HU's
debug-gate trust root can only be answered by extracting the verifier —
Path B renders this unnecessary anyway.

## Decision (current plan)

1. This car: SSH-independence recon (checklist item 1) — find the syset
   keystore + dropbear details. If the debug flag is writable from shell:
   flip it once → permanent SSH. Otherwise: USB-stick armed key-only dropbear.
2. Optional later hardening: Path B CA overwrite → own local signing.
3. Future cars: UART SYS_DEBUG (Path C) as the universal fallback;
   CA overwrite once inside.

## Update — verifier binary + cert structure (offline research)

- **Verifier binary identified**: `NbtDiagHuHighApp` at `/opt/sys/bin/` is the
  HSFZ/UDS diag server (port 6801) per KeenLab's Black Hat BMW paper —
  almost certainly hosts the FD2E/FD2F/FDAC routine handler. PULL IT next
  car session → extract the trusted cert/CA it verifies against.
- **BMW HU PKI structure**: Root → SigS → FscS cert chain embedded in
  `NBTCarHUTwo` at `/opt/car/bin` — certs located by binwalk/offsets (scene
  threads document `dd` extraction + full openssl recipes to regenerate an
  equivalent self-signed chain: Root RSA-1024 e=3, SigS 512 e=7, FscS 1024
  e=3, MD5 self-signed, matching DNs and validity dates).
- **Unknown (needs the binary)**: whether FD2F verifies against the SAME
  Root/SigS/FscS chain or a separate debug-gate CA. If same → one cert
  overwrite grants BOTH self-signed FSC codes AND self-signed SSH unlocks.
- **Leaked keys: dead end** — no public BMW private key exists; the scene
  universally does cert replacement. Newer cars (SecureCode 2.0) use a
  dealer/vendor signing service — keys stay with BMW, signatures are rented.
- KeenLab paper also documents a diag-protocol root exploit on the
  HU-Intel/NBT generation — worth checking our unit for the same class.

## Notes

- NBT Evo = two shell IPs: 169.254.199.99 (jacinto) / .119 (omap) — we use
  the omap side (our SSH target).
- starter.cfg OPT_DEBUG/APP_DEBUG strings suggest the starter spawns
  different args/env when the flag is set — worth diffing starter.cfg
  sections around those entries.
