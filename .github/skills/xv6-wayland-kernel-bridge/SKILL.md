---
name: xv6-wayland-kernel-bridge
description: 'Use when: tracing xv6-os compositor/kernel boundaries: KWin or legacy wlcomp input, evdev/readiness, Wayland fd passing and buffers, DRM/KMS presentation, or GUI event-loop freezes.'
argument-hint: 'Describe the GUI/compositor symptom'
---

# xv6 Wayland Kernel Bridge

## Select the running compositor

Start with the actual image, startup configuration, executable, and same-run
process roles. Current `rootfs-overlay/etc/startup` launches D-Bus, login1, and
`xv6-desktop-session`; `scripts/image/xv6-desktop-session.c` selects KDE, and
`scripts/image/kde-session.c` starts KWin with Xwayland and a Plasma child.
KWin uses the DRM device and Linux input stack. A historical `wlcomp` window,
log, or generated file is not evidence about this session.

For mouse exploration or visual progress audits, use
[GUI runtime](../xv6-debug-gui-runtime/SKILL.md). Investigate an ABI mismatch via
[Linux GUI ABI](../xv6-linux-gui-abi/SKILL.md). Use this skill to locate the
boundary that stalled; it does not require an audit to become an implementation
project. Root `AGENTS.md` and [repo workflow](../xv6-os-debugging/SKILL.md)
govern searches, waits, serial access, VM ownership, and cleanup.

## Current session and launch

- `scripts/launch/launch-gui.sh` uses the owned launcher and defaults to
  SDL/virgl. Read its resolved frontend, input device, and guest geometry;
  neither GTK nor VMware mouse is universal. Capture host input through the
  visible window when auditing real mouse behavior.
- `scripts/image/kde-session.c` sets the KDE environment, DRM card selection,
  XDG paths, KWin arguments, and optional diagnostic probes. Check actual
  environment and launch logs if a shell-launched app differs from its desktop
  launcher. Source defaults may differ from an older staged image.
- KDE's runtime directory is seeded under `/dev/shm/xdg-runtime-root`; derive
  `WAYLAND_DISPLAY` and the effective process environment in the current
  session. Do not substitute the legacy `/tmp/wayland-0.lock` readiness test.
- KWin and Plasma child logs are opened with `O_APPEND`. Capture baseline
  length/session boundaries before assigning a renderer line or failure to
  this boot. A socket, process, or `GL_RENDERER` line alone does not establish
  a visible, responsive desktop.

## Trace input across layers

1. Observe the target and pointer in a fresh screenshot. Account for host
   window decorations, viewport scaling, focus, and occlusion. Recompute
   coordinates after a window move, resize, menu, or view change.
2. Verify the selected host/QEMU device and kernel producer. Use the
   [input skill](../xv6-kernel-input/SKILL.md) for PS/2, VMware absolute input,
   virtio input, and evdev; do not read only legacy `/dev/mouse` counters when
   the compositor consumes `/dev/input/event*`.
3. Follow queued events, per-open reads/readiness, and compositor consumption.
   Queued input with no reads points toward registration or wakeup/dispatch;
   advancing reads with no semantic response points toward decoding, focus,
   hit testing, or client dispatch. These are hypotheses to distinguish.
4. Verify the specific effect: hover highlight, submenu opening, selection,
   navigation, drag, scroll, or activation. Single/double-click behavior may
   differ between desktop icons, file views, and dialogs. Pointer movement
   alone does not prove the intended button received the event.
5. For a frozen reader, route poll/epoll/kqueue and timed waits to
   [event wait](../xv6-kernel-event-wait/SKILL.md) and
   [timers](../xv6-kernel-timers/SKILL.md). Preserve producer/waiter/consumer
   identity and fd lifetime across the sample; do not replace readiness with
   an unconditional sleep as a final fix.

## Wayland and display boundaries

- Separate protocol connection and fd transfer, surface mapping/configure,
  buffer attach/commit, rendering, fence completion, KMS/page flip, and visible
  host presentation. A successful earlier phase does not prove a later one.
- For `wl_shm`, verify fd size, mapping bounds, stride/format, sharing, and
  buffer lifetime. For dma-buf, additionally verify format/modifier support,
  import/export ownership, synchronization, and render versus display device.
- Buffer release and frame callbacks have different meanings. Do not release
  a buffer still in use, or treat a callback as independent scanout proof.
- For AF_UNIX ancillary data, distinguish stream reads from `recvmsg` and
  preserve rights/control-message semantics; use the Linux GUI ABI skill.
- For DRM/KMS, inspect the relevant fragments under
  `kernel/kernel/dev/fb/` through `module.c`, plus
  `kernel/kernel/dev/drm_core.c`. Check commit acceptance, event/fence
  provenance, and teardown separately from actual visual progress.
- Capture both host/QEMU errors and guest evidence for graphics teardown.
  A disappearing client window is not sufficient evidence of process exit,
  a kernel fault, or a host renderer failure.

## Conditional legacy workflow

Read [legacy compositor notes](references/legacy-wlcomp.md) only for an
explicitly identified old `wlcomp` image or a task restoring that compositor.
The current `ports/wayland/CMakeLists.txt` removes staged `wlcomp`/`desktop`
executables; its old generated `wlcomp-build/wlcomp.c` is not the current
normal compositor build. Do not add generation rewrites to fix a KWin symptom.

## Source map

- Startup and environment: `rootfs-overlay/etc/startup`,
  `scripts/image/xv6-desktop-session.c`, `scripts/image/kde-session.c`,
  `scripts/image/kde-plasma-session-child.c`.
- Device input: `kernel/kernel/dev/ps2mouse.c`,
  `kernel/kernel/dev/ps2kbd.c`, `kernel/kernel/dev/evdev.c`; use the input skill
  to find the selected producer and its public ABI.
- Readiness: `kernel/kernel/kqueue/kqueue.c` and the event-wait skill.
- Graphics: `kernel/kernel/dev/fb/module.c`,
  `kernel/kernel/dev/drm_core.c`, and `docs/linux-drm-abi-audit.md`.
- Existing focused probes: `scripts/image/kde-libinput-probe.c`,
  `scripts/image/kde-wayland-seat-probe.c`,
  `scripts/image/kde-wayland-registry-probe.c`,
  `scripts/image/kde-drm-probe.c`, and
  `scripts/image/kde-kwin-screenshot-probe.c`. Select a probe only when its
  boundary matches the requested investigation; a probe pass is narrower
  than end-to-end GUI behavior.
