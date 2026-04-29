# GPU And OpenGL Plan

This plan tracks the graphics work after the WebKit runtime became usable.
The current xv6 GUI stack is a software Wayland compositor that draws into a
userspace framebuffer and presents through `/dev/fb0`.

This is the active GPU/OpenGL plan.  WebKit runtime validation lives in
`WEBKIT_TODO.md`; the retired WebKit override map lives in `WEBKIT_GAP_MAP.md`.

## Current OpenGL Status And Gap

The repo does **not** have complete OpenGL support yet.  What exists today is a
Mesa EGL/GLES smoke path that can run through both softpipe and an initial
virtio-gpu/virgl winsys.  It is useful for validating API shape, buffer
lifetime, and accelerated command submission, but it is still not a complete
OpenGL implementation or WebKit acceleration story.

The concrete gaps are:

- The kernel has an initial virtio-gpu/virgl command-submission path and a small
  `/dev/fb0` virgl ioctl ABI for capsets, contexts, mapped 3D resources,
  transfer-to/from-host, command submission, and fence query/wait.  It is still
  not a DRM/KMS driver: command-buffer validation is intentionally narrow, the
  fence ABI is synchronous at the syscall boundary, and the Mesa virgl winsys is
  xv6-specific rather than a standard DRM winsys.
- The framebuffer BO ABI now has kernel-owned pages, caller-local mappings, and
  handle import/export, but it is still not a DRM/dmabuf ABI: no standard
  Wayland dmabuf protocol and no virtio resource backing per BO.  It has
  synchronous present fences, but not async GPU completion fences.
- Wayland now has a small xv6-private GPU-buffer import path for framebuffer BO
  handles, but not standard `linux-dmabuf`, modifiers, fences, or multi-plane
  buffer negotiation.
- Mesa softpipe now builds as a port with EGL/GLESv2 and Wayland/surfaceless
  enabled.  The software Wayland EGL path now allocates xv6 framebuffer BOs
  directly and presents them through the compositor import protocol.
- Surfaceless Mesa EGL runtime validation now works inside the VM, including
  context creation and readback.
- A first Mesa-to-compositor smoke path now exists: `mesaglsmoke` renders with
  Mesa softpipe into a surfaceless pbuffer, reads pixels back into an xv6 GPU
  BO, and presents that BO through the compositor import path.  This is still a
  copy/readback path, not a Mesa-native winsys or zero-copy swapchain.
- A Mesa-native Wayland EGL smoke path now exists: `mesawlegl` uses
  `wl_egl_window`, `eglCreateWindowSurface`, and `eglSwapBuffers` with Mesa
  softpipe.  It validates Mesa window-system surfaces, swaps, resize, and
  teardown while using xv6 GPU BO imports instead of Mesa's stock `wl_shm`
  presentation path.
- No full `libGL` ABI yet.  The current Mesa checkpoint packages `libEGL` and
  `libGLESv2`; classic `libGL` remains tied to later GLX/dispatch decisions.
  Mesa can now create a desktop OpenGL context through EGL, so the OpenGL API
  lane is real, but applications that require a `libGL.so`/GLX ABI are still
  outside the supported surface.
- No shader/compiler pipeline, texture completeness, FBOs, depth/stencil,
  blending correctness, or conformance coverage beyond the simple smoke scene.
- WebKit still runs on the current software GTK drawing path by default.  An
  explicit `webkit_accel=1` launch mode selects the virgl Mesa environment, but
  the xv6 port now keeps WebKitGTK accelerated compositing disabled unless
  `WEBKIT_XV6_FORCE_COMPOSITING_MODE=1` is explicitly set.  The latest KVM/GTK
  smoke visibly paints local WebKit content without fatal faults; true WebKit
  WebGL/accelerated backing-store support remains gated on the ANGLE/dmabuf
  platform-display path.

The next meaningful milestone is API breadth and lifecycle hardening on top of
the xv6 BO-backed Mesa Wayland path, followed by the accelerated virtio-gpu/virgl
lane.  Demo rendering alone is not enough; each new feature needs create,
resize, swap, close, and fallback coverage.

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

Integration rule: prefer upstream external-library semantics as the forcing
function.  If Mesa, libdrm, Wayland, or another graphics component needs an OS
contract that xv6 lacks, fix or extend the kernel/userspace ABI rather than
papering over the gap with fragile library-local behavior.  Local shims are
acceptable as probes and bootstraps, but durable GPU/OpenGL support should make
the OS fit the graphics stack.

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
- [x] Consider an mmap path for framebuffer-backed staging buffers if ioctl copies
  remain a bottleneck.
- [x] Gate the boot-time framebuffer test pattern behind a debug cmdline option.

Current status:

- `FB_GPU_GET_STATS` reports full/partial/clipped/rejected blits and copy/fill
  counters.
- `_fbstat` prints those counters inside xv6.
- The later framebuffer BO ABI provides mmap-capable staging buffers through
  `FB_GPU_BO_CREATE`/`FB_GPU_BO_IMPORT`; raw pointer blits remain available as a
  fallback, but the mmap-backed path is the preferred graphics-buffer surface.
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
- [x] Add a virtio-gpu or DRM/KMS-style kernel driver with resource creation,
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
- `wlcomp` now defaults to a malloc-backed compositor buffer plus `FB_GPU_BLIT`
  because the handle-based compositor backbuffer path showed visible stale top
  rows under `virtio-gpu-gl`.  The BO-present path remains available for
  targeted triage with `XV6_WLCOMP_FB_BO=1`; client BO import is still enabled.
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
- [x] Add a minimal EGL platform/winsys layer that can create a Wayland surface,
  bind an xv6 graphics buffer, and present through the compositor import path.
- [x] Provide initial `libEGL` and `libGLESv2` packaging for the surfaceless
  Mesa checkpoint.  Full `libGL` stays gated on later GLX/dispatch decisions.
- [x] Prefer Mesa softpipe first.  Consider llvmpipe only if the thread/runtime
  and compiler assumptions fit xv6.
- [x] Keep this lane usable as the fallback whenever accelerated 3D is disabled
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
- [x] Switched the desktop `glsmoke=1` launcher to run the Mesa-backed
  `/bin/mesaglsmoke` by default.  The old no-dependency shim remains available
  with `glsmoke=1 glsmoke_compat=1`.
- [x] Added `mesawlegl`, a Mesa-native Wayland EGL smoke client using
  `wl_egl_window`, `eglCreateWindowSurface`, and `eglSwapBuffers`.  A validated
  KVM run completed
  `mesawlegl --frames=8 --loops=2 --resize-every=2`; Mesa reported
  `OpenGL ES 3.1 Mesa 26.2.0-devel` with `renderer=softpipe`, and `fbstat`
  showed `rejected_blits 0`.
- [x] Added a boot-time launcher selector for the native path:
  `glsmoke=1 glsmoke_native=1` runs `/bin/mesawlegl`, while plain `glsmoke=1`
  still runs `/bin/mesaglsmoke` and `glsmoke_compat=1` still runs the local shim.
  A validated KVM boot with
  `glsmoke=1 glsmoke_native=1 glsmoke_frames=4 glsmoke_loops=1 glsmoke_resize_every=2`
  launched `mesawlegl` from `/bin/desktop` and exited with `status=0`.
  `mesawlegl` defaults `LIBGL_ALWAYS_SOFTWARE=1` and
  `MESA_LOADER_DRIVER_OVERRIDE=softpipe` so the software-lane smoke test avoids
  non-fatal Mesa loader fallback warnings; a follow-up KVM boot with
  `glsmoke_frames=2` showed the clean softpipe startup line and `status=0`.
- [x] Wired Mesa's software Wayland buffer allocator to prefer the xv6 private
  GPU-buffer protocol when available.  The same native `mesawlegl` path now
  renders through Mesa's `eglCreateWindowSurface`/`eglSwapBuffers` flow while
  allocating `/dev/fb0` BOs and exporting them to `wlcomp`; a validated KVM run
  completed
  `glsmoke=1 glsmoke_native=1 glsmoke_frames=4 glsmoke_loops=1 glsmoke_resize_every=2`
  with `fbstat` at `bo_allocs 5`, `bo_imports 4`, `bo_handles 1`, and
  `rejected_blits 0`.  The lone live BO is the compositor backbuffer.
- [x] Fixed xv6 BO-backed Mesa buffer release so resize/discard defers BO
  destruction until the compositor sends `wl_buffer.release`; this keeps the
  Wayland ownership contract intact for imported buffers.
- [x] Revalidated the BO-backed native Mesa Wayland path after the release fix
  with a KVM boot running
  `glsmoke=1 glsmoke_native=1 glsmoke_frames=8 glsmoke_loops=3 glsmoke_resize_every=2`.
  All three `mesawlegl` loops completed with `status=0`; `fbstat` reported
  `bo_allocs 25`, `bo_imports 24`, `bo_presents 112`, `bo_handles 1`,
  `rejected_blits 0`, and `ps` showed no lingering `mesawlegl` process.
- [x] Extended `mesawlegl` into the software-lane API smoke test.  Its default
  path now compiles two shader programs, renders through a VBO into an FBO color
  texture with a packed depth/stencil renderbuffer, then samples that texture to
  the Wayland EGL window using blending, viewport, and scissor state before
  `eglSwapBuffers`.
- [x] Revalidated the API smoke path in KVM with
  `glsmoke=1 glsmoke_native=1 glsmoke_frames=8 glsmoke_loops=3 glsmoke_resize_every=2`.
  All three loops reported `native-wayland api-smoke` and `status=0`; `fbstat`
  reported `bo_allocs 25`, `bo_imports 24`, `bo_presents 67`, `bo_handles 1`,
  `rejected_blits 0`, and `ps` showed no lingering `mesawlegl` process.

Exit criteria:

- [x] `eglinfo` or an xv6-local EGL smoke test reports a real context.
- [x] A basic Mesa software renderer can draw through EGL into an imported
  compositor buffer.
- [x] The desktop GL smoke launcher can run the Mesa-backed path by default,
  while retaining the repo-local shim behind `glsmoke_compat=1`.
- [x] Context create/destroy, surface resize, buffer swap, and close/reopen loops
  survive without leaked processes, file descriptors, mappings, or buffers.
  Surfaceless create/readback/destroy is currently clean.  The interim xv6 BO
  readback path has passed `mesaglsmoke --frames=12 --loops=3 --resize-every=4`,
  and Mesa's BO-backed native Wayland surface path has passed
  `mesawlegl --frames=8 --loops=3 --resize-every=2` with no stale client
  process and only the compositor backbuffer BO live afterward.
- [x] Texture, shader, FBO, depth/stencil, blending, viewport, and scissor smoke
  tests exist before calling the API lane broadly useful.

## Stage 5B: Accelerated 3D Lane

- [x] Extend the virtio-gpu driver beyond 2D resources to query capsets needed by
  virgl.
- [x] Add 3D context creation/destruction plus context resource attach/detach
  smoke coverage.
- [x] Add initial virgl command submit and virtio fence completion smoke
  coverage.
- [x] Add user-visible virgl command submission with fence query/wait semantics.
- [x] Replace synchronous fence completion with an async interrupt-driven fence
  wait path.
- [x] Build or port the userspace pieces needed by Mesa's virgl Gallium driver.
- [x] Add mapped virgl resource create/destroy plus transfer-to/from-host
  coverage as the kernel ABI foundation for Mesa's virgl winsys.
- [x] Wire Mesa virgl to the xv6 graphics-buffer/winsys layer without bypassing
  the compositor import model.
- [x] Support QEMU `virtio-gpu-gl`/virglrenderer as the first accelerated target.
- [x] Keep the Mesa software EGL lane as a runtime fallback when virgl is absent
  or fails initialization.

Current checkpoint:

- [x] Added virtio-gpu `GET_CAPSET_INFO`/`GET_CAPSET` probing in the kernel and
  exposed discovery counters through `fbstat`.
- [x] Updated `scripts/run-qemu.sh` so `QEMU_GPU=virtio-gpu-gl` automatically
  enables `gtk,gl=on` when using the GTK display backend.
- [x] Added interrupt-backed virtio-gpu queue completion with a bounded polling
  fallback and exposed `virtio_irq_completions`/`virtio_poll_fallbacks` through
  `fbstat`.  A KVM `QEMU_GPU=virtio-gpu-gl` boot plus `virgltest` reported
  `virtio_irq_completions 1207`, `virtio_poll_fallbacks 0`,
  `virtio_failures 0`, and `virtio_timeouts 0`.
- [x] Added `FB_GPU_VIRGL_GET_CAPS` so Mesa's xv6 virgl winsys can fetch the
  selected virgl capset payload instead of relying on kernel-private probing.
  KVM `QEMU_GPU=virtio-gpu-gl` validation reported
  `virgltest: ctx=2 fence=3 signaled=3 capset=1 version=1 size=308` with
  `virtio_failures 0`, `virtio_timeouts 0`, and `virtio_poll_fallbacks 0`.
- [x] Added mapped virgl resource ioctls:
  `FB_GPU_VIRGL_RESOURCE_CREATE`, `FB_GPU_VIRGL_RESOURCE_DESTROY`,
  `FB_GPU_VIRGL_TRANSFER_TO_HOST`, and `FB_GPU_VIRGL_TRANSFER_FROM_HOST`.
  Resources are backed by kernel-owned page arrays, mapped into the caller, and
  attached to the optional virgl context before use.  `_virgltest` now validates
  capset fetch, context create, 16x16 BGRA resource create/map, upload,
  download/readback, NOP submit, fence wait, resource destroy, and context
  destroy.  A KVM `QEMU_GPU=virtio-gpu-gl` run reported
  `virgltest: ctx=2 res=4 map=140737479696384 fence=3 signaled=3 capset=1 version=1 size=308`;
  `fbstat` reported `virtio_commands 69`, `virtio_transfers 21`,
  `virtio_contexts 4`, `virtio_submits 2`, `virtio_fences 2`,
  `virtio_resources 1`, `virtio_failures 0`, `virtio_timeouts 0`,
  `virtio_irq_completions 69`, and `virtio_poll_fallbacks 0`.  The single live
  resource is the persistent scanout.
- [x] Investigate GTK/virtio-gpu-gl display scaling and pointer mapping: the
  current GUI window can appear scaled down, while the guest cursor only reaches
  part of the upper-left area and moves slower than the host cursor.  This is a
  display/input correctness bug, separate from virgl command support.
  A kernel-side display-domain mismatch was fixed by making the virtio-gpu
  fallback scanout use the active `/dev/fb0` mode (`1024x768`) instead of a
  hardcoded `640x480` sidecar; this still needs visual pointer confirmation.
  The QEMU launcher now defaults to a larger non-fullscreen 1280x800 guest mode,
  VMware absolute pointer input (`QEMU_VMMOUSE=1`), GTK grab-on-hover, and a
  hidden host cursor.  With the guest and GTK canvas using the same fixed mode,
  vmmouse avoids the frozen/slow relative-PS/2 behavior seen on GNOME/Wayland
  hosts; `QEMU_VMMOUSE=0` remains available for targeted relative-input triage.
  The compositor also always presents a visible software cursor rectangle,
  clamps the cursor glyph inside the framebuffer at screen edges, ignores client
  cursor buffers for now, and redraws the cursor rectangle with every damaged
  frame so animated GL clients cannot erase the cursor or leave stale cursor
  fragments.  A default KVM/GTK `virtio-gpu-gl` boot with WebKit over the GL
  demo showed Google on top, one visible guest cursor, no cursor debris, and
  active text entry in the browser address bar.  A follow-up KVM/GTK
  `virtio-gpu-gl` run drove pointer motion across the animated Mesa Wayland EGL
  demo through QMP and captured a 1280x800 screenshot with a single clean cursor,
  no stale pointer fragments, and no GL-display corruption.
- [x] Fix or retire compositor BO-present backing before making it the default
  again.  A KVM/GTK `virtio-gpu-gl` screenshot showed the BO-backed compositor
  path leaving stale black rows at the top of the display; forcing the compositor
  through malloc plus `FB_GPU_BLIT` removed the artifact while preserving client
  BO import.  The kernel BO-present copy path now uses explicit volatile stores
  to the framebuffer BAR, and the BO-backed compositor path is now retired as
  the default.  It remains opt-in for targeted triage with
  `XV6_WLCOMP_FB_BO=1`; client BO import stays enabled.
- [x] Validated plain `QEMU_GPU=virtio-gpu` in KVM: the kernel logged
  `capsets=0`, `virtio_gpu: no 3D capsets advertised`, and `fbstat` reported
  `virtio_capsets 0`, `virtio_virgl 0`, with no virtio failures or timeouts.
- [x] Validated `QEMU_GPU=virtio-gpu-gl` in KVM: the kernel logged
  `features0=0x30000003 scanouts=1 capsets=2`,
  `capset[0] id=1 version=1 size=308`, and
  `virtio_gpu: virgl capset ready id=1 version=1 size=308`; `fbstat` reported
  `virtio_capsets 2`, `virtio_virgl 1`, `virtio_virgl_version 1`,
  `virtio_virgl_size 308`, with no virtio failures or timeouts.
- [x] Validated `QEMU_GPU=virtio-gpu-gl` context/resource smoke in KVM: the
  kernel logged `virtio_gpu: 3D context smoke ok ctx=1 capset=1 resource=1`.
  `fbstat` reported `virtio_contexts 2`, `virtio_resources 1`,
  `virtio_failures 0`, and `virtio_timeouts 0`; the remaining live resource is
  the persistent scanout.
- [x] Added and validated a minimal `SUBMIT_3D` NOP command with
  `VIRTIO_GPU_FLAG_FENCE`.  KVM boot logged
  `virtio_gpu: 3D context smoke ok ctx=1 capset=1 resource=1 fence=2`; `fbstat`
  reported `virtio_submits 1`, `virtio_fences 1`, `virtio_last_fence 2`,
  `virtio_failures 0`, and `virtio_timeouts 0`.
- [x] Fixed the virtio-gpu scanout fallback for `virtio-gpu-gl`: when
  `GET_DISPLAY_INFO` does not advertise an enabled scanout, the persistent
  virtio resource now follows the active Bochs `/dev/fb0` resolution.  KVM boot
  now logs `virtio_gpu: using fb0 mode 1024x768 for scanout fallback` and
  `persistent scanout resource=3 size=1024x768`.
- [x] Added a minimal `/dev/fb0` virgl userspace ABI:
  `FB_GPU_VIRGL_CTX_CREATE`, `FB_GPU_VIRGL_CTX_DESTROY`,
  `FB_GPU_VIRGL_SUBMIT`, and `FB_GPU_VIRGL_FENCE`.  The first submit ABI accepts
  up to 256 KiB of virgl command dwords and returns the completed virtio fence
  id.
- [x] Added `_virgltest`, a guest smoke test that creates a virgl context,
  submits a virgl NOP, waits/queries the fence, and destroys the context.
  Validated under `QEMU_GPU=virtio-gpu-gl` with `virgltest: ctx=2 fence=3
  signaled=3`; `fbstat` reported `virtio_contexts 4`, `virtio_submits 2`,
  `virtio_fences 2`, `virtio_last_fence 3`, `virtio_failures 0`, and
  `virtio_timeouts 0`.
- [x] Validated the no-virgl fallback path under plain `QEMU_GPU=virtio-gpu`:
  `_virgltest` failed cleanly at `FB_GPU_VIRGL_CTX_CREATE`, while `fbstat`
  reported `virtio_capsets 0`, `virtio_virgl 0`, `virtio_submits 0`,
  `virtio_fences 0`, `virtio_failures 0`, and `virtio_timeouts 0`.
- [x] Enabled Mesa's upstream `virgl` Gallium driver alongside `softpipe` in the
  Mesa port.  `cmake --build build-x86_64/ports --target port-mesa -j2`
  completed and installed the rebuilt `libgallium-26.2.0-devel.so`,
  `libEGL.so.1.0.0`, and `libGLESv2.so.2.0.0`.  This proves the Mesa virgl
  userspace code now builds for xv6; runtime acceleration still needs an xv6
  virgl winsys/resource ABI instead of Mesa's stock DRM/vtest paths.
- [x] Revalidated the Mesa software fallback after adding the virgl driver to
  the Mesa build.  A KVM boot with
  `QEMU_GPU=virtio-gpu glsmoke=1 glsmoke_native=1 glsmoke_frames=4
  glsmoke_loops=1 glsmoke_resize_every=2` completed `mesawlegl` with
  `renderer=softpipe native-wayland api-smoke`; `fbstat` reported
  `bo_allocs 5`, `bo_imports 4`, `bo_handles 1`, `rejected_blits 0`,
  `virtio_failures 0`, and `virtio_timeouts 0`, and `ps` showed no lingering
  `mesawlegl` process.
- [x] Re-run the pointer visual check after the scanout fallback fix.  A
  KVM/GTK `virtio-gpu-gl` screenshot at 1280x800 now shows the guest cursor
  rendered over the OpenGL demo instead of disappearing behind partial damage
  presents.  Edge rendering is clamped so lower-right reach remains visible.
- [x] Revalidated the Mesa Wayland demo display path after the latest display
  fixes.  A KVM/GTK `QEMU_GPU=virtio-gpu-gl` boot with
  `demo3d=1 video=1024x768` launched `/bin/mesawlegl --demo`; the final
  screenshot showed the desktop icons, wallpaper, taskbar, and triangle without
  the stale black top band.  `fbstat` reported `rejected_blits 0`,
  `virtio_failures 0`, and `virtio_timeouts 0`.
- [x] Added an xv6 Mesa virgl winsys and fixed its `/dev/fb0` submit ABI layout.
  A KVM/GTK `QEMU_GPU=virtio-gpu-gl` run with
  `GALLIUM_DRIVER=virpipe MESA_LOADER_DRIVER_OVERRIDE=virpipe` completed
  `mesaeglinfo` with `GL renderer=virgl`, expected clear/readback pixel
  `64,115,166,255`, and real virgl submits/fences.
- [x] Extended the kernel virgl ABI for Mesa-sized workloads: command submission
  now accepts up to 256 KiB, 3D resources can grow up to 64 MiB, and
  attach-backing uses dynamically allocated descriptor lists instead of the old
  one-page list limit.
- [x] Revalidated native Wayland EGL on virgl with
  `mesawlegl --frames=8 --loops=2 --resize-every=2`.  Both loops completed with
  `renderer=virgl native-wayland api-smoke`; `fbstat` reported
  `virtio_submits 27`, `virtio_fences 27`, `virtio_failures 0`,
  `virtio_timeouts 0`, `bo_handles 0`, and `rejected_blits 0`; `ps` showed no
  lingering `mesawlegl` process.  The post-run GTK screenshot was clean.
- [x] Revalidated repeated Mesa Wayland EGL client loops after forcing compositor
  import synchronization for xv6 private buffers.  A KVM/headless software run
  completed three `mesawlegl` loops with
  `renderer=softpipe native-wayland api-smoke`.  A KVM/GTK virgl run completed
  three `mesawlegl` loops with `renderer=virgl native-wayland api-smoke`;
  `fbstat` reported `bo_imports 27`, `virtio_submits 34`,
  `virtio_fences 34`, `virtio_failures 0`, `virtio_timeouts 0`,
  `rejected_blits 0`, and `ps` showed no lingering `mesawlegl` process.  The
  earlier `FB_GPU_BO_IMPORT failed` race did not recur after the roundtrip fix.
- [x] Extended `mesaeglinfo` with `--api=gl` and `--all` so the same in-guest
  probe validates desktop OpenGL as well as GLES.  A KVM/GTK
  `QEMU_GPU=virtio-gpu-gl` run with
  `GALLIUM_DRIVER=virpipe MESA_LOADER_DRIVER_OVERRIDE=virpipe mesaeglinfo --all`
  completed both contexts: GLES reported
  `OpenGL ES 3.0 Mesa 26.2.0-devel` and desktop GL reported
  `3.1 Mesa 26.2.0-devel`, both with `GL renderer=virgl` and the expected
  readback pixel.  `fbstat` showed `virtio_submits 9`, `virtio_fences 9`,
  `virtio_failures 0`, and `virtio_timeouts 0`.
- [x] Added a narrow `libGL.so` and `gl.pc` shim for the Mesa EGL path.  It
  aliases the Mesa-dispatched GL entrypoints currently exposed through
  `libGLESv2.so`, so applications can link with `-lGL` when they create desktop
  OpenGL contexts through EGL.  This is not a GLX implementation.
- [x] Revalidated WebKit API close/reopen on the virgl-enabled rootfs with
  `webkit=1 webkit_accel=1 webkit_api_smoke=1 webkit_gpu_smoke=1
  webkit_reopen=2 video=1280x800`.  Both smoke launches exited cleanly; `fbstat`
  reported `virtio_failures 0`, `virtio_timeouts 0`, and the persistent scanout
  as the only live virtio resource.  WebKit still reported the GPU smoke title
  as `xv6 WebKit GPU Smoke: unavailable`, so WebGL remains blocked before Mesa.

Exit criteria:

- [x] The same EGL/GL smoke binary can run on both software Mesa and virgl.
- [x] Accelerated mode reports the expected virgl renderer/capset rather than the
  software renderer.
- [x] Fences prevent stale readback in the current synchronous virgl syscall
  path; async compositor/WebKit fences remain part of the WebKit acceleration
  gate.
- [x] Repeated create/draw/resize/close loops leave process, memory, buffer, and
  command counters stable.
- [x] Performance counters show that the accelerated path avoids the current
  guest software raster + SHM-copy model.

## Stage 5C: WebKit Acceleration Gate

- [x] Keep WebKit's current software/low-feature profile until EGL/GL rendering is
  stable under repeated navigation and close/reopen tests.
- [x] Add an opt-in WebKit accelerated-compositing launch mode only after both
  the software EGL lane and virgl lane have stable teardown behavior.
- [x] Validate WebKit API close/reopen, local-file load, process cleanup, and
  virtio counter stability under the accelerated launch mode.  Manual Google
  navigation and resize remain interactive validation items.
- [x] Keep a one-command fallback to the current software WebKit profile.

Current status:

- Default `webkit=1` still launches MiniBrowser with
  `WEBKIT_DISABLE_COMPOSITING_MODE=1`; KVM/GTK smoke reached the WebKitGTK
  MiniBrowser surface with `accel=0` and no fatal graphics/kernel faults in the
  startup/idle window.
- `webkit=1 webkit_accel=1` launches WebKit with virgl Mesa environment
  selection, but the WebKitGTK xv6 port now keeps accelerated compositing off in
  normal xv6 mode because no GTK accelerated backing store is available yet.
  Forced compositing is reserved for explicit experiments with
  `WEBKIT_XV6_FORCE_COMPOSITING_MODE=1`.
- `_fbstat` after the accelerated smoke still showed only the boot-time virgl
  submit (`virtio_submits 1`, `virtio_failures 0`, `virtio_timeouts 0`) and no
  WebKit-created BO imports, so WebKit accelerated compositing is not yet proven
  active.  The next step is to make WebKit exercise EGL/GL content and measure
  repeated navigation/close cleanup before checking the remaining gate.
- `/bin/webkitgpusmoke` is now a WebKitGTK API-level smoke client.  It creates
  `WebKitSettings`, explicitly enables WebGL, requests hardware acceleration
  policy `ALWAYS`, and loads the local smoke page with a file URI.  In xv6's
  default missing-GL-bridge mode, WebKit maps that request back to software
  drawing so the render path stays visible and stable.
- KVM/GTK validation with
  `webkit=1 webkit_accel=1 webkit_api_smoke=1 webkit_gpu_smoke=1
  webkit_timeout_ms=25000 video=1280x800` loaded and visibly painted
  `/share/webkit/gpu-smoke.html`; the page title reached
  `xv6 WebKit GPU Smoke`, the desktop timed out and shut down cleanly, and the
  log contained no fatal page faults, coredumps, panics, `vma_alloc` warnings,
  or virtio-gpu failures.
- WebKit WebGL/active accelerated compositing is still blocked before it reaches
  Mesa's working virgl lane.  Source inspection points at the GTK WebKit path's
  missing ANGLE/dmabuf/display contract: the upstream 2.42.5 code has GBM,
  surfaceless, and LibWPE paths, but xv6 currently lacks the DRM render-node,
  dmabuf, modifier, and fence contracts WebKitGTK expects for the accelerated
  backing store.  The durable fix should extend xv6 toward that OS contract
  rather than pretending the software drawing path is full WebKit GPU
  compositing.

Exit criteria:

- [ ] WebKit accelerated compositing remains opt-in until it survives repeated
  navigation, resizing, terminal close/reopen, and process cleanup tests.
- [x] The accelerated mode can be disabled without changing the rest of the GUI
  stack.
- [x] WebKit API smoke cleanup does not leave stale GPU buffers, fences,
  imported Wayland resources, or zombie helper processes in the validated
  local-file close/reopen path.

## Stage 6: Full OpenGL Claim Criteria

Do not claim "complete OpenGL" until these are true:

- [x] Public headers and libraries expose a coherent initial `libGL`/`libEGL`
  ABI for EGL-created contexts.  `libEGL` can create GLES and desktop OpenGL
  contexts; `libGL.so` is now packaged as an xv6 Mesa shim over the same
  dispatched GL entrypoints.  GLX remains intentionally unsupported.
- [x] Context creation, surface creation, buffer swap, resize, and teardown work
  for multiple clients.
- [x] Core fixed-function or GLES2-equivalent rendering paths are backed by Mesa
  or an explicitly scoped conformant implementation.
- [x] Textures, shaders, vertex buffers, FBOs, depth/stencil, blending, viewport,
  scissor, and error reporting are covered by smoke/regression tests.
- [x] GPU and software rendering paths both survive repeated launch/close cycles
  without leaked processes, mappings, buffers, or stale Wayland resources.
- [x] WebKit can run the defined local-file API smoke and close/reopen test set
  under the virgl-capable launch environment, with accelerated compositing
  intentionally mapped back to the software drawing path.  WebGL/active GPU
  compositing remains unsupported until the ANGLE/dmabuf platform-display gap is
  fixed.
- [x] Both lanes are documented: software Mesa for API correctness, and
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
