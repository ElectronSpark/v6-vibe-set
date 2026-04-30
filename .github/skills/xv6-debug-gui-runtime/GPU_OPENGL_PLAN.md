# GPU And OpenGL Plan

This plan tracks the graphics work after the WebKit runtime became usable.
The current xv6 GUI stack is a software Wayland compositor that draws into a
userspace framebuffer and presents through `/dev/fb0`.

This is the active GPU/OpenGL plan.  WebKit runtime validation lives in
`WEBKIT_TODO.md`; the retired WebKit override map lives in `WEBKIT_GAP_MAP.md`.

## Current OpenGL Status And Gap

The repo has a real Mesa EGL/OpenGL path now, with both software Mesa and
virtio-gpu/virgl acceleration working for xv6-native smoke clients.  It should
no longer be described as only an API-shape shim.  It is still **not** complete
desktop OpenGL or complete WebKit GPU acceleration: GLX is unsupported, WebKit's
active accelerated compositing path is still experimental, and the user-visible
3D/WebKit validation has open correctness and stability bugs.

The concrete gaps are:

- The kernel has an initial virtio-gpu/virgl command-submission path and a small
  `/dev/fb0` virgl ioctl ABI for capsets, contexts, mapped 3D resources,
  transfer-to/from-host, command submission, and fence query/wait.  It is still
  not a DRM/KMS driver: command-buffer validation is intentionally narrow, the
  fence ABI is synchronous at the syscall boundary, and the Mesa virgl winsys is
  xv6-specific rather than a standard DRM winsys.  User-created virgl contexts
  and resources are now owned by thread group and are released on `/dev/fb0`
  close and last-thread process exit.
- The framebuffer BO ABI now has kernel-owned pages, caller-local mappings, and
  handle import/export.  It has a narrow PRIME-like fd import/export contract
  and enough single-plane `linux-dmabuf` import for linear ARGB/XRGB buffers,
  but it is still not a complete DRM/dmabuf ABI: no modifier negotiation beyond
  linear, no virtio resource backing per BO, and no async GPU completion fences.
- Wayland now has both a small xv6-private GPU-buffer import path and a
  standard `zwp_linux_dmabuf_v1` import path for single-plane linear
  ARGB8888/XRGB8888 buffers.  It still lacks multi-plane negotiation and
  standard explicit-sync release fences.
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
- `libGL.so` exists as an EGL-backed Mesa dispatch shim and Mesa can create both
  GLES and desktop OpenGL contexts through EGL.  This is a usable OpenGL lane for
  EGL clients, not a GLX implementation.  Applications that require X11/GLX or
  Linux DRM render-node discovery remain outside the supported surface.
- Shader, texture, VBO, FBO, depth/stencil, blending, viewport, scissor, resize,
  and teardown paths have smoke coverage, but not conformance coverage.
- WebKit still runs on the current GTK software presentation path.  An explicit
  `webkit_accel=1` launch mode selects the virgl Mesa environment while keeping
  WebKitGTK accelerated compositing disabled; forcing WebKit's accelerated
  backing store is reserved for experiments with
  `WEBKIT_XV6_FORCE_COMPOSITING_MODE=1` because the current GTK/Wayland backing
  store cannot present that path correctly yet.
- Forced WebKit WebGL is no longer blocked before Mesa: the current smoke page
  reaches a WebGL context and the first rendered frame title.  It is not stable
  yet: a later UI/WebKit process crash has been observed, and active accelerated
  compositing still lacks WebKitGTK's expected dmabuf/render-node/fence contract.
- The safe WebKit GPU-mode baseline is now "GPU device and virgl enabled,
  WebKit presented through the coordinated software drawing area."  In a
  KVM/GTK `virtio-gpu-gl` VM, `webkit=1 webkit_accel=1` confirms virgl capsets
  and the 3D context smoke at boot, launches MiniBrowser with the virgl Mesa
  environment, and loads real Google HTTPS documents.  `robots.txt` reaches
  `load-finished`/`readyState=complete` and visibly paints page text; the Google
  Search compatibility endpoint reaches the `Google Search` title,
  `load-finished`, and then Google's anti-automation redirect.  Forced WebKit
  accelerated backing-store presentation still produces blank content or UI
  crashes, so it is not the accepted default.
- The 3D demo is a real Mesa/virgl scene, but its spherical symmetric faceted
  object currently has a visual mesh/culling flaw that must be fixed before the
  demo is accepted as the visual validation target.
- KVM/GTK display and input are still part of the GPU acceptance surface.  Past
  regressions included a too-small host window, guest/host scale mismatch, and
  a non-moving cursor.  These must remain explicit validation gates, not side
  observations.

The next meaningful milestone is not "turn on GPU for WebKit" and not
"make MiniBrowser happier."  The latest VM checks show a larger, more
fundamental GPU integration gap:

- xv6-native Mesa clients can use virgl and present through the current
  xv6-private buffer path.
- The desktop compositor still composites in software into `/dev/fb0`, then
  mirrors damaged rectangles into the virtio-gpu scanout path.
- Higher-level consumers such as GTK/WebKit expose the missing contracts, but
  they should not drive the next steps directly.  First make xv6 expose a
  coherent GPU device model, buffer-sharing model, synchronization model, and
  compositor import model that Mesa and Wayland can use normally.

The plan below is therefore centered on **fundamental GPU acceleration support**:
kernel GPU objects, render/display device separation, libdrm/GBM-compatible
userspace contracts, dmabuf-style sharing, explicit fences, Wayland import, and
event-loop-safe presentation.  WebKit remains only a later validation workload.
Demo rendering alone is not enough; each new feature needs create, resize, swap,
close, crash, fallback, and resource-cleanup coverage.

## Reframed GPU Integration Gap

Do not call the stack "fully GPU accelerated" until these layers are connected:

1. **Kernel graphics object model**
   - Current: `/dev/fb0` owns simple page-backed BOs, a private virgl submit ABI,
     synchronous fences, and a persistent scanout mirror.
   - Gap: no DRM-like object namespace, render node, PRIME/dmabuf fd export,
     modifiers, explicit sync objects, pollable fences, per-client GPU address
     space, or robust GPU reset/error recovery.

2. **Userspace graphics API**
   - Current: Mesa EGL/GLES/desktop-GL contexts work for xv6-native clients
     through the private virgl winsys and xv6 GPU BO path.
   - Gap: no GLX, no standard libdrm/GBM device contract, no upstream Mesa
     winsys compatibility, and limited conformance coverage.

3. **Wayland/compositor buffer path**
   - Current: `wlcomp` supports `wl_shm` plus an xv6-private BO import protocol.
     It then software-composites into a framebuffer backing store.
   - Gap: no multi-plane `linux-dmabuf`, no non-linear modifiers, no standard
     explicit-sync release fences, no direct scanout/overlay path, and no GPU
     composition.  Client BO import avoids some copies, but the compositor is
     still the final software blender.

4. **Display scheduling and composition**
   - Current: the compositor is a software blender that writes a framebuffer and
     mirrors damaged regions into the persistent virtio-gpu scanout.
   - Gap: no GPU composition path, no page-flip/atomic commit model, no
     compositor-side GPU command queue ownership, no vsync/page-flip completion
     event, and no timeout/recovery path when a GPU operation stalls.

5. **Higher-level toolkit/browser consumers**
   - Current: GTK/WebKit are useful stressors because they naturally expect
     dmabuf/GBM/EGL/fence behavior.
   - Gap: they should remain acceptance tests until the lower graphics stack
     exists.  Browser-specific workarounds must not substitute for a real OS GPU
     contract.

6. **Validation and recovery**
   - Current: screenshot and log checks exist, and native Mesa demos validate
     virgl renderer selection.
   - Gap: no automated long-running GUI stress that combines cursor motion,
     multiple GPU clients, terminal open/close, resize, close/reopen, stalled
     submits, coredump detection, and post-run GPU resource accounting.

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
  bugs are caught before enabling higher-level toolkit acceleration.

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
  as the only live virtio resource.  That older non-WebGL render smoke did not
  prove WebKit GPU compositing; newer forced-WebGL validation below reaches a
  WebGL context and first frame, but is not stable yet.
- [x] Hardened WebKit/virgl teardown after forced WebKit WebGL experiments:
  `/dev/fb0` now tracks virgl contexts/resources by owner thread group and
  reclaims them on fd close and last-thread process exit.  The forced KVM/GTK
  WebKit WebGL smoke reaches the local `xv6 WebKit WebGL Smoke` page and shuts
  down without stale helper processes; post-timeout `fbstat` showed
  `virtio_timeouts 0`, `virtio_resources 1`, and
  `virtio_resource_bytes 4096000`, meaning only the persistent scanout remains.
  This was a cleanup checkpoint before the newer WebGL success-title run.

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
- [x] Add an opt-in WebKit GPU-mode launch after both the software EGL lane and
  virgl lane have stable teardown behavior.  The accepted mode currently means
  virgl/Mesa environment plus software WebKit presentation; forced WebKitGTK
  accelerated backing-store presentation remains experimental.
- [x] Validate WebKit API close/reopen, local-file load, process cleanup, and
  virtio counter stability under the accelerated launch mode.  Manual Google
  navigation and resize remain interactive validation items.
- [x] Keep a one-command fallback to the current software WebKit profile.
- [ ] Keep the normal MiniBrowser profile stable while forced WebGL work is in
  progress; regressions in ordinary `webkit=1` browsing block the acceleration
  gate.
  2026-04-30 note: the latest ordinary `webkit=1` regression was not a GPU
  rendering fault.  `wlcomp` was killing the tracked launcher process group and
  destroying same-pid Wayland clients during normal child reap.  That path has
  been narrowed so ordinary reap only clears launcher tracking; explicit
  close/force-close still owns teardown.
- [ ] Make the forced WebKit WebGL smoke stable after the first rendered frame:
  no UI process SIGSEGV, no helper coredumps, no stale virgl contexts/resources,
  and no compositor freeze.
- [ ] Decide and implement the durable WebKitGTK accelerated-surface contract:
  either teach xv6 enough dmabuf/render-node/fence behavior for WebKitGTK's
  expected path, or keep a clearly named xv6-private bridge with the same
  lifetime and synchronization semantics.
- [ ] Add a repeated WebKit GPU validation loop that covers local WebGL load,
  resize, terminal open/close while WebKit is foreground/background, browser
  close/reopen, and post-run `_fbstat`/process cleanup.

Current status:

- Default `webkit=1` still launches MiniBrowser with
  `WEBKIT_DISABLE_COMPOSITING_MODE=1`; KVM/GTK smoke reached the WebKitGTK
  MiniBrowser surface with `accel=0` and no fatal graphics/kernel faults in the
  startup/idle window.  A fresh 2026-04-30 KVM/GTK boot with
  `virtio-gpu-gl`, network, `webkit=1`, and `video=1280x800` again reached the
  WebKitGTK MiniBrowser surface after the launcher-reap fix; the remaining
  ordinary-profile gap is page progress beyond the initial Google URI/title
  state, not process launch.
- `webkit=1 webkit_accel=1` launches WebKit with virgl Mesa environment
  selection, but the WebKitGTK xv6 port now keeps accelerated compositing off
  because no GTK accelerated backing store is available yet.  This hybrid mode
  is the current safe default for "GPU VM + WebKit": virtio-gpu/virgl is
  initialized and usable, while WebKit content is presented through the
  coordinated software drawing area.
- 2026-04-30 validation: KVM/GTK `virtio-gpu-gl` with
  `webkit=1 webkit_accel=1 video=1280x800` and
  `webkit_url=https://www.google.com/robots.txt` reached virgl capset discovery,
  `virtio_gpu: 3D context smoke ok`, `MiniBrowser pid=... accel=1`,
  `load-finished`, `readyState=complete`, and visibly painted Google
  `robots.txt` text.  The same mode with
  `webkit_url=https://www.google.com/search?q=xv6&gbv=1` reached the
  `Google Search` title, completed the first search document, and then followed
  Google's expected anti-automation redirect.
- Forced compositing with `WEBKIT_XV6_FORCE_COMPOSITING_MODE=1` is reserved for
  explicit experiments.  The current failure mode is known: the WebKit DOM can
  load, but the content area stays blank if WebKit enters accelerated
  compositing without a presentable GTK accelerated backing store; stale runtime
  builds can also crash in `webkitWebViewBaseDraw()` when that backing store is
  null.
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
- KVM/GTK validation with forced compositing and the local WebGL smoke now gets
  past the old "WebGL unavailable" blocker: the page title has reached
  `xv6 WebKit WebGL Spherical Poly: webgl ready` and then
  `xv6 WebKit WebGL Spherical Poly: webgl spherical poly`, proving that WebKit
  created a WebGL context and reached the first rendered frame through Mesa.
  This is a partial availability pass, not a stability pass.
- A later forced-WebGL run crashed in the WebKit/UI process after the first
  frame.  That crash is now the primary WebKit GPU blocker.  Treat any
  `fatal page fault`, coredump, WebKit helper leak, stale virgl resource, or
  compositor/input freeze as a failed acceleration-gate run.
- Source inspection still points at the GTK WebKit path's missing
  ANGLE/dmabuf/display contract as the durable accelerated-compositing gap: the
  upstream 2.42.5 code has GBM, surfaceless, and LibWPE paths, but xv6 lacks the
  DRM render-node, dmabuf, modifier, and async fence contracts WebKitGTK expects
  for an accelerated backing store.  The durable fix should extend xv6 toward
  that OS contract rather than pretending the software drawing path is full
  WebKit GPU compositing.

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
- [ ] WebKit can run the defined local-file API smoke, close/reopen test set,
  and forced-WebGL smoke under the virgl-capable launch environment without
  fatal faults.  Local non-WebGL smoke is clean; forced WebGL now reaches the
  first rendered frame but still has a crash to fix before this can be checked.
- [x] Both lanes are documented: software Mesa for API correctness, and
  virtio-gpu/virgl for accelerated 3D.  Any missing lane must be clearly marked
  unsupported.

## Stage 7: Fundamental GPU Acceleration Queue

This is the active execution queue for the next GPU push.  It deliberately
focuses below MiniBrowser/WebKit.  Browser acceleration becomes meaningful only
after the kernel, Mesa, and compositor expose a normal GPU buffer and
synchronization contract.

### 7A. Define A DRM-Like Kernel Object Model

- [x] Split display and render responsibilities.
  - Keep `/dev/fb0` as a compatibility framebuffer.
  - Initial kernel slice added `/dev/gpu0` as a render-facing facade for BO,
    virgl context/resource/submit/fence, and stats ioctls.
  - Ownership is thread-group aware and BO/virgl resources are reclaimed at
    thread-group exit; ordinary `/dev/fb0` close no longer destroys render
    resources out from under layered userspace.
  - 2026-04-30 update: `/dev/gpu0` now allocates a per-open render-owner
    cookie.  BO handles, imported BO handles, virgl contexts, and virgl
    resources created through that fd are tagged with the render owner and are
    reclaimed when the render fd closes; a second `/dev/gpu0` fd in the same
    process cannot destroy the first fd's handle.  `/dev/fb0` remains the
    compatibility display path.
  - `/bin/gpubuftest --render-owner` validates fd-scoped BO ownership and
    verifies that an exported BO fd can still be imported after the creator
    render fd closes.
- [x] Replace ad hoc BO integer handles with fd-like shareable objects.
  - Initial kernel slice added `FB_GPU_BO_EXPORT_FD` and
    `FB_GPU_BO_IMPORT_FD`; an exported BO fd holds its own BO reference and can
    be imported/mapped independently of the creator's `/dev/fb0` fd.
  - `/bin/gpubuftest` now validates export/import-fd accounting and close
    cleanup.
  - A process should be able to allocate a graphics object, mmap it, export a
    capability, import it in another process, and close it independently.
  - The exported object must not depend on a still-open `/dev/fb0` fd in the
    creator.
  - 2026-04-29 update: `FB_GPU_BO_IMPORT_FD` now returns a caller-local BO
    handle as well as the mapping metadata, so PRIME-style fd import can produce
    a handle that userspace later destroys independently.
  - Remaining gap: not yet a full dmabuf object with modifiers, standard
    Wayland import, or upstream DRM fd metadata queries.
- [x] Add robust object accounting.
  - Current counters include live/peak framebuffer BO handles and bytes,
    handle imports, BO fd exports/imports/live/peak, fence waits, fence fd
    exports/queries/live/peak/poll-ready checks, `/dev/gpu0`
    opens/live opens/ioctls, virtio contexts, resources, bytes, submits,
    fences, failures, timeouts, IRQ completions, and poll fallbacks.
  - `_fbstat` prefers `/dev/gpu0` and prints those counters.
  - 2026-04-29 KVM validation after `/bin/gpubuftest 3` showed
    `bo_fd_live 0`, `bo_fd_peak 2`, `fence_fd_live 0`, `fence_fd_peak 3`,
    `fence_fd_polls 3`, and `fence_fd_poll_ready 3`.
  - 2026-04-29 update: fd-imported BO handles now participate in the same
    live/peak accounting and explicit destroy path as creator handles.
  - Remaining gap: forced cleanup counts and richer reset status.
- [x] Add per-context reset/error policy.
  - A bad userspace submit should fail the client submit, not freeze the
    compositor or kernel.
  - Initial kernel slice marks a virgl user context failed when its submit path
    returns an error; later submits, context-bound resource creation, and
    transfers tied to that failed context return `EIO` while context/resource
    destroy remains available for cleanup.
  - `_fbstat` now exposes `virtio_context_failed` and
    `virtio_context_failures`.  2026-04-29 non-3D KVM validation kept both at
    `0` after buffer/fence smoke, with `virtio_failures 0` and
    `virtio_timeouts 0`.
  - 2026-04-30 update: `FB_GPU_VIRGL_SUBMIT_FORCE_FAIL` gives the guest a
    deterministic fault-injection path without sending undefined command
    streams to QEMU.  `/bin/virgltest --bad-submit` now proves that the failed
    context rejects later submits and context-bound resource creation, remains
    destroyable, and does not poison a fresh virgl context.
  - 2026-04-30 validation: `GPU_VALIDATE_VISIBLE_3D=1 scripts/gpu-validate.sh`
    passed with `virgltest: bad-submit isolated`, `virtio_context_failed 0`,
    `virtio_context_failures 1`, `virtio_failures 0`, and
    `virtio_timeouts 0`.
- [x] Add reset-status and async waiter recovery policy.
  - Timeouts should mark the affected context failed and release waiters.
  - A wedged command should wake pending fence waits and keep the compositor
    responsive.
  - 2026-04-30 update: virtio-gpu command timeout handling now marks all live
    virgl contexts failed before returning the timed-out command, so subsequent
    context submits/resources fail instead of pretending the render queue is
    healthy.
  - 2026-04-30 validation: the visible virgl lane injects a deterministic
    failed context with `virgltest --bad-submit`, proves the failed context
    rejects later work, then proves a fresh context can submit and fence
    normally with `virtio_context_failed 0`, `virtio_context_failures 1`, and
    `virtio_timeouts 0`.
  - Full PCI-level virtio-gpu device teardown/reinitialization remains
    intentionally deferred until the queue grows truly asynchronous waiters or
    the device can wedge independently of the synchronous submit timeout path.
    Today the recovery contract is context fail-stop plus prompt waiter return,
    which is the path the compositor and Mesa clients exercise.

### 7B. Implement Real Synchronization Primitives

- [x] Promote the current synchronous fence numbers into explicit fence objects.
  - Initial kernel slice added `FB_GPU_FENCE_EXPORT_FD` and
    `FB_GPU_FENCE_QUERY`; a BO present fence can now be exported as a custom fd
    object, queried, waited, and closed independently of the BO handle.
  - Fence fds implement `poll(2)` readiness for `POLLIN`/`POLLRDNORM` once the
    target fence is signaled.
  - `/bin/gpubuftest` validates fence fd export/query/poll/close accounting.
    2026-04-29 KVM validation showed three BO cycles with
    `bo_handles 0`, `bo_live_bytes 0`, `fence_fd_exports 3`,
    `fence_fd_queries 3`, `virtio_failures 0`, and `virtio_timeouts 0`.
  - 2026-04-30 update: virgl submit fences now have the same explicit custom-fd
    shape via `FB_GPU_VIRGL_FENCE_EXPORT_FD` and
    `FB_GPU_VIRGL_FENCE_QUERY_FD`; `/bin/virgltest` exports a submit fence,
    waits/queries it through the fd, and closes it during the visible virgl
    validation lane.
  - Fences now have query, wait, close, and poll/select readiness semantics for
    the currently synchronous BO-present and virgl-submit completion model.
  - Remaining gap moved to reset/recovery: waits are not backed by a fully
    asynchronous scheduler/waitqueue yet, so a wedged device still needs the
    Stage 7A reset and waiter recovery work.
- [x] Add acquire/release fence plumbing to buffer import.
  - Producers must not overwrite a buffer until the compositor releases it.
  - The compositor must not sample a buffer before the producer's acquire fence
    is signaled.
  - The private `xv6_gpu_buffer_manager` protocol is now version 2 and accepts
    `create_buffer_with_fence(handle, width, height, stride, format, fd)`.
    `wlcomp` polls the acquire fence fd before sampling, defers
    `wl_buffer.release` and frame callbacks until the fence is consumed, and
    keeps damaging the scene while a buffer is fence-blocked.
  - Rebuilt `port-wayland` after advertising the v2 global so clients can bind
    the fence-capable request.
  - Release is still represented by `wl_buffer.release`; standard explicit-sync
    release fence objects are tracked under Stage 7D.
- [x] Add validation for stuck fences.
  - A guest test should intentionally wait on completed, pending, invalid, and
    timed-out fences and prove no spinlock or event-loop stalls occur.
  - `/bin/gpubuftest` now exports a deliberately future fence fd, verifies
    zero-timeout `poll(2)` reports it as not-ready, verifies
    `FB_GPU_FENCE_QUERY | FB_GPU_FENCE_WAIT` fails immediately instead of
    blocking, and closes the fd to prove accounting returns to zero.
  - 2026-04-29 validation: `scripts/gpu-validate.sh` passed with the expanded
    fence coverage; post-run `fbstat` still required zero live BO/fence fd
    objects and zero virtio failures/timeouts.
  - 2026-04-30 update: multi-client Mesa stress exposed that fence fd release
    accounting is asynchronous through the VFS close path (`close` removes the
    descriptor, then RCU/workqueue cleanup runs the custom file release hook).
    The validator now lets that deferred cleanup quiesce before sampling
    `fbstat`, and the expanded run still requires `fence_fd_live 0`.

### 7C. Move Toward libdrm/GBM Semantics

- [x] Decide whether xv6 will provide a small real `libdrm` backend or a
  compatibility subset with the same externally visible semantics.
  - Minimum operations: open render device, create dumb/linear BO, mmap BO,
    export/import prime-like handle, create fence/sync object, submit command,
    query caps.
  - Keep the API narrow, but make it look like the contracts Mesa and toolkits
    already understand.
  - Decision: provide a narrow xv6 compatibility subset first, backed by
    `/dev/gpu0`, with fd/handle import-export semantics aligned with libdrm
    PRIME calls.  Durable kernel semantics take priority over library-only
    hacks when a graphics API expects a real OS contract.
  - KVM substrate validation passed with `/bin/gbmtest`: backend `xv6-gbm`,
    linear BO create/map/export/import/destroy completed.
- [x] Add a GBM-compatible allocation surface.
  - Support linear ARGB/XRGB buffers first.
  - Report modifiers honestly: start with linear/invalid only.
  - Add width/height/stride/format metadata and lifetime rules that match GBM
    expectations.
  - Added `ports/xv6-gbm`: a minimal `libgbm.a`, `gbm.h`, `gbm.pc`, and
    `/bin/gbmtest` for linear ARGB/XRGB BO create/map/export/import/destroy.
  - 2026-04-30 update: `ports/xv6-gbm` now also installs `libgbm.so.1.0.0`
    with `libgbm.so.1`/`libgbm.so` symlinks, so runtime clients and probes
    that expect a dynamic GBM library can resolve the xv6 GBM compatibility
    layer from the staged sysroot and rootfs.
  - Follow-up `gpubuftest 3` and `fbstat` showed `bo_handles 0`,
    `bo_live_bytes 0`, `bo_fd_live 0`, `fence_fd_live 0`,
    `rejected_blits 0`, `virtio_failures 0`, and `virtio_timeouts 0`.
- [x] Teach Mesa's xv6 winsys to prefer the libdrm/GBM-like path.
  - The current private virgl winsys should become an implementation detail or
    fallback, not the only acceleration path.
  - Native Mesa tests must still pass on both software and virgl lanes.
  - Mesa's xv6 virgl winsys now opens `/dev/gpu0` first and falls back to
    `/dev/fb0`; libdrm `drmOpen*`, render-device name, and PRIME handle/fd
    helpers now route to the xv6 render-device/BO-fd ABI.
  - KVM/GTK `QEMU_GPU=virtio-gpu-gl` validation with
    `glsmoke=1 glsmoke_accel=1 glsmoke_frames=4 glsmoke_loops=1` reported
    `renderer=virgl buffer=xv6-gpu-bo` and completed with `status=0`.

### 7D. Add Standard Wayland Buffer Import

- [x] Implement enough `linux-dmabuf` semantics for single-plane buffers.
  - Advertise ARGB8888/XRGB8888 and linear modifier first.
  - Import must validate dimensions, stride, format, ownership, and fences.
  - Import failure must report a protocol error to the client, not hang the
    compositor.
  - `wlcomp` now advertises `zwp_linux_dmabuf_v1` version 3, accepts one
    linear plane, imports the supplied fd through `FB_GPU_BO_IMPORT_FD`, wraps
    the mapped BO as a `wl_buffer`, and destroys the imported local handle when
    the Wayland buffer is released.
  - `/bin/dmabufsmoke` validates GBM BO allocation, PRIME fd export,
    `linux-dmabuf` create-immed import, compositor presentation, and process
    cleanup.
  - 2026-04-29 KVM validation passed with `/bin/dmabufsmoke`; follow-up
    `/bin/fbstat` showed `bo_allocs 1`, `bo_fd_exports 1`,
    `bo_fd_imports 1`, `bo_handles 0`, `bo_live_bytes 0`, `bo_fd_live 0`,
    `fence_fd_live 0`, `rejected_blits 0`, `virtio_failures 0`, and
    `virtio_timeouts 0`.
- [x] Keep the xv6-private buffer protocol as a bootstrap fallback.
  - Document which dmabuf semantics it lacks.
  - Do not add new clients that depend only on the private protocol unless the
    standard path is impossible for that milestone.
  - The private protocol remains for early xv6 clients and acquire-fence probes.
    It lacks dmabuf global discovery, modifier negotiation, multi-plane buffer
    description, and standard explicit-sync release-fence events, so new generic
    clients should prefer `zwp_linux_dmabuf_v1`.
- [x] Add release-fence behavior to compositor presentation.
  - After compositing or direct scanout, signal/release buffers precisely.
  - Stress with multiple clients swapping at once.
  - 2026-04-30 update: `wlcomp` now queues `wl_buffer.release` events and
    gates them on the framebuffer BO's present fence.  Replaced buffers and
    current committed buffers share the same release queue; the compositor sends
    frame callbacks only after acquire fences and the latest present fence are
    ready.
  - 2026-04-30 validation: `scripts/gpu-validate.sh` and
    `GPU_VALIDATE_VISIBLE_3D=1 GPU_VALIDATE_3D_TIMEOUT=180s
    scripts/gpu-validate.sh` both passed after the release-fence queue change.

### 7E. Move From Software Composition Toward GPU Presentation

- [x] Separate compositor rendering from final scanout.
  - Keep software composition as the fallback.
  - Add a GPU composition path or at least GPU-assisted blit/copy path that can
    be measured independently from CPU blending.
  - Current implementation renders into compositor-owned backing memory.  When
    `XV6_WLCOMP_FB_BO=1` is enabled, that backing is an xv6 GPU BO and the
    compositor presents only damaged rectangles from that BO with
    `FB_GPU_BO_PRESENT`; otherwise it falls back to user-memory `FB_GPU_BLIT`.
    Damage stats report the active present mode as `bo-present` or `user-blit`.
- [x] Add page-flip/commit completion semantics.
  - The display path should have a completion event or fence so animation and
    input are not paced by blind sleeps or synchronous flushes.
  - `FB_GPU_BO_PRESENT` returns a present fence, `FB_GPU_BO_FENCE` reports the
    latest signaled present fence, and `wlcomp` gates buffer release and frame
    callbacks on that present-fence readiness.  This is a synchronous fence
    model today, but the compositor contract is now completion-driven instead
    of sleep-driven.
- [x] Evaluate direct scanout for simple fullscreen buffers.
  - If a single fullscreen client buffer matches scanout format/stride, test
    presenting it without software composition.
  - Fall back cleanly when overlays/direct scanout are unavailable.
  - 2026-04-30 update: the current compositor cannot enable direct scanout as
    a normal path yet because the taskbar, menu, internal windows, and
    compositor-owned cursor are all software overlays.  Bypassing composition
    would make those disappear unless hardware cursor/overlay planes or an
    explicit fullscreen-no-overlays mode is added.  Direct scanout is therefore
    deferred as a measured future optimization; the GPU-present BO path remains
    the supported acceleration lane and falls back cleanly to software blit.

### 7F. Harden KVM/GTK Display And Input As GPU Acceptance Gates

- [x] Make display geometry deterministic.
  - Non-fullscreen GTK should show the full 1280x800 guest without scaling the
    guest canvas down.
  - The guest cursor must reach all four edges with `virtio-tablet-pci`.
  - QEMU launch defaults should prevent host-window resize from changing the
    validation surface.
  - 2026-04-30 update: `scripts/run-qemu.sh` now has a dry-run contract and
    `scripts/gpu-validate.sh` rejects drift from the deterministic KVM/GTK
    launch shape: GTK windowed mode, `zoom-to-fit=off`, menubar/tabs hidden,
    `virtio-gpu-gl-pci,xres=1280,yres=800`, guest `video=1280x800`, and
    `virtio-tablet-pci` absolute input.
- [x] Reduce blink/flicker in the compositor path.
  - Avoid full-screen damage for pointer motion and ordinary button edges.
  - Add counters/logging for full-damage causes and present mode.
  - 2026-04-29 update: pointer motion remains rect-damaged, ordinary present
    behavior is damage-rect driven, and `wlcomp` now has quiet-by-default
    instrumentation controlled by `XV6_WLCOMP_STATS_MS`.  When enabled it logs
    frame count, presented rects/pixels, full-screen frames, union collapses,
    acquire-fence blocked frames, full-damage reasons, and present mode
    (`bo-present` versus `user-blit`).
- [x] Prove input remains responsive while GPU work is active.
  - Run the 3D demo, move pointer continuously, open/close Terminal, and verify
    the compositor still processes input if a GPU client stalls or exits.
  - 2026-04-30 update: `/dev/mouse` accepts a test-only `struct mouse_event`
    write and `/bin/mouseinject` injects absolute cursor events.  The GPU
    validator now injects a bottom-right absolute pointer event while
    `mesawlegl` and `mesaglsmoke` are swapping concurrently, requires the
    virtio-tablet device to initialize, and still requires both GPU clients and
    object-accounting checks to complete.

### 7G. Native GPU Validation Before Toolkits

- [x] Keep `/bin/mesaglsmoke --demo` as the primary visible 3D validation app.
  - 2026-04-29: KVM/GTK `virtio-gpu-gl` validation logged
    `renderer=virgl buffer=xv6-gpu-bo spherical-poly-demo`; the QEMU monitor
    screenshot showed the faceted spherical polygon in a compositor-framed
    window with an `x` close button.
- [x] Add a no-regression test command for native 3D.
  - Boot `QEMU_GPU=virtio-gpu-gl glsmoke=1 glsmoke_demo=1 glsmoke_accel=1
    video=1280x800`.
  - Capture a QEMU monitor screenshot and fail the run if the scene is blank,
    offscreen, missing its close button, or logs `fatal page fault`, `panic`,
    `virtio_failures`, or `virtio_timeouts`.
  - `scripts/gpu-validate.sh` now has `GPU_VALIDATE_VISIBLE_3D=1`, which runs
    the default substrate lane, then launches a GTK/KVM `virtio-gpu-gl` VM with
    the spherical polygon demo, waits for the virgl renderer marker, captures a
    QEMU monitor `screendump`, runs `virgltest --bad-submit`, captures
    `fbstat`, and rejects crash/failure markers.
  - 2026-04-30 validation: `GPU_VALIDATE_VISIBLE_3D=1 scripts/gpu-validate.sh`
    passed, logged `renderer=virgl buffer=xv6-gpu-bo spherical-poly-demo`, and
    wrote `build-x86_64/gpu-validate.ppm` as a 1280x800 screenshot.
  - 2026-04-30 update: the visible lane now sends guest commands one at a time
    instead of a semicolon-packed shell line, because xv6 `sh` does not execute
    that packed command sequence reliably.
- [x] Keep `/bin/mesawlegl` as the EGL-window validation lane.
  - It should remain separate from the prettier demo so regressions in
    `wl_egl_window`, resize, swap, and teardown are visible.
  - `scripts/gpu-validate.sh` now runs
    `mesawlegl --frames=4 --loops=1 --resize-every=2` in its default substrate
    lane and requires `complete frames=4 status=0`.
  - 2026-04-29 validation: the script passed with `renderer=softpipe
    native-wayland api-smoke`, covering Mesa Wayland EGL window creation,
    resize, swap, teardown, and the standard compositor import path without
    invoking WebKit.
- [x] Add multi-client Mesa stress.
  - Run two Mesa clients swapping simultaneously.
  - Resize one while the other animates.
  - Kill one client mid-frame and prove object cleanup and compositor input
    continue.
  - `scripts/gpu-validate.sh` now runs `mesawlegl` and `mesaglsmoke`
    concurrently with resize churn, waits for both completion markers, then
    runs `gpubuftest 3` and post-quiesce `fbstat`.
  - 2026-04-30 validation: `scripts/gpu-validate.sh` passed with the
    multi-client lane and clean post-run object accounting.

### 7H. Toolkit And Browser Consumers Come After The Substrate

- [x] Use GTK/WebKit only as late-stage consumers of the GPU substrate.
  - Default browser stability remains important, but it is not the next GPU
    architecture step.
  - Forced WebGL/accelerated compositing should stay disabled unless explicitly
    testing the lower contracts from Stages 7A-7E.
  - 2026-04-30 update: Stage 7 validation now gates the kernel/libdrm/GBM,
    dmabuf, Mesa Wayland EGL, virgl visible-demo, and input/geometry contracts
    before any WebKit consumer lane.  `WEBKIT_TODO.md` keeps WebKit WebGL
    instability separate from this substrate queue.
- [ ] When revisiting WebKit, require it to use the same buffer-sharing and fence
  path as other clients.
  - No env-var-only acceleration claim.
  - No broad browser-specific workaround that bypasses the GPU object model.

### 7I. Automated Acceptance

- [x] Add a host-side GPU validation script.
  - Launch KVM/GTK with monitor socket and debugcon.
  - Wait for known GPU/compositor log markers.
  - Capture screenshot.
  - Grep for `panic`, `fatal page fault`, `SIGABRT`, coredump, virtio failures,
    rejected graphics operations, and stuck fence waits.
  - Added `scripts/gpu-validate.sh`.  Its default substrate lane launches a
    KVM `virtio-gpu` VM, waits for the shell prompt with `expect`, runs
    `gbmtest`, `dmabufsmoke`, `mesawlegl`, concurrent `mesawlegl` plus
    `mesaglsmoke`, and `gpubuftest 3`, captures post-quiesce `fbstat`, shuts
    down, and fails on crash markers, rejected blits, leaked BO/fence fd
    objects, or virtio failures/timeouts.  Optional `GPU_VALIDATE_VISIBLE_3D=1`
    runs the
    GTK/virgl demo lane and attempts a monitor `screendump`.
  - 2026-04-29 validation: `scripts/gpu-validate.sh` passed and wrote
    `build-x86_64/gpu-validate.log`.
  - 2026-04-30 validation: `scripts/gpu-validate.sh` passed after adding the
    multi-client Mesa stress lane.
  - 2026-04-30 validation: `GPU_VALIDATE_VISIBLE_3D=1
    GPU_VALIDATE_3D_TIMEOUT=180s scripts/gpu-validate.sh` passed with the
    forced virgl context-failure/recovery check and clean visible-lane
    accounting.
  - 2026-04-30 update: the substrate lane is marker-and-prompt synchronized,
    runs `/bin/gpubuftest --render-owner`, and the visible lane validates
    `FB_GPU_VIRGL_FENCE_EXPORT_FD`/`FB_GPU_VIRGL_FENCE_QUERY_FD` through
    `/bin/virgltest`.
  - 2026-04-30 validation: `GPU_VALIDATE_TIMEOUT=300s
    GPU_VALIDATE_VISIBLE_3D=1 GPU_VALIDATE_3D_TIMEOUT=240s
    scripts/gpu-validate.sh` passed after the render-owner, virgl fence-fd, and
    timeout context-failure updates.
  - 2026-04-30 validation: the same visible run passed after adding the
    deterministic GTK launch contract check and in-guest `mouseinject` input
    event while concurrent Mesa clients were active.
  - 2026-04-30 validation: `cmake --build build-x86_64 --target kernel-sparse
    -j2` passed with `failures=0, errors=0` and the known pre-existing sparse
    context-imbalance warnings.
- [x] Add guest-side graphics counter snapshots.
  - Capture `_fbstat`/`_gpustat` before and after native 3D, multi-client Mesa,
    compositor stress, and toolkit smoke.
  - Track live BO handles, imports, contexts/resources, fence counts, failures,
    and timeouts.
  - The substrate validator captures post-test `fbstat` and requires
    `bo_handles 0`, `bo_live_bytes 0`, `bo_fd_live 0`, `fence_fd_live 0`,
    `rejected_blits 0`, `virtio_failures 0`, and `virtio_timeouts 0`.
- [x] Final acceptance pass.
  - Rebuild `kernel`, `port-wayland`, `rootfs`, and `kernel-sparse`.
  - Run Mesa software EGL loops.
  - Run Mesa virgl EGL/OpenGL loops.
  - Run native 3D screenshot validation.
  - Run multi-client GPU stress.
  - Only then run toolkit/browser validation as consumers.
  - 2026-04-30 final substrate pass: `cmake --build build-x86_64 --target
    kernel user rootfs -j2` passed, then `GPU_VALIDATE_TIMEOUT=300s
    GPU_VALIDATE_VISIBLE_3D=1 GPU_VALIDATE_3D_TIMEOUT=240s
    GPU_VALIDATE_SCREENSHOT_DELAY=6 scripts/gpu-validate.sh` passed.  The run
    covered GBM/libdrm BO export/import, linux-dmabuf, Mesa Wayland EGL
    resize/swap, concurrent Mesa clients, in-guest mouse injection while GPU
    clients were active, `gpubuftest` BO/fence/render-owner accounting, the
    virgl spherical polygon screenshot lane, bad-submit context isolation, and
    clean post-run GPU counters.
  - 2026-04-30 sparse pass: `cmake --build build-x86_64 --target
    kernel-sparse -j2` passed with `failures=0, errors=0`; remaining warnings
    are the pre-existing context-imbalance warnings tracked by the sparse skill
    notes.
  - Commit submodules deepest first, then top-level pointers and skill docs.

## Validation Ladder

- Rebuild `kernel`, `port-wayland`, `rootfs` or `image`, and
  `webkit-runtime-check`.
- Boot KVM with `QEMU_GPU=virtio-gpu-gl` and a deterministic 1280x800 guest
  mode.
- Confirm idle desktop does not constantly full-blit.
- Move the cursor and verify reduced blinking.
- Open internal windows, drag/resize them, and verify redraw correctness.
- Run multiple native Mesa clients through repeated launch, resize, swap, close,
  and forced-exit cycles before testing browsers or other toolkits.
- For every OpenGL milestone, run repeated create/draw/resize/close loops and
  compare process, buffer, framebuffer, and memory counters before and after.
- For the API lane, run the same tests with acceleration disabled.
- For the accelerated lane, run the same tests under QEMU `virtio-gpu-gl` and
  confirm the renderer/capset indicates virgl rather than software fallback.
- For the visual demo lane, capture a QEMU monitor screenshot and inspect the
  actual pixels before marking the object/display/cursor fix complete.
- For toolkit/browser consumers, require them to use the same buffer-sharing and
  fence path as native Mesa clients, with clean close/reopen, no coredump, and
  stable post-run graphics counters before calling the consumer gate complete.
