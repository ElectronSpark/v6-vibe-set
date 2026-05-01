# WebKitGTK Override Retirement Map

Keep active WebKit validation work in `WEBKIT_TODO.md`, and keep GPU/OpenGL
work in `GPU_OPENGL_PLAN.md`.

The repo currently carries 8 WebKitGTK source override files under
`ports/webkit/overrides/webkitgtk-2.42.5`.  The current runnable WebKit path
still stages the repo-local prebuilt runtime from `ports/webkit/sysroot`, so
removing an override retires source rebuild debt but does not by itself rebuild
the staged browser binary.

## Recently Retired Overrides

- GCrypt secure-memory initialization is back on the upstream path after xv6
  gained `mlock2`, `mlockall`, `munlock`, and `munlockall` syscall aliases.
- WebKit shared memory is back on the upstream `memfd_create` path after xv6
  gained native x86/generic memfd syscall numbers and the toolchain exposes the
  corresponding `__NR_*` aliases.
- ANGLE's worker-thread source override was removed after the toolchain recipe
  switched full GCC/libstdc++ builds to POSIX threads.  A rebuilt toolchain
  should expose `std::thread`, `std::mutex`, `std::condition_variable`, and
  `std::call_once` from libstdc++ instead of relying on WebKit-side worker
  suppression.
- The GTK platform CMake install-path override was removed.  Runtime layout is
  now owned by the WebKit port staging recipe rather than by patched WebKit
  source build files.
- WebKit's C++/GLib one-time initialization overrides were removed after the
  target compiler was rebuilt with POSIX-threaded libstdc++ and target
  `std::thread`/`std::call_once` probes linked successfully.
- The WebCore `PlatformDisplay` surfaceless xv6 fallback override was removed;
  xv6 now provides a real Wayland/EGL path through the Mesa/libdrm/GBM
  compatibility stack, with surfaceless kept as a Mesa test lane rather than a
  WebKit source patch.
- The Unix IPC transport override was removed.  xv6 now keeps WebKit on the
  upstream GLib `SOCK_STREAM` IPC path and validates large split messages,
  nonblocking readiness, `SCM_RIGHTS`, descriptor lifetime, and fd pressure in
  `webkitabitest`, instead of forcing WebKit source to use `SOCK_SEQPACKET`.
- The generic IPC scheduling/async-reply tolerance override was removed.  xv6
  now treats missed async replies as a WebKit/runtime logic issue rather than
  a kernel ABI escape hatch; the kernel side is covered by the AF_UNIX and
  process-lifetime tests.
- The CSS selector parser inline-capacity override was removed.  The current
  toolchain/runtime path no longer needs source-level `Vector` capacity
  changes in WebCore selector parsing.
- The CSS selector-list override was removed after the parser inline-capacity
  override was retired; the remaining empty/null selector tolerance now needs a
  focused WebCore reproducer if it reappears.
- The selector-filter override was removed, restoring WebCore's normal selector
  hash collection path instead of globally disabling that CSS matching
  optimization under `EPOXY_XV6_ALLOW_MISSING`.
- The style rule-feature override was removed, restoring WebCore's normal style
  invalidation feature collection instead of skipping it globally.
- The WebCore document lifecycle override was removed; the empty initial render
  skip is no longer needed on the current MiniBrowser/Google validation path.
- The WebCore frame-loader override was removed, restoring upstream navigation
  policy checks for substitute-data loads.
- The WebKit UIProcess page-proxy override was removed, restoring upstream
  request dispatch for normal and deferred process-launch loads.

## Remaining Override Categories

- GTK accelerated-surface, dmabuf/render-node, and compositing lifetime gaps.
- Google/YouTube compatibility shims in MiniBrowser source-application patches.
- Remaining WebCore page/load lifecycle guards that need focused reproducers
  before they can be moved into an OS or toolchain fix.

## Current Validation Boundary

Override removal only retires repo source override files.  It does not claim
that an upstream WebKitGTK source rebuild is already functional without porting
work.  The supported runtime path remains:

- stage `ports/webkit/sysroot` into the xv6 sysroot;
- verify required MiniBrowser/WebKit helper binaries and libraries with
  `webkit-runtime-check`;
- boot the GUI runtime for browser behavior validation.

## Reproducer Coverage

These checks cover the kernel/toolchain side of the retirement work:

- `webkitabitest`: AF_UNIX stream IPC, nonblocking readiness, `SOCK_CLOEXEC`,
  multi-fd `SCM_RIGHTS`, fd lifetime, memfd/shared mmap, VFS cache-shape,
  waitpid, timerfd, random devices, executable-memory policy, memory-locking
  aliases, and native `memfd_create` aliases.
- `webkitnettest`: nonblocking TCP connect, `SO_ERROR`, send/recv, and parallel
  loopback TCP pressure.
- `webkit-runtime-check`: staged runtime present in sysroot and `fs.img`.
- KVM/GTK WebKit smoke: `webkit=1 webkit_accel=1` reached the Google title and
  `load-finished` with virgl initialized.

## Remaining Proof Work

- Rebuild the local toolchain with POSIX-threaded libstdc++ and prove
  `std::thread`/`std::call_once` with a target compile and guest smoke.
- Build WebKitGTK from clean upstream source without repo overrides.
- Re-run local HTML, HTTP, HTTPS, Google search, repeated navigation,
  close/reopen, and long-idle MiniBrowser validation.
- If clean source fails, add narrowly scoped fixes to a real in-tree port source
  or a new documented patch series, not to an untracked external checkout.
