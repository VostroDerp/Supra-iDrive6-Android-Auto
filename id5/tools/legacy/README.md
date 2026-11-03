# bavarian-ssh-client vetting

Artifact under test: `bavarian-ssh-client-linux.zip` v0.1.0 from https://ssh.bavarian-aa.uk
SHA-256 `69786b148a2da473371ce6a6295878bde1aec52dc43745a51050cefbb0079075` (verified
against the published sum). Contents: single 8.8 MB x86-64 stripped ELF — Rust/iced GUI.

## Verdict: approved for real-car use (safe debug-gate class)

The client is a **dumb pipe**. It opens HSFZ (TCP 6801) to the car and WSS to
`wss://pretzel.bavarian-aa.uk:8443`, then forwards HSFZ frames both ways. All unlock
logic is server-side; the binary contains no payloads, spawns no subprocesses, writes
no files outside GUI shader caches, and generates zero network traffic until
"Enable SSH" is clicked.

Observed complete unlock (MITM against the real server, mock car):

```
S>C status "Opening the debug gate..."
S>C  car-bound: 31 01 FD2E FFFFFFFF        RoutineControl start, RID 0xFD2E (request challenge)
car> 71 01 FD2E <4B echo> <8B challenge>  12-byte positive response
S>C  car-bound: 31 01 FD2F <4B len> <~256B signature>
S>C status "Gate: open" / "Enabling SSH..."
S>C  car-bound: 31 01 FDAC                RoutineControl start, RID 0xFDAC (enable SSH)
car> 71 01 FDAC <4B status>
S>C status "SSH: enabled" ; relay close (0x83)
```

- Exactly three UDS `0x31` RoutineControl calls on diag address `0x63` (HU_NBT2):
  `0xFD2E` challenge, `0xFD2F` signed response, `0xFDAC` enable.
- **No flash/write-class services** — no 0x34/0x35/0x36/0x37, no 0x2E, no 0x10/0x27
  session or security-access escalation observed.
- The `0xFD2F` blob is a server-computed signature over the car's 8-byte challenge —
  that key is the whole secret and why the tool needs internet. If the relay dies,
  the tool dies.
- Consistent with the site's claim: "only opens SSH until reboot, nothing is persisted."

## Residual risk

The server could send different frames at runtime (we audited one flow). Mitigation:
run the MITM on the real-car attempt and watch every frame live; anything beyond the
three known RIDs on address 0x63 gets flagged before it matters. The car leg is
plaintext HSFZ so nothing can hide.

Minor: server sees challenge/response traffic; blank-IP auto-discovery likely sends
VIN/MAC/diag-addr in the type-0x01 targets frame.

## Wire formats

### Relay (WebSocket, client <-> server)

`u8 type | u32be payload_len | payload`

- `0x01` client->server: targets/metadata (zeros when IP entered manually)
- `0x02` client->server: job name, e.g. `ssh_enable`
- `0x03` client->server: forwarded car frame (raw HSFZ incl. header)
- `0x06` client->server: client version string (`0.1.0`)
- `0x81` server->client: HSFZ frame to send to the car
- `0x82` server->client: status text for the UI (`82 02` info, `82 03`/`82 04` other severities)
- `0x83` server->client: end (`83 01` ok)

### HSFZ endpoint on the car (TCP 6801), as the client parses it

`00 00 | u16be len | 00 pad | u8 ctrl | u8 src | u8 tgt | uds[len-3]`

`len` counts `ctrl+src+tgt+uds` only. The client consumes one lookahead byte past
each frame, so single frames need a trailing byte appended or they sit in the buffer
(the forwarded `0x03` payload includes that lookahead byte — harmless).

- `ctrl=0x01` data, `ctrl=0x02` transport ack.
- Gateway ack = the **request echoed verbatim** (same ctrl, same src/tgt `f4 63`).
- Data response = `ctrl=0x01`, addresses swapped (`63 f4`), positive UDS (`sid+0x40`).
- `0xFD2E` response must be 12 bytes total: `71 01 FD 2E` + 8 payload bytes
  (last 8 = the challenge the server signs).

## nbt-ssh-unlock.py — our own client

A zero-dependency (stdlib-only) reimplementation of the unlock client. Same wire
behaviour as the upstream binary, two deliberate differences: no GUI, and a
hardcoded **car-facing allowlist** — the only bytes it will ever send to the car
are `RoutineControl(start)` frames to `0x63` with RIDs `FD2E`/`FD2F`/`FDAC`.
Anything else from the server is logged as `REFUSED` and the run aborts before
the frame reaches the car.

```
nbt-ssh-unlock.py 169.254.199.99                          # real unlock (auto-saves session)
nbt-ssh-unlock.py 169.254.199.99 --probe                  # FD2E challenge x2, exit (no server)
nbt-ssh-unlock.py 169.254.199.99 --replay session.json    # offline re-unlock from saved session
nbt-ssh-unlock.py 169.254.199.99 --server ws://127.0.0.1:8443 --insecure
```

Successful unlocks save `{challenge, fd2e, fd2f, fdac}` to `nbt-unlock-*.json`.
`--replay` re-sends them with **no server contact** — it works only if the car
returns the same challenge (checked live; aborts safely before FD2F if not).
That is the offline-reuse experiment: `--probe` on the real car tells us in
advance whether challenges are static.

`hu-recon.sh` — read-only inspection for the unlocked HU: iStep/version files,
FSC/feature-code state (is CarPlay licensed?), WLAN AP state, CarPlay configs,
plugin framework layout, filesystems. Run via SSH and save the output:
`ssh root@169.254.199.99 -o HostKeyAlgorithms=+ssh-rsa -m hmac-sha1 'sh -s' < hu-recon.sh | tee recon.txt`
Run a second pass inside `on -f /net/hu-omap/ /bin/sh` for the omap namespace.

Tested offline against `mock_car.py` + `scripted_relay.py` (replays the recorded
server flow): full happy-path unlock completes; injected `0x36` TransferData,
`0x2E` WriteDataByIdentifier, and unknown-RID frames are all refused before
reaching the car. `--probe` answers whether challenges are per-request random
(run it on the real car — if identical, captured signatures would replay and the
server dependency disappears; expected result: they differ).

## Harness usage (real-car run)

On the laptop plugged into ENET/bm3, with the HU reachable at e.g. 169.254.199.99:

```
mitm_relay.py   # ws relay on :8443, proxies to the real wss://pretzel...:8443,
                # logs every frame both directions
bavarian-ssh-client --server ws://127.0.0.1:8443 --insecure
```

`mock_car.py` + `mock_relay.py` reproduce the fully offline test (fake HU on :6801,
fake relay that plays the recorded server script — or no relay at all to isolate the
client's own behaviour). `dyn-mitm.sh` is the Docker/Xvfb orchestrator used during
vetting; adjust paths if reused outside the `bavarian-sandbox` image.

