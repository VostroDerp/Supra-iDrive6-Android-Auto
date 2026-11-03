# ID5 work (fork branch `id5-dev`)

Everything this fork adds on top of upstream lives in this folder. The rest of the tree is upstream as-is, so
rebasing on upstream never conflicts and nothing here can leak into an upstream PR by accident.

Car: BMW NBT Evo **ID5** head unit, **no CarPlay FSC**.

| File | What |
|---|---|
| `NEXT-SESSION.md` | The car-test runbook: ordered checklist, per-run protocol, decision tree, run log |
| `IMPROVEMENT-PLAN.md` | Approved work, proposals and the follow-up list |
| `findings.md` | Deep findings so far (USB §13, Cinemo §14, ...) |
| `adapter-method.md` | The working wireless setup through a BM3/OBD Wi-Fi dongle |
| `build-vm.md` | The QNX build VM |
| `tools/` | Laptop tools, see `tools/README.md` |
| `archive/` | Earlier notes kept for reference (previous agent's memory file, SSH-independence research) |
| `local/` | **Gitignored.** Recon output, files pulled from the HU, captures, unlock logs, deploy binaries, personal notes |

## Rules
- Fork `main` is a clean mirror of `upstream/main`. The pre-push guard enforces it.
- Upstream contributions are made on topic branches cut from `upstream/main`, never from `id5-dev`.
- Never commit personal identifiers (MACs, BMW Bluetooth name / VIN digits, network names). The pre-push guard checks
  generic patterns plus the local denylist `.git/info/push-denylist`.
- Head-unit test binaries are deployed under new names in `/var/opt/aa/`. The working release daemon is never
  overwritten. The stock SSH unlock path is never touched.

## Setup on a new clone
```
id5/tools/git-hooks/install.sh      # pre-push guard (+ empty local denylist)
```
