# Desk rig

Runs NBT-AAD's own Android Auto session code against a real phone on the laptop's USB port, so phone-side
questions get answered at the desk instead of in the car.

The session code is compiled **unmodified** from a milestone-7 tree: `aa_session.cpp`, `aa_cryptor.cpp`,
`sdr_builder.cpp`, `crash_handler.cpp`, plus `usb_aoap.hpp` for the accessory identity and the framing.
`desk_rig.cpp` replaces only the QNX USB layer with libusb, and does what `usb_direct.cpp` does on the car:
1. AOA protocol;
2. the daemon's identity strings;
3. start;
4. claim the accessory interface;
5. one AA frame per bulk transfer, with a zero-length packet after a whole number of packets.

## What it answers
- Does the phone accept accessory mode with our identity (and protocol 1 / 2)?
- Version exchange, TLS handshake with our cipher list, service discovery accepted?
- Does the phone start video (H.264 saved to `build/out/video.h264`, play with `ffplay -f h264`)?
- Which audio channels open, and does the phone ask for the mic?

## What it cannot answer
Anything QNX or car side:
- io-usb, the driver-path claim, devc-serusb-aoap / NbtMediaMainApp stealing transfers;
- the screen, touch, knob and car audio.

That is what car runs are for.

## Build and run
```
make                                   # against the -usb worktree (default NBT_SRC)
make NBT_SRC=/path/to/milestone-7      # any other tree, e.g. a vanilla PR #42 checkout
make list                              # which phones / accessories libusb sees
make run                               # one session (60 s), logged to build/out/session.log, then the
                                       # same stage table as car.sh stages
make run ARGS="--protocol 2 --seconds 30"
NBT_SSL_CIPHERS=... make run           # NBT_* settings work as on the car
```
The build line prints which commit of `NBT_SRC` it used. Note that the -usb worktree is usually on `pr42-id5`, the
patched tree.

Exit code: 0 = video frames received, 3 = session ran but no video, 1 = setup failed, 2 = bad arguments.

## Phone and laptop setup
- Phone: unlocked, USB set to file transfer or USB debugging on (the rig looks for an MTP/PTP or adb
  interface). Android Auto installed. Use the same data-capable cable as in the car.
- If adb is running, it may hold the adb interface. That is fine: the rig claims the accessory interface, not adb.
- **Permissions.** After the switch the phone is `18d1:2d00`..`2d05`, and no udev rule on this laptop gives the
  logged-in user access to those IDs, so libusb reports `LIBUSB_ERROR_ACCESS`. One-time fix (needs sudo):
  ```
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", ATTR{idProduct}=="2d0[0-5]", TAG+="uaccess"' | \
    sudo tee /etc/udev/rules.d/51-android-accessory.rules && sudo udevadm control --reload
  ```
- After a run, the phone stays in accessory mode until it is unplugged. Replug before the next run (or the rig
  reuses the accessory: "already in accessory mode").

Status: built and started on the host (2026-11-03); **not yet run against a phone**.
