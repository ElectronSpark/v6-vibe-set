# GPU Plan: virtio-gpu 3D (virgl) under KVM acceleration

Last updated: 2026-06-07

> **Status:** V4 desktop-wide virgl acceleration is accepted by default-run
> evidence. The GL/dma-buf zero-copy present path is stable for the 3D demo,
> desktop chrome, generic Wayland EGL clients, and WebKit's smoke page. Tier P
> is fully closed: P2 (scanout-rebind elimination) caches the full-screen
> page-flip resource set in the kernel so steady flips skip `SET_SCANOUT`
> (`scanout_rebinds=0`, `SET_SCANOUT=6` total per run), and P3/P4 are done.
> A fresh canonical glsmoke trace (2026-06-06) sustains `displayed_fps`
> tracking `app_loop_fps` at **80–96 FPS** on `path_cpu_only=0` with
> `frame_avg_us≈2.4ms` — well above the old 66–73 target band. The pointer runs
> on the virtio-gpu hardware cursor queue (Section V4.HC). Tier 0/1/2, the
> readback fallback, and no-virgl fail-closed are complete. The Hyper-V
> GPU-P/DXG ladder is done (Appendix A, reference only).
>
> **Open work:** optional Tier 3 windowed unredirection and V6 backend-flag
> honesty guardrails. The WebKit video-load host `SUBMIT_3D` stall on WSL d3d12
> virgl is narrowed and mitigated by defaulting WebKit's virgl winsys submits to
> synchronous mode; an opt-out remains for host-stall diagnostics. Reconcile
> the interactive `scripts/launch/launch-gui.sh` path with the trace harness:
> the interactive launcher currently presents in `mode=bo-present`
> (`displayed_fps=0.0` metric artifact, `path_cpu_only` high) instead of the
> GL-scanout page-flip path the canonical trace flags engage. The fix that
> mattered for the GL-path stall was in the compositor `.inc` files, not the
> kernel.
>
> **2026-06-07 — two reported regressions closed/diagnosed.** (1) The *idle
> 2-3s desktop-update* bug is **fixed** (Section V4.IDLE): when no toplevel is
> mapped the bare desktop now promotes its 1 Hz clock present to a full-surface
> flush, defeating the WSL d3d12 host's coalescing of sparse partial rects.
> (2) A **real-network** WebKit YouTube run (`QEMU_NET=1` SLIRP, live
> `youtube.com/watch`) proved the *YouTube "slowness"* is **not** the
> compositor/present path and **not** a virgl host stall — the YouTube SPA
> loads, renders real titles, and even autoplay-advances while the compositor
> stays on the accelerated GL page-flip path at ~42-71 FPS with zero `0x207`
> timeouts and zero faults. The gap is the **media-decode pipeline** (MSE/DASH
> → GStreamer produced no decoded frames). Full evidence in Section V4.D3.

## Mission

The active goal is **hardware-accelerated 3D from the xv6 guest through
virtio-gpu virgl on a KVM/QEMU host** — real OpenGL/GLES rendering executed by
the host's GL stack (virglrenderer) on behalf of the guest, surfaced to guest
userspace (Mesa's `virgl` Gallium driver) and ultimately to the Wayland desktop
and WebKit.

What "genuinely accelerated" means here, stated so success cannot be faked:

1. **Real host GL execution, not software rasterization in the guest.** Draw and
   compute work is encoded into virgl command buffers, submitted over the
   virtio-gpu control queue, and executed by host virglrenderer against a real
   host GL/EGL context. Evidence must be a value or pixel that could only come
   from host GL executing the work (a `glReadPixels` result from a host-rendered
   FBO, a host-signalled virtio-gpu fence), never a counter set in guest
   software.
2. **Invoke the virtio-gpu 3D / virgl transport.** The adapter reaches the guest
   as a `virtio-gpu` PCI device with the `VIRTIO_GPU_F_VIRGL` feature and a
   virgl capset; the in-tree driver (`kernel/kernel/virtio_gpu.c`) drives the
   3D control commands (`CREATE_CONTEXT`, `RESOURCE_CREATE_3D`,
   `TRANSFER_*_HOST_3D`, `SUBMIT_3D`) with real host fences.
3. **Fail closed when virgl is absent.** If the host did not negotiate
   `VIRTIO_GPU_F_VIRGL` or expose a virgl capset (e.g. QEMU without
   `virtio-gpu-gl`, or a host with no GL), every 3D path must reject and the
   backend must fall back to the dumb-buffer / software render node — never
   advertise `OPENGL_SUBMIT` or fabricate a fence.

> The previously-active **Hyper-V GPU-P / DXG (D3D12)** ladder is **complete and
> hardware-proven** (real RTX 4060 compute + offscreen GL in-guest). It is
> preserved as **Appendix A** for reference. The Nouveau / DDA route remains out
> of scope (see end). This plan now centers the **KVM virgl** path, which is the
> portable acceleration route for non-Hyper-V hosts.

## Status summary (virtio-gpu virgl on KVM)

| Capability | State | Notes |
|---|---|---|
| 2D scanout / display (virtio-gpu) | **Working** | `RESOURCE_CREATE_2D` + `SET_SCANOUT` + `TRANSFER_TO_HOST_2D` + flush back the framebuffer; desktop displays |
| virgl capset detection + 3D context | **Kernel code present** | `virtio_gpu_query_capsets`, `CREATE_CONTEXT`, `virtio_gpu_has_virgl()` gate |
| 3D resource / transfer / submit + fence | **Kernel code present** | `RESOURCE_CREATE_3D`, `TRANSFER_*_HOST_3D`, `SUBMIT_3D`, sync + async fences; `/dev/gpu0` `FB_GPU_VIRGL_*` ioctls |
| Kernel virgl ioctl self-test | **Working under KVM/virgl** | `user/programs/virgltest` exercises submit / fence / negative paths |
| Mesa `virgl` GL consumer in-guest | **Working under KVM/virgl** | `mesakmsgl` proves GBM/EGL/GLES on the `virgl` driver, with renderer `virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))` |
| Backend flag `OPENGL_SUBMIT` | **Set when `virtio_gpu_has_virgl()`** | Honest for the KVM/virgl backend after direct KMS visible-render proof; does not grant native wlcomp desktop-present credit |
| WebKit / Skia GL via virgl | **Working (smoke + video-load mitigation + forced-loss tolerance)** | Renders the accelerated smoke page on `path_cpu_only=0`; video playback reaches `event:ended`; failed-context recovery survives forced virgl context loss; WebKit uses synchronous virgl submits by default to avoid the WSL d3d12 host `SUBMIT_3D` wedge (see V4.D3) |
| Host requirement | **Host GL / `/dev/dri` needed** | QEMU `virtio-gpu-gl`; without host GL, virgl falls back to software (`check-gui-accel.sh`) |

**KMS scanout note.** Full-screen `DRM_IOCTL_MODE_SETCRTC` / `MODE_PAGE_FLIP`
framebuffers attempt `FB_GPU_BO_PRESENT_F_VIRGL_SCANOUT` first (full-resource
`SET_SCANOUT` + `RESOURCE_FLUSH`, no CPU copy), falling back to readback/CPU on
failure. This is limited to exact scanout-sized framebuffers — partial/window
resources must not be bound directly (QEMU treats that as a scanout resize and
the host window jumps). Validated by `mesakmsgl` +
`scripts/gpu/virgl-kms-validate.sh` (KMS 1280x800, `DRM_CAP_PRIME` export,
virgl NVIDIA renderer, FPS near refresh).

**Honesty gate.** `FB_GPU_BACKEND_F_OPENGL_SUBMIT` is credit only for the
KVM/virgl render backend (justified by the direct KMS `mesakmsgl` lineage). It
must not be treated as wlcomp/native-present credit, and a host that silently
falls back to software GL must not flip the flag.

## Verified code state — virtio-gpu virgl (2026-05-30 source audit)

`kernel/kernel/virtio_gpu.c` (standalone TU, ~3.3K lines) is the KVM/QEMU GPU
driver. Independent of a fresh runtime capture, the source establishes:

- **Device + queues: implemented.** virtio-pci discovery, control + cursor
  virtqueues, IRQ completion with a polled fallback, and fence-id tracking
  (`virtio_gpu_intr`, `virtio_gpu_complete_pending_locked`, async submit ring).
- **2D scanout: implemented.** `RESOURCE_CREATE_2D`, `SET_SCANOUT`,
  `TRANSFER_TO_HOST_2D`, and resource flush back the framebuffer (this is what
  the desktop currently displays).
- **virgl 3D: implemented (control path).** `virtio_gpu_query_capsets` finds the
  virgl capset (`virtio_gpu: virgl capset ready id=.. version=.. size=..`);
  `CREATE_CONTEXT`, `RESOURCE_CREATE_3D`, `TRANSFER_TO/FROM_HOST_3D`, and
  `SUBMIT_3D` marshal real commands with optional `VIRTIO_GPU_FLAG_FENCE` and
  sync **or** async fence completion. `virtio_gpu_has_virgl()` returns true only
  when initialized **and** a virgl capset is present.
- **Userspace ABI: present.** `/dev/gpu0` exposes `FB_GPU_VIRGL_SUBMIT`, context
  create/destroy, fence wait, and resource create/destroy/transfer. The in-tree
  self-test `user/programs/virgltest` covers async submit + fence wait, sync
  submit, a forced-failure negative path, and failed-context rejection.
- **Backend advertise: implemented.** When `virtio_gpu_has_virgl()` is true,
  `fb_drm_core_kms.c` `gpu_backend_fill` reports `FB_GPU_BACKEND_VIRGL` and sets
  `FB_GPU_BACKEND_F_VIRGL_OPENGL | FB_GPU_BACKEND_F_OPENGL_SUBMIT` (renderer
  string "OpenGL via virtio-gpu virgl").

### Known blockers — both resolved

- **B1 — virgl-ready boot race (RESOLVED).** The GPU backend capability is now
  evaluated after capset init (not latched early), so consumers no longer miss
  virgl or see `OPENGL_SUBMIT` flip on after a software decision. See V1.
- **B2 — WebKit/Skia GL-context crash (RESOLVED).** The old `SkiaGPUWorker`
  NULL deref at GL/EGL context creation is fixed; `webkit_accel=1` now imports
  WebKit client dma-bufs and renders the smoke page on `path_cpu_only=0`. The
  separate WebKit video-load host stall is mitigated in V4.D3.

---

## Active plan — virtio 3D (virgl) under KVM (do this top to bottom)

Each item lists **what to build/verify**, **which files**, and the **runtime
evidence** that lets you check the box. Build-success alone is never enough: you
need runtime evidence from a KVM host with virgl, plus a passing fail-closed
negative (a no-virgl image must fall back to the dumb buffer and never advertise
`OPENGL_SUBMIT`).

### Sections V0–V3 — prerequisites (DONE)

- [x] **V0 Host + launch.** Host GL/EGL + `/dev/dri` render node; QEMU launches
  `virtio-gpu-gl` and logs `virtio_gpu: virgl capset ready`. Plain `virtio-gpu`
  (no `-gl`) fails closed (`no virgl capset found`, dumb-buffer DRM only).
- [x] **V1 virgl-ready boot race (B1).** Backend capability evaluated after
  capset init; `fbstat` reports the `virgl` backend with `OPENGL_SUBMIT`
  consistently. Files: `kernel/kernel/dev/fb/fb_init_panic.c`,
  `fb_drm_core_kms.c` `gpu_backend_fill`.
- [x] **V2 Kernel virgl ioctl self-test.** `virgltest` passes sync + async
  submit/fence and the two negative paths; a no-virgl image fails closed at the
  open/capset gate.
- [x] **V3 Mesa virgl GL consumer.** `gldemo`/`mesaglfeature` render offscreen
  GLES via `GALLIUM_DRIVER=virgl` with host GL renderer string and two-size
  readback verification.

### Section V4. Desktop-wide GPU present (COMPLETE; optional Tier 3 parked)

Goal achieved: every surface the user sees is **GPU-presented through the virgl
GL/dma-buf compose path** — the 3D demo, the desktop chrome, and any Wayland
client — at a sustained **60+ FPS** (host band 66-73 FPS), with no host-window
resize, no blinking, correct Y orientation. Tier 0/1/2, Tier P, Tier D, V4.HC,
readback fallback, and no-virgl fail-closed are validated. Tier 3 below is an
optional future unredirection optimization, not part of V4 global acceptance.

**Why this is reachable:** the host is not the limit — Alpine on the same
WSL-d3d12 / RTX 4060 / single virtio-gpu control queue runs `glxgears` at 141
FPS and `weston-simple-egl` at 66-73 FPS (`build-x86_64/alpine-trace/...`). The
gap was the xv6 guest compositor present architecture, now built: the
double/triple-buffered page-flip swap is default-on for `-gl` (resource ids
cycle 4/5/6, queued releases wait for GL/display readiness), the GL/dma-buf
zero-copy compose is stable, and the mid-session `path_cpu_only` stall is fixed
(compositor `.inc` edits, not the kernel).

#### Completed prerequisites (do not redo)

- [x] **V4.1 Host-GL render visible on screen (fallback present).** Mesa Wayland
  demo renders via `virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`; sphere
  visible. Uses readback/CPU-present fallback (no native present credit).
- [x] **V4.2 Full-screen KMS virgl resource scanout.** Exact scanout-sized KMS
  framebuffers try `FB_GPU_BO_PRESENT_F_VIRGL_SCANOUT` first
  (`SET_SCANOUT`+`RESOURCE_FLUSH`), CPU/readback fallback on failure. Validated
  by `mesakmsgl` + `scripts/gpu/virgl-kms-validate.sh`. Windowed surfaces are
  *not* accelerated by this.
- [x] **V4.x Async steady-state flush.** `virtio_gpu_resource_flush_async()`
  posts the steady-state scanout flush and returns immediately
  (`virtio_gpu_async_scanout_flush`, default-on for `-gl`); present
  `frame_avg` dropped to ~14-16ms.
- [x] **V4.T0.1 Scanout coalesced to one host submit/frame.** The render loop
  filters CPU damage so GPU-only frames carry one rect, and
  `present_virgl_scanout_rects()` unions multiple rects into one `RESOURCE_FLUSH`
  unless `wlcomp_virgl_multi_flush`. Trace `scanout_rects/frame` may read 2 but
  the kernel emits a single flush. (Verified `/tmp/xv6-gui-pipe.log`.)
- [x] **V4.T0.2 Chrome CPU upload skipped on GPU-only frames.** `cpu_rects/frame
  == 0` on animating frames in the newer build; chrome still repaints on clock
  tick / window move via the filtered damage list.
- [x] **V4.T0.3 Async scanout flush taken on windowed frames.**
  `virtio_gpu_async_scanout_flush` default-on; no async-command-timeout panic.

#### Tier 0 — submit-count reduction (DONE)

`cpu_rects/frame=0` on GPU-only frames, scanout coalesced to one host
`RESOURCE_FLUSH`, async flush default-on. The Tier 0 FPS gate was not reached on
its own (the structural limiter was the single shared scanout buffer); Tier 1
fixed that. Do not redo Tier 0 work.

#### Tier 1 — double-buffered scanout + page-flip (DONE). ~28 → ~55-60 FPS.

The page-flip front/back swap is implemented and default-on for `-gl`: compose
routes into the inactive target, present flips it, and resource ids alternate
every frame so `displayed_fps` tracks `app_loop_fps`. Files:
`ports/wayland/src/wlcomp_fb.inc`, the `-gl` launch defaults in
`scripts/launch/run-qemu.sh` / `scripts/launch/launch-gui.sh`.

- [x] **V4.T1.1** Allocate two full-screen scanout BOs (front/back).
- [x] **V4.T1.2** Compose into the back buffer; drop the redundant copy + flush.
- [x] **V4.T1.3** Kernel page-flip alternates resources every frame
  (`SET_SCANOUT` the newly-composed resource + async flush; fails closed when
  virgl absent).
- [x] **V4.T1.4** Buffer-swap default-on for `-gl`
  (`wlcomp_page_flip_present=1 wlcomp_virgl_fb_damage_flip=1`), explicit opt-out
  preserved, plain non-GL path untouched. Fail-closed rechecked (dumb backend,
  no `OPENGL_SUBMIT`, no page-flip logs).
- **Gate:** met on the default `-gl` run — `displayed_fps` tracks `app_loop_fps`
  (the ~2x gap closed), pixels validated, window stays 1280x800.

#### Tier 2 — compositor pipelining (overlap render and present). Steady 60 FPS.

Files: `ports/wayland/src/wlcomp_fb.inc`, `wlcomp_render_loop.inc`,
`wlcomp_gl_compose.inc`, `wlcomp_buffer_shm.inc`, and the `-gl` defaults in
`scripts/launch/run-qemu.sh`. Acceptance requires real demo pixels inside the
window in a full 1280x800 screenshot — not counters or the FPS text alone.

- [x] **V4.T2.1** Triple-buffer (three scanout resources). `-gl` defaults
  `wlcomp_virgl_fb_buffers=3`; `render/back/extra` rotate so the next compose
  target is never the just-presented resource; all three EGL images cached.
- [x] **V4.T2.2** Reap/page-flip asynchronously; never block the event loop.
  `-gl` defaults `wlcomp_gl_submit_fence=1` (cheap GL fence/flush, not
  `glFinish()`); buffer release is queued and reaped only after both the present
  fence and the compositor GL sync are ready (no fixed frame-number delay).
  Compositor GL pixels are visible without `wlcomp_gl_flush=1`.
- [x] **V4.T2.3** Keep early frame-callback dispatch (`wlcomp_pipeline_callbacks`
  default 1); `app_loop_fps` and `displayed_fps` both ~60-73 within ~10%.
- [x] **V4.T2.4** Stabilize the GL/dma-buf compose path — eliminate the
  intermittent mid-session fallback to `path_cpu_only`. Fixed by the
  `wlcomp_fb.inc` / `wlcomp_xdg.inc` / `wlcomp_compositor_subsurface.inc` edits;
  ~593-596/600 demo frames stay on the GL path. The only `path_cpu_only` windows
  are post-demo desktop-idle chrome repaints (the V4.D1 gap). The prior kernel
  `virtio_gpu.c` change did NOT fix this; the compositor edits did.
- [x] **V4.T2.5** Raise the stable GL path into the 66-73 band consistently.
  Done for the single windowed GL client: with the visible-safe ordering
  (`wlcomp_frame_ms=1 wlcomp_callback_poll_ms=1 wlcomp_gl_submit_fence=1`),
  repeated no-frame-limit default runs sustain ≥60 FPS after warmup on the GL
  path, full-frame pixels validated, window 1280x800.
- **Gate:** met — default `-gl` keeps the client on the dma-buf/GL compose path
  for the whole demo and sustains ≥60 FPS; remaining latency cleanup is Tier P.

#### Tier P — latency & pipelining (squeeze every frame out of the GPU)

**Profiled bottleneck (corrected attribution).** Present work is cheap
(`frame_avg_us≈6-8ms`) and neither the present fence nor the loop is the
bottleneck: real `client_wait_avg_us` and `present_wait_avg_us` are tens of µs,
`loop_wait_avg_us≈1.4-2.4ms`. The big residual is `outside_frame_avg_us≈5.7-8.2
ms/frame` — the client rendering its next frame, which is genuine client work,
not a removable compositor stall. P2 (per-frame scanout rebind) is now closed;
the remaining compositor-side lever is P3 (compose/scanout overlap). P4 is
idle-desktop only.

| # | Bottleneck | Where |
|---|---|---|
| P2 | **DONE.** Full-screen page-flip resources are registered once in the kernel scanout set; steady flips skip `SET_SCANOUT` and only flush the selected resource. | `wlcomp_fb.inc:1211`/`:709`, `kernel/kernel/virtio_gpu.c` page-flip handler |
| P3 | Compose (~2.4-3.7ms) and scanout (~2.5-3ms) run back-to-back instead of `max(compose, scanout)`. | `wlcomp_render_loop.inc:1624-1660`, `wlcomp_gl_compose.inc:1377-1400` |
| P4 | Fixed 16ms `epoll_wait` idle fallback (small — only when no callback pending). Revisit for idle-desktop responsiveness, not demo FPS. | `wlcomp.c:524-537` |

- [x] **V4.P0** Attribute the per-frame idle. The present-trace now prints
  `loop_wait_avg_us` + `client_wait_avg_us`, which sum with the present phases
  to ≈ the real frame period. Files: `wlcomp_render_loop.inc`, `wlcomp.c`.
- [x] **V4.P1** Client pipeline bottleneck — CONCLUDED, no removable stall. The
  corrected trace shows the present/display fence is not a wait point (real
  `client_wait`/`present_wait` ~tens of µs); the ~5-8ms residual is
  `outside_frame` (client render), not a compositor stall. Latch-release and
  commit-time-callback experiments either crashed Mesa or made the app outrun
  the single present lane (`displayed_fps` collapsed). Default stays
  `wlcomp_pipeline_gpu_release=0`; callbacks go at latch, buffer release stays
  fence-ordered. Files: `wlcomp_render_loop.inc:315/:1230/:1696`,
  `wlcomp_buffer_shm.inc:288-357`.

- [x] **V4.P2 Kill the per-frame scanout rebind.** Pre-register the 2-3 virgl
  scanout resources with the kernel once, then make `FB_GPU_PAGE_FLIP` select
  the target by index/handle WITHOUT re-issuing `SET_SCANOUT` every frame —
  the kernel should only `SET_SCANOUT` when the resource set actually changes,
  and otherwise do a flip + `RESOURCE_FLUSH`. Confirm in the QEMU virtio-gpu
  trace that `SET_SCANOUT` count drops from per-frame to ~once.
  - Files: `kernel/kernel/virtio_gpu.c` (page-flip handler / scanout bind
    cache), `ports/wayland/src/wlcomp_fb.inc:1203-1290` (flip submit).
  - **Passing criteria:** `scanout_rebinds` per second drops from ≈ FPS to
    ~0 after warmup; `scanout_avg_us` falls; QEMU trace shows `SET_SCANOUT`
    issued ~once not per-frame; pixels still PASS; no host-window resize.
  - **Note:** the current virtio page-flip UAPI maps to a resource-id
    `SET_SCANOUT` whenever the front resource changes; there is no spec-level
    flip-index command in the existing guest path. Do not satisfy P2 by merely
    suppressing `SET_SCANOUT` (risks stale/transparent scanout); reducing the
    resource set is also not acceptable (`wlcomp_virgl_fb_buffers=2` and
    `virtio_gpu_async_depth=32` both failed no-frame-limit validation).
  - **Validation 2026-06-05:** `build-x86_64/virgl-p2-validate-final`
    PASS. Kernel page-flip logs register resources 5/6/4 once (`flags=0x3`),
    then steady flips return cached-only (`flags=0x2`); present trace after
    warmup shows `scanout_rebinds=0`, `scanout_submits=533`,
    `frame_avg_us=2982`, `gl_compose_avg_us=622`, `scanout_avg_us=1584`,
    and `displayed_fps=106.3` with `app_loop_fps=98.8`. Screenshot matrix
    PASS at 1280x800. QEMU trace matrix: `scanouts=4`, `flushes=998`,
    `flush_unique=3,4,5,6`, `p2_cached=1`, status PASS; raw trace counts
    `virtio_gpu_cmd_set_scanout=6`, `virtio_gpu_cmd_res_flush=999`,
    `virtio_gpu_cmd_ctx_submit=1990`, `virtio_gpu_fence_ctrl=1990`,
    `virtio_gpu_fence_resp=1990`.

- [x] **V4.P3 Overlap compose(N+1) with scanout(N).** Issue the scanout/flush
  of the just-composed target and let the next frame's `gl_compose` begin
  without CPU-blocking on the previous scanout, using the submit fence for
  ordering. Goal: frame cost trends from `compose+scanout` (6.1 ms) toward
  `max(compose, scanout)` (~3.7 ms).
  - Files: `ports/wayland/src/wlcomp_render_loop.inc:1624-1660` (phase
    ordering), `ports/wayland/src/wlcomp_gl_compose.inc:1377-1400` (fence).
  - **Passing criteria:** `frame_avg_us` drops measurably below
    `gl_compose_avg_us + scanout_avg_us`; FPS up; pixels PASS; no fence
    deadlock or dropped frame.
  - **Rejected probes 2026-06-05:** `wlcomp_gl_release_fence=0` regressed
    the desktop run (`scanout_avg_us≈12-13ms`, displayed FPS ~50) and failed
    screenshot capture, so it is not a P3 path. A kernel experiment that put
    async `RESOURCE_FLUSH` command storage inside the async ring slot instead
    of page-allocated command buffers caused immediate virtio-gpu timeouts
    (`SET_SCANOUT`, `RESOURCE_FLUSH`, then virgl context commands); reverted.
    The restored allocation-backed async flush path re-passed
    `scripts/gpu/gpu-validate.sh`.
  - **Opt-in probe 2026-06-05 (not accepted):** `wlcomp_async_scanout_submit=1`
    adds a compositor worker that issues `FB_GPU_PAGE_FLIP` off the main loop
    and avoids reusing an in-flight scanout target. A 60-second run
    (`build-x86_64/virgl-p3-async-overlap-seconds-60`) passed pixels +
    screenshot and kept `SET_SCANOUT=6`, with warm `scanout_avg_us≈150-195`
    and `scanout_rebinds=0`. However the stall moved into compositor GL submit
    on the shared virtio-gpu control path (`gl_compose_avg_us≈5.2-6.5ms`) and
    displayed FPS stayed ~52-55, below the Tier P gate. The worker remains
    opt-in for diagnosis; default stays off until this improves FPS rather than
    just relocating the wait.
  - **Follow-up probe 2026-06-05 (rejected):** the same opt-in worker with
    `VIRGL_DESKTOP_VALIDATE_SECONDS=40`, `wlcomp_async_scanout_submit=1`, and
    `virtio_gpu_async_depth=8` reached high warm telemetry
    (`displayed_fps≈73-77+`, `scanout_rebinds=0`, QEMU `SET_SCANOUT=6`) but
    failed the required screenshot/pixel gate: `fbstat ppm-current` showed the
    Mesa window chrome with a blank blue client area
    (`missing_demo_pixels`). A diagnostic kernel experiment that republished
    the cached page-flip resource as `g->scanout_resource` made even the
    non-async control read back the blank target, so it was reverted. Final
    default control after revert (`build-x86_64/virgl-default-after-p3-revert-seconds-20`)
    PASSed pixels/screenshot with `SET_SCANOUT=6`; keep the worker default-off.
  - **Seconds-based probe 2026-06-06 (default visible PASS, timing still
    open):** the
    validation harness now takes duration in seconds, not frame count
    (`VIRGL_DESKTOP_VALIDATE_SECONDS=N` -> guest `glsmoke_seconds=N` ->
    `mesawlegl --seconds=N`). With the early opt-in bundle
    `VIRGL_DESKTOP_VALIDATE_SECONDS=40`,
    `VIRGL_DESKTOP_VALIDATE_BUFFERS=3`,
    `VIRGL_DESKTOP_VALIDATE_DAMAGE_FLIP=1`,
    `VIRGL_DESKTOP_VALIDATE_PAGE_FLIP_PRESENT=1`, and
    `VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='virtio_gpu_scanout_perf=1 wlcomp_async_scanout_submit=1 virtio_gpu_async_depth=8 wlcomp_gl_target_warmup_read=1'`,
    `build-x86_64/virgl-p3-async-warmup-read-seconds-40c` PASSed pixels,
    screenshot, trace shape, and completion (`frames=2771 seconds=40
    elapsed=40.012 status=0`; QEMU `SET_SCANOUT=6`;
    `page_flip_trace_matrix ... p2_cached=1 status=PASS`; warm
    `displayed_fps≈69.7-76.2`). The first draw into each imported scanout
    target does a 1x1 diagnostic warmup read when async scanout submit is
    enabled.
  - **Default ordering fix 2026-06-06:** `wlcomp_async_scanout_submit` now
    defaults on only through the existing virgl page-flip/triple-buffer path,
    and compositor GL draws are explicitly `glFlush()`ed after queuing the GL
    release fence when the async scanout worker is active. This removes the
    prior "fast trace but blank client" race where the worker could flush a
    target before Mesa had submitted the compositor draw. Default seconds runs
    now PASS visible gates without extra P3 append flags:
    `build-x86_64/virgl-p3-default-async-flush-seconds-40` (`frames=3092
    seconds=40 elapsed=40.008 status=0`, screenshot PASS, QEMU
    `SET_SCANOUT=6 RESOURCE_FLUSH=3101 CTX_SUBMIT=6187`) and
    `build-x86_64/virgl-p3-default-async-accounted-seconds-60` (`frames=4499
    seconds=60 elapsed=60.010 status=0`, screenshot PASS, page-flip trace
    PASS, QEMU `SET_SCANOUT=6 RESOURCE_FLUSH=4506 CTX_SUBMIT=8999`).
    The present trace now includes the async worker's completed page-flip time
    in `scanout_avg_us` and reports `async_scanout_avg_us` /
    `async_scanout_completions`, so the P3 timing comparison is honest.
    This run did not close P3: in the 60s default run, warm mean `frame_avg_us` was
    ~3465us while mean `gl_compose_avg_us + scanout_avg_us` was ~3143us
    (4/11 warm trace windows were below the sum). Pixels, screenshot, trace
    shape, readback fallback, and `scripts/gpu/gpu-validate.sh` all PASS, but
    the strict timing predicate is not consistently met yet.
  - **Rejected chrome-default probe 2026-06-06:** making
    `wlcomp_gpu_chrome_compose` source-default on did reduce CPU upload
    accounting, but it made every animated frame compose both the full chrome
    source and the GL client (`gl_bufs/frame=2`), pushed
    `gl_compose_avg_us≈11-12ms`, dropped FPS to ~24-27, and failed the
    screenshot/pixel gate with a blank blue client (`missing_demo_pixels`).
    Reverted; do not use chrome source-default as the P3 fix.
  - **Content-damage + rejected mirror/chrome probes 2026-06-06:** promoted
    GPU buffer swaps were narrowed to damage only the client content rectangle
    for normal commits and deferred acquire-ready promotion; first-map,
    resize, non-GPU, D3D12, and staged-promotion paths keep the existing full
    damage behavior. This preserves pixels and keeps the default seconds run
    visible (`build-x86_64/virgl-p3-content-damage-accepted-seconds-60`:
    `frames=4349 seconds=60 elapsed=60.001 status=0`, screenshot PASS,
    page-flip trace PASS, QEMU `SET_SCANOUT=6 RESOURCE_FLUSH=4355`), but the
    strict P3 timing predicate still did not hold on the repeat
    (`warm frame_avg_us≈3536` vs
    `gl_compose_avg_us + scanout_avg_us≈3262`, 4/10 warm windows below the
    sum). Two follow-up attempts were rejected and reverted: a damage-only
    chrome-compose default (`build-x86_64/virgl-p3-chrome-damage-only-seconds-40`)
    drove `gl_bufs/frame` to ~5, FPS to ~10, and failed screenshot pixels
    (`missing_demo_pixels`); lazy inactive-target CPU damage mirroring
    (`build-x86_64/virgl-p3-lazy-mirror-seconds-40`) replayed chrome damage
    every frame, pushed CPU upload to ~13-16ms/frame, and also failed the
    pixel gate. Keep validation duration as seconds
    (`VIRGL_DESKTOP_VALIDATE_SECONDS`), not frame-count parameters.
  - **Accepted P3 fix 2026-06-06:** the remaining warm-frame CPU upload was not
    the tiny taskbar clock alone; present trace rectangle accounting showed
    repeated `FULL_DAMAGE_ACQUIRE` full-screen repairs that were then filtered
    into a large bottom band (`cpu_max_rect=0,622-1280,800`). The final fix
    keeps the async page-flip overlap path default-on, narrows GPU buffer
    promotions to client-content damage, makes chrome-only clock damage
    piggyback on the visible GPU compose batch, and prevents virgl async
    page-flip / regular virgl GPU acquire waits from scheduling broad
    `FULL_DAMAGE_ACQUIRE` repairs. The accepted 60-second run
    (`build-x86_64/virgl-p3-accepted-seconds-60`) passed pixels, screenshot,
    completion, and QEMU trace shape: `frames=5157 seconds=60 elapsed=60.010
    status=0`, screenshot PASS at 1280x800, `page_flip_trace_matrix ... p2_cached=1
    status=PASS`, raw trace `SET_SCANOUT=6 RESOURCE_FLUSH=5164
    CTX_SUBMIT=10317`, and no crash/timeout markers. Warm present trace
    satisfied the strict P3 predicate in every window: mean
    `frame_avg_us≈2388` vs `gl_compose_avg_us + scanout_avg_us≈3355`
    (10/10 warm windows below the sum), `path_cpu_only=0`, `FULL_DAMAGE_ACQUIRE=0`,
    and only the expected five 2912-pixel clock rects per trace window remained.
    Rechecks on the accepted image: readback fallback
    `build-x86_64/virgl-p3-accepted-readback-seconds-20` PASS; default
    `scripts/gpu/gpu-validate.sh` PASS after accepting the seconds-aware
    `mesawlegl` completion format; plain no-`-gl` `virtio-gpu` fail-closed
    (`build-x86_64/virgl-p3-accepted-nogl-failclosed-debugcon.log`) reported
    `no 3D capsets advertised`, dumb backend, `backend_opengl_submit 0`,
    `backend_opengl_submit_gate closed`, `backend_virgl_opengl 0`, and captured
    `fbstat ppm-current` with no panic/timeout markers.

- [x] **V4.P4 Tighten the main-loop idle. — DONE (2026-06-06).** Replace the
  fixed 16 ms `epoll_wait` fallback with a deadline derived from the next
  expected frame callback so a late client buffer never costs a full 16 ms
  slot; keep a sane idle cap when truly nothing is animating (don't busy-spin).
  - Files: `ports/wayland/src/wlcomp.c` main loop and
    `ports/wayland/src/wlcomp_surface_state.inc` frame-callback deadline
    helpers.
  - Implementation: deadline wait is default-on, but still fail-closed via
    `wlcomp_callback_deadline_wait=0` / `XV6_WLCOMP_CALLBACK_DEADLINE_WAIT=0`.
    Pending callbacks use the next callback deadline; recently active surfaces
    keep a short deadline window after callback delivery; truly idle/no mapped
    toplevel state falls back to the existing 16 ms cap. The default due path
    uses a 1 ms floor instead of a zero-timeout poll.
  - Evidence: baseline deadline-off 20-second run
    `build-x86_64/virgl-p4-baseline-deadline-off-seconds-20`
    (`wlcomp_callback_deadline_wait=0`) completed with `frames=1479`,
    `seconds=20`, `elapsed=20.004`, `status=0`; final default run
    `build-x86_64/virgl-p4-default-deadline-final-seconds-20` completed
    with `frames=1437`, `seconds=20`, `elapsed=20.008`, `status=0`,
    screenshot PASS at 1280x800,
    `page_flip_trace_matrix ... p2_cached=1 status=PASS`, and final FPS
    samples `app_loop_fps=73.3 displayed_fps=76.3` then
    `app_loop_fps=76.9 displayed_fps=76.1`.
  - P4 trace delta: deadline-off steady windows had
    `loop_wait_avg_us=1476/1430/1524` with `request_avg_ms=4..6` and occasional
    `last_request_ms=16`; final default windows had
    `loop_wait_avg_us=1348/1196/1100` with `request_avg_ms=1`,
    `last_request_ms=1`, no zero-ms busy-poll shape, and no crash/bad markers.
    QEMU trace shape stayed P2-cached: `SET_SCANOUT=6`,
    `RESOURCE_FLUSH=1444`, `CTX_SUBMIT=2877`, `fence_ctrl=2877`,
    `fence_resp=2877`.
  - Fail-closed unchanged: plain `QEMU_GPU=virtio-gpu` no-`-gl` negative
    artifacts `build-x86_64/virgl-p4-failclosed-no-gl-seconds-20` and
    `build-x86_64/virgl-p4-failclosed-no-gl-screendump` show
    `GPU: virgl unavailable; exposing dumb-buffer DRM only`,
    `wlcomp: linux-dmabuf disabled (no virgl)`, `backend dumb`,
    `backend_opengl_submit 0`, `backend_virgl_opengl 0`, no panic/timeout, and
    a 1280x800 screendump with `nonblack=1024000 bright=47760 status=PASS`.

  **Tier P gate:** met for P3 and P4 on accepted default `-gl` seconds runs.
  The accepted runs sustain `displayed_fps` above 60 with `app_loop_fps`
  tracking, entirely on `path_cpu_only=0` after warmup, pixels + screenshot
  PASS, window 1280x800, cached scanout set (`SET_SCANOUT=6`), and fail-closed
  + readback lanes re-verified.

#### Tier D — desktop-wide GPU acceleration (the universal-present goal)

This tier extends the proven single-client GL/dma-buf path to the *entire*
desktop and *every* capable client. Do these after V4.T2.5 (or in parallel
where independent); each must keep the fail-closed behavior intact.

- [x] **V4.D1 GPU-compose the desktop chrome (panel, background, decorations).**
  Route the compositor's own chrome/background surfaces through the same
  `gl_compose` / dma-buf scanout path so idle and animating chrome frames report
  `path_cpu_only=0` with `gl_bufs/frame>=1`. Landed behind
  `wlcomp_gpu_chrome_compose` (default-on for virgl GL compose). Validated: a
  no-3D idle run plus an internal window drag stay entirely on the GL path
  (`cpu_rects_total=0`, `path_cpu_only=0`), screenshot 1280x800 with intact
  desktop/taskbar. Files: `ports/wayland/src/wlcomp_gl_compose.inc`,
  `wlcomp_fb.inc`, `wlcomp_render_loop.inc`.

- [x] **V4.D2 Make zero-copy dma-buf import generic for ANY Wayland client.**
  The dma-buf import engages for every client that exports a buffer, not just
  `mesawlegl`. Validated: two simultaneous Mesa Wayland EGL clients each import
  from `client-fd` and compose together (`gl_bufs/frame=3 path_cpu_only=0`),
  visible side-by-side. A mixed GL + wl_shm frame is handled by prepending a
  full chrome/base source only when GL and CPU/shm damage coexist (fixes a
  black-frame bug), keeping the frame on the GL path; the shm window still
  composites. Files: `ports/wayland/src/wlcomp_buffer_shm.inc`,
  `wlcomp_gl_compose.inc`, the `linux-dmabuf` protocol glue.

- [x] **V4.D3 Bring WebKit/Skia GL onto the accelerated path (was B2).** The
  `SkiaGPUWorker` GL-context crash is fixed: MiniBrowser with `webkit_accel=1`
  imports WebKit client dma-bufs (`source=client-fd`, ARGB8888) and stays on
  `path_cpu_only=0` with no SIGSEGV. The compositor geometry gate allows
  GL-composed buffers whose xdg geometry is smaller than the buffer, and the
  virgl WebKit launch no longer forces WebKit's compositing mode. Software
  fallback (`webkit_accel=0`) and plain virtio-gpu fail-closed both rechecked.
  Files: `ports/wayland/src/desktop.c`, the Mesa virgl EGL port, the B1 gate.
  - **Update — video-load stall + per-context isolation fix:** a heavy WebKit case
    (`webkit_url=file:///share/webkit/video-direct.html`) exposed a *separate*
    failure: ~19s into playback a host-side `SUBMIT_3D` (0x207) stalls ≥5s
    (host virglrenderer/d3d12 batch under heavy video-decode GL load; the poll
    fallback reads the used-ring directly, so it is a genuine host stall, not a
    missed IRQ). The kernel's timeout handler then marked **all** in-use virgl
    contexts failed (`virtio_gpu_mark_all_contexts_failed_locked`), poisoning the
    compositor as well as WebKit, which cascaded into `virgl framebuffer upload
    failed errno=22` and a WebKit `cr2=0x0` NULL-deref (SIGSEGV) that tore down
    the whole desktop. Fix: `kernel/kernel/virtio_gpu.c` now fails **only the
    offending context** at all three timeout sites (async-abort + the two sync
    submit paths) via `virtio_gpu_lookup_context_locked(ctx_id)` +
    `virtio_gpu_mark_context_failed_locked`; the over-broad
    `mark_all_contexts_failed_locked` helper was removed. Re-validated on the
    same video case: the stall still occurs (`async command 0x207 timed out
    (ctx=5)`) but now **0** EINVAL cascade, **0** WebKit fatal faults, the
    compositor keeps presenting, and the video keeps playing through the stall
    (timeupdate 20.6 → 27.1s).
  - **Update — forced failed-context tolerance validated 2026-06-06:** WebKit's
    helper processes are wrapped by a host-glibc launcher in
    `ports/wayland/src/webkit_preload_wrapper.c`, installed by
    `ports/wayland/src/install_webkit_preload_wrappers.sh` as both the helper
    executable and `.real` path. The wrapper links `libxv6memshim.so`, forces
    the Skia NULL-member SIGSEGV recovery hook, and keeps a watchdog reinstalling
    the handler because WebKit resets signal state after the shim constructor.
    `ports/wayland/src/xv6memshim.c` now exports
    `xv6_webkit_skia_signal_recovery_force_install()`.
    Validation artifact
    `build-x86_64/webkit-video-forced-loss-seconds-fixedcapture-062641`:
    `webkit_virgl_force_context_loss_after_seconds=2`, video timeupdates
    progressed 4.784 → 29.862 seconds, forced context 4/5 losses were logged,
    the shim recovered two NULL-member lookups, guest `fbstat ppm-current`
    captured a 1280x800 screenshot mid-playback, `webkit_screenshot_matrix`
    PASSed (`nonblack=1023894 bright=416977 colorful=567108
    unique_sample=1023`), and the run saw the video `event:ended` with no
    panic, fatal fault, `SkiaGPUWorker`, WebProcess crash, or nonzero client-exit
    marker. QEMU trace shape remained bounded: `SET_SCANOUT=6`,
    `RESOURCE_FLUSH=205`, `CTX_SUBMIT=23`, `fence_ctrl=23`, `fence_resp=23`.
    This validated the failure-tolerance lane; the default accelerated
    video-load stall mitigation is recorded below.
  - **Update — host stall narrowed 2026-06-06:** current-image diagnostic run
    `build-x86_64/webkit-video-timeout-diag-default-065116` reproduced the
    default accelerated video stall once, but the per-context failure path kept
    the desktop and WebKit alive: video timeupdates progressed 5.155, 10.195,
    13.769, then after the timeout jumped to 25.266 and reached `event:ended`
    at 30.031 seconds with `__WEBKIT_API_SMOKE_DONE_0__`. The new timeout
    diagnostic in `kernel/kernel/virtio_gpu.c` reported:
    `type=0x207 ctx=5 owner_tgid=58 owner_id=16 fence=1154 desc=8
    cmd_len=32 data_len=536 ndwords=134 age_us=6876714
    head=0001001c,00000004,00010803,0000039d`. Decoding `0x0001001c` as the
    virgl command header gives `cmd=28 len=1`, matching
    `VIRGL_CCMD_CREATE_SUB_CTX` in Mesa's `virgl_protocol.h`. That points away
    from a large decoded-frame upload and toward a small WebKit/Mesa virgl
    context or sub-context management batch wedging virglrenderer/d3d12 under
    video load. QEMU trace shape for the accelerated run stayed bounded:
    `SET_SCANOUT=6`, `RESOURCE_FLUSH=712`, `CTX_SUBMIT=2092`,
    `fence_ctrl=2092`, `fence_resp=2092`.
  - **Update — WebKit video-load stall mitigated by default 2026-06-06:** a
    richer timeout decoder in `kernel/kernel/virtio_gpu.c` showed the wedged
    accelerated WebKit batch is small, not a decoded-frame bulk upload:
    `type=0x207 ctx=5 owner_tgid=56 owner_id=16 fence=2243 desc=8
    cmd_len=32 data_len=428 ndwords=107 age_us=6554764`, followed by
    virgl stream entries `CREATE_SUB_CTX`, `DESTROY_OBJECT` /
    `CREATE_OBJECT` for object 8 (`VIRGL_OBJECT_SURFACE`),
    `SET_VERTEX_BUFFERS`, `TEXTURE_BARRIER`, `DRAW_VBO`,
    `SET_CONSTANT_BUFFER`, `CLEAR`, and `RESOURCE_INLINE_WRITE`. That points to
    a WSL d3d12 virglrenderer stall on a small WebKit accelerated-compositing
    draw/update batch under video playback. Mesa's xv6 virgl winsys now accepts
    `XV6_VIRGL_SYNC_SUBMIT=1`; `ports/wayland/src/desktop.c` defaults that env
    for accelerated MiniBrowser while preserving
    `webkit_virgl_sync_submit=0` as an opt-out A/B diagnostic knob. This keeps
    the compositor and other virgl clients on the async fast path.
    Validation after rebuilding `build-x86_64/fs.img`:
    `build-x86_64/webkit-video-default-sync-submit-seconds-80-071847`
    launched the 30.031-second video **without** passing
    `webkit_virgl_sync_submit=1`, logged
    `virgl-xv6: context 5 using synchronous submit`, captured
    `/webkit-video-default-sync-submit.ppm` at playback time 18.693 seconds
    (`fb_ppm_current ... screen=1280x800 scanout=1280x800`), reached
    `event:ended` and `__WEBKIT_API_SMOKE_DONE_0__`, and exited with
    `EXPECT_RESULT captured=1 ended=1 failed=0 saw_sync=1`. QEMU trace shape
    stayed bounded and P2-cached:
    `SET_SCANOUT=6`, `RESOURCE_FLUSH=647`, `CTX_SUBMIT=2206`,
    `fence_ctrl=2206`, `fence_resp=2206`; the serial log had no timeout,
    panic, fatal fault, WebProcess crash, SIGABRT, or nonzero client-exit
    marker. The mid-playback screenshot shows the desktop and MiniBrowser at
    1280x800 with a white WebKit content area while the title telemetry proves
    video progression; do not use the visual content area alone as video-frame
    proof.
  - **Software comparator (diagnostic, not a pass gate):**
    `build-x86_64/webkit-video-timeout-diag-software-seconds-80-065716`
    launched the same 30.031 second video with `webkit_accel=0`. It reached
    `event:ended` with no virtio timeout, no fatal fault, and WebKit logs showed
    `driCreateNewScreen3 fd=-1 type=2` with `softpipe`. The QEMU trace had only
    compositor traffic plus one context submit (`SET_SCANOUT=6`,
    `RESOURCE_FLUSH=719`, `CTX_SUBMIT=1`, `fence_ctrl=1`, `fence_resp=1`).
    The capture exited before `fbstat ppm-current` completed, so use this only
    as root-cause narrowing evidence, not as a full pixels/screenshot pass.
  - **Update — real-network live YouTube diagnosis 2026-06-07 (network now
    on by default):** `scripts/launch/run-qemu.sh` defaults `QEMU_NET=1`
    (user-mode SLIRP, hostfwd-capable; `QEMU_NET_BACKEND=tap` is the
    SLIRP-bypass alternative). A live run with
    `webkit_url=https://www.youtube.com/watch?v=jNQXAC9IVRw` (and **no**
    `QEMU_NET=0`) was used to settle whether YouTube slowness lives in the
    network fetch, the video decode, or the compositor present path. Results,
    all from `/tmp/xv6-debugcon.log` (guest serial) and
    `/tmp/xv6-audit-run.log` (launcher stdout):
    - **Network is healthy.** `DHCP discovery... / DHCP lease acquired`,
      `DNS from DHCP: 10.0.2.3`, `bound=1`; the WebKit launch correctly blocked
      on `waiting for network before WebKit URL` and then loaded the live URL.
    - **The live YouTube SPA loads, renders, and runs JS against real data.**
      The WebKit probe title progressed `[Private] YouTube` →
      `[Private] Me at the zoo - YouTube` (the requested video `jNQXAC9IVRw`)
      → autoplay-advanced to `[Private] The Creepiest "Kids" Movies - YouTube`.
      Real titles + autoplay-advance prove the network fetch + Kevlar SPA boot +
      DOM/layout pipeline all work against live youtube.com.
    - **The compositor present path is healthy and accelerated during
      playback.** After a brief page-load warmup (3 stat windows of
      `path_cpu_only=9/7`, `frame_avg_us≈125000–212000`, `scanout_rebinds`
      settling 3→0), steady state holds the GL fast path for the rest of the
      run: `path_gl_preflush=84–142` per 2 s window (≈42–71 FPS present),
      `path_cpu_only=0`, `scanout_rebinds=0` throughout, `gl_compose_avg_us≈
      8300–8660` (WebKit composite is heavier than the GL demo's ≈1200 but
      bounded), `frame_avg_us≈10000–17000`. A few windows dip to ≈13–21 FPS
      (`frame_avg_us` 53099/74381) on JS/network bursts and recover. **Zero**
      `0x207` SUBMIT_3D timeouts, **zero** panics, **zero** WebProcess
      crashes/SIGSEGV this run.
    - **The video itself never decoded a frame.** `/tmp/gst-debug.log` was
      **empty** (0 bytes); the serial log had only `waiting` media events with
      **no** `timeupdate`/`playing`/`canplay` and **no** MSE/codec chatter
      (`isTypeSupported`, `MediaSource`, `decodebin`, `vp9`/`av01`/`opus`) at
      `GST_DEBUG=1`. The `<video>` element produced no decoded output; the
      `[Private]` title prefix plus the autoplay-advance indicate the player
      engaged autoplay but could not sustain real frames.
    - **Conclusion (data-backed):** the perceived YouTube slowness is **not**
      the compositor present path (proven accelerated, 42–71 FPS, no stall, no
      crash) and **not** a virgl host `SUBMIT_3D` stall (zero `0x207` this
      run). The remaining gap is the **media-decode pipeline**: YouTube serves
      adaptive media over MSE/DASH (VP9/AV1 video + Opus audio) into GStreamer,
      and this build decoded none of it. The most likely sub-causes, in order:
      (a) **codec coverage** — the WebKit/GStreamer build only has software
      AVC up to 720P (`WEBKIT_GST_MAX_AVC1_RESOLUTION=720P`, no hardware
      decode), so `MediaSource.isTypeSupported` for the VP9/AV1 streams YouTube
      offers likely returns false and the player can never select a decodable
      representation; (b) **DASH segment fetch throughput** over single-threaded
      SLIRP (prior measured TCP ceiling ≈50 Mbit/s, `tcpip_thread` CPU-bound)
      keeps the media buffer starved even when a codec is selectable. The page
      chrome composites smoothly while the video surface stays empty/buffering.
    - **Next step for video (out of GPU-plan scope, recorded for the media
      lane):** instrument `MediaSource.isTypeSupported`/`canPlayType` for the
      exact YouTube MIME+codec strings, raise `GST_DEBUG` on the
      decode categories, and confirm which GStreamer demux/decode elements are
      registered. This is a media/codec gap, not a virtio-gpu present gap; the
      accelerated present path needs no further change for it.

#### Tier 3 — windowed unredirection (optional; single full-window client).

- [ ] **V4.T3.1 Kernel partial-scanout RECTANGLE/overlay, no mode resize.**
  - Files: `kernel/kernel/virtio_gpu.c`
    (`virtio_gpu_allow_partial_scanout` guard), `fb_scanout.c`.
  - **Passing criteria:** binding a sub-fullscreen client rect updates only that
    region while the QEMU window **stays 1280x800** (no resize/jump — that jump
    is the forbidden "too large" regression); fails closed if the host cannot do
    a non-resizing partial scanout.
- [ ] **V4.T3.2 Direct-scanout a single opaque covering window; composite
  fallback otherwise.**
  - Files: `ports/wayland/src/wlcomp_render_loop.inc`,
    `ports/wayland/src/wlcomp_surface_state.inc`.
  - **Passing criteria:** one maximized opaque client reaches glxgears-class
    FPS; occluded / translucent / moving / sub-threshold windows fall back to
    the Tier 1/2 composited path with correct pixels; no "too large" regression.

#### V4 global acceptance (the definition of done for this section)

All seven met (validated by trace + full-frame pixels + screenshot, never
counters alone):

1. [x] **FPS** — `displayed_fps` tracks `app_loop_fps`, sustained ≥60 FPS
   post-warmup on repeated default `-gl` no-frame-limit runs, all on
   `path_cpu_only=0`.
2. [x] **Desktop-wide** — with no 3D client, desktop chrome presents on
   `path_cpu_only=0` (D1); two simultaneous GL clients import via the generic
   `client-fd` dma-buf path (D2); mixed GL + wl_shm frames stay on the GL path
   with a chrome/base source; WebKit renders the smoke page accelerated (D3).
3. [x] **No host-window resize** — window stays 1280x800; QEMU trace rejects
   post-desktop non-1280x800 `SET_SCANOUT`; page-flip cycles resources 4/5/6.
4. [x] **Correct output** — no panel/desktop blinking, Y-flip applied once, demo
   and chrome both visible and updating (screenshot matrix PASS).
5. [x] **Pixels validated** with `fbstat ppm-current` / `fbstat sample` and
   visual inspection — never FPS text alone.
6. [x] **Fail-closed** — plain `virtio-gpu` (no `-gl`) falls back to the dumb
   buffer, never sets `OPENGL_SUBMIT`, never panics (`no 3D capsets advertised`,
   `linux-dmabuf disabled (no virgl)`).
7. [x] **Readback fallback** — the forced CPU/readback lane still works
   (`path_cpu_only`, `scanout_submits=0`, low/mid-50 FPS), no regression.

#### Honesty gates (carry through every step)

- **Paths are workspace-relative.** The workspace root is the `xv6-os` folder
  (this file is at its top level). Reference all source/build files relative to
  that root, e.g. `ports/wayland/src/wlcomp.c`, `kernel/kernel/virtio_gpu.c`,
  `build-x86_64/fs.img` — not absolute `/home/es/xv6-os/...` paths. The only
  absolute paths that belong here are transient runtime artifacts outside the
  repo (`/tmp/xv6-debugcon.log`) and the memory notes, which are read through
  the memory tool at its scope paths `/memories/repo/...` and
  `/memories/session/...` (a memory-tool scope, not files in the workspace).
- Never claim success from counters or the on-screen FPS number alone — verify
  destination pixels.
- Never resize the display mode for a sub-fullscreen client.
- Apply the Y-flip exactly once in the GPU composition path.
- Keep changes scoped; do not revert unrelated dirty GPU files.
- Do **not** commit (nested submodules) unless explicitly told to.

#### Validation harness (exact commands)

Build (compositor + rootfs):

```sh
cmake --build build-x86_64/ports --target port-wayland -j"$(nproc)" \
  && scripts/image/make-rootfs.sh build-x86_64/sysroot build-x86_64/fs.img 2304
```

Run with present trace (serial -> log; runs in background). Run all commands
from the workspace root (the `xv6-os` folder):

```sh
rm -f /tmp/xv6-gui-pipe.log && \
DISPLAY_MODE=gtk QEMU_GPU=virtio-vga-gl-primary QEMU_INPUT=virtio QEMU_NET=0 \
QEMU_REQUIRE_KVM=0 QEMU_APPEND='root=/dev/disk0 netsurf=0 webkit=0 glsmoke=1 \
glsmoke_demo=1 video=1280x800 wlcomp_gpu_compose=1 \
wlcomp_stats_ms=2000 wlcomp_trace_present=1 virtio_gpu_async_scanout_flush=1' \
bash scripts/launch/run-qemu.sh x86_64 \
  build-x86_64/kernel/build/kernel/xv6.bin build-x86_64/fs.img \
  >/tmp/xv6-gui-pipe.log 2>&1 &
```

Read FPS + present breakdown:

```sh
grep -aE 'app_loop_fps|present-trace' /tmp/xv6-debugcon.log | tail -12
```

Duration-bounded automated desktop runs should use seconds, not a frame-count
parameter:

```sh
VIRGL_DESKTOP_VALIDATE_SECONDS=60 bash scripts/gpu/virgl-desktop-validate.sh
```

This maps to guest `glsmoke_seconds=N` and launches the demo as
`mesawlegl --seconds=N`; omitting it keeps the normal no-limit/default run.
The accepted current-image recheck
`build-x86_64/virgl-seconds-current-20s-capturefix` completed
`mesawlegl --seconds=20` (`frames=1544 seconds=20 elapsed=20.007 status=0`),
PASSed screenshot pixels at 1280x800, and kept the P2 trace shape
(`SET_SCANOUT=6`, `RESOURCE_FLUSH=1549`, `CTX_SUBMIT=3090`,
`page_flip_trace_matrix ... p2_cached=1 status=PASS`). Do not add or rely on
frame-count input parameters for GPU validation clients.

Validate pixels (in guest) and fail-closed (host):

```sh
fbstat ppm-current /current.ppm 0 0 1280 800
# Fail-closed: relaunch with QEMU_GPU=virtio-gpu (no -gl) and confirm the log
# shows dumb-buffer fallback, no OPENGL_SUBMIT, no panic, and capture pixels.
```

### Section V4.HC. Hardware cursor — Alpine-style virtio-gpu cursor queue (DONE 2026-06-05)

Goal (the two reported symptoms, verbatim): *"Cursor movement is not responsive
enough and not smooth. Also, when running 3D demo, moving cursor disturbs the
demo too much."* Both are now fixed by moving the pointer onto the **virtio-gpu
hardware cursor plane** (the dedicated cursor virtqueue), exactly as Alpine /
upstream Linux do — instead of drawing the arrow as a topmost CPU layer.

#### Root cause

The software arrow was the top CPU compose layer. `wlcomp_render_loop.inc:1742`
gates the GPU chrome-compose path on `!cursor_damage_frame`, so **every
cursor-move frame skipped GPU compose** and fell back to a CPU
`TRANSFER_TO_HOST_2D` on the single virtio-gpu *control* queue — the same queue
the GL demo presents on. Result: laggy cursor (CPU-bound) and a demo that
stutters whenever the pointer moves (queue contention).

#### Design (decoupled cursor queue)

virtio-gpu exposes two virtqueues: `controlq` (index 0) and `cursorq`
(index 1). Cursor commands `UPDATE_CURSOR` (0x0300) / `MOVE_CURSOR` (0x0301)
ride the cursorq and are processed by the host independently of the scanout /
GL-present traffic on the controlq. Pointer motion therefore never touches the
control queue and never forces a CPU compose frame — the cursor is smooth and
the demo is undisturbed.

Fail-closed contract preserved: a plain virtio-gpu device (`num_queues < 2`, no
`-gl`) leaves `cursor_ready = 0`; the compositor's `g_hw_cursor` stays 0 and the
old software-arrow path runs unchanged.

#### What was implemented (all built + validated)

- **Kernel ABI** — [kernel/kernel/inc/dev/fb.h](kernel/kernel/inc/dev/fb.h):
  `FB_GPU_SET_CURSOR` (0x4638) / `FB_GPU_MOVE_CURSOR` (0x4639) ioctls,
  `struct fb_gpu_cursor_image` (≤64×64 BGRA + hotspot) and
  `struct fb_gpu_cursor_move`. Prototypes in
  [kernel/kernel/inc/defs.h](kernel/kernel/inc/defs.h).
- **Kernel driver** — [kernel/kernel/virtio_gpu.c](kernel/kernel/virtio_gpu.c):
  `virtio_gpu_cursor_queue_init` brings up queue 1; `virtio_gpu_cursor_post`
  is a fire-and-forget poster under the cursorq lock only (no `op_lock`, so it
  never serializes against control traffic); `virtio_gpu_user_set_cursor`
  lazily creates a 64×64 BGRA cursor resource + uploads the image;
  `virtio_gpu_user_move_cursor` posts `MOVE_CURSOR` (or `UPDATE_CURSOR` to
  hide/show). Cursor init is optional in `virtio_gpu_init` — failure ⇒ SW
  fallback.
- **ioctl dispatch** (two-level allowlist) — added the two cmds to
  [kernel/kernel/dev/fb/fb_drm_dispatch.c](kernel/kernel/dev/fb/fb_drm_dispatch.c),
  [kernel/kernel/dev/fb/fb_device_ioctl.c](kernel/kernel/dev/fb/fb_device_ioctl.c)
  (handlers validate dims/hotspot, copy-in pixels via `either_copyin`), and the
  name table in [kernel/kernel/dev/fb/fb_scanout.c](kernel/kernel/dev/fb/fb_scanout.c).
- **Compositor** — [ports/wayland/src/](ports/wayland/src): `init_hw_cursor()`
  builds the arrow and calls `FB_GPU_SET_CURSOR`; on success sets `g_hw_cursor`
  and routes `process_mouse()` cursor moves to `FB_GPU_MOVE_CURSOR` (no
  `damage_cursor_at`, no SW arrow). `draw_default_cursor()` early-returns when
  `g_hw_cursor` is set.

#### Validation (2026-06-05)

- **Motion-injected:** 1,129 absolute pointer moves at ~125 Hz during a live GL
  demo (QMP `input-send-event`). Log shows `cursor queue init size=16`,
  `wlcomp: hardware cursor enabled`, `cpu_rects/frame=0` on every GL window
  during motion (`cpu_rects_total=0` for 23/27 samples — proof motion no longer
  triggers CPU cursor uploads), FPS held 69–72, `path_cpu_only=0`, no cursor
  errors. (Bring-up bug fixed: poster now accepts QEMU's 16-entry cursorq —
  `qsize>=2`, caps to 64, wraps `% g->cursor_ring`.)
- **Client cursor:** `wl_pointer.set_cursor` uploads committed Wayland cursor
  surfaces via `FB_GPU_SET_CURSOR` (hotspot clamp, ARGB/XRGB ≤64×64). The
  `cursorsmoke` client set a 32×32 hotspot-7,7 cursor; screenshot
  `build-x86_64/hwcursor-cursorsmoke-live2.png` clean, `cpu_rects_total=0`.
- **Fail-closed:** plain no-`-gl` falls back to the dumb framebuffer
  (`virgl unavailable`, `linux-dmabuf disabled`, no `OPENGL_SUBMIT`, no panic);
  `num_queues < 2` leaves `cursor_ready=0` and the SW arrow runs.

#### Remaining cursor polish (optional, not blocking)

- [x] Wire `wl_pointer.set_cursor` client requests through to `FB_GPU_SET_CURSOR`.
- [ ] Cursor theming / richer per-app cursor shapes.
- [ ] Hide/show on focus changes via `FB_GPU_MOVE_CURSOR` with
  `FB_GPU_CURSOR_F_VISIBLE` cleared.

### Section V4.IDLE. Idle desktop 2-3s update bug (FIXED 2026-06-07)

Reported symptom (verbatim): *"when the 3D demo is not running, the whole
desktop only updates every 2-3 seconds"* — the entire window, including the
taskbar clock, visibly refreshes only every 2–3 s when the desktop is idle.

#### Root cause

The compositor main loop is **healthy** when idle: epoll blocks for exactly
the 16 ms frame-callback deadline (~50 Hz; confirmed per-iteration with
`wlcomp_frame_perf=1` — `frame loop wait samples=48-50 request_avg_ms=16
elapsed_avg_ms=16`). The P4 deadline-wait was **not** at fault. The real cause:
when the desktop is truly idle (no toplevel mapped), the only damage each second
is the 1 Hz taskbar clock, which presents as a tiny **partial CPU-rect scanout
flush**. The WSL d3d12 host **coalesces** those sparse partial flushes, so the
on-screen window only visibly refreshes every 2–3 s. The 3D demo never shows the
bug because it drives continuous **full** page-flips.

#### Fix (compositor, default-on, fail-closed)

- [ports/wayland/src/wlcomp_fb.inc](ports/wayland/src/wlcomp_fb.inc): added
  `wlcomp_idle_full_present_enabled()` (next to `damage_repair_interval_ms()`),
  gated by flag `wlcomp_idle_full_present` / env
  `XV6_WLCOMP_IDLE_FULL_PRESENT`, default **on** (accepts `0/no/false/off` to
  disable).
- [ports/wayland/src/wlcomp_render_loop.inc](ports/wayland/src/wlcomp_render_loop.inc)
  `composite_and_flip()`, immediately after the
  `if (!damage_has_any()) { damage_all_frame_callbacks(now); return; }` block:
  ```c
  if (wlcomp_idle_full_present_enabled() &&
      !any_frame_callbacks_pending() &&
      surface_existing_mapped_toplevels() == 0)
      damage_full_reason(FULL_DAMAGE_REPAIR);
  ```
  This promotes an idle present to a **full-surface** flush so the host cannot
  coalesce it away.

**Critical gate:** the `surface_existing_mapped_toplevels() == 0` condition is
load-bearing. A first version gated **only** on `!any_frame_callbacks_pending()`
misfired on WebKit — WebKit has no pending `wl` frame-callback at composite
time, so the promotion forced a full-surface CPU upload **every** frame
(`cpu_upload_avg_us≈57000`, `frame_avg_us≈68000–75000`, ~13 FPS). Restricting
the promotion to the bare desktop (no client window mapped) means any mapped
client — WebKit, a terminal, the GL demo — drives present from its own damage
and the idle promotion never fires.

#### Validation (2026-06-07, `fs.img` rebuilt via `port-wayland` + `make-rootfs`)

- **Idle (`webkit=0 glsmoke=0`):** `cpu_clock_rects=0 cpu_full_rects=5
  repair=5` — every idle present is now a full-surface flush. Bug gone.
- **WebKit `video-direct.html` (regression check):** `repair=0
  cpu_full_rects=0 cpu_clock_rects=5`, `cpu_upload_avg_us≈10947–12991` (was
  57218–60156), `frame_avg_us≈15607–18695` (was 67778–75075), `event:ended`
  ×9, 0 fatal faults — **unregressed**.
- **GL demo (canonical trace):** `repair=0 path_gl_preflush=425–440
  path_cpu_only=0 scanout_rebinds=0`, 73–93 FPS — **unregressed**.

### Section V5. WebKit / Skia GL via virgl (promoted to V4.D3)

- [x] **V5.1 Root-cause and fix the `SkiaGPUWorker` GL-context crash (blocker
  B2).** This is now tracked as **V4.D3** in the desktop-wide tier — WebKit is
  the headline "all user programs" consumer, no longer deferred behind the FPS
  work. Determine whether the NULL deref is the guest Mesa `virgl` EGL path, a
  missing host GL capability, or B1 handing WebKit a half-ready backend. Files:
  `ports/wayland/src/desktop.c` (WebKit GPU policy), the Mesa virgl EGL port,
  the backend flag from V1. Evidence: `webkit_accel=1` loads a page and renders
  a GPU-composited frame without `SkiaGPUWorker` SIGSEGV; the fail-closed
  software fallback still works when virgl is absent.
  Done 2026-06-05 via V4.D3: accelerated MiniBrowser renders
  `/share/webkit/gpu-smoke.html` on `path_cpu_only=0` with imported WebKit
  dma-bufs and no WebKit/Skia crash.

### Section V6. Backend flag honesty (gated on V4)

- [x] **V6.1 Keep `OPENGL_SUBMIT` honest.** Advertise it as native-present
  credit only when the V4 page-flip present lineage is proven. Until then it is
  render-backend credit only (see the honesty gate at the top of this file).
  Evidence: `fbstat` / `scripts/check-gui-accel.sh` report virgl only when host
  GL is real; otherwise the dumb-buffer render node.
  - **Validation 2026-06-05:** `scripts/gpu/gpu-validate.sh` PASS
    (`build-x86_64/gpu-validate.log`) after updating the harness for the
    now-default P2/T2 triple-scanout shape. Positive KVM/virgl evidence:
    `backend virgl flags 0x27`, `backend_opengl_submit 1`,
    `backend_opengl_submit_gate open`, `backend_virgl_opengl 1`, and
    `opengl_submit_backend_separation_matrix ... native_present_credit=0
    opengl_submit_credit=1 status=PASS`; no virtio failures or timeouts.
    Negative plain `virtio-gpu` run with `gpu_validate=1`: `no 3D capsets
    advertised`, `GPU: virgl unavailable; exposing dumb-buffer DRM only`,
    `backend dumb flags 0x3`, `backend_opengl_submit 0`,
    `backend_opengl_submit_gate closed`, `backend_virgl_opengl 0`, and
    `opengl_submit_credit=0`; virgl probes skipped and the validator exited 0.

### virtio virgl dependency graph

```
V0 host GL + virtio-gpu-gl launch   [done]
  -> V1 fix virgl-ready boot race (B1)        [done]
       -> V2 kernel virgl ioctl self-test      [done]
            -> V3 Mesa virgl GL consumer        [done]
                 -> V4 desktop-wide GPU present    [done]
                      T2.5 FPS -> D1 chrome -> D2 any-client -> D3 WebKit
                      -> V6 honest backend flag      [gated on V4]
```

---

## Appendix A: Hyper-V GPU-P / DXG ladder (COMPLETE — reference)

The DXG / D3D12 ladder below is **complete and hardware-proven** on a Hyper-V
GPU-P host (real RTX 4060: in-guest `d3d12probe` compute PASS and offscreen GLES
via Mesa's `d3d12` Gallium driver). It is retained for reference and for any
future Hyper-V GPU-P host; it is **not** the active KVM virgl plan above. The
same honesty gates apply (never report a synthetic fence/readback as success).

The full G0-G6 step log (host GPU-P prereqs, DXG VMBus channel + adapter
identity, process/device/context lifetime, allocations/residency/GPUVA, the
keystone real-submit + hardware monitored-fence proof, the present
investigation, and the honest `GPU_COMPUTE` capability bit) is preserved in git
history and in the prior revision of this file. Summary of the proven result,
kept here so it is not re-litigated:

- **G0-G4 (compute keystone): DONE, hardware-proven in-guest 2026-05-29.**
  `/bin/d3d12probe` inside the xv6 GPU-P guest printed
  `adapter[0] "NVIDIA GeForce RTX 4060 Laptop GPU" hw=1 vram=7957MiB` then
  `PASS GPU copied 16384 bytes, fence signalled` — real `SUBMITCOMMAND` +
  monitored-fence through the xv6 DXG forwarding path
  (`tmp/d3d12probe-inguest-pass.log`).
- **G5 (present): N/A on a plain GPU-P guest.** Native display/scanout is
  unreachable without WSLg's host compositor agent or a custom host tool (both
  out of scope). The display-bind code stays fail-closed; do not weaken it.
- **G6 (capability flag): DONE, compute-only.** A separate
  `FB_GPU_BACKEND_F_GPU_COMPUTE` bit is set only when the D3DKMT path is
  reachable at runtime; `FB_GPU_BACKEND_F_OPENGL_SUBMIT` (which promises a
  *presented* GL frame) stays `0` for the DXG backend on this host. Mesa's
  `d3d12` Gallium driver additionally runs GLES2/3 offscreen on the real RTX
  4060 (`gldemo`, `mesawlegl`, `mesaglfeature`), validated by readback.

This appendix is reference only. It is **not** the active plan. The active plan
is Section V4 above.

<details>
<summary>Archived G-section detail (collapsed; reference only)</summary>

### Section G0. Host GPU-P Prerequisites (Windows host, not guest code)

- [x] **G0.1 Confirm and pin the GPU-P partition. — DONE 2026-05-29.**
  - `Get-VMGpuPartitionAdapter -VMName xv6-os-hyperv` shows the `GPUPARAV`
    InstancePath for `VEN_10DE&DEV_28A0` (already true on this host).
  - Record `MinPartitionVRAM/MaxPartitionVRAM`, compute, and encode allotments.
  - Evidence (captured 2026-05-29): adapter record is
    `INSTANCE=\\?\PCI#VEN_10DE&DEV_28A0&SUBSYS_13B61462&REV_A1#4&2961dbb7&0&0008#{064092b3-625e-43bf-9eb5-dc845897dd59}\GPUPARAV`
    with `VM_STATE=Running`. (The VRAM/compute/encode allotment properties are
    not surfaced by name on this Windows build — the `GPUPARAV` partition path
    plus a Running VM is the partition confirmation.) The VM boots with the
    partition attached and the in-guest `dxgprobe`/`d3d12probe` runs below
    enumerate the partitioned RTX 4060.
- [x] **G0.2 Stage the host-driver user-mode components the guest will need.**
  - DONE 2026-05-29. GPU-P relies on host-driver UMD/KMD libraries surfaced
    into the guest (the WSLg `/usr/lib/wsl/lib` model: `libd3d12.so`,
    `libdxcore.so`, the NVIDIA UMD). `scripts/stage-gpup-umd.sh` copies the
    runtime from the host `/usr/lib/wsl/lib` into
    `rootfs-overlay/usr/lib/wsl/lib` (gitignored proprietary blobs), writes
    `/etc/ld.so.conf.d/gpup-wsl.conf` + `/etc/profile.d/gpup-d3d12.sh`, and
    stages `/bin/d3d12probe`.
  - Evidence: `stage-gpup-umd.sh` lists the 5 core libs staged; the userspace
    stack is host-validated (see critical-path step 2 — `d3d12probe` PASS on the
    RTX 4060). Remaining: list them from the *running xv6 guest* and confirm the
    same PASS there. If a future host lacks the runtime, the script prints
    `missing` and exits non-zero — fail closed, do not proceed to G3.

### Section G1. Bring up the DXG VMBus channel and adapter — KERNEL CODE PRESENT

> Audit 2026-05-29: implemented in the kernel and forwarded to the host. These
> items flip to `[x]` only once a GPU-P hardware boot logs the real negotiated
> version + real adapter LUID. **Both captured in-guest 2026-05-29** — see the
> `dxgprobe --qai-admission` evidence below (`tmp/dxgprobe-inguest-qai.log`).

- [x] **G1.1 Open the DXG VMBus channel and negotiate the interface version. — DONE (in-guest 2026-05-29).**
  - Files: `kernel/kernel/dev/hyperv/hyperv_dxg_device.c`,
    `kernel/kernel/dev/hyperv/hyperv_defs_state.c` (`HV_DXG_*`,
    `HV_DXGK_VMBCOMMAND_*`).
  - Verify the channel opens and `HV_DXG_VMBUS_INTERFACE_VERSION` (40) is
    accepted by the host (fall back to OLD/last-compatible as the wire allows).
  - Evidence: the in-guest `dxgprobe --qai-admission` run could not have
    enumerated the host adapter at all unless the DXG VMBus channel opened and
    the interface version was accepted — it returned a real partitioned
    adapter: `qai_admission: enum adapters2 layout=list-first count=1` then
    `selected_vendor=0x10de selected_device=0x28a0
    selected_name=NVIDIA_GeForce_RTX_4060_Laptop_GPU hardware_rc=0 status=PASS`.
    The channel fails closed when the GPU-P endpoint is absent (no-GPU image
    reports `adapter_present=0`).
- [x] **G1.2 Open the adapter and read real adapter identity. — DONE (in-guest 2026-05-29).**
  - Issue `HV_DXGK_VMBCOMMAND_OPENADAPTER` + `QUERYADAPTERINFO`; capture the
    real adapter LUID, driver/UMD version, and feature levels from the host.
  - Evidence (in-guest, `tmp/dxgprobe-inguest-qai.log`): both the enum2 and
    enum3 routes selected the **real RTX 4060 LUID** and
    `OPENADAPTERFROMLUID` succeeded from inside the guest —
    `qai_admission_route route=direct-openadapterfromluid open_adapter=PASS
    luid=3:4f94fc65 handle=0x40000081` and
    `direct_luid=3:4f94fc65 list_luid=3:4f94fc65 direct_list_luid_match=1`
    (`vendor=0x10de device=0x28a0`, name `NVIDIA GeForce RTX 4060 Laptop GPU`).
    `QUERYADAPTERINFO` type 27 round-trips (`rc=0 status=PASS`). On a no-GPU
    image it reports `adapter_present=0 status=PASS_FAILCLOSED`.

### Section G2. Process / device / context lifetime

- [x] **G2.1 Create the DXG process, device, and context objects. — DONE (in-guest 2026-05-29).**
  - Files: `kernel/kernel/dev/hyperv/hyperv_dxg_handle_manager.c`,
    `hyperv_dxg_ioctls.c`; ABI `kernel/kernel/inc/uabi/d3dkmthk.h`. Model
    ownership on WSL `dxgkrnl` (`dxgprocess`/`dxgdevice`/`dxgcontext`).
  - Commands: `CREATEPROCESS`, `CREATEDEVICE`, `CREATECONTEXTVIRTUAL`.
  - Evidence (in-guest, `tmp/dxgprobe-inguest-qai.log`): real host handles came
    back and round-tripped through the guest process handle table —
    `selected_handle=0x40000001` (enum2), `0x40000041` (enum3),
    `handle=0x40000081` (direct OPENADAPTERFROMLUID), and `handle=0x400000c1`
    (create-adapter-list). The keystone `d3d12probe` PASS additionally created
    the device + context and submitted to host device `0x40000000` (see G4.1),
    proving the full process/device/context lifetime against the real GPU. The
    destroy path is clean (no leaked host objects reported).

### Section G3. Real allocations, residency, and GPUVA

- [x] **G3.1 Create allocations and make them resident on the real GPU. — DONE (in-guest 2026-05-29).**
  - Commands: `CREATEALLOCATION`, `CREATEPAGINGQUEUE`, `MAKERESIDENT`,
    `RESERVE/MAP/FREEGPUVIRTUALADDRESS`.
  - Evidence: the in-guest `d3d12probe` run created host-backed allocations and
    mapped them; the serial log shows real `hyperv-dxg: lock2 host ...
    forwarded` and `unlock2 host ... forwarded=1` transitions against the host
    device `0x40000000` (RTX 4060), not synthetic ones
    (`tmp/d3d12probe-inguest-pass.log`).

### Section G4. Real submission + hardware fence (keystone proof) — HARDWARE-PROVEN IN-GUEST 2026-05-29

> Audit 2026-05-29: `LX_DXSUBMITCOMMAND(TOHWQUEUE)`, the wait/signal sync-object
> ioctls, and `_D3DDDI_MONITORED_FENCE` all forward real packets to the host;
> the kernel never fabricates a fence. The keystone client `d3d12probe` linking
> the staged NVIDIA UMD now PASSES **inside the xv6 GPU-P guest** against the
> real RTX 4060 — the GPU genuinely executed the submitted copy and signalled
> the monitored fence.

- [x] **G4.1 Submit a real command and observe a real monitored-fence signal. — DONE (in-guest 2026-05-29).**
  - Commands: `CREATESYNCOBJECT`, `SUBMITCOMMAND`,
    `SIGNALSYNCOBJECT` / `WAITFORSYNCOBJECTFROMCPU`.
  - The command buffer must be produced by a real UMD, not by hand. **The
    keystone client now exists: `user/programs/d3d12probe/d3d12probe.cpp`.** It
    links the staged GPU-PV runtime (`libd3d12`/`libdxcore` → NVIDIA UMD),
    records a GPU copy-engine command buffer (UPLOAD→DEFAULT→READBACK), submits
    it, waits on a GPU-signalled fence, and verifies the GPU-copied bytes.
  - Host status (2026-05-29): PASS on the real RTX 4060 on the WSL host.
  - **In-guest status (2026-05-29): PASS.** Booting `xv6-os-hyperv` (RTX 4060
    GPU-P) and running `/bin/d3d12probe` printed `D3D12PROBE: adapter[0] "NVIDIA
    GeForce RTX 4060 Laptop GPU" hw=1 vram=7957MiB` then `D3D12PROBE: PASS GPU
    copied 16384 bytes, fence signalled`. SUBMITCOMMAND + monitored fence routed
    through the xv6 DXG forwarding path (serial log shows create-sync / lock2 /
    `unlock2 ... forwarded=1` to host device `0x40000000`). Evidence:
    `tmp/d3d12probe-inguest-pass.log`.
  - **This proves the host GPU is genuinely doing work driven from xv6.**

### Section G5. Present / display handoff — INVESTIGATION COMPLETE (2026-05-29)

- [x] **G5.1 Investigate host-composition / cross-VM present options. — DONE (verdict: no native present on a plain GPU-P guest).**
  - **Findings (from source audit + WSL/WDDM knowledge):**
    1. **WSL `dxgkrnl` has no scanout/display-bind ioctl or VMBus sender.**
       Confirmed again from `fb_dxg_present.c`: the display-bind provider
       (`fb_dxg_present_provider_submit_display_bind` →
       `hyperv_dxg_display_bind_submit`) is structurally complete but stays
       fail-closed on three gates that no host ABI satisfies: `no_host_abi=1`
       (host resource-scanout-bind ABI absent), `no_sender=1` (no GPU-P/WSLg
       display-bind packet sender), `no_completion=1` (no display-completion
       demux). These are host-side absences, not guest bugs.
    2. **`SHAREOBJECTWITHHOST` / `CREATENTSHAREDOBJECT` exist but are for compute
       resource sharing, not scanout.** They hand an NT-shared resource to
       *another DXG process*, not to a host display compositor. They do not
       constitute a present path.
    3. **WSLg shows frames via a host-side Weston/RDP-RAIL agent**, fed through
       WSLg-specific plumbing (the `/mnt/wslg` socket + RDP). A plain Hyper-V
       GPU-P guest does **not** have that host compositor agent, and adding one
       is explicitly disallowed by the honesty gate ("do not add custom host
       tools as the acceptance path").
    4. **Blit-present to the synthvid console** would require first reading the
       GPU-rendered allocation back to guest memory and memcpy'ing it to the
       Hyper-V synthetic framebuffer. The render would be real GPU, but with no
       native present ABI the "present" is a CPU readback+blit, which the
       honesty gate gives **zero display credit**.
  - **Verdict: native display/scanout from a plain Hyper-V GPU-P guest is NOT
    reachable** without either WSLg's host agent or a custom host tool (both
    out of scope). **Scope is therefore compute / offscreen only.** Display
    credit is abandoned on this host; `FB_GPU_BACKEND_F_OPENGL_SUBMIT` (which
    specifically promises a *presented* GL frame) stays `0` permanently here.
  - Display-bind code stays fail-closed exactly as it is — do not weaken its
    gates to manufacture a present.

- [x] **G5.2 — N/A on this host.** No present transport is reachable (see G5.1),
  so no present code is written. The forward path is compute/offscreen (G3/G4)
  validated by a real client, surfaced via a compute-capability bit (G6.1),
  not via the OpenGL-submit/present flag.

### Section G6. Backend flag + consumers (gated on G4, and G5 if present is in scope)

- [x] **G6.1 Decide what `FB_GPU_BACKEND_F_OPENGL_SUBMIT` means for GPU-P. — DONE (2026-05-29, compute-only).**
  - G5 concluded present is unreachable on a plain GPU-P guest, so a separate
    honest capability bit was added: `FB_GPU_BACKEND_F_GPU_COMPUTE` (0x0400) in
    `kernel/kernel/inc/dev/fb.h` means "real GPU compute (D3D12/D3DKMT submit +
    hardware monitored-fence) is reachable", proven in-guest by `d3d12probe`.
    `FB_GPU_BACKEND_F_OPENGL_SUBMIT` stays `0` for the DXG backend (it
    specifically promises a *presented* GL frame, which G5 cannot satisfy here).
  - File: `kernel/kernel/dev/fb/fb_drm_core_kms.c` (`gpu_backend_fill`): the bit
    is set only when `hyperv_dxg_d3dkmt_ready()` is true at runtime (reachable
    D3DKMT path), never on build-success alone.
  - Evidence: `fbstat`/`dxgprobe` report `GPU_COMPUTE` honestly; `OPENGL_SUBMIT`
    remains `0`.
- [x] **G6.2 Route a real consumer (Mesa D3D12 / dzn, or a compute client). — DONE (2026-05-29, compute client).**
  - The real consumer is `user/programs/d3d12probe` linking the staged GPU-PV
    runtime (NVIDIA D3D12 UMD). It submitted a real GPU copy-engine command
    buffer that completed on the host RTX 4060 via the G1–G4 contract (in-guest
    PASS, `tmp/d3d12probe-inguest-pass.log`). On a no-GPU image it fails closed
    (`D3D12PROBE: ERROR no hardware D3D12 adapter`, non-zero exit).
  - `dxgprobe` now decodes and reports the capability honestly:
    `qai_admission_backend ... backend_gpu_compute=<0|1>`
    (`user/programs/dxgprobe/dxgprobe.c` `probe_backend_opengl_submit_flag`),
    while `backend_opengl_submit` stays `0`. Mesa `dzn` is not required (the
    native NVIDIA UMD builds real command buffers).
- [x] **G6.3 Broaden the OpenGL/GLES consumer coverage on the real GPU. — DONE (in-guest 2026-05-29).**
  - Beyond the compute client, Mesa's **d3d12 Gallium driver** runs GLES2/3 on
    the real RTX 4060 via the staged GPU-PV UMD (libdxcore/libd3d12 + NVIDIA UMD)
    over `/dev/dxg` — **no `/dev/dri` render node**. Three GL consumers are now
    proven in-guest, in increasing breadth:
    - `gldemo` — offscreen GLES2 FBO triangle + `glReadPixels` verify.
    - `mesawlegl` — on-screen spherical-poly demo (GPU render + blit-present).
    - `mesaglfeature` — **broad GLES2 feature probe**: shader compile/link, VBO,
      texture sampling, FBO with `DEPTH24_STENCIL8`, viewport/scissor, blending
      (`GL_ONE,GL_ONE`), and depth-test, with two-size readback verification.
  - Fix that enabled the broad probe in-guest: `mesaglfeature` `main()` had
    forced `MESA_LOADER_DRIVER_OVERRIDE=softpipe` + `LIBGL_ALWAYS_SOFTWARE=1`
    unconditionally, which overrode the shell's `GALLIUM_DRIVER=d3d12` and
    fail-closed in `require_d3d12_renderer()`. It now branches on a requested
    GPU path (env `XV6_MESAGLFEATURE_REQUIRE_D3D12` / `GALLIUM_DRIVER=d3d12` /
    `MESA_LOADER_DRIVER_OVERRIDE=d3d12`): GPU mode leaves the software override
    unset; otherwise it keeps the softpipe software smoke default.
  - Evidence (in-guest, bare `/bin/mesaglfeature`, no env prefix):
    `mode=gpu-d3d12 (hardware render)`,
    `renderer=D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)` OpenGL ES 3.1,
    `D3D12 renderer confirmed`, `pass size=32x32 center=80,160,240,255
    corner=0,0,0,255`, `pass size=64x32 ...`, `mesaglfeature: ok`.
    `tmp/mesaglfeature-inguest.log` (host smoke: `tmp/mglf-host.log`).
  - Honesty unchanged: this is **offscreen GPU render + CPU readback**, not a
    native present. `FB_GPU_BACKEND_F_OPENGL_SUBMIT` (which promises a
    *presented* GL frame) stays `0` on this host (G5).

### GPU-P / DXG dependency graph

```
G0 host GPU-P + UMD staged
  -> G1 DXG channel + adapter identity
       -> G2 process/device/context
            -> G3 allocations + residency + GPUVA
                 -> G4 real submit + HW fence  (keystone real-GPU proof)
                      -> G5 present (INVESTIGATE first; may be compute-only)
                           -> G6 backend capability flag + real consumer
```

</details>

---

## Out of scope: Nouveau / DDA (dropped 2026-05-29)

The DDA + Nouveau-on-raw-silicon route is **abandoned as a goal** on this host
and is no longer planned work. It is physically unreachable here (GPU-P exposes
no MMIO BARs; true DDA fails with `0xC035001E` on this consumer RTX 4060 Laptop
GPU). The scaffold files (`kernel/kernel/dev/fb/fb_nouveau.c` and the
`nouveau_*` stats) stay in the tree **fail-closed** for a hypothetical
DDA-capable host, but no further Nouveau bring-up is on this plan. Do not spend
effort implementing Nouveau MMIO/VRAM/firmware/channel/submit paths.

## Next focus

Section V4 is complete for the required desktop-wide virgl acceptance gate:
Tier 0/1/2 + Tier D (D1 chrome, D2 any-client, D3
WebKit), Tier P P2/P3/P4, and the hardware cursor (V4.HC) are complete and
validated. A fresh canonical glsmoke trace (2026-06-06) holds 80–96 FPS with
the scanout set cached (`scanout_rebinds=0`). Remaining open work outside
required V4 acceptance:

- **Interactive vs trace parity** — `scripts/launch/launch-gui.sh` presents in
  `mode=bo-present` (`displayed_fps=0.0` metric artifact, `path_cpu_only` high)
  rather than the GL-scanout page-flip path the trace flags engage. Make the
  interactive launcher default-enable the same present path (or document why it
  differs) so what the user sees matches the validated architecture.
- **V6** — keep `OPENGL_SUBMIT` honest (native-present credit only once the
  page-flip present lineage is proven).
- **Tier 3** (windowed unredirection) is optional.

Diagnostic watch item: the underlying WSL d3d12 virglrenderer wedge is narrowed
to a small WebKit accelerated-compositing draw/update batch. WebKit defaults to
synchronous virgl submits to avoid it; `webkit_virgl_sync_submit=0` remains for
A/B diagnosis. Do not chase this in the kernel for FPS.
