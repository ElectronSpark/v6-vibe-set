# xv6-os Skills Index

Current source is authoritative. Treat any local `xv6-tmp` checkout as historical/reference material only; when it conflicts with `/home/es/xv6-os`, prefer the current repo, especially for x86_64/QEMU boot, timers, interrupts, devices, and e1000.

The authoritative skill files for this repo live in `.github/skills`. `.codex/skills` entries are redirects only; migrate durable content into `.github/skills` and update this index.

## Repo Workflow Skill

- `xv6-os-debugging`: repo-level workflow for QEMU, kernel symbols, GUI/Wayland ports, NetSurf, OpenSSL/OpenSSH, rootfs images, and nested submodule commit/push rules.

## Kernel Module Skills

- `xv6-kernel-arch-platform`: arch boot, MMU, platform, SMP, IPI, trapframe ABI.
- `xv6-kernel-traps-syscalls`: traps, exceptions, IRQ dispatch, syscall ABI.
- `xv6-kernel-process-scheduler`: threads, run queues, scheduler classes, process lifecycle, workqueues.
- `xv6-kernel-sleep-wakeup`: thread queues, futex, timed sleeps, missed wakeups.
- `xv6-kernel-timers`: generic timer core, scheduler timers, hardware ticks, RTC, timeout consumers.
- `xv6-kernel-memory-management`: pages, kalloc, slab, VM, rmap, page cache, reclaim, OOM.
- `xv6-kernel-locking-rcu`: spinlocks, mutexes, rwsems, completions, RCU, lock order.
- `xv6-kernel-device-core`: device registry, cdev operations, devtmpfs-visible device lifecycle.
- `xv6-kernel-block-storage`: blkdev, bio, iosched, gendisk, partitions, virtio, ramdisk, loop.
- `xv6-kernel-input`: PS/2 keyboard/mouse, vmmouse, input rings, cdev poll readiness.
- `xv6-kernel-network-devices`: netdev, e1000, platform NICs, RX/TX, `/dev/netconf`.
- `xv6-kernel-vfs-core`: paths, mounts, fdtable, files, dentries, inodes, special fds.
- `xv6-kernel-vfs-data-io`: regular-file data I/O separation, generic VFS/page-cache mapping, readahead, writeback, mmap faults, and xv6fs/ext4fs data path migration.
- `xv6-kernel-filesystems`: tmpfs, xv6fs, devtmpfs, procfs, ext4/lwext4.
- `xv6-kernel-event-wait`: kqueue, epoll, poll callbacks, readiness, timed event waits.
- `xv6-kernel-lwip-networking`: lwIP port, sockets, DHCP/DNS/TCP/UDP, network daemons.
- `xv6-kernel-tty-console`: TTY, PTY, termios, sessions, job control, console/UART.
- `xv6-kernel-ipc`: message queues, semaphores, shared memory, IPC IDs and wakeups.
- `xv6-kernel-debugging`: GDB stub, symbols, backtraces, coredump, diagnostics, asm offsets.
- `xv6-kernel-data-structures`: list, hlist, rbtree, maple tree, xarray, intrusive containers.
- `xv6-kernel-utility-tools`: bits, strings, kobject, accounting, compiler/cache/type helpers.
- `xv6-kernel-build-init`: CMake, linker scripts, image layout, `start_kernel`, init order.

## Specialized Debug Skills

- `xv6-kernel-freeze-triage`: first stop for QEMU/KVM freezes and `xv6-freeze` captures.
- `xv6-kernel-timers-scheduler`: deep `xv6-timers`, `sleep_ms`, scheduler timeout triage.
- `xv6-kernel-network-e1000`: focused QEMU e1000 RX/TX and timer/IRQ hotspot triage.
- `xv6-wayland-kernel-bridge`: compositor/kernel boundary, generated `wlcomp.c`, input/event loop regressions.

## Fluid Debugging Skills

These skills are deliberately provisional methodology playbooks. They are not ground truth, may be incomplete, and can become deprecated without notice. Use them for active debugging practice, common failure patterns, live-capture workflows, and hypotheses that are not yet stable enough to promote into source-derived module skills.

- `xv6-debug-fluid-triage`: uncertainty-first methodology, evidence labels, and common hypothesis traps.
- `xv6-debug-live-gdb`: live QEMU/GDB sampling method, stale VM checks, and recurring capture mistakes.
- `xv6-debug-build-repro`: reproducibility methodology for fresh clones, Docker, copied toolchains, stamps, CMake/Ninja, and submodules.
- `xv6-debug-gui-runtime`: GUI debugging method for producer/waiter/consumer/renderer splits and recurring compositor traps.

## Routing Hints

- GUI input freezes: start with `xv6-wayland-kernel-bridge`, then `xv6-kernel-input`, `xv6-kernel-event-wait`, and `xv6-kernel-timers`.
- Ambiguous or changing debug evidence: start with `xv6-debug-fluid-triage`, then promote stable conclusions into the matching module skill.
- Fresh build or container reproducibility failures: start with `xv6-debug-build-repro`, then route to `xv6-kernel-build-init` once the failure is stable.
- All CPUs idle or KVM hangs: start with `xv6-kernel-freeze-triage`.
- CPU0 stuck in network RX: use `xv6-kernel-network-e1000`, then `xv6-kernel-network-devices`.
- Open/read/write/path bugs: use `xv6-kernel-vfs-core`, then the matching filesystem or device skill.
- Page fault or mmap bugs: use `xv6-kernel-traps-syscalls`, `xv6-kernel-memory-management`, and `xv6-kernel-arch-platform`.
- Lost terminal/job-control behavior: use `xv6-kernel-tty-console`, then process/signal skills.
