#!/usr/bin/env bash

if [[ -n "${XV6_HINTS_SHOWN:-}" ]]; then
    return 0 2>/dev/null || exit 0
fi
export XV6_HINTS_SHOWN=1

cat <<'HINTS'

xv6 container commands:
  xv6-build         build kernel, userland, ports, and fs.img
  xv6-kernel-x86    compile the x86_64 kernel
  xv6-user-ports    compile user programs and all ports
  xv6-images        build fs.img, initrd.cpio.gz, and boot.img
  xv6-launch-nokvm  boot x86_64 in QEMU with USE_KVM=0
  xv6-help          print full usage

Useful environment:
  XV6_SOURCE_DIR=/src/xv6-os
  XV6_BUILD_DIR=/src/xv6-os/build-x86_64
  XV6_PARALLEL_JOBS=2
  DISPLAY_MODE=nographic|gtk|sdl
  XV6_WEBKIT_REF_SYSROOT=/path/to/host-glibc-webkit-sysroot

Host shortcut:
  scripts/enter-container.sh

HINTS
