---
name: xv6-os-debugging
description: 'Use when: working in xv6-os on QEMU boot, KDE/Wayland GUI audits, virgl or Hyper-V GPU diagnostics, kernel symbols, Linux GUI ABI, rootfs images, port builds, or nested submodule workflows.'
argument-hint: 'Describe the xv6-os build, runtime, or port symptom'
---

# xv6-os Debugging

## Authority and scope

The authoritative repository skills live under `.github/skills`; `.codex/skills`
contains redirects. Use [the index](../INDEX.md) to select a narrow subsystem.
Read `docs/active-work-plan.md` for current progress and evidence. Historical
plans are under `docs/archive/plan-consolidation-20260907/`.

Match the user's task: a graphical audit calls for operating the existing VM
image and recording observations. A request to fix an ABI failure may require
a reducer and an implementation change. Documentation review alone needs no
build or VM. Report image provenance so an older image is not presented as
proof of the current dirty source tree.

## Host and VM safety

Root `AGENTS.md` is binding. Repeat its search, process-wait, serial-console,
`pgrep`, and QEMU-cleanup rules verbatim or by explicit reference in every
worker prompt. In particular:

- Use `/home/es/.local/bin/rg` for repository searches. Never bypass the guarded
  wrapper or recursively search binary/build/image/archive artifact trees.
  Recursive `-a`, `-u`, `--no-ignore*`, and `--binary` forms are forbidden.
  For named regular artifacts use `scripts/audit/safe-rg-artifact.sh PATTERN FILE...`;
  its repository containment, symlink-parent, type, extension, and 64 MiB
  checks must pass. Output limits do not bound resource use.
- Only one potentially large search may run across workers. Wait synchronously
  on every returned process/session/cell handle before another search or heavy
  command. Do not replace the real wait with an unowned monitor.
- Authorize at most one VM lane; mark other lanes no-boot. Before dispatch,
  require no active VM worker and run
  `scripts/launch/qemu-exact-inventory.sh --require-zero`, which scans exact
  `/proc/*/exe` identities. Never use `pgrep` for VM ownership or cleanup.
- Use an owned PID/process group with synchronous reap, bounded cleanup, and a
  final exact zero-QEMU check. Never leave QEMU running. Finish cleanup before
  authorizing the next VM lane; guest memory is not capped by this rule.
- Serial commands must be short, marker-delimited, and synchronously completed.
  Account for the bracketed-paste first-character drop. Serial silence is not
  completion; check VM state and obtain a fresh prompt after a timeout.
- Keep at most three completed generated build/image iterations. Before a
  fourth, remove only the oldest confirmed-unused iteration; preserve active
  disks, Docker data, deployed images, sources, and the newest good rollback.

## Current GUI launch and evidence

Use [GUI runtime](../xv6-debug-gui-runtime/SKILL.md) for interactive audits and
capture details; use [Linux GUI ABI](../xv6-linux-gui-abi/SKILL.md) for failures
that implicate Linux compatibility.

- `scripts/launch/launch-gui.sh` selects `XV6_RECEIPT`, then a complete latest
  reproduction receipt, then the configured `build-x86_64` fallback. Pass
  `KERNEL` and `FSIMG` explicitly when auditing particular artifacts. Keep
  `AUTO_BUILD=0` when auditing an existing image.
- The GUI wrapper defaults to SDL, `QEMU_GPU=virtio-vga-gl-primary`, 6 CPUs,
  8 GiB, and `QEMU_INPUT=virtio`. The lower-level launcher has different
  defaults. Inspect the resolved command and `display-contract` receipt;
  record any frontend, input, geometry, or module override.
- The wrapper delegates to `scripts/launch/run-owned-qemu.sh`, which protects
  the base image with a temporary qcow2 overlay by default and records process
  ownership. Wait for it to finish and verify its cleanup. Emergency cleanup
  uses `scripts/launch/cleanup-owned-qemu.sh PID START_TICKS RUN_TOKEN` with the
  captured identity, never a name-wide kill.
- `rootfs-overlay/etc/startup` selects KDE through `xv6-desktop-session` and
  `kde-session`. The current compositor is KWin. Legacy `wlcomp` traces and
  generated-source instructions apply only to an explicitly identified older
  image; use [the bridge skill](../xv6-wayland-kernel-bridge/SKILL.md).
- VM virgl enablement, compositor acceleration, browser acceleration, visible
  input response, media/audio output, and performance are separate claims.
  Require evidence for each requested layer from the same run.

## Build and image selection

For implementation work, inspect dirty state and build only the affected layer.
Read [build reproduction](../xv6-debug-build-repro/SKILL.md) for container and
immutable-receipt workflows. In an already configured mutable build tree:

- Kernel iteration: `cmake --build build-x86_64 --target kernel -j2`.
- Focused port iteration: `cmake --build build-x86_64/ports --target port-netsurf -j2`
  or the verified target for the changed port.
- Staged runtime/overlay refresh: `cmake --build build-x86_64 --target rootfs-refresh -j2`.
  This uses existing payloads and configured overlays; it does not rebuild a
  changed port. Do not overwrite an immutable receipt or an active VM disk.
- Direct image construction is `scripts/image/make-rootfs.sh SYSROOT OUT_IMG auto`.
  It expects host-glibc Linux userland. Preserve the configured
  `ROOTFS_EXTRA_OVERLAYS` when reproducing the normal desktop image; the old
  fourth musl-libdir argument is obsolete.

Record the kernel/rootfs paths, hashes or receipt, image refresh method, and
source revisions/dirty state. File timestamps help find stale staging but do
not prove that an image contains current source.

## Symbols and focused checks

- A healthy boot reports embedded kernel symbol initialization. If backtraces
  lack symbols, first compare the artifact booted by QEMU with the ELF loaded
  into GDB. See [kernel debugging](../xv6-kernel-debugging/SKILL.md) and
  [live GDB](../xv6-debug-live-gdb/SKILL.md).
- For locking/page-cache/VM/RCU implementation changes,
  `cmake --build build-x86_64 --target kernel-sparse -j2` is a focused static
  check. It uses `kernel/scripts/run_sparse.py`, `__CHECKER__`, and compiler
  lock/context annotations. Check tool availability before claiming a pass.
- Test mapping alignment by syscall and ABI; do not apply blanket rounding to
  `mmap`, `mprotect`, `munmap`, `msync`, `madvise`, or `mremap`. Page-aligned VMA
  bounds and byte-granular `brk` state are distinct. Follow the memory and
  syscall skills for source-specific localization.

## Conditional references

- [Hyper-V GPU contracts](references/hyperv-gpu.md): DXG/D3DKMT ownership,
  publication, fences, native display admission, and backend-specific proof.
- [Port and rootfs diagnostics](references/ports-and-rootfs.md): NetSurf,
  TLS/OpenSSH packaging, and legacy Wayland launch distinctions.

## Nested repositories

Check `git status --short` in the root and each affected submodule before edits
or staging. Preserve unrelated dirty state. When commits/pushes are requested,
work from the deepest changed submodule upward, commit parent pointer updates,
and publish submodule commits before their parent pointers. Report any
unpublished dependency or rejected remote honestly; this skill does not itself
request a commit or push.
