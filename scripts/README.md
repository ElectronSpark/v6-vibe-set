# Scripts

This directory contains source-controlled helpers for building, launching,
debugging, and validating xv6-os. Generated sysroots, downloaded packages,
rootfs images, QEMU screenshots, and other build outputs should stay outside
this directory and out of git.

## Daily Build And Launch

- `launch-gui.sh` - boot the default x86_64 GUI image in QEMU.
- `run-qemu.sh` - lower-level QEMU launcher used by CMake and GUI wrappers.
- `enter-container.sh` - create or reuse a local Docker development container.
- `docker-build-webkit.sh` - build inside Docker with an explicit local
  WebKitGTK runtime sysroot.

## Image And Build Internals

- `build-linux-host-libs.sh` - stage host glibc runtime support libraries.
- `build-linux-host-probes.sh` - build small host-glibc ABI probe programs.
- `make-rootfs.sh` - generate the ext4 root filesystem image from the sysroot.
- `make-hyperv-image.sh` - generate a Hyper-V Gen2 bootable VHDX from
  `xv6.bin` and `fs.img`.
- `make-initrd.sh` - legacy initrd builder retained for the CMake `initrd`
  target.
- `make-image.sh` - legacy boot-image builder retained for the CMake `image`
  target.
- `container-xv6-command.sh` - command dispatcher copied into the Docker image.
- `container-hints.sh` - interactive shell hints for the Docker image.

## Validation And Debugging

- `validate-webkit-runtime.sh` - check that a sysroot or rootfs image contains
  the WebKit runtime and media dependencies needed by MiniBrowser.
- `gpu-validate.sh` - run QEMU graphics substrate checks.
- `attach-gdb.sh` and `xv6.gdb` - attach GDB to a QEMU debug stub.
- `run-tcpstress.sh`, `setup-tap.sh`, and `tcpstress-host-server.py` - network
  validation helpers.
- `linux_abi_audit.py` and `linux_abi_semantic_audit.py` - regenerate Linux ABI
  audit documents.

## Maintenance

- `setup-submodules.sh` - sync and initialize the nested `kernel`, `user`, and
  `ports` submodules.
- `patch_vim_exit.sh` - source patch helper used by the Vim port build.
