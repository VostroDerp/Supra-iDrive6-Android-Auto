# ID5 tools

Laptop-side tools for the BMW NBT Evo ID5 work. None of this is upstream material.

| Folder | What | Use |
|---|---|---|
| `car/` | `car.sh`: knock, shell, push/pull, `collect`/`stages`/`run` for car tests, `fallback` (the stock unlock + :22 over ENET). `start-aa.sh`: manual AA bring-up on the HU (mm-renderer + daemon) | Daily |
| `knock/` | knockd (our SSH gate, runs on the HU) + `knock.py` client + `genkey.sh` | Daily, via car.sh |
| `nbtservice/` | `libNbtService.so`: the iDrive plugin that starts knockd at boot and holds the "SSH (15 min)" toggle (A1 + A5). Host-tested, not on the car yet | Install once |
| `hu-install/` | What is installed on the HU: the USB start-up script `nbt-aad-usb.sh` (becomes `/opt/aa/nbt-aad`), env, sshd_config, knock.conf, plugin XML | Reference + redeploy |
| `recon/` | Read-only HU state dumps (`recon-full.sh`, `recon-knock.sh` via car.sh; `*-recon.sh`, `probe-ap.sh` run on the HU) | As needed |
| `desk-rig/` | Linux test rig: our AA session code against a real phone over USB, at the desk | Before car tests |
| `git-hooks/` | pre-push guard (paths, sizes, identifiers, clean `main`) + `install.sh` | Once per clone |
| `legacy/` | Retired paths: the bavarian unlock client + its vetting write-up, `hu.sh` (BM3/:22 password path), `nbt-aad-bm3.sh` (the Wi-Fi dongle wrapper), `wifi/` (the car's own hotspot, dropped for Wi-Fi Direct), MITM/relay and gdb/pdebug experiments | Fallback / history only |

Outputs go to `id5/local/` (gitignored), never into the tree.
