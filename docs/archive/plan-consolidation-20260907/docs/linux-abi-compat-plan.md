# Linux x86_64 ABI Compatibility Plan

Current x86_64 direction: host-built Linux userland is the only normal
compatibility target. The x86_64 build uses the host compiler, host glibc
headers, and staged `/lib64/ld-linux-x86-64.so.2`; repo-local musl/toolchain
syscall overrides and private xv6 user ABI compatibility are legacy-only and
out of the default path.

## Goal

Make x86_64 xv6 compatible with Linux-style executables by treating the
Linux x86_64 ABI as the primary user/kernel contract. Private xv6 syscall
compatibility must not collide with native Linux syscall numbers or silently
change the meaning of a Linux executable's raw syscall.

Target x86_64 first. RISC-V is intentionally out of scope for this plan.

Compatibility means:

- Linux x86_64 syscall numbers.
- Linux x86_64 syscall argument convention: `rdi`, `rsi`, `rdx`, `r10`,
  `r8`, `r9`.
- Linux return convention: negative errno in `rax`.
- Linux-compatible behavior for syscall structs, flags, errno values,
  fd semantics, signal semantics, process/thread semantics, memory mapping,
  procfs behavior, and unsupported syscall handling.
- Correct behavior for programs that make raw Linux syscalls directly, not
  only programs built against `musl-xv6`.

## Current High-Risk Findings

The current x86_64 dispatcher has several ABI hazards that should be fixed
before deeper feature work.

### Legacy Alias Misdispatch

`kernel/arch/x86_64/irq/syscall.c` has `legacy_xv6_syscall_alias()`. If a
Linux-native syscall number is not directly installed in `syscalls[]`, it can
fall through to an unrelated old xv6 syscall.

Examples of dangerous misdispatch:

- Linux `getuid(102)` can route to old `SYS_listen`.
- Linux `getgid(104)` can route to old `SYS_sconnect`.
- Linux `setuid(105)` can route to old `SYS_sendto`.
- Linux `setpgid(109)` can route to old `SYS_shutdown`.
- Linux `getppid(110)` can route to old `SYS_getpeername`.
- Linux `setsid(112)` can route to old `SYS_sendmsg`.
- Linux `getgroups(115)` can route to old `SYS_sendfile`.
- Linux `getpgid(121)` can route to old `SYS_writev`.
- Linux `getsid(124)` can route to old `SYS_clock_gettime`.
- Linux `statfs(137)` can route to old `SYS_faccessat`.
- Linux scheduler syscalls `144..148` can route to uid/group syscalls.

Wrong syscall dispatch is worse than `-ENOSYS`; it creates silent behavioral
corruption. Fix this first.

### Direct Number Collision

`SYS_memfd_create_generic` is currently defined as `279`. On Linux x86_64,
syscall `279` is `move_pages`, not `memfd_create`. This generic alias must
not be active in the x86_64 dispatch table.

### Defined But Not Wired

Some x86-native syscall aliases are defined but not installed in the x86
dispatcher:

- `SYS_vfork_x86 = 58`
- `SYS_getrlimit_x86 = 97`
- `SYS_pselect6_x86 = 270`

These need direct table entries.

### Libc Still Masks Kernel ABI Gaps

`toolchain/musl-xv6/arch/x86_64/bits/syscall.h.in` still maps most Linux
`SYS_*` names to private xv6 numbers. That hides kernel ABI gaps for code
compiled against this libc, but it does not help Linux executables or code
that performs raw Linux syscalls.

## Phase 1: Generate And Maintain An ABI Inventory

Create a generated audit artifact comparing:

- Host Linux syscall table:
  `/usr/include/x86_64-linux-gnu/asm/unistd_64.h`
- Kernel syscall numbers:
  `kernel/kernel/inc/syscall.h`
- x86 dispatcher:
  `kernel/arch/x86_64/irq/syscall.c`
- Toolchain syscall header:
  `toolchain/musl-xv6/arch/x86_64/bits/syscall.h.in`
- Runtime ABI test:
  `user/programs/linuxsyscallabitest/`

For every Linux x86_64 syscall number, classify it as:

- `native-ok`: number maps to the correct syscall.
- `native-missing`: syscall exists internally but is not reachable by the
  Linux number.
- `wrong-dispatch`: Linux number maps to an unrelated xv6 syscall.
- `unsupported-ok`: syscall returns `-ENOSYS`.
- `unsupported-bad`: syscall logs noisily, panics, returns the wrong errno,
  or reaches legacy fallback.
- `struct-risk`: number exists but ABI structs, flags, or behavior may differ.

Recommended durable outputs:

- `docs/linux-abi-audit.md`
- `docs/linux-abi-audit.csv`
- a small script under `scripts/` to regenerate the audit.

## Phase 2: Stop Wrong Dispatch

Fix the wrong-dispatch class before adding more syscall coverage.

Tasks:

1. Remove the global fallback to `legacy_xv6_syscall_alias()` from normal
   x86_64 Linux syscall dispatch.
2. Preserve old xv6 syscall compatibility only behind an explicit mechanism:
   a build flag, ELF note, process ABI tag, or private high-number range.
3. Make any unmapped Linux syscall return `-ENOSYS`.
4. Remove x86 dispatch entries for generic aliases that collide with Linux
   x86_64 numbers, especially `SYS_memfd_create_generic = 279`.
5. Add tests proving raw Linux syscall numbers no longer route to unrelated
   handlers.

Minimum tests:

- Raw syscall `102` must behave as Linux `getuid`, or deliberately return a
  Linux-compatible result. It must never call `listen`.
- Raw syscall `110` must behave as Linux `getppid`. It must never call
  `getpeername`.
- Raw syscall `137` must behave as Linux `statfs` or return `-ENOSYS`. It
  must never call `faccessat`.

## Phase 3: Wire Implemented Syscalls To Native x86_64 Numbers

Add direct x86_64 aliases for implemented syscalls. Prefer naming native
aliases as `SYS_<name>_x86` when a private `SYS_<name>` already exists.

Highest priority:

- Process and identity:
  `getuid`, `getgid`, `geteuid`, `getegid`, `setuid`, `setgid`, `getppid`,
  `setpgid`, `getpgid`, `setsid`, `getsid`, `getgroups`, `setgroups`,
  `setresuid`, `getresuid`, `setresgid`, `getresgid`.
- Already-defined missing aliases:
  `vfork(58)`, `getrlimit(97)`, `pselect6(270)`.
- Memory:
  `mremap(25)`, `msync(26)`, `mincore(27)`, `madvise(28)`.
- Filesystem:
  `fchmod(91)`, `fchown(93)`, `statfs(137)`, `fstatfs(138)`, `chroot(161)`,
  `sync(162)`, `mount(165)`, `umount2(166)`, `reboot(169)`.
- At-family:
  `openat(257)`, `mkdirat(258)`, `mknodat(259)`, `fchownat(260)`,
  `newfstatat(262)`, `unlinkat(263)`, `renameat(264)`, `linkat(265)`,
  `symlinkat(266)`, `readlinkat(267)`, `fchmodat(268)`, `faccessat(269)`,
  `utimensat(280)`.
- FD, event, and network:
  `accept4(288)`, `eventfd2(290)`, `epoll_create1(291)`, `dup3(292)`,
  `pipe2(293)`, `preadv(295)`, `pwritev(296)`, `sendmmsg(307)`,
  `preadv2(327)`, `pwritev2(328)`, `clone3(435)`.
- Signal:
  `rt_sigreturn(15)`, `rt_sigpending(127)`, `rt_sigtimedwait(128)`,
  `rt_sigqueueinfo(129)`, `rt_sigsuspend(130)`, `sigaltstack(131)`.

## Phase 4: Audit Behavior By ABI Area

For each syscall family, audit both the syscall number and the behavior.

### Process And Thread ABI

Audit:

- `clone`, `vfork`, `exit`, `exit_group`, `wait4`.
- Thread IDs vs process IDs.
- `kill`, `tkill`, `tgkill`, including signal `0`.
- Zombie handling.
- Process group and session behavior.
- `set_tid_address`.
- Robust futex list.
- TLS through `arch_prctl`.

Expected Linux behavior:

- Unsupported flags should return Linux-style errno, not partially succeed.
- `exit_group` should terminate the process/thread group consistently.
- `kill(pid, 0)` should perform existence and permission probing only.
- Dead tasks should disappear from procfs consistently once reaped.

### Memory ABI

Audit:

- `mmap`, `munmap`, `mprotect`, `mremap`, `msync`, `madvise`, `mincore`,
  `brk`.
- Page alignment rules.
- `MAP_FIXED`, `MAP_ANONYMOUS`, `MAP_PRIVATE`, `MAP_SHARED`.
- Invalid address errno.
- Partial unmap behavior.
- VMA split and merge behavior.

Expected Linux behavior:

- Unaligned byte ranges for `munmap`, `mprotect`, `msync`, `madvise`, and
  `mremap` should be normalized or rejected according to Linux semantics.
- `MAP_FIXED` addresses must stay page-aligned and must not silently move.
- Unsupported flags should return deterministic Linux-style errors.

### Signal ABI

Audit:

- Linux `rt_sigaction` layout.
- Signal masks.
- Signal delivery across threads.
- Signal default actions.
- Restart behavior.
- `sigaltstack`.
- `rt_sigreturn` trampoline expectations.

Expected Linux behavior:

- Native `rt_sigreturn(15)` must be reachable if signals use Linux-style
  trampoline paths.
- Signal `0` behavior must match Linux probing semantics.
- Bad user pointers must return `-EFAULT`.

### FD And VFS ABI

Audit:

- `open`, `openat`, and all open flags.
- `O_CLOEXEC`, `O_NONBLOCK`, `O_DIRECTORY`, `O_NOFOLLOW`, `O_TMPFILE`.
- FD allocation and close-on-exec.
- `fcntl` command compatibility.
- `poll`, `ppoll`, `pselect6`.
- `stat`, `lstat`, `fstat`, `newfstatat`, `statx` struct layouts.
- `AT_FDCWD`, empty path behavior, symlinks, and trailing slash handling.

Expected Linux behavior:

- Unsupported flags should fail predictably.
- `*at()` syscalls must use Linux argument order and `AT_*` flag semantics.
- Struct layout exposed to userspace must match the selected Linux ABI.

### Socket And Network ABI

Audit:

- Native socket syscall numbers.
- Blocking and nonblocking behavior.
- `connect`, `accept`, `accept4`, `sendmsg`, `recvmsg`, `sendmmsg`,
  `recvmmsg`.
- `getsockopt`, `setsockopt`.
- `shutdown`, `getpeername`, `getsockname`.

Expected Linux behavior:

- Socket errno paths should be Linux-compatible.
- Nonblocking sockets must not hang GUI/WebKit event loops.
- DNS and rootfs config should be debugged only after kernel socket behavior
  is known-good.

### Event ABI

Audit:

- `epoll_create`, `epoll_create1`, `epoll_ctl`, `epoll_wait`,
  `epoll_pwait`, `epoll_pwait2`.
- `eventfd2`.
- `timerfd_create`, `timerfd_settime`, `timerfd_gettime`.
- Close semantics and fd lifetime.

Expected Linux behavior:

- Epoll wrappers over kqueue must preserve Linux-visible event masks and
  lifetime behavior.
- Closing watched fds should not leave stale events or leaked compositor state.

### Time And Resource ABI

Audit:

- `clock_gettime`, `clock_getres`, `clock_nanosleep`.
- `gettimeofday`.
- `getrlimit`, `setrlimit`, `prlimit64`.
- `getrusage`, `sysinfo`.
- Unsupported clocks and resources.

Expected Linux behavior:

- Unsupported clocks/resources should return Linux-style errno.
- Read-only resource limit probes should not fail merely because enforcement
  is incomplete.

### Procfs And Linux Filesystem Expectations

Audit:

- `/proc/self`.
- `/proc/<pid>`.
- `/proc/<pid>/status`.
- `/proc/<pid>/fd`.
- Dead process visibility.
- WebKit and GLib procfs probes.

Expected Linux behavior:

- Procfs should support existence checks used by GUI apps.
- Closed or dead clients should not leave lingering desktop windows.

### Unsupported Syscall ABI

Audit:

- Every unsupported Linux syscall should return `-ENOSYS`.
- Unsupported syscalls should not log noisily during normal app startup.
- Unsupported syscalls must never route to old xv6 behavior.

## Phase 5: Expand Raw Syscall Tests

Extend `user/programs/linuxsyscallabitest/` into a table-driven raw syscall
suite.

Add tests for:

- Raw syscall numbers `100..148` to catch legacy misrouting.
- Identity syscalls:
  `getuid(102)`, `getgid(104)`, `geteuid(107)`, `getegid(108)`.
- Process syscalls:
  `getppid(110)`, `setpgid(109)`, `getpgid(121)`, `setsid(112)`,
  `getsid(124)`.
- At-family:
  `openat(257)`, `newfstatat(262)`, `faccessat(269)`, `readlinkat(267)`.
- FD/network:
  `dup3(292)`, `pipe2(293)`, `accept4(288)`.
- Memory:
  `mremap(25)`, `msync(26)`, `madvise(28)`, `mincore(27)`.
- Signal:
  `rt_sigreturn(15)` through real signal delivery if possible.
- Event:
  `epoll`, `eventfd`, `timerfd`.
- Unsupported syscall probes verifying `-ENOSYS`.

Add a wrong-syscall detector: pass intentionally invalid arguments where the
correct syscall should return a known errno, and verify the result does not
match an unrelated syscall path.

## Phase 6: Move libc Overrides Back Toward Linux Numbers

After the kernel accepts Linux native numbers:

1. Change `toolchain/musl-xv6/arch/x86_64/bits/syscall.h.in` so normal
   Linux `SYS_*` names use native Linux x86_64 numbers.
2. Keep private xv6-only syscalls in the 1200+ range.
3. Remove libc behavior overrides that only exist because the kernel lacked
   Linux ABI behavior.
4. Rebuild host-glibc userland and ports.

Build commands:

```sh
cmake --build build-x86_64 --target kernel user -j2
cmake --build build-x86_64/ports --target port-wayland -j2
```

Refresh rootfs:

```sh
scripts/make-rootfs.sh build-x86_64/sysroot /tmp/xv6-linux-abi.img 1536
```

## Phase 7: Validate Linux-Style Executable Compatibility

Use three validation tiers.

### Tier 1: Raw Syscall Tests

- `linuxsyscallabitest`.
- Small static raw-syscall binaries.
- Negative errno tests.
- Wrong-dispatch regression tests.

### Tier 2: Libc Tests

- Rebuild musl.
- Run libc smoke tests.
- Verify pthread create/destroy.
- Verify DNS, sockets, epoll, timerfd, signal delivery.

### Tier 3: Desktop Applications

- Wayland compositor. Core session coverage now includes host-glibc
  `/bin/desktop`, `/bin/wlcomp`, and `/bin/glsmoke` using
  `/lib64/ld-linux-x86-64.so.2`; `/bin/glsmoke` maps in the VM against
  `wayland-0` and reports the xv6 EGL/GLES compatibility renderer.
- GTK.
- WebKit MiniBrowser.
- YouTube page load.
- Close WebKit and confirm no lingering window or process.
- Repeat launch/close cycles.

GUI VM launch pattern:

```sh
FSIMG=/tmp/xv6-linux-abi.img USE_KVM=1 QEMU_NET=1 bash scripts/launch-gui.sh
```

Headless smoke pattern:

```sh
DISPLAY_MODE=nographic QEMU_NET=1 FSIMG=/tmp/xv6-linux-abi.img bash scripts/launch-gui.sh
```

## Phase 8: Regression Gates

Before each commit:

```sh
cmake --build build-x86_64 --target kernel user -j2
cmake --build build-x86_64 --target kernel-sparse -j2
cmake --build build-x86_64/ports --target port-wayland -j2
```

Then boot the fresh image and run:

- `linuxsyscallabitest`.
- Networking smoke.
- WebKit local page.
- WebKit YouTube.
- Close WebKit and confirm no lingering window/process.

## Recommended Implementation Order

1. Disable or gate legacy alias fallback.
2. Fix direct number collisions.
3. Add x86 aliases for identity and process syscalls.
4. Add raw syscall tests for those exact numbers.
5. Add x86 aliases for at-family and fd syscalls.
6. Add memory syscall aliases and behavior tests.
7. Add signal ABI tests, especially `rt_sigreturn`.
8. Add event and socket aliases.
9. Validate WebKit and YouTube behavior.
10. Move the musl x86_64 syscall header toward native Linux numbers.
11. Rebuild the toolchain, rootfs, userland, and ports.
12. Run fresh VM validation.

## Notes For Future Agents

- Do not treat libc success as proof of Linux ABI success. Raw Linux syscall
  numbers must work too.
- Prefer returning `-ENOSYS` over wrong dispatch for unsupported Linux
  syscalls.
- Do not reintroduce generic syscall aliases into the x86_64 table unless
  they are proven non-conflicting with Linux x86_64 numbers.
- Do not revert unrelated dirty state in submodules.
- Commit from deepest changed submodules upward when publishing changes.
