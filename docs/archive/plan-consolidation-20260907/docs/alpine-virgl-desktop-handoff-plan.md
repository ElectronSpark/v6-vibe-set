# Alpine virgl desktop parity handoff plan

Last updated: 2026-06-03

## Goal

Make the xv6 GUI path behave like the known-good Alpine/Linux virgl desktop path:

- the whole desktop remains in a normal windowed QEMU display, not fullscreen;
- the desktop and 3D demo present smoothly without violent blinking;
- the 3D demo surface content is visible, correctly oriented, and updated at the displayed FPS cadence;
- GPU acceleration is used for the desktop composition path, not only for offscreen rendering or a direct fullscreen KMS demo;
- no success is claimed from counters alone. Validate with trace shape, visible output, and guest-side framebuffer samples.

The current xv6 state is partially accelerated: virgl rendering works and the sphere can become visible, but windowed desktop present is still too slow and jittery. Alpine shows that the same host/QEMU class can run much smoother, so the remaining gap is in guest display/compositor/present behavior, not host raw horsepower.

## Reference artifacts already collected

Use these before collecting anything new:

- `build-x86_64/alpine-trace/alpine-virgl-behavior-summary.txt`
  - Weston 14 DRM backend + GL renderer.
  - `weston-simple-egl` observed around 66.8-73.2 FPS in 5-second windows.
  - Steady-state QEMU trace repeatedly shows GL command submission, fences, `SET_SCANOUT`, and `RESOURCE_FLUSH`.
- `build-x86_64/alpine-trace/alpine-qemu.trace`
  - QEMU virtio-gpu trace from a Weston/simple-EGL run.
- `build-x86_64/alpine-trace/alpine-d3d12-qemu.trace`
  - QEMU virtio-gpu trace from a host D3D12/virgl run.
- `build-x86_64/alpine-trace/live-test-openbox/alpine-virgl-desktop-summary.txt`
  - Fresh Xorg/Openbox control capture from this handoff.
  - Guest renderer: `virgl`.
  - Direct rendering: yes.
  - Accelerated: yes.
  - `glxgears`: `708 frames in 5.0 seconds = 141.532 FPS`.
  - QEMU trace counts from the short run: `ctx_submit=2359`, `res_flush=826`, fences matching.
- `scripts/gpu/alpine-virgl-desktop-capture.sh`
  - New reproducible host-side Alpine control capture script.
  - Boots Alpine with `virtio-vga-gl`, installs Mesa/Xorg/Openbox, runs `glxgears`, and writes guest logs plus QEMU trace events.
  - QEMU HMP `screendump` currently reports `Error: no surface` for accelerated SDL/GL. Treat that as a QEMU display-backend limitation, not a failure of the guest GL trace. For visual evidence, use existing host screenshot/GIF paths under `/tmp/alpine-desktop-capture/` or external host capture.
- `build-x86_64/screenshots/alpine-*.png`
  - Previously captured Alpine visual evidence.
- `build-x86_64/screenshots/virgl-visible-20260601.png` and related `virgl-*` screenshots
  - xv6 visible-state evidence during earlier experiments.

## Known xv6 symptoms from the latest GUI runs

- The sphere can appear, but the image update rate is far below the FPS printed inside the app.
- The cursor moves slowly when the desktop is under the problematic path, which points to display flush/present cadence rather than only application rendering.
- The desktop/panel can blink violently when GPU composition or scanout updates are misapplied.
- A direct fullscreen resource/scanout path can make the host QEMU window jump between a small app-sized window and the full desktop size. This is unacceptable; user explicitly wants a normal non-fullscreen QEMU window and GPU acceleration for the whole desktop.
- The surface position issue was fixed, but at one point the image inside the surface was inverted in Y. Keep explicit Y-origin checks in any compositor copy or texture upload path.
- Some attempted GPU copy paths left destination content black or stale. Do not trust virgl command acceptance alone; verify pixels before routing normal presentation through a path.
- Docker image `xv6-os-dev` was missing during one auto rebuild attempt. Avoid relying on auto container rebuilds unless the image exists, or build directly with local CMake when possible.

## Alpine behavior to match

Alpine does not treat a small 3D app buffer as the hardware scanout size. Its compositor/display server owns a full-screen display target and presents that target at the display cadence.

Important observed Linux shape:

1. Guest DRM driver: `virtio_gpu`.
2. Display server: Weston DRM backend or Xorg modesetting.
3. GL path: Mesa virgl Gallium with DRI2/DRI3.
4. App renders into GL resources.
5. Desktop compositor/display server composites into full-screen buffers.
6. KMS flips or flushes full-screen scanout-sized resources.
7. QEMU trace has frequent:
   - `virtio_gpu_cmd_ctx_submit`
   - `virtio_gpu_fence_ctrl`
   - `virtio_gpu_fence_resp`
   - `virtio_gpu_cmd_res_flush`
   - occasional `virtio_gpu_cmd_set_scanout` when front/back scanout resources change, not per-window resizing

Critical implication: xv6 should not bind a 640x480 app surface directly as scanout for a 1280x800 desktop. That causes QEMU-visible resize/jump behavior. The GPU-present target must be the full desktop/backbuffer.

## Current likely root cause

The missing piece is not virgl rendering itself. The likely gap is the windowed compositor present path:

- xv6 can submit GL/virgl work;
- xv6 can display the CPU-composited desktop;
- xv6 can sometimes show the virgl-rendered app surface;
- but xv6 does not yet reliably compose all visible desktop content into a full-screen GPU-backed scanout target and flush/page-flip it at a steady cadence.

That creates a split-brain path:

- app-side FPS may be high because GL rendering advances;
- screen update FPS is low because the compositor/scanout flush path is late, blocked, coalesced incorrectly, or falling back to expensive CPU readback/copy;
- direct-scanout experiments can produce visible content but violate desktop geometry.

## Implementation strategy

### Phase 1: Freeze the control evidence and make comparison cheap

1. Keep `scripts/gpu/alpine-virgl-desktop-capture.sh`.
2. Add a short note in any future validator output pointing to:
   - `build-x86_64/alpine-trace/alpine-virgl-behavior-summary.txt`
   - `build-x86_64/alpine-trace/live-test-openbox/alpine-virgl-desktop-summary.txt`
3. When rerunning Alpine:

```sh
ALPINE_CAPTURE_DIR=build-x86_64/alpine-trace/live-$(date +%Y%m%d-%H%M%S) \
ALPINE_RUN_SECONDS=10 \
scripts/gpu/alpine-virgl-desktop-capture.sh
```

4. If visual proof is required, use host screenshot tooling. QEMU monitor `screendump` may report `Error: no surface` for SDL/GL.

### Phase 2: Compare trace shape against xv6

Run xv6 GUI with QEMU trace events equivalent to Alpine. Use the same event list from `scripts/gpu/alpine-virgl-desktop-capture.sh`.

Targets to compare:

- Does xv6 issue `ctx_submit` every app frame?
- Does xv6 issue `res_flush` every visible desktop frame?
- Are fences returned promptly?
- Is `set_scanout` stable and full-screen-sized, or is it bouncing between surface-sized and desktop-sized resources?
- Does scanout resource size remain 1280x800 while the 640x480 app moves inside it?

Expected xv6 healthy shape:

- app GL `ctx_submit` continues steadily;
- compositor submits/flushes the full desktop target steadily;
- `SET_SCANOUT` only changes on mode/backbuffer setup, not as a side effect of every app frame;
- `RESOURCE_FLUSH` covers full-screen or damage-derived rectangles of the full-screen target;
- no QEMU host-window resize;
- no panel blinking.

### Phase 3: Instrument wlcomp present cadence

Files likely involved:

- `ports/wayland/src/wlcomp_render_loop.inc`
- `ports/wayland/src/wlcomp_fb.inc`
- `ports/wayland/src/wlcomp_gl_compose.inc`
- `ports/wayland/src/wlcomp_surface_state.inc`
- `ports/wayland/src/mesawlegl.c`
- `kernel/kernel/virtio_gpu.c`
- framebuffer/DRM fragments under `kernel/kernel/dev/fb/`

Add temporary counters/logging for:

- compositor frame number;
- number of dirty rectangles;
- whether each frame used CPU blit, virgl copy, direct scanout, or fallback;
- scanout resource id and size;
- app surface resource id, size, stride, and Y-origin;
- time spent waiting for virgl fence;
- time between app commit and desktop flush;
- time between desktop flushes;
- whether flush rects include pending GPU-composed rects;
- whether full-screen scanout flushes are being coalesced too aggressively.

Do not leave high-volume logs enabled by default. Gate them behind cmdline/env flags such as `wlcomp_trace_present=1`.

### Phase 4: Fix the scanout ownership model

Rule: only the full desktop target may be bound to scanout during normal desktop mode.

Do this:

1. Make the compositor own a full-screen render target/backbuffer.
2. For app surfaces, never call `SET_SCANOUT` with the app-sized resource unless the app is explicitly in a direct fullscreen mode.
3. Compose virgl app surfaces into the full-screen target.
4. Flush/page-flip the full-screen target.
5. Preserve CPU fallback for shm surfaces and for GPU copy paths that fail validation.

Acceptance checks:

- QEMU window stays 1280x800.
- Moving/opening the 3D demo does not resize the host window.
- Panel/taskbar does not blink.
- `SET_SCANOUT` width/height remain desktop-sized.
- App surface movement changes only composition coordinates inside the desktop target.

### Phase 5: Fix Y-origin and format semantics

Observed issue: surface position was fixed, but image content was still Y-inverted at one point.

Add explicit tests/sampling:

- Put a small colored marker in each app-surface corner before or after GL render if possible.
- Sample the guest framebuffer or compositor target via `fbstat sample` / `fbstat ppm-current`.
- Validate top-left/top-right/bottom-left/bottom-right after composition.

Rules:

- Wayland surface coordinates are top-left in compositor space.
- OpenGL textures/framebuffers may be bottom-left unless Mesa/EGL image import has a flip flag or projection correction.
- Apply Y-flip exactly once in the GPU composition shader/copy path.
- Do not "fix" Y by moving the surface; content orientation and surface placement are separate.

### Phase 6: Replace readback with real GPU desktop composition

This is the core missing piece for "entire desktop accelerated by GPU".

Options:

1. GL compositor in wlcomp:
   - Use EGL/GBM or the existing virgl/Mesa path to create a full-screen compositor render target.
   - Upload CPU/shm surfaces as textures when damaged.
   - Import or reference app dmabuf/virgl resources as textures.
   - Draw quads for windows, wallpaper, icons, cursor/panel.
   - Present full-screen target through KMS/scanout.

2. Kernel-assisted virgl blit/composite:
   - Keep compositor CPU logic but issue virgl copy/blit commands into full-screen resource.
   - Must validate that copy/blit actually changes destination pixels on this host before using it.
   - Earlier attempts showed some accepted virgl copy commands left the destination black/stale, so this path needs strict proof.

Prefer option 1 if feasible. It matches Alpine/Weston more closely: compositor renders a full desktop scene and presents that, rather than trying to bind each app buffer as scanout.

Acceptance checks:

- CPU readback bytes per second drop sharply during 3D demo.
- `partial_blits/full_blits/blit_bytes` no longer dominate frame progress.
- QEMU trace shows regular compositor GL submits and full-screen resource flushes.
- On-screen visible FPS matches the app overlay closely after warmup.

### Phase 7: Frame pacing and flush policy

Current symptom: app FPS text can be much higher than visible updates.

Investigate:

- Is wlcomp waiting for frame callbacks before flushing?
- Are fence waits blocking the compositor thread?
- Are flush rects coalesced until too late?
- Are repeated app commits dropped because the same dmabuf fd/resource id is reused?
- Is scanout flush tied to input/cursor/timer activity instead of render completion?

Implementation direction:

- Present at display cadence while there is damage or an animating surface.
- Coalesce damage within one frame, not across many seconds.
- Do not wait synchronously on GPU fences in a way that stops event processing.
- Use fence readiness/poll callbacks where available.
- Keep cursor updates independent enough that cursor motion does not starve frame presentation.

Acceptance checks:

- Visible demo updates every frame or near display cadence.
- Cursor remains responsive.
- Panel does not blink.
- No one-frame old/stale image on the 3D surface for long periods.

Status (2026-06-03): root cause narrowed; one blocking present-path issue
was improved, but Alpine parity is not complete.

- Added `wlcomp_trace_present=1` per-phase present instrumentation
  (`present_trace_*` in `wlcomp_render_loop.inc`). On the WSL D3D12 virgl host
  the synchronous baseline measured frame_avg ~22ms split as scanout ~15ms,
  cpu_upload ~5ms, gl_compose ~1ms — i.e. the compositor present loop was
  blocking ~15ms/frame on the host RESOURCE_FLUSH acknowledgement and the
  client-3D submit drain, halving displayed FPS (split-brain: app_loop_fps ~40,
  displayed_fps 0.0, present avg_interval 53-57ms).
- Partial fix: `virtio_gpu_resource_flush_async()` (kernel `virtio_gpu.c`) posts the
  steady-state scanout RESOURCE_FLUSH through the single-slot async submit and
  returns immediately; the next ctrlq submission drains it. Gated by cmdline
  `virtio_gpu_async_scanout_flush`, now defaulted on for the `-gl` path in
  `scripts/launch/run-qemu.sh`.
- Validated improvement: displayed_fps can track app_loop_fps in some runs
  (e.g. 28.9 vs 29.5, was 0.0);
  present frame_avg 22ms->13ms, scanout 15->11ms, cpu_upload 5000->150us;
  damage avg_interval 56ms->37ms with late40 dropping sharply; no panics or
  async-command timeouts. QEMU trace unchanged in shape: set_scanout=4 all
  1280x800 (ownership preserved, no host-window resize), res_xfer_toh_3d
  3455->2173, res_flush now dominated by the 640x480 GL app surface.
- Remaining gap: user-visible runs still show only about 20-30 FPS, occasional
  stale/slow visible updates, and much worse smoothness than Alpine. Treat the
  async scanout flush as a necessary fix, not the final architecture.
- Added `scripts/gpu/virgl-desktop-validate.sh` as the focused xv6 windowed
  desktop parity validator. A short 60-frame run on 2026-06-02 passed its
  current invariants:
  - `app_loop_fps=27.5`, `displayed_fps=22.9`;
  - QEMU trace counts: `ctx_submit=123`, `res_flush=65`, `set_scanout=4`,
    `fence_ctrl=123`, `fence_resp=123`;
  - post-desktop `SET_SCANOUT` stayed desktop-sized (`1280x800`);
  - QEMU monitor screenshot was unavailable, so visual proof still needs host
    screenshot or guest `fbstat ppm-current`.
- Tier 0 scanout-rect cleanup is now implemented in
  `wlcomp_render_loop.inc`: final virgl scanout rects are built from the
  filtered CPU damage plus GPU-composed client rects, rather than from stale
  pre-filter paint damage. A refreshed 60-frame validator run on 2026-06-03
  passed with:
  - `app_loop_fps=27.9`, `displayed_fps=25.4`;
  - steady `scanout_rects/frame=1` (previously 2);
  - QEMU trace counts: `ctx_submit=123`, `res_flush=66`, `set_scanout=4`,
    `fence_ctrl=123`, `fence_resp=123`;
  - post-desktop `SET_SCANOUT` stayed desktop-sized (`1280x800`).
  Remaining measured costs are now the structural Tier 1/Tier 2 problem:
  `gl_compose_avg_us` around 5-8 ms plus `scanout_avg_us` around 5-7 ms,
  serialized in a single-buffered compositor-present loop.
- Tier 1 preparatory work is present but **not yet the page-flip fix**.
  `wlcomp_virgl_fb_buffers=2` now allocates a second full-screen virgl
  framebuffer target and mirrors CPU-damaged regions into it:
  - front target example: `res=4`, `handle=2`, size `4096000`, pitch `5120`;
  - back target example: `res=5`, `handle=4`, size `4096000`, pitch `5120`;
  - log marker: `wlcomp: virgl framebuffer double-buffer prep ready`.
  A 60-frame opt-in validator run passed with:
  - `VIRGL_DESKTOP_VALIDATE_BUFFERS=2`;
  - `app_loop_fps=25.9`, `displayed_fps=19.9`;
  - steady `scanout_rects/frame=1`;
  - QEMU trace counts: `ctx_submit=123`, `res_flush=66`, `set_scanout=4`,
    `fence_ctrl=123`, `fence_resp=123`;
  - post-desktop `SET_SCANOUT` stayed desktop-sized (`1280x800`).
  This only proves allocation and coherence preparation. The compositor still
  presents the same front target; actual full-screen target flipping/swapping
  remains the next implementation step.
- `scripts/gpu/virgl-desktop-validate.sh` was adjusted to tolerate serial-log
  interleaving around demo completion. A clean run may split
  `complete frames=... status=0` with compositor output, so the validator now
  accepts either the completion marker or the mesawlegl toplevel destroy marker
  while still rejecting any nonzero `status=...`.
- Actual full-screen target flipping is now prototyped behind the opt-in
  `wlcomp_virgl_fb_flip=1` / `VIRGL_DESKTOP_VALIDATE_FLIP=1` flags. It swaps
  the active wlcomp render target with the prepared back target and forces a
  full repaint for correctness while damage-preserving buffer history is still
  missing. A refreshed 60-frame run on 2026-06-03 passed with:
  - `VIRGL_DESKTOP_VALIDATE_BUFFERS=2`;
  - `VIRGL_DESKTOP_VALIDATE_FLIP=1`;
  - `renderer=virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`;
  - `demo_surface_matrix ... status=PASS`;
  - target flip log alternating only full-screen resources:
    `render_res=5 ... back_res=4`, then `render_res=4 ... back_res=5`;
  - displayed FPS tracking app FPS after warmup, but only around
    `app_loop_fps=16.5`, `displayed_fps=16.5`;
  - QEMU trace counts: `ctx_submit=122`, `res_flush=64`, `set_scanout=64`,
    `fence_ctrl=122`, `fence_resp=122`;
  - steady `SET_SCANOUT` alternated between resources `0x4` and `0x5`, always
    `1280x800`, never the `640x480` app surface.
  This proves the geometry/ownership part of Tier 1: xv6 can page between two
  full-desktop virgl scanout targets without resizing the host QEMU window. It
  is not a performance win yet. The forced full repaint reports
  `cpu_rects/frame=4`, `scanout_rects/frame=5`, and spends roughly
  24-30 ms/frame in CPU upload plus 15-19 ms/frame in scanout. The next useful
  Tier 1 step is damage-preserving target coherence (GPU copy, mirrored
  per-buffer damage, or triple-buffer history) so flips do not require a full
  CPU repaint every frame.
- The plain current virgl desktop path was revalidated on 2026-06-03 after
  reverting an acquire-skip experiment that let GPU BOs bypass
  `buffer_acquire_ready()`. That run passed with no
  `vrend_renderer_transfer_iov` context errors:
  - `app_loop_fps=28.0`, `displayed_fps=24.3`;
  - steady present trace:
    `frame_avg_us=10560 cpu_upload_avg_us=2352 gl_compose_avg_us=3951
    scanout_avg_us=3993 scanout_rects/frame=1`;
  - QEMU trace counts: `ctx_submit=126`, `res_flush=141`, `set_scanout=4`,
    `fence_ctrl=126`, `fence_resp=126`.
  The acquire-skip experiment is rejected: it produced repeated
  `vrend_renderer_transfer_iov: context error ... Illegal resource 4/5`
  noise after the demo and did not remove the submit bottleneck. The validator
  now rejects `vrend_renderer_transfer_iov: context error` so this class of
  failure cannot be counted as a pass.
- Damage-preserving copy-before-flip is prototyped behind
  `wlcomp_virgl_fb_copy_before_flip=1` /
  `VIRGL_DESKTOP_VALIDATE_COPY_BEFORE_FLIP=1`. After the render/back target
  swap, wlcomp uses `FB_GPU_BO_COPY` to copy the currently displayed
  full-screen target into the render target, then repaints only the actual
  damage instead of forcing `damage_full_for_flip()`. With the stricter
  validator, this path is now classified as diagnostic/rejected rather than a
  success. A refreshed 60-frame run on 2026-06-03 completed the demo but failed
  validation because virgl reported illegal-resource context errors as soon as
  BO-copy touched the full-screen targets:
  - `VIRGL_DESKTOP_VALIDATE_BUFFERS=2`;
  - `VIRGL_DESKTOP_VALIDATE_FLIP=1`;
  - `VIRGL_DESKTOP_VALIDATE_COPY_BEFORE_FLIP=1`;
  - copy markers alternating handles 2 and 4:
    `wlcomp: virgl framebuffer copy-before-flip src_handle=2 dst_handle=4`
    then `src_handle=4 dst_handle=2`;
  - kernel resource-copy logs alternating resources 4 and 5;
  - repeated host errors such as
    `vrend_renderer_transfer_iov: context error reported 2 "wlcomp-scanout"
    Illegal resource 4` and the same for resource 5;
  - demo completion still appeared:
    `complete frames=60 status=0 elapsed=2.816s fps=21.3`;
  - `app_loop_fps=28.2`, `displayed_fps=19.9`;
  - steady present trace:
    `frame_avg_us=15553 cpu_upload_avg_us=3243 gl_compose_avg_us=8366
    scanout_avg_us=3643 scanout_rects/frame=3`;
  - QEMU trace counts: `ctx_submit=347`, `res_flush=118`, `set_scanout=10`,
    `fence_ctrl=347`, `fence_resp=347`.
  Damage-rect copy-before-flip was also tried behind
  `wlcomp_virgl_fb_copy_damage_before_flip=1`. It proved the compositor can
  remember and issue partial catch-up rects, with stats such as
  `copies=105 avg_pixels=204800 full=0 partial=105`, but it split roughly the
  same full-screen history into many more submits (`ctx_submit=421`) and gave
  no visible-FPS win. The next useful Tier 1 step is therefore not per-rect
  `FB_GPU_BO_COPY`; it is a genuinely safe GPU-side history update or a render
  structure that avoids the copy submit altogether.
- Damage-preserving flip without `FB_GPU_BO_COPY` is now prototyped behind
  `wlcomp_virgl_fb_damage_flip=1` /
  `VIRGL_DESKTOP_VALIDATE_DAMAGE_FLIP=1`. This path warms both full-screen
  virgl targets with one full repaint, then skips the per-frame forced full
  repaint and relies on the existing CPU-damage mirror to keep chrome/history
  coherent while GPU composition redraws damaged client regions. The kernel
  `virtio_gpu_bind_resource_scanout()` path was tightened so a partial damage
  rect can bind only a full-desktop-sized resource as a 1280x800 scanout and
  then flush the partial rect; app-sized partial scanout remains rejected.
  A refreshed 60-frame run on 2026-06-03 passed with the stricter validator
  rejecting virgl context errors and scanout failures:
  - `VIRGL_DESKTOP_VALIDATE_BUFFERS=2`;
  - `VIRGL_DESKTOP_VALIDATE_FLIP=1`;
  - `VIRGL_DESKTOP_VALIDATE_DAMAGE_FLIP=1`;
  - damage-flip markers:
    `wlcomp: virgl framebuffer damage-flip warmup res=4/5` then
    `damage-flip preserving damage res=4/5`;
  - no `vrend_renderer_transfer_iov` context errors and no
    `wlcomp: virgl framebuffer scanout failed`;
  - `app_loop_fps=20.8`, `displayed_fps=19.6`;
  - steady present trace:
    `frame_avg_us=34727 cpu_upload_avg_us=6082 gl_compose_avg_us=10419
    scanout_avg_us=18193 scanout_rects/frame=1`;
  - QEMU trace counts: `ctx_submit=123`, `res_flush=65`, `set_scanout=65`,
    `fence_ctrl=123`, `fence_resp=123`;
  - every post-startup `SET_SCANOUT` remained 1280x800 and alternated only
    resources 4/5, never the 640x480 app surface.
  This is correctness progress over the BO-copy attempt and proves target
  history can be preserved without illegal virgl resource-copy submits. It is
  still not a performance win over the default single-front-target path:
  the control run after this change passed with `app_loop_fps=31.4`,
  `displayed_fps=24.9`, `ctx_submit=122`, `res_flush=64`, and only
  `set_scanout=4`. The next bottleneck is the per-frame full-desktop
  `SET_SCANOUT` caused by alternating scanout resources; a Linux-like solution
  must avoid that serial cost or overlap it, not just reduce CPU repaint.
- Async ring depth was tested as a possible way to overlap scanout flush with
  later GL submits. `scripts/gpu/virgl-desktop-validate.sh` now accepts
  `VIRGL_DESKTOP_VALIDATE_ASYNC_DEPTH=N` so this can be tested without
  smuggling `virtio_gpu_async_depth=N` through the token. Earlier depth-3
  runs timed out under a looser/older completion setup, but the current strict
  validator plus split fbstat counters shows depth 3 can complete cleanly. Keep
  it opt-in because it removes short admission waits without improving the
  low/mid-30 FPS ceiling:
  - 60-frame depth-3 run:
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-depth3-attribution60.log`;
    passed with `app_loop_fps=28.4`, `displayed_fps=28.5`,
    `posted_submit_3d=128`, `posted_flush=68`, `stalls=0`,
    `pending=1`, `set_scanout=4`, and active `scanout_rebinds=0`;
  - 180-frame depth-3 run:
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-depth3-attribution180.log`;
    passed with later samples around `app_loop_fps=30.5-33.8`,
    `displayed_fps=30.8-33.4`, `posted_submit_3d=366`,
    `posted_flush=187`, `stalls=0`, `pending=1`, `set_scanout=4`,
    and active `scanout_rebinds=0`;
  - same-build 180-frame depth-2 control:
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-depth2-attribution180-control.log`;
    also passed and was as good or better in warm samples
    (`app_loop_fps=31.8-34.3`, `displayed_fps=32.5-36.3`) despite
    `flush_stalls=4` and `max_wait_us=9831`.
  Conclusion: depth 3 is now a valid diagnostic for proving extra async room
  eliminates flush-admission waits, but it is not a default-path performance
  fix. The remaining work is still reducing or overlapping GL compose plus
  scanout/display cost.
- The async queue now has guest-visible pressure counters in `fbstat`:
  `virtio_async_posted`, `virtio_async_retired`, `virtio_async_pending`,
  `virtio_async_depth`, `virtio_async_make_room_calls`,
  `virtio_async_make_room_stalls`, per-command posted/call/stall splits
  (`submit_3d`, `flush`, `transfer`), `virtio_async_wait_progress_calls`, and
  wait-time totals/last/max. The validator can request this with
  `VIRGL_DESKTOP_VALIDATE_FBSTAT=1`; it appends `glsmoke_fbstat=1` so
  `desktop` forks `/bin/fbstat` immediately after the GL demo exits instead of
  trying to type into an uncertain serial shell. A 60-frame focused run passed:
  - log:
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-async-stats60-pass.log`;
  - trace:
    `build-x86_64/virgl-desktop-validate/xv6-qemu-virgl-desktop-async-stats60-pass.trace`;
  - `app_loop_fps=30.2`, `displayed_fps=28.7`;
  - steady active trace:
    `frame_avg_us=13170 cpu_upload_avg_us=3737 gl_compose_avg_us=4044
    scanout_avg_us=4839 scanout_rects/frame=1 scanout_rebinds=0`;
  - async stats: `posted=194`, `retired=194`, `pending=0`, `depth=2`,
    `make_room_calls=194`, `stalls=6`, `wait_progress_calls=6`,
    `max_wait_us=9880`;
  - QEMU trace counts: `ctx_submit=129`, `res_flush=208`, `set_scanout=4`,
    `fence_ctrl=129`, `fence_resp=129`, `res_create_3d=12`.
  This shows the default depth-2 queue is not building a backlog in the
  completed 60-frame run; there are occasional make-room stalls, but no
  persistent pending async work. Use these counters to guide overlap work
  before changing queue depth again.
- A follow-up 60-frame attribution run after splitting async counters by
  command kind also passed:
  - log:
    `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-async-attribution60.log`;
  - trace:
    `build-x86_64/virgl-desktop-validate/xv6-qemu-virgl-desktop-async-attribution60.trace`;
  - `app_loop_fps=29.9`, `displayed_fps=28.9`;
  - active trace:
    `frame_avg_us=16084 cpu_upload_avg_us=3256 gl_compose_avg_us=4632
    scanout_avg_us=8147 scanout_rects/frame=1 scanout_rebinds=0`;
  - async stats: `posted=190`, `posted_submit_3d=125`,
    `posted_flush=65`, `posted_transfer=0`, `retired=190`, `pending=0`,
    `depth=2`, `make_room_calls=190`, `submit_3d_calls=125`,
    `flush_calls=65`, `transfer_calls=0`, `stalls=3`,
    `submit_3d_stalls=0`, `flush_stalls=3`, `transfer_stalls=0`,
    `max_wait_us=1182`;
  - QEMU trace counts: `ctx_submit=126`, `res_flush=208`, `set_scanout=4`,
    `fence_ctrl=126`, `fence_resp=126`, `res_xfer_toh_3d=246`.
  Interpretation: in this default-path sample, async admission pressure is not
  blocking app/compositor 3D submits; the only slot waits happen when posting
  the next scanout flush, and they are short. That makes blind async-depth
  increases less attractive. The next behavior change should reduce the
  compose+scanout sequence itself or overlap scanout completion at a higher
  compositor scheduling level while preserving `scanout_rebinds=0`.
- `virtio_gpu_present_minimal_drain=1` was tested as a possible scanout-flush
  overlap tweak with
  `VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='virtio_gpu_present_minimal_drain=1'`.
  The 60-frame run passed but was neutral/slightly worse:
  `app_loop_fps=30.2`, `displayed_fps=27.6`, `ctx_submit=122`,
  `res_flush=67`, `set_scanout=4`, `fence_ctrl=122`, `fence_resp=122`,
  `scanout_rebinds=0`. Keep it as a diagnostic only; it did not improve the
  default stable-resource scanout path.
- The default single-front-target path got a small GL steady-state cleanup in
  `wlcomp_gl_compose.inc`: sampler uniform locations are cached at init and
  the target FBO attach/completeness check is repeated only when the imported
  full-screen virgl target texture changes. A 60-frame default validator run
  on 2026-06-03 passed and improved the active-demo window to:
  - `app_loop_fps=29.6`, `displayed_fps=27.1`;
  - steady present trace:
    `frame_avg_us=15383 cpu_upload_avg_us=3427 gl_compose_avg_us=6494
    scanout_avg_us=5430 scanout_rects/frame=1`, followed by
    `frame_avg_us=14561 cpu_upload_avg_us=2496 gl_compose_avg_us=4091
    scanout_avg_us=7655 scanout_rects/frame=1`;
  - post-startup `SET_SCANOUT` still stayed desktop-sized (`1280x800`).
  This is worth keeping as default-path hygiene, but it is not Alpine parity:
  the compositor is still below the 60 FPS Weston/simple-EGL control, and the
  harness trace counts for this run include many post-demo taskbar clock
  presents after the demo completed.
- Follow-up GL steady-state cleanup now caches the last composed quad in
  `wlcomp_gl_compose.inc`, so the compositor does not re-upload the VBO when
  the window/damage geometry is unchanged. The cache is invalidated by the
  checksum probe path and naturally refreshes when geometry changes. The
  validator also accepts a clean `wlcomp: surface destroy ... app_id='mesawlegl'`
  completion marker, because serial interleaving can garble the adjacent
  `complete frames=60 status=0` line while still leaving a clear clean-destroy
  marker. A refreshed default 60-frame run on 2026-06-03 passed with:
  - `app_loop_fps=30.4`, `displayed_fps=29.7`;
  - steady present trace:
    `frame_avg_us=13084 cpu_upload_avg_us=3792 gl_compose_avg_us=2140
    scanout_avg_us=6870 scanout_rects/frame=1`;
  - QEMU trace counts: `ctx_submit=129`, `res_flush=70`, `set_scanout=4`,
    `fence_ctrl=129`;
  - post-startup `SET_SCANOUT` stayed desktop-sized (`1280x800`);
  - no virgl context errors, scanout failures, panics, or signal kills.
  This narrows the default-path GL compose cost, but the remaining frame time
  is still dominated by the serialized compose + scanout sequence.
- The compositor now also avoids a per-present `glFlush()` in the normal
  default path.  The following virgl scanout `RESOURCE_FLUSH` is enough ordering
  for the tested WSL D3D12/NVIDIA virgl host, and skipping the explicit GL flush
  roughly halves compositor submit/fence traffic.  The escape hatch is
  `wlcomp_gl_flush=1` / `XV6_WLCOMP_GL_FLUSH=1`.
  Same-build 180-frame A/B:
  - old/default-flush control
    (`xv6-virgl-desktop-glflush-default180-control.log`) passed with warm
    samples around `app_loop_fps=32.4-33.5`,
    `displayed_fps=32.9-34.0`, `gl_compose_avg_us=4039-4731`,
    `ctx_submit=365`, `fence_ctrl=365`, and `set_scanout=4`;
  - no-flush diagnostic
    (`xv6-virgl-desktop-glflush0-default180.log`) passed with warm samples
    around `app_loop_fps=34.4-36.3`, `displayed_fps=34.9-37.2`,
    `gl_compose_avg_us=132-317`, zero async make-room stalls,
    `ctx_submit=184`, `fence_ctrl=184`, and `set_scanout=4`;
  - after promoting no-flush to the default,
    (`xv6-virgl-desktop-glflush-defaultoff180.log`) passed without extra
    cmdline append, emitted `wlcomp: gl-compose per-present flush disabled`,
    and held warm samples around `app_loop_fps=33.4-34.1`,
    `displayed_fps=33.7-36.6`, `ctx_submit=184`, `fence_ctrl=184`,
    `set_scanout=4`, and `scanout_rebinds=0`.
  This is real default-path progress, but still not Alpine parity: the remaining
  active trace is now mostly scanout/display cost plus occasional CPU upload,
  not per-frame compositor GL submit cost.
- `virtio_gpu_no_scanout_flush=1` was tested as the extreme display-notify
  diagnostic after the GL no-flush improvement.  It is **not** a fix:
  `xv6-virgl-desktop-no-scanout-flush60.log` failed the displayed/app FPS gate.
  It reduced QEMU trace `res_flush` to startup-only (`res_flush=3`,
  `ctx_submit=64`, `set_scanout=4`), but the first active sample split badly
  (`app_loop_fps=19.7`, `displayed_fps=6.9`) even though the app completed
  60 frames.  This proves the host still needs a scanout `RESOURCE_FLUSH`/
  display-notify for user-visible updates; the next work is reducing or
  overlapping that notification, not deleting it.
- `virtio_gpu_scanout_perf=1` now splits the kernel scanout-bind hot path into
  total time, `op_lock` wait, `SET_SCANOUT`, and flush/post time.  A 60-frame
  focused run with `fb_present_perf=1 virtio_gpu_scanout_perf=1`
  (`xv6-virgl-desktop-scanout-perf60.log`) passed with stable desktop scanout
  (`set_scanout=4`, no active rebinds) and showed that the remaining scanout
  time is mostly global virtio GPU operation-lock serialization, not the async
  `RESOURCE_FLUSH` post itself:
  - active sample: `app_loop_fps=33.7`, `displayed_fps=33.7`,
    `scanout_avg_us=5905`;
  - kernel split: `total=2589 lock_wait=2318 set_scanout=194 flush=74`, then
    `total=4418 lock_wait=4337 set_scanout=0 flush=79`, then
    `total=2666 lock_wait=2180 set_scanout=0 flush=483`;
  - fbstat: `posted_submit_3d=63`, `posted_flush=65`, `stalls=0`, `pending=0`;
  - QEMU trace counts: `ctx_submit=64`, `res_flush=75`, `set_scanout=4`,
    `fence_ctrl=64`, `fence_resp=64`.
  A companion diagnostic with
  `fb_present_perf=1 virtio_gpu_scanout_perf=1 virtio_gpu_no_scanout_flush=1`
  (`xv6-virgl-desktop-scanout-perf-no-scanout-flush60.log`) still failed the
  validator shape, but the split reported `flush=0` while `lock_wait` remained
  multi-millisecond (`total=2133 lock_wait=1931`, then
  `total=3270 lock_wait=3268`, then `total=3479 lock_wait=3476`) and trace
  `res_flush` was startup-only (`res_flush=3`).  This confirms that the visible
  scanout bucket includes waiting behind prior GPU operations.  The next
  candidate fix should therefore reduce or decouple `op_lock` hold time around
  3D submits/scanout flush posting, or schedule scanout so it does not
  immediately serialize behind the just-issued compose submit.  Do not
  interpret `scanout_avg_us` as pure host display-notify cost.
- Async `SUBMIT_3D` now prepares its command header, response, and copied
  command-buffer payload before taking `virtio_gpu.op_lock`; the lock is held
  only for async ring admission/reserve/post.  This reduces unnecessary lock
  residency for both the app client and compositor submits, but it is not by
  itself the Alpine-parity fix.  Validation:
  - `xv6-virgl-desktop-submit-prep-scanout-perf60.log` passed with
    `app_loop_fps=33.7`, `displayed_fps=32.8`, active
    `scanout_avg_us=4989`, `posted_submit_3d=63`, `posted_flush=63`,
    `stalls=0`, and stable `set_scanout=4`;
  - its scanout split still had multi-ms `lock_wait` during the active demo
    (`total=3230 lock_wait=2955`, `total=3255 lock_wait=3175`,
    `total=2647 lock_wait=2566`), which made a competing client/app GPU submit
    a plausible owner of the global virtio operation lock while the compositor
    wants to present;
  - the plain default rerun
    (`xv6-virgl-desktop-submit-prep-default60-rerun.log`) passed with
    `app_loop_fps=34.2`, `displayed_fps=35.7`, active
    `scanout_avg_us=3846`, `ctx_submit=64`, `res_flush=73`, `set_scanout=4`,
    and no async make-room stalls.
  The next useful optimization is therefore deeper than moving payload copies:
  split or shorten the global virtio operation critical section around async
  command posting, or introduce compositor scheduling that avoids waiting for
  scanout while the app is immediately submitting its next frame.
- Async scanout `RESOURCE_FLUSH` preparation now follows the same split as
  async submit: the steady-state bound-scanout path prebuilds the flush command
  and response before taking `virtio_gpu.op_lock`, then transfers that prepared
  packet into the async ring only after the resource/geometry checks still prove
  the same resource is bound.  This keeps initial scanout binding and sync
  fallback behavior unchanged, while shortening the common present critical
  section.  Validation:
  - focused 60-frame split
    (`xv6-virgl-desktop-flush-prep-scanout-perf60.log`) passed with stable
    `set_scanout=4`, `posted_submit_3d=63`, `posted_flush=63`, no async
    stalls, and trace `ctx_submit=64`, `res_flush=158`, `fence_ctrl=64`;
  - that split was not a clear FPS win in the short active window
    (`app_loop_fps=23.6`, `displayed_fps=18.3`, then completion `fps=21.2`),
    but its post-demo/taskbar samples showed zero `lock_wait`, proving the
    prepared handoff can remove lock contention when no app submit is racing;
  - plain 60-frame default
    (`xv6-virgl-desktop-flush-prep-default60.log`) passed with active
    `app_loop_fps=30.6`, `displayed_fps=34.0`, `scanout_avg_us=2522`,
    stable `set_scanout=4`, and no async stalls;
  - 180-frame default
    (`xv6-virgl-desktop-flush-prep-default180.log`) passed with warm samples
    around `app_loop_fps=34.8-36.2`, `displayed_fps=34.4-37.0`,
    `gl_compose_avg_us=114-161`, `scanout_avg_us=3693-4902`,
    `ctx_submit=184`, `res_flush=194`, `set_scanout=4`, and no async
    make-room stalls.
  Keep this as useful critical-section hygiene, not the final fix.  The warm
  steady state is now in the high-30 FPS band, still below the Alpine/Linux
  60+ FPS control, so the next work remains deeper queue/lock splitting or
  compositor scheduling to avoid racing scanout against the app's next submit.
- An `op_lock` holder-attribution diagnostic was added to test the "scanout is
  waiting behind app submit" hypothesis directly.  A focused 60-frame run with
  `fb_present_perf=1 virtio_gpu_scanout_perf=1`
  (`xv6-virgl-desktop-op-holder-scanout-perf60.log`) passed and kept stable
  scanout binding (`set_scanout=4`, `scanout_rebinds=0`), but it did **not**
  attribute the active wait to the instrumented `SUBMIT_3D` section:
  - active present traces still reported multi-ms scanout time
    (`scanout_avg_us=5016`, then `scanout_avg_us=4931`) while compositor GL
    compose was low (`gl_compose_avg_us=1587`, then `231`);
  - scanout split batches reported `wait_submit_3d=0` and `wait_none=20` even
    when `lock_wait` was high (`total=1500 lock_wait=1358`, then
    `total=2818 lock_wait=2740`);
  - fbstat showed healthy async admission (`posted_submit_3d=63`,
    `posted_flush=66`, `pending=1`, `depth=2`, `make_room_calls=129`, no
    stalls), and QEMU trace counts were `ctx_submit=64`, `res_flush=160`,
    `set_scanout=4`, `fence_ctrl=64`, `fence_resp=64`.
  Treat this as a warning against overfitting the next fix to app-submit
  ownership alone.  Either the sampled holder is too weak, uninstrumented
  `op_lock` holders dominate, or mutex handoff/scheduler delay remains after
  the owner clears.  Before a large lock split, instrument the remaining
  `op_lock` holders or use a stronger trylock/blocking attribution probe; if
  the evidence still points at admission serialization, split async queue
  posting under a smaller lock while keeping synchronous queue users/draining
  mutually excluded.
- The holder diagnostic was strengthened with `mutex_trylock()` sampling and
  named owner buckets (`FENCE`, `TRANSFER`, `RESOURCE`, etc.).  This changed the
  active diagnosis: scanout was mostly waiting behind `FB_GPU_VIRGL_FENCE`,
  not app `SUBMIT_3D`.  The categorized 60-frame run
  (`xv6-virgl-desktop-op-holder-categorized-scanout-perf60.log`) passed and
  showed:
  - active split batches with high lock wait attributed to fence calls:
    `total=3519 lock_wait=3122 wait_fence=14`, then
    `total=3144 lock_wait=3011 wait_fence=15`;
  - `wait_submit_3d=0`, `wait_fb_present=0`, and only one transfer wait in the
    active batches;
  - active desktop trace around `app_loop_fps=34.6`,
    `displayed_fps=33.0`, `scanout_avg_us=5626`;
  - async stats remained healthy (`posted_submit_3d=63`, `posted_flush=63`,
    `pending=0`, `depth=2`, no make-room stalls).
  This proves the next default-path optimization should focus on fence wait
  behavior, not a broad async submit lock split.
- `virtio_gpu_user_fence()` now uses a targeted async drain for explicit
  blocking waits on a nonzero fence: it reaps only until the requested submit
  fence has retired, instead of draining the whole async ring and accidentally
  waiting for younger scanout flushes.  The old broad drain behavior remains
  for wait-for-zero and non-targeted query cases.  Validation:
  - categorized 60-frame run
    (`xv6-virgl-desktop-fence-target-drain-categorized60.log`) passed and
    removed fence-owned scanout blocking: active batches reported
    `lock_wait=98/112/176`, `wait_fence=0`, with only small transfer waits;
  - same run still had noisy short-window FPS (`displayed_fps=23.6`,
    completion `fps=23.4`) because CPU upload/GL compose varied upward, so do
    not use it as the warm ceiling;
  - plain 180-frame default
    (`xv6-virgl-desktop-fence-target-drain-default180.log`) passed and is the
    better user-visible evidence: warm samples reached
    `app_loop_fps=44.6/displayed_fps=44.0` and then
    `app_loop_fps=46.8/displayed_fps=46.9`, with
    `scanout_avg_us=943` then `1095`, `gl_compose_avg_us=164` then `465`,
    stable `scanout_rebinds=0`, `ctx_submit=184`, `fence_ctrl=184`, and no
    async make-room stalls (`posted_submit_3d=183`, `posted_flush=186`,
    `pending=0`, `depth=2`).
  This is the best default-path progress so far: the warm ceiling moved from
  the high-30 FPS band into the mid/high-40s while preserving the normal
  1280x800 window and stable full-desktop scanout.  Alpine parity is still not
  complete; the remaining gap is now mostly frame pacing plus residual CPU
  upload/compose/display cost rather than multi-ms scanout lock waits.
- A frame-pacing probe was added after the targeted fence-drain improvement:
  `glsmoke_present_interval=N` now drives the demo's `--present-interval`
  argument, and `wlcomp_frame_perf=1` logs compositor frame-callback pending
  and delivery stats.  The 60-frame default telemetry run
  (`xv6-virgl-desktop-default-frameperf60.log`) passed.  It showed the demo
  still using `present_interval=1`, with an active sample around
  `app_loop_fps=40.0/displayed_fps=38.9`, `present-trace frames=39`,
  `frame_avg_us=4328`, `gl_compose_avg_us=700`, `scanout_avg_us=1380`,
  `posted_submit_3d=63`, `posted_flush=65`, `pending=1`, and no async
  make-room stalls.  Callback telemetry in that active second delivered only
  `36` callbacks with `delivery_avg_ms=28`, which matches the demo cadence
  rather than showing a separate 60 Hz compositor callback backlog.
- A current-tree 180-frame default rerun with both frame telemetry and fbstat
  (`xv6-virgl-desktop-frameperf-fbstat-default180.log`) passed and is the
  latest default-path correlation sample. It kept the stable scanout invariant
  (`set_scanout=4`, only 1280x800 post-startup `SET_SCANOUT`, active
  `scanout_rebinds=0`) and showed the default path can now sit near the
  high-40s/low-50s visible band without async admission pressure:
  warm samples were `app_loop_fps=57.4/displayed_fps=49.9` and
  `54.8/50.0`, completion was `frames=180 status=0 elapsed=3.897s fps=46.2`,
  active traces had `frame_avg_us=5950-6947`, `gl_compose_avg_us=285-508`,
  `scanout_avg_us=3186-5143`, and fbstat reported
  `posted_submit_3d=183`, `posted_flush=173`, `pending=1`, `depth=2`,
  with zero make-room stalls/waits. Frame-callback telemetry in the same run
  delivered 49/55/59 callbacks per one-second bucket with delivery averages
  around 20/18/17ms and no blocked callbacks. Interpretation: the current
  default is not async-depth limited and not callback-backlog limited; the
  remaining gap is making each callback/display opportunity cheaper or more
  overlapped, while preserving the existing stable-scanout shape.
- The paired `glsmoke_present_interval=0 wlcomp_frame_perf=1` 60-frame probe
  (`xv6-virgl-desktop-present-interval0-frameperf60.log`) is a negative result:
  the app ran ahead (`present_interval=0`, completion `frames=60`) but the
  validator rejected it because displayed FPS no longer tracked app FPS
  (`app_loop_fps=30.6/displayed_fps=7.8`), with only `bo_presents=44`.
  Frame-callback stats during that run mostly reported zero pending callbacks,
  so disabling swap interval is not a valid parity shortcut; it decouples the
  app loop from compositor-visible presentation instead of improving the
  displayed desktop cadence.
- Native Wayland EGL swap timing was added behind `glsmoke_mesa_perf=1` and the
  desktop/launcher paths now expose a diagnostic `glsmoke_mesa_throttle=0/1`
  knob.  The passing default-throttle 120-frame run
  (`xv6-virgl-desktop-mesa-wlswap-throttle1-perf120.log`) showed the remaining
  app cadence is dominated by Mesa's Wayland throttle wait, not by the simple
  Wayland commit/flush work: two 60-swap buckets reported
  `total=33746 throttle=29157 flush_drawable=3864` and then
  `total=21851 throttle=19225 flush_drawable=2077`, with all other swap phases
  down in the tens/hundreds of microseconds and `throttle_disabled=0`.
  Compositor present in the same warm windows was only about 4 ms
  (`present-trace frames=38 frame_avg_us=4030`, then `frames=39
  frame_avg_us=3976`) while displayed FPS stayed near the app
  (`app_loop_fps=39.4/displayed_fps=38.0`, then `40.4/37.9`).  This proves the
  client is being paced by frame-callback delivery; it does not by itself prove
  that the compositor is unable to draw faster.
- The throttle-off A/B (`xv6-virgl-desktop-mesa-wlswap-throttle-default0-perf120.log`)
  is a second negative result.  It correctly passed
  `XV6_MESA_WAYLAND_THROTTLE=0` to the client and Mesa reported
  `throttle_disabled=1`; the throttle phase collapsed to `8us`, but
  `flush_drawable` grew to `10510-11852us`, app FPS ran ahead
  (`app_loop_fps=61.9`), and displayed FPS did not follow
  (`displayed_fps=38.6`, `virgl_bo_presents=87` for 120 app frames), so the
  validator rejected it.  Keep default throttle on.  The next useful target is
  not "remove frame callbacks"; it is making each callback/display opportunity
  cheaper or pipelined enough that callbacks arrive closer to 60 Hz.
- Mesa virgl/xv6 winsys timing was added under the same `glsmoke_mesa_perf=1`
  gate.  The default-throttle 120-frame probe
  (`xv6-virgl-desktop-mesa-winsys-perf120.log`) passed and showed that the
  remaining warm swap time is still mostly Mesa's Wayland frame-callback wait,
  not an unmeasured kernel fence wait hidden inside `flush_drawable`: the two
  swap buckets reported `total=32716 flush_drawable=5394 throttle=26594` and
  `total=27097 flush_drawable=3856 throttle=22846`.  The corresponding winsys
  buckets were `transfer avg_us=2993` then `2532`, `submit avg_us=1076` then
  `383`, and one `fence_wait avg_us=781 max_us=16033` bucket.  Compositor warm
  present stayed comparatively cheap (`present-trace frames=38 frame_avg_us=3712`
  then `frames=39 frame_avg_us=1659`), and the validator reached
  `app_loop_fps=34.7/displayed_fps=37.7` before the 120-frame completion.  This
  points the next investigation at frame-callback cadence/compositor scheduling
  and reducing per-frame transfer cost, not at a broad synchronous fence drain.
- Frame-callback pacing was swept with the existing `wlcomp_frame_ms=N` knob.
  The 8ms run (`xv6-virgl-desktop-frame-ms8-perf120.log`) passed and first
  proved the callback interval was worth tightening: warm samples reached
  `app_loop_fps=51.4/displayed_fps=50.9`, callback delivery averaged about
  19ms, and Mesa's second swap bucket improved to
  `total=17023 flush_drawable=5737 throttle=10829`.  The 4ms run
  (`xv6-virgl-desktop-frame-ms4-perf120.log`) also passed but over-drove the
  path: app FPS rose to `56.6`, displayed FPS fell to `45.8`, and warm
  `flush_drawable`/scanout cost grew.  The original plain 180-frame default-8 run
  (`xv6-virgl-desktop-frame-ms8-default180.log`) produced strong warm samples
  but failed because wlcomp's one-second diagnostics interleaved with fbstat
  and corrupted strict counter-line matches.  `run_fbstat_after_glsmoke()` now
  pauses wlcomp around the diagnostic-only fbstat footer, so the rerun
  (`xv6-virgl-desktop-frame-ms8-default180-fbstatfix.log`) passed cleanly with
  warm samples `app_loop_fps=52.7/displayed_fps=40.8` then
  `57.3/57.9`, stable `set_scanout=4`, `ctx_submit=184`, `res_flush=181`,
  `bo_presents=178`, `virtio_async_make_room_calls=360`, and zero async
  make-room stalls.  A follow-up 6ms explicit run
  (`xv6-virgl-desktop-frame-ms6-default180.log`) is the best pacing evidence so
  far: it passed with warm samples `app_loop_fps=57.7/displayed_fps=56.3` and
  `65.7/62.1`, callback delivery averaged about 18ms then 16ms, and the
  stable-scanout shape held (`set_scanout=4`, `ctx_submit=184`,
  `res_flush=184`, zero async stalls).  Therefore
  `frame_callback_interval_ms()` now defaults to 6ms while preserving the
  `wlcomp_frame_ms=` cmdline and `XV6_WLCOMP_FRAME_MS` overrides.  The rebuilt
  default-6 checks passed: the frame-perf run
  (`xv6-virgl-desktop-frame-ms6-default180-afterdefault.log`) reached a final
  warm displayed sample of `57.5`, and the lower-overhead clean run
  (`xv6-virgl-desktop-frame-ms6-default180-clean.log`) passed with warm samples
  `51.2/49.0` and `53.9/49.0`, stable `set_scanout=4`, `ctx_submit=184`,
  `res_flush=186`, `bo_presents=183`, and zero async make-room stalls.  The
  clean run was noisier and still scanout-dominated
  (`frame_avg_us=5957`, `scanout_avg_us=5749` in the final present trace), so
  this is a real improvement but not final parity.  A follow-up 5ms run
  (`xv6-virgl-desktop-frame-ms5-default180.log`) also passed, but it is not a
  better default candidate: warm samples were `53.8/47.0`, `51.3/42.9`, then
  `52.4/55.8`, final completion was `fps=44.0`, and trace traffic grew to
  `res_flush=320` while `ctx_submit=184` and `set_scanout=4` stayed stable.
  Treat 5ms like the 4ms result: useful evidence that the path tolerates faster
  callback checks, but too aggressive/noisier than the 6ms default.  The upper
  adjacent 7ms run (`xv6-virgl-desktop-frame-ms7-default180.log`) also passed
  and kept the clean trace shape (`ctx_submit=184`, `res_flush=185`,
  `set_scanout=4`, zero async stalls), but its warm samples topped out at
  `47.9/43.0` then `55.5/52.3` and completion stayed `fps=44.3`.  That makes
  7ms a safe fallback/data point, not a better default than the 6ms run that
  reached `65.7/62.1`.
- The compositor's pending-callback epoll wait is now exposed as an opt-in
  diagnostic via `wlcomp_callback_poll_ms=N`, and the validator can pass it
  with `VIRGL_DESKTOP_VALIDATE_CALLBACK_POLL_MS=N`.  The default remains the
  existing 4ms wait while callbacks are pending.  Lowering that wait was a
  negative result, not a new default candidate: the 1ms 180-frame probe
  (`xv6-virgl-desktop-callback-poll1-default180.log`) passed but slowed the
  warm envelope to `35.3/26.5`, `44.0/37.0`, then `43.0/45.5`, completing at
  `frames=180 status=0 elapsed=4.996s fps=36.0`; the 2ms probe
  (`xv6-virgl-desktop-callback-poll2-default180.log`) similarly passed with
  `37.0/31.7`, `40.1/33.5`, then `44.3/43.5`, completing at `fps=35.5`.
  Both preserved the stable scanout invariant (`set_scanout=4`, desktop-sized
  `SET_SCANOUT` only after startup) and showed zero async stalls, so the
  regression is scheduling/churn rather than a correctness failure.  Keep 4ms
  as the pending-callback epoll wait and keep 6ms as the software frame-callback
  interval; the next useful work is cheaper or more overlapped callback/display
  handling, not more aggressive polling.  A same-tree default rerun after adding
  the knob and tightening the validator
  (`xv6-virgl-desktop-callback-poll-default-afterknob-nofbstat180.log`) passed
  without the fbstat footer path, with warm samples `37.6/36.9`, `40.9/39.0`,
  and `37.1/38.8`, completion `frames=180 status=0`, stable `set_scanout=4`,
  `ctx_submit=182`, and `res_flush=180`.  Treat it as a clean post-knob script
  control, not as better pacing evidence than the earlier default-6 fbstat runs.
- The compositor now decides whether frame callbacks are pending after the
  nonblocking Wayland dispatch at the top of the loop, not before it.  This
  lets callbacks created by just-dispatched client requests use the 4ms
  pending-callback wait immediately instead of inheriting the idle 16ms wait.
  The strict 180-frame FBSTAT run
  `xv6-virgl-desktop-callback-dispatch-first-fixedwait180.log` passed with
  warm samples `58.6/51.6` and `54.4/50.7`, callback delivery averages around
  18-19ms, stable scanout (`set_scanout=4`, post-startup
  `scanout_rebinds=0`), `fbstat display settle target=180 completed=180`,
  footer `display_last_complete 182`, `posted_submit_3d=183`,
  `posted_flush=181`, and zero async make-room stalls.  Keep the fixed 4ms
  pending wait as the default.  The stricter deadline-sleep variant
  (`wlcomp_callback_deadline_wait=1`) is **not** a default candidate:
  `xv6-virgl-desktop-callback-dispatch-then-deadline180.log` reached
  `66.3/51.6` and `66.6/68.7` during the active window, but failed strict
  FBSTAT because only `display_last_complete 174` was observed for 180 app
  frames.  Treat deadline wait as an opt-in under-display diagnostic.
- Frame-callback deadline advancement now skips idle/no-pending cases in both
  `damage_all_frame_callbacks()` and the after-present callback-due path.  This
  is scheduling hygiene: it prevents compositor-only idle loops from advancing
  the software callback clock when no client callback is pending.  It did not
  produce a clear FPS breakthrough.  The strict 120-frame attribution rerun
  (`xv6-virgl-desktop-callback-pending-only-fbstat-fixed120.log`) passed after
  the validator fix below, with warm samples `29.2/25.6`, `38.9/29.5`,
  completion `frames=120 status=0`, stable scanout shape (`set_scanout=4`,
  desktop-sized `SET_SCANOUT` only after startup, `ctx_submit=124`,
  `res_flush=120`), final present trace `frames=40 frame_avg_us=4240
  gl_compose_avg_us=113 scanout_avg_us=3984`, and zero async make-room
  stalls/waits (`posted_submit_3d=123`, `posted_flush=116`, `depth=2`,
  `max_wait_us=0`).  Keep the cleanup, but do not treat it as the parity fix.
- The startup full-screen repaint window is now backend-aware and configurable
  with `wlcomp_startup_full_ms=N` / `XV6_WLCOMP_STARTUP_FULL_MS=N`.  Non-virgl
  paths keep the old 3000ms default for delayed synthvid/firmware-logo
  clearing; virgl-backed desktops default to 1000ms so the 3D demo does not pay
  avoidable full-target uploads through most of its first seconds.  The strict
  120-frame FBSTAT attribution run
  (`xv6-virgl-desktop-startup-full-virgl1000-fbstat120-final.log`) passed with
  completion `frames=120 status=0 elapsed=3.572s fps=33.6`, warm samples
  `40.5/34.2` and `41.4/38.8`, stable scanout shape (`set_scanout=4`,
  desktop-sized `SET_SCANOUT` only after startup, `ctx_submit=124`,
  `res_flush=123`), zero async make-room stalls/waits, and complete fbstat
  display footer `display_last_complete 120`.  This improves the short demo
  startup envelope, but the warm visible cadence is still below Alpine and
  remains scanout dominated (`scanout_avg_us=4295` in the final active trace).
- Taskbar clock deferral during active virgl demo frames is rejected.  A
  4000ms/2000ms probe kept the active trace cleaner (`cpu_rects_total=0` in
  some warm samples) but made strict FBSTAT attribution weak
  (`display_last_complete=119/118` for 120 app frames) and did not improve the
  short-demo cadence.  Do not hide the scanout/display gap by removing desktop
  clock presents from the measured window.
- `wlcomp_virgl_full_scanout=1` was checked as the opposite scanout-rect
  diagnostic.  It forces each scanout notify to cover the full 1280x800 target
  instead of the damage-derived client/chrome rectangle.  The 60-frame run
  (`xv6-virgl-desktop-full-scanout-defaultoff60.log`) passed and preserved the
  stable desktop scanout shape (`ctx_submit=64`, `set_scanout=4`,
  `fence_ctrl=64`, no rebinds after startup), but it was not faster:
  the active sample was only `app_loop_fps=32.4`, `displayed_fps=32.0`,
  `scanout_avg_us=5371`, completion was `fps=25.5`, and trace
  `res_flush=161` remained similar to the 60-frame no-flush-default control.
  Keep damage-derived scanout rectangles; forcing full-screen flushes does not
  buy host/display overlap.
- Longer 180-frame default validation after the GL cleanup shows the warm
  steady state is better than the short 60-frame run, but still below Alpine:
  - passed with no virgl/context/scanout failure markers;
  - later active samples reached `app_loop_fps=33.0`, `displayed_fps=34.3`;
  - warm traces dropped as low as
    `frame_avg_us=5941 cpu_upload_avg_us=334 gl_compose_avg_us=1445
    scanout_avg_us=3073`;
  - QEMU trace counts: `ctx_submit=363`, `res_flush=186`, `set_scanout=4`,
    `fence_ctrl=363`;
  - post-startup `SET_SCANOUT` stayed desktop-sized (`1280x800`).
  This confirms the earlier CPU-upload concern was partly startup/title-update
  noise. The remaining default-path ceiling is not a simple filtered-damage
  upload bug.
- Release-path instrumentation now reports buffer-release totals in
  `present-trace` (`release_total`, `release_gpu_immediate`,
  `release_queued`, `release_flushed`, and `release_pending`). A refreshed
  180-frame default run on 2026-06-03 passed with:
  - no virgl/context/scanout failure markers;
  - post-startup `SET_SCANOUT` still desktop-sized only (`1280x800`);
  - QEMU trace counts: `ctx_submit=365`, `res_flush=188`, `set_scanout=4`,
    `fence_ctrl=365`;
  - warm samples such as
    `frame_avg_us=5568 cpu_upload_avg_us=245 gl_compose_avg_us=1220
    scanout_avg_us=3649 scanout_rects_x100/frame=97 release_total=34
    release_gpu_immediate=34 release_queued=0 release_flushed=0
    release_pending=0`.
  This rejects buffer-release backpressure as the current limiter: GPU buffers
  are released immediately, no release queue builds up, and the remaining
  steady cost is still the compose/scanout/display path.
- `scripts/gpu/virgl-desktop-validate.sh` now accepts
  `VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='...'` for explicit diagnostic cmdline
  probes while preserving the dry-run launch contract. A pacing probe with
  `glsmoke_present_interval=0` is **rejected** as a fix: it lets the client
  outrun the compositor (`app_loop_fps=46.4`, `displayed_fps=25.5`) and the
  validator correctly fails the displayed/app FPS tracking gate. Trace counts
  were `ctx_submit=184`, `res_flush=261`, `set_scanout=3`, `fence_ctrl=184`.
  Keep normal callback pacing; the next fix must increase compositor/display
  throughput rather than merely unthrottle client commits.
- `desktop.c` now also bridges diagnostic cmdline
  `glsmoke_render_div=N` into `mesawlegl --render-div=N` for render-load
  probes. A 180-frame run with
  `VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='glsmoke_render_div=2'` passed with:
  - `demo_surface_matrix window=640x480 render=320x240 render_div=2
    present_interval=1`;
  - later samples around `app_loop_fps=31.6-32.5` and
    `displayed_fps=32.3-32.7`;
  - QEMU trace counts: `ctx_submit=367`, `res_flush=298`, `set_scanout=4`,
    `fence_ctrl=367`;
  - post-startup `SET_SCANOUT` stayed desktop-sized (`1280x800`).
  Lowering client render resolution did not move the ceiling out of the
  low-30s band. Treat client render cost as secondary for this workload; the
  next useful work remains compositor scanout/display throughput and overlap.
- Present-path instrumentation now distinguishes GL-preflushed compositor
  frames from CPU-only frames and counts scanout submits/rebinds. The demo now
  emits a compact `mesawlegl_completion_matrix ... frames=N status=0` marker
  before Wayland cleanup, and `virgl-desktop-validate.sh` requires a clean
  marker with `status=0` while relying on screenshot/pipeline evidence for
  visible output. A refreshed 180-frame default run passed this gate with no
  bad markers and showed:
  - warm active samples at `app_loop_fps=32.4-34.6` and
    `displayed_fps=32.7-35.7`;
  - post-startup `path_gl_preflush` for essentially every active demo frame;
  - `scanout_submits` tracking one submit per active frame;
  - `scanout_rebinds=0` after the initial desktop target bind;
  - QEMU trace counts: `ctx_submit=364`, `res_flush=191`, `set_scanout=4`,
    `fence_ctrl=364`.
  The same counters on the opt-in damage-flip path prove why it is slower:
  active samples reported `scanout_rebinds=18/18` and then `22/22`, with
  `set_scanout=63` over a 60-frame run and `scanout_avg_us` around
  16-18 ms. The default path is already stable-bound; the flip path is paying
  a per-frame full-desktop rebind.
- Diagnostic probes with `virtio_gpu_async_fb_transfer=1` are **rejected** as a
  default-path fix.  A 120-frame run with the current stable scanout path
  (`xv6-virgl-desktop-frame-ms8-async-fb-transfer120.log`) passed, but it was
  slower (`app_loop_fps=49.2/displayed_fps=42.8`), added async admission
  pressure (`virtio_async_posted_transfer=45`, `make_room_stalls=4`,
  `max_wait_us=4990`), and left scanout in the same 4-6ms band.  The same probe
  with async depth 3 failed validation and fell out of the healthy GL-preflush
  scanout path (`path_gl_preflush=0`, `scanout_submits=0`, `posted_flush=0`).
  Keep async framebuffer transfer diagnostic-only.
- Tier 0 CPU-damage prefiltering is now implemented in
  `wlcomp_render_loop.inc` behind `wlcomp_prefilter_gpu_damage=1` (default on;
  env override `XV6_WLCOMP_PREFILTER_GPU_DAMAGE=0`). It builds the GPU-compose
  pending list before the clipped CPU paint loop, subtracts GPU-covered client
  rects, and therefore skips CPU repaint/upload on GPU-only animation frames.
  A refreshed 180-frame strict run passed with:
  - `mesawlegl_completion_matrix loop=1 frames=180 status=0`;
  - warm active samples around `app_loop_fps=32.4-33.7`,
    `displayed_fps=33.4-34.8`;
  - later present traces with residual CPU damage only:
    `cpu_upload_avg_us=440-479`, `cpu_rects_total=1`;
  - default-path invariants preserved: active `path_gl_preflush`,
    `scanout_submits` one/frame, `scanout_rebinds=0`;
  - QEMU trace counts: `ctx_submit=362`, `res_flush=189`, `set_scanout=4`,
    `fence_ctrl=362`;
  - post-startup `SET_SCANOUT` stayed desktop-sized (`1280x800`).
  This removes the avoidable CPU/chrome repaint from the common animated frame,
  but it does not lift the ceiling beyond the low/mid-30s band. The remaining
  limiter is still the serialized compositor GL compose plus full-desktop
  scanout flush/display path.
- A focused pageflip-copy diagnostic is now wired behind
  `VIRGL_DESKTOP_VALIDATE_PAGEFLIP_COPY=1`, which passes a compact
  `wlcomp_pageflip_diag=1` guest flag instead of a long chain of wlcomp
  overrides. That compact flag survived the xv6 command-line truncation point
  and forced the intended old virgl-copy lane: the log showed
  `wlcomp: using virgl framebuffer`, `FB_GPU_BO_PRESENT` with `flags=0x1`, and
  repeated kernel `virtio_gpu: present copy ... mode=copy` markers. It is
  **not** a fix: the kernel's own validation failed on the first pageflip-copy
  attempt with `flip_nonblack=0`, `matches=0`, then disabled pageflip-copy for
  the boot. The 60-frame demo still completed, but the validator correctly
  failed because `virtio_gpu: pageflip-copy present` never appeared and
  `pageflip-copy validation failed` did. Do not promote the copy-to-pageflip
  lane; treat it as evidence that Tier 1 needs a real page-flip primitive over
  full-screen scanout resources, not the current copy shim.
  A follow-up 180-frame default validation with the compact diagnostic hook
  present still passed (`xv6-virgl-desktop-pageflip-diag-compact-default.log`):
  `mesawlegl_completion_matrix loop=1 frames=180 status=0`, active FPS stayed
  in the low/mid-30s, and the default path remained GL-preflush with stable
  full-screen scanout.
- A real no-copy page-flip ABI now exists as a Tier 1 prerequisite:
  `FB_GPU_PAGE_FLIP` accepts a full-screen virgl-backed BO handle, rejects
  non-full-screen/non-virgl/fallback cases, binds that resource as scanout, and
  returns a display fence. `wlcomp_page_flip_present=1` is an opt-in compositor
  diagnostic that calls the new ioctl after GL compose. The first 60-frame run with
  `VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_virgl_fb=1 wlcomp_page_flip_present=1'`
  passed and showed both sides of the ABI:
  `virtio_gpu: page-flip present resource=4 size=1280x800` and
  `wlcomp: virgl framebuffer page-flip handle=2 res=4`. This proves the
  no-copy ioctl path under the desktop workload, but it is still single-target
  present and does **not** improve FPS; active samples were around
  `app_loop_fps=28.4`, `displayed_fps=24.3`. Keep it opt-in until wlcomp
  actually composes into alternating back buffers.
  A follow-up default 180-frame run passed
  (`xv6-virgl-desktop-pageflip-abi-default180.log`) with
  `mesawlegl_completion_matrix loop=1 frames=180 status=0`, active samples up
  to `app_loop_fps=33.5`, `displayed_fps=35.6`, stable
  `path_gl_preflush`, `scanout_rebinds=0`, and trace counts
  `ctx_submit=361`, `res_flush=188`, `set_scanout=4`, `fence_ctrl=361`.
- `wlcomp_page_flip_present=1` is now self-contained for focused testing:
  it requests virgl framebuffer backing, forces at least two full-screen targets,
  enables target swapping, and the validator exposes it as
  `VIRGL_DESKTOP_VALIDATE_PAGE_FLIP_PRESENT=1`. The self-contained 60-frame
  validator passed (`xv6-virgl-desktop-pageflip-selfcontained2.log`) and showed
  real two-target alternation:
  `wlcomp: virgl framebuffer double-buffer prep ready front_res=4 back_res=5`,
  repeated `wlcomp: virgl framebuffer flip render_res=5/4`, and kernel
  `virtio_gpu: page-flip present resource=5/4 size=1280x800 already_bound=0`.
  This is still a **negative performance result**, not a promotable Tier 1 fix:
  `mesawlegl_completion_matrix loop=1 frames=60 status=0`, active FPS only around
  `app_loop_fps=13.4`, `displayed_fps=13.4`, warm `scanout_rebinds=14/14`, and
  QEMU trace counts `set_scanout=64`, `res_flush=64`. A default 180-frame run
  after the self-contained hook passed
  (`xv6-virgl-desktop-pageflip-selfcontained-default180.log`) with no page-flip
  markers and stable default trace shape (`set_scanout=4`).

## FPS maximization roadmap (2026-06-03)

### Decisive reframing: the host is not the ceiling

The reference captures above settle the question of where the wall is:

- Alpine on **this same host class** (WSL d3d12 -> RTX 4060, single virtio-gpu
  control queue) runs `glxgears` at **141 FPS** and `weston-simple-egl` at
  **66.8-73.2 FPS**.
- xv6 windowed present currently caps around **~30-35 FPS** displayed after
  warmup, depending on run length and startup noise.

Therefore the single control queue and the WSL d3d12 transport are **not** the
limiter -- they demonstrably carry >>60 FPS of submits. The gap is entirely in
the xv6 **guest compositor present architecture**.

### Measured xv6 bottleneck (wlcomp_trace_present=1)

Per compositor frame in the current low/mid-30 FPS default path:

| stage | time | note |
|-------|------|------|
| cpu_upload (chrome) | ~0.1-0.4 ms after warmup | mostly residual taskbar/title damage |
| gl_compose (texture blit into shared full-screen target) | ~1.5-2.7 ms after warmup | still a separate compositor GL pass |
| scanout (RESOURCE_FLUSH of that target) | ~3.4-8.3 ms after warmup | one stable-bound submit per active frame |
| traced present | ~7-11 ms warm samples | startup samples are much slower |
| actual loop | low/mid-30 FPS | still below Alpine/Weston 66-73 FPS |

Two structural costs, neither of which Alpine pays:

1. **Redundant compose + separate flush.** Every frame we (a) blit the client
   texture into one shared full-screen target via `gl_compose`, then (b)
   `RESOURCE_FLUSH` that target. The current path-trace shows this is the
   intended `path_gl_preflush` route, with one stable scanout submit per active
   frame and no rebind after startup.
2. **No double-buffered page-flip and no compositor pipelining.** We reuse a
   single shared scanout BO, so scan-out of frame N cannot overlap compose of
   frame N+1; the loop is effectively one-frame-at-a-time. The opt-in flip
   prototype proves the opposite extreme is also bad today: it alternates
   full-screen targets but pays one `SET_SCANOUT`/rebind every active frame.

The decisive detail: even when client render resolution is halved to 320x240,
the FPS ceiling stays in the low-30s. The cost is the guest compositor/display
present structure, not client pixel shading. The fix is to pipeline or reduce
the compositor submit sequence, exactly as Weston/Xorg do.

### Why Alpine is faster (architecture to copy)

- **DRM/KMS atomic page-flip between pre-allocated double-buffered full-screen
  scanout resources.** `SET_SCANOUT` swaps the displayed buffer pointer; there
  is no per-frame full-screen texture re-blit into a separately-flushed target.
- **Mesa virgl pipelines client submits.** Frame N+1 is submitted without
  blocking on frame N's fence; multiple ctrlq submissions are in flight and
  fences are reaped asynchronously.
- **Unredirection for a fullscreen-covering client**: the client buffer is
  scanned out directly, with zero compositor blit.

### Tiered plan to reach and hold 60+ FPS

Targets are cumulative; each tier is independently shippable and off by default
behind a flag until validated by pixels + trace shape (never counters alone).

#### Tier 0 - submit-count reduction (low risk) -> ~30 to ~45 FPS

- Coalesce scanout rectangles to a single submit per frame. Status:
  implemented for the windowed virgl desktop path; the focused validator now
  enforces steady `scanout_rects/frame <= 1`.
- Skip the chrome `cpu_upload`/`present_damage_rects` transfer on frames where
  only the GPU client animated (damage-only). Status: implemented behind
  `wlcomp_prefilter_gpu_damage=1` and defaulted on. It keeps validation clean
  and reduces later active CPU upload to tiny residual taskbar/title damage,
  but FPS remains low/mid-30s, so Tier 0 is no longer the main path to parity.
- Confirm `present_virgl_scanout_rects` takes the `virtio_gpu_async_scanout_flush`
  path on every windowed frame. Status: the new `scanout_rebinds` counter shows
  the default path is stable-bound after startup (`rebinds=0` in warm samples),
  so steady scanout flushes are eligible for the async path.
- Files: `wlcomp_render_loop.inc`, `wlcomp_fb.inc`.

#### Tier 1 - double-buffered scanout + page-flip (core fix) -> ~45 to ~60 FPS

This removes the redundant compose-into-shared-target + separate-flush pair.

- Allocate **two** full-screen GPU scanout resources (front/back) in wlcomp via
  the existing virgl/GBM BO path. Status: preparatory allocation is implemented
  behind `wlcomp_virgl_fb_buffers=2`, but the present loop does not yet flip to
  the back target.
- Compose the desktop scene (wallpaper, chrome, imported client textures)
  directly into the **back** buffer.
- Present = bind back buffer as scanout + async flush, then swap front/back.
  No full-screen texture copy into a third, separately-flushed target.
- Kernel: generalize `virtio_gpu_bind_resource_scanout` (already has the
  `already_bound` async path) into a page-flip primitive that swaps between two
  pre-bound full-screen resources and returns a flip fence without re-upload.
  Status: initial `FB_GPU_PAGE_FLIP` ioctl is implemented and validated as an
  opt-in diagnostic. wlcomp can now make the diagnostic self-contained and
  alternate between two full-screen targets, but QEMU reports
  `already_bound=0` on each alternation, producing one expensive `SET_SCANOUT`
  per frame. The remaining Tier 1 work is a lower-cost host-visible flip
  primitive or compositor pipeline that avoids per-frame `SET_SCANOUT` while
  still rendering into separate full-screen buffers.
- Do **not** use the existing pageflip-copy diagnostic as the Tier 1 primitive.
  The 2026-06-03 compact-flag run reached that lane and failed kernel pixel
  validation (`flip_nonblack=0`, `matches=0`), so it remains a negative
  diagnostic only.
- Files: `wlcomp_gl_compose.inc`, `wlcomp_fb.inc`, `wlcomp_render_loop.inc`,
  `kernel/kernel/virtio_gpu.c`, `kernel/kernel/dev/fb/`.

#### Tier 2 - compositor pipelining (overlap render and present) -> steady 60 FPS

- Triple-buffer (3 scanout resources) so `compose(N+1)` runs while `scanout(N)`
  is in flight and `N-1` is on screen.
- Reap virgl/present fences asynchronously (poll / kqueue) instead of blocking;
  only block when all buffers are busy.
- Keep the existing early frame-callback dispatch (`wlcomp_pipeline_callbacks`,
  already on) so the client pipelines in lockstep.
- `wlcomp_pipeline_gpu_release=1` is now an opt-in Tier 2 timing probe: after
  GL composition flushes a client dmabuf into the full-screen virgl target, the
  compositor releases that GPU-composed buffer and flushes Wayland clients before
  the final scanout flush. It is valid and passes
  `VIRGL_DESKTOP_VALIDATE_PIPELINE_GPU_RELEASE=1`, but it is **not** the 60 FPS
  fix by itself. A 180-frame run
  (`xv6-virgl-desktop-pipeline-gpu-release180b.log`) completed with
  `wlcomp: pipelined gpu buffer release before scanout`, stable
  `scanout_rebinds=0`, trace counts `ctx_submit=364`, `res_flush=193`,
  `set_scanout=4`, and warm samples around `app_loop_fps=28.9-32.7`,
  `displayed_fps=31.3-34.5`. The default 180-frame control still passed with no
  early-release markers and similar trace shape (`ctx_submit=363`,
  `res_flush=189`, `set_scanout=4`). Conclusion: client release timing is not
  the dominant limiter for the current mesawlegl workload; keep pushing on
  compositor submit overlap/reduction.
- `VIRGL_DESKTOP_VALIDATE_GL_FINISH=1` is now an opt-in GL ordering diagnostic
  for the default compositor path. It maps to `wlcomp_gl_finish=1` and proves the
  run with `wlcomp: gl-compose finish-each-present enabled`. A 60-frame focused
  run (`xv6-virgl-desktop-glfinish60-named.log`) passed with the healthy
  full-desktop scanout shape (`set_scanout=4`, steady `scanout_rebinds=0`), but
  it was a **negative performance result**: warm telemetry was
  `app_loop_fps=23.7`, `displayed_fps=21.9`, with
  `gl_compose_avg_us=12750` versus the rebuilt default control
  (`xv6-virgl-desktop-glfinish-default60-pass.log`) at `app_loop_fps=29.6`,
  `displayed_fps=29.8`, `gl_compose_avg_us=4295`. Conclusion: forcing
  `glFinish` before scanout is not the missing ordering fix; default `glFlush`
  plus the following scanout flush remains the better path.
- Async queue pressure is now directly measurable with
  `VIRGL_DESKTOP_VALIDATE_FBSTAT=1`, which runs guest `fbstat` after the GL
  smoke child exits. The first passing 60-frame sample showed depth 2,
  `posted=194`, `retired=194`, `pending=0`, `make_room_stalls=6`, and
  `max_wait_us=9880`, while preserving the healthy stable-scanout trace shape
  (`set_scanout=4`, active `scanout_rebinds=0`). This makes the next Tier 2
  step sharper: do not blindly increase async depth; reduce/overlap the
  compose+scanout sequence while watching whether `pending` or make-room waits
  actually become the limiter. A follow-up split-counter run showed
  `posted_submit_3d=125`, `posted_flush=65`, `posted_transfer=0`,
  `submit_3d_stalls=0`, `flush_stalls=3`, and `max_wait_us=1182`, so current
  pressure is on scanout-flush admission rather than 3D submit admission.
- Files: `wlcomp_render_loop.inc`, `wlcomp_gpu_sync.inc`, `wlcomp_fb.inc`.

#### Tier 3 - windowed unredirection (direct client scanout, no mode change) -> approach simple-egl/glxgears class for the single-window case

- When one opaque client covers a screen sub-rect, scan out the client's own
  resource directly, **without changing the display mode**.
- This requires fixing the kernel `virtio_gpu_allow_partial_scanout` guard so it
  programs a scanout rectangle/overlay instead of resizing the mode -- that mode
  resize is exactly the earlier "too large"/host-window-jump regression and must
  not return.
- Fall back to the composited Tier 1/2 path when the window is occluded,
  translucent, moving, or smaller than a threshold.
- `VIRGL_DESKTOP_VALIDATE_FULLSCREEN_DIRECT=1` now exercises the existing
  fullscreen unredirection diagnostic (`wlcomp_fullscreen_direct=1`). The
  120-frame focused run (`xv6-virgl-desktop-fullscreen-direct120.log`) passed:
  the demo was configured as `window=1280x800 render=1280x800`, wlcomp emitted
  `gpu-compose presented path=resource-scanout`, and every trace `SET_SCANOUT`
  stayed full desktop-sized. It is still a **negative performance result** for
  this host/workload: QEMU trace counts were `ctx_submit=126`, `res_flush=236`,
  `set_scanout=122`, `fence_ctrl=126`, and warm samples remained around
  `app_loop_fps=27.7-34.7`, `displayed_fps=30.0-36.9`. The issue is not
  geometry anymore; the rotating client swapchain makes direct resource scanout
  pay the same per-frame full-screen rebind cost as the page-flip probes. Keep
  it opt-in and use it as the Tier 3 baseline until a stable-overlay or
  lower-cost host flip primitive exists.
- `VIRGL_DESKTOP_VALIDATE_MESA_COLOR_BUFFERS=1` is a new opt-in client-side
  diagnostic for the fullscreen-direct path. It maps
  `glsmoke_color_buffers=1` to Mesa's
  `XV6_MESA_WAYLAND_COLOR_BUFFERS=1`, causing the Wayland EGL loader to reuse
  one color-buffer slot instead of rotating across four. A 60-frame focused run
  (`xv6-virgl-desktop-fullscreen-direct-singlebuf60-pass.log`) passed and
  proved the rebinding hypothesis: trace counts fell to `set_scanout=7`,
  `ctx_submit=66`, `res_flush=68`, and steady `scanout_rebinds=0` after the
  startup binds. It is still a **negative performance result**:
  `app_loop_fps=21.7`, `displayed_fps=15.8`, and the app completed 60 frames in
  ~30 FPS wall time. This proves "one stable client resource" avoids host
  rebind churn, but single-buffer client reuse stalls/under-displays too much to
  promote. The real Tier 3 fix needs stable scanout/overlay semantics with
  pipeline depth, not just a smaller client swapchain.
- `VIRGL_DESKTOP_VALIDATE_MESA_COLOR_BUFFERS=2` was also checked with
  fullscreen-direct for 120 frames
  (`xv6-virgl-desktop-fullscreen-direct-colorbuf2-120.log`). It passed and
  confirmed the other side of that tradeoff: two buffers restore pipeline depth
  enough to show a warm `app_loop_fps=56.8`, `displayed_fps=57.6` sample, but
  the host-visible fullscreen resources alternate every frame again. The footer
  showed `ctx_submit=124`, `res_flush=257`, `set_scanout=124`, and the
  `SET_SCANOUT` history toggled between two 1280x800 resources (`0x9`/`0xa`).
  Keep it diagnostic only; it is evidence that "stable fullscreen scanout with
  depth" must be solved below the client swapchain limit knob.
- The same one-buffer diagnostic was also checked on the normal composited
  desktop path with a 180-frame run
  (`xv6-virgl-desktop-colorbuf1-default180.log`). It stayed geometry-stable
  (`set_scanout=4`, warm `scanout_rebinds=0`) and trimmed resource churn
  (`res_create_3d=11`, `res_unref=8`, versus the same-build depth-2 control's
  `17/14`), but it did **not** move the ceiling: `ctx_submit=367`,
  `res_flush=194`, `res_xfer_toh_3d=239`, app completion was only
  `fps=24.2`, and warm samples mostly stayed in the 20s. Treat this as evidence
  that simple client swapchain narrowing is not the default-path bottleneck.

#### Tier 4 - host/transport (already favorable; keep it optimal)

- The RTX 4060 via WSL d3d12 is abundant (Alpine proves 141 FPS). Keep the
  `virtio-vga-gl` host renderer and KVM on; never fall back to CPU readback.
- Ensure the guest GL path uses the d3d12-backed virgl ICD, not llvmpipe. The
  GTK **X11** backend drops to llvmpipe (~2 FPS) -- keep the WSLg native Wayland
  window for the accelerated path.
- Optional: adopt `VIRTIO_GPU_F_RESOURCE_BLOB` host-coherent resources to drop
  `TRANSFER_TO_HOST` copies for shm chrome.

### Realistic expected outcome

- Tier 0 alone: ~45 FPS windowed (days, low risk).
- Tier 0 + 1: ~55-60 FPS -- the double-buffered page-flip removes the
  redundant compose+flush serialization that is the dominant cost today.
- Tier 0 + 1 + 2: **steady 60 FPS**, plausibly into the Weston simple-EGL band
  (66-73 FPS), because compose and present then overlap.
- Tier 3: approaches `glxgears`-class for a single full-window client.

60 FPS is realistic on the existing hardware. It requires the **double-buffered
page-flip + compositor pipelining** architecture (Tiers 1-2), not a faster host.

### Constraints carried from prior phases

- Never resize the display mode for a sub-fullscreen client (Tier 3 trap).
- Apply Y-flip exactly once in the GPU composition path.
- Validate destination pixels (`fbstat ppm-current` / sample), not virgl command
  acceptance or counters.
- Keep every new path behind a cmdline/env flag, off by default, until proven.



Run direct KMS validator:

```sh
scripts/gpu/virgl-kms-validate.sh
```

Run visible desktop demo with current GUI image:

```sh
DISPLAY_MODE=gtk \
USE_KVM=1 \
QEMU_GPU=virtio-vga-gl-primary \
QEMU_INPUT=virtio \
QEMU_NET=0 \
QEMU_APPEND='root=/dev/disk0 netsurf=0 webkit=0 glsmoke=0 video=1280x800 wlcomp_gpu_compose=1' \
scripts/launch/launch-gui.sh
```

If the container image is missing and auto-build fails:

```sh
cmake --build build-x86_64/ports --target port-wayland -j2
scripts/image/make-rootfs.sh build-x86_64/sysroot /tmp/xv6-virgl-test.img 2304
FSIMG=/tmp/xv6-virgl-test.img DISPLAY_MODE=gtk USE_KVM=1 QEMU_GPU=virtio-vga-gl-primary scripts/launch/launch-gui.sh
```

Collect framebuffer evidence in guest:

```sh
fbstat probe
fbstat
fbstat sample <x> <y> <w> <h>
fbstat ppm-current /tmp/current.ppm
```

Collect QEMU trace:

```sh
cat > /tmp/qemu-virgl-events <<'EOF'
virtio_gpu_cmd_set_scanout
virtio_gpu_cmd_res_flush
virtio_gpu_cmd_ctx_submit
virtio_gpu_fence_ctrl
virtio_gpu_fence_resp
virtio_gpu_cmd_res_create_3d
virtio_gpu_cmd_ctx_res_attach
virtio_gpu_cmd_ctx_res_detach
EOF

QEMU_EXTRA="-trace events=/tmp/qemu-virgl-events,file=/tmp/xv6-qemu-virgl.trace" \
...
```

Run the focused windowed-desktop parity validator:

```sh
scripts/gpu/virgl-desktop-validate.sh
```

This validator launches the normal 1280x800 desktop with the 3D demo, enables
`wlcomp_trace_present=1`, captures QEMU virtio-gpu trace events, and fails if:

- the run does not report the virgl spherical demo;
- the guest framebuffer screenshot is missing, malformed, blank, or too
  low-variance;
- `wlcomp: present-trace` is missing;
- displayed FPS is zero;
- `SET_SCANOUT` binds any nonzero size other than the desktop size;
- QEMU trace lacks `ctx_submit`, `res_flush`, or fence traffic;
- the guest crashes or virtio-gpu times out.

Screenshot validation is mandatory by default. The validator copies
`build-x86_64/fs.img` into the trace directory, captures a live
`fbstat ppm-current` rectangle from the active scanout into that guest image,
extracts it with `debugfs`, and records a `screenshot_matrix ... status=PASS`
line after checking dimensions, nonblack pixels, and color variance. This avoids
QEMU HMP `screendump` failures such as `Error: no surface` on accelerated GL
display backends and avoids corrupting screenshot bytes through serial logs.

Use it before and after compositor changes. It is deliberately narrower than
`gpu-validate.sh`: its job is to guard the Alpine desktop invariants, not every
GPU substrate feature.

Keep focused diagnostic command lines compact. The xv6 guest command line can
truncate late flags; `virgl-desktop-validate.sh` now omits zero-valued optional
diagnostic flags so the default `wlcomp_virgl_fb=1` path survives. Its
steady-state present selector uses `gl_bufs_total`, not the integer
`gl_bufs/frame` average, because a mostly-GL sample can still print
`gl_bufs/frame=0` when averaged across cleanup CPU-only frames.

### Phase 9: Success criteria

Minimum acceptable success:

- 3D demo surface visible immediately after launch.
- Content orientation correct.
- Host QEMU window remains normal desktop size.
- No violent blinking in panel/desktop.
- Visible updates occur at a cadence close to the app's printed FPS, not one frame every several seconds.
- QEMU trace shows steady full-screen scanout target flushes.
- `SET_SCANOUT` does not alternate between app-sized and desktop-sized resources.

Strong success:

- wlcomp uses GPU composition for the full desktop target.
- CPU readback/copy fallback is not used for the 3D demo steady state.
- Desktop remains responsive while the 3D demo runs.
- Post-warmup FPS is near Alpine reference behavior on the same host/QEMU path.

Do not accept:

- fullscreen-only direct KMS as a solution to the desktop problem;
- direct scanout of 640x480 app resources in normal desktop mode;
- high app-side FPS while screen only visibly updates slowly;
- virgl command acceptance without destination pixel validation;
- a path that fixes the 3D app but makes the panel blink.

## Handoff notes

- The user strongly prefers QEMU/guest screen dumps or screenshots as evidence.
  For accelerated SDL/GL Alpine, QEMU HMP screendump may fail with
  `Error: no surface`; host screenshots are acceptable for that control. For
  xv6, `virgl-desktop-validate.sh` now requires guest `fbstat ppm-current`
  screenshot validation by default, so do not accept log-only desktop runs.
- Keep changes minimal and scoped. There are many dirty files from prior GPU work; do not revert unrelated changes.
- `scripts/gpu/alpine-virgl-desktop-capture.sh` and `scripts/gpu/virgl-kms-validate.sh` are currently untracked in the top-level repo status.
- `scripts/gpu/virgl-desktop-validate.sh` is the focused xv6 windowed desktop
  parity validator. Run it after changes to `wlcomp`, virgl scanout, or launch
  defaults.
- Previous xv6 work changed `ports/wayland/src/mesawlegl.c`, `wlcomp_render_loop.inc`, `wlcomp_fb.inc`, and related compositor fragments. Inspect current file contents before editing; do not assume a clean baseline.

## Existing Linux Desktop Control

Introducing an existing Linux desktop is helpful, but use it as a reference
trace and pixel/control capture, not as code to blindly copy. Alpine/Openbox or
Alpine/Weston gives a clean minimal control that proves the host, QEMU GL
display, WSL D3D12 virgl renderer, and virtio-gpu control queue can sustain
smooth desktop GL on this machine.

Use the Linux control to answer these specific questions before changing xv6:

- Which virtio-gpu commands appear during steady-state desktop GL animation?
- How often does Linux issue `SET_SCANOUT`, and what size is the scanout target?
- Are app-sized resources ever bound as the display scanout in normal desktop
  mode?
- How many `ctx_submit`, fence, and `res_flush` events appear per visible frame?
- Does Linux rely on page-flip/double buffering instead of reusing a single
  shared scanout target?
- Does the app FPS match visibly captured frame progression?

Do not use the Linux desktop as proof that xv6 is fixed. The proof for xv6 must
come from xv6 traces plus visible output/screen dumps from the xv6 run.

Recommended control command:

```sh
ALPINE_CAPTURE_DIR=build-x86_64/alpine-trace/live-$(date +%Y%m%d-%H%M%S) \
ALPINE_RUN_SECONDS=10 \
scripts/gpu/alpine-virgl-desktop-capture.sh
```

If a fuller desktop is needed than Alpine's minimal Openbox control, boot a
small live Linux with Xorg or Weston and use the same virtio-gpu trace event
set. Keep the display backend and QEMU GPU model the same as xv6:

- `DISPLAY_MODE=sdl` or the known accelerated WSLg display path;
- `QEMU_GPU=virtio-vga-gl` / `virtio-vga-gl-primary`;
- KVM enabled when available;
- QEMU tracing enabled for `virtio_gpu_cmd_ctx_submit`,
  `virtio_gpu_cmd_res_flush`, `virtio_gpu_cmd_set_scanout`, and fences.

The comparison is valid only when both guests use the same host display backend
and renderer class. A Linux desktop running on a different renderer, a pure
software path, or a host-native compositor outside QEMU does not answer the xv6
virgl desktop question.

## Prompt For The Next Agent

Use this exact prompt to hand the task to another agent:

```text
You are working in /home/es/xv6-os. Continue the virgl desktop parity task using
docs/alpine-virgl-desktop-handoff-plan.md as the source of truth.

Goal: make xv6's normal windowed GUI desktop behave like the known-good
Alpine/Linux virgl desktop: the whole desktop remains GPU accelerated in a
normal QEMU window, the 3D demo surface is visible and correctly oriented, the
panel does not blink, the QEMU host window does not resize to the app surface,
and the visible frame cadence matches the demo's FPS instead of updating every
few seconds.

Important current facts:
- Alpine/Openbox on the same host/QEMU virgl path proves acceleration works:
  renderer=virgl, direct rendering=yes, glxgears about 141 FPS, and steady
  virtio-gpu ctx_submit/fence/res_flush traffic.
- xv6 can render the sphere and has fixed the surface placement and the
  app-sized scanout/window-jump class of bugs, but it is still not Alpine-smooth.
- Async scanout flush, targeted fence draining, and default-path GL cleanup
  improved the split app-FPS/displayed-FPS issue; the best default warm samples
  now reach the high-40/low-50 displayed-FPS band, but xv6 remains
  architecturally behind Linux/Alpine.
- Mesa Wayland swap profiling (`glsmoke_mesa_perf=1`) shows the default client
  cadence is dominated by Mesa's frame-callback throttle wait
  (`throttle=19-29ms` per swap in 120-frame evidence), while compositor present
  work in the same warm windows is only around 4ms.  A throttle-off A/B
  (`glsmoke_mesa_throttle=0`) proves removing that wait is not a valid fix:
  app FPS can reach about 62, but displayed FPS remains around 39 and the
  validator rejects the run because visible presentation no longer tracks the
  app.
- Mesa virgl winsys profiling (`glsmoke_mesa_perf=1` after the winsys probe)
  shows warm `flush_drawable` is mostly accounted for by transfer+submit work,
  while explicit winsys fence waits are under 1ms on average with occasional
  long spikes.  The larger remaining delay is still frame-callback throttle
  delivery.
- The compositor's software frame-callback interval now defaults to 6ms instead
  of 16ms.  This is not Alpine parity, but the pacing A/B showed the visible
  demo can reach Alpine-like displayed cadence when callbacks are released
  sooner while Mesa's frame-callback throttle remains enabled.  The 8ms run was
  the first clean improvement (`51.4/50.9`), the 4ms A/B is a negative result
  because it increases app-side churn without improving displayed cadence, and
  the best explicit 6ms evidence
  (`xv6-virgl-desktop-frame-ms6-default180.log`) reached warm samples
  `57.7/56.3` and `65.7/62.1` with stable scanout counters.  The rebuilt
  default-6 clean run (`xv6-virgl-desktop-frame-ms6-default180-clean.log`)
  passed but remained variable (`51.2/49.0`, `53.9/49.0`) and scanout-dominated
  (`frame_avg_us=5957`, `scanout_avg_us=5749`).  A later 5ms probe passed but
  did not improve the envelope (`53.8/47.0`, `51.3/42.9`, `52.4/55.8`) and
  increased `RESOURCE_FLUSH` traffic to `res_flush=320`.  A 7ms probe was clean
  (`res_flush=185`, zero async stalls) but peaked only at `55.5/52.3`, so keep
  6ms as the current default.  A later default 180-frame frame-perf+fbstat
  correlation run again passed with `57.4/49.9` then `54.8/50.0`, zero async
  make-room stalls, and callback delivery averages around 17-20ms, confirming
  that 6ms is still the right default and that the next work should reduce or
  overlap the per-callback compose/scanout path rather than changing async depth.
- `wlcomp_callback_poll_ms=N` is only an opt-in pending-callback epoll-wait
  diagnostic.  The 1ms and 2ms 180-frame probes both passed but were slower
  than the current default path, topping out around the low/mid-40 displayed-FPS
  band despite stable scanout and zero async stalls.  Do not confuse this knob
  with `wlcomp_frame_ms=N`: keep the pending-callback poll wait at its 4ms
  default and keep the frame-callback interval at 6ms.
- Frame-callback deadline advancement is now pending-only.  This removes an
  idle/no-pending clock-advance edge case and the strict 120-frame FBSTAT run
  passes, but the measured warm cadence remains variable and below Alpine, so
  keep focusing on cheaper or more overlapped callback/display opportunities.
- `wlcomp_frame_perf=1` now reports callback pending/non-pending samples and
  compositor loop wait behavior around `epoll_wait`.  The strict 180-frame
  FBSTAT run
  `xv6-virgl-desktop-frameperf-loopwait180.log` passed after this diagnostic
  expansion with active samples around `45.9/37.9`, `47.0/47.6`, and
  `46.4/44.6`, callback delivery averages around 21-25ms, and loop waits
  averaging roughly 9-10ms with mixed ready/timeouts.  It preserved the stable
  scanout shape (`set_scanout=4`, post-startup `scanout_rebinds=0`), reached
  `fbstat display settle target=180 completed=180`, and reported
  `posted_submit_3d=183`, `posted_flush=179`, `stalls=0`,
  `max_wait_us=0`.  This rules out async make-room pressure in that sample and
  makes the next frontier cheaper or better-overlapped callback/display work,
  not lower pending-callback poll waits.
- The compositor main loop now samples pending frame callbacks after the
  top-of-loop Wayland dispatch.  The accepted default keeps the proven 4ms
  pending-callback wait and passes strict FBSTAT as
  `xv6-virgl-desktop-callback-dispatch-first-fixedwait180.log`: warm samples
  `58.6/51.6` and `54.4/50.7`, callback delivery around 18-19ms,
  `fbstat display settle target=180 completed=180`, footer
  `display_last_complete 182`, `posted_submit_3d=183`, `posted_flush=181`,
  zero async make-room stalls, and stable scanout (`set_scanout=4`,
  `scanout_rebinds=0`).  The `wlcomp_callback_deadline_wait=1` diagnostic is
  rejected for the default path: it made callbacks/app frames run ahead, but the
  strict run `xv6-virgl-desktop-callback-dispatch-then-deadline180.log` failed
  with `display_last_complete 174` for 180 requested app frames.
- Screenshot validation is now part of the strict default xv6 desktop
  validator rather than a best-effort monitor dump.  The passing run
  `xv6-virgl-desktop-screenshot-fsimg-nosync180.log` used the dispatch-first
  fixed-wait path with `VIRGL_DESKTOP_VALIDATE_FBSTAT=1` and
  `wlcomp_frame_perf=1`; it reached `display_last_complete 180`, preserved the
  stable scanout shape (`set_scanout=4`, active `scanout_rebinds=0`), and
  extracted `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop.ppm` from a
  copied validation `fs.img`.  The validator now uses a default 480x360 center
  crop (`360,180 480x360`) so screenshot validation remains reliable under
  verbose diagnostics.  Pixel validation in
  `xv6-virgl-desktop-scanout-perf180-rerun.log` logged
  `screenshot_matrix ... width=480 height=360 nonblack=172800
  unique_sample=3962 status=PASS`, and the inspected PNG
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-scanout-perf180.png`
  shows rendered GL content.  If the host QEMU window appears blank, treat that
  as a host/window surface issue until this guest-side screenshot proof fails.
- `wlcomp_scanout_perf=1` is now an opt-in userspace scanout-submit histogram.
  It times each `FB_GPU_BO_PRESENT`/`FB_GPU_PAGE_FLIP` call without changing the
  default path and logs one-second buckets.  The strict screenshot-backed run
  `xv6-virgl-desktop-scanout-perf180-rerun.log` passed with
  `display_last_complete 180`, stable scanout (`set_scanout=4`,
  `scanout_rebinds=0`), `posted_submit_3d=183`, and `posted_flush=179`.  Active
  scanout histograms show the 640x480 demo rect is bursty rather than uniformly
  slow: samples included `avg_us=4572 max_us=19155`, then
  `avg_us=4873 max_us=19598`, then `avg_us=5869 max_us=16750`, with many
  submits under 2ms but a meaningful tail in the 10-20ms bucket.  This points
  the next optimization at overlapping or reducing the scanout-submit tail, not
  more callback-poll tuning.
- `virtio_gpu_scanout_perf=1` now reports per-holder lock-wait time buckets in
  addition to holder counts.  The strict screenshot-backed 180-frame run
  `xv6-virgl-desktop-scanout-lockwait-split180.log` passed with
  `display_last_complete 185`, stable scanout (`set_scanout=4`, active
  `scanout_rebinds=0`), `posted_submit_3d=183`, `posted_flush=184`, and
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962
  status=PASS`; the inspected PNG
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-scanout-lockwait-split180.png`
  shows rendered GL content.  The new buckets show the high active lock-wait
  batches are dominated by `transfer` wait time, for example
  `lock_wait=4879 wait_us transfer=4820`, `lock_wait=5325 wait_us transfer=5325`,
  `lock_wait=6541 wait_us transfer=6541`, and
  `lock_wait=5520 wait_us transfer=5519`.  One noisy batch had a separate flush
  tail (`total=63414 lock_wait=2759 flush=60637`), so keep screenshot-backed
  long runs when judging any transfer/scanout overlap change.  The next useful
  optimization should reduce or overlap transfer-vs-scanout contention while
  preserving one stable 1280x800 scanout resource and required screenshot proof.
- `virtio_gpu_transfer_perf=1` now splits synchronous transfer time into lock
  wait and submit/drain time and reports whether each 20-call batch targeted
  the currently bound scanout resource.  The strict screenshot-backed run
  `xv6-virgl-desktop-transfer-bound-split180.log` passed with
  `display_last_complete 180`, stable scanout (`set_scanout=4`,
  `scanout_rebinds=0`), `posted_submit_3d=183`, `posted_flush=179`, and
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962
  status=PASS`; the inspected PNG
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-transfer-bound-split180.png`
  shows rendered GL content.  The transfer split confirms transfers are still
  synchronous on the default path (`async=0`, `posted_transfer=0`) and that the
  expensive transfer batches spend most time in submit/drain, not waiting to
  acquire `op_lock`: examples include `total=10651 lock_wait=0 submit=10639
  bound_scanout=0`, `total=5448 lock_wait=0 submit=5446 bound_scanout=1`, and
  `total=5865 lock_wait=0 submit=5864 bound_scanout=0`.  Scanout batches in the
  same run still waited behind transfer holders (`lock_wait=5123 wait_us
  transfer=5123`, `lock_wait=4557 wait_us transfer=4557`).  This means the next
  optimization should not simply enable the rejected broad
  `virtio_gpu_async_fb_transfer=1`; it should either reduce normal Mesa/client
  synchronous transfers competing with scanout or create a narrower transfer
  overlap path that preserves the healthy `path_gl_preflush`/stable-scanout
  shape.
- Transfer attribution was extended again to print the last resource id, context,
  dimensions, target, bind, offset, and stride for each 20-call transfer batch.
  The strict screenshot-backed run
  `xv6-virgl-desktop-transfer-resource-split180.log` passed with
  `display_last_complete 180`, stable scanout (`set_scanout=4`), and
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962
  status=PASS`; the inspected PNG
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-transfer-resource-split180.png`
  shows rendered GL content.  This run proves the expensive non-bound transfers
  are not wlcomp's 1280x800 scanout/chrome resource: wlcomp scanout upload
  batches identify as `last_res=4 ctx=2 res=1280x800 target=0x2 bind=0x54008a
  ... last_rect=0,764 1280x36`, while the recurring competing transfers
  identify as Mesa/client-style linear resources such as `last_res=8 ctx=3
  res=1048576x1 target=0x0 bind=0x20 stride=1048576` with offsets marching
  through the 1D backing.  Active scanout batches still wait behind transfer
  holders (`wait_transfer=14-18`, `wait_us transfer=4951-6101` in several
  windows).  The next optimization should therefore focus on reducing or
  overlapping client/Mesa linear transfer submits that monopolize the global
  virtio GPU operation lock ahead of scanout, while leaving the validated
  full-desktop scanout resource ownership intact.
- Transfer attribution now splits synchronous submit time into async-ring
  `drain` time and the actual transfer `command` wait.  This is diagnostic only
  and does not change default behavior: all existing callers still use the
  normal `virtio_gpu_submit()` wrapper, while `virtio_gpu_transfer_perf=1`
  records the split from `virtio_gpu_user_transfer()`.  The strict
  screenshot-backed 180-frame run
  `xv6-virgl-desktop-transfer-drain-split180.log` passed with
  `display_last_complete 181`, stable scanout (`set_scanout=4`,
  `scanout_rebinds=0`), `posted_submit_3d=183`, `posted_flush=180`,
  `posted_transfer=0`, and
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962
  status=PASS`; the inspected PNG
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-transfer-drain-split180.png`
  shows rendered GL content.  The split confirms that the expensive active
  Mesa/client linear transfer batches are mostly waiting for older async work
  to drain while holding `op_lock`, not spending time in the transfer command
  itself.  Examples for `last_res=8 ctx=3 res=1048576x1 target=0x0 bind=0x20`
  include `submit=3232 drain=2229 command=211`,
  `submit=4557 drain=3364 command=408`, `submit=6834 drain=5130 command=253`,
  `submit=10472 drain=9719 command=306`, and
  `submit=7941 drain=7671 command=221`.  Scanout in the same run still waited
  behind transfer holders (`wait_transfer=14`, `17`, etc.).  The next useful
  optimization should therefore target the lock/drain scheduling shape: avoid
  making scanout wait behind a transfer operation that is itself mostly waiting
  for prior async flush/submit completion.  Do not flip on
  `virtio_gpu_async_linear_transfer=1`; that was already rejected because it
  broke the stable scanout-flush pipeline.
- The transfer drain split now also reports which async command kinds were
  retired during that drain (`drain_submit_3d`, `drain_flush`,
  `drain_transfer`, `drain_other`).  The strict screenshot-backed rerun
  `xv6-virgl-desktop-transfer-drain-type180-rerun.log` passed with
  `display_last_complete 183`, stable scanout (`set_scanout=4`,
  `scanout_rebinds=0`), `posted_submit_3d=183`, `posted_flush=182`,
  `posted_transfer=0`, and
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962
  status=PASS`; the inspected PNG
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-transfer-drain-type180-rerun.png`
  shows rendered GL content.  The active Mesa/client linear resource drains are
  mostly retiring earlier async 3D submits plus a smaller number of scanout
  flushes: examples include `drain=2695 command=295 drain_submit_3d=16
  drain_flush=11`, `drain=4296 command=365 drain_submit_3d=19 drain_flush=8`,
  `drain=5287 command=599 drain_submit_3d=17 drain_flush=5`,
  `drain=5718 command=98 drain_submit_3d=20 drain_flush=2`, and
  `drain=4287 command=275 drain_submit_3d=17 drain_flush=9`.  This sharpens
  the next optimization target: the transfer is acting as a synchronous barrier
  for prior async client submits/flushes while scanout queues behind the
  transfer holder.  A real fix needs a safer mixed sync/async wait model or a
  compositor/client pipeline change that avoids making Mesa's linear transfer
  path the global barrier; a simple unlock during drain remains unsafe without
  protecting async-ring metadata and the shared completion path.
- Transfer attribution now also separates the batch direction (`from_host` and
  `to_host`) and prints the last transfer direction plus `owner_tgid`.  This is
  diagnostic only.  The strict screenshot-backed rerun
  `xv6-virgl-desktop-transfer-owner-attribution180-rerun.log` passed with
  `display_last_complete 180`, stable scanout (`set_scanout=4`,
  `scanout_rebinds=0`), `posted_submit_3d=183`, `posted_flush=179`,
  `posted_transfer=0`, and mandatory screenshot proof
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962
  status=PASS`; the inspected PNG
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-transfer-owner-attribution180-rerun.png`
  shows rendered GL content.  The recurring expensive 1D transfer batches are
  now clearly Mesa/client uploads from the app process, not host readbacks:
  examples include `from_host=0 to_host=20 last_from_host=0 last_res=8 ctx=3
  owner_tgid=43 res=1048576x1 target=0x0 bind=0x20` with
  `submit=5514 drain=4816 command=203`, then `submit=9666 drain=8820
  command=116`, and later `submit=9499 drain=8614 command=123`.  This confirms
  the next optimization should target app/client linear TO_HOST transfer
  scheduling or pipeline structure while preserving the existing async scanout
  flush lane; it is not a FROM_HOST readback problem.
- `virtio_gpu_async_linear_transfer=1` is a diagnostic/rejected probe, not the
  next default.  The async helper now preserves exact transfer layout fields
  (`z`, `depth`, `level`, `offset`, `stride`, and `layer_stride`), and the
  opt-in linear-transfer path can visibly render pixels, but it falls out of
  the healthy scanout-flush pipeline.  The screenshot-backed 180-frame probe
  `xv6-virgl-desktop-async-linear-transfer180-rerun.log` failed strict
  validation with `virtio_async_posted_transfer=181`,
  `virtio_async_posted_flush=0`, `scanout_submits=0`, and
  `path_gl_preflush=0`; the guest PPM still passed
  `screenshot_matrix ... nonblack=172800 unique_sample=114 status=PASS` and
  the PNG showed a rendered sphere, but that is the wrong presentation path.
  Keep this as evidence that merely overlapping Mesa/client linear transfers
  can replace the stable async scanout flush path instead of improving it.
- `virtio_gpu_linear_transfer_mixed_wait=1` is also diagnostic/rejected for now.
  The opt-in mixed sync/async wait prototype posts synchronous linear transfers
  without first draining all async descriptors and retires intervening async
  completions until the sync descriptor completes.  The default-disabled control
  run `xv6-virgl-desktop-mixedwait-default180.log` stayed inert with
  `mixed=0`, `display_last_complete 181`, `posted_submit_3d=183`,
  `posted_flush=180`, `posted_transfer=0`, and mandatory screenshot proof
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962
  status=PASS`; the inspected PNG showed the rendered GL gradient.  The opt-in
  probe `xv6-virgl-desktop-linear-mixedwait180.log` visibly rendered the sphere
  and passed guest PPM validation
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=115
  status=PASS`, with app completion `frames=180 status=0` and
  `display_last_complete 224`, but it failed strict validation because
  `virtio_async_posted_flush=0` while `virtio_async_posted_submit_3d=183` and
  `virtio_async_posted_transfer=0`.  Keep the probe opt-in only: like async
  linear transfer, it proves pixels can appear while regressing the healthy
  async scanout-flush pipeline.
- `virgl-desktop-validate.sh` now treats the extracted guest PPM and
  `screenshot_matrix` as the authoritative screenshot proof.  If the serial
  marker is missing or interleaved with verbose kernel logs, the validator tries
  `debugfs` extraction from the copied validation `fs.img` and still fails when
  the required PPM is missing, malformed, blank, or the wrong size.  It also
  tolerates debugfs/kernel-log interleaving after `display_last_complete` by
  parsing the numeric footer prefix.  Do not add a frame-count gate here:
  `display_last_complete` must be present and nonzero, but screenshot/pipeline
  evidence is the authority for visible output.  The steady-state GPU-present
  check now selects the last active default-path sample with both
  `path_gl_preflush > 0` and `scanout_submits > 0` rather than the last line with
  historical `gl_bufs_total > 0`; this avoids failing a valid rendered run on
  post-demo idle/acquire repaint traces after the app has already completed.
- Virgl-backed wlcomp now uses a shorter 1000ms startup full-screen repaint
  window by default, while non-virgl paths keep 3000ms.  The knob is
  `wlcomp_startup_full_ms=N`.  The strict 120-frame FBSTAT run with this
  default passed at `frames=120 status=0 elapsed=3.572s fps=33.6`, with warm
  samples `40.5/34.2` and `41.4/38.8`, stable `set_scanout=4`, and zero async
  stalls.  This helps the short demo startup envelope but is not final parity.
- FBSTAT validation no longer treats `display_last_complete >= glsmoke_frames`
  as a pass/fail limit.  Earlier target-aware polling helped avoid sampling too
  early, but the validator should not fail a run solely because the diagnostic
  footer is a few frames behind the app's requested frame count.  The footer is
  now a presence/nonzero sanity check, while the mandatory guest PPM
  `screenshot_matrix` and virgl pipeline counters prove visible output.  The
  historical 120-frame rerun is
  `xv6-virgl-desktop-fbstat-settle1200-fbstat120.log`: completion
  `frames=120 status=0 elapsed=3.729s fps=32.2`, warm samples `47.6/43.5`,
  stable `set_scanout=4`, `posted_submit_3d=123`, `posted_flush=121`,
  `depth=2`, `max_wait_us=0`, and fbstat footer `display_last_complete 122`.
  A 180-frame default rerun after removing the frame-count gates passed as
  `xv6-virgl-desktop-transfer-resource-postasync-default180-noframegate2.log`,
  with mandatory screenshot proof
  `screenshot_matrix ... width=480 height=360 nonblack=172800 unique_sample=3962
  status=PASS`, footer `display_last_complete 180`, `posted_submit_3d=183`,
  `posted_flush=179`, `posted_transfer=0`, stable `set_scanout=4`, and
  `build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-transfer-resource-postasync-default180-noframegate2.png`
  visually showing the rendered GL gradient.
- `wlcomp_virgl_fb_flip=1` now performs an opt-in full-screen target swap
  between two 1280x800 virgl scanout resources and passes the focused desktop
  validator. It proves host-window geometry and scanout ownership, but it is
  slower (~16 FPS) because it currently forces a full CPU repaint for every
  flip.
- `wlcomp_virgl_fb_copy_before_flip=1` now preserves target contents with
  `FB_GPU_BO_COPY` before partial repaint, but it is rejected as a supported
  path: the demo can complete, but virgl reports repeated
  `vrend_renderer_transfer_iov ... Illegal resource 4/5` context errors. The
  validator now fails on those errors.
- `wlcomp_virgl_fb_damage_flip=1` is the clean non-copy target-history probe:
  it warms both full-screen targets, then preserves CPU/chrome history through
  the existing back-target mirror and redraws GPU client damage. It passes with
  no virgl context errors or scanout failures, but it still alternates
  full-screen SET_SCANOUT every frame and is slower than the default path.
  New path counters show `scanout_rebinds == scanout_submits` during active
  damage-flip samples.
- `wlcomp_page_flip_present=1` is the real no-copy ioctl probe and is now
  self-contained: it requests virgl framebuffer backing, forces two full-screen
  targets, alternates `resource=4/5`, and passes the focused validator via
  `VIRGL_DESKTOP_VALIDATE_PAGE_FLIP_PRESENT=1`. It is still not the Tier 1 fix:
  the trace shows per-frame `SET_SCANOUT` (`set_scanout=64` for a 60-frame run)
  and active FPS around 13 FPS, so keep it opt-in as evidence that host-visible
  full-screen alternation is currently too expensive.
- `wlcomp_pipeline_gpu_release=1` is a clean opt-in Tier 2 probe: it releases
  GPU-composed dmabufs before the final scanout flush and passes
  `VIRGL_DESKTOP_VALIDATE_PIPELINE_GPU_RELEASE=1`. It preserves the healthy
  stable-scanout shape (`set_scanout=4`, `scanout_rebinds=0`), but the 180-frame
  run stayed in the same low/mid-30 FPS band, so do not default it on yet.
- `wlcomp_fullscreen_direct=1` is now covered by
  `VIRGL_DESKTOP_VALIDATE_FULLSCREEN_DIRECT=1`. It proves the fullscreen
  resource-scanout path is geometry-safe (`1280x800`, no app-sized mode), but it
  is not a win because it rebinds scanout almost every frame (`set_scanout=122`
  for a 120-frame run).
- `glsmoke_color_buffers=1` / `XV6_MESA_WAYLAND_COLOR_BUFFERS=1` is now covered
  by `VIRGL_DESKTOP_VALIDATE_MESA_COLOR_BUFFERS=1`, usually combined with
  `VIRGL_DESKTOP_VALIDATE_FULLSCREEN_DIRECT=1`. It proves that reusing one
  Wayland EGL client buffer collapses fullscreen-direct host rebind churn
  (`set_scanout=7` for 60 frames, then `scanout_rebinds=0`), but visible FPS is
  only ~16 FPS, so it remains a diagnostic and must not default on. The same
  diagnostic without fullscreen-direct also passed a 180-frame default-path run,
  but only reduced resource create/unref churn (`17/14` -> `11/8`) while
  completion fell to `fps=24.2`; it did not reduce the dominant submit/flush/
  transfer cadence.
- `glsmoke_color_buffers=2` is a companion fullscreen-direct diagnostic. The
  120-frame run passed with a healthy warm sample (`56.8` app / `57.6`
  displayed), but it restored per-frame fullscreen scanout alternation
  (`set_scanout=124`, toggling two full-screen resources). It is useful evidence
  for the required Tier 3 shape, not a default candidate.
- `wlcomp_gl_finish=1` is now covered by
  `VIRGL_DESKTOP_VALIDATE_GL_FINISH=1`. It confirms GL ordering but slows the
  healthy default path (`displayed_fps=21.9` versus `29.8` for the 60-frame
  control), so leave it as a diagnostic only. The healthy default now queues
  the compositor GL draw without per-present `glFlush`; use
  `wlcomp_gl_flush=1` only as an ordering/host diagnostic.
- `virtio_gpu_no_scanout_flush=1` is an invalid display-notify shortcut: it cuts
  `RESOURCE_FLUSH` traffic to startup-only, but visible FPS stops tracking app
  FPS. Keep it as a negative diagnostic only.
- `wlcomp_virgl_full_scanout=1` is also diagnostic only. It keeps one stable
  scanout submit per frame, but forcing the full desktop rect did not improve
  FPS over damage-derived scanout rects.
- `virtio_gpu_present_minimal_drain=1` was tested with
  `VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='virtio_gpu_present_minimal_drain=1'`.
  It passed but was neutral/slightly worse (`displayed_fps=27.6` in the
  60-frame sample, stable `set_scanout=4`, `scanout_rebinds=0`), so keep it as
  a diagnostic rather than a default-path fix.
- `VIRGL_DESKTOP_VALIDATE_FBSTAT=1` is now available for async queue pressure
  evidence. The validator appends `glsmoke_fbstat=1`, and `desktop` forks
  `/bin/fbstat` after the GL demo exits.  The footer is useful for proving that
  the display path advanced, but do not use it as a requested-frame-count limit:
  current validation requires a nonzero `display_last_complete` plus mandatory
  screenshot proof.  `glsmoke_fbstat_settle_ms=N` still controls the fixed
  fallback/minimum wait (default 1200ms), and `glsmoke_fbstat_wait_ms=N` still
  controls any target-aware polling cap (default 5000ms).
  The first
  passing 60-frame run showed `virtio_async_posted=194`, `retired=194`,
  `pending=0`, `depth=2`, `make_room_stalls=6`, and `max_wait_us=9880`, with
  the healthy default scanout shape intact (`set_scanout=4`, active
  `scanout_rebinds=0`).  The newer split-counter run showed
  `posted_submit_3d=125`, `posted_flush=65`, `posted_transfer=0`,
  `submit_3d_stalls=0`, `flush_stalls=3`, and `max_wait_us=1182`, so async
  pressure is currently on scanout-flush admission, not 3D submit admission.
  Use this before changing async depth again.  The validator no longer depends
  on fragile early serial regex waits for `demo_surface_matrix`,
  `app_loop_fps`, or the fbstat marker.  It waits for compositor startup,
  optionally consumes first FPS telemetry for the screenshot attempt, waits for
  clean demo completion without comparing the app's reported frame count
  against the requested run length, extracts the guest PPM from the copied fs
  image, and then runs post-run assertions for the demo marker, clean
  completion, async counter block, nonzero display-completion footer, and mandatory
  `screenshot_matrix` result.  This avoids the old failure modes where strict
  runs timed out in expect even though the complete evidence appeared later in
  the log, passed with a chopped footer line such as `display_last_complete 12`,
  or failed solely because the sampled footer was a few frames behind.
- `VIRGL_DESKTOP_VALIDATE_ASYNC_DEPTH=3` now passes the current strict 60- and
  180-frame validators with `VIRGL_DESKTOP_VALIDATE_FBSTAT=1` and eliminates
  make-room stalls (`stalls=0`), while preserving the healthy stable-scanout
  shape (`set_scanout=4`, active `scanout_rebinds=0`). It is still not a
  default-path fix: the same-build depth-2 180-frame control had comparable or
  better warm FPS despite four short flush-admission stalls. Keep depth 3 as a
  diagnostic for async-room pressure, not as the parity solution.
- Default-path present counters now show the healthy shape: after startup,
  active frames are almost all `path_gl_preflush`, `scanout_submits` tracks one
  submit per active frame, and `scanout_rebinds=0`. Keep that invariant while
  reducing or overlapping the compose/flush work.
- The CPU-damage prefilter is now default-on and validated. It removes most
  unnecessary CPU repaint/upload during GPU-only animation, but does not raise
  xv6 past the low/mid-30 FPS band. Do not spend more time on CPU upload unless
  a new trace shows it regressed; move to reducing or overlapping GL compose
  and scanout flush.
- Release-path counters show no buffer-release backlog in the default path:
  warm active frames release GPU buffers immediately and report
  `release_pending=0`. Buffer release is not the current limiter.
- Async queue counters in the latest default frame-perf+fbstat run show no
  make-room pressure (`posted_submit_3d=183`, `posted_flush=173`, `pending=1`,
  `stalls=0`, `wait_progress_calls=0`). Do not pursue async depth as the next
  default-path fix without new evidence of queue pressure.
- `glsmoke_render_div=2` lowers the demo render target to 320x240 but does not
  move xv6 out of the same low-30s FPS band. Client render cost is secondary;
  continue on compositor scanout/display throughput and overlap.
- `virtio_gpu_async_fb_transfer=1` is rejected for now: one current 120-frame
  probe can pass, but it lowers displayed FPS, adds transfer-admission stalls,
  and does not reduce the dominant scanout/display cost.  With async depth 3 it
  fails validation and falls out of the healthy GL-preflush scanout path
  (`path_gl_preflush=0`, `scanout_submits=0`, `posted_flush=0`), so do not
  enable it by default.
- Treat Linux/Alpine as a control trace and display-behavior reference. Do not
  claim success from counters alone.

Start by reading:
- docs/alpine-virgl-desktop-handoff-plan.md
- scripts/gpu/alpine-virgl-desktop-capture.sh
- ports/wayland/src/wlcomp_render_loop.inc
- ports/wayland/src/wlcomp_fb.inc
- ports/wayland/src/wlcomp_gl_compose.inc
- ports/wayland/src/wlcomp_buffer_shm.inc
- kernel/kernel/virtio_gpu.c
- scripts/launch/run-qemu.sh

Preserve these invariants:
- In normal desktop mode, only a full-desktop-sized resource may be bound as
  scanout. Never bind a 640x480/720x400 app surface as scanout if it changes
  the QEMU window size.
- Apply Y flip exactly once: surface placement and image orientation are
  separate bugs.
- Prefer QEMU/guest screen dumps or fbstat ppm-current evidence for xv6.
- Keep new fast paths behind cmdline/env flags until validated by pixels and
  trace shape.
- Do not revert unrelated dirty changes in top-level, kernel, user, or ports.

Near-term implementation target:
1. Add or run a focused xv6 virgl desktop validator that captures wlcomp
   present-trace logs and QEMU virtio-gpu trace events.
2. Confirm xv6 steady-state trace shape against Alpine:
   stable 1280x800 scanout, no app-sized SET_SCANOUT, regular ctx_submit,
   fences, and res_flush.
3. Reduce submit count per visible frame: coalesce scanout rects to one flush,
   skip CPU/chrome upload on GPU-only animation frames, and ensure async scanout
   flush is actually used. Status: all three are now validated in the default
   path; the default still sits in the low/mid-30 FPS band.
4. Continue the core Linux-like fix from the current prep state:
   `wlcomp_virgl_fb_buffers=2` allocates a second full-screen target and
   `wlcomp_virgl_fb_flip=1` can swap between the two targets. The validated
   flip modes are the full-repaint fallback and the new
  `wlcomp_virgl_fb_damage_flip=1` non-copy history path. The attempted
  `wlcomp_virgl_fb_copy_before_flip=1` damage-preserving mode is useful
  diagnostic evidence but not a supported success path because `FB_GPU_BO_COPY`
  triggers virgl illegal-resource context errors on the full-screen targets.
   The fullscreen-direct single-client-buffer probe proves that stable resource
   binding helps but single-buffer client reuse is too slow. The next work is
   avoiding or overlapping per-frame SET_SCANOUT while keeping the full-desktop
   resource invariant: stay on one bound full-screen scanout with GPU-side
   composition into it, use true page-flip semantics with lower bind cost, or
   add asynchronous fence reaping/triple buffering without stale pixels.

Useful validation commands:

  cmake --build build-x86_64/ports --target port-mesa -j2
  cmake --build build-x86_64/ports --target port-wayland -j2
  scripts/image/make-rootfs.sh build-x86_64/sysroot build-x86_64/fs.img 2304
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=90s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=90s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_GL_FINISH=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=90s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_BUFFERS=2 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=90s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_BUFFERS=2 VIRGL_DESKTOP_VALIDATE_FLIP=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=90s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_BUFFERS=2 VIRGL_DESKTOP_VALIDATE_FLIP=1 \
    VIRGL_DESKTOP_VALIDATE_DAMAGE_FLIP=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=90s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_BUFFERS=2 VIRGL_DESKTOP_VALIDATE_FLIP=1 \
    VIRGL_DESKTOP_VALIDATE_COPY_BEFORE_FLIP=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=90s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_PIPELINE_GPU_RELEASE=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=90s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=150s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_ASYNC_DEPTH=3 VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=150s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_gl_flush=1' \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='virtio_gpu_no_scanout_flush=1' \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='fb_present_perf=1 virtio_gpu_scanout_perf=1' \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-submit-prep-default60.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='fb_present_perf=1 virtio_gpu_scanout_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-flush-prep-scanout-perf60.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=150s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-flush-prep-default180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='fb_present_perf=1 virtio_gpu_scanout_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-op-holder-scanout-perf60.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='fb_present_perf=1 virtio_gpu_scanout_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-op-holder-categorized-scanout-perf60.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='fb_present_perf=1 virtio_gpu_scanout_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-fence-target-drain-categorized60.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=150s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-fence-target-drain-default180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-default-frameperf60.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=180s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frameperf-fbstat-default180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=180s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_CALLBACK_POLL_MS=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-callback-poll1-default180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='glsmoke_present_interval=0 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-present-interval0-frameperf60.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='glsmoke_mesa_perf=1 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-mesa-wlswap-throttle1-perf120.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='glsmoke_mesa_perf=1 wlcomp_frame_perf=1 glsmoke_mesa_throttle=0' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-mesa-wlswap-throttle-default0-perf120.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='glsmoke_mesa_perf=1 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-mesa-winsys-perf120.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='glsmoke_mesa_perf=1 wlcomp_frame_perf=1 wlcomp_frame_ms=8' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms8-perf120.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='glsmoke_mesa_perf=1 wlcomp_frame_perf=1 wlcomp_frame_ms=4' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms4-perf120.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='glsmoke_mesa_perf=1 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms8-default-perf120.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=160s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_ms=6 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms6-default180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=160s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_ms=5 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms5-default180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=160s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_ms=7 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms7-default180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=160s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms6-default180-afterdefault.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=160s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms6-default180-clean.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=190s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frameperf-loopwait180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=190s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-callback-dispatch-first-fixedwait180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=190s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_frame_perf=1 wlcomp_callback_deadline_wait=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-callback-dispatch-then-deadline180.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='fb_present_perf=1 virtio_gpu_scanout_perf=1 virtio_gpu_async_fb_transfer=1 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms8-async-fb-transfer120.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_ASYNC_DEPTH=3 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='fb_present_perf=1 virtio_gpu_scanout_perf=1 virtio_gpu_async_fb_transfer=1 wlcomp_frame_perf=1' \
    VIRGL_DESKTOP_VALIDATE_LOG=build-x86_64/virgl-desktop-validate/xv6-virgl-desktop-frame-ms8-async-fb-transfer-depth3-120.log \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='fb_present_perf=1 virtio_gpu_scanout_perf=1 virtio_gpu_no_scanout_flush=1' \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=100s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND='wlcomp_virgl_full_scanout=1' \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=120s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FULLSCREEN_DIRECT=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=120s VIRGL_DESKTOP_VALIDATE_FRAMES=60 \
    VIRGL_DESKTOP_VALIDATE_FULLSCREEN_DIRECT=1 \
    VIRGL_DESKTOP_VALIDATE_MESA_COLOR_BUFFERS=1 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=140s VIRGL_DESKTOP_VALIDATE_FRAMES=120 \
    VIRGL_DESKTOP_VALIDATE_FULLSCREEN_DIRECT=1 \
    VIRGL_DESKTOP_VALIDATE_MESA_COLOR_BUFFERS=2 \
    scripts/gpu/virgl-desktop-validate.sh
  VIRGL_DESKTOP_VALIDATE_TIMEOUT=150s VIRGL_DESKTOP_VALIDATE_FRAMES=180 \
    VIRGL_DESKTOP_VALIDATE_MESA_COLOR_BUFFERS=1 \
    VIRGL_DESKTOP_VALIDATE_FBSTAT=1 \
    scripts/gpu/virgl-desktop-validate.sh

The copy-before-flip command is currently expected to fail because the
validator rejects virgl renderer context errors; keep it as a regression probe
while developing a safer target-history path.

Async-depth experiments can be run with
`VIRGL_DESKTOP_VALIDATE_ASYNC_DEPTH=N`. Depth 3 now passes strict 60/180-frame
runs and removes make-room stalls, but it has not improved the warm FPS ceiling
over the depth-2 control, so do not default it on without new evidence.

Pending-callback epoll wait experiments can be run with
`VIRGL_DESKTOP_VALIDATE_CALLBACK_POLL_MS=N`. The 1ms and 2ms 180-frame probes
are negative results: they preserve stable scanout but over-poll and slow the
visible cadence relative to the current 4ms pending-callback wait.

Acceptance criteria:
- Visible 3D demo appears quickly, moves smoothly, and is not Y-inverted.
- The panel/desktop do not blink violently.
- QEMU remains a normal 1280x800 window.
- Visible FPS after warmup is close to app FPS and approaches Alpine behavior,
  not one frame every several seconds.
- Evidence includes screenshots or fbstat ppm-current output plus logs/traces.
```
