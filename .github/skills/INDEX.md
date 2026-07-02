# xv6-os Skills Index

Current source is authoritative. Treat any local `xv6-tmp` checkout as historical/reference material only; when it conflicts with `/home/es/xv6-os`, prefer the current repo, especially for x86_64/QEMU boot, timers, interrupts, devices, and e1000.

The authoritative skill files for this repo live in `.github/skills`. `.codex/skills` entries are redirects only; migrate durable content into `.github/skills` and update this index.

The single active plan is `docs/active-work-plan.md`. Former plan files under
`docs/archive/plan-consolidation-20260701/` are historical references only; do
not route new skill instructions or handoffs to archived plans.

## How To Use This Index (read this first)

1. If the task references current work, run state, or evidence, read
   `docs/active-work-plan.md` before anything else. Skills hold durable
   mechanisms and workflows; the plan holds current status and archives.
2. Pick ONE starting skill from the Routing Hints below and read it fully
   before editing code. Follow its cross-references instead of guessing.
3. Skills never contain run IDs or current measurements; when a skill says
   "see the plan", that is where the current numbers live.
4. Before any VM run: check for stale QEMU (`pgrep -af qemu`), rebuild the
   changed layer, and verify `build-x86_64/fs.img` is fresh if userland
   changed. The exact commands are in `xv6-os-debugging`.
5. After a VM proof: archive artifacts per the commands in
   `xv6-os-debugging` before reusing the scratch directory.

## Skill Maintenance Rules

- Keep durable workflow lessons in the matching `SKILL.md`; keep run IDs,
  current logs, and transient evidence pointers in `docs/active-work-plan.md`
  or the run artifact.
- Keep large retired baselines, long TODOs, and role maps in companion files
  linked from the skill body. The skill body should stay focused on routing,
  workflow, and the minimum non-obvious rules needed at trigger time.
- When adding, renaming, or archiving a skill, update this index and any
  `.codex/skills` redirect that points at it.

## Repo Workflow Skill

- [`xv6-os-debugging`](xv6-os-debugging/SKILL.md): repo-level workflow for QEMU, kernel symbols, GUI/Wayland ports, NetSurf, OpenSSL/OpenSSH, rootfs images, and nested submodule commit/push rules.
- [`xv6-linux-gui-abi`](xv6-linux-gui-abi/SKILL.md): Linux GUI ABI workflow for X11/XWayland,
  Chromium/WebKit host-app stress, AF_UNIX/SCM, D-Bus Unix sockets,
  procfs/fdtable/epoll/futex issues exposed by GUI apps, and desktop launcher
  ABI policy.

## Kernel Module Skills

- [`xv6-kernel-arch-platform`](xv6-kernel-arch-platform/SKILL.md): arch boot, MMU, platform, SMP, IPI, trapframe ABI.
- [`xv6-kernel-traps-syscalls`](xv6-kernel-traps-syscalls/SKILL.md): traps, exceptions, IRQ dispatch, syscall ABI.
- [`xv6-kernel-process-scheduler`](xv6-kernel-process-scheduler/SKILL.md): threads, run queues, scheduler classes, process lifecycle, workqueues.
- [`xv6-kernel-sleep-wakeup`](xv6-kernel-sleep-wakeup/SKILL.md): thread queues, futex, timed sleeps, missed wakeups.
- [`xv6-kernel-timers`](xv6-kernel-timers/SKILL.md): generic timer core, scheduler timers, hardware ticks, RTC, timeout consumers.
- [`xv6-kernel-memory-management`](xv6-kernel-memory-management/SKILL.md): pages, kalloc, slab, VM, rmap, page cache, reclaim, OOM.
- [`xv6-kernel-locking-rcu`](xv6-kernel-locking-rcu/SKILL.md): spinlocks, mutexes, rwsems, completions, RCU, lock order.
- [`xv6-kernel-device-core`](xv6-kernel-device-core/SKILL.md): device registry, cdev operations, devtmpfs-visible device lifecycle.
- [`xv6-kernel-block-storage`](xv6-kernel-block-storage/SKILL.md): blkdev, bio, iosched, gendisk, partitions, virtio, ramdisk, loop.
- [`xv6-kernel-input`](xv6-kernel-input/SKILL.md): PS/2 keyboard/mouse, vmmouse, input rings, cdev poll readiness.
- [`xv6-kernel-network-devices`](xv6-kernel-network-devices/SKILL.md): netdev, e1000, platform NICs, RX/TX, `/dev/netconf`.
- [`xv6-kernel-vfs-core`](xv6-kernel-vfs-core/SKILL.md): paths, mounts, fdtable, files, dentries, inodes, special fds.
- [`xv6-kernel-vfs-data-io`](xv6-kernel-vfs-data-io/SKILL.md): regular-file data I/O separation, generic VFS/page-cache mapping, readahead, writeback, mmap faults, and xv6fs/ext4fs data path migration.
- [`xv6-kernel-filesystems`](xv6-kernel-filesystems/SKILL.md): tmpfs, xv6fs, devtmpfs, procfs, ext4/lwext4.
- [`xv6-kernel-event-wait`](xv6-kernel-event-wait/SKILL.md): kqueue, epoll, poll callbacks, readiness, timed event waits.
- [`xv6-kernel-lwip-networking`](xv6-kernel-lwip-networking/SKILL.md): lwIP port, sockets, DHCP/DNS/TCP/UDP, network daemons.
- [`xv6-kernel-tty-console`](xv6-kernel-tty-console/SKILL.md): TTY, PTY, termios, sessions, job control, console/UART.
- [`xv6-kernel-ipc`](xv6-kernel-ipc/SKILL.md): message queues, semaphores, shared memory, IPC IDs and wakeups.
- [`xv6-kernel-debugging`](xv6-kernel-debugging/SKILL.md): GDB stub, symbols, backtraces, coredump, diagnostics, asm offsets.
- [`xv6-kernel-data-structures`](xv6-kernel-data-structures/SKILL.md): list, hlist, rbtree, maple tree, xarray, intrusive containers.
- [`xv6-kernel-utility-tools`](xv6-kernel-utility-tools/SKILL.md): bits, strings, kobject, accounting, compiler/cache/type helpers.
- [`xv6-kernel-build-init`](xv6-kernel-build-init/SKILL.md): CMake, linker scripts, image layout, `start_kernel`, init order.

## Specialized Debug Skills

- [`xv6-kernel-freeze-triage`](xv6-kernel-freeze-triage/SKILL.md): first stop for QEMU/KVM freezes and `xv6-freeze` captures.
- [`xv6-kernel-timers-scheduler`](xv6-kernel-timers-scheduler/SKILL.md): deep `xv6-timers`, `sleep_ms`, scheduler timeout triage.
- [`xv6-kernel-network-e1000`](xv6-kernel-network-e1000/SKILL.md): focused QEMU e1000 RX/TX and timer/IRQ hotspot triage.
- [`xv6-wayland-kernel-bridge`](xv6-wayland-kernel-bridge/SKILL.md): compositor/kernel boundary, generated `wlcomp.c`, input/event loop regressions.

## Fluid Debugging Skills

These skills are deliberately provisional methodology playbooks. They are not ground truth, may be incomplete, and can become deprecated without notice. Use them for active debugging practice, common failure patterns, live-capture workflows, and hypotheses that are not yet stable enough to promote into source-derived module skills.

- [`xv6-debug-fluid-triage`](xv6-debug-fluid-triage/SKILL.md): uncertainty-first methodology, evidence labels, and common hypothesis traps.
- [`xv6-debug-live-gdb`](xv6-debug-live-gdb/SKILL.md): live QEMU/GDB sampling method, stale VM checks, and recurring capture mistakes.
- [`xv6-debug-build-repro`](xv6-debug-build-repro/SKILL.md): reproducibility methodology for fresh clones, Docker, copied toolchains, stamps, CMake/Ninja, and submodules.
- [`xv6-debug-gui-runtime`](xv6-debug-gui-runtime/SKILL.md): GUI debugging method for producer/waiter/consumer/renderer splits and recurring compositor traps.
  Companion files: `GPU_OPENGL_PLAN.md` for the current GPU milestone shape,
  `VALIDATED_GPU_BASELINES.md` for retired GPU/native-present baseline notes,
  `WEBKIT_TODO.md`, `WEBKIT_GAP_MAP.md`, and `AGENT_TEAM.md` for long GPU/GUI
  role coordination and current Hyper-V lessons learned.

## Routing Hints

- Performance/latency triage (slow desktop, slow app launch, low FPS):
  - Presented FPS far below rendered/decoded FPS: `xv6-debug-gui-runtime`
    (Present Pacing section).
  - Everything-is-slow or syscall-heavy phases lag: `xv6-kernel-traps-syscalls`
    (Per-Syscall Cost Map, `syscalltlb` benchmark).
  - Suspected slow scheduling after wakeup: `xv6-kernel-process-scheduler`
    (Wake-To-Run Latency Diagnostic) before touching scheduler code.
  - copyin/copyout or VMA-validate hot buckets: `xv6-kernel-memory-management`
    (User Copy Path Facts).
- Linux GUI ABI, X11/XWayland, Chromium host-app stress, AF_UNIX/SCM fd
  passing, or desktop symlink-vs-launcher policy: start with
  `xv6-linux-gui-abi`, then route to the matching kernel module skill.
- GUI input freezes: start with `xv6-wayland-kernel-bridge`, then `xv6-kernel-input`, `xv6-kernel-event-wait`, and `xv6-kernel-timers`.
- `rcu_head_cache` double-free crash signature: `xv6-kernel-locking-rcu`
  (Known Intermittent section) — do not blame the change under test on the
  first occurrence.
- Ambiguous or changing debug evidence: start with `xv6-debug-fluid-triage`, then promote stable conclusions into the matching module skill.
- Fresh build or container reproducibility failures: start with `xv6-debug-build-repro`, then route to `xv6-kernel-build-init` once the failure is stable.
- All CPUs idle or KVM hangs: start with `xv6-kernel-freeze-triage`.
- CPU0 stuck in network RX: use `xv6-kernel-network-e1000`, then `xv6-kernel-network-devices`.
- Open/read/write/path bugs: use `xv6-kernel-vfs-core`, then the matching filesystem or device skill.
- Page fault or mmap bugs: use `xv6-kernel-traps-syscalls`, `xv6-kernel-memory-management`, and `xv6-kernel-arch-platform`.
- Lost terminal/job-control behavior: use `xv6-kernel-tty-console`, then process/signal skills.
- Hyper-V GPU/OpenGL work: start with `xv6-os-debugging` for build/deploy/live
  probe rules, then use `xv6-debug-gui-runtime/GPU_OPENGL_PLAN.md` and
  `xv6-debug-gui-runtime/AGENT_TEAM.md` for the graphics gap map and role
  split. Keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT` false until non-readback D3D12
  presentation and the finite 480p >60 FPS demo are validated.
