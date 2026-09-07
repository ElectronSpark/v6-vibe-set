# xv6-os Skills Index

Current source is authoritative. Treat any local `xv6-tmp` checkout as historical/reference material only; when it conflicts with `/home/es/xv6-os`, prefer the current repo, especially for x86_64/QEMU boot, timers, interrupts, devices, and e1000.

The authoritative skill files for this repo live in `.github/skills`. `.codex/skills` entries are redirects only; migrate durable content into `.github/skills` and update this index.

## Repo Workflow Skill

- [xv6-os-debugging](xv6-os-debugging/SKILL.md): repo workflow for owned QEMU launches, KDE/Wayland audits,
  virgl, kernel symbols, host-glibc images, ports, and requested nested commits/pushes.
  Conditional references cover Hyper-V GPU contracts and port diagnostics.
- [xv6-linux-gui-abi](xv6-linux-gui-abi/SKILL.md): Linux GUI ABI workflow for X11/XWayland,
  Chromium/WebKit host-app stress, AF_UNIX/SCM, D-Bus Unix sockets,
  procfs/fdtable/epoll/futex issues exposed by GUI apps, and desktop launcher
  ABI policy.

## Kernel Module Skills

- [xv6-kernel-arch-platform](xv6-kernel-arch-platform/SKILL.md): arch boot, MMU, platform, SMP, IPI, trapframe ABI.
- [xv6-kernel-traps-syscalls](xv6-kernel-traps-syscalls/SKILL.md): traps, exceptions, IRQ dispatch, syscall ABI.
- [xv6-kernel-process-scheduler](xv6-kernel-process-scheduler/SKILL.md): threads, run queues, scheduler classes, process lifecycle, workqueues.
- [xv6-kernel-sleep-wakeup](xv6-kernel-sleep-wakeup/SKILL.md): thread queues, futex, timed sleeps, missed wakeups.
- [xv6-kernel-timers](xv6-kernel-timers/SKILL.md): generic timer core, scheduler timers, hardware ticks, RTC, timeout consumers.
- [xv6-kernel-memory-management](xv6-kernel-memory-management/SKILL.md): pages, kalloc, slab, VM, rmap, page cache, reclaim, OOM.
- [xv6-kernel-locking-rcu](xv6-kernel-locking-rcu/SKILL.md): spinlocks, mutexes, rwsems, completions, RCU, lock order.
- [xv6-kernel-device-core](xv6-kernel-device-core/SKILL.md): device registry, cdev operations, devtmpfs-visible device lifecycle.
- [xv6-kernel-block-storage](xv6-kernel-block-storage/SKILL.md): blkdev, bio, iosched, gendisk, partitions, virtio, ramdisk, loop.
- [xv6-kernel-input](xv6-kernel-input/SKILL.md): PS/2, vmmouse, virtio input, Linux evdev ABI, input rings,
  per-open delivery and poll readiness.
- [xv6-kernel-network-devices](xv6-kernel-network-devices/SKILL.md): netdev, e1000, platform NICs, RX/TX, `/dev/netconf`.
- [xv6-kernel-vfs-core](xv6-kernel-vfs-core/SKILL.md): paths, mounts, fdtable, files, dentries, inodes, special fds.
- [xv6-kernel-vfs-data-io](xv6-kernel-vfs-data-io/SKILL.md): regular-file data I/O separation, generic VFS/page-cache mapping, readahead, writeback, mmap faults, and xv6fs/ext4fs data path migration.
- [xv6-kernel-filesystems](xv6-kernel-filesystems/SKILL.md): tmpfs, xv6fs, devtmpfs, procfs, ext4/lwext4.
- [xv6-kernel-event-wait](xv6-kernel-event-wait/SKILL.md): kqueue, epoll, poll callbacks, readiness, timed event waits.
- [xv6-kernel-lwip-networking](xv6-kernel-lwip-networking/SKILL.md): lwIP port, sockets, DHCP/DNS/TCP/UDP, network daemons.
- [xv6-kernel-tty-console](xv6-kernel-tty-console/SKILL.md): TTY, PTY, termios, sessions, job control, console/UART.
- [xv6-kernel-ipc](xv6-kernel-ipc/SKILL.md): message queues, semaphores, shared memory, IPC IDs and wakeups.
- [xv6-kernel-debugging](xv6-kernel-debugging/SKILL.md): GDB stub, symbols, backtraces, coredump, diagnostics, asm offsets.
- [xv6-kernel-data-structures](xv6-kernel-data-structures/SKILL.md): list, hlist, rbtree, maple tree, xarray, intrusive containers.
- [xv6-kernel-utility-tools](xv6-kernel-utility-tools/SKILL.md): bits, strings, kobject, accounting, compiler/cache/type helpers.
- [xv6-kernel-build-init](xv6-kernel-build-init/SKILL.md): CMake, linker scripts, image layout, `start_kernel`, init order.

## Specialized Debug Skills

- [xv6-kernel-freeze-triage](xv6-kernel-freeze-triage/SKILL.md): first stop for QEMU/KVM freezes and `xv6-freeze` captures.
- [xv6-kernel-timers-scheduler](xv6-kernel-timers-scheduler/SKILL.md): deep `xv6-timers`, `sleep_ms`, scheduler timeout triage.
- [xv6-kernel-network-e1000](xv6-kernel-network-e1000/SKILL.md): focused QEMU e1000 RX/TX and timer/IRQ hotspot triage.
- [xv6-wayland-kernel-bridge](xv6-wayland-kernel-bridge/SKILL.md): KWin/input/readiness/DRM boundaries; a conditional
  reference covers the retired generated `wlcomp` workflow.

## Fluid Debugging Skills

These skills are deliberately provisional methodology playbooks. They are not ground truth, may be incomplete, and can become deprecated without notice. Use them for active debugging practice, common failure patterns, live-capture workflows, and hypotheses that are not yet stable enough to promote into source-derived module skills.

- [xv6-debug-fluid-triage](xv6-debug-fluid-triage/SKILL.md): uncertainty-first methodology, evidence labels, and common hypothesis traps.
- [xv6-debug-live-gdb](xv6-debug-live-gdb/SKILL.md): live QEMU/GDB sampling method, stale VM checks, and recurring capture mistakes.
- [xv6-debug-build-repro](xv6-debug-build-repro/SKILL.md): reproducibility methodology for fresh clones, Docker, copied toolchains, stamps, CMake/Ninja, and submodules.
- [xv6-debug-gui-runtime](xv6-debug-gui-runtime/SKILL.md): current KDE/SDL/virgl graphical audits, mouse
  interaction coverage, captures, and producer/waiter/consumer/renderer triage.
  Active GPU/WebKit work lives in `docs/active-work-plan.md`.
  `WEBKIT_GAP_MAP.md` and `AGENT_TEAM.md` retain reference notes and role
  coordination. Original plans and GPU checklists are preserved under
  `docs/archive/plan-consolidation-20260907/`.

## Routing Hints

- Graphical progress review, icons/buttons, or mouse exploration: start with
  `xv6-debug-gui-runtime` and the owned launch rules in `xv6-os-debugging`.
  An audit alone does not require a reducer, source fix, or image rebuild.
- Linux GUI ABI, X11/XWayland, Chromium host-app stress, AF_UNIX/SCM fd
  passing, or desktop symlink-vs-launcher policy: start with
  `xv6-linux-gui-abi`, then route to the matching kernel module skill.
- GUI input freezes: identify the running compositor with
  `xv6-wayland-kernel-bridge`, then use `xv6-kernel-input`,
  `xv6-kernel-event-wait`, and `xv6-kernel-timers`. Current KDE work must not be
  routed into historical `wlcomp` generated-source changes.
- Ambiguous or changing debug evidence: start with `xv6-debug-fluid-triage`, then promote stable conclusions into the matching module skill.
- Fresh build or container reproducibility failures: start with `xv6-debug-build-repro`, then route to `xv6-kernel-build-init` once the failure is stable.
- All CPUs idle or KVM hangs: start with `xv6-kernel-freeze-triage`.
- CPU0 stuck in network RX: identify the selected driver with
  `xv6-kernel-network-devices`; use `xv6-kernel-network-e1000` for an e1000
  device/stack and follow the virtio-net path when that is the actual driver.
- Open/read/write/path bugs: use `xv6-kernel-vfs-core`, then the matching filesystem or device skill.
- Page fault or mmap bugs: use `xv6-kernel-traps-syscalls`, `xv6-kernel-memory-management`, and `xv6-kernel-arch-platform`.
- Lost terminal/job-control behavior: use `xv6-kernel-tty-console`, then process/signal skills.
- Hyper-V GPU/OpenGL work: start with `xv6-os-debugging` for build/deploy/live
  probe rules, then use `docs/active-work-plan.md` and
  `xv6-debug-gui-runtime/AGENT_TEAM.md` for current graphics scope and role
  split. Check the active plan before reopening GPU-P display work; validation
  contracts are not a request to add an unavailable host display transport.
  On Hyper-V, keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT` false until the
  backend's non-readback D3D12 presentation and finite 480p >60 FPS gate are
  validated. KVM/virgl control evidence is separate from Hyper-V credit.
