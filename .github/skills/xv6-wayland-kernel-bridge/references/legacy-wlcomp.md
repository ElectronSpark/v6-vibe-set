# Legacy wlcomp boundary notes

Use only after identifying an image that actually runs the old custom
`wlcomp` compositor, or for an explicit restoration task. The normal image
runs KWin/Plasma. Historical paths and behaviors below describe an older
implementation; confirm them in that revision before editing or testing.

## Locate the effective code

The former source path was `ports/wayland/src/wlcomp.c`; the former generated
path was `build-x86_64/ports/wayland/wlcomp-build/wlcomp.c`. The current normal
port no longer builds this compositor. If working on a historical checkout,
compare its source, CMake transformations, installed executable, and booted
image. Inspect a named generated file only through the guarded artifact-search
workflow; never recursively content-search its build tree.

Historical `desktop` launched `wlcomp`, waited for its Wayland lock, and
optionally started NetSurf. Launcher shortcuts, boot `netsurf=0`, and manual
shell activation were different launch paths. Derive those paths from the
selected revision instead of applying them to KDE.

## Input and event loop invariants

- The custom `/dev/mouse` event ABI used signed `int16_t dx, dy`, button/flag
  bytes, signed wheel byte, and padding. Absolute coordinates held normalized
  unsigned 16-bit values: widen through `uint16_t` before scaling by guest
  dimensions. This is distinct from Linux evdev records.
- The event loop drained mouse, keyboard, PTYs, client dispatch, and painting
  before a bounded readiness wait. `wl_event_loop_dispatch(loop, 0)` had to
  remain nonblocking so input rings could be drained.
- A blocking internal Wayland wait with input queued in the outer epoll set
  can mimic a kernel input freeze. Compare the actual syscall timeout,
  registration, readiness, successful reads, cursor state, and client events.
- Internal windows, taskbar, menus, desktop shortcuts, and Wayland surfaces
  have competing hit-test/focus paths. Trace the consuming path rather than
  assuming every event reaches the focused client.

## Buffers, painting, and application closure

- The old software path composited into a framebuffer buffer and submitted a
  GPU blit. Other historical GPU paths existed; select the one in the image.
- Some generated variants suppressed per-frame `wl_buffer.release` while
  retaining release on replacement. Releasing a still-committed buffer can
  permit client reuse before composition finishes. Verify the selected
  implementation's ownership and callbacks before changing release timing.
- Frame callbacks and protocol flushes must continue after composition.
  They are not independent proof of host display completion.
- Legacy built-in terminal/editor windows used PTYs and shell/editor children;
  their behavior does not establish Konsole's Qt/Linux PTY behavior.
- A mapped NetSurf or MiniBrowser surface does not prove networking, TLS,
  subprocess closure, input, or clean teardown. Check the selected launch
  environment and fresh application logs before attributing failure to the
  kernel.
