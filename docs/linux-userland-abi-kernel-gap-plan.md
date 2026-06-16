# Linux x86_64 Userland ABI Kernel Gap Plan

This plan is the kernel-side continuation of:

- `docs/linux-abi-compat-plan.md`
- `docs/linux-abi-audit.md`
- `docs/linux-abi-semantic-audit.md`

Scope: x86_64 Linux user/kernel ABI compatibility for Linux-style executables.
RISC-V is intentionally out of scope here.

Current direction: x86_64 user programs are host-glibc Linux executables.
The repo-local xv6 toolchain and musl libc override are no longer part of the
normal x86_64 build, rootfs, or audit path. Legacy/private xv6 interfaces are
not compatibility targets for host-built userland; new work must use Linux
syscall numbers, Linux libc headers, and the staged host dynamic loader.

Current generated audit status:

- `native-ok`: 302
- `struct-risk`: 71
- `unsupported-ok`: 0
- `native-missing`: 0
- `wrong-dispatch`: 0
- `unsupported-bad`: 0

Current semantic audit status:

- `compatible-by-inspection`: 180
- `probed-compatible-core`: 193

Current host-userland build status:

- The x86_64 `user` target builds every `user/programs/*` program with the
  host compiler and host glibc.
- `/bin/sh` is the host-built shell; the old `_sh`/`host-sh` split is gone.
- `scripts/make-rootfs.sh` stages the host glibc loader/runtime and rejects
  musl-linked payloads.
- `scripts/linux_abi_audit.py` no longer consumes or reports musl-xv6 syscall
  mappings.
- The GUI launcher defaults to the Bochs framebuffer path for `QEMU_GPU=auto`.
  Virgl/GTK GL remains available by setting `QEMU_GPU=virtio-gpu-gl-primary`
  explicitly, but it is no longer the default because host EGL/Zink failures
  can produce a black QEMU window while the guest compositor is running.

The important conclusion is that syscall dispatch is no longer the main
blocker. The remaining work is behavioral ABI compatibility: Linux struct
layouts, flags, errno values, process/thread semantics, ELF startup state,
procfs, and socket/event semantics.

## Current Kernel Difference Summary

### Dispatch

Current state:

- x86_64 syscall arguments use Linux register order in
  `kernel/arch/x86_64/irq/syscall.c`: `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`.
- Unmapped Linux numbers return `-ENOSYS`.
- `legacy_xv6_syscall_alias()` is behind `ENABLE_LEGACY_XV6_SYSCALL_ALIAS`.
- The generated audit reports zero `wrong-dispatch` and zero `native-missing`
  entries.

Differences and risks:

- `kernel/kernel/inc/syscall.h` still carries generic aliases with Linux-number
  collisions, for example `SYS_memfd_create_generic = 279`, where Linux x86_64
  279 is `move_pages`. It is not dispatched on x86_64 today, but it remains a
  footgun for future edits.
- The x86 dispatcher still contains two shape-based compatibility shortcuts:
  Linux `munmap(11)` can be rewritten to private `SYS_munmap` if it looks like
  a Linux unmap, and Linux `exit(60)` can be rewritten for threaded teardown.
  These should eventually become unnecessary once private low-number xv6 ABI
  compatibility is fully separated.
- Old private xv6 syscall numbers remain in high ranges. That is acceptable,
  but all new Linux compatibility work must prefer Linux-native numbers.

Fix plan:

1. Keep `scripts/linux_abi_audit.py` as a hard gate: no change may reintroduce
   `wrong-dispatch`, `native-missing`, or `unsupported-bad`.
2. Add a static assertion or generated check that x86 dispatch never installs a
   generic alias whose number is a different Linux x86_64 syscall.
3. Move all generic aliases that collide with x86_64 Linux numbers behind
   non-x86 guards or rename them as non-dispatchable constants.
4. Remove the `munmap(11)` and threaded `exit(60)` shape rewrites after the
   musl-xv6 syscall header and all userland are fully moved to native Linux
   numbers or high private numbers.

### ELF Exec And Initial Process ABI

Current state:

- `kernel/kernel/exec.c` supports `ET_EXEC`, `ET_DYN`, and `PT_INTERP`.
- The aux vector currently includes `AT_PAGESZ`, `AT_PHENT`, `AT_PHNUM`,
  `AT_ENTRY`, `AT_FLAGS`, `AT_PHDR`, `AT_BASE`, `AT_UID`, `AT_EUID`, `AT_GID`,
  `AT_EGID`, `AT_SECURE`, `AT_RANDOM`, `AT_EXECFN`, `AT_PLATFORM`,
  `AT_CLKTCK`, `AT_HWCAP`, `AT_HWCAP2`, and `AT_NULL`.
- The thread group stores `exec_path`, interpreter base, interpreter dynamic
  address, interpreter path, and a bounded exec snapshot for procfs/GDB
  support.
- `/proc/<pid>/cmdline`, `/proc/<pid>/environ`, and `/proc/<pid>/auxv` are
  generated from the exec snapshot using Linux binary/NUL-separated formats.
- `scripts/build-linux-host-probes.sh` builds host glibc static/dynamic startup
  probes plus `host-sh` from `user/programs/sh/sh.c`, and
  `STAGE_HOST_GLIBC=1 scripts/make-rootfs.sh ...` stages
  `/lib64/ld-linux-x86-64.so.2` plus host shared-library dependencies into a
  test rootfs.

Differences vs Linux:

- `AT_HWCAP`/`AT_HWCAP2` are conservative placeholders. Fill them from CPUID
  when applications require specific feature bits.
- `AT_SYSINFO_EHDR` is still absent; keep that policy explicit unless vdso
  support is added.
- Larger desktop applications and libraries are not fully host-glibc yet:
  Mesa/GTK/WebKit/NetSurf clients such as `/bin/mesaglsmoke` and `/bin/netsurf`
  still come from the existing xv6 musl port toolchain and remain the next
  desktop migration layer.
- Host-built static and dynamic glibc startup probes now reach `main()`,
  validate auxv/procfs, and exit cleanly in the VM.
- Static glibc exit/fini is fixed by clearing the x86_64 entry `%rdx`
  `rtld_fini` register for exec startup.
- Host distro dynamic executables using the staged native glibc loader and
  libc have been smoke-tested in the VM: `/usr/bin/true`,
  `/usr/bin/env`, and `/usr/bin/printf` run as staged `host-*` binaries.
- `user/programs/sh/sh.c` has a host-glibc build path staged as `host-sh`;
  it uses the native loader/libc, raw Linux `getdents64`, and `fork()` for
  glibc-safe child execution.
- The core desktop session programs are now host-glibc executables:
  `/bin/desktop`, `/bin/wlcomp`, and `/bin/glsmoke` request the staged native
  `/lib64/ld-linux-x86-64.so.2`. A fresh KVM/network VM boot on `/tmp/xv6.img`
  reached `wlcomp: entering main loop`; running host-glibc `/bin/glsmoke` with
  `XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0` mapped a Wayland toplevel
  and reported `EGL 1.4, GL OpenGL ES 2.0 xv6-compat`.
- A fresh `/tmp/xv6-linux-abi.img` boot with KVM and networking reaches
  `wlcomp: entering main loop` on the framebuffer path. QEMU `screendump`
  produced a 1280x800 image with 1159 colors, proving the desktop is painted
  rather than stuck on a black framebuffer.

Fix plan:

1. Done: extend `exec()` stack construction to copy:
   - 16 random bytes for `AT_RANDOM`.
   - the executed path string for `AT_EXECFN`.
   - an x86_64 platform string such as `x86_64` for `AT_PLATFORM`.
2. Done: add auxv entries:
   - `AT_RANDOM`: pointer to the 16 random bytes.
   - `AT_EXECFN`: pointer to the copied exec path.
   - `AT_PLATFORM`: pointer to the copied platform string.
   - `AT_CLKTCK`: scheduler clock tick value.
   - `AT_HWCAP` and `AT_HWCAP2`: initially conservative values, then fill from
     CPUID if libc or applications need feature bits.
3. Done: store an exec snapshot in `thread_group`: argv bytes, env bytes,
   and auxv pairs, with bounded lifetime and pid-lock-protected replacement.
4. Done: implement `/proc/<pid>/auxv`, `/proc/<pid>/cmdline`, and
   `/proc/<pid>/environ` from that snapshot using Linux NUL-separated formats.
5. Done: add host-built static and dynamic glibc probes that print auxv/procfs
   state when the required loader files are staged.
6. Done: debug and fix static glibc exit/fini after successful startup probe.

### VFS, FD, And Path ABI

Struct-risk syscalls:

`open(2)`, `stat(4)`, `fstat(5)`, `lstat(6)`, `poll(7)`, `ioctl(16)`,
`readv(19)`, `writev(20)`, `pipe(22)`, `fcntl(72)`, `getdents(78)`,
`statfs(137)`, `fstatfs(138)`, `getdents64(217)`, `openat(257)`,
`newfstatat(262)`, `pselect6(270)`, `ppoll(271)`, `pipe2(293)`,
`preadv(295)`, `pwritev(296)`, `preadv2(327)`, `pwritev2(328)`,
`statx(332)`.

Observed differences and risks:

- `open`/`openat` do not yet cover the full Linux flag matrix:
  `O_PATH`, `O_TMPFILE`, `O_DIRECTORY`, `O_NOFOLLOW`, `O_NOATIME`,
  `O_DIRECT`, creation mode handling, trailing slash behavior, and exact errno
  differences.
- `openat` currently creates with a fixed `0644` instead of honoring the mode
  argument and umask in the same way as `open`.
- `fstatat` copies the kernel `struct stat` directly. This is only safe if the
  kernel struct is locked to Linux x86_64 layout. The comment still references
  a riscv64 layout, which is a documentation and audit smell.
- `statx` fills basic fields but ignores much of `mask`, sync flags, btime,
  mnt id, DIO alignment, and attributes.
- `fchmodat` and `utimensat` have explicit comments/behavior gaps around
  `AT_SYMLINK_NOFOLLOW`.
- `fcntl` implements common commands, locks, and memfd seals, but deeper async
  notification behavior must still be audited.
- `poll`, `ppoll`, and `pselect6` are wired but signal mask restore and race
  behavior need Linux-level tests.
- `pipe`/`pipe2` need Linux capacity, atomicity, `O_NONBLOCK`, `O_CLOEXEC`, and
  `POLL*` behavior coverage.
- `getdents`/`getdents64` pass core tests but large directories, short buffers,
  d_type, and offset semantics need a wider matrix.
- The following common VFS syscalls were added and covered by raw or host
  probes: `select`, `eventfd`, `truncate`, `fchdir`, `rmdir`, `creat`,
  `chmod`, `chown`, `lchown`, `utime`, `utimes`, `futimesat`, `openat2`,
  `close_range`, `copy_file_range`, `fchmodat2`, `inotify_init`,
  `inotify_init1`, `inotify_add_watch`, `inotify_rm_watch`, and
  xattr-family clean `-ENOTSUP` stubs. Cache/writeback hint syscalls
  `readahead`, `sync_file_range`, and `syncfs` are implemented as validated
  no-op/minimal-success models and probed.
- `O_PATH`, `O_DIRECTORY`, and `O_TMPFILE` unsupported-filesystem fallback are
  implemented and probed. `O_NOFOLLOW`, `O_NOATIME`, `O_DIRECT`, creation,
  permission, trailing-slash, and full extended metadata matrices remain open.

Fix plan:

1. Create a Linux UAPI header subset under `kernel/kernel/inc/uabi/` for
   x86_64 `stat`, `statfs`, `statx`, `open_how`, `dirent`, `dirent64`,
   `pollfd`, and `iovec`; add `_Static_assert` size/offset checks where
   possible.
2. Split VFS syscall copyout into explicit Linux ABI structs instead of
   copying internal structs directly.
3. Implement and test `open`/`openat` flag semantics:
   - Done for the probed subset: `O_DIRECTORY` fails non-directories with
     `-ENOTDIR`.
   - `O_NOFOLLOW`: fail final symlink with `-ELOOP`.
   - Done for the probed subset: `O_PATH` allocates a path-only fd with
     `fstat`/`fcntl`/close support and `-EBADF` for read/write style I/O.
   - Done for the fallback subset: `O_TMPFILE` validates the directory target
     and write access mode, then returns `-EOPNOTSUPP` for filesystems without
     anonymous tmpfile support.
   - `O_CLOEXEC` and `O_NONBLOCK`: apply atomically.
   - `mode`: honor mode argument with umask for all create paths.
4. Done: add `openat2(437)` with `struct open_how`; initially support the flags that
   map to `openat`, reject unsupported `RESOLVE_*` bits with Linux errno.
5. Done: add `close_range(436)` for libc/runtime cleanup.
6. Done for the listed core subset: implement common metadata syscalls:
   `truncate`, `creat`, `chmod`, `chown`, `lchown`, `fchdir`, `rmdir`,
   `utime`, `utimes`, `futimesat`, `fchmodat2`.
7. Partly done: implement xattrs as a minimal filesystem-neutral namespace:
   `getxattr`, `lgetxattr`, `fgetxattr`, `setxattr`, `lsetxattr`,
   `fsetxattr`, `listxattr`, `llistxattr`, `flistxattr`, `removexattr`,
   `lremovexattr`, `fremovexattr`. If persistence is too large at first,
   implement Linux-compatible `-ENODATA`, `-ENOTSUP`, and size-probe behavior
   with tests.
8. Done: add `copy_file_range(326)` using VFS read/write fallback first, with correct
   offset pointer handling.
9. Done for the new subset: expand `linuxsyscallabitest` plus host-built
   probes for VFS startup/runtime paths, including inotify smoke coverage.
   Broader flag and struct matrices remain.
10. Done for the hint subset: implement `readahead(187)`,
    `sync_file_range(277)`, and `syncfs(306)` with Linux-style fd/range/flag
    validation and minimal success semantics.
11. Done for fixed-size anonymous pipes: implement and probe `fcntl`
    `F_GETPIPE_SZ` plus conservative `F_SETPIPE_SZ` compatibility.
12. Done for stored async-I/O metadata: implement and probe `fcntl`
    `F_SETOWN`/`F_GETOWN`, `F_SETSIG`/`F_GETSIG`, and owner-ex variants. Actual
    `SIGIO` generation remains a later event-delivery task.
13. Done for byte-range locking: implement and probe `F_OFD_GETLK`,
    `F_OFD_SETLK`, and `F_OFD_SETLKW` on top of per-open-file-description lock
    ownership.
14. Done for lease/dnotify probe compatibility: implement `F_GETLEASE`,
    conservative `F_SETLEASE`, and `F_NOTIFY` clear/validation behavior.
15. Done: remove the stale `cmd == 14` `F_DUPFD_CLOEXEC` alias for x86_64 Linux
    callers; probe Linux's real `F_DUPFD_CLOEXEC` value (`1030`) and `cmd 14`
    `-EINVAL`.
16. Done for core scatter/gather I/O: add raw and host-built probes for
    `readv(19)` and `writev(20)`.
17. Done for core file truncation: fix ext4 cache teardown on shrink and add
    raw plus host-built `ftruncate(77)` probes, including the truncate-after-
    cached-read sequence used by glibc-host probes.
18. Done for the next host file/path matrix slice: add raw and host-built
    probes for `pwrite64(18)`, `access(21)`, `fsync(74)`, `fdatasync(75)`,
    and basic `chdir`/`mkdir`/`rmdir`/`link`/`rename`/`symlink`/`readlink`/
    `unlink` paths. This fixed old `rmdir(84)` argument adaptation, symlink
    target preservation, and Linux-style exact-buffer `readlink` truncation.
19. Done for the next open-flag slice: add raw and host-built probes for
    normal `O_NOFOLLOW` symlink `-ELOOP`, `O_PATH|O_NOFOLLOW` opening the
    symlink itself, `O_NOATIME`/`O_DIRECT` acceptance, creation mode, and
    non-directory trailing-slash errors.
20. Done for the first socket flag slice: validate unknown `accept4(288)` flag
    bits with `-EINVAL` after fd lookup, and add raw plus host-built AF_UNIX
    probes proving atomic `SOCK_NONBLOCK` and `SOCK_CLOEXEC` application on
    accepted fds.
21. Done for `fstatfs(138)` core coverage: add raw and host-built probes for
    Linux-sized statfs copyout fields plus `-EBADF` on an invalid fd.
22. Done for the first TTY ioctl ABI slice: convert `TCGETS`/`TCSETS*`
    copyin/copyout through Linux x86_64's 36-byte kernel `struct termios`
    instead of the kernel's internal/glibc-shaped `struct termios`. This fixes
    host glibc `tcgetattr()` stack-canary corruption and is covered by
    dynamic/static stack-guard probes plus staged host `env`/`printf`.

### Procfs And Sysfs Expectations

Current state:

- procfs exposes many Linux-looking files: `/proc/self`, `/proc/meminfo`,
  `/proc/cpuinfo`, `/proc/<pid>/status`, `maps`, `statm`, `stat`, `cmdline`,
  `comm`, `mountinfo`, `mounts`, `limits`, `smaps`, `fd`, and `fdinfo`.
- `/proc/<pid>/auxv`, `/proc/<pid>/cmdline`, and `/proc/<pid>/environ` are
  generated from the exec snapshot in Linux binary/NUL-separated formats.
- `/proc/<pid>/exe` is a symlink to the stored exec path.

Differences vs Linux:

- `/proc/<pid>/status`, `stat`, `statm`, `maps`, `smaps`, `fdinfo`, and
  `mountinfo` are approximations. They need targeted compatibility against
  glibc, GLib, GTK, WebKit, libdrm/Mesa, Python, and shell/coreutils probes.
- sysfs coverage is not represented in the syscall inventory but matters for
  GPU, DRM, Mesa, udev-like discovery, and desktop apps.

Fix plan:

1. Done: use the exec snapshot described above for `cmdline`, `environ`, and
   `auxv`.
2. Add procfs format tests that compare field counts and parseability against
   Linux expectations, not just file existence.
3. Fill process state fields needed by common libraries: thread counts, UIDs,
   GIDs, signal masks, capability zeros, CPU masks, and memory fields.
4. Expand `/proc/self/fd` and `/proc/self/fdinfo` to match Linux symlink
   targets and octal flag formatting closely.
5. Add a minimal sysfs compatibility plan for `/sys/devices`, `/sys/class`,
   `/sys/bus/pci`, `/sys/class/drm`, and `/sys/dev/char` as needed by Mesa and
   desktop probes.

### Process, Thread, Credential, And Resource ABI

Struct-risk syscalls:

`clone(56)`, `wait4(61)`, `getrlimit(97)`, `getrusage(98)`, `sysinfo(99)`,
`getgroups(115)`, `setgroups(116)`, `getresuid(118)`, `getresgid(120)`,
`arch_prctl(158)`, `setrlimit(160)`, `clone3(435)`.

Recently implemented runtime syscalls:

`fork(57)`, `times(100)`, `getpgrp(111)`, `setfsuid(122)`, `setfsgid(123)`,
`capget(125)`, `capset(126)`, `waitid(247)`, `getcpu(309)`, plus legacy
`eventfd(284)` and scheduler parameter stubs. `rseq(334)` is wired but
intentionally fails closed with `-ENOSYS` until the scheduler-side Linux
contract is implemented.

Observed differences and risks:

- `clone` accepts many Linux flags but validation and exact combinations are
  partial. Namespace, pidfd, cgroup, and some thread-group edge semantics are
  not complete.
- `clone3` is currently returned as clean `-ENOSYS` so glibc falls back to the
  working `clone` ABI. The previous partial implementation returned `EINVAL`
  for pthread startup and blocked glibc fallback.
- `fork` is implemented as `clone(SIGCHLD)` without `CLONE_VM`.
- `wait4` routes to waitpid-compatible behavior; `rusage` and options need a
  Linux matrix.
- `waitid` has a basic `P_ALL`/`P_PID`/`P_PGID`, `WEXITED`, `WNOHANG`, and
  `WNOWAIT` model. Full stopped/continued and rusage parity remain.
- `arch_prctl` supports FS but rejects GS. That is probably fine for userland,
  but exact errno and FS/GS read/write behavior should be tested.
- `rseq(334)` intentionally returns `-ENOSYS` for now. The previous
  registration-only model was unsafe because Linux success also promises
  scheduler updates to the user rseq area and critical-section aborts on
  migration/preemption.
- `getcpu(309)` reports CPU id and NUMA node zero.
- Credentials and capabilities are minimal. `capget`/`capset` provide a
  harmless Linux-compatible empty capability model.
- `setfsuid`/`setfsgid` store thread-group credential fields.

Fix plan:

1. Done: implement `fork(57)` as `clone(SIGCHLD)` without `CLONE_VM`, using the
   existing process clone path.
2. Expand clone validation:
   - reject impossible flag combinations with Linux errno;
   - support `CLONE_PARENT_SETTID`, `CLONE_CHILD_SETTID`,
     `CLONE_CHILD_CLEARTID`, `CLONE_SETTLS`, and `CLONE_THREAD` exactly;
   - explicitly reject unsupported namespace/cgroup/pidfd bits until
     implemented.
3. Expand `clone3` after the fallback-compatible `-ENOSYS` policy:
   - validate `size` like Linux;
   - reject unknown nonzero tail fields;
   - implement `pidfd` or reject `CLONE_PIDFD` consistently;
   - validate `set_tid`/`set_tid_size`;
   - document and test every unsupported flag.
4. Done for the basic model: implement `waitid(247)` on top of wait internals with Linux `siginfo_t`
   output and `WNOWAIT` policy.
5. Fill `wait4` `rusage` when requested.
6. Done for `times(100)`; still improve `getrusage(98)` from scheduler
   accounting.
7. Done: implement `getpgrp(111)` as `getpgid(0)`.
8. Done: implement `getcpu(309)` with CPU id and optional node zero.
9. Reopened: keep `rseq(334)` fail-closed with `-ENOSYS` until xv6 implements
   Linux's full scheduler contract. A registration-only stub is not compatible
   enough for Chromium-class runtimes.
10. Done: implement `capget`/`capset` as an all-zero capability model with correct
    version handling and `-EPERM` for unsupported raises.
11. Done: implement `setfsuid`/`setfsgid` fields in thread group credentials.
12. Done: add host glibc startup/runtime probes for fork/wait, pthreads, TLS,
    rseq-adjacent startup, file APIs, and capabilities.

### Memory ABI

Struct-risk syscalls:

`mmap(9)`, `mprotect(10)`, `munmap(11)`, `mremap(25)`, `msync(26)`,
`mincore(27)`, `madvise(28)`.

Additional common unsupported memory syscalls:

`remap_file_pages(216)`, `mbind(237)`, `set_mempolicy(238)`,
`get_mempolicy(239)`, `migrate_pages(256)`, `move_pages(279)`,
`process_vm_readv(310)`, `process_vm_writev(311)`, `process_madvise(440)`,
`pkey_mprotect(329)`, `pkey_alloc(330)`, `pkey_free(331)`,
`memfd_secret(447)`, `process_mrelease(448)`, `cachestat(451)`,
`map_shadow_stack(453)`.

Observed differences and risks:

- The VM layer has substantial Linux-like support for fixed mappings,
  `MAP_FIXED_NOREPLACE`, `mremap`, `madvise`, and file-backed mappings, but the
  full flag and errno matrix is not verified.
- `madvise` implements or no-ops many advice values. Some Linux-visible
  behavior is intentionally approximate.
- NUMA, pkeys, process memory access, and shadow stack syscalls are not
  implemented.

Fix plan:

1. Build a memory ABI matrix test covering:
   `MAP_PRIVATE`, `MAP_SHARED`, `MAP_ANONYMOUS`, file mappings,
   `MAP_FIXED`, `MAP_FIXED_NOREPLACE`, unaligned addresses, zero lengths,
   overflow, permission faults, and COW behavior.
2. Lock exact errno behavior for `mmap`, `munmap`, `mprotect`, `mremap`,
   `msync`, `mincore`, and `madvise`.
3. Done: implement `readahead` as a validated no-op cache hint.
4. Implement `process_vm_readv`/`process_vm_writev` only after process memory
   permission and lifetime rules are ready; otherwise keep clean `-ENOSYS`.
5. Implement pkey syscalls as either a Linux-compatible disabled model
   (`-EINVAL`/`-ENOSPC` as appropriate) or real per-VMA pkey bits if needed.
6. Defer NUMA and shadow-stack syscalls unless a target program requires them.

### Signal ABI

Struct-risk syscalls:

`rt_sigaction(13)`, `rt_sigpending(127)`, `rt_sigtimedwait(128)`,
`rt_sigqueueinfo(129)`, `rt_sigsuspend(130)`, `sigaltstack(131)`.

Common unsupported signal/event-related syscalls:

`rt_tgsigqueueinfo(297)`, `pidfd_send_signal(424)`.

Observed differences and risks:

- `rt_sigaction` checks `sigsetsize` but delegates to the internal sigaction
  layout. Verify layout exactly matches Linux x86_64 userspace expectations.
- `rt_sigtimedwait` validates zero timeout, but nonzero timeouts fall through
  to blocking `sigwait` behavior. That is not Linux-compatible.
- `rt_sigqueueinfo` copies user siginfo but signal payload fidelity and
  permission semantics are partial.
- `sigaltstack` exists, but delivery on the alternate stack and all flag/error
  cases need tests.
- `signalfd`/`signalfd4` have Linux-number creation, fd-update, CLOEXEC,
  NONBLOCK, poll-empty, and empty-read smoke coverage. Queued signal delivery
  and `signalfd_siginfo` copyout remain open.

Fix plan:

1. Define explicit Linux x86_64 `sigaction`, `sigset_t`, `siginfo_t`,
   `ucontext`, and signal-frame structs with size/offset checks.
2. Audit signal delivery frame and `rt_sigreturn(15)` against Linux x86_64
   expectations.
3. Implement correct `rt_sigtimedwait` nonzero timeout behavior.
4. Expand queued signal payload support for `rt_sigqueueinfo` and
   `rt_tgsigqueueinfo`.
5. Finish `sigaltstack`: `SS_DISABLE`, `SS_ONSTACK`, minimum stack size, and
   actual alternate-stack delivery.
6. Done for smoke coverage: implement `signalfd4(289)` first, then alias old
   `signalfd(282)`. Finish queued delivery and `signalfd_siginfo` copyout.
7. Add signal torture tests using host-built binaries and raw syscall probes.

### Futex, Robust List, And Thread Runtime ABI

Current state:

- `futex(202)` is wired.
- `set_tid_address(218)`, `set_robust_list(273)`, and
  `get_robust_list(274)` are wired.
- Futex supports wait, wake, bitset, requeue, cmp requeue, wake op, and robust
  list cleanup.

Differences and risks:

- `futex_waitv(449)`, `futex_wake(454)`, `futex_wait(455)`, and
  `futex_requeue(456)` are newer syscalls and unsupported.
- PI futexes and some advanced operations are not implemented.
- Shared futex keying and file-backed mappings need stress tests.
- `rseq` is wired but intentionally fail-closed with `-ENOSYS`. Full
  restartable-sequence support remains deferred until scheduler migration and
  abort semantics are implemented.

Fix plan:

1. Done for smoke coverage: add pthread tests using host-built glibc static
   and dynamic binaries. Add stress coverage next.
2. Verify shared futexes across `MAP_SHARED` mappings and after fork.
3. Implement modern futex syscalls as wrappers around existing futex internals
   where possible.
4. Defer PI futexes until a real target requires them; return Linux-compatible
   errors for unsupported PI operations.

### Socket And Network ABI

Struct-risk syscalls:

`connect(42)`, `accept(43)`, `sendto(44)`, `recvfrom(45)`, `sendmsg(46)`,
`recvmsg(47)`, `getsockname(51)`, `getpeername(52)`, `setsockopt(54)`,
`getsockopt(55)`, `accept4(288)`, `sendmmsg(307)`.

Additional socket syscall:

`recvmmsg(299)` is wired when `USE_LWIP` is enabled but semantic coverage
still needs expansion.

Observed differences and risks:

- Socket syscalls are backed by lwIP and AF_UNIX compatibility code. Common
  cases work, but Linux socket option coverage is partial.
- `setsockopt` currently accepts some unknown `SOL_SOCKET` options silently;
  Linux often returns `-ENOPROTOOPT`.
- `sendmsg`/`recvmsg` implement SCM_RIGHTS for AF_UNIX, but ancillary data,
  truncation, name handling, iovec limits, and flags need a full matrix.
- Nonblocking connect/accept/read/write behavior needs tests against Linux
  errno timing: `EINPROGRESS`, `EALREADY`, `EAGAIN`, `EWOULDBLOCK`,
  `ECONNRESET`, and `EPIPE`.
- IPv6 and advanced socket families/options are not generally complete.

Fix plan:

1. Build socket ABI tests for AF_UNIX stream/datagram, TCP, UDP, blocking and
   nonblocking paths.
2. Audit all `SOL_SOCKET`, `IPPROTO_TCP`, `IPPROTO_IP`, and timeout options
   used by libc, curl, OpenSSL, GTK/WebKit, and Python.
3. Replace silent acceptance of unsupported socket options with Linux-compatible
   success only where Linux accepts/ignores them; otherwise return
   `-ENOPROTOOPT`.
4. Expand `sendmsg`/`recvmsg` tests for SCM_RIGHTS, `MSG_CMSG_CLOEXEC`,
   `MSG_TRUNC`, `MSG_CTRUNC`, `MSG_PEEK`, `MSG_DONTWAIT`, and short control
   buffers.
5. Done for the probed AF_UNIX subset: validate `accept4` flags and atomically
   apply `SOCK_NONBLOCK`/`SOCK_CLOEXEC` to accepted fds. Broader blocking and
   sockaddr matrices remain under the socket test expansion.
6. Finish `sendmmsg`/`recvmmsg` partial-success and timeout behavior.

### Event, Timer, And Multiplexing ABI

Struct-risk syscalls:

`poll(7)`, `pselect6(270)`, `ppoll(271)`, `epoll_wait(232)`,
`epoll_ctl(233)`, `epoll_pwait(281)`, `timerfd_settime(286)`,
`timerfd_gettime(287)`, `epoll_pwait2(441)`.

Common unsupported syscalls:

`timer_create(222)`, `timer_settime(223)`,
`timer_gettime(224)`, `timer_getoverrun(225)`, `timer_delete(226)`,
and POSIX timer teardown/query calls.

Observed differences and risks:

- epoll is implemented over kqueue. Basic readiness works, but edge-trigger,
  one-shot, nested epoll, close lifetime, and event mask semantics need a Linux
  matrix.
- `eventfd2(290)` and old `eventfd(284)` are implemented and covered.
- `timerfd_create(283)` is implemented, but the audit lists set/get as
  struct-risk. Absolute timers, cancel-on-set, clock support, and read
  semantics need tests.
- `select(23)` is implemented on top of the poll backend and covered.
- POSIX timers are unsupported.

Fix plan:

1. Done: implement `select(23)` using the poll backend.
2. Done: implement `eventfd(284)` as `eventfd2(initval, 0)`.
3. Expand epoll tests for:
   `EPOLLIN`, `EPOLLOUT`, `EPOLLERR`, `EPOLLHUP`, `EPOLLRDHUP`,
   `EPOLLET`, `EPOLLONESHOT`, `EPOLL_CTL_ADD`, `MOD`, `DEL`, close behavior,
   and duplicate fd behavior.
4. Done for the core zero-timeout ABI: `pselect6` and `ppoll` now enforce
   Linux's exact x86_64 sigset-size requirement, with raw and host-built
   probes for valid, short, and oversized mask sizes. Full signal race tests
   remain.
5. Finish `epoll_pwait` and `epoll_pwait2` signal mask and timespec behavior.
6. Implement POSIX timers only after timerfd/select/epoll are solid.

### Time, Scheduler, And Resource ABI

Common unsupported syscalls:

`adjtimex(159)`, `settimeofday(164)`, `clock_adjtime(305)`.

Observed differences and risks:

- Basic clock and scheduler syscalls are wired, but many return minimal data.
- `sched_getparam`/`sched_setparam` and `sched_getattr`/`sched_setattr` expose
  a minimal Linux `SCHED_OTHER` policy model and are covered by raw plus
  host-built probes.
- `ioprio_get`/`ioprio_set` expose a minimal validated no-op block I/O priority
  model and are covered by raw plus host-built probes.
- Linux programs expect unsupported scheduler features to return stable Linux
  errors, not silently succeed with misleading state.
- Resource limits are partial; enforcement and `/proc/<pid>/limits` must agree.

Fix plan:

1. Define policy: xv6 has a simple scheduler, so most advanced scheduler calls
   should be validated no-ops or return stable Linux errors.
2. Done: implement `sched_getparam` and `sched_setparam` minimally for current
   policy.
3. Done: implement `sched_getattr`/`sched_setattr` enough for probes.
4. Done: implement `times(100)` from existing scheduler accounting.
5. Expand resource limit storage/enforcement and sync it with
   `getrlimit`, `setrlimit`, `prlimit64`, and `/proc/<pid>/limits`.

### IPC ABI

Current state:

- SysV IPC numbers are wired: semaphores, shm, and msg syscalls are native-ok
  by dispatch inventory.

Unsupported common IPC:

`mq_open(240)`, `mq_unlink(241)`, `mq_timedsend(242)`,
`mq_timedreceive(243)`, `mq_notify(244)`, `mq_getsetattr(245)`.

Fix plan:

1. Add semantic tests for existing SysV IPC layout and error behavior.
2. Implement POSIX message queues if a target runtime requires them.
3. Otherwise keep POSIX mq syscalls cleanly `-ENOSYS` until the core ABI is
   complete.

## Complete Unsupported Syscall List

The authoritative unsupported inventory is now the generated
`docs/linux-abi-audit.md` table, which reports `unsupported-ok: 121` and no
wrong dispatch. Do not maintain a handwritten syscall list here: it becomes
stale quickly as compatibility slices land. Use the generated CSV/Markdown for
the exact current syscall set, then update this plan's priority sections with
the behavioral gaps that remain.

## Priority Implementation Order

### Priority 0: Keep Dispatch Correct

1. Regenerate `docs/linux-abi-audit.*`.
2. Confirm zero `wrong-dispatch`, zero `native-missing`, zero
   `unsupported-bad`.
3. Add raw syscall tests for any newly wired Linux number.

### Priority 1: Host Compiler Hello-World And Dynamic Loader

Goal: host-built static and dynamic C programs start, print, allocate memory,
and exit.

Steps:

1. Add host-built probes outside the xv6 toolchain.
2. Fix `exec` auxv: `AT_RANDOM`, `AT_EXECFN`, `AT_PLATFORM`, `AT_CLKTCK`,
   `AT_HWCAP`, `AT_HWCAP2`.
3. Implement real `/proc/self/auxv`, `/proc/self/cmdline`,
   `/proc/self/environ`.
4. Stage glibc dynamic loader and libc in a test rootfs.
5. Implement or validate startup syscalls:
   `brk`, `mmap`, `mprotect`, `munmap`, `arch_prctl`, `set_tid_address`,
   `set_robust_list`, `getrandom`, `prlimit64`, `readlinkat`; keep `rseq`
   fail-closed until full support exists.

Status: done for the startup probe surface. Static and dynamic host glibc
startup probes pass in the VM.

### Priority 2: Process And Thread Runtime

Goal: libc, pthreads, shell-like programs, and common runtimes work.

Steps:

1. Implement `fork`, `waitid`, `times`, `getpgrp`, `getcpu`, `capget`,
   `capset`, `setfsuid`, `setfsgid`; keep `rseq` as a full-support item, not
   a registration-only stub.
2. Complete `clone` and `clone3` validation and supported semantics.
3. Stress futex, robust list, clear child tid, TLS, and thread exit.
4. Expand resource limits and `/proc/<pid>/limits`.

Status: item 1 is done and covered by raw plus host glibc runtime probes.
`clone` supports glibc pthread startup; `clone3` intentionally returns
`-ENOSYS` until full semantics are implemented.

### Priority 3: VFS And Procfs Compatibility

Goal: shell/coreutils/Python/GTK-style file probing works without xv6-specific
libc masking.

Steps:

1. Complete Linux open/stat/path structs and flags.
2. Implement `openat2`, `close_range`, `copy_file_range`, metadata syscalls,
   xattrs, `inotify_init1`, `inotify_add_watch`, `inotify_rm_watch`.
3. Expand procfs and minimal sysfs for library probes.
4. Run host-built file API test matrix.

Status: `openat2`, `close_range`, `copy_file_range`, the listed core metadata
syscalls, inotify smoke support, `O_PATH`/`O_DIRECTORY`/`O_TMPFILE` fallback,
cache/writeback hint syscalls, and xattr `-ENOTSUP` stubs are implemented and
probed. The broader open/stat/path matrix remains.

### Priority 4: Signals, Events, And Timers

Goal: event loops and signal-heavy runtimes work.

Steps:

1. Complete signal frame and `sigaltstack`.
2. Implement `signalfd4`, `eventfd`, `select`, and full pselect/ppoll mask
   semantics.
3. Expand epoll and timerfd behavior.
4. Add POSIX timers only if required by real programs.

Status: `signalfd4`/`signalfd`, `eventfd`, and `select` have smoke coverage in
raw and host-built probes. Full signal delivery through signalfd and full
pselect/ppoll/epoll/timerfd matrices remain.

### Priority 5: Sockets And Desktop/Network Apps

Goal: curl/OpenSSL/GTK/WebKit/Python network code behaves like Linux.

Steps:

1. Complete socket option errno behavior.
2. Finish nonblocking connect/accept/send/recv tests.
3. Complete `sendmsg`, `recvmsg`, `sendmmsg`, `recvmmsg` semantics.
4. Validate DNS, TLS, WebKit, and repeated GUI app launch/close cycles.

### Priority 6: Optional And Rare Syscalls

Implement these last unless a target application proves they are needed:

- Obsolete/dead: `uselib`, `ustat`, `_sysctl`, old epoll syscalls,
  `get_kernel_syms`, `query_module`, `nfsservctl`, `getpmsg`, `putpmsg`,
  `afs_syscall`, `tuxcall`, `security`, `vserver`.
- Kernel/admin/security: `ptrace`, `syslog`, `vhangup`, `modify_ldt`,
  `pivot_root`, `adjtimex`, `acct`, `settimeofday`, swap, hostname/domain,
  iopl/ioperm, module syscalls, kexec, seccomp, bpf, perf, Landlock, LSM.
- Specialized subsystems: NUMA policy/migration, POSIX mq, keyrings, fanotify,
  file handles, namespaces and new mount API, pidfd advanced calls, AIO,
  io_uring, userfaultfd, pkeys, memfd_secret, shadow stack, statmount/listmount.

## Test Plan

For every phase:

1. Regenerate:
   `python3 scripts/linux_abi_audit.py`
   `python3 scripts/linux_abi_semantic_audit.py`
2. Build:
   `cmake --build build-x86_64 --target kernel user -j2`
   `cmake --build build-x86_64 --target kernel-sparse -j2`
3. Run raw syscall tests:
   `linuxsyscallabitest`
4. Build host probes with the host compiler:
   - static glibc if available;
   - dynamic glibc with staged `/lib64/ld-linux-x86-64.so.2`;
   - raw-syscall no-libc probes.
5. Refresh rootfs:
   `scripts/make-rootfs.sh build-x86_64/sysroot /tmp/xv6-linux-abi.img 1536`
6. Boot:
   `FSIMG=/tmp/xv6-linux-abi.img USE_KVM=1 QEMU_NET=1 bash scripts/launch-gui.sh`
7. Validate:
   - host-built hello world;
   - dynamic loader hello world;
   - pthread/futex probe;
   - fork/exec/wait probe;
   - file/path/stat matrix;
   - signal matrix;
   - epoll/timerfd/eventfd matrix;
   - socket matrix;
   - Python, curl/OpenSSL, GTK/WebKit smoke when available.

## Completion Criteria

The kernel can be called "complete userland ABI support" only when:

1. The generated dispatch audit has zero `wrong-dispatch`, zero
   `native-missing`, and zero `unsupported-bad`.
2. Every syscall used by a baseline host-built glibc dynamic executable either
   behaves compatibly or returns a Linux-compatible errno that glibc tolerates.
3. Host-built static and dynamic probes pass without musl-xv6 headers.
4. Common runtime stacks pass: pthreads, fork/exec/wait, signals, file/path,
   procfs, epoll/timerfd/eventfd, sockets, DNS/TLS.
5. Unsupported rare syscalls are documented and intentionally deferred, with
   clean `-ENOSYS` or Linux-compatible stubs.
6. The VM boots a fresh rootfs and runs the ABI tests without noisy
   unsupported-syscall logs during normal startup.
