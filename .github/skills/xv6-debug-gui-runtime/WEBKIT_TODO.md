# Kernel-Side TODO for WebKit on xv6

Focus this checklist on kernel and kernel-adjacent gaps that force WebKitGTK overrides. WebKit patches should be treated as symptoms: for each item, add a kernel/user ABI test, fix the xv6 primitive if possible, then retire the matching WebKit workaround.

## Override-To-Kernel Gap Map

- [ ] Build a table for every file in `ports/webkit/overrides/webkitgtk-2.42.5`.
- [ ] For each override, identify the xv6 primitive involved: syscall, fd semantics, VFS/ext4, socket/IPC, process launch, signal, mmap, Wayland SHM bridge, or scheduler/threading.
- [ ] Record the minimal non-WebKit reproducer that should fail before the kernel-side fix and pass after it.
- [ ] Mark debug-only and UX-only WebKit changes separately so they are not confused with kernel gaps.

## Unix Sockets And IPC

- [ ] Decide whether to implement enough `AF_UNIX SOCK_SEQPACKET` for WebKit's upstream IPC path.
- [ ] Add kernel tests for `socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds)`.
- [ ] Add kernel tests for `socketpair(AF_UNIX, SOCK_STREAM, 0, fds)` message framing under nonblocking reads/writes.
- [ ] Validate `sendmsg`/`recvmsg` fd passing, including multiple fds per message.
- [ ] Validate `SCM_RIGHTS` lifetime: sender close before receiver use, receiver close, process exit cleanup.
- [ ] Validate `O_NONBLOCK`, `fcntl(F_SETFL)`, `poll`, `POLLIN`, `POLLOUT`, `POLLHUP`, and `POLLERR` behavior on Unix sockets.
- [ ] Validate `FD_CLOEXEC` and `SOCK_CLOEXEC` through `exec`.
- [ ] Retire or justify WebKit's `SOCK_STREAM` override in `ConnectionUnix.cpp`.

## File Descriptors And Process Launch

- [ ] Add tests for `posix_spawn`/`fork` plus fd inheritance that mirror GLib `GSubprocess`.
- [ ] Validate `dup`, `dup2`, `close_range` if present, and close-on-exec behavior with helper processes.
- [ ] Validate parent/child socket handoff used by WebKit WebProcess and NetworkProcess.
- [ ] Validate `waitpid`, `WNOHANG`, child exit status, and zombie cleanup under many helper launches.
- [ ] Check process limits, fd table growth, and cleanup after repeated MiniBrowser navigation.
- [ ] Retire or reduce WebKit `ProcessLauncherGLib.cpp` changes after kernel/process semantics are proven.

## Shared Memory And mmap

- [ ] Add syscall tests for `memfd_create`, including name handling and `MFD_CLOEXEC`.
- [ ] Validate `ftruncate` on memfd and ext4/tmpfs files.
- [ ] Validate `mmap(MAP_SHARED)` visibility between parent and child after fd passing.
- [ ] Validate `munmap`, process exit unmap, and page-cache/reference cleanup.
- [ ] Check whether WebKit requires `F_ADD_SEALS`, `F_GET_SEALS`, or other memfd sealing semantics.
- [ ] Validate large shared-memory objects used for WebKit painting and IPC bodies.
- [ ] Retire or justify direct `memfd_create` changes in `SharedMemoryUnix.cpp`.

## VFS, ext4, tmpfs, And Cache Semantics

- [ ] Reproduce WebKit network-cache open/write/read with a small standalone kernel/VFS test.
- [ ] Validate nested directory creation, permissions, unlink while open, and rename-over-existing.
- [ ] Validate large file writes, sparse/truncated files, `fstat`, `stat`, and `statvfs`.
- [ ] Validate `fsync`, `fdatasync`, and behavior when they are no-ops or partial implementations.
- [ ] Validate advisory file locks if WebKit/SQLite-style storage expects them.
- [ ] Validate mmap-backed file writes and truncate-after-mmap behavior.
- [ ] Investigate what `FileSystem::markPurgeable` maps to on xv6 and whether it needs a harmless kernel/userspace implementation.
- [ ] Re-enable WebKit disk network cache only after the standalone VFS/cache reproducer passes.

## Networking Syscalls

- [ ] Add HTTPS-oriented socket tests independent of WebKit: nonblocking `connect`, `EINPROGRESS`, `poll(POLLOUT)`, `getsockopt(SO_ERROR)`.
- [ ] Validate `recv`, `send`, short writes, EOF, reset, and timeout behavior under concurrent TLS connections.
- [ ] Validate DNS resolver paths used by GLib/libsoup.
- [ ] Validate many parallel TCP connections because Google opens several.
- [ ] Keep socket debug logging off by default; make tracing opt-in so serial output cannot cause apparent browser freezes.
- [ ] Confirm OpenSSL/GIO fetch succeeds before changing WebKit network code.

## Signals, Timers, And Threads

- [ ] Validate signal delivery to multithreaded GUI processes.
- [ ] Validate `sigaction` with `SA_SIGINFO`, user context registers, and alt/default handlers.
- [ ] Validate `pthread` creation, join/detach, TLS, futex/wakeup behavior, and thread exit cleanup under WebKit-like worker counts.
- [ ] Validate timers used by GLib main loops: `poll` timeout, monotonic clock, realtime clock, and timerfd if present.
- [ ] Validate scheduler fairness while WebKit has UI, network, web, GC, and worker threads active.

## Executable Memory And JavaScriptCore

- [ ] Add tests for `mmap(PROT_READ | PROT_WRITE)`, `mprotect(PROT_EXEC)`, and direct `mmap(PROT_EXEC)` if supported.
- [ ] Validate W^X policy decisions explicitly.
- [ ] Validate instruction-cache coherency requirements on all supported architectures.
- [ ] Keep JSC JIT disabled until executable-memory tests pass.
- [ ] Re-enable JSC tiers one at a time only after kernel memory semantics are proven.

## Wayland Kernel Bridge And Compositor ABI

- [ ] Audit the kernel framebuffer/input/ioctl ABI used by `wlcomp`.
- [ ] Validate mouse and keyboard event delivery under heavy WebKit repaint.
- [ ] Validate epoll/kqueue readiness behavior for compositor fds.
- [ ] Ensure `wlcomp` releases `wl_buffer` after copying SHM buffers, then add a stress test for repeated GTK/WebKit paints.
- [ ] Validate cursor surfaces, pointer shape updates, popups, subsurfaces, and close-button input routing.
- [ ] Decide whether missing compositor behavior belongs in userspace `wlcomp` or needs kernel ABI support.

## Graphics Memory And Acceleration Primitives

- [ ] Keep WebKit accelerated compositing disabled until the kernel/userspace graphics path supports the required primitives.
- [ ] Decide whether xv6 will support EGL/GBM/DMABUF or intentionally stay with software SHM rendering.
- [ ] If pursuing acceleration, list required kernel objects, mmap/ioctl semantics, fencing/synchronization, and buffer lifetime rules.
- [ ] If staying software-only, document the kernel/compositor guarantees needed for stable WebKit SHM rendering.

## Sandbox And Namespace Primitives

- [ ] Decide whether xv6 should support any subset of Linux namespace, mount, seccomp, or bubblewrap behavior.
- [ ] If not, document a deliberate no-sandbox WebKit policy for xv6.
- [ ] Validate file URL, upload/open-panel, and resource-directory access without relying on Linux sandbox extensions.
- [ ] Retire sandbox-related WebKit workarounds only if the kernel primitives exist and are tested.

## Kernel Resource Accounting

- [ ] Track fd counts per process during Google load/search/idle.
- [ ] Track VMAs, mapped pages, shared-memory pages, and page-cache pages during WebKit use.
- [ ] Track socket counts and TCP states during Google load/search/idle.
- [ ] Add leak checks for process exit: no leaked PTEs, fds, sockets, pages, or shm objects after closing MiniBrowser.
- [ ] Add stress tests for repeated launch, navigate, close, and relaunch.

## Build And Runtime Reproducibility

- [ ] Keep clean container builds independent of `/home/es/xv6/xv6-tmp`.
- [ ] Treat `ports/webkit/sysroot` as a runtime artifact until WebKit can be rebuilt in-container from source.
- [ ] Add a kernel/runtime validation target that boots the container image and checks WebKit helper processes launch.
- [ ] Verify `fs.img` contains the same runtime tested by QEMU.

## Validation Ladder

- [ ] Boot desktop and launch MiniBrowser to `about:blank`.
- [ ] Load local HTML.
- [ ] Load HTTP.
- [ ] Load HTTPS through GLib/GIO/OpenSSL.
- [ ] Load `https://www.google.com/`.
- [ ] Submit a Google search with JavaScript enabled.
- [ ] Navigate repeatedly for several minutes.
- [ ] Close and reopen MiniBrowser.
- [ ] Leave MiniBrowser idle long enough to catch delayed freezes.
- [ ] Repeat after a fresh container build.

## Patch Retirement Rule

- [ ] Do not remove a WebKit override until the corresponding kernel/ABI reproducer passes.
- [ ] After each kernel-side fix, rebuild WebKit from clean source plus remaining overrides.
- [ ] Re-run the validation ladder.
- [ ] Update this TODO with the kernel commit, the retired WebKit patch, and the passing test.
