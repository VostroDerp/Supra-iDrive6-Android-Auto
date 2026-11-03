# knockd: our own SSH gate

Status: deployed and proven on cold boots since 2026-11-02. v2 work (allowlist, open window, review fixes) is in
`id5/IMPROVEMENT-PLAN.md` §A/§C.

**Daily access: `car.sh`** (`status`, `knock`, `sh '<cmd>'`, `push <local> <remote>`,
`pull <remote> [local]`, `login`). It knocks when sshd :3804 is closed, then uses
`plink -i ~/.ssh/nbt_knock.ppk -P 3804 root@169.254.199.119`; push/pull check sizes
with `ls -l` on the HU. `legacy/hu.sh` (BM3 network, :22 password, bavarian unlock) is only
the fallback if knockd is down. The recon scripts use car.sh.

Alternative to the online unlock: a tiny ARM daemon (`knockd.c`, ~240 lines,
statically linked) listens on dwc0:851. Laptop client `knock.py` answers a
fresh 32-byte nonce with an RSA-2048 PKCS#1v1.5/SHA256 signature; valid sig runs
SSHCMD. Fully additive — the stock FDAC unlock is untouched and remains the fallback.

Live layout on HU (/var/opt/aa/): knockd, knock.conf, knock.pub (hex modulus),
sshd_config (Port 3804, key-only, AuthorizedKeysFile=/var/opt/aa/authorized_keys),
authorized_keys. Boot launch via start-aa.sh guarded hook (+x arms, bounded 5x retry).

Why ports 851/3804: stock `pf` anchor `filter_OBD` already passes them on dwc0 —
ZERO firewall changes. Neither port is in uap0's pass list, so the car's own Wi-Fi can't reach
them. **Not ENET-only, though:** the BM3 dongle's Wi-Fi is bridged onto dwc0, so its clients can reach
:851 (and :3804 once knocked). The v2 allowlist fixes that (IMPROVEMENT-PLAN A2). sshd spawns per-knock, dies with reboot => SSH is never permanently on.

Proven: cold boot -> knockd auto-armed via start-aa.sh -> knock.py -> 'K' ->
sshd:3804 -> key-only root login on hu-omap. Garbage/wrong-key sigs rejected + logged.
Gotcha found+fixed: bare TCP probes (nc -z) killed the daemon via SIGPIPE on send
-> signal(SIGPIPE, SIG_IGN). (rc=-1 in knock.log after SSHCMD is cosmetic:
SA_NOCLDWAIT makes system()'s waitpid fail; sshd still spawns.)

Key custody: private key = ~/.ssh/nbt_knock.pem (laptop) + ~/.ssh/nbt_knock.ppk
(plink login) + copy in password manager. HU holds ONLY public material.
Lost key -> bavarian unlock still works -> rotate pair.
