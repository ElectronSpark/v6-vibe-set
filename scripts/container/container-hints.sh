#!/usr/bin/env bash

if [[ -n "${XV6_HINTS_SHOWN:-}" ]]; then
    return 0 2>/dev/null || exit 0
fi
export XV6_HINTS_SHOWN=1

cat <<'HINTS'

Host one-liner:
  scripts/container/enter-container.sh xv6-build   # start container and build everything

Build commands:
  xv6-build           kernel + userland + ports + fs.img  (default jobs: nproc)
  xv6-kernel-x86      x86_64 kernel only
  xv6-user-ports      user programs and all ports
  xv6-images          fs.img, initrd.cpio.gz, boot.img
  xv6-rootfs-refresh  refresh fs.img from existing staged sysroot
  xv6-hyperv-image    Hyper-V Gen2 bootable VHDX

Launch commands:
  xv6-launch-nokvm    build kernel/rootfs then boot QEMU (KVM disabled)
  xv6-check-gui-accel check KVM / DRI / udmabuf passthrough

Environment:
  XV6_PARALLEL_JOBS   parallelism (default: nproc)
  DISPLAY_MODE        nographic | gtk | sdl
  XV6_WEBKIT_REF_SYSROOT  host-glibc WebKitGTK runtime sysroot

  xv6-help  — full usage and GUI acceleration notes

HINTS
