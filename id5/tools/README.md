# ID5 tools

Laptop-side tools for the BMW NBT Evo ID5 work. None of this is upstream material.

| Folder | What | Use |
|---|---|---|
| `car/` | `car.sh`: knock, shell, push/pull, `collect`/`stages`/`run` for car tests. `start-aa.sh`: manual AA bring-up on the HU (mm-renderer + daemon) | Daily |
| `knock/` | knockd (our SSH gate, runs on the HU) + `knock.py` client + `genkey.sh` | Daily, via car.sh |
| `hu-install/` | What is deployed on the HU under `/var/opt/aa/` (wrapper, env, sshd_config, knock.conf, plugin XML) | Reference + redeploy |
| `recon/` | Read-only HU state dumps (`recon-full.sh`, `recon-knock.sh` via car.sh; `*-recon.sh`, `probe-ap.sh` run on the HU) | As needed |
| `desk-rig/` | Linux test rig: our AA session code against a real phone over USB, at the desk | Before car tests |
| `git-hooks/` | pre-push guard (paths, sizes, identifiers, clean `main`) + `install.sh` | Once per clone |
| `legacy/` | The bavarian unlock client + its vetting write-up, `hu.sh` (BM3/:22 password path), MITM/relay and gdb/pdebug experiments | Fallback / history only |

Outputs go to `id5/local/` (gitignored), never into the tree.
