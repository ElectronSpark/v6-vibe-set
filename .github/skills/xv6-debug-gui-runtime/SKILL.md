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
- Do not let serial shells spoof GUI capability by exporting `XV6_GUI_SESSION`, `XDG_RUNTIME_DIR`, and `WAYLAND_DISPLAY`. The shell should treat GUI capability as a launch-time property from `sh --gui-session`, not only as inherited environment text.
- For NetSurf fetch failures after the window maps, separate browser UI success from network/TLS success. Confirm `NETSURF_USE_CURL := YES`, `NETSURF_USE_OPENSSL := YES`, static OpenSSL symbols in `build-x86_64/sysroot/bin/netsurf`, DNS in `/etc/resolv.conf`, and socket/connect logs before changing compositor code.
- For MiniBrowser/WebKit fetch failures, first separate launch from the WebKit multi-process runtime. Verify `/libexec/webkit2gtk-4.1/MiniBrowser`, `WebKitNetworkProcess`, `WebKitWebProcess`, `/lib/libwebkit2gtk-4.1.so.0`, `/lib/libjavascriptcoregtk-4.1.so.0`, `/lib/webkit2gtk-4.1/injected-bundle`, and `/lib/gio/modules/libgioopenssl.so` are staged in both `build-x86_64/sysroot` and `fs.img`.
- WebKit launch needs the compositor environment to include Wayland/GTK variables plus `GIO_MODULE_DIR=/lib/gio/modules`, `GIO_USE_TLS=openssl`, `WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1`, and `WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle`. Missing helpers or GIO modules can look like a page-fetch failure even when the window maps.
- If MiniBrowser repeatedly connects to `127.0.0.1:80` and gets `errno=104`, verify the running/generated compositor first. An old launcher preflight in `wlcomp.c` probed localhost before exec and produced this exact noise even when the MiniBrowser URL was `https://www.google.com/`; remove launcher-side localhost waits rather than chasing WebKit networking.
- If WebKit aborts with `Data too big for buffer (4084 + 20 > 4096)`, check the Wayland server buffer cap. Raising the compositor display limit with `wl_display_set_default_max_buffer_size()` lets larger WebKit startup messages pass and avoids misdiagnosing it as a page-fetch failure.
- If GTK/WebKit windows look offset, have an extra floating close button, or route clicks oddly, inspect compositor-side decoration and xdg geometry first. GTK client-side decorations rely on `xdg_surface.set_window_geometry`; the compositor should use that geometry for hit testing and draw the buffer at `surface_pos - window_geometry_offset`. Avoid drawing an extra compositor close button over GTK CSD windows.
- Wayland `ARGB8888` buffers are premultiplied-alpha. If GTK cursors or translucent shadows look dark/fringed, fix compositor blending to use `src + dst * (1 - alpha)` for ARGB8888 instead of multiplying the source color by alpha a second time.
- If MiniBrowser shows the requested URL and then the window cannot be closed, sample with `xv6-threads` before assuming a compositor event-loop freeze. A known failure mode is the MiniBrowser thread-group leader in `ZOMBIE` while non-leader UI threads plus `WebKitNetworkProcess` and `WebKitWebProcess` remain alive; the compositor may still be rendering in framebuffer `ioctl`, but the client no longer processes its own close UI.
- If `wlcomp` reports the MiniBrowser child exited but `xv6-threads` still shows TGID-matching MiniBrowser workers, check kernel process lifecycle before chasing networking. Parent `waitpid` must not reap a zombie thread-group leader while `live_threads > 0`; fatal signal/killed trap paths should use `thread_group_exit()`, and `exit()` should promote leader exits with live siblings to group exit as a backstop.
- If the launcher child exits but the stale window remains, explicitly destroy Wayland clients whose credentials match the launched PID before/alongside process-group termination. Process-group `SIGKILL` can be delayed by helper-process wait paths, but the compositor should remove orphaned surfaces immediately.
- If WebKit helper launches log `GLib-WARNING **: waitpid(pid:NN) failed: No child process (10)`, look for duplicate child reaping. `GSubprocess` already owns its child watch; adding `g_child_watch_add()` to the same PID in `ProcessLauncherGLib.cpp` races GLib and can produce `ECHILD` plus spurious helper-death reports.
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
