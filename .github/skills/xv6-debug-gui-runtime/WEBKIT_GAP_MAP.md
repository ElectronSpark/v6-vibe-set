# WebKitGTK Override Retirement Map

The repo no longer carries WebKitGTK source overrides under
`ports/webkit/overrides/webkitgtk-2.42.5`; the active override count is `0`.
The current WebKit path stays functional by staging the repo-local prebuilt
runtime from `ports/webkit/sysroot`.

## Retired Override Set

The retired set contained 32 files covering MiniBrowser UI/runtime settings,
WebKit IPC, helper-process logging, network/cache policy, shared memory, JSC
feature policy, sandbox policy, and GTK build options.  Those files were
removed from the source tree after the kernel/user reproducer pass and the
fresh container runtime validation.

Retired override categories:

- Debug-only crash/helper/process logs.
- MiniBrowser UI and runtime settings.
- AF_UNIX stream IPC and `SCM_RIGHTS` workarounds.
- Shared-memory and memfd handling workarounds.
- Network cache, libsoup/GIO/OpenSSL, and loader guards.
- No-sandbox, no-acceleration, and interpreter-only JSC policy settings.
- GTK/WebKit build option edits.

## Current Validation Boundary

The removal only retires the repo source override files.  It does not claim
that an upstream WebKitGTK source rebuild is already functional without porting
work.  The supported runtime path remains:

- stage `ports/webkit/sysroot` into the xv6 sysroot;
- verify required MiniBrowser/WebKit helper binaries and libraries with
  `webkit-runtime-check`;
- boot the GUI runtime for browser behavior validation.

## Reproducer Coverage

These checks passed before the source overrides were removed:

- `webkitabitest`: AF_UNIX stream IPC, nonblocking readiness, `SOCK_CLOEXEC`,
  multi-fd `SCM_RIGHTS`, fd lifetime, memfd/shared mmap, VFS cache-shape,
  waitpid, timerfd, random devices, and executable-memory policy.
- `webkitnettest`: nonblocking TCP connect, `SO_ERROR`, send/recv, and parallel
  loopback TCP pressure.
- `webkit-runtime-check`: staged runtime present in sysroot and `fs.img`.
- Fresh container rebuild: `xv6-images` and `webkit-runtime-check` passed after
  removing the previous container/image/build directory.
- Headless desktop WebKit smoke: `webkit=1` reached the Google page title and
  survived a short idle run.
- Fresh host rebuild after override retirement: removed `build-x86_64`, rebuilt
  `image` plus `webkit-runtime-check`, launched KVM with
  `root=/dev/disk0 netsurf=0 webkit=1`, reached Google, submitted an `xv6`
  Google search, navigated through GitHub, YouTube, and xv6-public GitHub pages,
  and remained responsive for several minutes.

## Remaining Proof Work

- Build WebKitGTK from clean upstream source without repo overrides.
- Re-run local HTML, HTTP, HTTPS, Google search, repeated navigation,
  close/reopen, and long-idle MiniBrowser validation.
- If clean source fails, add narrowly scoped fixes to a real in-tree port source
  or a new documented patch series, not to an untracked external checkout.
