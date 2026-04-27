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
- For NetSurf fetch failures after the window maps, separate browser UI success from network/TLS success. Confirm `NETSURF_USE_CURL := YES`, `NETSURF_USE_OPENSSL := YES`, static OpenSSL symbols in `build-x86_64/sysroot/bin/netsurf`, DNS in `/etc/resolv.conf`, and socket/connect logs before changing compositor code.
- For MiniBrowser/WebKit fetch failures, first separate launch from the WebKit multi-process runtime. Verify `/libexec/webkit2gtk-4.1/MiniBrowser`, `WebKitNetworkProcess`, `WebKitWebProcess`, `/lib/libwebkit2gtk-4.1.so.0`, `/lib/libjavascriptcoregtk-4.1.so.0`, `/lib/webkit2gtk-4.1/injected-bundle`, and `/lib/gio/modules/libgioopenssl.so` are staged in both `build-x86_64/sysroot` and `fs.img`.
- WebKit launch needs the compositor environment to include Wayland/GTK variables plus `GIO_MODULE_DIR=/lib/gio/modules`, `GIO_USE_TLS=openssl`, `WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1`, and `WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle`. Missing helpers or GIO modules can look like a page-fetch failure even when the window maps.
- If MiniBrowser repeatedly connects to `127.0.0.1:80` and gets `errno=104`, verify the shortcut has an `Arg=` URL; otherwise the compositor fallback URL is being used and no local HTTP server is present.
- If MiniBrowser shows the requested URL and then the window cannot be closed, sample with `xv6-threads` before assuming a compositor event-loop freeze. A known failure mode is the MiniBrowser thread-group leader in `ZOMBIE` while non-leader UI threads plus `WebKitNetworkProcess` and `WebKitWebProcess` remain alive; the compositor may still be rendering in framebuffer `ioctl`, but the client no longer processes its own close UI.
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

## Pitfalls

- Do not fix GUI freezes by replacing `epoll_wait` with sleeps as a final answer; that can hide kernel readiness bugs.
- Do not validate a source `wlcomp.c` change without checking the generated file and rebuilt image/rootfs state.
- Do not debug browser, network, compositor, and input hypotheses all at once unless the capture proves they interact.
