# GPU Plan: virtio-gpu 3D (virgl) under KVM acceleration

Last updated: 2026-06-04

> **Active focus (2026-06-04): windowed virgl desktop FPS parity reached.**
> V0-V3 and V4 Tier 0-2 are done on the default `-gl` path. The desktop now
> uses the page-flip scanout swap, three virgl scanout resources, nonblocking GL
> submit fences, and queued release after GL/display readiness, so
> `displayed_fps` tracks `app_loop_fps` in the 60+ FPS band with real demo
> pixels in a full 1280x800 screenshot. Keep the strict screenshot/pixel gate
> for regressions. Everything outside V4 is either done (reference), optional
> Tier 3, or deferred (WebKit B2).

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
| WebKit / Skia GL via virgl | **Blocked** | `SkiaGPUWorker` SIGSEGV at GL-context creation under `virtio-gpu-gl` (blocker B2) |
| Host requirement | **Host GL / `/dev/dri` needed** | QEMU `virtio-gpu-gl`; without host GL, virgl falls back to software (`check-gui-accel.sh`) |

2026-06-01 update: xv6 now proves both paths separately. The Mesa Wayland demo
renders through `renderer=virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`
on WSL/QEMU and shows the rendered sphere on screen, but wlcomp's windowed
present path still cannot claim native GPU-present credit:
`VIRGL_CCMD_RESOURCE_COPY_REGION`, `VIRGL_CCMD_BLIT`, and the
`VIRGL_CCMD_COPY_TRANSFER3D` diagnostic are accepted by QEMU/virglrenderer but
strict source/destination sample validation shows that they leave the
destination black on this D3D12 virgl host. The kernel detects those failed
GPU-side copies and falls back automatically to the readback/CPU-present lane
instead of leaving a blank window. That fallback is correct and visible at about
40-52 FPS.

The Alpine trace has been reproduced in the xv6 KMS path for the case it
actually exercises: full-screen `DRM_IOCTL_MODE_SETCRTC` /
`DRM_IOCTL_MODE_PAGE_FLIP` framebuffers now attempt `FB_GPU_BO_PRESENT_F_VIRGL_SCANOUT`
first, which issues a full-resource `SET_SCANOUT` + `RESOURCE_FLUSH` instead of
copying the framebuffer through CPU memory. The `mesakmsgl` probe mirrors
Alpine's GBM/EGL/KMS shape and `scripts/gpu/virgl-kms-validate.sh` validates
KMS mode 1280x800, `DRM_CAP_PRIME` export/import (`has_export=1`), the virgl
NVIDIA renderer, and post-warmup FPS samples near the display refresh rate. If
scanout bind fails, the existing readback/CPU-present fallback is retried. This
is deliberately limited to exact scanout-sized KMS framebuffers; partial/window
sized resources must not be bound directly because QEMU treats that as a
scanout resize and the host window jumps. The remaining smoothness gap is a
real virgl compositor pass for windowed surfaces, or converting wlcomp itself
into a full-screen page-flippable virgl compositor.

Honesty gate: **`FB_GPU_BACKEND_F_OPENGL_SUBMIT` is only credit for the
KVM/virgl render backend.** It is justified by the direct KMS `mesakmsgl`
lineage on this host, but it must not be treated as wlcomp/native-present
credit. A host that silently falls back to software GL must not flip the flag.

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

### Known blockers (must fix before claiming KVM acceleration)

- **B1 — virgl-ready boot race.** `fbdevinit` prints
  `GPU: virgl unavailable; exposing dumb-buffer DRM only` because
  `virtio_gpu_has_virgl()` is still false when the framebuffer initializes, yet
  the capset becomes ready moments later (`virtio_gpu: virgl capset ready`). Any
  consumer that latches the GPU backend flag once at init can miss virgl, or —
  worse — see `OPENGL_SUBMIT` flip on after a software decision was already
  taken. The backend capability must be evaluated **after** capset
  initialization completes (or be re-queried lazily), not latched early.
- **B2 — WebKit/Skia GL-context crash under `virtio-gpu-gl`.** With
  `webkit_accel=1`, the WebProcess `SkiaGPUWorker` thread takes a fatal NULL
  deref (`cr2=0x28`) at GL/EGL context creation and the page never loads;
  software mode (`webkit_accel=0`) renders fine. Root-cause whether this is the
  guest Mesa `virgl` EGL path, a missing host GL capability, or B1 handing
  WebKit a half-ready backend.

---

## Active plan — virtio 3D (virgl) under KVM (do this top to bottom)

Each item lists **what to build/verify**, **which files**, and the **runtime
evidence** that lets you check the box. Build-success alone is never enough: you
need runtime evidence from a KVM host with virgl, plus a passing fail-closed
negative (a no-virgl image must fall back to the dumb buffer and never advertise
`OPENGL_SUBMIT`).

### Section V0. Host + launch prerequisites (KVM/QEMU)

- [ ] **V0.1 Confirm the host can run virgl.** Host has a usable GL/EGL stack and
  a `/dev/dri` render node; QEMU launches with `virtio-gpu-gl` (or
  `virtio-vga-gl-primary`). `scripts/check-gui-accel.sh` must not warn
  "no host /dev/dri nodes are visible". Record the host GL renderer string.
- [ ] **V0.2 Boot xv6 under KVM with virtio-gpu-gl** via `scripts/run-qemu.sh`
  (`QEMU_GPU=virtio-gpu-gl`) and capture the serial log showing
  `virtio_gpu: virgl capset ready id=.. version=.. size=..`. Fail-closed check:
  a plain `virtio-gpu` (no `-gl`) launch must log `no virgl capset found` and
  `GPU: virgl unavailable; exposing dumb-buffer DRM only`.

### Section V1. Fix the virgl-ready boot race (blocker B1)

- [ ] **V1.1 Evaluate the GPU backend capability after capset init, not before.**
  Files: `kernel/kernel/dev/fb/fb_init_panic.c` (the early
  `virtio_gpu_has_virgl()` print) and `fb_drm_core_kms.c` `gpu_backend_fill`.
  Ensure `virtio_gpu_query_capsets` has completed before any consumer latches
  the backend flag, or make `gpu_backend_fill` reflect late capset readiness.
  Evidence: a boot where the framebuffer no longer prints "virgl unavailable"
  while virgl is in fact present, and `fbstat` reports the `virgl` backend with
  `OPENGL_SUBMIT` consistently across reads.

### Section V2. Kernel virgl ioctl self-test under KVM

- [ ] **V2.1 Run `virgltest` in-guest on the virtio-gpu-gl host.** It must pass
  sync submit, async submit + fence wait, and the two negative paths
  (`FB_GPU_VIRGL_SUBMIT_FORCE_FAIL` rejected; a failed context rejects later
  submits). Evidence: `virgltest: async-submit queued ... final_signaled` past
  `initial_signaled` with a real host-advanced fence, plus the negative paths
  failing closed. A no-virgl image must make `virgltest` fail closed at the open
  or capset gate.

### Section V3. Mesa virgl GL consumer in-guest

- [ ] **V3.1 Offscreen GLES render via the Mesa `virgl` Gallium driver.** Run
  `gldemo` (offscreen GLES2 FBO triangle + `glReadPixels`) with
  `GALLIUM_DRIVER=virgl` and verify the pixels came from host GL, not softpipe.
  Evidence: the renderer string identifies virgl / host GL and the readback
  center/corner pixels match the drawn triangle. (`gldemo`/`mesaglfeature`
  already pass on the DXG `d3d12` Gallium driver; this proves the `virgl` path.)
- [ ] **V3.2 Broaden coverage** with `mesaglfeature` (shader compile/link, VBO,
  texture sampling, FBO depth/stencil, blending, depth-test) on
  `GALLIUM_DRIVER=virgl`, with two-size readback verification.

### Section V4. Windowed GPU present + FPS maximization (ACTIVE)

Goal of this section: make the **windowed** desktop present at a sustained
**60+ FPS** (Alpine simple-EGL band is 66-73 FPS on this exact host), with the
3D demo and the desktop chrome both GPU-presented, no host-window resize, no
blinking, correct Y orientation.

#### Why this is reachable (decisive evidence)

The reference captures already in the tree settle that the **host is not the
limit**: Alpine on this same WSL-d3d12 / RTX 4060 / single virtio-gpu control
queue runs `glxgears` at **141 FPS** and `weston-simple-egl` at **66-73 FPS**
(`build-x86_64/alpine-trace/...`). The gap is entirely the xv6 **guest
compositor present architecture**.

Two configurations measured 2026-06-03:

- **Swap OFF (default path, `/tmp/xv6-gui-pipe.log`):** `app_loop_fps ~46-56`
  but `displayed_fps stuck ~25-29` — display advances at **roughly half** the
  app loop. Present breakdown: `cpu_upload ~0.2ms` (Tier 0 chrome-skip working,
  `cpu_rects/frame=0`), `gl_compose ~6ms`, `scanout ~8ms`, `frame_avg ~14-16ms`.
  The kernel collapses the scanout rects to a single `RESOURCE_FLUSH`, so the
  host-submit count is already 1.
- **Swap ON (`wlcomp_page_flip_present=1 wlcomp_virgl_fb_damage_flip=1
  wlcomp_virgl_fb_buffers=1`, clean run `/tmp/xv6-flip-validate.log`):**
  `displayed_fps` **tracks** `app_loop_fps` — ~48-54 on virgl/D3D12 and ~67-70
  on LLVMpipe. The trace shows front/back resources alternating every frame
  (`virgl framebuffer flip render_res=5 back_res=4` ... `render_res=4
  back_res=5`), `double-buffer prep ready front_res=4 back_res=5`, and two
  imported GL targets. **The ~2x gap is closed in this configuration.**

**Status of the page-flip pipeline (verified at runtime 2026-06-03):** the
double-buffered front/back swap is **implemented, works, and is default-on for
the `-gl` path**; Tier 2 triple-buffering and compositor GL submit fences are
also default-on for `-gl`. Compose routes into the inactive target, present
flips it, resource ids cycle 4/5/6, and queued releases wait for GL/display
readiness without a fixed frame-number limit. The strict full-frame screenshot
gate now passes with real demo pixels, closing the earlier blank/blue client
body failure.

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

#### Step M (baseline) — record the starting numbers before changing anything

- **Do:** Build, run the trace launch (see Validation harness), capture
  `app_loop_fps`, `displayed_fps`, and the `present-trace` line
  (`frame_avg_us`, `gl_compose_avg_us`, `scanout_avg_us`,
  `scanout_rects/frame`, `cpu_rects/frame`). Save them to a baseline note.
- **Passing criteria:** A recorded baseline log exists with all of the above
  numbers. No code changed yet. This is the reference every later step is
  compared against.

#### Tier 0 — submit-count reduction (low risk). DONE (see Completed prerequisites).

Tier 0 is complete: `cpu_rects/frame=0` on GPU-only frames, scanout coalesced to
one host `RESOURCE_FLUSH`, async flush default-on. The Tier 0 FPS gate was *not*
reached on its own (the old default path had `displayed_fps ~28`, about half of
`app_loop_fps ~50`) because the structural limiter was the single shared
scanout buffer. Tier 1 fixed that on the default `-gl` path. Do not re-do Tier
0 work; continue with Tier 2.

#### Tier 1 — double-buffered scanout + page-flip (THE LIVE GAP). Target: ~28 -> ~55-60 FPS.

State (validated at runtime 2026-06-03): the page-flip front/back swap is
**implemented, works, and is default-on for `-gl`**: compose routes into the
inactive target, present flips it, and the resource ids alternate every frame so
`displayed_fps` tracks `app_loop_fps`.

- [x] **V4.T1.1 Allocate two full-screen scanout BOs (front/back).** Done:
  `alloc_virgl_framebuffer_target("back", &g_fb_virgl_back_target)` allocates a
  second full-screen virgl resource when `wlcomp_page_flip_present` (or
  `wlcomp_virgl_fb_buffers>=2`) is set (`wlcomp_fb.inc` ~line 952). Logs
  `double-buffer prep ready front_res=.. back_res=..` and
  `using virgl framebuffer back target res=.. handle=..` (both confirmed in
  `/tmp/xv6-flip-validate.log`).
- [x] **V4.T1.2 Compose into the back buffer; drop the redundant copy + separate
  flush.** **DONE (validated 2026-06-03).** With the swap enabled the compose +
  chrome upload route into the inactive (back) target, present flips it, and the
  front/back roles swap. Runtime evidence: two imported GL targets
  (`gl-compose target import ok handle=2 tex=3 res=4` and `handle=4 tex=5
  res=5`), `cpu_rects/frame=0` on animating frames, pixels correct, QEMU window
  stays 1280x800. The redundant second full-screen pass is gone.
- [x] **V4.T1.3 Make the kernel page-flip actually alternate resources.**
  **DONE (validated 2026-06-03).** `present_virgl_scanout_rects()` passes the
  back BO handle on alternating frames; the trace shows the flip alternating
  between the two resources every frame:
  `virgl framebuffer flip render_res=5 render_handle=4 back_res=4` then
  `render_res=4 render_handle=2 back_res=5`, monotonic `count=..`. The kernel
  `SET_SCANOUT`s the newly-composed resource and async-flushes (no re-upload).
  The ioctl still fails closed when virgl is absent (the swap path only arms
  when the back virgl target allocated).
- [x] **V4.T1.4 Make the validated buffer-swap default-on for `-gl`.**
  **DONE (validated 2026-06-03).** `scripts/launch/run-qemu.sh` now defaults
  `wlcomp_page_flip_present=1` and `wlcomp_virgl_fb_damage_flip=1` for
  `*-gl` launches via `qemu_append_default_flag`, preserving explicit opt-outs
  (`wlcomp_page_flip_present=0 wlcomp_virgl_fb_damage_flip=0`) and leaving the
  plain non-GL `virtio-gpu` path untouched.
  - Files: `ports/wayland/src/wlcomp_fb.inc`
    (`wlcomp_page_flip_present_enabled`, the buffer-count / damage-flip default
    selection), the `-gl` launch defaults in `scripts/launch/run-qemu.sh` /
    `scripts/launch/launch-gui.sh`, and any `QEMU_APPEND` default wiring.
  - Evidence: `scripts/gpu/virgl-desktop-validate.sh` with no page-flip env
    flags passed in
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-default-pageflip180.log`.
    Warm samples: `app_loop_fps=48.6 displayed_fps=49.2` and
    `app_loop_fps=54.2 displayed_fps=51.6`; present trace has
    `cpu_rects/frame=0`, `gl_compose_avg_us=93`, `scanout_avg_us=7699`,
    `scanout_rects/frame=1`, `scanout_submits=52`, `scanout_rebinds=52`.
    QEMU trace matrix:
    `page_flip_trace_matrix scanouts=185 unique=3,4,5 sample=5,4,5,4,... flushes=186 status=PASS`;
    full-size scanouts alternate resources 5/4 with no sub-fullscreen resize.
    Screenshot validation passed:
    `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962 status=PASS`.
  - Fail-closed evidence: plain `QEMU_GPU=virtio-gpu` run
    `build-x86_64/virgl-desktop-validate/xv6-nogl-failclosed.log` reports
    `GPU: virgl unavailable; exposing dumb-buffer DRM only`, `backend dumb`,
    `backend_opengl_submit 0`, `backend_virgl_opengl 0`,
    `linux-dmabuf disabled (no virgl)`, and no page-flip/double-buffer logs.
- **Tier 1 gate:** met on the **default** `-gl` run: `displayed_fps` tracks
  `app_loop_fps` (the ~2x gap is closed), pixels validated, and the window
  remains 1280x800. Continue to Tier 2 for sustained >=60 FPS.

#### Tier 2 — compositor pipelining (overlap render and present). Target: steady 60 FPS.

- [x] **V4.T2.1 Triple-buffer (three scanout resources).**
  **DONE (validated 2026-06-03).** `*-gl` launches now default
  `wlcomp_virgl_fb_buffers=3`; `wlcomp_fb.inc` allocates a third scanout target
  (`back2`) and rotates `render/back/extra` so the next compose target is not
  the just-presented resource. `wlcomp_gl_compose.inc` caches all three target
  EGL images/textures to avoid per-frame re-import.
  - Files: `wlcomp_fb.inc`, `wlcomp_render_loop.inc`.
  - Evidence: default `-gl` validation
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-default-triple180.log`
    passed with screenshot validation. Logs show
    `triple-buffer prep ready front_res=4 back_res=5 extra_res=6` and the
    cycle `render_res=5 -> 6 -> 4`. QEMU trace matrix:
    `page_flip_trace_matrix scanouts=184 unique=4,5,6 sample=5,6,4,5,6,4,... status=PASS`;
    counts are balanced (`4=61`, `5=61`, `6=61`) with no sub-fullscreen
    `SET_SCANOUT`. Before T2.2/T2.3, warm FPS still settled around
    `app_loop_fps=51.9-53.9` / `displayed_fps=53.6-53.9`, so the submit-fence
    and async release work below was required for sustained >=60 FPS.
- **Strict-pixel correction (2026-06-03):** after strengthening
  `scripts/gpu/virgl-desktop-validate.sh` to capture the full 1280x800 frame
  and require real demo pixels inside the window, the pre-T2.2 default `-gl`
  run failed screenshot validation even though its counters could reach
  `app_loop_fps=64.3 displayed_fps=64.0`, `62.4/60.3`, and `63.0/62.7`.
  Evidence:
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop.log` ends with
  `screenshot_matrix ... width=1280 height=800 ... demo_bright=75 demo_cyan=0
  demo_dark=49 status=FAIL reason=missing_demo_pixels`. The screenshot shows
  desktop chrome and the window frame/title, but the app body is blank/blue.
  Diagnostic `wlcomp_gl_flush=1` makes screenshot validation pass
  (`demo_bright=10079 demo_cyan=15122 demo_dark=197422 status=PASS`) but drops
  the run below the acceptance band and has stability issues. Therefore T2.2 is
  not only a scanout rebind/FPS problem; it must also make compositor GL work
  visible to scanout without blocking the loop.
- [x] **V4.T2.2 Reap/page-flip asynchronously; never block the event loop.**
  **DONE (validated 2026-06-03).** The default `-gl` launch now enables
  `wlcomp_gl_submit_fence=1`, so the compositor issues a cheap GL fence/flush
  after drawing instead of `wlcomp_gl_flush=1`/`glFinish()` on every present.
  Buffer release is queued and reaped only after both the display present fence
  and the compositor GL sync are ready; the release path does **not** use a
  fixed frame-number delay.
  - Files: `ports/wayland/src/wlcomp_gl_compose.inc`,
    `ports/wayland/src/wlcomp_buffer_shm.inc`,
    `ports/wayland/src/wlcomp_render_loop.inc`, and the `-gl` default in
    `scripts/launch/run-qemu.sh`.
  - Evidence: default strict validation
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-default-final.log`
    passed. Present trace steady-state samples show no CPU uploads on animating
    frames (`cpu_rects/frame=0`), cheap compositor submission
    (`gl_compose_avg_us` commonly ~130-320us), async page-flip scanout
    (`scanout_avg_us` commonly ~3.1-5.3ms), and queued release rather than
    same-frame immediate release:
    `release_gpu_immediate=0 release_queued=70 release_flushed=70
    release_pending=2`. Full-frame screenshot validation passed without
    `wlcomp_gl_flush=1`:
    `screenshot_matrix ... width=1280 height=800 ... demo_bright=10035
    demo_cyan=15170 demo_dark=197354 demo_colorful=32252 demo_unique=108
    status=PASS`.
  - QEMU trace evidence: `page_flip_trace_matrix scanouts=8471 unique=4,5,6
    sample=5,6,4,5,6,4,5,6,... flushes=8472 flush_unique=3,4,5,6
    status=PASS`; the validator rejected any post-desktop non-1280x800
    `SET_SCANOUT`, so no host-window resize path was taken.
  - **Passing criteria:** no synchronous fence wait in steady-state present-
    trace; compositor GL pixels are visible in full-frame screenshots without
    `wlcomp_gl_flush=1`; cursor stays responsive while the demo runs; the loop
    blocks only when all three buffers are in flight.
- [x] **V4.T2.3 Keep early frame-callback dispatch (already on).**
  **DONE (validated 2026-06-03).**
  - File: `wlcomp_render_loop.inc` (`wlcomp_pipeline_callbacks`, default 1).
  - Evidence: default strict validation
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-default-final.log`
    shows sustained post-warmup samples in the target band, for example
    `app_loop_fps=69.6 displayed_fps=66.9`, `71.2/71.6`, `68.9/67.9`,
    `70.9/69.8`, `68.9/68.9`, and `70.5/73.0`. The displayed/app gap is
    within ~10% on the steady samples and both are >=60 FPS.
  - **Passing criteria:** `app_loop_fps` and `displayed_fps` both ~60 and within
    ~10% of each other.
- **Tier 2 gate:** met on the default `-gl` run: `displayed_fps` sustained
  **>= 60 FPS** post-warmup, frequently in the 66-73 FPS Alpine simple-EGL
  band, with full-frame demo pixels validated.

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

1. [x] `displayed_fps` within ~10% of `app_loop_fps`, sustained **>= 60 FPS**
   post-warmup (Tier 2 complete). Evidence:
   `build-x86_64/virgl-bisect/final-default-completion.log` tail-20 average
   `app_loop_fps=68.3 displayed_fps=67.7`, max app/display gap `7.5%`,
   with the final sample `68.3/72.6`.
2. [x] QEMU window stays 1280x800 throughout; no host-window resize at any
   point. Evidence: the same log's QEMU trace rejects non-desktop post-desktop
   `SET_SCANOUT`, and its page-flip matrix reports full-size scanouts cycling
   resources 4/5/6:
   `page_flip_trace_matrix scanouts=8877 unique=4,5,6 ... status=PASS`.
3. [x] No panel/desktop blinking; correct Y orientation; demo **and** chrome
   both visible and updating. Evidence: full-frame screenshot matrix in
   `final-default-completion.log` passes with real centered demo pixels and
   nonblank desktop/chrome pixels:
   `demo_bright=9873 demo_cyan=13645 demo_dark=197246
   demo_colorful=31692 status=PASS`.
4. [x] Pixels validated with `fbstat ppm-current` / `fbstat sample` — never FPS
   text or counters alone. Evidence:
   `build-x86_64/virgl-bisect/final-default-completion.ppm`,
   `build-x86_64/virgl-bisect/final-readback-virglcopy-pass.ppm`, and
   `build-x86_64/virgl-bisect/final-nogl-failclosed-desktop.ppm` all pass
   pixel matrices and were visually inspected.
5. [x] Every new path is flag-gated and fails closed when virgl is absent: a
   plain `virtio-gpu` (no `-gl`) image falls back to the dumb buffer, never sets
   `OPENGL_SUBMIT`, and does not panic. Evidence:
   `build-x86_64/virgl-bisect/final-nogl-failclosed-desktop.log` reports
   `virtio_gpu: no 3D capsets advertised`, `GPU: virgl unavailable; exposing
   dumb-buffer DRM only`, `dumb render node`, `linux-dmabuf disabled (no
   virgl)`, no `OPENGL_SUBMIT`, no page-flip logs, no panic, and
   `nogl_screenshot_matrix ... width=1280 height=800 ... status=PASS`.
6. [x] The existing readback/CPU-present fallback lane still works (no
   regression). Evidence:
   `build-x86_64/virgl-bisect/final-readback-virglcopy-pass.log` forced the
   fallback shape explicitly with
   `wlcomp_gl_compose=0 wlcomp_virgl_fb=0 wlcomp_page_flip_present=0
   wlcomp_virgl_fb_damage_flip=0 wlcomp_gl_submit_fence=0
   wlcomp_gpu_virgl_copy=1`; it stayed on `path_cpu_only` with
   `scanout_submits=0 scanout_rebinds=0`, tail-10 average
   `app_loop_fps=54.9 displayed_fps=54.5`, and the full-frame screenshot matrix
   passed with `demo_bright=8386 demo_cyan=13879 demo_dark=198790
   demo_colorful=32271 status=PASS`.

**Revalidated 2026-06-04 after the submit-fence throttle fix and explicit
opt-out parser fix.** Strict default `-gl` validation passed in
`build-x86_64/virgl-bisect/final-default-completion.log`: tail-20 averaged
`app_loop_fps=68.3 displayed_fps=67.7`, full-frame pixels passed
(`final-default-completion.ppm`, `demo_bright=9873 demo_cyan=13645
status=PASS`), and QEMU trace page-flip validation cycled full-size resources
`4,5,6` only. Plain no-`-gl` fail-closed validation passed in
`final-nogl-failclosed-desktop.log` / `.ppm`: dumb backend, no virgl, no
OpenGL-submit/page-flip markers, and visible 1280x800 desktop pixels. Forced
readback/CPU-present fallback passed in `final-readback-virglcopy-pass.log` /
`.ppm`, staying on `path_cpu_only` with `scanout_submits=0 scanout_rebinds=0`
and tail-10 average `54.9/54.5`.

#### Honesty gates (carry through every step)

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

Run with present trace (serial -> log; runs in background):

```sh
cd /home/es/xv6-os && rm -f /tmp/xv6-gui-pipe.log && \
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

Validate pixels (in guest) and fail-closed (host):

```sh
fbstat ppm-current /current.ppm 0 0 1280 800
# Fail-closed: relaunch with QEMU_GPU=virtio-gpu (no -gl) and confirm the log
# shows dumb-buffer fallback, no OPENGL_SUBMIT, no panic, and capture pixels.
```

#### Handover prompt (paste verbatim to the next agent)

```
You are continuing a single, well-defined task in the xv6-os repo at
/home/es/xv6-os: maintain the WINDOWED virgl Wayland desktop FPS parity work on
the DEFAULT -gl RUN by following Section V4 of GPU_REMAINING_GAPS.md. Tier 0,
Tier 1 (V4.T1.1-T1.4), and Tier 2 (V4.T2.1-T2.3) are DONE and validated.
Future work should start with regression triage or optional Tier 3, not by
redoing the closed Tier 2 pipeline.

Verified current status (2026-06-04):
- The double-buffered page-flip swap and Tier 2 triple-buffer cycle are
  IMPLEMENTED, WORK, and are DEFAULT-ON for the virgl `-gl` path. Default
  validation `build-x86_64/virgl-bisect/final-default-completion.log` showed
  `displayed_fps` tracking `app_loop_fps` with tail-20 average
  `68.3/67.7`, max gap `7.5%`, and QEMU trace alternation
  `page_flip_trace_matrix scanouts=8877 unique=4,5,6 ... status=PASS`.
- V4.T2.2/T2.3 are complete after moving submit-fence waiting out of the
  compose hot path and flushing compositor GL after scanout handoff. The same
  default strict validation passed full-frame screenshot pixels
  (`final-default-completion.ppm`,
  `demo_bright=9873 demo_cyan=13645 demo_dark=197246 status=PASS`) with
  no sub-fullscreen `SET_SCANOUT`.
- Plain `QEMU_GPU=virtio-gpu` fail-closed validation passed in
  `build-x86_64/virgl-bisect/final-nogl-failclosed-desktop.log` /
  `.ppm`: no 3D capsets, dumb render node, virgl unavailable,
  `linux-dmabuf disabled (no virgl)`, no `OPENGL_SUBMIT`, no page-flip logs,
  no panic, and `nogl_screenshot_matrix ... status=PASS`.
- The readback/CPU-present fallback lane still works when forced explicitly:
  `build-x86_64/virgl-bisect/final-readback-virglcopy-pass.log` used
  `wlcomp_gl_compose=0 wlcomp_virgl_fb=0 wlcomp_page_flip_present=0
  wlcomp_virgl_fb_damage_flip=0 wlcomp_gl_submit_fence=0
  wlcomp_gpu_virgl_copy=1`, stayed on `path_cpu_only` with
  `scanout_submits=0 scanout_rebinds=0`, produced tail-10 average
  `54.9/54.5`, and passed full-frame screenshot validation.

THE TASK FOR FUTURE REGRESSION WORK: preserve the Tier 2 pipeline and rerun the
strict screenshot validator before claiming success. Relevant files include
`ports/wayland/src/wlcomp_gl_compose.inc`,
`ports/wayland/src/wlcomp_buffer_shm.inc`,
`ports/wayland/src/wlcomp_render_loop.inc`, `ports/wayland/src/wlcomp_fb.inc`,
and the `-gl` defaults in `scripts/launch/run-qemu.sh`.

Ground truth you must accept:
- The host is NOT the limit. Alpine hits 141 FPS glxgears / 66-73 FPS
  simple-EGL on this same WSL-d3d12 / RTX 4060 / single virtio-gpu control
  queue. The cap is the xv6 guest compositor present architecture.
- The swap mechanism, triple-buffer resource cycle, submit-fence ordering, and
  queued release path already work on the default path. Preserve this shape:
  compose(N+1) overlaps present(N), releases are ordered by GL/display
  readiness, and no fixed frame-number delay is used.

Process you must follow for EVERY future step:
1. Read the step and its passing criteria in V4 of GPU_REMAINING_GAPS.md.
2. Read the listed files before editing. Make the smallest change that
   satisfies the step. Keep opt-out/fail-closed behavior intact; the default
   -gl path must fail closed when virgl/back-target allocation fails.
3. Build with the Validation harness commands. Fix all build errors.
4. Run the DEFAULT -gl trace launch (no extra wlcomp_* flags), read
   app_loop_fps + displayed_fps + present-trace, and capture fbstat
   ppm-current. Also capture and inspect a screenshot image. The step PASSES
   only when the DEFAULT run shows sustained >=60 FPS post-warmup with
   displayed_fps tracking app_loop_fps, verified by PIXELS + SCREENSHOT + TRACE
   SHAPE + FPS telemetry, never by a counter or the on-screen FPS text alone.
5. Record the new numbers vs the current baseline above. Tick the checkbox in
   the doc. Move to the next step. Honor each Tier gate before advancing.

Hard rules (violating any is a failure):
- Never resize the display mode for a sub-fullscreen client (causes the
  forbidden host-window 'too large' jump).
- Apply the Y-flip exactly once.
- Fail closed when virgl is absent: a plain virtio-gpu (no -gl) image must
  fall back to the dumb buffer, never set OPENGL_SUBMIT, never panic. Re-verify
  this after Tier 1 and Tier 2.
- Do not regress the existing low/mid-50 FPS readback fallback lane.
- Keep changes scoped; do not revert unrelated dirty GPU files.
- Do NOT git commit or push anything (nested submodules) unless explicitly
  told to.

Consult repo memory for build/run details:
/memories/repo/xv6-os-build.md, xv6-os-runtime.md, xv6-os-hyperv-fb.md,
xv6-os-dxg.md, and session note /memories/session/virgl-desktop-parity.md.

If a step is genuinely blocked, do NOT brute-force or fake it: diagnose with
the present-trace + QEMU virtio-gpu trace, write down the exact failing
criterion and the evidence, try the next viable approach for that step, and
only escalate if no approach satisfies the passing criteria. Then continue
with the remaining steps. You are done ONLY when the V4 global acceptance
list is fully satisfied and re-verified end to end.
```

### Section V5. WebKit / Skia GL via virgl (DEFERRED until V4 is fast)

- [ ] **V5.1 Root-cause and fix the `SkiaGPUWorker` GL-context crash (blocker
  B2).** Deferred: do not start until V4 reaches its global acceptance. Then
  determine whether the NULL deref is the guest Mesa `virgl` EGL path, a missing
  host GL capability, or B1 handing WebKit a half-ready backend. Files:
  `ports/wayland/src/desktop.c` (WebKit GPU policy), the Mesa virgl EGL port,
  the backend flag from V1. Evidence: `webkit_accel=1` loads a page and renders
  a GPU-composited frame without `SkiaGPUWorker` SIGSEGV; the fail-closed
  software fallback still works when virgl is absent.

### Section V6. Backend flag honesty (gated on V4)

- [ ] **V6.1 Keep `OPENGL_SUBMIT` honest.** Advertise it as native-present
  credit only when the V4 page-flip present lineage is proven. Until then it is
  render-backend credit only (see the honesty gate at the top of this file).
  Evidence: `fbstat` / `scripts/check-gui-accel.sh` report virgl only when host
  GL is real; otherwise the dumb-buffer render node.

### virtio virgl dependency graph

```
V0 host GL + virtio-gpu-gl launch   [done]
  -> V1 fix virgl-ready boot race (B1)        [done]
       -> V2 kernel virgl ioctl self-test      [done]
            -> V3 Mesa virgl GL consumer        [done]
                 -> V4 windowed present + FPS max  [ACTIVE: Tier0->1->2(->3)]
                      -> V5 WebKit/Skia GL (B2)     [deferred]
                           -> V6 honest backend flag [gated on V4]
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

The single active engineering goal is **Section V4: windowed GPU present + FPS
maximization** (Tier 0 -> Tier 1 -> Tier 2, optional Tier 3), to a sustained
60+ FPS. WebKit/Skia GL (V5/B2) is deferred until V4 reaches global acceptance;
the software browser route already works and is tracked in
`YOUTUBE_KERNEL_GAP_REPORT.md`.
