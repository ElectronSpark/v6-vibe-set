# xv6-os Kernel Modular Hierarchy

This report maps the kernel into two functional layers: top-level modules and skill-sized submodules. It is based on the actual build graph in `kernel/kernel/CMakeLists.txt`, the architecture CMake files, public include namespaces under `kernel/kernel/inc`, and the current source tree.

Use this as a source document for future `.github/skills/<name>/SKILL.md` files. Each second-layer item is intended to be small enough to become one skill or one section inside a closely related skill.

## Build-Time Shape

The linked kernel is composed from these object-library modules:

- `arch_${ARCH}`: architecture-specific code from `kernel/arch/riscv` or `kernel/arch/x86_64`.
- `irq`: generic interrupt registration and dispatch glue.
- `dev`: generic device core plus block, char, input, display, and platform device drivers.
- `lock`: synchronization primitives and RCU.
- `mm`: physical memory, virtual memory, allocators, page cache, reclaim.
- `proc`: threads, scheduler, process lifecycle, signals, futexes, workqueues.
- `timer`: generic scheduler timer object library.
- `vfs_core`, `vfs_tmpfs`, `vfs_xv6fs`, `vfs_devtmpfs`, `vfs_procfs`: VFS core and in-tree filesystems.
- `ipi`: generic IPI stub layer; architecture code supplies the real IPI backend.
- `tty`: TTY, PTY, termios, sessions.
- `lwip`: lwIP core plus xv6 port glue and socket syscalls.
- `ext4fs`: lwext4 library plus xv6 VFS port.
- `daemons`: kernel-resident network daemons.
- `gdbstub`: remote GDB protocol support.
- `kqueue_core`: kqueue and epoll compatibility.
- `ipc`: System V style IPC.

Root-level kernel files add bootstrap, core libraries, diagnostics, storage/network drivers that predate the `dev/` split, symbol support, and generic data structures.

## 1. Architecture And Platform Layer

Responsibility: isolate CPU, MMU, trap, interrupt-controller, timer, boot, and platform details behind generic kernel interfaces.

Representative directories: `kernel/arch/riscv`, `kernel/arch/x86_64`, `kernel/kernel/inc/arch`, `kernel/kernel/inc/smp`.

Submodules:

- **Boot and early CPU entry**: `start.c`, `entry.S`, `ap_trampoline.S` on x86_64, RISC-V boot integration under `arch/riscv/boot`. Owns first C entry, hart/AP startup, stack transition, early CPU state.
- **Architecture trap and syscall entry**: `arch/*/irq/trap.c`, `trapvec.S`, `trampoline.S`, `kernelvec.S`, arch `syscall.c`. Owns CPU exception decoding, user/kernel trapframes, syscall entry ABI, return-to-user path.
- **Interrupt controllers**: x86_64 `lapic.c`, `ioapic.c`; RISC-V `plic.c`. Owns interrupt enablement, EOI, external IRQ routing, local timer interrupt plumbing.
- **Architecture timers and RTC**: `arch/*/timer/timer.c`, RISC-V `goldfish_rtc.c`. Owns low-level tick source programming and RTC read/write support.
- **MMU and page tables**: `arch/*/mm/vm.c`, `arch/*/mm/pgtable.c`, `inc/arch/pgtable_defs.h`, `inc/arch/vm.h`. Owns page table format, TLB flushes, kernel mappings, user mapping helpers.
- **Context switch, FPU, and signal trampoline ABI**: `arch/*/proc/swtch.S`, `fpu.S`, `sig_trampoline.S`, `inc/arch/context.h`, `inc/arch/trapframe.h`. Owns saved register ABI and assembly-visible offsets.
- **SMP and IPI backend**: `arch/*/ipi/ipi.c`, `kernel/kernel/ipi/ipi.c`, `ipi_stub.c`, `inc/smp/percpu.h`, `inc/smp/atomic.h`. Owns cross-CPU callbacks, per-CPU state access, and architecture IPI delivery.
- **Platform discovery**: `platform_x86.c`, `platform_riscv.c`, RISC-V `dev/fdt.c`, x86 PCI-facing platform setup. Owns CPU count, platform capability discovery, and platform-global init data.
- **Architecture debugging**: `arch/*/backtrace.c`, `arch/*/gdbstub_arch.c`. Owns register decoding, backtrace mechanics, and remote-debug architecture hooks.

Skill granularity: arch boot, arch traps/syscall ABI, arch MMU, arch timers/interrupt controllers, SMP/IPI, arch debug.

## 2. Interrupt, Trap, And Syscall Core

Responsibility: connect generic kernel interrupt handling and syscall dispatch to architecture entry paths.

Representative files: `kernel/kernel/irq/irq.c`, `kernel/kernel/irq/CMakeLists.txt`, `kernel/kernel/inc/arch/irq.h`, `kernel/kernel/inc/syscall.h`, plus arch trap/syscall files.

Submodules:

- **Generic IRQ dispatch**: `kernel/kernel/irq/irq.c`. Owns IRQ handler registration, dispatch, and generic interrupt accounting hooks.
- **Syscall numbering and dispatch contract**: `inc/uabi/syscall.h`, `inc/syscall.h`, architecture `syscall.c`, subsystem `sys_*` implementations. Owns syscall ABI boundaries and return/error conventions.
- **Trapframe and exception handoff**: `inc/trap.h`, `inc/trapframe.h`, architecture trap code. Owns the contract between arch exception state and generic thread/process handling.

Skill granularity: IRQ dispatch and driver registration, syscall ABI and dispatch, trapframe/user-return debugging.

## 3. Process, Thread, Scheduler, And Work Execution

Responsibility: thread lifecycle, scheduling, sleep/wakeup, signals, process IDs/groups, futexes, kernel workqueues, and process-facing syscalls.

Representative directories: `kernel/kernel/proc`, `kernel/kernel/inc/proc`.

Submodules:

- **Thread lifecycle and clone/exit**: `thread.c`, `clone.c`, `exit.c`, `thread_group.c`, `proc_private.h`. Owns thread allocation, task state, clone semantics, process/thread-group lifecycle, reaping.
- **PID, process groups, and sessions bridge**: `pid.c`, `pgroup.c`, `inc/proc/pgroup*.h`. Owns PID table, process groups, session-facing process membership, process dumps.
- **Scheduler core and run queues**: `sched.c`, `rq.c`, `inc/proc/rq*.h`, `SCHEDULER_DESIGN.md`. Owns run queue invariants, context-switch prepare/finish, `on_cpu`/`on_rq`, CPU affinity, wakeup races.
- **Scheduling classes**: `sched_idle.c`, `sched_fifo.c`, `sched_eevdf.c`. Owns policy-specific enqueue/dequeue/pick-next behavior.
- **Sleep, wakeup, and thread queues**: `thread_queue.c`, `THREAD_QUEUE_DESIGN.md`, sleep helpers in scheduler code. Owns channel sleeps, timed waits, ordered wait queues, wakeup protocol.
- **Signals and user signal ABI**: `signal.c`, `sys_signal.c`, `sig_trampoline.S`, `inc/signal*.h`, `inc/uabi/signal.h`. Owns signal masks, handlers, stop/continue behavior, trampoline setup.
- **Futexes and user synchronization syscalls**: `futex.c`. Owns fast userspace wait/wake integration with scheduler sleeps.
- **Kernel workqueues**: `workqueue.c`, `inc/proc/workqueue*.h`. Owns deferred process-context work used by drivers such as e1000 RX.
- **Misc process syscalls and resource view**: `sysproc.c`, `sys_misc.c`, `accounting.c`, `inc/resource.h`. Owns process control, accounting, resource-facing calls.

Skill granularity: scheduler/runqueue, sleep-wakeup/thread queues, signals, lifecycle/PID, futex, workqueue.

## 4. Timers And Timeouts

Responsibility: generic timer nodes, scheduler-timer timebase, timeout callbacks, sleep and event timeout support.

Representative files: `kernel/kernel/timer/sched_timer.c`, `kernel/kernel/timer/timer.c`, `kernel/kernel/inc/timer/*`, architecture timer files.

Submodules:

- **Generic timer root and node API**: `timer.c`, `timer.h`, `timer_types.h`. Owns ordered timer lists, retry limits, callback invocation, current/next tick state.
- **Scheduler timer service**: `sched_timer.c`, `sched_timer_private.h`. Owns `__sched_timer_ms`, scheduler timeout nodes, `sleep_ms`, timed wait callbacks, and wakeup integration.
- **Architecture tick source**: `arch/*/timer/timer.c`. Owns hardware tick programming and handoff into generic timer/scheduler time.
- **RTC and wall clock**: RISC-V `goldfish_rtc.c`, `inc/timer/goldfish_rtc.h`, wall-clock facing user/kernel paths. Owns real-time clock support separate from scheduler ticks.
- **Timer consumers**: kqueue timeouts, `timerfd.c`, futex timeouts, sleep syscalls. Owns cross-module timeout usage and debugging patterns.

Skill granularity: timer core, scheduler timers, arch timer source, timeout consumers.

## 5. Memory Management

Responsibility: physical page ownership, kernel allocation, virtual memory, page cache, reclaim, OOM handling, and memory syscalls.

Representative directories: `kernel/kernel/mm`, `kernel/kernel/inc/mm`.

Submodules:

- **Early and physical page allocation**: `early_allocator.c`, `page.c`, `kalloc.c`, `page_private.h`. Owns boot-time allocation, page metadata, reference counts, buddy/page allocator behavior.
- **Slab and kernel object allocation**: `slab.c`, `slab_private.h`, `inc/mm/slab*.h`. Owns small-object caches and slab shrinker integration.
- **Folios and page containers**: `folio.c`, `inc/mm/folio*.h`. Owns compound page abstractions used by cache and VM layers.
- **Virtual memory and memory syscalls**: `vm.c`, `sysmm.c`, `inc/mm/vm*.h`, `inc/uabi/mman.h`. Owns user address spaces, VMAs, mmap/brk-style syscalls, page faults in cooperation with arch MMU.
- **Reverse mapping**: `rmap.c`, `inc/mm/rmap.h`. Owns page-to-mapping bookkeeping needed for VM and reclaim.
- **Page cache and buffer heads**: `pcache.c`, `buffer.c`, `inc/mm/pcache*.h`, `inc/mm/buffer_head.h`. Owns cached file/block pages and block buffer metadata.
- **Reclaim and memory pressure**: `shrinker.c`, `mm_watermark.c`, `oom_kill.c`, `inc/mm/shrinker.h`, `inc/mm/mm_watermark.h`, `inc/mm/oom_kill.h`. Owns pressure thresholds, shrinker callbacks, OOM victim selection.
- **Memory statistics and UABI**: `inc/mm/memstat.h`, `inc/uabi/memstat.h`. Owns user-visible memory state formatting.

Skill granularity: physical allocator, slab, VM/mmap/page faults, page cache, reclaim/OOM.

## 6. Locking, Synchronization, And Lifetime Safety

Responsibility: primitive locks, sleeping synchronization, RCU, completions, and cross-module lock-order patterns.

Representative directories: `kernel/kernel/lock`, `kernel/kernel/inc/lock`.

Submodules:

- **Spinlocks and IRQ-safe locking**: `spinlock.c`, `rwlock.c`, `inc/lock/spinlock.h`, `inc/lock/rwlock*.h`. Owns raw lock behavior used in scheduler, IRQ, and device paths.
- **Sleeping locks**: `mutex.c`, `rwsem.c`, `semaphore.c`, `inc/lock/mutex*.h`, `rwsem*.h`, `semaphore*.h`. Owns blocking lock primitives and scheduler integration.
- **Completions**: `completion.c`, `inc/lock/completion*.h`. Owns one-shot wait/wakeup synchronization.
- **RCU**: `rcu.c`, `RCU_README.md`, `inc/lock/rcu*.h`. Owns read-side critical sections, grace periods, callback execution, scheduler quiescent-state integration, RCU-safe list/hlist patterns.
- **Lock tests and diagnostics**: `*_test.c` files gated by build/env. Owns runtime validation patterns for lock primitives.

Skill granularity: spin/IRQ locking, sleeping locks, RCU, completions, lock-order debugging.

## 7. Device Model And Driver Layer

Responsibility: device registration, character and block device APIs, I/O scheduling, partitions, input, framebuffer, platform buses, and concrete drivers.

Representative directories: `kernel/kernel/dev`, `kernel/kernel/inc/dev`, root-level legacy drivers `e1000.c`, `virtio_disk.c`, `ramdisk.c`, `pci.c`, `uart.c`.

Submodules:

- **Core device registry**: `dev.c`, `dev_test.c`, `inc/dev/dev*.h`. Owns device numbers, registration, lookup, and common device lifecycle.
- **Character devices**: `cdev.c`, `inc/dev/cdev.h`, drivers such as `nullrand.c`, `ps2mouse.c`, `ps2kbd.c`, `fb.c`, TTY registration. Owns read/write/ioctl/poll operation dispatch.
- **Block devices and request queues**: `blkdev.c`, `bio.c`, `iosched.c`, root `bio.c`, `inc/dev/blkdev.h`, `inc/dev/bio*.h`, `inc/dev/iosched*.h`. Owns block I/O submission, scheduling, completion, and buffer interaction.
- **Disk topology and partitions**: `gendisk.c`, `mbr.c`, `gpt.c`, `inc/dev/gendisk*.h`, `inc/dev/mbr.h`, `inc/dev/gpt.h`. Owns disk registration and partition discovery.
- **Storage drivers**: root `virtio_disk.c`, root `ramdisk.c`, `dev/loop.c`, `dev/x1_sdhci.c`. Owns virtio block, RAM disk, loop, and SDHCI-backed block devices.
- **Network device layer and drivers**: `dev/netdev.c`, root `net.c`, root `sysnet.c`, root `e1000.c`, `dev/x1_emac.c`, `dev/yt8531.c`, `inc/dev/net*.h`. Owns netdev abstraction, e1000/QEMU NIC, OrangePi EMAC/PHY, `/dev/netconf` style state.
- **Input devices**: `ps2mouse.c`, `ps2kbd.c`, `inc/dev/ps2mouse.h`, `inc/dev/ps2kbd.h`. Owns PS/2 and vmmouse input packets, rings, nonblocking reads, cdev poll readiness.
- **Display and console-adjacent devices**: `fb.c`, root `console.c`, root `uart.c`, `inc/dev/fb.h`, `inc/dev/uart.h`. Owns framebuffer device and low-level console/UART output paths.
- **Platform buses and discovery**: root `pci.c`, `dev/fdt.c`, `dev/x1_i2c.c`, architecture FDT/PCI hooks. Owns bus enumeration and platform-specific device discovery.

Skill granularity: device core/cdev, block I/O, partitions/storage, netdev/e1000, input, framebuffer/console, bus discovery.

## 8. Virtual File System And Filesystems

Responsibility: path resolution, mount tree, files, descriptors, inodes, dentries, file locks, pipes, sockets, special fd objects, and filesystem backends.

Representative directories: `kernel/kernel/vfs`, `kernel/kernel/inc/vfs`, `kernel/kernel/lwext4`, `kernel/kernel/lwext4_port`.

Submodules:

- **VFS object model**: `fs.c`, `inode.c`, `dcache.c`, `file.c`, `VFS_DESIGN.md`, `inc/vfs/fs.h`, `inc/vfs/file.h`, `inc/vfs/vfs_types.h`. Owns superblocks, inodes, dentries, file objects, refcounts, mount traversal.
- **File descriptor table and syscalls**: `fdtable.c`, `vfs_syscall.c`, `inc/vfs/fcntl.h`, `inc/uabi/fcntl.h`. Owns fd allocation, open/close/read/write/stat-like syscalls.
- **Path permissions and file locking**: `vfs_permission.c`, `file_lock.c`, `inc/vfs/file_lock.h`. Owns access checks and advisory locking.
- **I/O vectors and read/write helpers**: `uio.c`, `inc/vfs/uio.h`, `inc/vfs/rwf.h`. Owns vectorized I/O and shared read/write plumbing.
- **Pipes and Unix sockets**: `pipe.c`, `unix_socket.c`, `inc/vfs/pipe*.h`, `inc/vfs/unix_socket.h`. Owns anonymous pipes and AF_UNIX-style endpoints.
- **Event and timer file descriptors**: `eventfd.c`, `timerfd.c`. Owns file-descriptor based event counters and timer waits.
- **Netlink file integration**: `netlink.c`, `inc/netlink.h`. Owns VFS-facing netlink hooks used by the network stack.
- **Mount/unmount and orphan lifecycle**: `UNMOUNT_DESIGN.md`, `fs.c`, inode/superblock state. Owns lazy unmount, mount references, orphan inodes.
- **tmpfs**: `vfs/tmpfs/*`. Owns in-memory file storage, truncation, tmpfs inode/file/superblock operations.
- **xv6fs**: `vfs/xv6fs/*`. Owns the xv6 disk filesystem, block cache, log, truncate, inode/file/superblock operations.
- **devtmpfs**: `vfs/devtmpfs/*`, `inc/devtmpfs.h`. Owns filesystem view of registered devices.
- **procfs**: `vfs/procfs/*`. Owns process/system info filesystem nodes.
- **ext4 via lwext4**: `lwext4/src/*`, `lwext4_port/ext4fs_*.c`. Owns third-party ext4 core plus xv6 VFS adapter and blockdev bridge.

Skill granularity: VFS core, fd/syscalls, pipes/sockets, special fds, tmpfs, xv6fs, devtmpfs/procfs, ext4fs port, unmount/orphans.

## 9. Event Notification And Multiplexing

Responsibility: kqueue core, epoll compatibility, filter registration, event readiness, timeout and signal/proc/file event delivery.

Representative directories: `kernel/kernel/kqueue`, `kernel/kernel/inc/kqueue*.h`, `kernel/kernel/inc/vfs/poll.h`, `kernel/kernel/inc/uabi/poll.h`.

Submodules:

- **Kqueue core**: `kqueue.c`, `kqueue_syscall.c`. Owns kqueue objects, knote lifecycle, ready queue, wait loop, timeout handling.
- **Filter implementations**: `kqueue_filters.c`. Owns read/write readiness, timer filters, signal/process/file filters.
- **Epoll compatibility layer**: `epoll.c`. Owns Linux epoll API translation onto kqueue filters.
- **Poll callback contract**: cdev/file `.poll` callbacks, `inc/vfs/poll.h`, `inc/uabi/poll.h`. Owns cross-module readiness semantics used by input, TTY, sockets, pipes, and special fds.
- **Timed event waits**: kqueue wait timeout plus scheduler timer integration. Owns the interaction with `sched_timer` and `CHAN=0` timed sleeping.

Skill granularity: kqueue core, epoll compatibility, poll/readiness contract, timed waits.

## 10. Networking Stack And Kernel Network Services

Responsibility: lwIP integration, socket syscalls, netdev bridge, protocol apps, kernel daemons, and user-visible network state.

Representative directories: `kernel/kernel/lwip`, `kernel/kernel/lwip_port`, `kernel/kernel/daemons`, plus device net drivers.

Submodules:

- **lwIP core import**: `lwip/src/core`, `lwip/src/api`, `lwip/src/netif`. Owns TCP/IP protocol logic, DHCP, DNS, TCP, UDP, pbufs, timeouts.
- **xv6 lwIP OS port**: `lwip_port/sys_arch.c`, `lwip_port/lwip_glue.c`, `lwip_port/lwipopts.h`, `lwip_port/arch/*`. Owns memory, threading, time, mailbox/semaphore style abstractions for lwIP.
- **Socket syscall bridge**: `lwip_port/sys_socket.c`, VFS socket integration. Owns BSD socket syscall behavior and file/socket boundary.
- **Netdev bridge**: root `net.c`, `sysnet.c`, `dev/netdev.c`, driver callbacks. Owns packet handoff between devices and lwIP.
- **NIC drivers**: root `e1000.c`, `dev/x1_emac.c`, `dev/yt8531.c`. Owns hardware-specific TX/RX, interrupts, polling, deferred RX work.
- **Kernel daemons**: `daemons/telnetd.c`, `tftpd.c`, `iperfd.c`, `sntpd.c`, `mdnsd.c`, `netbiosd.c`. Owns kernel-resident service startup and protocol endpoints.
- **Network management state**: `inc/dev/netconf.h`, `/dev/netconf` users. Owns user-visible link/IP/DNS/gateway display data.

Skill granularity: lwIP port, socket syscalls, netdev bridge, e1000/NIC drivers, kernel daemons, netconf/debugging.

## 11. TTY, PTY, Console, And Sessions

Responsibility: terminal device behavior, line discipline, PTYs, termios, session foreground control, console integration.

Representative directories: `kernel/kernel/tty`, `kernel/kernel/inc/tty`, root `console.c`, `uart.c`, `printf.c`, `diag.c`.

Submodules:

- **TTY core and line discipline**: `tty.c`, `tty_dev.c`, `TTY_DESIGN.md`, `inc/tty/tty*.h`. Owns canonical/raw behavior, input/output queues, echoing, TTY cdev registration.
- **PTY and ptmx**: `pty.c`, `ptmx.c`. Owns pseudo-terminal master/slave behavior and `/dev/ptmx` allocation.
- **Termios**: `termios.c`, `inc/tty/termios.h`, `inc/uabi/termios.h`. Owns POSIX terminal attribute state and ioctls.
- **Sessions and job-control bridge**: `session.c`, `inc/tty/session*.h`, process groups/signals. Owns controlling terminal, foreground process group checks, terminal-generated signals.
- **Console, UART, and kernel printing**: root `console.c`, `uart.c`, `printf.c`, `diag.c`. Owns boot/debug I/O and console device paths.

Skill granularity: TTY core, PTY/ptmx, termios, sessions/job control, console/UART debug output.

## 12. IPC And Shared Kernel/User Coordination

Responsibility: System V style message queues, semaphores, shared memory, IPC identifiers, and namespace-like lookup utilities.

Representative directories: `kernel/kernel/ipc`, `kernel/kernel/inc/ipc.h`.

Submodules:

- **IPC object utilities**: `ipc_util.c`, `inc/ipc.h`. Owns IDs, keys, lookup, permission-like shared helpers.
- **Message queues**: `msg.c`. Owns `msgget`, send/receive semantics, queue state, sleeping senders/receivers.
- **Semaphores**: `sem.c`. Owns semaphore arrays and `semop` behavior.
- **Shared memory**: `shm.c`. Owns shared memory object lifecycle and process attachment/detachment hooks into VM/proc.

Skill granularity: IPC utilities, message queues, semaphores, shared memory.

## 13. Debugging, Diagnostics, Symbols, And Crash Support

Responsibility: kernel observability, panic/debug output, backtraces, symbol lookup, GDB remote protocol, coredumps, and build-generated asm offsets.

Representative files: `gdbstub/gdbstub.c`, `ksymbols.c`, `backtrace.c`, `diag.c`, `coredump.c`, `kernel/kernel/inc/README.md`, scripts under `kernel/scripts`.

Submodules:

- **GDB stub**: `gdbstub/gdbstub.c`, `arch/*/gdbstub_arch.c`. Owns remote GDB protocol, register access, breakpoints, debug transport assumptions.
- **Backtrace and symbols**: root `backtrace.c`, `ksymbols.c`, `ksymbols_placeholder.S`, `kernel.sym` generation scripts. Owns stack traces and address-to-symbol support.
- **Diagnostics and printing**: `diag.c`, `printf.c`, `console.c`, `uart.c`. Owns panic/debug output and low-level formatting.
- **Coredump and fatal signal support**: `coredump.c`, ELF headers. Owns process coredump generation on fatal events.
- **Generated asm offsets**: `kernel/kernel/inc/CMakeLists.txt`, `inc/README.md`, `scripts/gen_asm_offsets.py`. Owns C-to-assembly structure offset synchronization.
- **Command line and boot configuration**: `cmdline.c`, `inc/cmdline.h`. Owns parsing of kernel boot arguments such as runtime feature switches.

Skill granularity: GDB stub, backtraces/symbols, diagnostics/printing, coredump, asm offsets, cmdline.

## 14. Generic Kernel Data Structures

Responsibility: reusable containers and indexing structures shared across proc, VFS, MM, devices, RCU, and event subsystems.

Representative root files: `hlist.c`, `bintree.c`, `rbtree.c`, `maple_tree.c`, `xarray.c`.

Representative headers: `inc/list.h`, `inc/list_type.h`, `inc/llist.h`, `inc/hlist.h`, `inc/hlist_type.h`, `inc/bintree.h`, `inc/bintree_type.h`, `inc/rbtree.h`, `inc/maple_tree.h`, `inc/maple_tree_type.h`, `inc/maple_tree_config.h`, `inc/xarray.h`, `inc/xarray_type.h`, `inc/xarray_config.h`.

Submodules:

- **Intrusive doubly linked lists**: `inc/list.h`, `inc/list_type.h`. Owns the simple list-node pattern used for timer queues, ready queues, device lists, and VFS lists.
- **Lockless/singly linked lists**: `inc/llist.h`. Owns lightweight forward-list patterns used where a full doubly linked list is unnecessary.
- **Hash lists**: `hlist.c`, `inc/hlist.h`, `inc/hlist_type.h`. Owns hash-bucket list primitives used by process tables and other indexed registries.
- **Binary tree primitives**: `bintree.c`, `inc/bintree.h`, `inc/bintree_type.h`. Owns simpler ordered-tree operations used where full rb-tree balancing is not required.
- **Red-black trees**: `rbtree.c`, `inc/rbtree.h`. Owns balanced ordered containers used by scheduler/runqueue-style ordered selection and generic sorted maps.
- **Maple tree**: `maple_tree.c`, `inc/maple_tree.h`, `inc/maple_tree_type.h`, `inc/maple_tree_config.h`. Owns range/index mapping patterns suitable for VMAs or other sparse range lookups.
- **XArray**: `xarray.c`, `inc/xarray.h`, `inc/xarray_type.h`, `inc/xarray_config.h`. Owns dense/sparse integer-indexed object lookup with kernel-style allocation and lifetime assumptions.

Skill granularity: list/hlist usage, rb-tree/binary-tree usage, maple tree/range maps, xarray/indexed object maps.

## 15. Core Kernel Utility Tools

Responsibility: non-container helper code used broadly by kernel modules: bit manipulation, string/memory helpers, object lifetime conventions, accounting/resource metrics, compiler/cache helpers, and power hooks.

Representative root files: `bits.c`, `string.c`, `kobject.c`, `accounting.c`, `power.c`.

Representative headers: `inc/bits.h`, `inc/string.h`, `inc/kobject.h`, `inc/accounting.h`, `inc/kstats.h`, `inc/resource.h`, `inc/compiler.h`, `inc/cache.h`, `inc/types.h`, `inc/errno.h`, `inc/defs.h`.

Submodules:

- **Bit operations**: `bits.c`, `inc/bits.h`. Owns bit scans, masks, bitmap-style helpers, and other low-level bit manipulation used by allocators, schedulers, and device code.
- **String and memory primitives**: `string.c`, `inc/string.h`. Owns freestanding libc-like helpers such as copy, compare, clear, length, and bounded string routines.
- **Compiler, cache, and type helpers**: `inc/compiler.h`, `inc/cache.h`, `inc/types.h`, `inc/errno.h`, `inc/defs.h`. Owns attributes, alignment/cache macros, base integer types, kernel-wide declarations, and error conventions.
- **Kernel objects and lifetime**: `kobject.c`, `inc/kobject.h`. Owns reference-counted object conventions and generic object lifetime helpers.
- **Accounting and resource metrics**: `accounting.c`, `inc/accounting.h`, `inc/kstats.h`, `inc/resource.h`. Owns CPU/resource counters, runtime accounting, and user-visible resource-limit style data.
- **Power management hooks**: `power.c`. Owns shutdown, reboot, halt, and future platform power hooks where implemented.

Skill granularity: bit operations, string/memory primitives, kobject/refcount lifetime, accounting/resource metrics, power hooks.

## 16. Boot, Init, And Kernel Assembly Linkage

Responsibility: generic kernel initialization sequence, linker scripts, early subsystems, kernel entry files, and final image symbol embedding.

Representative files: `start_kernel.c`, `start.c`, `entry.S`, `kernel.ld.in`, `kernel_x86_64.ld.in`, top-level `kernel/CMakeLists.txt`, `kernel/kernel/CMakeLists.txt`.

Submodules:

- **Generic init sequence**: `start_kernel.c`. Owns ordered initialization of memory, devices, proc, VFS, network, daemons, and first user/root setup.
- **Generic entry and root-level start files**: root `start.c`, `entry.S`. Owns common entry glue outside arch-specific code.
- **Linker scripts and kernel image layout**: `kernel.ld.in`, `kernel_x86_64.ld.in`, CMake symbol embedding rules. Owns virtual/physical base, sections, symbol index reservation.
- **Kernel build configuration**: top-level `kernel/CMakeLists.txt`, `kernel/kernel/CMakeLists.txt`. Owns ARCH/PLATFORM/OPT_LEVEL, toolchain detection, object-library aggregation.

Skill granularity: init order, linker/image layout, kernel build mechanics.

## Suggested Skill Set From This Hierarchy

A compact but complete skill set would use these names:

- `xv6-kernel-arch-platform`
- `xv6-kernel-traps-syscalls`
- `xv6-kernel-process-scheduler`
- `xv6-kernel-sleep-wakeup`
- `xv6-kernel-timers`
- `xv6-kernel-memory-management`
- `xv6-kernel-locking-rcu`
- `xv6-kernel-device-core`
- `xv6-kernel-block-storage`
- `xv6-kernel-input`
- `xv6-kernel-network-devices`
- `xv6-kernel-vfs-core`
- `xv6-kernel-filesystems`
- `xv6-kernel-event-wait`
- `xv6-kernel-lwip-networking`
- `xv6-kernel-tty-console`
- `xv6-kernel-ipc`
- `xv6-kernel-debugging`
- `xv6-kernel-data-structures`
- `xv6-kernel-utility-tools`
- `xv6-kernel-build-init`

The six existing skills already cover parts of this map:

- `xv6-kernel-freeze-triage`: cross-cutting runtime/debug flow.
- `xv6-kernel-input`: item 7 input devices plus item 9 readiness contract.
- `xv6-kernel-event-wait`: item 9.
- `xv6-kernel-timers-scheduler`: item 4 plus scheduler timeout interactions.
- `xv6-kernel-network-e1000`: item 10 NIC-driver slice.
- `xv6-wayland-kernel-bridge`: user-space boundary for input/event-wait symptoms.

## High-Risk Cross-Module Boundaries

- **Scheduler, timers, and locks**: wakeups, timed sleeps, run queue locks, and RCU quiescent states interact tightly.
- **Event waits, devices, and VFS**: kqueue/epoll depends on file and cdev `.poll` callbacks and must preserve level-triggered readiness.
- **Input, Wayland, and event waits**: kernel rings can be healthy while the compositor blocks before reaching `epoll_wait`.
- **Network drivers, timers, and workqueues**: RX in timer/IRQ context can starve CPU0; defer heavy RX to workqueues.
- **VFS, MM, and block devices**: page cache, buffer heads, block I/O, reclaim, and filesystem locking form a dense dependency path.
- **TTY, sessions, process groups, and signals**: terminal input can generate process-control signals through proc/session state.
- **Architecture traps and generic proc/mm**: page faults, syscalls, signals, and return-to-user paths cross arch/generic boundaries.

## Notes For Future Skill Authoring

- Keep each skill description keyword-rich and tied to symptoms, subsystem names, and file names.
- Use the second-layer submodules as the default granularity. Merge adjacent submodules only when the debug workflow is inseparable.
- Put cross-module workflows, such as freeze triage, in separate skills that route to subsystem skills rather than duplicating every module detail.
- Prefer procedures based on actual repo commands and GDB helpers: `xv6-freeze`, `xv6-input`, `xv6-timers`, `cmake --build build-x86_64 --target kernel`, and generated artifact checks.
