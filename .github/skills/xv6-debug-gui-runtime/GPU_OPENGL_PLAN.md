# GPU And OpenGL Plan

This plan tracks the graphics work after the WebKit runtime became usable.
The current xv6 GUI stack is a software Wayland compositor that draws into a
userspace framebuffer and presents through `/dev/fb0`.

This is the active GPU/OpenGL plan.  WebKit runtime validation lives in
`WEBKIT_TODO.md`; the retired WebKit override map lives in `WEBKIT_GAP_MAP.md`.

## Current OpenGL Status And Gap

The repo does **not** have complete OpenGL support yet.  What exists today is a
software, GL-shaped smoke path that proves we can render a simple 3D scene into
a memory buffer and present it through Wayland SHM.  It is useful for validating
the GUI path, but it is not Mesa-compatible OpenGL, not hardware accelerated,
and not enough for arbitrary OpenGL applications.

The concrete gaps are:

- No kernel GPU driver with command submission.  `/dev/fb0` only exposes mode
  query/set, fill, blit, and stats; it does not expose virtio-gpu/DRM-style
  resources, command queues, contexts, or fences.
- No GPU buffer-object ABI.  There is no kernel-managed graphics buffer with
  allocation, mmap, lifetime ownership, sharing, or synchronization semantics.
- No zero-copy Wayland GPU buffer import.  The compositor accepts SHM buffers,
  but it does not support `linux-dmabuf` or an xv6-private equivalent for GPU
  buffers.
- No EGL/GLX/WGL platform layer.  Clients cannot create real GL contexts or
  swap buffers through an EGL surface tied to Wayland.
- No Mesa winsys/driver integration.  Mesa cannot target xv6 buffers or submit
  rendering work to an xv6 GPU backend.
- No full `libGL`/`libEGL` ABI.  The current smoke path is intentionally tiny
  and only covers the API shape needed by the demo.
- No shader/compiler pipeline, texture completeness, FBOs, depth/stencil,
  blending correctness, or conformance coverage beyond the simple smoke scene.
- No WebKit accelerated compositing path.  WebKit still runs in the current
  software/low-feature profile; GPU compositing should stay disabled until
  buffer sharing, EGL, and repeated navigation/close tests are stable.

The next meaningful milestone is **not** "more demo rendering"; it is a small
graphics-buffer ABI plus a Wayland import path.  Without that, any OpenGL work
remains a software compatibility shim copied through SHM.

## Two-Lane Goal: Correct API And Accelerated 3D

The desired end state is both:

- **Correct OpenGL API**: real `libEGL`/`libGL` behavior backed first by Mesa
  software rendering, so applications can create contexts, render, resize, swap,
  and tear down correctly even without GPU acceleration.
- **Accelerated 3D**: virtio-gpu 3D/virgl support through QEMU so rendering work
  can be submitted to the host GPU path instead of only rasterizing in guest
  software.

The shared foundation for both lanes is:

- xv6 graphics-buffer objects with mmap, lifetime ownership, sharing/import, and
  synchronization semantics.
- A compositor import path for those buffers, separate from `wl_shm`.
- Repeated create/draw/resize/close validation so buffer and process lifetime
  bugs are caught before enabling WebKit acceleration.

Do not build the accelerated lane first.  Without the graphics-buffer ABI,
Wayland import path, and software Mesa/EGL lane, virgl has no clean presentation
or fallback story.

## Reference Drivers

Use these as references for behavior and architecture, not as drop-in code:

- Linux `virtio-gpu` DRM: complete virtio-gpu/virgl behavior reference, including
  resource, context, command, fence, and capset handling.
- OpenBSD `viogpu`: smaller virtio-gpu display-driver reference with less Linux
  framework surface area.
- Linux Bochs DRM tiny driver: reference for the existing display-only fallback
  and framebuffer cleanup path.
- Redox `virtio-gpud`: small-OS userspace-driver architecture reference for
  separating kernel mechanics from graphics policy.

## Stage 1: Reduce Blinking On The Current Stack

- [x] Track compositor damage rectangles instead of presenting the full screen on
  every event-loop tick.
- [x] Present only the damaged union through `FB_GPU_BLIT`.
- [x] Damage old and new cursor bounds when the pointer moves.
- [x] Damage Wayland surfaces on commit/damage requests.
- [x] Damage internal windows on terminal output, monitor refresh, dragging,
  resizing, and 3D demo animation.
- [x] Keep conservative full-screen damage for structural desktop changes.

This stage keeps the existing kernel ABI and does not add dependencies.

## Stage 2: Strengthen The Framebuffer ABI

- [x] Add framebuffer present counters for full-screen vs partial blits.
- [ ] Batch multiple damage rectangles if a single union becomes too large.
- [x] Validate `FB_GPU_BLIT` source pitch and bounds more strictly.
- [ ] Consider an mmap path for framebuffer-backed staging buffers if ioctl copies
  remain a bottleneck.
- [x] Gate the boot-time framebuffer test pattern behind a debug cmdline option.

Current status:

- `FB_GPU_GET_STATS` reports full/partial/clipped/rejected blits and copy/fill
  counters.
- `_fbstat` prints those counters inside xv6.
- The boot LFB pattern is shown only with `fbtest=1`; normal GUI boots start
  from a black framebuffer.

## Stage 3: Software OpenGL Compatibility

- [x] Add a repo-local software GL renderer port before requiring real GPU
  acceleration.
- [x] Prefer a small OSMesa/TinyGL-style path first: render GL into a memory
  surface, then expose that surface to Wayland as SHM.
- [ ] Provide minimal `libGL` or `libEGL` loader stubs only for the APIs the first
  demo/client needs.
- [x] Add a simple GL smoke app that draws a rotating triangle into a Wayland
  surface.

This gives demo-level GL-shaped coverage while the kernel still exposes only a
framebuffer.

Current status:

- `/bin/glsmoke` is a no-dependency Wayland SHM client with a tiny GL-shaped
  software raster path and rotating triangle smoke scene.
- The desktop launcher includes `GL Smoke`.
- This is API-shape validation, not Mesa-compatible OpenGL, EGL, or hardware
  acceleration.

Exit criteria before calling this stage complete:

- [ ] A tiny `libGL`/`libEGL` compatibility surface exists for the smoke client,
  even if it is software-only.
- [ ] The smoke client can create/destroy its context repeatedly without leaking
  processes, file descriptors, or SHM buffers.
- [ ] The plan clearly marks the software path as a compatibility shim, not full
  OpenGL.

## Stage 4: Real GPU Buffer Infrastructure

- [ ] Choose the first real backend: virtio-gpu is the preferred QEMU target;
  Bochs framebuffer remains the fallback display-only path.
- [ ] Add a virtio-gpu or DRM/KMS-style kernel driver with resource creation,
  attach backing, transfer, flush, and basic mode/display handling.
- [ ] Add buffer-object allocation, mmap, lifetime, and sharing semantics.
- [ ] Define a small xv6 graphics-buffer ioctl ABI before attempting Mesa winsys
  integration.
- [ ] Add Wayland `linux-dmabuf` or a simpler xv6-private buffer protocol to avoid
  copying rendered buffers through SHM.
- [ ] Add explicit synchronization or a simple fence model once clients can render
  asynchronously.
- [ ] Add observability: buffer counts, bytes allocated, blit/import counts,
  command/fence counters, and clear error reporting in `_fbstat` or a sibling
  graphics diagnostic tool.
- [ ] Add a buffer import path in `wlcomp`, initially xv6-private if
  `linux-dmabuf` is too much surface area.

Exit criteria:

- [ ] A userspace test can allocate a graphics buffer, mmap/fill it, submit it
  to the display path, and release it without leaks.
- [ ] The compositor can import at least one kernel graphics buffer without
  copying through wl_shm.
- [ ] Repeated open/close and resize tests do not leave stale buffers or pinned
  mappings.

## Stage 5A: Mesa Software EGL Lane

- [ ] Build Mesa only after the kernel buffer ABI and Wayland buffer import path
  exist.
- [ ] Start with software rasterization using xv6 buffer objects.
- [ ] Add a minimal EGL platform/winsys layer that can create a Wayland surface,
  bind an xv6 graphics buffer, and present through the compositor import path.
- [ ] Provide `libEGL`, `libGLESv2`/`libGL` packaging only after the ABI is stable
  enough for repeated client startup/shutdown.
- [ ] Prefer Mesa softpipe first.  Consider llvmpipe only if the thread/runtime
  and compiler assumptions fit xv6.
- [ ] Keep this lane usable as the fallback whenever accelerated 3D is disabled
  or unavailable.

Exit criteria:

- [ ] `eglinfo` or an xv6-local EGL smoke test reports a real context.
- [ ] A basic Mesa software renderer can draw through EGL into an imported
  compositor buffer.
- [ ] The GL smoke app can switch from the repo-local shim to EGL without changing
  its rendering code.
- [ ] Context create/destroy, surface resize, buffer swap, and close/reopen loops
  survive without leaked processes, file descriptors, mappings, or buffers.
- [ ] Texture, shader, FBO, depth/stencil, blending, viewport, and scissor smoke
  tests exist before calling the API lane broadly useful.

## Stage 5B: Accelerated 3D Lane

- [ ] Extend the virtio-gpu driver beyond 2D resources to query capsets needed by
  virgl.
- [ ] Add 3D context creation/destruction, context attach, resource association,
  command submit, and fence completion.
- [ ] Build or port the userspace pieces needed by Mesa's virgl Gallium driver.
- [ ] Wire Mesa virgl to the xv6 graphics-buffer/winsys layer without bypassing
  the compositor import model.
- [ ] Support QEMU `virtio-gpu-gl`/virglrenderer as the first accelerated target.
- [ ] Keep the Mesa software EGL lane as a runtime fallback when virgl is absent
  or fails initialization.

Exit criteria:

- [ ] The same EGL/GL smoke binary can run on both software Mesa and virgl.
- [ ] Accelerated mode reports the expected virgl renderer/capset rather than the
  software renderer.
- [ ] Fences prevent tearing/stale reads when clients render asynchronously.
- [ ] Repeated create/draw/resize/close loops leave process, memory, buffer, and
  command counters stable.
- [ ] Performance counters show that the accelerated path avoids the current
  guest software raster + SHM-copy model.

## Stage 5C: WebKit Acceleration Gate

- [ ] Keep WebKit's current software/low-feature profile until EGL/GL rendering is
  stable under repeated navigation and close/reopen tests.
- [ ] Add an opt-in WebKit accelerated-compositing launch mode only after both
  the software EGL lane and virgl lane have stable teardown behavior.
- [ ] Validate with repeated Google Search navigation, tab/window close,
  resize, process cleanup, and memory/buffer counter checks.
- [ ] Keep a one-command fallback to the current software WebKit profile.

Exit criteria:

- [ ] WebKit accelerated compositing remains opt-in until it survives repeated
  navigation, resizing, terminal close/reopen, and process cleanup tests.
- [ ] The accelerated mode can be disabled without changing the rest of the GUI
  stack.
- [ ] WebKit process cleanup does not leave stale GPU buffers, fences, imported
  Wayland resources, or zombie helper processes.

## Stage 6: Full OpenGL Claim Criteria

Do not claim "complete OpenGL" until these are true:

- [ ] Public headers and libraries expose a coherent `libGL`/`libEGL` ABI.
- [ ] Context creation, surface creation, buffer swap, resize, and teardown work
  for multiple clients.
- [ ] Core fixed-function or GLES2-equivalent rendering paths are backed by Mesa
  or an explicitly scoped conformant implementation.
- [ ] Textures, shaders, vertex buffers, FBOs, depth/stencil, blending, viewport,
  scissor, and error reporting are covered by smoke/regression tests.
- [ ] GPU and software rendering paths both survive repeated launch/close cycles
  without leaked processes, mappings, buffers, or stale Wayland resources.
- [ ] WebKit can run with accelerated compositing enabled for a defined test set,
  or the plan explicitly states that WebKit acceleration is still unsupported.
- [ ] Both lanes are documented: software Mesa for API correctness, and
  virtio-gpu/virgl for accelerated 3D.  Any missing lane must be clearly marked
  unsupported.

## Validation Ladder

- Rebuild `port-wayland`, `image`, and `webkit-runtime-check`.
- Boot KVM with `root=/dev/disk0 netsurf=0 webkit=1`.
- Confirm idle desktop does not constantly full-blit.
- Move the cursor and verify reduced blinking.
- Open internal windows, drag/resize them, and verify redraw correctness.
- Run MiniBrowser through Google Search and repeated navigation.
- Add GL smoke tests only after the software GL path exists.
- For every OpenGL milestone, run repeated create/draw/resize/close loops and
  compare process, buffer, framebuffer, and memory counters before and after.
- For the API lane, run the same tests with acceleration disabled.
- For the accelerated lane, run the same tests under QEMU `virtio-gpu-gl` and
  confirm the renderer/capset indicates virgl rather than software fallback.
