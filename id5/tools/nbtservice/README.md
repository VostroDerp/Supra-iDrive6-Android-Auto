# NbtService: SSH independent of the Android Auto app

Status: **host-tested only, never loaded on the car.** IMPROVEMENT-PLAN A1 (option a) + A5.

A second, tiny iDrive PluginApp. It does two things and nothing else:

1. **Starts knockd at boot** (A1). HmiMain3D loads it on its own, so SSH no longer depends on
   the Android Auto app, on `/var/opt/aa/autostart`, on `nbt-aad.sh`, or on the AA app's boot
   guard latching autostart off after three crash-boots.
2. **Offers "SSH (15 min)"** in its own iDrive page (A5). Being in the car is the
   authorization: the checkbox opens knockd's window over the local control port with no
   knock. sshd stays key-only either way. A status row below it shows what knockd actually
   reports — closed, open with roughly how long is left, or gate not running.

```
make test     host test: builds the plugin, the fake framework and a real knockd, drives all three
make db       regenerate the text and image DBs
make car      cross-build libNbtService.so for the HU
```

## Files

| | |
|---|---|
| `nbt_service_app.cpp` | the plugin |
| `NbtService.manifest` / `NbtService.xml` | what HmiMain3D reads: the entry row, the page, the models |
| `make_db.py` | `NbtService_ALL_Texts.zip` + `_ALL_Images.zip` (the padlock icon is drawn in code, not committed as a binary) |
| `nbtservice_fake_appframework.{hpp,cpp}` | host stand-in for `libAppFramework.A.so`, with the recording the test asserts on |
| `nbtservice_host_test.cpp` + `test_nbtservice.py` | the host test |

## Install (its own car step, never with a USB/AA test)

Under `/var`, so nothing of ours is on the image a firmware update would rewrite (F4):

```
make car db
car.sh sh 'mkdir -p /var/opt/hmi/data/ID5/Plugins/NbtService'
car.sh push libNbtService.so          /var/opt/hmi/data/ID5/Plugins/NbtService/NbtService
car.sh push NbtService.manifest       /var/opt/hmi/data/ID5/Plugins/NbtService/NbtService.manifest
car.sh push NbtService.xml            /var/opt/hmi/data/ID5/Plugins/NbtService/NbtService.xml
car.sh push NbtService_ALL_Texts.zip  /var/opt/hmi/data/ID5/Plugins/NbtService/NbtService_ALL_Texts.zip
car.sh push NbtService_ALL_Images.zip /var/opt/hmi/data/ID5/Plugins/NbtService/NbtService_ALL_Images.zip
```

Check `/var/opt/hmi/data/ID5/Plugins` really is a plugin directory on this car first (the
upstream readme names it; the ID5 recon has not confirmed it). If it is not, the read-only
`/opt/hmi/ID5/data/ro/Plugins/` next to NbtAad works but gives up the easy recovery.

**Before the first reboot with it installed**, have the way back ready: the plugin is loaded
by HmiMain3D at boot, and a plugin that crashes it takes the whole HMI down. The way back is
the stock FDAC unlock plus dropbear on :22, then `rm -r` of the directory above. `touch
/var/opt/aa/service.disable` also makes `create()` return NULL without doing anything, which
is the softer switch once SSH works.

Keep the knockd line in `nbt-aad.sh` until a cold boot has shown the plugin arming the gate.
Both starting knockd is harmless: the second one cannot bind the port and exits.

## What is not verified

- Whether ID5 accepts the entry row where `NbtService.xml` puts it (`insertStateID="20"`,
  Media/Radio). That attribute is the one thing to change after the menu recon; A5 prefers
  Settings, then My Vehicle, then here.
- Whether `applicationWeight="201"` really puts the row after the Android Auto row.
- Whether a `<button>` with `property 2 = false` renders as a plain status line.
- Whether `/var/opt/hmi/data/ID5/Plugins` is scanned on this car.
- Everything about the padlock icon's appearance at the real screen size.

## Design notes

- Nothing happens in `create()`; `onInit()` only registers handlers and posts two runnables.
  knockd is started from the first runnable 2 s later, retried up to 5 times 30 s apart, and
  stopped as soon as the control port answers.
- `chmod -x /var/opt/aa/knockd` still disarms the gate, as with the old boot wrapper: the
  plugin treats "not executable" as a deliberate state and does not retry.
- The checkbox never shows what we asked for, only what knockd reports. A single missed
  status probe does not flip the row to "gate not running": knockd is single-threaded and can
  miss one while it spawns sshd.
- The plugin reads `CTL_PORT` and `OPEN_S` out of `/var/opt/aa/knock.conf`, so the port and
  the window length have one source of truth.
- It does **not** stop knockd in `onFinalize`. The gate must outlive the HMI.
- The text of a row can only come from the text DB by id, so the remaining time is four
  bucket texts (15 / 10 / 5 / under a minute) rather than a formatted string.
