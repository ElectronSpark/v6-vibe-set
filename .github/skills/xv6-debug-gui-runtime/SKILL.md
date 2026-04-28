---
name: xv6-debug-gui-runtime
description: 'Use when: debugging fluid xv6-os GUI runtime behavior, Wayland desktop freezes, cursor/input symptoms, NetSurf side effects, generated wlcomp.c drift, compositor loop hypotheses, or GUI observations that are not yet stable enough for source-derived skills.'
argument-hint: 'Describe the GUI symptom and latest runtime observation'
---

# xv6 GUI Runtime Debugging

## Fluidity Notice

This skill is a moving debug notebook for GUI runtime behavior. It is not ground truth and can become deprecated without notice. Prefer current generated `wlcomp.c`, kernel source, and the stable `xv6-wayland-kernel-bridge` skill when they conflict.

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
- For MiniBrowser/WebKit fetch failures, first separate launch from the WebKit multi-process runtime. Verify `/libexec/webkit2gtk-4.1/MiniBrowser`, `WebKitNetworkProcess`, `WebKitWebProcess`, `/lib/libwebkit2gtk-4.1.so.0`, `/lib/libjavascriptcoregtk-4.1.so.0`, `/lib/webkit2gtk-4.1/injected-bundle`, and `/lib/gio/modules/libgioopenssl.so` are staged in both `build-x86_64/sysroot` and `fs.img`.
- WebKit launch needs the compositor environment to include Wayland/GTK variables plus `GIO_MODULE_DIR=/lib/gio/modules`, `GIO_USE_TLS=openssl`, `WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1`, and `WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle`. Missing helpers or GIO modules can look like a page-fetch failure even when the window maps.
- If MiniBrowser repeatedly connects to `127.0.0.1:80` and gets `errno=104`, verify the running/generated compositor first. An old launcher preflight in `wlcomp.c` probed localhost before exec and produced this exact noise even when the MiniBrowser URL was `https://www.google.com/`; remove launcher-side localhost waits rather than chasing WebKit networking.
- If WebKit aborts with `Data too big for buffer (4084 + 20 > 4096)`, check the Wayland server buffer cap. Raising the compositor display limit with `wl_display_set_default_max_buffer_size()` lets larger WebKit startup messages pass and avoids misdiagnosing it as a page-fetch failure.
- If GTK/WebKit windows look offset, have an extra floating close button, or route clicks oddly, inspect compositor-side decoration and xdg geometry first. GTK client-side decorations rely on `xdg_surface.set_window_geometry`; the compositor should use that geometry for hit testing and draw the buffer at `surface_pos - window_geometry_offset`. Avoid drawing an extra compositor close button over GTK CSD windows.
- Wayland `ARGB8888` buffers are premultiplied-alpha. If GTK cursors or translucent shadows look dark/fringed, fix compositor blending to use `src + dst * (1 - alpha)` for ARGB8888 instead of multiplying the source color by alpha a second time.
- If MiniBrowser shows the requested URL and then the window cannot be closed, sample with `xv6-threads` before assuming a compositor event-loop freeze. A known failure mode is the MiniBrowser thread-group leader in `ZOMBIE` while non-leader UI threads plus `WebKitNetworkProcess` and `WebKitWebProcess` remain alive; the compositor may still be rendering in framebuffer `ioctl`, but the client no longer processes its own close UI.
- If `wlcomp` reports the MiniBrowser child exited but `xv6-threads` still shows TGID-matching MiniBrowser workers, check kernel process lifecycle before chasing networking. Parent `waitpid` must not reap a zombie thread-group leader while `live_threads > 0`; fatal signal/killed trap paths should use `thread_group_exit()`, and `exit()` should promote leader exits with live siblings to group exit as a backstop.
- Do not force-destroy Wayland clients or kill the process group merely because a tracked launcher child became waitable. WebKit can have a launcher-visible leader/helper mismatch; normal reaping should free the child slot only and let Wayland HUP/resource cleanup remove genuinely dead clients. Keep client destruction/process-group kill for explicit close/force-close paths.
- For WebKit Google fetch failures, validate plain network and TLS independently before changing browser code. `openssl s_client -connect google.com:443 -servername google.com` should resolve and connect; if it only fails verification, check `/etc/ssl/cert.pem` and `/etc/ssl/certs/ca-certificates.crt`. The staged NetSurf bundle at `/share/netsurf/ca-bundle` is a known-good source for these default OpenSSL paths.
- If MiniBrowser maps, shows `https://www.google.com/`, and no kernel socket log ever shows an outbound Google connect, suspect WebKit helper IPC rather than DNS/TCP/TLS. A launched `WebKitNetworkProcess` that never reaches fetch setup can leave the UI blank with no connect attempts.
- WebKit/JSC treats `/dev/urandom` as mandatory. If MiniBrowser SIGABRTs in `WTF::RandomDevice::RandomDevice()` or `WTF::cryptographicallyRandomValuesFromOS()` during GTK/WebKit startup, verify both `/dev/random` and `/dev/urandom` exist and are readable in the guest; a working `getrandom()` syscall is not enough for this code path.
- WebKit's disk network cache can abort early on xv6 before any Google TCP connect. If NetworkProcess tracing stops at `NetworkCache::Cache::open()`, launch MiniBrowser with `WEBKIT_DISABLE_NETWORK_CACHE=1` or bypass disk cache until the cache filesystem assumptions are supported.
- Autostarted GUI clients should redirect stdout/stderr to `/tmp/app_log.txt` like compositor-launched apps. A heavily instrumented WebKit build can flood serial output during Google load and make a healthy compositor look unresponsive.
- Keep kernel socket tracing off for browser responsiveness validation. `SOCK_DEBUG=1` in `kernel/lwip_port/sys_socket.c` prints every socket/connect/SENDPLUS callback, and unconditional syscall-level `fcntl(F_SETFL)` traces are similarly noisy; Google opens enough TLS connections that this serial/debugcon flood can make MiniBrowser or the compositor appear frozen after the page has successfully loaded.
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
- If the shell prompt returns with status 0 immediately after MiniBrowser activation while `WebKitNetworkProcess` and `WebKitWebProcess` remain alive, the UIProcess is no longer available to answer `WebProcessProxy::GetNetworkProcessConnection`. Hold the `GApplication` after the main browser window has been added, and release it when that window is destroyed, before chasing lower network layers.
- If that status-0 MiniBrowser return persists after a window-scoped `GApplication` hold, instrument both sides of process teardown before changing networking: connect MiniBrowser `shutdown`, `window-added`, `window-removed`, and `atexit` logs, and temporarily log zero-code `exit`/`exit_group` for `MiniBrowser`/`WebKit*` in the kernel. This distinguishes normal GTK application shutdown from raw thread exit or direct `_exit(0)`.
- On xv6, `g_application_run()` can leave MiniBrowser without a live UIProcess even though no GTK shutdown or `atexit` log fires. A practical WebKit port workaround is to manually `g_application_register()`, `g_application_activate()`, then keep the main thread in an explicit `GMainLoop` until the main window is destroyed; this keeps the UIProcess available for `WebProcessProxy::GetNetworkProcessConnection`.
- If the shell prompt returns after `MiniBrowser` logs that it entered a manual `GMainLoop`, inspect kernel wait/reap behavior rather than GTK lifecycle. Log browser-child reaps from `wait()`/`waitpid()` with `pid`, `tgid`, state, `thread_group->live_threads`, and parent name to catch premature leader reaping or thread-group accounting drift.
- If `waitpid` reaps MiniBrowser with `xstate=-1`, the UIProcess died via the kernel killed/fatal-signal path. Add targeted `usertrap` logs for browser threads (`scause`, `sepc`, `stval`, fault kind) before assuming WebKit IPC or TLS is the next blocker.
- If no `usertrap` fault log precedes `xstate=-1`, instrument signal termination itself: log when browser threads set `THREAD_KILLED`, including the first pending terminating signal and whether it came from default action, mask changes, invalid handler delivery, or `handle_signal()` termination.
- If MiniBrowser/WebKit is reaped with `xstate=-1` and the signal trace reports `signum=6`, treat it as a SIGABRT before continuing networking/TLS work. Add a narrow `kill`/`tgkill`/`tkill` trace for SIGABRT that prints sender, target, and the sender's user backtrace; without the sender-side trace, the later default-action log only proves who died, not which assertion or abort path fired.
- For headless serial validation of MiniBrowser, boot with `netsurf=0 webkit=1` so `/bin/desktop` launches MiniBrowser through the compositor/session path. The xv6 shell cannot reliably express the long Wayland/WebKit environment as `VAR=value command`, and serial shells should normally refuse GUI-only programs.
- Before trusting kernel substring-based debug filters, verify `strstr()` handles shorter haystacks. A broken bounded search can overread short process names like `ps`/`cp`, produce noisy false-positive exit logs, and make process-lifecycle evidence look much worse than it is.
- For wedged WebKit GUI clients, prefer a compositor-side force-close path that records Wayland client credentials, destroys the `wl_client`, and kills the launched process group. This contains the stuck surface/helper-process problem while the WebKit/thread-group root cause is debugged.
- Use a healthy control sample. A running compositor should periodically appear in framebuffer work, outer epoll waits, or input processing depending on where it is interrupted.
- If input is queued but not consumed, first ask whether readiness is level-correct and whether the compositor reaches its drain point.
- If rendering continues but interaction fails, focus on input routing, focus state, pointer/keyboard protocol delivery, or client state rather than framebuffer.

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
