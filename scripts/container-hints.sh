#!/usr/bin/env bash

if [[ -n "${XV6_HINTS_SHOWN:-}" ]]; then
    return 0 2>/dev/null || exit 0
fi
export XV6_HINTS_SHOWN=1

cat <<'HINTS'

xv6 container commands:
  xv6-toolchain     compile the cross toolchain
  xv6-kernel-x86    compile the x86_64 kernel
  xv6-user-ports    compile user programs and all ports
  xv6-images        build fs.img, initrd.cpio.gz, and boot.img
  xv6-qemu-nokvm    boot x86_64 in QEMU with USE_KVM=0

Useful environment:
  XV6_SOURCE_DIR=/src/xv6-os
  XV6_BUILD_DIR=/src/xv6-os/build-x86_64
  XV6_PARALLEL_JOBS=2
  XV6_PREBUILT_TOOLCHAIN_PREFIX=/opt/xv6-prebuilt-toolchain
  DISPLAY_MODE=nographic|gtk|sdl

Host shortcut:
  scripts/enter-container.sh

HINTS