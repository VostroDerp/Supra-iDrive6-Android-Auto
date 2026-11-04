# NBT-AAD on a BMW NBT Evo ID5 without CarPlay (fork branch `id5-dev`)

One owner's port of [NBT-AAD](../milestone-7/readme.md) to a BMW F36 with an NBT Evo head unit on **ID5**
software and **no CarPlay licence**. Everything this fork adds lives in this folder; the rest of the tree is
upstream as-is, so rebasing never conflicts and nothing here leaks into an upstream PR.

## Status (2026-11-04)

| | |
|---|---|
| Android Auto over USB | **Works** with upstream main: video, touch, controller, Bluetooth audio, Assistant, calls, reverse, replug. |
| Starts by itself after a reboot | Not yet. The USB start-up script is written (NEXT-SESSION step 1). |
| Our SSH access | Works when started by hand (port knock, key only). The hardened version is built, not car-tested (steps 4-5). |
| iDrive switched off and on with the phone plugged in | Needs a replug today. A fix is written, not car-tested (step 3). |
| Wireless Android Auto | Next to test: the car's Wi-Fi Direct group, through upstream's `NBT_ALT_WIFI` (step 6). |

## Files

| File | What |
|---|---|
| [NEXT-SESSION.md](NEXT-SESSION.md) | The next car session: steps in order, each with its check and its way back; the run log |
| [findings.md](findings.md) | What has been verified on this car: access, file systems, boot chain, network, USB, audio, Wi-Fi |
| [IMPROVEMENT-PLAN.md](IMPROVEMENT-PLAN.md) | Our SSH access design (knockd v2, NbtService) and the backlog that is ours alone |
| [tools/](tools/README.md) | Laptop and head-unit tools (`car.sh`, knockd, NbtService, recon, desk rig, pre-push guard) |
| `local/` | **Gitignored.** Logs, captures, files pulled from the head unit, private notes |

Older notes (the Wi-Fi dongle method, the hotspot and gdb experiments, the build VM, the first USB attempt) are in
the git history: `git show 934156c:id5/<file>`.

## Rules
- Fork `main` is a clean mirror of `upstream/main`. Upstream contributions are topic branches cut from
  `upstream/main`, never from `id5-dev`.
- One change per car test. Every step has a way back, tried before it is needed.
- Test daemons go to `/var/opt/aa/` under new names; what works is never overwritten. The stock SSH unlock path is
  never touched, and nothing is written to `/etc`.
- No car identifiers (VIN, Bluetooth or Wi-Fi names, keys, MAC addresses) are committed. The pre-push guard checks
  generic patterns plus the local denylist `.git/info/push-denylist`.

## Setup on a new clone
```
id5/tools/git-hooks/install.sh      # pre-push guard (+ empty local denylist)
```
