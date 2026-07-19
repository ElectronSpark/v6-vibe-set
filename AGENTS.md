# Repository Agent Rules

These rules are binding for every agent and worker operating in this repository.
Repeat the search, process-wait, serial-console, `pgrep`, and QEMU-cleanup rules
verbatim or by explicit reference in every worker prompt.

## Host resource and search safety

- Every repository content search must use the guarded `rg` installed at
  `/home/es/.local/bin/rg` (the repository implementation is
  `scripts/audit/safe-rg.sh`). Never invoke `/usr/bin/rg` directly; only the
  guarded wrappers may call it.
- Recursive searches must never use `-a`/`--text`, any `-u`/`-uuu` form,
  `--no-ignore` (including its variants), or `--binary`. Do not use clustered
  short options that contain a banned option.
- Never recursively content-search generated build trees, filesystem images,
  disk images, rootfs artifacts, archives, or other binary artifact trees.
  Search source trees only. For a necessary artifact search, name each regular
  file explicitly and use `scripts/audit/safe-rg-artifact.sh`; every file must
  pass its symlink-parent, type, case-insensitive image/archive extension, and
  64 MiB size checks. Search operands must canonicalize inside this repository;
  absolute host roots and `..` traversal are forbidden.
- `head`, output-token limits, and similar consumers bound displayed output,
  not scanning, allocation, or runtime. They are not resource controls.
- Across the conductor and all workers, only one potentially large search may
  run at a time. The guarded wrappers enforce a global lock; do not bypass it.
- If an exec/tool call returns a `session_id`, cell ID, or other running-process
  handle, synchronously wait on that exact process until it exits before
  starting another search or resource-heavy command. Do not use monitors or
  polling loops that can abandon the real process.
- Keep at most the three most recent completed iterations of generated build
  trees and build images. Before creating a fourth iteration, remove the oldest
  confirmed-unused iteration. Active VM disks, Docker data, deployed runtime
  images, source trees, and the newest known-good rollback are never automatic
  cleanup candidates.

## VM and console safety

- Use synchronous waits only for builds, VM runs, and serial-console commands.
  Serial silence is not proof of inactivity; wait on the actual command, then
  check VM state and obtain a fresh prompt before drawing a verdict.
- Keep serial commands short and use marker variables. Account for the known
  first-character drop after a bracketed-paste prompt.
- Avoid the `pgrep` self-match trap. Prefer exact PID ownership or exact process
  names (`ps -C qemu-system-x86_64`) and verify the command line before acting.
- Every QEMU run must have an owned PID/process group, synchronous reap, bounded
  cleanup, and a final exact no-QEMU check. Never leave QEMU running.
- The conductor may authorize at most one VM worker at a time; every other lane
  must be explicitly no-boot. Before dispatch, verify that no VM worker is
  active and that an exact `/proc/*/exe` scan finds zero `qemu-system-*` or
  `qemu-kvm` processes; do not use `pgrep`. Do not authorize another VM worker
  until the current worker has synchronously finished, its owned cleanup has
  passed, and the exact process count is again zero. This orchestration rule
  does not cap guest `QEMU_MEMORY` or `-m`; increase them when justified.
