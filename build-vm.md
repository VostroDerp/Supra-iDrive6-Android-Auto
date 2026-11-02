# Build VM (`qnx-build`)

Isolated Debian 12 build host for the QNX toolchain. Nothing QNX-related is
installed on the laptop itself.

- Libvirt: `qemu:///system`, VM name `qnx-build`, autostart enabled
- Specs: 4 vCPU, 8 GB RAM, 80 GB qcow2 (overlay on the stock cloud image)
- Network: `default` NAT, currently `192.168.122.96` (check with
  `virsh -c qemu:///system domifaddr qnx-build`)
- Access: `ssh dev@<ip>` (key-only; the laptop's `~/.ssh/id_ed25519` works)
- Inside: Docker 20.10, git, make, python3, jq; repo at
  `~/Supra-iDrive6-Android-Auto` (tar-synced copy — rsync/git push to refresh)

## Reprovision / rebuild

Base image: `~/vms/debian-12-genericcloud-amd64.qcow2` (Debian 12 genericcloud).
Overlay: `~/vms/qnx-build.qcow2` (`qemu-img create -f qcow2 -b <base> -F qcow2 ... 80G`).

Provisioning is done **offline with `virt-customize`** via the `guestfs` docker
image (`Dockerfile`: fedora:41 + libguestfs-tools + libguestfs-appliance +
guestfs-tools + kernel-core). Do NOT use `virt-install --cloud-init` — it
injects `ds=nocloud` through SMBIOS, which hijacks the NoCloud datasource into
DMI-only mode and silently ignores the `cidata` seed ISO (no users, no keys).

```
docker run --rm --privileged -v ~/vms:/vms -v ~/.ssh:/ssh:ro guestfs bash -c '
virt-customize -a /vms/qnx-build.qcow2 \
  --run-command "useradd -m -s /bin/bash dev" \
  --ssh-inject dev:file:/ssh/id_ed25519.pub \
  --run-command "echo \"dev ALL=(ALL) NOPASSWD:ALL\" > /etc/sudoers.d/dev && chmod 440 /etc/sudoers.d/dev" \
  --run-command "touch /etc/cloud/cloud-init.disabled" \
  --install docker.io,git,make,python3-pip,unzip,curl,jq \
  --run-command "usermod -aG docker dev" \
  --run-command "systemctl enable docker"'
```

Note: `~/vms` lives under `$HOME` — the home dir needs `o+x` for qemu to
traverse (already set: `chmod 711 ~`).

## SOLVED: QNX SDP 6.5.0 toolchain (2026-11-02)

No myQNX needed after all. Working toolchain = licensed prebuilt image:

- **Image**: `docker pull andreluizpd/qnx-qcc-docker:6.5.0` (683MB, i386 —
  run with `--platform linux/386`). Packages licensed SDP 6.5 qcc host tools
  + armv7le target + `/etc/qnx/license/licenses` (key baked in).
- **Build** (on laptop directly — no VM needed):
  ```
  docker run --rm --platform linux/386 -v <milestone-dir>:/src -w /src \
    andreluizpd/qnx-qcc-docker:6.5.0 make
  ```
  Verified: full milestone-7 build incl. usb-pr42 branch → nbt-aad +
  libNbtAad.so + test bins all produced. One quirk: parallel `make` dropped
  src/dsi_client.o once — `make nbt-aad` again finished cleanly.
- **Local install also exists** at `~/qnx650` (2GB, silent-installed from
  archive.org `qnxsdp-65.7z` → DVD ISO → `.bin` installer with
  `-silent -options` response file in `~/vms/qnx/install.opts`). qcc needs
  `/etc/qnx/license/licenses` (root path, no sudo) so prefer the image;
  extracted license cached at `~/vms/qnx/license/licenses` for later.
- armv7 `libusbdi.so.2` copied to `tools/ssh-vetting/aa-install/` —
  deploy alongside usb builds if HU lacks it.
- myQNX account still pending — no longer blocking anything.
- VM note: `qnx-build` has NO outbound internet (host NAT masquerade lost;
  net restart didn't restore it — needs sudo on host). Files go in via
  scp/tar-ssh from the laptop. Not needed for builds anymore anyway.
