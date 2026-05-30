# Scripts

Source-controlled helpers for building, launching, debugging, and validating
xv6-os, grouped by purpose. Generated sysroots, downloaded packages, rootfs
images, QEMU screenshots, and other build outputs stay outside this directory
and out of git.

```
scripts/
├── container/   Docker development container helpers
├── build/       host-glibc build prep and submodule bootstrap
├── image/       rootfs / initrd / boot / Hyper-V image builders
├── launch/      QEMU launchers
├── gpu/         GPU, Hyper-V, and WebKit runtime validation
├── net/         TCP stress and tap networking helpers
├── debug/       GDB attach helpers
└── audit/       Linux ABI audit generators
```

## container/

- `enter-container.sh` - create or reuse a local Docker development container;
  forwards `/dev/kvm`, `/dev/dri`, `/dev/udmabuf`, and host display sockets when
  they exist. Pass a command (e.g. `xv6-build`) to run it one-shot.
- `container-xv6-command.sh` - command dispatcher copied into the image as
  `xv6-build`, `xv6-help`, and friends.
- `container-hints.sh` - interactive shell hints sourced by the image.
- `check-gui-accel.sh` - installed as `xv6-check-gui-accel`; checks KVM, DRI,
  and udmabuf passthrough for GUI video.
- `docker-build-webkit.sh` - build inside Docker with an explicit local
  WebKitGTK runtime sysroot.

## build/

- `build-linux-host-libs.sh` - stage host glibc runtime support libraries.
- `build-linux-host-probes.sh` - build small host-glibc ABI probe programs.
- `patch_vim_exit.sh` - source patch helper used by the Vim port build.
- `setup-submodules.sh` - sync and initialize the nested `kernel`, `user`, and
  `ports` submodules.

## image/

- `make-rootfs.sh` - generate the ext4 root filesystem image from the sysroot.
- `make-hyperv-image.sh` - generate a Hyper-V Gen2 bootable VHDX from `xv6.bin`
  and `fs.img`.
- `hyperv-efiloader.c` - EFI loader source compiled by `make-hyperv-image.sh`.
- `make-initrd.sh` - legacy initrd builder retained for the CMake `initrd`
  target.
- `make-image.sh` - legacy boot-image builder retained for the CMake `image`
  target.

## launch/

- `launch-gui.sh` - boot the default x86_64 GUI image in QEMU.
- `run-qemu.sh` - lower-level QEMU launcher used by CMake and the GUI wrapper.

## gpu/

- `gpu-validate.sh` - run QEMU graphics substrate checks.
- `validate-webkit-runtime.sh` - check that a sysroot or rootfs image contains
  the WebKit runtime and media dependencies needed by MiniBrowser.
- `hyperv-gpu-core-validate.sh` - build, deploy, and validate the core Hyper-V
  GPU substrate.
- `hyperv-gpu-stress.sh` - repeated Hyper-V GPU stress runs.
- `hyperv-3d-fps-validate.sh` - finite Hyper-V 3D demo FPS gate.
- `hyperv-3d-visual-check.sh` - Hyper-V screenshot visual check.
- `hyperv-dxg-validate.sh` - rebuild, deploy, and validate the Hyper-V
  DXG/D3DKMT graphics lane.
- `hyperv-webkit-gpu-validate.sh` - repeated local WebKit GPU/API smoke without
  network pages.
- `webkit-virgl-gpu-validate.sh` - WebKit GPU validation over the virgl lane.
- `stage-gpup-umd.sh` - stage the Hyper-V/WSL GPU-PV D3D12 user-mode runtime.

## net/

- `run-tcpstress.sh` - drive the TCP stress validation against a booted guest.
- `setup-tap.sh` - bring up a host tap interface for guest networking.
- `tcpstress-host-server.py` - host-side TCP server used by `run-tcpstress.sh`.

## debug/

- `attach-gdb.sh` and `xv6.gdb` - attach GDB to a QEMU debug stub.

## audit/

- `linux_abi_audit.py` and `linux_abi_semantic_audit.py` - regenerate the Linux
  ABI audit documents under `docs/`.
