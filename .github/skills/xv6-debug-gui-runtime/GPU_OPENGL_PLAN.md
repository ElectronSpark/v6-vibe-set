# GPU And OpenGL Plan

This plan tracks the graphics work after the WebKit runtime became usable.
The current xv6 GUI stack is a software Wayland compositor that draws into a
userspace framebuffer and presents through `/dev/fb0`.

## Stage 1: Reduce Blinking On The Current Stack

- Track compositor damage rectangles instead of presenting the full screen on
  every event-loop tick.
- Present only the damaged union through `FB_GPU_BLIT`.
- Damage old and new cursor bounds when the pointer moves.
- Damage Wayland surfaces on commit/damage requests.
- Damage internal windows on terminal output, monitor refresh, dragging,
  resizing, and 3D demo animation.
- Keep conservative full-screen damage for structural desktop changes.

This stage keeps the existing kernel ABI and does not add dependencies.

## Stage 2: Strengthen The Framebuffer ABI

- Add framebuffer present counters for full-screen vs partial blits.
- Batch multiple damage rectangles if a single union becomes too large.
- Validate `FB_GPU_BLIT` source pitch and bounds more strictly.
- Consider an mmap path for framebuffer-backed staging buffers if ioctl copies
  remain a bottleneck.
- Gate the boot-time framebuffer test pattern behind a debug cmdline option.

## Stage 3: Software OpenGL Compatibility

- Add a repo-local software GL renderer port before requiring real GPU
  acceleration.
- Prefer a small OSMesa/TinyGL-style path first: render GL into a memory
  surface, then expose that surface to Wayland as SHM.
- Provide minimal `libGL` or `libEGL` loader stubs only for the APIs the first
  demo/client needs.
- Add a simple GL smoke app that draws a rotating triangle into a Wayland
  surface.

This gives API-level OpenGL coverage while the kernel still exposes only a
framebuffer.

## Stage 4: Real GPU Buffer Infrastructure

- Add a virtio-gpu or DRM/KMS-style kernel driver.
- Add buffer-object allocation, mmap, lifetime, and fd-passing semantics.
- Define a small xv6 graphics-buffer ioctl ABI before attempting Mesa winsys
  integration.
- Add Wayland `linux-dmabuf` or a simpler xv6-private buffer protocol to avoid
  copying rendered buffers through SHM.
- Add explicit synchronization or a simple fence model once clients can render
  asynchronously.

## Stage 5: Mesa/EGL Integration

- Build Mesa only after the kernel buffer ABI and Wayland buffer import path
  exist.
- Start with software rasterization using xv6 buffer objects.
- Move to virtio-gpu acceleration if the driver can support the required
  command queue, resource attach, transfer, and fence model.
- Keep WebKit's current software/low-feature profile until EGL/GL rendering is
  stable under repeated navigation and close/reopen tests.

## Validation Ladder

- Rebuild `port-wayland`, `image`, and `webkit-runtime-check`.
- Boot KVM with `root=/dev/disk0 netsurf=0 webkit=1`.
- Confirm idle desktop does not constantly full-blit.
- Move the cursor and verify reduced blinking.
- Open internal windows, drag/resize them, and verify redraw correctness.
- Run MiniBrowser through Google Search and repeated navigation.
- Add GL smoke tests only after the software GL path exists.
