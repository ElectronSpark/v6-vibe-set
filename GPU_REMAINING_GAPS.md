# GPU Remaining Gaps

Last updated: 2026-05-17

This file is now scoped to work that is still missing. Completed baseline
capabilities were moved into the skill docs, mainly
`.github/skills/xv6-debug-gui-runtime/SKILL.md` and
`.github/skills/xv6-os-debugging/SKILL.md`.

## Source Audit

Checked against the current dirty tree on May 17, 2026:

- `kernel/kernel/dev/hyperv_input.c`
- `kernel/kernel/dev/fb.c`
- `kernel/kernel/inc/uabi/d3dkmthk.h`
- `user/programs/dxgprobe/dxgprobe.c`
- `ports/wayland/src/desktop.c`
- `ports/wayland/src/desktop_clients.inc`
- `ports/wayland/src/mesawlegl.c`
- `ports/wayland/src/mesademo.c`
- `ports/wayland/src/gl_fps_overlay.c`
- `ports/wayland/src/wlcomp_dmabuf.inc`
- `ports/wayland/src/wlcomp_buffer_shm.inc`
- `ports/mesa/src/src/gallium/frontends/dri/drisw.c`
- `ports/mesa/src/src/gallium/drivers/d3d12/d3d12_resource.cpp`
- `scripts/hyperv-dxg-validate.sh`
- `scripts/hyperv-gpu-stress.sh`
- `scripts/hyperv-webkit-gpu-validate.sh`
- `scripts/hyperv-3d-visual-check.sh`

The current code already has substantial Hyper-V DXG and general GPU substrate
coverage: DXG transport, D3DKMT readiness reporting, stable `dxgprobe`
coverage, existing-sysmem pin tracking, late allocation unwind, NT resource and
sync-object fd export/open, render-node/GBM/PRIME-style BO fd support, linear
ARGB/XRGB/NV12 linux-dmabuf import, explicit-sync release objects, compositor
GPU BO present/direct-scanout paths, display completion accounting, virgl
OpenGL-submit on KVM, WebKit fallback gating, and 3D-demo visual/FPS overlay
smokes. Those are no longer listed as remaining gaps here.

## Current Honest State

- Hyper-V can expose `/dev/dri/renderD128`, DXG transport, and D3DKMT readiness.
- Hyper-V must still report `FB_GPU_BACKEND_F_OPENGL_SUBMIT == 0`.
  `fb.c` currently sets `FB_GPU_BACKEND_F_OPENGL_SUBMIT` only for virgl.
  Keep that invariant until the non-readback D3D12 present path and 480p
  >60 FPS demo are validated.
- Mesa D3D12 on Hyper-V reaches real D3D12 UMD work and real hardware-queue
  submit in observed runs, but the desktop present path still goes through
  Mesa's DRI software/readback lane and remains below the 480p >60 FPS target.
- WebKit acceleration and `webkit_dmabuf=1` are correctly gated on
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT`; this is a safety gate, not completion of
  the accelerated WebKit surface contract.

## Remaining Checklist

### DXG Kernel Semantics

- [ ] Replace growable per-open trackers with a WSL-style `dxgprocess` object
  graph and typed handle table.
  - Current source state: `hvdxg_open_state` now has growable per-open arrays
    for devices, contexts, HW queues, paging queues, sync objects, allocations,
    resources, and GPUVA reservations. That removed several fixed diagnostic
    table limits.
  - Still missing: WSL-like `hmgrtable` semantics, typed object krefs, global
    versus process-local handle namespaces, fork/exec rules, lock ordering, and
    exact teardown/unwind ordering for arbitrary close/error paths.

- [ ] Match WSL resource/shared-resource sealing and metadata lifetime exactly.
  - Current source state: resource and sync NT fd export/open paths exist,
    clone tracked metadata, and hold the VFS file reference while query/open
    consumes the metadata.
  - Still missing: a sealed WSL-style resource object model for runtime private
    data, resource private data, per-allocation private data, allocation sizes,
    cache flags, shared-owner lifetime, late destruction, and multiple-open
    aliasing beyond the current tracked-resource clone.

- [ ] Verify every WDDM private payload and return layout against the real UMD
  sequence.
  - Current source state: the UAPI surface includes the relevant context,
    allocation, GPUVA, residency, sync, shared-object, HW-queue, submit,
    sync-file, and process-enumeration ioctls. The kernel forwards large
    `CREATECONTEXTVIRTUAL`, `CREATEHWQUEUE`, allocation private data, and HW
    queue submit payloads far enough for real D3D12 work in observed runs.
  - Still missing: same-adapter automated comparison for every private payload,
    host return layout, residency packet, fence value, cacheability side effect,
    and cleanup ordering. Kernel-side diagnostics must prove what the host saw
    after any xv6 packet rewrite; user-space `LD_PRELOAD` traces only show the
    pre-kernel ioctl arguments.

- [ ] Resolve the real Mesa/NVIDIA second-FBO residency failure.
  - Last observed blocker: the real D3D12 path can pass the first 32x32 FBO
    readback, then fails on the 64x32 FBO path when a multi-allocation
    `LX_DXMAKERESIDENT` batch (`count=2`, `flags=0x1`) receives host
    `STATUS_INVALID_PARAMETER` / `-EINVAL`, causing D3D12 device removal.
  - Still missing: kernel-side packet diagnostics for the forwarded allocation
    list/order/device field/status/fence, plus a focused pure-C or Mesa probe
    that reproduces and then proves the corrected residency packet.

### Hyper-V OpenGL Present Path

- [ ] Stop presenting Hyper-V D3D12 through the DRI software-present/readback
  lane.
  - Current source state: `mesademo` selects `GALLIUM_DRIVER=d3d12` on Hyper-V,
    and `mesawlegl` uses a native Wayland EGL surface, but Mesa still falls
    through `drisw.c` for the Wayland presenter.
  - Still missing: a native D3D12 Wayland swap path that hands compositor-visible
    GPU resources across process boundaries without CPU readback.

- [ ] Add a D3D12 shared-resource Wayland buffer path.
  - Current source state: `wlcomp_dmabuf.inc` imports standard linux-dmabuf fds
    through `FB_GPU_BO_IMPORT_FD`, which assumes xv6 framebuffer BO semantics
    and mappable linear planes.
  - Still missing: a DXG-aware import path for D3D12 NT shared-resource fds,
    including resource metadata, allocation private data, owning/opening device
    context, and release lifetime.

- [ ] Extend Wayland protocol/glue for D3D12 resource type and fences.
  - Current source state: the private `xv6_gpu_buffer_manager` carries xv6 GPU
    BO handles and an acquire fence fd; linux-dmabuf carries linear plane fds,
    format, modifier, offsets, and strides.
  - Still missing: a protocol contract that says "this fd is a D3D12 NT shared
    resource plus sync/fence object" and carries enough metadata for the
    compositor to open it through `/dev/dxg`.

- [ ] Implement GPU-side present/composite for D3D12 resources on Hyper-V.
  - Current source state: `FB_GPU_BO_PRESENT`, direct scanout, and compositor
    GPU compose speed up framebuffer BO presentation. They do not make a D3D12
    render target a native compositor input.
  - Still missing: compositor-side D3D12 copy/present from the shared resource,
    or an equivalent WSLg-style host compositor path, with no repeated CPU
    texture readback/copy.

### Validation

- [ ] Add pure-C D3D12 shared-resource export/import/open/present validation.
  - Current source state: `dxgprobe` validates resource and sync NT fd
    export/query/open across parent/child opens, but that proves only the
    metadata/share half.
  - Still missing: one process exporting a D3D12-renderable resource, another
    process or compositor opening it through `/dev/dxg`, synchronizing it with
    a fence/sync object, and presenting it without mapping/copying pixels
    through CPU memory.

- [ ] Make the pure-C WSL-trace replay validator pass as an equivalence test.
  - Current source state: `dxgprobe --wsl-trace-replay` exists and replays an
    ordered device, paging queue, allocation, residency, GPUVA, lock, context,
    HW queue, submit, and teardown sequence.
  - Still missing: WSL-equivalent success criteria for the real UMD packet
    shapes. The validator should assert matching return statuses, fence
    progress, object ownership, and cleanup for the same adapter instead of
    remaining an opt-in repro for known host rejections.

- [ ] Add a finite GUI performance validator that fails below 60 FPS after
  warmup.
  - Current source state: the 3D demo defaults to a 640x480 Wayland/EGL window,
    is closeable/resizable, draws an RTC-based FPS overlay inside the GL
    surface, logs FPS, and has screenshot visual checks.
  - Still missing: an automated Hyper-V validator that launches the desktop
    demo, confirms a visible nonblack in-window overlay, skips warmup, samples a
    fixed duration/frame window, and fails unless sustained visible FPS exceeds
    60.

### WebKit Consumer Contract

- [ ] Implement the durable WebKitGTK accelerated-surface contract.
  - Current source state: WebKit acceleration and `webkit_dmabuf=1` are gated
    on `FB_GPU_BACKEND_F_OPENGL_SUBMIT`, so Hyper-V falls back safely while
    virgl can opt in.
  - Still missing: WebKit using the same render-node, D3D12/dmabuf/shared
    resource, and fence path as native Mesa clients on Hyper-V. An environment
    variable or render-node existence alone is not accepted evidence.

## Acceptance Gate

Hyper-V GPU/OpenGL support is complete only when all of the following are true:

- `mesaglfeature` passes on Hyper-V D3D12 without tracing or device removal.
- A Mesa Wayland client presents a D3D12-rendered surface through a shared GPU
  resource/fence path, not through DRI software readback.
- The desktop-launched 640x480 or equivalent 480p 3D demo is visible, closeable,
  resizable, and sustains more than 60 FPS after warmup.
- `fbstat` can honestly report `backend_opengl_submit 1` on Hyper-V.
- WebKit acceleration uses that same validated OpenGL-submit/shared-surface
  contract and stays gated off when the contract is unavailable.
