# Wi-Fi exploration: the car's own AP without CarPlay (retired)

**Dropped 2026-11-04:** the car's Wi-Fi Direct group (`wfd0`) is up at boot without CarPlay, and upstream's
`NBT_ALT_WIFI=DIRECT` uses it, so the hotspot route below is no longer pursued. Kept for reference.

Goal 3: Android Auto over the head unit's own Wi-Fi (`uap0`, 172.16.224.1). This car has no CarPlay FSC, so
nothing turns the AP on. Earlier sessions got it beaconing a few times but never kept it up, and never
captured why it stopped.

## What changed since those attempts
They ran over the BM3 dongle, which dropped laptop->HU UDP and only let TCP 22/23/80/8000 through. That is why
mwud (the Wi-Fi control daemon, UDP 127.0.0.1:9096) needed a patched copy and then gdb/pdebug injection. With
the ENET cable, our SSH and the QNX toolchain, **`mwu-send` runs on the HU itself** and talks to the stock mwud
directly. The `legacy/` injection tools are not needed.

Treat the old findings (`git show 934156c:id5/findings.md`) §8–12 as leads, not facts: the decoded wire format (§10) and mwud's command set are
reliable; the conclusions about why the AP died are guesses.

## Tools
| Tool | Where | What |
|---|---|---|
| `mwu-send` | HU, `/var/opt/aa/mwu-send` | one mwud command, prints the reply (`make car`; host test `make test`) |
| `beacon-watch.sh [PATTERN] [INTERVAL]` | laptop | is the AP on air? logs every scan, prints SEEN/GONE. In nmcli's terse output BSSIDs have escaped colons (`AA\:BB…`), so match SSIDs |
| `hu-watch.sh [INTERVAL]` | laptop | timeline of Wi-Fi processes (pid = respawn visible) and uap0/wfd0 state, changes only |

Logs go to `id5/local/wifi/`. Every manual mwud run logs to a **new** file (`mwud-<time>.log`): an old
`mwud_run.log` once looked like a fresh success (findings §9, "stale log trap").

## Method (one variable per attempt)
1. Start `beacon-watch.sh 'NBTAA|DIRECT-'` and `hu-watch.sh` in two terminals. Keep them running throughout.
2. **Observe stock** for 2 min: is mwud running at all? scp? Is uap0 up, and does any beacon appear?
3. **Zero-code paths first** (USER at the screen): the hidden service menu (hold MENU ~10 s) for WLAN/hotspot
   entries, and any Wi-Fi/hotspot menu in Settings. Photos.
4. **mwud direct.** If mwud isn't running, start the stock one, logging to a new file:
   `car.sh sh 'cd /opt/conn/bin && ./mwud -c /opt/conn/bin/mwu_dev.conf -d /opt/conn/bin/wifidirect_defaults.conf </dev/null >/var/opt/aa/mwud-$(date +%H%M%S).log 2>&1 &'`
   Then `mwu-send cmd=ap_init iface=uap0 ssid=NBTAA-TEST key=<8+ chars> auth=0020 encrypt=0008` and
   `mwu-send cmd=ap_start iface=uap0`. Note each reply.
5. **Watch for at least 5 min.** For each GONE in beacon-watch, read hu-watch at the same timestamp: what
   appeared, disappeared or got a new pid? That event is the lead for the next attempt.
6. Next attempt = change exactly the thing the timeline points at. For example: mwud replaced by scp's own ->
   `slay scp` first; an ifconfig change -> check who did it; nothing changed but GONE -> radio or channel.
   One change, then watch again.

Stop and restore if iDrive misbehaves. Slain stock services (scp, Connectivity) respawn on their own in
~30 s (findings §8). A reboot resets all of this.
