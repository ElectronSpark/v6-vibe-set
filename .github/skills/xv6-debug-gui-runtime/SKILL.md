---
name: xv6-debug-gui-runtime
description: 'Use when: debugging fluid xv6-os GUI runtime behavior, Wayland desktop freezes, cursor/input symptoms, NetSurf side effects, generated wlcomp.c drift, compositor loop hypotheses, or GUI observations that are not yet stable enough for source-derived skills.'
argument-hint: 'Describe the GUI symptom and latest runtime observation'
---

# xv6 GUI Runtime Debugging

## Fluidity Notice

This skill is a moving debug notebook for GUI runtime behavior. It is not ground truth and can become deprecated without notice. Prefer current generated `wlcomp.c`, kernel source, and the stable `xv6-wayland-kernel-bridge` skill when they conflict.

Current companion docs in this directory:

- `WEBKIT_TODO.md`: active WebKit validation checklist.
- `WEBKIT_GAP_MAP.md`: archived WebKitGTK override-retirement notes.
- `GPU_OPENGL_PLAN.md`: active GPU/OpenGL plan and gap tracker.
- `AGENT_TEAM.md`: role map for long GPU/GUI implementation sessions and the
  current Hyper-V lessons learned.

## When to Use

- The desktop behavior changes depending on KVM, NetSurf, input timing, or generated compositor output.
- Kernel input counters move but the cursor, keyboard, or Wayland clients do not respond.
- You need to separate compositor blocking, kernel event waits, and user-space rendering.
- A GUI observation is useful but not ready to become permanent documentation.

## Workflow

1. Check the generated compositor, not only the source:
   - `build-x86_64/ports/wayland/wlcomp-build/wlcomp.c`
   - Confirm `wl_event_loop_dispatch(loop, 0)` and outer `epoll_wait(epfd, events, 8, 16)` shape unless testing a deliberate experiment.
2. Keep NetSurf out of base freeze triage unless it is the target:
   - Under KVM, `QEMU_NETSURF=auto` should append `netsurf=0`.
3. For input freezes, take both sides of the bridge:
   - kernel: `xv6-input`, `xv6-kqueue wlcomp`, `xv6-syscall wlcomp`
   - user-space artifact: generated `wlcomp.c` event-loop and input ABI
4. Treat repeated framebuffer `ioctl` samples as evidence that the compositor is still rendering.
5. Treat a blocked internal Wayland kqueue with outer input queued as an event-wait/timer problem until proven otherwise.
6. Promote stable findings back to `xv6-wayland-kernel-bridge`, `xv6-kernel-event-wait`, or `xv6-kernel-input` after validation.

## Methodology

- Split every GUI symptom into producer, wait path, consumer, and renderer. For cursor freezes, that means mouse IRQ/ring, cdev poll/kqueue, compositor read loop, and framebuffer update.
- Always compare source intent with generated compositor output before changing kernel code.
- Keep browser/client effects separate from base desktop effects. Disable NetSurf for kernel freeze triage unless the browser is the experiment.
- For NetSurf launch failures, separate the two launchers first: `desktop.c` autostart at session boot and `wlcomp.c` desktop/menu launchers after the compositor is running.
- Capture the browser contract before changing code: `/proc/cmdline`, generated `wlcomp.c`, `/tmp/app_log.txt`, `WAYLAND_DISPLAY`, `GDK_BACKEND`, `XDG_RUNTIME_DIR`, `HOME`, `XV6_GUI_SESSION`, and whether `/tmp/wayland-0.lock` exists.
- When testing GUI apps from an interactive terminal, confirm the terminal shell came from `wlcomp` as `sh --gui-session`; serial, ssh, and telnet shells intentionally refuse known GUI-only commands instead of fabricating a desktop session.
- Do not let serial shells spoof GUI capability by exporting `XV6_GUI_SESSION`, `XDG_RUNTIME_DIR`, and `WAYLAND_DISPLAY`, or by manually running `sh --gui-session`. The shell should treat GUI capability as a launch-time property from a `wlcomp` parent, not only as inherited environment text.
- For NetSurf fetch failures after the window maps, separate browser UI success from network/TLS success. Confirm `NETSURF_USE_CURL := YES`, `NETSURF_USE_OPENSSL := YES`, static OpenSSL symbols in `build-x86_64/sysroot/bin/netsurf`, DNS in `/etc/resolv.conf`, and socket/connect logs before changing compositor code.
- For NetSurf tofu regressions, verify the font files and fontconfig search paths in the actual `fs.img`, not only in `build-x86_64/sysroot`. NetSurf should stage repo-sourced DejaVu fonts into `/share/netsurf/fonts`, `/share/fonts/dejavu`, and `/usr/share/fonts/truetype/dejavu`, keep framebuffer `glyph_data` in `/share/netsurf/fonts/glyph_data`, and fontconfig should include `/share/fonts` plus `/share/netsurf/fonts` in `/etc/fonts/fonts.conf`. The GTK frontend should request concrete families such as `DejaVu Sans` and `DejaVu Sans Mono`; generic aliases like `Sans`, `Serif`, and `Monospace` are unreliable in this minimal fontconfig install.
- For MiniBrowser/WebKit fetch failures, first separate launch from the WebKit multi-process runtime. Verify `/libexec/webkit2gtk-4.1/MiniBrowser`, `WebKitNetworkProcess`, `WebKitWebProcess`, `/lib/libwebkit2gtk-4.1.so.0`, `/lib/libjavascriptcoregtk-4.1.so.0`, `/lib/webkit2gtk-4.1/injected-bundle`, and `/lib/gio/modules/libgioopenssl.so` are staged in both `build-x86_64/sysroot` and `fs.img`.
- WebKit launch needs the compositor environment to include Wayland/GTK variables plus `GIO_MODULE_DIR=/lib/gio/modules`, `GIO_USE_TLS=openssl`, `WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1`, and `WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle`. Missing helpers or GIO modules can look like a page-fetch failure even when the window maps.
- If MiniBrowser repeatedly connects to `127.0.0.1:80` and gets `errno=104`, verify the running/generated compositor first. An old launcher preflight in `wlcomp.c` probed localhost before exec and produced this exact noise even when the MiniBrowser URL was `https://www.google.com/`; remove launcher-side localhost waits rather than chasing WebKit networking.
- If WebKit aborts with `Data too big for buffer (4084 + 20 > 4096)`, check the Wayland server buffer cap. Raising the compositor display limit with `wl_display_set_default_max_buffer_size()` lets larger WebKit startup messages pass and avoids misdiagnosing it as a page-fetch failure.
- If GTK/WebKit windows look offset, have an extra floating close button, or route clicks oddly, inspect compositor-side decoration and xdg geometry first. GTK client-side decorations rely on `xdg_surface.set_window_geometry`; the compositor should use that geometry for hit testing and draw the buffer at `surface_pos - window_geometry_offset`. Avoid drawing an extra compositor close button over GTK CSD windows.
- Wayland `ARGB8888` buffers are premultiplied-alpha. If GTK cursors or translucent shadows look dark/fringed, fix compositor blending to use `src + dst * (1 - alpha)` for ARGB8888 instead of multiplying the source color by alpha a second time.
- Graphics validation includes `/bin/glsmoke`, a repo-local no-dependency Wayland client that uses the xv6 `libEGL.a`/`libGLESv2.a` compatibility shim to draw a GLES2-style rotating triangle, plus `_fbstat`, which now prefers `/dev/gpu0` and falls back to `/dev/fb0` for `FB_GPU_GET_STATS`. For lifecycle stress, run `/bin/glsmoke --frames=N --loops=N`, or boot the desktop with `glsmoke=1 glsmoke_frames=N glsmoke_loops=N`; unlike browser clients, desktop leaves glsmoke stdout/stderr on the console so EGL setup and completion lines are visible in captured QEMU logs.
- Mesa software OpenGL bring-up lives in `ports/mesa` and currently builds upstream Mesa softpipe with Wayland/surfaceless EGL and GLESv2.  It depends on `ports/libdrm`, `khronos-headers`, `libexpat`, `wayland-libs`, `wayland-protocols`, and `zlib`; validate the build checkpoint with `cmake --build build-x86_64/ports --target port-mesa -j2`.  The Wayland port installs `mesaeglinfo`, a surfaceless Mesa EGL/GLES probe, and `mesaglsmoke`, a Mesa-backed Wayland client that renders with Mesa, copies readback pixels into an xv6 GPU BO, and presents that BO through `wlcomp`; `mesaglsmoke --demo` draws the visible faceted spherical 3D demo and can use virgl with `glsmoke_accel=1`.  The desktop launcher now maps boot arg `glsmoke=1` to `mesaglsmoke` by default; use `glsmoke=1 glsmoke_compat=1` to force the old repo-local shim.  After regenerating the rootfs, boot a VM and run `mesaeglinfo`; then run `mesaglsmoke --frames=20`, `mesaglsmoke --demo`, or the lifecycle stress form `mesaglsmoke --frames=12 --loops=3 --resize-every=4` with `XDG_RUNTIME_DIR=/tmp` and `WAYLAND_DISPLAY=wayland-0` exported from the shell.  Confirm Mesa reports the intended renderer, the buffer path reports `xv6-gpu-bo`, both probes exit cleanly, and there are no `freewalk` leaks.  The xv6 toolchain's libstdc++ is single-threaded, so this port uses the shared Meson `CPP_ARGS` hook to force-include `toolchain/musl-xv6/compat/cxx_mutex_compat.h` for C++ sources only.
- Early virtio-gpu bring-up uses `QEMU_GPU=virtio-gpu` as a sidecar device while Bochs `/dev/fb0` remains the active display fallback. Kernel PCI logs should show `virtio-gpu detected ...` plus common/notify/ISR/device cap offsets; the minimal driver should then log `virtio_gpu: initialized ...` and `virtio_gpu: display info ok scanout0=...`.
- Basic virtio-gpu command-path validation should also log `virtio_gpu: resource smoke ok ...`; that covers create-resource-2d, attach-backing, transfer-to-host-2d, resource-flush, and unref against QEMU.
- `fbstat` includes render-device and virtio-gpu counters. A healthy resource smoke plus persistent scanout run should show nonzero `virtio_commands`, zero `virtio_failures`/`virtio_timeouts`, one live `virtio_resources`, `virtio_resource_bytes` matching the persistent scanout backing, and two `virtio_transfers` plus two `virtio_flushes`. New render-substrate checks should also watch `gpu_opens`, `gpu_live_opens`, `gpu_ioctls`, `bo_live_bytes`, `bo_peak_handles`, `bo_peak_bytes`, `bo_fd_live`, `bo_fd_peak`, `fence_fd_live`, `fence_fd_peak`, `fence_fd_polls`, `fence_fd_poll_ready`, `virtio_context_failed`, and `virtio_context_failures`.
- The virtio-gpu smoke path also exercises `SET_SCANOUT`; a healthy run should show `virtio_scanouts 3` because the smoke resource is attached to scanout 0, detached, and then the persistent scanout-sized resource is attached.
- Runtime framebuffer mirror validation should show `virtio_transfers` and `virtio_flushes` increasing beyond the boot-time value after `wlcomp` starts presenting through `/dev/fb0`; failures and timeouts should remain zero.
- Graphics buffer validation uses `/bin/gpubuftest N`, which exercises `FB_GPU_BO_CREATE`, fills the returned mapping, queries `FB_GPU_BO_IMPORT`, exports the BO as an fd-like capability with `FB_GPU_BO_EXPORT_FD`, imports/maps it with `FB_GPU_BO_IMPORT_FD`, presents with the returned handle through `FB_GPU_BO_PRESENT`, exports/queries/polls the present fence with `FB_GPU_FENCE_EXPORT_FD`, `FB_GPU_FENCE_QUERY`, and `poll(2)`, then exports a deliberately future fence and proves zero-timeout poll is not-ready while wait-query fails immediately instead of blocking. It destroys both creator and fd-imported handles with `FB_GPU_BO_DESTROY`, then releases the mappings with `munmap()`. `/bin/gpubuftest --render-owner` specifically checks `/dev/gpu0` per-open ownership: a second render fd cannot destroy the first fd's BO handle, closing the creator render fd reclaims the stale handle, and an exported BO fd can still be imported afterward. Follow with `/bin/fbstat`; healthy output should show `bo_allocs`, `bo_imports`, `bo_fd_exports`, `bo_fd_imports`, `fence_fd_exports`, `fence_fd_queries`, `fence_fd_polls`, and `fence_fd_poll_ready` increasing, `rejected_blits 0`, no leaked test handles (`bo_handles 0` for pure test BOs and stable `bo_live_bytes` after process exit), and no leaked fd capabilities (`bo_fd_live 0`, `fence_fd_live 0` after the test exits).
- GBM/libdrm substrate validation uses `/bin/gbmtest` after rebuilding `port-xv6-gbm` and `rootfs`. It should print backend `xv6-gbm` and complete linear XRGB BO create/map/export/import/destroy. The xv6 libdrm compatibility path opens `/dev/gpu0` first, reports `/dev/gpu0` as the render device name, and maps `drmPrimeHandleToFD`/`drmPrimeFDToHandle` onto `FB_GPU_BO_EXPORT_FD`/`FB_GPU_BO_IMPORT_FD`. Follow `gbmtest` with `/bin/fbstat`; healthy output has no leaked BO fd objects or imported BO handles after the process exits.
- The private Wayland GPU-buffer protocol is version 2. Fence-aware clients may call `create_buffer_with_fence`; `wlcomp` polls the acquire fence fd before sampling, defers release/frame callbacks until the fence is ready, and keeps the surface damaged while waiting. This private path remains a bootstrap/fence-probe fallback; new generic clients should prefer standard `zwp_linux_dmabuf_v1`.
- Compositor buffer release is now present-fence-aware. `wlcomp` queues `wl_buffer.release` for replaced and committed buffers, queries the framebuffer BO present fence with `FB_GPU_BO_FENCE`, and only sends releases/frame callbacks after acquire fences and the latest present fence are ready. If GUI clients appear to stall in SHM/memfd allocation again, verify both acquire-fence readiness and the release queue before assuming a toolkit bug.
- Standard Wayland dmabuf validation uses `/bin/dmabufsmoke` after rebuilding `port-wayland`, `port-xv6-gbm`, and `rootfs`. Export `XDG_RUNTIME_DIR=/tmp` and `WAYLAND_DISPLAY=wayland-0`, then run `dmabufsmoke`; healthy output is `dmabufsmoke: presented linux-dmabuf buffer` plus `wlcomp: client app_id: dmabufsmoke`. Follow with `/bin/fbstat`; healthy output has the BO fd export/import counters incremented, `bo_handles 0`, `bo_live_bytes 0`, `bo_fd_live 0`, `fence_fd_live 0`, `rejected_blits 0`, and zero virtio failures/timeouts.
- Host-side graphics substrate validation now lives in `scripts/gpu-validate.sh`. By default it first checks the `scripts/run-qemu.sh` dry-run launch contract for deterministic GTK geometry (`zoom-to-fit=off`, hidden menubar/tabs, 1280x800 guest mode, and `virtio-tablet-pci`), then uses KVM plus `QEMU_GPU=virtio-gpu`, waits for command success markers and the guest prompt with `expect`, runs `gbmtest`, `dmabufsmoke`, `mesawlegl --frames=4 --loops=1 --resize-every=2`, concurrent `mesawlegl` plus `mesaglsmoke` resize stress, injects a bottom-right absolute pointer event with `/bin/mouseinject`, then runs `gpubuftest 3` and `gpubuftest --render-owner`, captures post-quiesce `fbstat`, and rejects crash markers, rejected blits, leaked BO/fence fd objects, and virtio failures/timeouts. The post-quiesce step matters because custom BO fd release accounting runs after VFS `close()` via RCU/workqueue cleanup; software fence fds use VFS `early_release_on_close` for exact object lifetime when no hidden/concurrent refs remain. Use `GPU_VALIDATE_BUILD=1` to rebuild first, and `GPU_VALIDATE_VISIBLE_3D=1` for the optional GTK/virgl visible-demo lane; that path waits for `renderer=virgl buffer=xv6-gpu-bo spherical-poly-demo`, captures `build-x86_64/gpu-validate.ppm` through the QEMU monitor, runs `virgltest --bad-submit` to force one virgl context into the failed state, captures `fbstat`, and rejects the same crash/failure markers. A healthy visible run shows `virgltest: bad-submit isolated`, `virtio_context_failed 0`, one increment in `virtio_context_failures`, and zero virtio failures/timeouts. Normal `/bin/virgltest` also validates explicit virgl submit fence fds with `FB_GPU_VIRGL_FENCE_EXPORT_FD` and `FB_GPU_VIRGL_FENCE_QUERY_FD`.
- Virgl context fault validation uses the private xv6 submit flag `FB_GPU_VIRGL_SUBMIT_FORCE_FAIL`. This is a deterministic test hook for the kernel policy: it marks only the target context failed and returns `EIO`, without sending malformed command buffers to QEMU. After the forced failure, submits and context-bound resource creation against that context should fail, context destroy should still work, and a fresh context should submit a NOP successfully.
- `wlcomp` has quiet-by-default damage instrumentation for blink/flicker work. Set `XV6_WLCOMP_STATS_MS=<ms>` in the compositor environment to log frame count, presented rects/pixels, full-screen frames, union collapses, acquire-fence blocked frames, full-damage causes, and present mode (`bo-present` or `user-blit`).
- `wlcomp` should prefer an exportable `FB_GPU_BO_CREATE` compositor backbuffer and log `wlcomp: using fb GPU buffer ... handle=...`; if that ioctl fails it falls back to malloc plus `FB_GPU_BLIT`.
- Direct scanout remains evaluated but deferred, not forgotten: current `wlcomp` still needs software overlays for taskbar, menu, internal windows, and the compositor-owned cursor. Keep using GPU-backed compositor present (`FB_GPU_BO_PRESENT`) as the supported acceleration lane until hardware cursor/overlay planes or an explicit fullscreen-no-overlays mode exists.
- If MiniBrowser shows the requested URL and then the window cannot be closed, sample with `xv6-threads` before assuming a compositor event-loop freeze. A known failure mode is the MiniBrowser thread-group leader in `ZOMBIE` while non-leader UI threads plus `WebKitNetworkProcess` and `WebKitWebProcess` remain alive; the compositor may still be rendering in framebuffer `ioctl`, but the client no longer processes its own close UI.
- If `wlcomp` reports the MiniBrowser child exited but `xv6-threads` still shows TGID-matching MiniBrowser workers, check kernel process lifecycle before chasing networking. Parent `waitpid` must not reap a zombie thread-group leader while `live_threads > 0`; fatal signal/killed trap paths should use `thread_group_exit()`, and `exit()` should promote leader exits with live siblings to group exit as a backstop.
- Do not force-destroy Wayland clients or kill the process group merely because a tracked launcher child became waitable. WebKit can have a launcher-visible leader/helper mismatch; normal reaping should free the child slot only and let Wayland HUP/resource cleanup remove genuinely dead clients. Keep client destruction/process-group kill for explicit close/force-close paths.
- For WebKit Google fetch failures, validate plain network and TLS independently before changing browser code. `openssl s_client -connect google.com:443 -servername google.com` should resolve and connect; if it only fails verification, check `/etc/ssl/cert.pem` and `/etc/ssl/certs/ca-certificates.crt`. The staged NetSurf bundle at `/share/netsurf/ca-bundle` is a known-good source for these default OpenSSL paths.
- If MiniBrowser maps, shows `https://www.google.com/`, and no kernel socket log ever shows an outbound Google connect, suspect WebKit helper IPC rather than DNS/TCP/TLS. A launched `WebKitNetworkProcess` that never reaches fetch setup can leave the UI blank with no connect attempts.
- WebKit/JSC treats `/dev/urandom` as mandatory. If MiniBrowser SIGABRTs in `WTF::RandomDevice::RandomDevice()` or `WTF::cryptographicallyRandomValuesFromOS()` during GTK/WebKit startup, verify both `/dev/random` and `/dev/urandom` exist and are readable in the guest; a working `getrandom()` syscall is not enough for this code path.
- WebKit's disk network cache can abort early on xv6 before any Google TCP connect. If NetworkProcess tracing stops at `NetworkCache::Cache::open()`, launch MiniBrowser with `WEBKIT_DISABLE_NETWORK_CACHE=1` or bypass disk cache until the cache filesystem assumptions are supported.
- Autostarted GUI clients should redirect stdout/stderr to `/tmp/app_log.txt` like compositor-launched apps. A heavily instrumented WebKit build can flood serial output during Google load and make a healthy compositor look unresponsive.
- Keep compositor-launched MiniBrowser on the same quiet environment as desktop-autostart MiniBrowser. Do not leave `G_MESSAGES_DEBUG=all` or `WEBKIT_DEBUG=all` in normal launch envs; Google Search can generate enough redirected WebKit/GLib log traffic after navigation to make the page area appear frozen even after the initial Google page works.
- For current xv6 MiniBrowser validation, launch WebKit with a lightweight settings profile: keep JavaScript enabled for Google Search, but disable WebGL, WebAudio, mediasource/media-stream, page-cache, DNS prefetching, and offline web application cache via MiniBrowser WebKitSettings options. Do not pass `default-font-size`/`minimum-font-size` as MiniBrowser argv: those WebKitSettings properties are unsigned and MiniBrowser's dynamic option generator does not expose them. Set MiniBrowser's default/minimum font sizes directly in `Tools/MiniBrowser/gtk/main.c`, and keep the URL entry legible in `BrowserWindow.c` because `GDK_DPI_SCALE` alone is not enough for xv6's 1024x768 desktop.
- If Google loads but later stops responding with JavaScript enabled, keep the browser in interpreter-only JavaScriptCore mode before changing kernel networking: set `JSC_useJIT=0`, `JSC_useBaselineJIT=0`, `JSC_useDFGJIT=0`, `JSC_useFTLJIT=0`, `JSC_useRegExpJIT=0`, `JSC_useDOMJIT=0`, `JSC_useBBQJIT=0`, `JSC_useOMGJIT=0`, `JSC_useConcurrentJIT=0`, and shrink JSC compiler/GC worker counts in the MiniBrowser env. This keeps Google Search JavaScript available while avoiding xv6-sensitive executable-code and concurrent-compiler paths.
- For default MiniBrowser browsing, keep JavaScript enabled because the current Google load/probe path depends on it. Heavy script pages such as YouTube can keep the UI process busy after the title changes while the compositor and guest kernel continue running; pass `webkit_js=0` only for explicit low-script experiments. Both `/bin/desktop` autostart and menu-launched `wlcomp` MiniBrowser honor `webkit_timeout_ms=<ms>` as an explicit recovery guard; keep the normal default at `0` so healthy loaded pages are not closed by the launcher.
- Keep kernel socket tracing off for browser responsiveness validation. `SOCK_DEBUG=1` in `kernel/lwip_port/sys_socket.c` prints every socket/connect/SENDPLUS callback, and unconditional syscall-level `fcntl(F_SETFL)` traces are similarly noisy; Google opens enough TLS connections that this serial/debugcon flood can make MiniBrowser or the compositor appear frozen after the page has successfully loaded.
- If MiniBrowser freezes after Google renders and live user-gdb shows the UI thread stopped at musl `syscall` with `rdi=0x37e` and the caller in GTK's `open_shared_memory()` / `_gdk_wayland_display_create_shm_surface()`, treat it as a compositor Wayland SHM buffer lifecycle problem before chasing JavaScript. GTK allocates memfd-backed SHM buffers for paints; `wlcomp` must send `wl_buffer.release` after copying a committed buffer to the framebuffer, otherwise GTK/WebKit can accumulate unreleased memfds and stall inside `memfd_create`.
- If MiniBrowser/WebKit goes further after SHM buffer release but later freezes with multiple browser threads blocked in `sys_memfd_create -> vfs_iput -> rwsem_acquire_write`, inspect the tmpfs superblock rwsem. A matching live capture showed `/tmp` with `readers=1`, `write_queue>0`, and the active reader in `vfs_get_inode_cached()` waiting on an inode mutex while holding a superblock read lock. Do not sleep on inode mutexes under a superblock read lock; use a try-lock/retry path and prefer writer-priority superblock rwsems for WebKit-style metadata churn.
- WebKit's GLib IPC on xv6 uses AF_UNIX socketpairs. The upstream 4 KiB inline IPC cap falls back to out-of-line shared-memory plus fd passing for larger messages; xv6 IPC and fd-passing paths are still more fragile than byte-stream delivery. Keep WebKit `ConnectionUnix.cpp` `messageMaxSize` large enough for startup messages and keep the kernel AF_UNIX ring substantially larger than that cap, because WebKit treats a successful `sendmsg` as whole-message delivery and partial stream writes can silently corrupt IPC.
- For AF_UNIX `sendmsg()` with `SCM_RIGHTS`, never enqueue the descriptor before confirming the nonblocking byte payload can fit. WebKit sends fd-bearing IPC on nonblocking socketpairs; queuing the fd and then returning EAGAIN/short byte count can detach the descriptor from its message and lead to later WebProcess/UIProcess SIGABRT before any Google TCP connect appears.
- For AF_UNIX `SCM_RIGHTS`, never silently drop queued descriptors when the ancillary queue fills. Return `EAGAIN`/retry instead, and balance both the sender's fd-table lookup ref and the receiver's installed-fd ref. Dropping or leaking fd refs while still delivering the protocol bytes shows up as Wayland `file descriptor expected` errors, NetSurf compositor disconnects, or WebKit `GetNetworkProcessConnection` hangs before any Google HTTP connect.
- Preserve `SCM_RIGHTS` batches, not only the first fd in a control message. WebKit IPC and Wayland both model attachments as a count paired with protocol bytes; delivering fewer fds than the message advertises can strand `CreateNetworkConnectionToWebProcess` or corrupt Wayland shm-pool creation even when the byte stream itself looks healthy.
- When auditing `SCM_RIGHTS` batching, check both the queue code and the syscall copy buffer. A helper can appear to support many fds while `sys_sendmsg()` still caps `msg_control` to `CMSG_SPACE(sizeof(int))`, which truncates WebKit fd batches before the enqueue path sees them.
- If Google fetches but MiniBrowser's page area stops responding and logs repeat `sendmsg EAGAIN ... expected=<n>`, inspect AF_UNIX writable readiness. Do not notify EVFILT_WRITE on the writer after consuming socket-buffer space, and do not report POLLOUT for tiny free-space fragments when `sendmsg()` requires a whole WebKit IPC payload to fit.
- When increasing AF_UNIX ring buffers with `kvmalloc()`, free them with `kvfree()`. A `kfree()` mismatch may only surface when a stuck WebKit or Wayland client is interrupted and its socketpair is released, making the cleanup path look like the primary browser failure.
- WebKit's upstream NetworkProcess responsiveness timeout is short for xv6 while `InitializeNetworkProcess`/`AddWebsiteDataStore` are still slow and heavily instrumented. If `CreateNetworkConnectionToWebProcess` never reaches the NetworkProcess and the UI gets an empty async reply, relax the timeout before assuming the network helper crashed.
- If `WebKitNetworkProcess` is alive but no `/tmp/webkit-networkprocess-trace.log` is created and no Google socket connect appears, check whether `WebProcess::ensureNetworkProcessConnection()` reaches its synchronous `WebProcessProxy::GetNetworkProcessConnection` request. A stall immediately after `WebProcess_InitializeWebProcess` is before libsoup fetch and before the NetworkProcess creates its WebProcess connection.
- If WebKit helper launches log `GLib-WARNING **: waitpid(pid:NN) failed: No child process (10)`, look for duplicate child reaping. `GSubprocess` already owns its child watch; adding `g_child_watch_add()` to the same PID in `ProcessLauncherGLib.cpp` races GLib and can produce `ECHILD` plus spurious helper-death reports.
- When `WebProcess::getNetworkProcessConnection()` logs `sendSync begin` and the UIProcess logs `NetworkProcessProxy::getNetworkProcessConnection enter`, the WebProcess-to-UI sync IPC path is working. If `NetworkProcess::createNetworkConnectionToWebProcess` does not follow, inspect UI-to-NetworkProcess IPC send state first: pending output, short `sendmsg()`, AF_UNIX writable notification, or NetworkProcess main-loop dispatch can strand the request before any HTTP/TLS code runs.
- For WebKit SIGABRT triage, make the abort/assertion source self-identifying before changing network code. Temporary file logs in `WTFReportAssertionFailure`, `WTFReportFatalError`, `WTFCrash`, and `ConnectionUnix.cpp` are much easier to recover from xv6 than interleaved serial stderr; look for `/tmp/webkit-wtf-crash.log`, `/tmp/webkit-ipc-trace.log`, and `/tmp/webkit-networkprocess-trace.log` after a failed run.
- If the NetworkProcess handles `InitializeNetworkProcess` and `AddWebsiteDataStore` but exits before `CreateNetworkConnectionToWebProcess`, suspect an early AF_UNIX readiness close rather than TLS. On xv6, GLib `G_IO_HUP`/`G_IO_ERR` from an IPC socket can be too early or advisory; WebKit's `ConnectionUnix.cpp` should drain/read and only close on actual `recvmsg()` EOF or hard error, otherwise the UIProcess returns an empty `NetworkProcessConnectionInfo`.
- WebKit runtime staging is optional and must not depend on a hardcoded external checkout. Use `-DXV6_WEBKIT_REF_SYSROOT=<sysroot>` or `XV6_WEBKIT_REF_SYSROOT=<sysroot>` only when intentionally staging a prebuilt MiniBrowser/WebKit runtime; without it, `ports/webkit` should skip cleanly.
- For kernel-side WebKit readiness, run the in-guest probes before blaming MiniBrowser: `webkitabitest` should report `16 passed, 1 skipped, 0 failed` with only the deliberate `SOCK_SEQPACKET` policy skip, and `webkitnettest` should pass with its default self-contained loopback TCP server.  These cover AF_UNIX stream IPC, SCM_RIGHTS batching/lifetime, close/poll readiness, ext4/tmpfs/memfd/mmap behavior, waitpid cleanup, timerfd/random/executable-memory policy, and nonblocking TCP connect/send/recv.
- WebKit source porting fixes must be captured as a real in-tree port source change or a documented patch series, never only in an external `xv6-tmp` source checkout or a generated `build-x86*/webkit-stack` directory. The old `ports/webkit/overrides/webkitgtk-2.42.5` tree has been retired; `ports/webkit/apply-xv6-overrides.sh` is now a clean no-op when no overrides are present.
- Optional WebKit staging must also keep the desktop honest. If `/libexec/webkit2gtk-4.1/MiniBrowser` is not executable in the staged sysroot/rootfs, `make-rootfs.sh` should not create `webkit.desktop`, and `wlcomp` should skip any `.desktop` or default shortcut whose `Exec` target is missing. A stale WebKit icon in a no-runtime container build launches a child that exits `127` before MiniBrowser fully execs, often showing the old process name in backtraces.
- If the shell prompt returns with status 0 immediately after MiniBrowser activation while `WebKitNetworkProcess` and `WebKitWebProcess` remain alive, the UIProcess is no longer available to answer `WebProcessProxy::GetNetworkProcessConnection`. Hold the `GApplication` after the main browser window has been added, and release it when that window is destroyed, before chasing lower network layers.
- If that status-0 MiniBrowser return persists after a window-scoped `GApplication` hold, instrument both sides of process teardown before changing networking: connect MiniBrowser `shutdown`, `window-added`, `window-removed`, and `atexit` logs, and temporarily log zero-code `exit`/`exit_group` for `MiniBrowser`/`WebKit*` in the kernel. This distinguishes normal GTK application shutdown from raw thread exit or direct `_exit(0)`.
- On xv6, `g_application_run()` can leave MiniBrowser without a live UIProcess even though no GTK shutdown or `atexit` log fires. A practical WebKit port workaround is to manually `g_application_register()`, `g_application_activate()`, then keep the main thread in an explicit `GMainLoop` until the main window is destroyed; this keeps the UIProcess available for `WebProcessProxy::GetNetworkProcessConnection`.
- If the shell prompt returns after `MiniBrowser` logs that it entered a manual `GMainLoop`, inspect kernel wait/reap behavior rather than GTK lifecycle. Log browser-child reaps from `wait()`/`waitpid()` with `pid`, `tgid`, state, `thread_group->live_threads`, and parent name to catch premature leader reaping or thread-group accounting drift.
- If `waitpid` reaps MiniBrowser with `xstate=-1`, the UIProcess died via the kernel killed/fatal-signal path. Add targeted `usertrap` logs for browser threads (`scause`, `sepc`, `stval`, fault kind) before assuming WebKit IPC or TLS is the next blocker.
- If no `usertrap` fault log precedes `xstate=-1`, instrument signal termination itself: log when browser threads set `THREAD_KILLED`, including the first pending terminating signal and whether it came from default action, mask changes, invalid handler delivery, or `handle_signal()` termination.
- If MiniBrowser/WebKit is reaped with `xstate=-1` and the signal trace reports `signum=6`, treat it as a SIGABRT before continuing networking/TLS work. Add a narrow `kill`/`tgkill`/`tkill` trace for SIGABRT that prints sender, target, and the sender's user backtrace; without the sender-side trace, the later default-action log only proves who died, not which assertion or abort path fired.
- For headless serial validation of MiniBrowser, boot with `netsurf=0 webkit=1` so `/bin/desktop` launches MiniBrowser through the compositor/session path. The xv6 shell supports `VAR=value command` and assignment-only lines, but serial shells should normally refuse GUI-only programs unless the shell was launched by the compositor as a real GUI session.
- Before trusting kernel substring-based debug filters, verify `strstr()` handles shorter haystacks. A broken bounded search can overread short process names like `ps`/`cp`, produce noisy false-positive exit logs, and make process-lifecycle evidence look much worse than it is.
- For wedged WebKit GUI clients, prefer a compositor-side force-close path that records Wayland client credentials, destroys the `wl_client`, and kills the launched process group. This contains the stuck surface/helper-process problem while the WebKit/thread-group root cause is debugged.
- Use a healthy control sample. A running compositor should periodically appear in framebuffer work, outer epoll waits, or input processing depending on where it is interrupted.
- If input is queued but not consumed, first ask whether readiness is level-correct and whether the compositor reaches its drain point.
- If rendering continues but interaction fails, focus on input routing, focus state, pointer/keyboard protocol delivery, or client state rather than framebuffer.

## Validated GPU Baselines

These items were retired from `GPU_REMAINING_GAPS.md` after the May 17, 2026
source audit. Re-check current source and validation logs before changing them,
but do not treat them as open plan items by default.

### Native Present Dependency Tree

- Treat Hyper-V D3D12 acceleration as a dependency chain, not as independent
  green checks: shared-resource import, sync-file acquire, compositor GPU copy,
  DXG present-source commit, display completion, visible-content/FPS credit,
  backend OpenGL-submit, then WebKit acceleration.
- `dxg-resource-scanout-bind` or an equivalent GPU-P/DDA display-bind transport
  is the root missing piece. Until it returns a nonzero present id and display
  completion for the same resource generation, callbacks/releases may be
  drained only as fail-closed lifecycle cleanup, not as native-present credit.
- Keep the rejected native-present lanes explicit in validator output. WSL
  present-history guest-to-host command IDs and the host-to-VM
  `PROPAGATEPRESENTHISTORYTOKEN` completion enum are only candidates without
  sender/completion contracts. Synthvid GPA dirty rectangles, Linux Hyper-V DRM
  shadow blits, and a separate DDA/Nouveau PCI display path are also zero-credit
  until one of them proves a real D3D12 resource-to-display completion path.
- Keep the WSL present-history distinction kernel-owned: VMBus command enum
  IDs are not Linux ioctls, and the stats row must report no sender,
  resource-bind, or display-completion contract before any native-present
  credit can be considered. `/dev/dxg` exposes `dxg_host_to_vm_last`, including
  `PROPAGATEPRESENTHISTORYTOKEN` count, length, command id, and payload-head
  bytes; keep `dxg_host_to_vm_presenthistory_completion_matrix` zero-credit
  until a real sender and matching host-to-VM completion payload are proven.
  `dxg_presenthistory_telemetry_not_completion_matrix` should stay green with
  `linux_inband_handler=absent`; present-history telemetry is not a
  display-bind completion contract.
  `dxg_presenthistory_orphan_completion_rejection_matrix` is the stricter
  downstream guard: even if present-history packets are observed, they are
  zero-credit unless they match a provider pending record and completion demux.
  Today that row must report `provider_pending_match=0`,
  `completion_demux_registered=0`, zero display-bind ids, and zero
  native-present/OpenGL-submit credit.
- Keep the fail-closed proof scalarized: WSL ioctl namespace checked,
  display-bind ioctl absent, standard allocations classified as private driver
  data rather than scanout binding, synthvid limited to GPA dirty rectangles,
  and DDA/Nouveau PCI display split from D3D12 resource import, scanout bind,
  and hardware flip completion.
  Provider pending publication evidence must preserve WSL-shaped provenance
  without overstating retained krefs: `dxgprocess_generation`,
  `process_adapter_generation`, `hmgr_index_unique_valid`,
  `device_object_ref_active`, `resource_object_ref_active`,
  `allocation_object_ref_active`, `shared_parent_snapshot_valid`,
  `opened_child_snapshot_valid`, `syncobject_object_ref_active`, and
  `owner_close_cancelled=0` while fail-closed.
  Sender/source classification is also explicit zero-credit evidence while the
  provider is fail-closed: require `host_saw_display_bind_packet=0`,
  `display_bind_transport_source=none`, and
  `wsl_presenthistory_completion_credit=0` in the source catalog, host-ABI
  discovery, query-fields, and provider-pending rows. Future positive credit
  needs a non-WSL/DDA transport source, `host_saw_display_bind_packet=1`,
  provider completion demux, and source/resource-correlated display
  completion; WSL present-history telemetry remains zero-credit.
- When planning the remaining native-present work, keep the chunks ordered:
  host ABI discovery/proof, kernel scanout-bind path, compositor handoff,
  native completion/lifetime, then FPS/backend/WebKit credit. Do not split
  those into independent pass claims; each later chunk depends on nonzero
  display-correlated completion from the earlier source/resource generation.
- Keep the remaining GPU plan wired through
  `gpu_remaining_plan_dependency_skeleton_matrix`: this row ties the root
  display-bind gate, native completion validators, finite 480p gate, demo
  interaction, Hyper-V OpenGL-submit, KVM/virgl recheck, WebKit route/content,
  and final WebKit artifact together. It should stay green only while WSL has
  no Linux display-bind ioctl or in-band present-history completion handler,
  GPU-P sender/completion contracts are zero, DDA/Nouveau import/scanout-bind/
  hardware-flip completion are absent, and native-present/OpenGL/WebKit credit
  is zero. The pure-C row must also spell out the granular closed gates:
  `real_display_bind_sender=0`, `real_display_bind_completion=0`,
  `native_completion_validator_gate=closed`, `finite_480p_gate=closed`,
  `backend_opengl_submit_gate=closed`, `webkit_enabled_artifact_gate=closed`,
  and `DDA/Nouveau-separate-display-not-D3D12-bind`.
  `gpu_remaining_holistic_skeleton_matrix` is the same dependency graph in
  execution order. It names the active open plan count, ordered chunks, selected
  lane, completion authority, and all downstream gates so future edits cannot
  open FPS, backend, or WebKit credit before display-bind completion exists.
  Keep its source-audited proof rows nearby:
  `wsl_dxg_uapi_namespace_negative_matrix`,
  `wsl_dxg_adapter_display_caps_negative_matrix`, and
  `dda_nouveau_non_readback_display_proof_matrix`, plus
  `wsl_submit_present_fields_not_bind_matrix`,
  `wsl_stdalloc_and_alloc_flags_not_bind_matrix`,
  `wsl_trace_display_bind_negative_matrix`, and
  `provider_credit_gate_negative_matrix`,
  `host_display_bind_source_catalog_matrix`,
  `d3d12_display_bind_host_abi_discovery_matrix`,
  `d3d12_completion_source_authority_matrix`, and
  `native_present_completion_source_namespace_matrix`. WSL adapter display caps,
  submit/present metadata, written primaries, standard-allocation private data,
  allocation flags, trace-visible open-resource/sync-file evidence, provider
  invocation counters, WSLg-channel absence, synthvid GPA-dirty evidence, and
  present-history telemetry remain negative proof. Completion authority is
  narrower than progress telemetry: only the source-local display-bind provider
  may issue D3D12 present/completed ids, while KMS vblank/page-flip and Nouveau
  IRQ-cause counters stay in a separate native-display namespace. DDA/Nouveau
  needs real Linux-shaped display creation, non-virtual connectors, hardware
  vblank IRQs, KMS `NOUVEAU_HW` page flips, and hardware flip completions
  before it can receive `dda_native_display_credit`, and even that remains
  separate from `d3d12_native_present_credit`. A D3D12 display-bind result must
  use `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`; do not
  let `dda_nouveau_native_display` satisfy the D3D12 resource-to-display bind
  gate.
- WebKit and FPS validators should consume `/tmp/wlcomp-d3d12-present` as the
  current-run evidence source and reject title/chrome/cursor-only progress,
  stale logs, dmabuf/render-node-only evidence, and app-loop FPS numbers.
  Keep this file self-contained: it must include canonical `display_bind_*`
  fields, and fail-closed rows should use
  `display_bind_completion_source=missing` with zero ids. Demo-side
  `effective_presented_fps` should be derived from native completion deltas,
  not visible/app-loop FPS.
- Treat `/tmp/wlcomp-d3d12-present` as an atomically sealed compositor
  artifact. The producer should write a temp file, flush/fsync it, then rename
  it into place. Consumers must require
  `d3d12_evidence_seal_begin=1`, `d3d12_evidence_seal_end=1`,
  `d3d12_evidence_seal_complete=1`, matching begin/end generation, and seal
  run ids equal to both `d3d12_run_id` and
  `d3d12_present_identity_compositor_run_id`. Unsealed or partial files are
  stale/partial evidence and must not supply native-present, FPS, backend, or
  WebKit credit.
- Keep FPS evidence source-isolated. `hyperv-3d-fps-validate.sh` wraps sampled
  `/tmp/wlcomp-d3d12-present`, `/tmp/mesawlegl-fps`, `/tmp/wlcomp-fps`,
  `fbstat`, and `/proc/uptime` output in source markers; canonical
  `display_bind_*`, `present_id`, and `completed` native-present evidence
  belongs to the compositor-owned `/tmp/wlcomp-d3d12-present` block. Client
  logs such as `mesawlegl_fps_sample` may carry diagnostic-prefixed copies of
  the evidence they read, but they must not emit canonical display-bind ids
  that the FPS parser could count as compositor proof. Use
  `fps_artifact_source_isolation_negative_matrix` to prove forged app-side
  display-bind/native-present/backend/content/final-handoff scalars stay
  zero-credit.
- Treat `display_bind_*` evidence as the canonical bridge between the kernel
  present-source contract, `wlcomp`, FPS, and WebKit. The required keys are
  `display_bind_backend`, `display_bind_transport`,
  `display_bind_transport_source`, `host_saw_display_bind_packet`,
  `wsl_presenthistory_completion_credit`, `display_bind_present_id`,
  `display_bind_completed_id`, `display_bind_resource_generation`, and
  `display_bind_completion_source`/`completion_source`. Fail-closed Hyper-V
  evidence may name the selected backend/transport, but present/completed ids
  must stay zero and no consumer may grant native-present credit from that.
  Future-positive FPS/demo/WebKit evidence must use
  `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`,
  `host_saw_display_bind_packet=1`, and
  `wsl_presenthistory_completion_credit=0`; WSL present-history telemetry is
  not display-bind source authority.
- Credit-bearing D3D12 evidence rows must also be provider-owned and
  line-scoped. Require `evidence_provider=wlcomp`,
  `evidence_path=/tmp/wlcomp-d3d12-present`, a nonzero evidence generation,
  and the current validation run id on the same display-bind dependency, FPS,
  content-progress, or WebKit-open record that carries the canonical
  backend/transport/source-authority/native-completion tuple. Do not assemble
  a passing tuple from separate scalar lines, older logs, or app-side
  diagnostic copies.
- Display-bind source cleanup can be observed in two phases. A same-process
  `dxgprobe` stale-source row may report
  `cleanup_state=deferred_until_process_exit` when stale queries reject and all
  present/completed ids remain zero; the parent C validator must still observe
  nonzero `release_clears` after the child exits before the section passes.
- Kernel native-present work should go through the source-local display-bind
  request/result provider boundary in `fb_dxg_present.c`. The provider may
  remain fail-closed, but it must preserve source/resource generation,
  completion source, status, block reason, and zero present/completed ids until
  a documented DDA/GPU-P sender replaces the stub.
- Keep display-bind cancellation Hyper-V-owned as well as submission-owned.
  `hyperv_dxg_display_bind_cancel()` is the future async sender cancellation
  boundary for owner-close/unregister paths; while fail-closed it reports
  no host packet, zero WSL present-history credit, zero present/completed ids,
  and no native-present/OpenGL-submit credit.
- `scripts/hyperv-dxg-validate.sh` supports
  `DXG_VALIDATE_SEGMENT=pure-c-lifetime` for the WSL-style DXG lifetime,
  handle-table, shared-resource, sync-file, WDDM payload, and residency
  validators. Use this segment before Mesa-heavy runs when validating a full
  lifetime subsection; its default command line disables boot-time glsmoke so
  Mesa readback/device-removal probes cannot freeze before pure-C rows are
  collected. Full mode must still run before claiming
  Mesa/native-present/FPS/WebKit completion.
- The display-bind provider boundary must be sleepable-safe: snapshot under
  `fb_state.lock`, submit outside that lock, then revalidate the present source
  and resource generation before accepting or recording any provider result.
  The visible diagnostics are `provider_submits`, `lock_dropped_submits`,
  `revalidate_attempts`, `revalidate_successes`, and
  `revalidate_failures`.
- The Hyper-V display-bind provider may be the concrete owner even before the
  host ABI exists. Keep `hyperv_dxg_display_bind_submit_failclosed()` explicit
  about pinned dxg/resource metadata, no host ABI, no sender, and no display
  completion; this is a future sender slot, not native-present proof.
- The public provider boundary is `hyperv_dxg_display_bind_submit()`. Today it
  must route to the fail-closed implementation and expose provider-returned
  `pin_revalidated`, `no_host_abi`, `no_sender`, and `no_completion`
  diagnostics. Do not replace those with credit until a documented GPU-P/DDA
  sender and display completion source exists.
- The provider request itself must be complete before the fail-closed result is
  accepted as useful evidence. Require
  `d3d12_display_bind_request_metadata_matrix` with complete device, resource,
  allocation, dimensions, format/modifier, adapter LUID, source/resource
  generation, and sync/fence metadata; the same row must keep present ids,
  native-present credit, and OpenGL-submit credit at zero until the real
  GPU-P/DDA sender exists.
- Treat the display-bind request as a source-owned pending object before any
  sleepable host sender runs. Publish the pending id and source/resource
  generations before dropping `fb_state.lock`, resolve it on provider return,
  and cancel it on source unregister or owner close. Validate lifecycle and
  revalidation separately: `d3d12_display_bind_pending_lifetime_matrix` proves
  pending entries drain with zero credit, while
  `d3d12_display_bind_generation_revalidation_matrix` proves the live
  bind-contract source/resource generation and pinned resource generation
  survived the provider lock drop.
- Keep the future real-sender publication contract explicit even while the
  sender is absent. `d3d12_display_bind_provider_pending_publication_matrix`
  must be backed by provider-owned kernel stats, not hardcoded validator
  placeholders: publication attempts, publish-before-send, transport pending
  id, command id, transaction id, channel, completion-demux registration,
  resolve/cancel, and ref release. Until a documented GPU-P/DDA sender exists,
  publication attempts may advance, the sender-owned fields stay zero, and the
  row must be `PASS_FAILCLOSED` with
  `provider_no_host_abi=1`, `provider_no_sender=1`, and
  `provider_no_completion=1`.
- Also require the no-send preflight handoff row. A sampled fail-closed
  provider must emit `d3d12_display_bind_provider_no_send_preflight_matrix`
  with complete request metadata, `provider_pin_revalidated=1`,
  source/resource generation matches, `preflight_ready=1`, zero send and
  completion-demux attempts, nonzero no-ABI/no-contract blocked counters, no
  host-saw packet, no transport source, zero present/completed ids, and zero
  native-present/OpenGL-submit credit. This is the replacement point for a
  future documented GPU-P/DDA sender, not proof that one exists.
- The fail-closed provider pending row must use provider-owned lifetime
  fields, not validator inference: provider owner/source/resource generations
  must match the kernel pending owner/source/resource generations, while
  `no_host_abi_cancelled`, `no_host_abi_refs_released`,
  publish-before-send, transport pending id, command id, transaction id,
  channel, completion demux, and native-present/OpenGL-submit credit remain
  zero until a documented sender exists. Pair this with
  `d3d12_display_bind_stale_source_zero_credit_matrix`; stale/after-release
  rejects may advance, but any late present/completed ids must remain zero.
- WSL 6.6.87 and 6.18 dxgkrnl source audits found shared-resource,
  present-history, and sync-file primitives, but no Linux UAPI display-bind
  ioctl and no exposed Linux display-completion handler for binding a D3D12
  resource to scanout. Keep
  `d3d12_display_bind_success_shape_matrix` and
  `d3d12_display_bind_query_fields_matrix` as the current query/success-shape
  guards: they must preserve source/resource generation, provider status/block
  reason, completion source, dirty metadata, host-ABI/sender/completion
  presence, no-host/no-sender/no-completion diagnostics, pin revalidation, and
  zero native-present/OpenGL-submit credit until a documented GPU-P/DDA sender
  replaces the fail-closed provider.
  The May 26, 2026 follow-up audit also found no WSL Linux UAPI ioctl, VMBus
  sender, or host-to-VM demux that binds a D3D12 resource/allocation/fence to
  scanout and returns source/resource-correlated present completion. Treat
  `VM_PKT_COMP` transaction replies and `PROPAGATEPRESENTHISTORYTOKEN` as
  zero-credit telemetry until a provider-owned sender/completion contract
  exists.
- A future provider-success result must pass the full source-local accept
  shape before `fb_dxg_present.c` may copy out native ids: provider status
  success, real transport, scanout-bind operation, host ABI present, sender
  present, display completion present, revalidated pins, no negative provider
  diagnostics, zero block reason, matching source/resource generations,
  nonzero present id, completed >= present id, and display completion source.
  The current fail-closed provider deliberately returns zero ids plus
  no-host/no-sender/no-completion, so validators should keep
  `d3d12_display_bind_id_shape_matrix`,
  `d3d12_provider_credit_gate_matrix`, and
  `wsl_standard_alloc_not_display_bind_matrix` green with zero native-present
  and OpenGL-submit credit.
- `FB_GPU_BACKEND_F_OPENGL_SUBMIT` remains false on Hyper-V until the native
  present dependency chain and the finite 480p FPS gate both pass.
- Treat the animated WebKit fixture as a liveness probe until the compositor
  supplies content-owned CRC/frame/hash evidence tied to the same
  native-present completion. `webkitgpusmoke` should require nonzero content
  CRC, frame counter, frame hash, `NATIVE_PRESENT_COMPLETE` content states,
  and visible/native content credits before `webkit_gpu_contract_matrix ok=1`;
  the Hyper-V validator may run the fixture, but must keep the WebKit
  acceleration gate closed when that native/content evidence is absent.
- D3D12 content-progress evidence should come from compositor-owned
  `wlcomp_buffer` sample fields. Reset them on each D3D12 shared-resource
  commit, tie them to display-bind present/completed/resource generation, and
  leave `source_owned=0` plus zero visible/native credit until a real
  non-readback native-present content sampler fills CRC/frame/hash values.
  The finite FPS and WebKit gates require both content CRC/frame counters and
  compositor-owned frame-hash progress; title/FPS overlay movement, fixture
  title liveness, or CRC-only samples are not enough.
  Effective FPS must also be clamped by compositor-owned content cadence:
  sample-window and visual-window content-frame FPS are part of the minimum
  alongside native/display completion FPS, so app-loop or displayed FPS cannot
  pass when visible content advances at one-digit cadence.
  Final credit must use visible compositor-owned fields only:
  `d3d12_visible_content_crc`, `d3d12_visible_content_frame`,
  `d3d12_visible_frame_hash`, and
  `d3d12_content_progress_source_owned=1`. Client/app hashes are diagnostic
  context and must not open FPS/WebKit gates.
- FPS/WebKit consumers must also require exact identity between content
  progress, DXG present ids, display-bind ids, completion ids, and resource
  generation. They must also require current-run content-progress identity
  fields and final-handoff present/completed/resource-generation equality
  before accepting FPS or WebKit acceleration evidence. Generic
  KMS/vblank/OUT_FENCE/display-wait progress is not native D3D12 completion
  and should be reported by an explicit zero-credit
  `d3d12_native_completion_not_kms_matrix`.
- `webkitgpusmoke` emits
  `webkit_inprocess_contract_gate_matrix` from the pure-C contract validator.
  Keep that row zero-credit on Hyper-V until `FB_GPU_BACKEND_F_OPENGL_SUBMIT`,
  nonzero display-bind/native-present ids, shared-resource/fence identity, and
  compositor-owned content identity all pass together; the shell WebKit
  validator remains responsible for the downstream finite-480p FPS artifact.
- KMS/DRM remains a generic software-present compatibility path until a real
  DDA/Nouveau display engine exists. `gpu_kms_present_fb()` must try the
  native-present discriminator first and record reject reasons, then fall back
  to `fb_blit_from_bo_format()` without granting `kms_present_nouveau_hw`,
  native-present credit, or OpenGL-submit credit. Accepted DDA/Nouveau PCI
  probes may publish BAR/DMA/IRQ diagnostics and a fail-closed display-create
  attempt, but not heads/connectors/vblank/flip-completion success until those
  are backed by real hardware programming.
  Generic display completion must not be printed as D3D12 native-present
  credit; use `generic_display_last_complete` for ordinary scanout progress
  and keep `d3d12_native_present_credit=0`. KMS page-flip/vblank diagnostics
  should distinguish software display completion from future native hardware,
  with native hardware fields zero on the current path.
  Keep GBM scanout support in lockstep with KMS primary-plane support: NV12 is
  metadata/import-render only until the primary plane can scan it out without
  software fallback.

### Source Layout

- GPU/framebuffer implementation now enters through
  `kernel/dev/fb/module.c`, with owned fragments under `kernel/dev/fb/`.
  Route scanout/fbdev changes to `fb_scanout.c`, BO/GEM/TTM/dmabuf changes
  to `fb_bo_ttm_dmabuf.c`, DRM/KMS changes to `fb_drm_core_kms.c` or
  `fb_kms_atomic.c`, syncobj/PRIME/virtgpu changes to
  `fb_syncobj_prime_virtgpu.c`, Hyper-V present bridge changes to
  `fb_dxg_present.c`, and Nouveau compatibility changes to
  `fb_nouveau.c`.
- Hyper-V implementation now enters through `kernel/dev/hyperv/module.c`, with
  owned fragments under `kernel/dev/hyperv/`. Route VMBus/SynIC work to
  `hyperv_vmbus_core.c`, synthetic input/storage/network/video work to
  `hyperv_synth_devices.c`, vPCI work to `hyperv_vpci_config.c`, DXG
  object/shared-resource lifetime to `hyperv_dxg_objects_shared.c`, D3DKMT
  ioctl shaping to `hyperv_dxg_ioctls.c`, and DXG status/readiness exports to
  `hyperv_dxg_device.c` or `hyperv_dxg_status_device.c`.

### WSL And Linux Reference Split

- WSL2 `dxgkrnl` is the reference for Hyper-V DXG/D3DKMT object and wire
  behavior. In the Microsoft WSL2 Linux kernel it lives under
  `drivers/hv/dxgkrnl`, with UAPI in `include/uapi/misc/d3dkmthk.h`; do not
  look under `drivers/gpu/dxgkrnl`.
- Use WSL `dxgprocess`, `hmgrtable`, `dxgprocess_adapter`,
  `dxgsharedresource`, `dxgsharedsyncobject`, `dxgsyncfile`, and `dxgvmbus`
  code as the comparison anchors for `/dev/dxg` process lifetime, typed handle
  tables, shared-resource sealing, monitored-fence sync-file behavior, and
  D3DKMT packet layout.
- For `CREATEALLOCATION` and `OPENRESOURCE` unwind paths, keep cleanup packets
  on the same per-open `dxgprocess` host handle as the successful create/open
  packet. A global process handle in `DESTROYALLOCATION` cleanup is a WSL
  parity bug even if the helper usually succeeds on a single-process smoke.
- For `CREATEALLOCATION` and `OPENRESOURCE` late user-publication failures,
  keep a pure-C copyout-fault matrix in `dxgprobe`: force `-EFAULT` after the
  host succeeds, prove same-process `DESTROYALLOCATION`, prove no local handle
  was published by retrying destroy on the returned host handles, and prove
  existing-sysmem active page pins return to the pre-fault count.
  `OPENRESOURCE` should fault `open_alloc_info` before the final args copyout
  so cleanup uses the host-returned resource handle, not the still-zero user
  `req.resource` mirror. Also require parent rollback fields:
  `parent_same=1`, `parent_refs_balanced=1`, `parent_child_unlinked=1`, and
  `sealed_generation_coherent=1`.
- For WSL-like shared resources, treat the explicit resource metadata record
  and per-allocation records as the canonical seal/query/open lifetime model.
  The older flat fields can remain as compatibility mirrors only while
  validators require `shared_model_coherent=1` and `dxg_sharedresource_model`
  reports valid records that match the mirror.
- Preserve WSL-style shared-resource NT/global-share metadata through the
  parent object and each opened child. A display-bind pin may not succeed from
  a by-value clone or a child with stripped sharing state; use
  `dxg_display_bind_pin_diag` to compare typed fd kind, opened child, parent
  id, global share, sealed generation, and pinned refs before trusting the
  future sleepable provider.
- Current shared-resource parent work has a scaffold phase and a semantic
  phase. The scaffold may expose `dxg_sharedresource_parent`, parent id/ref/
  opened-child counters, and sealed allocation `num_pages`/`cached` metadata,
  but it does not close WSL parity while resource fds still deep-clone
  `hvdxg_tracked_resource` state. The semantic phase must introduce a real
  `dxgsharedresource`-style parent whose metadata and children outlive fd
  closes according to refs.
- The semantic parent phase is considered covered only when
  `shared_resource_parent_lifetime_matrix` and
  `shared_resource_sealed_alloc_metadata_matrix` are emitted by `dxgprobe` and
  PASS. Those rows prove fd refs, child refs, parent id stability, close-fd
  survival, and sealed allocation pages/cached/flags stability across
  share/query/open/close.
- For normal DXG object handles, keep the WSL `hmgrtable` shape visible:
  index-addressed lookup, destroyed-entry stale rejection, unique bump on free,
  free-count/head/tail diagnostics, and minimum-free expansion. Do not regress
  this back to unordered linear scans or silent best-effort tracking drops.
- For DXG teardown, keep the WSL order explicit. Device/process cleanup should
  stop the device, drop sync objects locally, handle allocations/resources,
  drop contexts and their HW queues, drop paging queues, then destroy the
  device and finally the process. Explicit destroy ioctls that WSL makes stale
  before host destroy should untrack local handles before sending the host
  packet. `/dev/dxg` must expose `d3dkmt_cleanup_wsl_order` with `valid:1`
  for the final-close validator.
- For WSL-style NT shared-object fds from `LX_DXSHAREOBJECTS`, require one
  object per call, preserve resource-vs-sync fd kind, set close-on-exec on the
  returned fd, and prove copyout-failure cleanup separately from normal close.
- For DXG sync-file parity, be precise: xv6 currently has a custom
  `anon_inode:sync_file` fd that follows the WSL DXG lifecycle shape, not a
  full Linux `sync_file`/`dma_fence`. Keep validators for create-copyout
  fd/event cleanup, open-copyout host sync-object destruction, temporary
  `WAITSYNCFILE` sync-object destruction, child-process open from the same fd,
  and `dxg_syncfile_lifetime` host-event/live-count balance.
- The top-level Hyper-V DXG validator must consume WSL lifetime rows, not just
  leave them as optional probe output: `dxg_process_mem_lifetime_matrix`,
  `dxgprocess_adapter_matrix`, `handle_lifetime_stale_matrix`,
  `shared_resource_parent_lifetime_matrix`,
  `shared_resource_sealed_alloc_metadata_matrix`, `sync_file_matrix`, and both
  `dxg_syncfile_*_unwind_matrix` rows.
- For native Wayland/D3D12 fence acquire, the chosen contract is WSL-style DXG
  sync-file acquire, not direct D3D12 fence fd import. `d3d12sharedsmoke`
  should emit `d3d12_fence_sharing_policy_matrix` and
  `d3d12_fence_sharing_validation_matrix` with
  `decision=dxg_syncfile_acquire`, same-adapter WSL trace provenance, direct
  D3D12 fence fd use disabled, and zero native-present/OpenGL-submit credit
  until the real display handoff exists.
- For runtime D3D12 Wayland resource-buffer admission, distinguish the shared
  fd's canonical creator-side resource handles from the compositor's per-open
  `OPENRESOURCEFROMNTHANDLE` handles. FB present-source registration should
  validate the compositor `/dev/dxg` owner table entry against the shared fd's
  global share and sealed metadata generation; it should not require the fd's
  stored creator handles to equal the compositor-opened resource/allocation.
  The focused runtime C gate is
  `d3d12sharedsmoke --runtime --allow-failclosed-present`: it must prove real
  `LX_DXCREATESYNCFILE` export, `LX_DXOPENSYNCOBJECTFROMSYNCFILE` import,
  same-LUID compositor resource/fence open,
  `d3d12_wayland_resource_buffer_admission_matrix`,
  per-open present-source register success, expected fail-closed
  `EOPNOTSUPP` at the missing `dxg-resource-scanout-bind` host ABI, drained
  callbacks/releases, and zero native-present/OpenGL-submit credit.
- For `dxgprocess` lifetime, keep reuse keyed by TGID like WSL. Do not reuse a
  retained host process handle across TGIDs; if a host workaround is ever
  necessary, expose it as non-parity diagnostics instead of sharing namespaces.
- For D3DKMT ioctls, keep the TGID ownership gate in the common `/dev/dxg`
  dispatch before per-open host-process binding, adapter alias creation, user
  copyout, or packet forwarding. Discovery/bind ioctls (`ENUMADAPTERS*`,
  `OPENADAPTERFROMLUID`, and `QUERYADAPTERINFO`) still bind in their own
  WSL-order paths, but inherited fds from a different TGID must fail before
  those paths can mint local aliases or query adapter data.
- For WSL packet-shape parity, keep the source comparison tight. WSL leaves
  `CREATEDEVICE.cdd_device` zeroed, sizes `MAKERESIDENT` as the base command
  plus allocation handles with no extra local tail dword, and forwards VGPU
  D3DKMT packets with the owning `dxgprocess` host handle. If a path cannot
  yet match WSL CPU-event signal or async-message semantics, make it an
  explicit active plan item with validator evidence instead of burying it in
  generic unsupported logging.
- For same-adapter WSL replay coverage, keep `dxgprobe --wsl-trace-replay`
  tied to the selected OPENADAPTER LUID and the current NVIDIA trace reference.
  The section is not covered unless `wsl_trace_replay_packet_matrix` rows prove
  each replayed D3DKMT packet shape and `wsl_trace_replay_signature` reports
  `status=PASS`.
- For shared-resource/shared-sync regression coverage, treat the focused core
  runner as the index: `shared_resource_seal_provenance_matrix`,
  `shared_mutation_rejection_matrix`, `shared_lifetime_record_matrix`,
  `resource_import_negative_matrix`, `opensync_layout_source_matrix`,
  `sync_import_negative_matrix`, `sync_file_matrix`, and the sync-file unwind
  rows must all stay green before editing native present or WebKit gates.
- For create-path publication faults, keep host cleanup before local handle
  publication WSL-shaped. `dxgprobe --create-publication-faults-validate`
  should fault the final result page for `CREATEDEVICE`,
  `CREATECONTEXTVIRTUAL`, and `CREATEHWQUEUE`, then require the same owning
  host process handle to destroy the host-created object and require stale
  local destroy retries to fail for the unpublished handles.
- For WSL `enqueue_cpu_event` signal paths, `SIGNALSYNCHRONIZATIONOBJECT` and
  `SIGNALSYNCHRONIZATIONOBJECTFROMGPU2` should allocate an eventfd-backed host
  event, send that host-event id in the VMBus `cpu_event_handle`, keep
  remove-after-signal ownership with the host-event table, and remove/fput on
  send failure. Validators should require `sync_signal_cpu_event_matrix`,
  `sync_gpu2_cpu_event_matrix`, and `dxg_synccpuevent_signal`, not only the
  absence of `-ENOTSUP`.
- For WSL `hdr.async_msg` parity, keep the command family exact:
  `SUBMITCOMMAND`, `SIGNALSYNCOBJECT`, `WAITFORSYNCOBJECTFROMGPU`, and
  `SUBMITCOMMANDTOHWQUEUE` can use `dxgvmb_send_async_msg()` when the host
  advertises async messages; `WAITFORSYNCOBJECTFROMCPU` remains synchronous.
  xv6 should expose both the send decision and the packet shape through
  `dxg_async_message_matrix` and `dxg_async_send_last`, with sync fallback
  reported explicitly when the host capability is absent.
- For broad packet-marshalling closure, prefer one aggregate pure-C matrix over
  loose status rows. `dxg_packet_shape_matrix` should prove command/result
  lengths, owner process handles, private blob order, first resource/sync/HWQ
  handles, async or sync-fallback send policy, and create-publication unwind
  counters before the packet-shape plan row is checked.
- For NT shared-object import coverage, validate both directions of fd kind
  separation: resource query/open must reject sync fds, and sync open must
  reject resource fds. Keep this as a pure-C `dxgprobe --import-negative`
  contract before using any shared handle as native-present evidence.
- For NT shared-object fd publication, prove the WSL-style copyout failure
  path separately from ordinary close: a bad `shared_handle` pointer must
  deallocate the just-installed fd, run last-fd close cleanup, drop the
  NT shared-object ref to zero, and still allow a subsequent valid share of the
  same resource or sync object.
- WSL `dxgkrnl` is not the DRM/KMS/Nouveau reference. Use Linux DRM, GEM, TTM,
  `dma_fence`, `dma_resv`, KMS atomic, PCI runtime, and Nouveau sources for
  `/dev/dri`, PRIME/dma-buf, scanout, and Nouveau compatibility work.
- Keep these tracks separate in plans and validators: WSL parity can close DXG
  transport/object gates, but it cannot by itself prove native display handoff,
  KMS scanout, Nouveau command submission, FPS, WebKit acceleration, or
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT`.

#### GPU Module Index

- `kernel/dev/fb/module.c`: unity root for the framebuffer/GPU module.
- `kernel/dev/fb/fb_common.c`: common includes shared by x86 and stubs.
- `kernel/dev/fb/fb_internal.c`: GPU constants, shared state, boot logo, and
  internal prototypes.
- `kernel/dev/fb/fb_scanout.c`: framebuffer scanout, blit, scanout map, flush,
  and display-completion accounting.
- `kernel/dev/fb/fb_bo_ttm_dmabuf.c`: BO/GEM objects, TTM placement,
  reservations, and dma-buf metadata/lifetime.
- `kernel/dev/fb/fb_dxg_present.c`: Hyper-V DXG present-source registration,
  commit, query, fail-closed native-present diagnostics, and the
  `FB_GPU_DXG_PRESENT_BIND_CONTRACT_QUERY` skeleton that ties native handoff to
  a registered source, source/resource generations, required metadata, selected
  GPU-P/DDA lane, and display-completion source.
  Verified sources pin their owning `/dev/dxg` fd plus typed
  `anon_inode:dxgresource` fd at registration time, then unpin on source
  unregister/owner close so future sleepable display-bind submissions cannot
  race fd close/reuse or process/resource lifetime.
  The selected lane is GPU-P/DDA `dxg-resource-scanout-bind`, with WSLg display
  channel unavailable, synthvid limited to GPA-dirty VRAM, no custom host tool,
  and `dxg_present_lane_selection_matrix` as the fbstat evidence row.
  `present_source_software_path_rejection_matrix` is the pure-C zero-credit
  proof that framebuffer blit, CPU map/readback, DRI software present,
  copy-export fallback, and callback-only/release-only paths cannot satisfy
  this native-present contract while the real display-bind lane is missing.
  `d3d12_shared_resource_fd_lifetime_matrix`,
  `d3d12_present_source_admission_matrix`,
  `d3d12_acquire_fence_lifetime_matrix`, and
  `d3d12_present_bind_contract_failclosed_matrix` are the current pure-C
  fail-closed D3D12 resource/fence/present-source validators. They prove fd
  lifetime, same-adapter admission, D3DKMT metadata, monitored-fence acquire
  metadata, stale/foreign source rejection, software-path rejection, cleanup
  balance, and zero native-present/OpenGL-submit credit; they do not prove
  native display completion.
  `d3d12_wayland_resource_buffer_admission_matrix` is compositor-side
  intermediate evidence that the Wayland D3D12 buffer path accepted a
  same-LUID resource/fence import. `d3d12_wayland_present_failclosed_identity_matrix`
  records the same client/resource/generation when GPU-copy proof exists but
  native display completion remains absent. Both rows are diagnostic and must
  be rejected by strict DXG/FPS/WebKit gates until nonzero present/completion,
  callbacks, releases, content progress, and backend OpenGL-submit all pass.
- `kernel/dev/fb/fb_fd_sync.c`: exported BO/fence/sync fd file operations,
  poll, close, and callback lifecycle.
- `kernel/dev/fb/fb_device_ioctl.c`: `/dev/fb0` and `/dev/gpu0` ownership,
  open/close, read/write, and `FB_GPU_*` ioctl dispatch.
- `kernel/dev/fb/fb_drm_core_kms.c`: DRM core helpers, legacy ioctls, KMS
  resources, properties, planes, CRTC, connector, and framebuffer metadata.
- `kernel/dev/fb/fb_kms_atomic.c`: KMS framebuffer lifecycle, leases,
  modeset, page-flip, vblank, and atomic commit/fence behavior.
  Atomic `OUT_FENCE_PTR` validation should use
  `atomic_out_fence_provenance_matrix out_fence_source=display_completion`
  with `out_fence_display_correlated=1`,
  `out_fence_software_scanout_correlated=0`, and zero native/OpenGL-submit
  credit. `IN_FENCE_FD` validation must still prove stale, duplicate, future,
  and nonblocking rejection plus balanced fd refs.
- `kernel/dev/fb/fb_syncobj_prime_virtgpu.c`: DRM syncobj/timeline,
  sync-file bridge, dumb BO, PRIME, and virtgpu compatibility ioctls.
- `kernel/dev/fb/fb_nouveau.c`: Nouveau PCI facade and Nouveau private DRM
  ioctl compatibility.
- `kernel/dev/fb/fb_drm_dispatch.c`: DRM ioctl switch, render-node file ops,
  and DRM mmap.
- `kernel/dev/fb/fb_init_panic.c`: GPU device registration, framebuffer init,
  firmware framebuffer setup, and panic screen renderer.
- `kernel/dev/fb/fb_non_x86.c`: non-x86 framebuffer/GPU stubs.

#### Hyper-V Module Index

- `kernel/dev/hyperv/module.c`: unity root for the Hyper-V module.
- `kernel/dev/hyperv/hyperv_common.c`: common includes shared by x86 and
  stubs.
- `kernel/dev/hyperv/hyperv_defs_state.c`: Hyper-V/VMBus/DXG constants, wire
  structs, and shared per-channel/device state.
- `kernel/dev/hyperv/hyperv_vpci_config.c`: Hyper-V vPCI config-window backend
  and bus-relations parsing.
- `kernel/dev/hyperv/hyperv_dxg_state_diag.c`: DXG adapter admission, WSL
  parity diagnostics, payload caches, and status text helpers.
- `kernel/dev/hyperv/hyperv_dxg_status_device.c`: `/dev/dxg` status read path,
  IO-space/MMIO helpers, and existing-sysmem mapping helpers.
- `kernel/dev/hyperv/hyperv_dxg_objects_shared.c`: DXG process/object/handle,
  allocation/resource/sync tracking, NT shared resource/sync fds, and teardown.
- `kernel/dev/hyperv/hyperv_dxg_ioctls.c`: D3DKMT ioctl validation,
  ownership checks, packet shaping, forwarding, and completion handling.
- `kernel/dev/hyperv/hyperv_dxg_device.c`: DXG cdev/file operations and public
  transport/D3DKMT readiness exports.
- `kernel/dev/hyperv/hyperv_vmbus_core.c`: Hyper-V CPUID/MSR, SynIC, VMBus
  ring buffers, packets, events, completions, and DXG send/wait helpers.
- `kernel/dev/hyperv/hyperv_synth_devices.c`: synthetic HID/keyboard,
  StorVSC, NetVSC, SynthVid, and vPCI channel helpers.
- `kernel/dev/hyperv/hyperv_init_public.c`: Hyper-V channel opening, public
  init entry points, video status/dirty APIs, and startup sequencing.
- `kernel/dev/hyperv/hyperv_non_x86.c`: non-x86 Hyper-V stubs.

- Hyper-V DXG has a usable transport and D3DKMT readiness lane: `/dev/dxg`
  exposes global/vGPU transports, adapter enumeration/open works, and
  `fbstat`/`FB_GPU_BACKEND_QUERY` distinguish `DXG_TRANSPORT`, `D3DKMT`, and
  `OPENGL_SUBMIT`.
- Stable `dxgprobe` coverage includes adapter query, video memory query, device
  creation, paging queue creation, allocation create/destroy, residency/evict,
  GPUVA map/reserve/free, allocation priority/offer/reclaim/cache invalidate,
  CPU monitored-fence signal, shared resource NT fd query/open, shared sync NT
  fd open, owner isolation, leak-close cleanup, and unsupported ioctl handling.
- Hyper-V DXG allocation handling now includes existing-sysmem page pinning,
  PFN-list forwarding, cleanup-time unpinning, and late-failure unwind for host
  allocations when post-host-create local copyout/tracking fails.
- `/dev/dxg` per-open tracking is growable for devices, contexts, HW queues,
  paging queues, sync objects, allocations, resources, and GPUVA reservations.
  This is a baseline improvement, not the final WSL-style `dxgprocess` object
  graph.
- The general render substrate has `/dev/dri/renderD128`, libdrm/GBM discovery,
  PRIME-style BO fd export/import, render-fd ownership cleanup, pollable fence
  fd accounting, and no-leak validation through `gpubuftest`, `gbmtest`,
  `drmprimeprobe`, and `drmgpuprobe`.
- On GPU-P-only Hyper-V images, Nouveau must remain fail-closed unless a real
  BAR-backed DDA NVIDIA PCI function is accepted. `fbstat` should emit
  `nouveau_gpup_failclosed_matrix` with no fake BAR/DMA/IRQ/getparam/native
  present/OpenGL-submit credit. Accepted DDA PCI facts are still only
  diagnostics for native display until Linux-shaped display create, nonvirtual
  heads/connectors, hardware vblank IRQ, KMS `NOUVEAU_HW` page flip, and
  hardware flip completion all correlate in the same validation lineage.
- `nouveau_pci_runtime_contract_matrix` is the Linux-shaped PCI runtime
  diagnostic row. On GPU-P-only images it should pass with DMA/coherent masks
  not configured, BAR claims not attempted, no IRQ handler or delivery,
  runtime PM/remove-path deferred, and zero native-present/OpenGL-submit credit.
  On accepted DDA hardware it is still diagnostic until real MSI/legacy IRQ
  delivery, runtime PM, remove, and engine/native-present behavior are proven.
- `nouveau_pci_runtime_interface_matrix` keeps the same split at interface
  granularity. On GPU-P-only Hyper-V it should report resource tree and DMA
  mapping as `GPU_P_FAIL_CLOSED`, MSI/MSI-X not attempted, IRQ absent,
  runtime PM/remove/hot-remove deferred, native engine absent, and zero
  native-present/OpenGL-submit credit. A DDA/Nouveau device can only move this
  row forward after real BAR, DMA, IRQ, runtime-PM, engine, and present
  evidence exists.
- For Nouveau/PCI work on GPU-P-only Hyper-V, keep `accepts=0` and expose no
  fake BAR, DMA, IRQ, or native engine state. A real DDA path must claim BARs
  before `pci_iomap()`, report owner/unclaimed resource counters, arm a real
  IRQ handler before accepting the device, and count IRQ delivery only from
  handler execution.
- `dxg_resource_scanout_bind_host_abi_matrix` is the source-audited native
  present blocker row. It must say the selected lane is
  `gpup_dxg_scanout_bind`, no custom host tool is used, WSL dxgkrnl has no
  display-bind ioctl, synthvid is GPA-dirty-only, and D3DKMT shared-resource
  admission still grants zero display target, present id, native-present credit,
  or OpenGL-submit credit.
- `wsl_standard_alloc_surface_abi_matrix` proves only WSL VMBus private-data
  layout parity for `DDIGETSTANDARDALLOCATIONDRIVERDATA`: the shared-primary,
  shadow, staging, and GDI surface union arms are present. This is not a
  native display handoff and must keep `display_bind_ioctl=0` plus zero
  native-present/OpenGL-submit credit.
- `d3d12_present_resource_fd_typed_admission_matrix` is the FB-to-DXG
  admission guard before native present. It should prove the resource fd is a
  typed WSL-style `anon_inode:dxgresource`, sealed shared-resource records are
  visible, the fd metadata matches the D3DKMT handles/allocation count, the
  bind-contract resource generation comes from the sealed resource generation,
  stale source cleanup works, and native-present/OpenGL-submit credit stays
  zero.
- `d3d12_display_bind_pin_lifetime_matrix` is the WSL-style lifetime guard
  for the future sleepable display-bind provider. It should prove verified
  sources pin both the `/dev/dxg` owner fd and typed resource fd, carry nonzero
  resource/process generations, process refs, parent id/ref/opened-child
  counters, balance unpins after source cleanup, and keep native-present/
  OpenGL-submit credit at zero.
- `d3d12_present_syncfile_preopen_matrix` is the acquire-fence bridge guard
  before native present. It should prove a monitored fence can be exported as
  a WSL-style sync-file fd, reopened into a D3DKMT sync object, used for
  present-source wait metadata, and rejected for wrong fd kinds, while still
  granting zero native-present/OpenGL-submit credit.
- `d3d12_native_completion_zero_credit_matrix` and
  `d3d12_display_bind_absent_matrix` are the pre-native completion guards.
  They must show absent display bind/transport, zero present/completion ids,
  blocked or deferred callback/release ordering, required per-client
  generation matching, and zero native-present/OpenGL-submit credit.
- `d3d12_native_completion_lifetime_matrix` is the order/lifetime guard. It
  may pass preflight before any provider submit, but after a fail-closed
  provider submit it must show `provider_no_completion=1`; after a future
  successful provider submit it must require nonzero display-correlated
  present/completed ids, callback/release after completion, close-before-signal
  cancellation, cleanup balance, and no provider negative diagnostics.
- `d3d12_display_bind_stale_source_zero_credit_matrix` is the stale-source
  guard. Owner close or explicit unregister must clear source/global
  display-bind ids, reject after-close queries, avoid late completion credit,
  and keep native-present/OpenGL/WebKit credit at zero.
- `d3d12_dda_nouveau_separate_display_not_bind_matrix` is the DDA split guard.
  A separate DDA/Nouveau PCI display path is zero-credit for D3D12 native
  present because it is not the D3D12 resource scanout-bind path. Keep it in a
  native-display namespace unless a future source documents an explicit bridge
  from a D3D12 resource generation into the Nouveau display engine.
- `foreign_prime_import_gap_matrix` is the DDA/Nouveau PRIME bridge guard.
  Local xv6 dma-buf/PRIME imports are not a D3D12 foreign-resource import or a
  Nouveau scanout-bind handoff; valid foreign fd rejects, zero D3D12 import
  credit, zero Nouveau scanout-bind import credit, and zero native/OpenGL credit
  must stay visible until a real Linux-shaped external import bridge exists.
- `public_present_api_not_guest_bind_matrix` is the public Windows/WSLg/RDP
  source-audit guard. ReactOS/Windows KMT present declarations, DirectX shared
  handles, and HWND-oriented sharing-contract APIs are not a guest VMBus or
  WSLg/RDP display-bind protocol; RDP-style frame/copy/dirty transport remains
  zero-credit for native D3D12 present until a source/resource-correlated
  scanout completion contract exists.
- `d3d12_display_bind_host_abi_discovery_matrix` is the bounded host-ABI
  source-audit gate. It must prove no custom host tool, no WSL display-bind
  ioctl, WSLg/FreeRDP absent, RDP copy/dirty-frame only, GPU-P sender contract
  zero, completion-demux contract zero, DDA/Nouveau D3D12 import/scanout/
  hardware flip absent, provider fail-closed, transport/present/completed ids
  zero, and native-present/OpenGL/WebKit credit zero.
- `d3d12_negative_abi_manifest_matrix` is the canonical source-audited
  negative ABI manifest. It must keep WSL `d3dkmthk.h`/`dxgvmbus.c`
  display-bind absent, present-history/redirected-flip/BLT/HWQUEUE enum
  candidates classified as telemetry or normal submit rather than scanout bind,
  synthvid classified as GPA dirty-rect display, DDA/Nouveau classified as a
  separate PCI display path, and host packet/completion/native-present credit
  at zero.
- For sampled fail-closed provider submits,
  `d3d12_display_bind_provider_pending_publication_matrix` should show the
  no-host-ABI pending path resolved and refs released
  (`resolved_or_cancelled=1`, `refs_released=1`,
  `no_host_abi_cancelled=1`, `no_host_abi_refs_released=1`) while sender-owned
  publish-before-send, command/channel, completion demux, native-present, and
  OpenGL-submit fields remain zero.
- The matching `d3d12_display_bind_provider_no_send_preflight_matrix` should
  prove the same sample was ready to send but intentionally performed no send:
  `preflight_ready=1`, `send_attempts=0`,
  `send_blocked_no_host_abi>0`, `completion_demux_attempts=0`, and
  `completion_demux_blocked_no_contract>0`, with source/resource generation
  matches and zero host-saw packet/native-present/OpenGL-submit credit.
- `d3d12_display_bind_backend_boundary_matrix` is the canonical boundary row.
  It should mirror kernel `dxg_display_bind_*` stats and keep the current
  GPU-P-only path at `backend=gpup_dxg_scanout_bind`, transport absent,
  completion source required, present/completed ids zero, custom host tooling
  zero, and zero native-present/OpenGL-submit credit.
- `hyperv_opengl_submit_gate_matrix` is the backend flag invariant. On Hyper-V
  it must remain `backend_gate=closed` with `backend_opengl_submit=0` until
  native present, finite FPS, and the WebKit shared-surface contract are all
  proven from current-run evidence.
- For DDA/Nouveau `GETPARAM`, keep provenance split: PCI vendor/device,
  bus type, BAR/VRAM aperture, chipset, and VRAM base are DDA PCI facts;
  `HAS_BO_USAGE`, `HAS_PAGEFLIP`, `EXEC_PUSH_MAX`, `VRAM_USED`, and
  `HAS_VMA_TILEMODE` are local driver capabilities; unsupported engine/timer
  facts fail closed until sourced. `nouveau_getparam_ddafacts_matrix` must
  report zero synthetic hardware facts before this plan row is considered
  closed.
- Legacy Nouveau channel work is tracked separately from NVIF. The old
  `CHANNEL_ALLOC`/`GROBJ_ALLOC`/`NOTIFIEROBJ_ALLOC`/`GPUOBJ_FREE` ioctls should
  maintain per-open channel/object state, reject duplicates and unsupported
  classes, and report `nouveau_channel_object_matrix`. Publish channel/VM state
  only after successful ioctl copyout, so `-EFAULT` cannot leave active state
  behind. Mesa's newer NVIF object/subchannel path is still governed by the
  active NVIF plan row.
- NVIF support must not advertise made-up engine classes. Until the Nouveau
  class hierarchy is real, `DRM_NOUVEAU_NVIF` should parse v0 SCLASS/NEW/DEL
  and method/register/map/notify operations, return an empty SCLASS list, reject
  NEW and unsupported operations explicitly, and report
  `nouveau_nvif_failclosed_matrix`.
- Non-empty Nouveau submission remains fail-closed until a DDA command engine
  exists. Keep zero-op/fence-only `PUSHBUF`, `EXEC`, and `VM_BIND` separate
  from non-empty command buffers; `nouveau_submit_failclosed_matrix` should show
  non-empty pushbuf/exec/vm-bind rejects and zero native-present/OpenGL-submit
  credit.
- `nouveauabitest` is the Mesa/libdrm Nouveau smoke gate. DDA runs must reach
  libdrm winsys/device-info plus channel/BO/map/PRIME paths; GPU-P-only runs
  may pass only with `nouveau_mesa_smoke_gate_matrix` showing
  `synthetic_gpup_rejected=PASS` and no Mesa NVIF enablement.
- Wayland/compositor baseline includes standard `zwp_linux_dmabuf_v1` import for
  linear ARGB8888/XRGB8888/NV12, dmabuf feedback, explicit-sync release objects,
  acquire-fence waits with stall recovery, GPU BO present/direct scanout for
  framebuffer BOs, display completion accounting, and screenshot visual checks.
- Generic framebuffer/DRM/KMS diagnostics must stay distinct from
  D3D12/WebKit contract gates. Kernel ioctl trace output should use
  `fb-gpu-trace`; `fbstat` should emit `gpu_diagnostics_separation_matrix`
  with generic DRM/KMS/fb prefixes, DXG-present/WebKit policy as separate
  namespaces, and zero native-present/OpenGL-submit credit from generic
  scanout evidence alone.
- KVM/virtio-gpu/virgl is the current validated OpenGL-submit backend. It owns
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT` today; Hyper-V does not.
  `scripts/gpu-validate.sh` should prove that with `backend virgl`,
  `backend_opengl_submit 1`, `backend_opengl_submit_gate open`,
  `backend_virgl_opengl 1`, and
  `opengl_submit_backend_separation_matrix ... opengl_submit_credit=1`.
- The desktop 3D demo now launches through `mesademo`/`mesawlegl --demo` with a
  real 640x480 Wayland/EGL window, close/resize handling, and an RTC-based FPS
  overlay drawn inside the GL surface.
- WebKit acceleration is intentionally gated on `FB_GPU_BACKEND_F_OPENGL_SUBMIT`.
  Hyper-V render-node or D3DKMT presence alone must keep WebKit on the stable
  fallback path.
  `wlcomp_launcher` must compare the generated WebKit run id with both
  `d3d12_run_id` and `d3d12_present_identity_compositor_run_id` before it may
  select the D3D12 WebKit environment; `webkit_gpu_contract_matrix` is the
  launcher-side current-run gate row.

### DRM/KMS Validation Baselines

- Treat KMS format support as a two-level contract:
  - framebuffer metadata support means `ADDFB2`/`GETFB2` can store and
    round-trip format, modifier, plane handle, pitch, and offset fields;
  - scanout support means the format is advertised by `GETPLANE` and all
    modeset/present paths can use it without software-only side effects.
- Until native scanout exists for a format, validators should prove unsupported
  `SETCRTC`, page flip, atomic commit, and atomic `TEST_ONLY` reject before:
  - queuing DRM events;
  - taking or waiting in-fence refs;
  - creating, exporting, cleaning, or placeholder-writing out-fences;
  - mutating current KMS framebuffer state;
  - advancing display, DXG-present, native-present, or OpenGL-submit credit.
- The current Hyper-V KMS NV12 baseline is metadata-only: NV12
  `ADDFB2`/`GETFB2` round-trips, primary `GETPLANE` advertises only
  XRGB8888/ARGB8888, `DRM_CAP_ADDFB2_MODIFIERS` advertises only the accepted
  linear metadata contract, and the NV12 scanout/present matrix is
  fail-closed.
- Primary-plane `IN_FORMATS` must match actual scanout, not framebuffer
  metadata breadth. The current valid blob is immutable and advertises only
  XRGB8888/ARGB8888 with `DRM_FORMAT_MOD_LINEAR`; validators require
  `kms_in_formats_blob_matrix ... nv12_scanout=0 nonlinear_modifiers=0
  native_present_credit=0 opengl_submit_credit=0 status=PASS`.
- KMS vblank/page-flip event-source provenance is its own layer:
  - display-correlated timing requires `kms_vblank_synthetic=0`,
    `kms_vblank_display_correlated=1`, and
    `display_completion_correlated=PASS`;
  - those events are still not native-present evidence unless the same run also
    proves real atomic OUT_FENCE/native display handoff completion;
  - Hyper-V must keep `backend_opengl_submit 0` while OUT_FENCE provenance is
    `software_scanout_commit`.
- Native-display readiness is kernel-owned state, not a user-space inference
  from generic KMS success. On GPU-P-only Hyper-V the required rows are
  `native_display_readiness_failclosed_matrix`,
  `nouveau_display_failclosed_matrix`, and
  `kms_present_discriminator_failclosed_matrix`, each with zero
  native-present/OpenGL-submit credit and `reject_reasons=0x7f` until a real
  DDA/Nouveau display object, heads/connectors, vblank source, and hardware
  flip completion exist.
  The Linux-shaped Nouveau display skeleton is more granular than those first
  counters: require `nouveau_linux_display_readiness_matrix` to show absent
  display-engine object, `mode_config`, CRTC/head, encoder/outp,
  primary-plane, outp/connector/head masks, `nvif_head`, HPD/DP IRQ event,
  per-head vblank event, atomic commit-tail, and page-flip completion source
  on GPU-P-only Hyper-V, with only the linear-required policy set.
- Linux-shaped KMS/Nouveau/TTM diagnostics are allowed to pass only as
  fail-closed mismatch rows until the matching native layer exists. Keep rows
  such as `nouveau_display_kms_registration_matrix`,
  `nouveau_kms_vblank_irq_source_matrix`,
  `kms_scanout_cpu_convert_separation_matrix`,
  `ttm_real_move_backend_matrix`, `nouveau_gem_mmap_backing_matrix`, and
  `nouveau_gpuvm_mapping_failclosed_matrix` zero-credit on GPU-P-only Hyper-V.
- For DRM sync_file validation, distinguish the layers:
  - pending export/import readiness proves live source tracking;
  - callback lifecycle proves poll-arm, signal-fire, close-cancel, and
    no-late-fire accounting;
  - syncobj wait callback lifecycle proves actual sleeping waits arm per-state
    callbacks, signal/transfer fires them, finite timeout cancels them, and
    `wait_callback_late_delta=0`;
  - pending syncobj timeline transfer must copy the source dependency before
    it is signaled, then wake destination waiters only after the source point
    signals; the index row is `syncobj_pending_transfer_matrix`;
  - fd-visible cancellation should happen from `.last_fd_close`; delayed
    `.release` accounting is not enough evidence for close-before-signal
    behavior;
  - exported software fence fds need their own add/fire/remove/late callback
    matrix before broad dma-fence language is justified; exact software
    fence-object lifetime uses `early_release_on_close` when the fd has no
    hidden or concurrent refs;
  - the broad dma-fence row is closed only by
    `drm_dma_fence_lifetime_contract_matrix`, which ties the single
    `fb_gpu_fence` backing object to GEM/PRIME/dma-buf, KMS OUT_FENCE,
    syncobj/sync-file, poll callback removal, and final release without
    native-present/OpenGL-submit credit;
  - the broad dma-resv row is closed only when `ttmtest` and
    `gpucorevalidate` require `ttm_dma_resv_ww_mutex_matrix`: a
    `ww_acquire_ctx`-shaped reservation context, ordered multi-object acquire,
    reversed-order retry/backoff, balanced releases, and zero
    native-accel/OpenGL-submit credit across the existing TTM, PRIME/dma-buf,
    KMS, syncobj, sync-file, eviction, and teardown attach points;
  - native KMS OUT_FENCE and display-correlated vblank/page-flip completion are
    separate gates; passing the vblank/page-flip source matrix does not close
    the OUT_FENCE gate.
  - `kms_vblank_native_present_separation_matrix` is the focused source-level
    guard: vblank/page-flip/display counters may advance while native D3D12
    present and OpenGL-submit credit remain zero.
- The finite 480p FPS validator runs the anti-inflation preflight by default
  before heavy Hyper-V sampling. The required negative is the observed around-40
  app-loop/demo FPS with single-digit visible cadence, stale run id, static
  content, or frozen-window evidence. `mesawlegl` must keep the on-screen
  overlay/title at `0.0` FPS until strict native-present credit exists; the
  untrusted draw-loop rate belongs in `app_loop_fps` diagnostics only.
- `fps_frozen_window_rejection_matrix` and
  `fps_sustained_post_warmup_progress_matrix` are the freeze-after-motion
  guards. The FPS validator must sample more than one visual window and reject
  runs where native/display/content/callback/release/present/completed counters
  or thumbnails move briefly and then stop, even if the overlay still prints a
  plausible FPS number.
- The FPS validator's preflight must leave durable rejection evidence, not only
  console text. `fps_forged_display_bind_negative_matrix` belongs in the log,
  outside-overlay CRC transitions must be computed before sustained-window
  checks, and `finite_fps_credit` must stay separate from
  `opengl_submit_credit`; the latter requires both finite native-present FPS
  and `backend_opengl_submit=1`.
- `mesawlegl` owns `/tmp/mesawlegl-fps` app telemetry. Treat its app-loop
  FPS as context-only unless each sample has matching validation run id,
  `process_id == d3d12_client_pid`, nonzero DXG present/completed counters,
  `d3d12_native_present_requirements_satisfied=1`, no readback, and current
  compositor evidence generation/time/resource/buffer-generation metadata.
  The displayed `visible_fps`/`overlay_fps` fields should remain `0.000`
  without that credit, while `app_loop_fps` may carry the raw draw-loop number.
  `mesawlegl_fps_context_only_matrix` and
  `fps_overlay_inflation_rejection_matrix` are rejection evidence, not pass
  evidence for the 60 FPS gate.
- Use the present/FPS provenance rows to keep the GUI gate honest:
  `wlcomp` emits `d3d12_wayland_present_fps_provenance_matrix`, and
  `mesawlegl` emits `mesawlegl_fps_present_credit_matrix`. On fail-closed
  Hyper-V these must show `effective_presented_fps=0.000`,
  `visible_fps_ignored=1`, `overlay_fps=0.000`, and zero
  native-present/OpenGL-submit credit even if `app_loop_fps` is higher.
- Treat `/tmp/wlcomp-d3d12-present` as the durable handoff file for those
  provenance rows. It should carry both the matrix row and
  `d3d12_fps_provenance_*` scalar keys so FPS/WebKit validators do not depend
  on stderr timing. It must also carry the evidence-seal keys described in the
  native-present dependency tree; FPS/WebKit parsers should reject canonical
  `display_bind_*` rows from that file until the seal generation and run id
  match the current validation run.
- Treat visible-content progress as zero-credit until native D3D12 display
  completion is proven for the same resource/generation. The compositor should
  emit `d3d12_wayland_content_progress_matrix` and
  `d3d12_content_progress_*` scalar keys in `/tmp/wlcomp-d3d12-present`; on
  fail-closed Hyper-V these must report `d3d12_content_progress_state=DEFERRED`,
  `d3d12_visible_content_progress=DEFERRED`,
  `d3d12_content_progress_current_run_valid=0`,
  `d3d12_content_progress_identity_complete=0`,
  `d3d12_content_progress_requires_native_present=1`,
  `d3d12_visible_content_requires_native_present_completion=1`,
  `d3d12_visible_content_credit_before_native_present=0`, and zero
  native-present/OpenGL-submit credit. The evidence file should also include
  explicit zero-valued content sample fields
  (`d3d12_present_content_crc`, `d3d12_visible_content_crc`,
  `d3d12_present_content_frame`, `d3d12_visible_content_frame`,
  `d3d12_present_frame_hash`, `d3d12_visible_frame_hash`) until real
  compositor-owned native-present content correlation exists. `mesawlegl`
  source-side `client_content_hash`/`client_content_frame` and
  `content_region=client-content-no-title-fps` are useful context, not native
  present proof by themselves.
- Passing FPS evidence must include the exact geometry contract:
  `window=640x480 render=640x480 render_div=1`. WebKit's GPU validator should
  reject prior FPS artifacts that lack that full-resolution token.
- The 480p demo lifecycle gate has two evidence producers. `mesawlegl` owns
  source/app telemetry and must only set `resizable_demo=1` after an actual
  resize count. `wlcomp` owns compositor lifecycle telemetry and appends
  `wayland_demo_visible_close_resize_matrix` to `/tmp/mesawlegl-fps` with
  validation run id, client pid, mapped/closeable/resize state, geometry, and
  zero native/OpenGL-submit credit. Do not let these lifecycle rows carry fake
  present ids, completed ids, or content-progress credit.
- The lightweight FPS selftest emits
  `fps_demo_interaction_selftest_matrix`. Keep stale run-id, missing-resize,
  source-only, callback-only, and forged display-bind cases rejected with zero
  native-present/OpenGL-submit credit until the final native-present/FPS run
  can prove the demo is visible, closeable, and resizable.
- Final FPS display-bind acceptance is canonical only:
  `display_bind_backend=gpup_dxg_scanout_bind` and
  `display_bind_transport=gpu-p-dxg-resource-scanout-bind`, with exact
  `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`,
  `host_saw_display_bind_packet=1`,
  `wsl_presenthistory_completion_credit=0`, and
  `display_bind_completion_source=display` on the same evidence record as the
  nonzero present id, completed id, and resource generation. Fail-closed
  `/tmp/wlcomp-d3d12-present` rows should instead report
  `display_bind_transport_source=none`, `host_saw_display_bind_packet=0`,
  and `wsl_presenthistory_completion_credit=0`, and remain zero-credit.
  Legacy aliases such as `hyperv-dxg`, `gpu-p`, `dda`, `nouveau`,
  `dxg-resource-scanout-bind`, `host-display-channel`,
  `FB_GPU_DXG_PRESENT_COMPLETION_DISPLAY`, numeric enum values, and
  case-changed `DISPLAY` are diagnostic text only, not final pass tokens.
  FPS acceptance must require coherent sample-window and visual-window
  display-bind tuples before any FPS credit.
- Kernel display-bind success acceptance must be stricter than the fail-closed
  metadata rows. A future positive result must have provider publication,
  publish-before-send ordering, nonzero transport pending id, command id,
  transaction id, channel, completion demux registration,
  `host_saw_display_bind_packet=1`, and
  `wsl_presenthistory_completion_credit=0` before nonzero present/completed ids
  can count. `d3d12_display_bind_success_shape_matrix` should expose those
  fields for both `fbstat` and `dxgprobe`.
- WebKit's animated native-present fixture currently proves liveness only.
  Treat `/share/webkit/webkit-animated-content-native-present.html` title/frame
  progress as insufficient until `/tmp/wlcomp-d3d12-present` supplies matching
  nonzero content CRC/frame/hash, same run/client/resource generation, native
  display completion, and canonical final display-bind names
  (`gpup_dxg_scanout_bind` and `gpu-p-dxg-resource-scanout-bind`).
- Keep both WebKit launch paths on the same strict D3D12 evidence contract.
  `wlcomp_launcher` and `desktop` should parse `/tmp/wlcomp-d3d12-present` as
  whitespace/line-bounded `key=value` tokens, require the generated WebKit run
  id to match both `d3d12_run_id` and
  `d3d12_present_identity_compositor_run_id`, and reject acceleration unless
  compositor-owned content CRC/frame/hash progress matches the provider-owned
  display-bind present/completed ids, resource generation, and source-authority
  tuple. Policy artifacts should expose `d3d12_run_id_match`,
  `d3d12_content_progress`, `d3d12_evidence_seal`,
  `display_bind_transport_source`, `host_saw_display_bind_packet`, and
  `wsl_presenthistory_completion_credit` so shell validators can fail stale,
  unsealed, prefixed, WSL-telemetry, host-copy, or chrome/title-only evidence.
- `webkitgpusmoke --negative-selftests` is the pure-C consumer-negative
  validator for WebKit evidence. Keep
  `webkit_contract_parser_negative_matrix`,
  `webkit_lineage_equality_negative_matrix`, and
  `webkit_animated_content_fixture_negative_matrix` green before trusting a
  shell WebKit artifact. Also keep
  `webkit_animated_content_native_present_negative_matrix` green: even
  plausible compositor-owned content CRC/frame/hash plus source-authority
  fields remain zero-credit when native present, prior finite FPS,
  backend OpenGL-submit, or shared-surface contract evidence is missing.
  Prefixed/suffixed keys, malformed numerics, backend/transport aliases,
  completion-source aliases, stale D3D12/FPS/content lineage, backend-zero
  nonzero-id claims, title-only animated fixture progress, and forged content
  progress must all remain zero-credit.
- WebKit acceleration validation starts with the
  `webkit_evidence_rejection_matrix` policy preflight. Chrome/title/cursor-only,
  callback-only, release-only, render-node-only, dmabuf-only, env-only, and
  software-fallback evidence must all fail before any enabled WebKit artifact is
  considered.
- WebKit policy preflight should also reject plausible but incomplete
  acceleration-looking rows: nonzero display/native ids while
  `backend_opengl_submit=0`, stale D3D12 run ids, and stale FPS artifact run
  ids. Downstream gate rows should keep separate reason fields for backend-zero,
  display-bind completion-source rejection, native-id rejection, and lineage
  rejection so a future enabled artifact cannot hide which dependency opened.
- WebKit C consumers must parse policy and D3D12 evidence as whitespace/line
  bounded `key=value` tokens. Raw substring searches can accept prefixed keys,
  suffixed keys, malformed numeric values, or backend/transport aliases; keep
  `display_bind_backend=gpup_dxg_scanout_bind` and
  `display_bind_transport=gpu-p-dxg-resource-scanout-bind` as the canonical
  final tokens.
- WebKit shell consumers must also be line-scoped. Before emitting any open
  WebKit gate, require one provider-owned display-bind record carrying exact
  backend, transport,
  `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`,
  `host_saw_display_bind_packet=1`,
  `wsl_presenthistory_completion_credit=0`,
  `display_bind_completion_source=display`, `completion_source=display`,
  nonzero present/completed/resource generation, `backend_opengl_submit=1`,
  and a matching native completion id. Do not fill missing native completion
  ids from display-bind ids, and do not count final-handoff-only host-display
  aliases or WSL present-history telemetry as GPU-P/DDA commit acceptance.
- Pure-C validator consumers should use the same discipline. `gpucorevalidate`
  output checks should match whitespace/line-bounded tokens while still
  allowing deliberate `field=` prefix probes, and `dxgprobe` should parse
  `/dev/dxg` host-to-VM status fields on one line with whole-token numeric
  values before using present-history telemetry rows.
- WebKit also requires
  `webkit_stale_display_bind_evidence_rejection_matrix` and
  `webkit_enabled_artifact_contract_matrix`. Stale/after-close display-bind
  evidence is always zero-credit. A future enabled artifact must prove
  current-run D3D12 display-bind completion, finite 480p FPS,
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT`, shared-resource/fence identity, and
  compositor-owned content CRC/frame/hash identity from the same lineage.
  The enabled artifact row must also echo and require the provider-owned
  source-authority tuple:
  `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`,
  `host_saw_display_bind_packet=1`, and
  `wsl_presenthistory_completion_credit=0`; WSL present-history telemetry and
  host-copy aliases remain zero-credit even if other display-bind ids are
  nonzero.
- For WSL `hmgrtable` parity, keep local adapter handles and normal DXG object
  handles distinct:
  - `hvdxg_process_state` should keep WSL-shaped process object refs separate
    from process memory refs; validators should look for
    `dxg_process_mem_lifetime_matrix` and the kernel
    `d3dkmt_process_lifetime=` status row before closing process-lifetime
    parity items;
  - local adapter handles use the per-process local handle namespace and should
    prove index/unique encoding plus the WSL minimum-free reuse delay
    (`min_free=128`) with `dxgprobe --handle-lifetime-validate`;
  - stale-handle validation should explicitly name and reject every locally
    tracked class in the matrix: device, context, HW queue, HW-queue progress
    fence sync, sync object, paging queue, paging-queue sync, resource,
    standalone allocation, and GPUVA reservation;
  - `dxgprocess_adapter` parity means `CREATEDEVICE` must resolve a
    process-local adapter handle, not the raw host adapter handle, and final
    close of that per-process adapter should tear down still-live child
    devices; require `dxgprocess_adapter_matrix` before checking that plan row;
  - normal object handles still need full free-list parity before the broad
    handle-table gate can close, even if tombstone diagnostics and stale
    rejection counters look healthy.

## Common Problems

- **Generated-source drift**: `ports/wayland/src/wlcomp.c` says one thing while `build-x86_64/ports/wayland/wlcomp-build/wlcomp.c` runs another.
- **Sleep workaround trap**: replacing event waits with sleeps can make the GUI appear alive while hiding readiness bugs.
- **Browser noise**: NetSurf, networking, DNS, TLS, and GTK startup can obscure base compositor problems.
- **ABI mismatch suspicion**: input packet layout is blamed before checking whether user space is actually reading events.
- **Nested wait confusion**: the internal Wayland event-loop fd and outer compositor epoll fd can be mistaken for the same wait.
- **Render/input conflation**: framebuffer activity proves drawing progress, not necessarily input delivery or client focus correctness.
- **Launcher path mismatch**: boot autostart may be disabled by `netsurf=0` while the compositor icon/menu launcher is separately disabled by generated `wlcomp.c` rewrites.
- **Silent browser exit**: stdout/stderr redirection to `/tmp/app_log.txt` can hide the useful failure unless the log is copied or read from inside xv6.
- **Environment drift**: GTK/Wayland clients can fail before mapping a surface if `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, `GDK_BACKEND`, or `HOME` are missing or inconsistent.
- **WebKit helper drift**: MiniBrowser can launch while the network or web helper process fails to exec, or while GIO cannot find the OpenSSL TLS module.
- **Child-watch drift**: temporary WebKit instrumentation that adds a child watch to a `GSubprocess` PID can break GLib's own child lifecycle handling and make the network process look like it died for the wrong reason.

## Pitfalls

- Do not fix GUI freezes by replacing `epoll_wait` with sleeps as a final answer; that can hide kernel readiness bugs.
- Do not validate a source `wlcomp.c` change without checking the generated file and rebuilt image/rootfs state.
- Do not debug browser, network, compositor, and input hypotheses all at once unless the capture proves they interact.
- Do not call Hyper-V OpenGL complete while it still presents through the
  D3D12-to-software/readback bridge or while the finite 480p demo remains below
  60 FPS.
- Do not compare a current NVIDIA Hyper-V failure against an old Intel WSL trace
  without capturing a same-adapter control trace.
