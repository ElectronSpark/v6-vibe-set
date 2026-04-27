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

## Pitfalls

- Do not fix GUI freezes by replacing `epoll_wait` with sleeps as a final answer; that can hide kernel readiness bugs.
- Do not validate a source `wlcomp.c` change without checking the generated file and rebuilt image/rootfs state.
- Do not debug browser, network, compositor, and input hypotheses all at once unless the capture proves they interact.
