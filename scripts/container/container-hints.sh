#!/usr/bin/env bash

if [[ -n "${XV6_HINTS_SHOWN:-}" ]]; then
    return 0 2>/dev/null || exit 0
fi
export XV6_HINTS_SHOWN=1

cat <<'HINTS'

Host one-liner:
  scripts/container/reproduce-workspace.sh         # rebuild container + clean complete image

Build commands:
  xv6-build           clean kernel + KDE image + immutable receipt (default jobs: 2)
  xv6-build-incremental  explicitly reuse the current build tree
  xv6-kernel-x86      x86_64 kernel only
  xv6-user-ports      user programs and all ports
  xv6-images          fs.img, initrd.cpio.gz, boot.img
  xv6-rootfs-refresh  refresh fs.img from existing staged sysroot
  xv6-hyperv-image    Hyper-V Gen2 bootable VHDX

Launch on the host so external VMs remain visible to the exact /proc gate:
  scripts/launch/launch-gui.sh

Environment:
  XV6_PARALLEL_JOBS   parallelism (default: nproc)
  DISPLAY_MODE        nographic | gtk | sdl
  XV6_WEBKIT_REF_SYSROOT  host-glibc WebKitGTK runtime sysroot

  xv6-help  — full usage and GUI acceleration notes

HINTS
