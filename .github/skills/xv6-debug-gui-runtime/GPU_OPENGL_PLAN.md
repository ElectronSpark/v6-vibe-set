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
the GUI path.  The ports tree now also has a Mesa softpipe/EGL/GLESv2 build
checkpoint, but it is not wired to an xv6 winsys/compositor buffer path yet, is
not hardware accelerated, and is not enough for arbitrary OpenGL applications.

The concrete gaps are:

- No kernel GPU driver with command submission.  `/dev/fb0` only exposes mode
  query/set, fill, blit, and stats; it does not expose virtio-gpu/DRM-style
  resources, command queues, or contexts.
- The framebuffer BO ABI now has kernel-owned pages, caller-local mappings, and
  handle import/export, but it is still not a DRM/dmabuf ABI: no standard
  Wayland dmabuf protocol and no virtio resource backing per BO.  It has
  synchronous present fences, but not async GPU completion fences.
- Wayland now has a small xv6-private GPU-buffer import path for framebuffer BO
  handles, but not standard `linux-dmabuf`, modifiers, fences, or multi-plane
  buffer negotiation.
- Mesa softpipe now builds as a port with EGL/GLESv2 and Wayland/surfaceless
  enabled, but there is not yet an xv6 Mesa winsys/platform path that targets
  xv6 graphics buffers or compositor imports.
- Surfaceless Mesa EGL runtime validation now works inside the VM, including
  context creation and readback.  Window-system surfaces, buffer swaps, resize,
  and compositor-buffer teardown are still not wired to Mesa.
- A first Mesa-to-compositor smoke path now exists: `mesaglsmoke` renders with
  Mesa softpipe into a surfaceless pbuffer, reads pixels back into an xv6 GPU
  BO, and presents that BO through the compositor import path.  This is still a
  copy/readback path, not a Mesa-native winsys or zero-copy swapchain.
- No full `libGL` ABI yet.  The current Mesa checkpoint packages `libEGL` and
  `libGLESv2`; classic `libGL` remains tied to later GLX/dispatch decisions.
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
- [x] Batch multiple damage rectangles instead of always presenting one large
  union; collapse back to a union only when that is cheaper.
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
- `wlcomp` defaults to damage-driven presentation without a periodic full-screen
  repair pass.  Set `XV6_WLCOMP_REPAIR_MS=<n>` only when deliberately bounding
  suspected missed-damage artifacts during triage.

## Stage 3: Software OpenGL Compatibility

- [x] Add a repo-local software GL renderer port before requiring real GPU
  acceleration.
- [x] Prefer a small OSMesa/TinyGL-style path first: render GL into a memory
  surface, then expose that surface to Wayland as SHM.
- [x] Provide minimal `libGL` or `libEGL` loader stubs only for the APIs the first
  demo/client needs.
- [x] Add a simple GL smoke app that draws a rotating triangle into a Wayland
  surface.

This gives demo-level GL-shaped coverage while the kernel still exposes only a
framebuffer.

Current status:

- `/bin/glsmoke` is a no-dependency Wayland client that creates an EGL display,
  EGL window surface, GLES2-style context, and rotating triangle smoke scene
  through repo-local `libEGL.a` and `libGLESv2.a` compatibility libraries.
- The desktop launcher includes `GL Smoke` and supports boot-time stress knobs:
  `glsmoke_frames=<n>` and `glsmoke_loops=<n>`.
- This is API-shape validation, not Mesa-compatible EGL/OpenGL or hardware
  acceleration.  The compatibility libraries intentionally implement only the
  calls used by the smoke client.
- Fresh VM validation reached `glsmoke: EGL 1.4, GL OpenGL ES 2.0 xv6-compat`;
  repeated short runs exited cleanly and `_fbstat` showed partial blits
  advancing with no rejected blits.
- A fresh KVM rootfs boot with
  `glsmoke=1 glsmoke_frames=5 glsmoke_loops=3` completed three independent
  Wayland/EGL/GLES create-render-destroy cycles:
  `glsmoke[0..2]: complete frames=5 status=0`, with no matching panic, fatal
  fault, warning, leak, or failed-operation log lines.

Exit criteria before calling this stage complete:

- [x] A tiny `libGL`/`libEGL` compatibility surface exists for the smoke client,
  even if it is software-only.
- [x] The smoke client can create/destroy its context repeatedly without leaking
  processes, file descriptors, or SHM buffers.
- [x] The plan clearly marks the software path as a compatibility shim, not full
  OpenGL.

## Stage 4: Real GPU Buffer Infrastructure

- [x] Choose the first real backend: virtio-gpu is the preferred QEMU target;
  Bochs framebuffer remains the fallback display-only path.
- [x] Add a minimal virtio-gpu PCI transport driver that negotiates features,
  initializes the control queue, reads device config, and issues
  `GET_DISPLAY_INFO`.
- [x] Validate the first virtio-gpu 2D resource command sequence:
  create-resource-2d, attach-backing, transfer-to-host-2d, resource-flush, and
  unref.
- [x] Validate basic virtio-gpu scanout programming by binding the smoke
  resource to scanout 0, flushing it, and detaching scanout before unref.
- [x] Keep a persistent scanout-sized virtio-gpu resource alive after boot,
  backed by contiguous buddy pages and bound to scanout 0.
- [x] Mirror `/dev/fb0` damage into the persistent virtio-gpu resource and
  submit transfer/flush for framebuffer writes, blits, fills, copy-rects, and
  buffer-object presents.
- [ ] Add a virtio-gpu or DRM/KMS-style kernel driver with resource creation,
  attach backing, transfer, flush, and basic mode/display handling.
- [x] Add first-pass buffer-object allocation, mmap, and lifetime semantics.
- [x] Add framebuffer BO handle export/import semantics with real shared
  page-backed mappings.
- [x] Define a small xv6 graphics-buffer ioctl ABI before attempting Mesa winsys
  integration.
- [x] Add Wayland `linux-dmabuf` or a simpler xv6-private buffer protocol to avoid
  copying rendered buffers through SHM.
- [x] Add explicit synchronization or a simple fence model once clients can render
  asynchronously.
- [x] Add observability: buffer counts, bytes allocated, blit/import counts,
  command/fence counters, and clear error reporting in `_fbstat` or a sibling
  graphics diagnostic tool.
- [x] Add a buffer import path in `wlcomp`, initially xv6-private if
  `linux-dmabuf` is too much surface area.

Current status:

- `scripts/run-qemu.sh` accepts `QEMU_GPU=bochs|virtio-gpu|virtio-gpu-primary|virtio-gpu-gl|virtio-gpu-gl-primary|none`.
  `bochs` remains the default; `virtio-gpu` attaches a sidecar virtio-gpu PCI
  device while preserving the working Bochs `/dev/fb0` fallback.
- The x86 PCI scan recognizes virtio-gpu transitional and modern PCI IDs, stores
  discovery data, and logs BARs, IRQ routing, and virtio PCI cap offsets.
- `kernel/virtio_gpu.c` now initializes the modern PCI transport for sidecar
  virtio-gpu, maps common/notify/ISR/device config capabilities, brings up queue
  0, and queries display information.  Bochs `/dev/fb0` is still the active
  display fallback.
- Fresh KVM validation with `QEMU_GPU=virtio-gpu` detected `1af4:1050`, logged
  the virtio-gpu common/notify/ISR/device caps, registered Bochs `/dev/fb0`, and
  completed a `glsmoke` frame-limited run.
- A fresh headless boot with `QEMU_GPU=virtio-gpu QEMU_NET=0` logged
  `virtio_gpu: initialized queues=2 features0=0x30000002 scanouts=1 capsets=0`
  and `virtio_gpu: display info ok scanout0=1280x800+0+0`.
- The same boot now also logs
  `virtio_gpu: resource smoke ok resource=1 size=32x32 bytes=4096`, proving the
  transport can submit the basic 2D resource lifecycle to QEMU.
- The virtio-gpu driver tracks resource IDs, backing bytes, live resource count,
  command completions, failures, timeouts, transfers, and flushes.  `fbstat`
  reports these alongside the existing Bochs framebuffer counters; a validated
  virtio-gpu boot showed `virtio_commands 6`, `virtio_failures 0`,
  `virtio_timeouts 0`, `virtio_resources 0`, `virtio_transfers 1`, and
  `virtio_flushes 1`.
- Basic scanout programming is wired into the smoke sequence.  A validated boot
  showed `virtio_commands 8`, `virtio_failures 0`, `virtio_timeouts 0`,
  `virtio_resources 0`, `virtio_transfers 1`, `virtio_flushes 1`, and
  `virtio_scanouts 2`.
- The virtio-gpu driver now supports multi-page contiguous resource backing via
  the buddy allocator and leaves a persistent 1280x800 scanout resource attached
  after the smoke cycle.  A validated headless KVM boot logged
  `virtio_gpu: persistent scanout resource=2 size=1280x800 bytes=4096000
  alloc=4194304`; `/bin/fbstat` then reported `virtio_commands 13`,
  `virtio_failures 0`, `virtio_timeouts 0`, `virtio_resources 1`,
  `virtio_resource_bytes 4096000`, `virtio_transfers 2`, `virtio_flushes 2`,
  and `virtio_scanouts 3`.
- `/dev/fb0` damage is now mirrored into that persistent virtio-gpu resource.
  The bridge copies the damaged rectangle from the Bochs linear framebuffer into
  virtio backing and submits transfer/flush for writes, blits, fills,
  copy-rects, mode clears, and buffer-object presents.  A validated headless KVM
  compositor boot showed `virtio_commands 35`, `virtio_failures 0`,
  `virtio_timeouts 0`, `virtio_resources 1`, `virtio_transfers 13`, and
  `virtio_flushes 13`, confirming runtime presents are reaching the virtio-gpu
  command path.
- `/dev/fb0` BOs are now kernel-owned page arrays with stable handles.
  `FB_GPU_BO_CREATE` maps the pages into the creator, `FB_GPU_BO_IMPORT` maps the
  same pages into the importer, `FB_GPU_BO_PRESENT` reads directly from BO pages
  by byte offset, and `FB_GPU_BO_DESTROY` drops only the handle while live VM
  mappings continue to be released by normal `munmap()`/exit teardown.
  Pointer-based blit/present remains available as fallback.  A validated
  headless KVM run completed `gpubuftest 4`, including writes through the
  imported mapping; `/bin/fbstat` showed `bo_allocs 5`, `bo_presents 41`,
  `bo_handles 1`, `bo_imports 4`, `rejected_blits 0`, `virtio_failures 0`, and
  `virtio_timeouts 0`.  The remaining live BO handle is the compositor
  backbuffer.
- `wlcomp` uses `FB_GPU_BO_CREATE` for its compositor backbuffer when available
  and presents damage with `FB_GPU_BO_PRESENT`; it falls back to malloc plus
  `FB_GPU_BLIT` on older kernels.
- `wlcomp` advertises a small `xv6_gpu_buffer_manager` Wayland global.  Clients
  can allocate an exportable `/dev/fb0` BO, render into their caller-local
  mapping, and create a `wl_buffer` from the handle; the compositor imports that
  handle into its own VM with `FB_GPU_BO_IMPORT` and composites from the shared
  pages without `wl_shm`.
- The xv6 EGL/GLES smoke shim now prefers the private GPU-buffer protocol and
  falls back to `wl_shm` if unavailable.  A validated headless KVM run with
  `glsmoke=1 glsmoke_frames=5 glsmoke_loops=2` completed both loops; `_fbstat`
  reported `bo_allocs 3`, `bo_imports 2`, `bo_presents 26`, `bo_handles 1`, and
  `rejected_blits 0`.
- `glsmoke` can now stress BO-backed EGL surface resize churn with
  `--resize-every=N`; the desktop launcher exposes this as
  `glsmoke_resize_every=<n>`.  The resize path recreates the EGL surface over
  alternating window sizes while keeping the Wayland toplevel/context alive,
  which exercises compositor import release and BO destroy/recreate behavior.
  A validated KVM run with
  `glsmoke=1 glsmoke_frames=8 glsmoke_loops=2 glsmoke_resize_every=2` left
  `_fbstat` at `bo_handles 1`, `bo_allocs 6`, `bo_imports 5`,
  `bo_presents 71`, and `rejected_blits 0`; the one remaining handle is the
  compositor backbuffer.
- `/dev/fb0` now assigns a monotonic completed fence to each handle-based
  `FB_GPU_BO_PRESENT` and exposes `FB_GPU_BO_FENCE` for query/wait semantics.
  This is still synchronous because BO presents complete before the ioctl
  returns, but it gives Mesa/Wayland integration a stable synchronization shape
  to build on before true async virtio/virgl fences exist.  A validated KVM run
  completed `glsmoke=1 glsmoke_frames=5 glsmoke_loops=2` and `gpubuftest 4`;
  `_fbstat` showed `bo_fences 34`, `bo_fence_waits 4`, `bo_imports 6`,
  `bo_handles 1`, and `rejected_blits 0`.
- `_gpubuftest` validates repeated create/fill/present/munmap cycles.  A
  headless KVM run completed 6 cycles and `_fbstat` reported `bo_allocs 6`,
  `bo_presents 6`, and `rejected_blits 0`.
- A headless KVM compositor boot logged
  `wlcomp: using fb GPU buffer addr=... size=3145728 pitch=4096` and completed a
  `glsmoke` frame-limited run.

Exit criteria:

- [x] A userspace test can allocate a graphics buffer, mmap/fill it, submit it
  to the display path, and release it without leaks.
- [x] The compositor can import at least one kernel graphics buffer without
  copying through wl_shm.
- [x] Repeated open/close and resize tests do not leave stale buffers or pinned
  mappings.

## Stage 5A: Mesa Software EGL Lane

- [x] Build Mesa only after the kernel buffer ABI and Wayland buffer import path
  exist.
- [x] Start with software rasterization using xv6 buffer objects.
- [ ] Add a minimal EGL platform/winsys layer that can create a Wayland surface,
  bind an xv6 graphics buffer, and present through the compositor import path.
- [x] Provide initial `libEGL` and `libGLESv2` packaging for the surfaceless
  Mesa checkpoint.  Full `libGL` stays gated on later GLX/dispatch decisions.
- [x] Prefer Mesa softpipe first.  Consider llvmpipe only if the thread/runtime
  and compiler assumptions fit xv6.
- [ ] Keep this lane usable as the fallback whenever accelerated 3D is disabled
  or unavailable.

Current checkpoint:

- [x] Added `libdrm` core as a Meson port and built it into the xv6 sysroot.
- [x] Added Mesa upstream as a port, configured for Wayland/surfaceless EGL,
  GLESv2, OpenGL core sources, Gallium softpipe, no LLVM, no Vulkan, no GLX, and
  no hardware Gallium drivers.
- [x] Added per-port Meson C++ flags so Mesa can force-include the existing xv6
  `std::mutex` compatibility layer without applying that C++ header to C
  sources.
- [x] `cmake --build build-x86_64/ports --target port-mesa -j2` completes and
  installs `libEGL.so.1.0.0`, `libGLESv2.so.2.0.0`,
  `libgallium-26.2.0-devel.so`, EGL/GLES/GL headers, and pkg-config metadata.
- [x] Added `mesaeglinfo`, an xv6-local surfaceless EGL/GLES probe, to the
  Wayland port.  It reports Mesa softpipe, clears/reads a pbuffer pixel, and
  exits cleanly in a VM.
- [x] Fixed VM teardown iteration over high-address maple-tree gaps so dynamic
  Mesa mappings near `UVMTOP` are unmapped before `freewalk()`.
- [x] Added `mesaglsmoke`, a Mesa-backed Wayland client that renders with real
  Mesa softpipe and presents through an xv6 GPU BO imported by `wlcomp`.
- [x] Extended `mesaglsmoke` with repeated lifecycle and resize coverage:
  `--loops=N` recreates the whole Wayland/EGL client, and `--resize-every=N`
  recreates the Mesa pbuffer plus xv6 GPU BO while running.
- [ ] Add the xv6 Mesa winsys/platform glue and switch the local GL smoke path
  from the repo shim to Mesa EGL.

Exit criteria:

- [x] `eglinfo` or an xv6-local EGL smoke test reports a real context.
- [x] A basic Mesa software renderer can draw through EGL into an imported
  compositor buffer.
- [ ] The GL smoke app can switch from the repo-local shim to EGL without changing
  its rendering code.
- [ ] Context create/destroy, surface resize, buffer swap, and close/reopen loops
  survive without leaked processes, file descriptors, mappings, or buffers.
  Surfaceless create/readback/destroy is currently clean; window-system surfaces
  still need the winsys/compositor path.  The interim readback path has passed
  `mesaglsmoke --frames=12 --loops=3 --resize-every=4`.
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
