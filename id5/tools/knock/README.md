# knockd: our own SSH gate

Status: v1 deployed and proven on cold boots since 2026-11-02. **v2 (this directory) is
host-tested only — not on the car yet.** It implements IMPROVEMENT-PLAN §A2, §A3, §A8 and
all of §C.

**Daily access: `car.sh`** (`status`, `knock`, `sh '<cmd>'`, `push <local> <remote>`,
`pull <remote> [local]`, `login`). It knocks when sshd :3804 is closed, then uses
`plink -i ~/.ssh/nbt_knock.ppk -P 3804 root@169.254.199.119`; push/pull check sizes
with `ls -l` on the HU. `legacy/hu.sh` (BM3 network, :22 password, bavarian unlock) is only
the fallback if knockd is down. The recon scripts use car.sh.

Alternative to the online unlock: a small ARM daemon (`knockd.c`, statically linked) listens
on :851. Laptop client `knock.py` answers a fresh 32-byte challenge with an RSA-2048
PKCS#1v1.5/SHA256 signature; a valid signature opens sshd for a while. Fully additive — the
stock FDAC unlock and the stock dropbear on :22 are untouched and remain the fallback.

## What v2 changes

| | v1 (on the car) | v2 (here) |
|---|---|---|
| Who may knock | anyone who can reach :851 — including the BM3 dongle's bridged Wi-Fi | `ALLOW_NETS` (ENET link-local + the car AP). knockd still binds 0.0.0.0, because addresses appear late at boot, and checks the **local** address of each accepted socket |
| sshd exposure | `ListenAddress 0.0.0.0` until reboot | knockd writes `sshd_config.active` with one `ListenAddress` per allowed local address and runs sshd on that |
| How long | until the HU reboots | `OPEN_S` (900 s), then SIGTERM to the listener. Established sessions are separate processes and keep running |
| Slow client | `SO_RCVTIMEO` is per `recv()`, so 1 byte every 5 s held the only accept slot for ~20 min | one overall `CONN_S` deadline per connection |
| Bad knocks | one unbounded log line each (a scanner could fill `/var`) | capped log (`LOG_MAX`, truncated with a marker) + one summary line per minute |
| No `/dev/urandom` | fell back to `srand(time ^ pid ^ &out)` | refuses to run / refuses the connection: no randomness, no gate |
| sshd spawn | `system()` with `SA_NOCLDWAIT`, so `rc` was meaningless and every knock ran sshd again | forked child, tracked by pid, `-D` added so the listener really is ours; a second knock extends the window instead |
| Signature covers | `SHA256(nonce)` | `SHA256("nbt-knock-v2" ‖ HU_ID ‖ nonce)`; v1 still accepted while `ACCEPT_V1=1` |
| Opening without a knock | — | UDP 127.0.0.1 `CTL_PORT`: `open [secs]` / `close` / `status`, for the iDrive "SSH (15 min)" toggle |

Replies: `K` window open, `N` rejected, `E` signature good but sshd did not start.

## Build and test

```
make test           # host test: 42 checks, builds knockd with gcc and drives the real daemon
make car            # ARMv7LE static binary through the QNX docker image
```

`test_knock.py` needs nothing but python3, gcc, openssl and pgrep. It uses a throwaway key
and a stub sshd in a temp directory; it never touches the car.

## Live layout on the HU (`/var/opt/aa/`)

`knockd`, `knock.conf`, `knock.pub` (hex modulus), `sshd_config` (the template: Port 3804,
key-only, `AuthorizedKeysFile=/var/opt/aa/authorized_keys`), `sshd_config.active` (written by
knockd), `authorized_keys`, `knock.log`. Boot launch via the `start-aa.sh` guarded hook
(+x arms, bounded 5x retry) until the NbtService plugin (§A1) takes over.

Why ports 851/3804: the stock `pf` anchor `filter_OBD` already passes them on dwc0 — zero
firewall changes. Neither port is in uap0's pass list, so reaching knockd over the car's own
AP needs a pf anchor (`pf.nbtaad.sample`, loaded by `PFCMD`; off by default, not yet verified
on the car).

## Deploying v2 over v1

One car step of its own, never together with a USB/AA test, and verified on a cold boot
before v1 is removed:

```
make car
car.sh push knockd        /var/opt/aa/knockd.v2
car.sh push ../hu-install/knock.conf    /var/opt/aa/knock.conf.v2
car.sh push ../hu-install/sshd_config   /var/opt/aa/sshd_config.v2
# knock by hand with the v2 pair of files first (keeps the running v1 gate as the way back):
car.sh sh '/var/opt/aa/knockd.v2 /var/opt/aa/knock.conf.v2'
```

For that side-by-side test `knock.conf.v2` needs its own ports, so the running v1 gate keeps
:851 and its sshd keeps :3804: set `PORT` to a spare port, `CTL_PORT` to another, and `Port`
in `sshd_config.v2` to a third (pf passes only 851/3804 on dwc0, so knock and ssh on the test
ports need `pfctl` or an ENET session that is already open). Only once a v2 knock has opened
that sshd, and the window has been seen closing on its own, swap the real files and reboot.

The one way v2 can refuse where v1 would not: no address inside `ALLOW_NETS` is up, so there
is nothing safe for sshd to bind. On this car the ENET link-local address (169.254.x) exists
from boot, so that case means the cable or the dongle is gone — and then SSH would not have
worked anyway. The FDAC unlock plus the stock dropbear on :22 stays the way back regardless.

Correction 2026-11-03: nothing runs start-aa.sh at boot, and knock.log shows only manual starts, so the
"cold boot" auto-arm below was not what happened. Boot start: the USB start-up script (NEXT-SESSION step 1) and NbtService (step 6).
Proven on the car (v1): knockd started -> knock.py -> 'K' ->
sshd:3804 -> key-only root login on hu-omap. Garbage/wrong-key signatures rejected + logged.
Gotcha found+fixed: bare TCP probes (`nc -z`) killed the daemon via SIGPIPE on send ->
`signal(SIGPIPE, SIG_IGN)`.

Key custody: private key = `~/.ssh/nbt_knock.pem` (laptop) + `~/.ssh/nbt_knock.ppk`
(plink login) + copy in the password manager. The HU holds ONLY public material.
Lost key -> the bavarian unlock still works -> rotate the pair.
`HU_ID` is a plain label, never VIN-derived, and not a secret: the daemon sends it in the
challenge so the two sides cannot drift apart.
