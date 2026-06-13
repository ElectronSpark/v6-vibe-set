# Linux DRM / GPU Graphics ABI Compatibility Plan

Last updated: 2026-06-12 (rewritten as a checklist; the full per-item
narratives, gap analyses, and closure evidence live in git history and
`docs/linux-drm-abi-audit.md`).

**Status: the core DRM-ABI convergence work is complete for this host; the
2026-06-11 §13 active closure queue is closed.** Phases 0–6 are landed and
committed, all validators pass, stock Mesa/GBM/libdrm and upstream Weston
drive the kernel through the standard Linux UAPI, and the runtime crutches
are deleted. The new active focus is **Linux GUI ABI compatibility, with X11
and XWayland first**. Host GUI programs are probes for missing kernel/ABI
behavior, not deliverables to port as applications; app-specific WebKit,
YouTube, and Chromium work is stress backlog unless it reduces to a concrete
minimal Linux ABI defect.

**VM re-verification 2026-06-12 (current image):**

- [x] `drmabitest` full suite under virgl — `DRMABI-KCMP-PASS card0=1
      renderD128=1`, zero FAIL/mismatch lines; caps=behavior held (virgl boot
      reports `RESOURCE_BLOB=0` and blob create honestly fails `-EOPNOTSUPP`).
- [x] Headless non-GL `virtio-gpu` blob probe — `GETPARAM(RESOURCE_BLOB)=1`,
      fail-closed `HOST_VISIBLE=0`, real guest blob creates on
      card0/renderD128, host-visible probe `skipped=1`, nonzero `fb0:sample`.
- [x] §8 step-7 fullscreen-video gate — latest post-image run
      `expect scripts/gpu/perf-video-gate.expect` on 2026-06-12:
      `RESULT pass fps=59.8 speed=1.004 decodedFPS=59.8 dropPct=0.00
      advanced=15.34`, `__WEBKIT_API_SMOKE_DONE_0__`, with durable frame
      proof at `build-x86_64/perf-video-gate/perf-video-frame.ppm/.png`.
- [x] GUI-session baseline clean — `dma_fence: selftest ok`, card0 +
      renderD128 registered, virgl capsets 1+2, Weston desktop with 19
      entries on the current image, zero async-timeout/EIO/panic markers in
      the gate baseline. Earlier imported `eglgears_wayland` /
      `host-es2gears-wayland` black-box attempts remain negative evidence
      for those clients only; the imported WLEGL smoke proof below is now the
      passing GL/EGL/Wayland representative.

## Goal

Make the xv6-os graphics subsystem compatible with the **Linux DRM/GPU kernel
ABI** so that unmodified Linux userland graphics stacks — `libdrm`, Mesa,
`libgbm`, `libEGL`, Wayland compositors — drive the kernel through the same
ioctl contract they use on Linux:

- the same device-node layout (`/dev/dri/card0`, `/dev/dri/renderD128`);
- the same `DRM_IOCTL_*` numbers, struct layouts, and field semantics;
- the same object models (per-file GEM handles, dumb buffers, PRIME dma-buf,
  DRM syncobj / `dma_fence`, KMS atomic state);
- the same `virtio-gpu` UAPI (`DRM_IOCTL_VIRTGPU_*`) so stock Mesa binds
  without xv6-specific shims.

Scope is **x86_64** (consistent with the syscall ABI plans); RISC-V graphics
is out of scope.

---

## 1. Phase checklist (all landed and committed)

- [x] **Phase 0 — audit + `drmabitest`.** `GET_CAP` reconciled with behavior;
      probe at `user/programs/drmabitest`; baseline in
      `docs/linux-drm-abi-audit.md`.
- [x] **Phase 1 — `dma_fence` core + syncobj/sync_file.** `dev/fb/dma_fence.c`;
      `dma_fence: selftest ok` at boot; exported fds are real `sync_file`s;
      `SYNCOBJ_EVENTFD` signals.
- [x] **Phase 2 — per-file GEM + FLINK/OPEN + dma-buf.** Per-file handle
      tables with global BO refcount; `fb_gem_flink`; generic dma-buf wrapper
      ops + `mmap`; PRIME import creates a new per-file handle.
- [x] **Phase 3 — KMS atomic + blobs + cursor + vblank.** Writable
      `CREATEPROPBLOB`; atomic check/commit with publish-after-present and
      real out-fences; cursor plane on the virtio cursor queue;
      present-driven vblank and `CRTC_*_SEQUENCE`.
- [x] **Phase 4 — standard virtio-gpu UAPI.** `EXECBUFFER` honors BO list +
      in/out fences; per-resource `WAIT`; virtgpu→PRIME bridge; validated with
      stock Mesa `virgl_drm_winsys` on `renderD128`.
- [x] **Phase 5 — blob resources (transfer model complete; host-visible
      zero-copy optional/host-refused).** `F_RESOURCE_BLOB` +
      `RESOURCE_CREATE_BLOB`, SHM-cap/BAR discovery, `MAP_BLOB`/`UNMAP_BLOB`,
      checked PFNMAP mmap, dirty-rect flushes; init-time map probe proves the
      rutabaga host refuses mappable HOST3D (`create=-5`), so
      `GETPARAM(HOST_VISIBLE)` stays fail-closed `0`. Alpine 3.23.4 on this
      host runs the classic transfer model at 66–73 FPS with zero
      blob/map-blob commands, so zero-copy is an optimization, not a blocker.
- [x] **Phase 6 — structural cleanup.** Unified `fb_shmem_*` allocator;
      KMS/virtgpu file splits; `fb_bo_shmem_dmabuf.c` rename; retained
      `FB_GPU_TTM_*` labels documented as compatibility metadata.

**Validation evidence summary:** Mesa virgl `gpu-validate` (dmabuf import OK,
`virtio_failures 0`, `virtio_timeouts 0`); libdrm `modetest`/`drmdevice`
against both nodes; upstream `drm_info`; upstream `kmscube` (EGL 1.5 / GLES
3.1, `virgl (D3D12 …)`); direct KMS GBM/EGL (`mesakmsgl`, ~81 FPS);
damage-aware resource-bind scanout with on-screen proof.

---

## 2. Kernel implementation map

Files under `kernel/kernel/`:

| Area | File(s) |
|---|---|
| DRM generic core (device/file/auth/magic/dispatch) | `dev/drm_core.c`, `inc/dev/drm_core.h` |
| DRM UAPI numbers/structs | `inc/uabi/drm.h` |
| Node registration (`card0`, `renderD128`, `gpu0`, `fb0`) | `dev/fb/fb_init_panic.c` |
| KMS modesetting + properties + blobs | `dev/fb/fb_drm_core_kms.c`, `dev/fb/fb_drm_kms_*.c` |
| KMS atomic + page-flip + FB lifecycle | `dev/fb/fb_kms_atomic.c` |
| Ioctl router | `dev/fb/fb_drm_dispatch.c` |
| GEM/BO + shmem + dma-buf wrapper | `dev/fb/fb_bo_shmem_dmabuf.c` |
| syncobj + PRIME + virtgpu user ioctls | `dev/fb/fb_syncobj_prime_virtgpu.c` |
| Fence fd / dma-buf file ops | `dev/fb/fb_fd_sync.c` |
| Scanout / BO→framebuffer present | `dev/fb/fb_scanout.c` |
| virtio-gpu driver (2D + 3D virgl, async ring) | `virtio_gpu.c`, `virtio_gpu_*.c` |
| Device facade ioctls (`FB_GPU_*`, internal/diagnostic) | `dev/fb/fb_device_ioctl.c` |

---

## 3. Remaining gap inventory

- [x] **Host-visible zero-copy blob — closed as host-blocked/fail-closed
      2026-06-12.** Kernel side is code-complete and fail-closed on this
      host. Proof: current `proof-drmabitest.log` shows guest
      `RESOURCE_BLOB` support with `GETPARAM(HOST_VISIBLE)=0`, real guest
      blob creation, and skipped host-visible probes on both `card0` and
      `renderD128`; `docs/linux-drm-abi-audit.md` records the rutabaga
      mappable `HOST3D` refusal as `create=-5`; same-image perf-video gate
      passed. Reopen only on a backend that actually accepts mappable HOST3D
      blobs.
- [x] **`sg_table`-equivalent scatter-list abstraction — closed
      2026-06-12.** GEM/BO metadata now carries an `fb_gpu_sg_table` view over
      shmem pages, including entry count, total length, first/base/last DMA
      addresses, and propagation through GEM copies/imported handles. Proof:
      `expect scripts/gpu/ttm-sg-table-proof.expect` with
      `ttmtest: ttm_sg_table_matrix sg_nents=16 expected=16
      total_len=65536 ... status=PASS` and `TTM-SG-TABLE-PASS`
      (`build-x86_64/ttm-sg-table-proof/run.log`). The same work fixed
      `FB_GPU_TTM_VALIDATE` per-owner handle resolution so dma-buf imports
      hit shared reservation conflicts instead of global-handle aliases.
- [x] **Multi-CRTC / hotplug / overlay planes — closed for current
      single-scanout target 2026-06-12.** The validated QEMU/virtio host
      advertises `scanouts=1` (`build-x86_64/perf-video-gate/run.log` and
      `proof-drmabitest.log`), and DRM resource enumeration honestly exposes
      one CRTC, one connector, one encoder, and two supported planes
      (primary + cursor): `GETRESOURCES ... crtcs=1 connectors=1 encoders=1`
      and `GETPLANERESOURCES ... planes=2 plane0=4 plane1=7`. Overlay planes
      and hotplug are not advertised, so no unsupported Linux KMS object is
      faked. Reopen only when testing a real multi-head/hotplug target.
- [x] **fbdev struct-layout audit (x86_64).** Closed 2026-06-11:
      `sizeof(fb_var_screeninfo)==160`, `sizeof(fb_fix_screeninfo)==80`, all
      audited offsets match Linux.
- [x] **`kcmp` syscall.** `KCMP_FILE` implemented for same-process fd
      comparison, covered by `drmabitest`.

---

## 4. Standing correctness invariants

Keep these when touching the code:

- [x] **Caps = behavior.** Never advertise a `GET_CAP`/`GETPARAM` capability
      whose ioctl path returns `-EOPNOTSUPP`; flip the cap in the same change
      that implements the feature. Current proof 2026-06-12:
      `build-x86_64/proof-drmabitest.log` shows
      `GETPARAM(RESOURCE_BLOB)=1`, `GETPARAM(HOST_VISIBLE)=0`, and
      host-visible blob creation skipped rather than advertised.
- [x] **Fail-closed, honest errno.** Unimplemented paths return the Linux
      errno a real driver would; never fake success to pass a validator.
      Current proof 2026-06-12: `proof-drmabitest.log` and
      `proof-gpu-validate.log` reject invalid virtgpu/KMS ioctls and bad
      submits without panic or success.
- [x] **Fences must signal.** Any exported fence/out-fence must be backed by a
      real completion; a never-signaling fd hangs a compositor. Current proof
      2026-06-12: `proof-gpu-validate.log` records sync fd polling,
      `virgltest` fence signaled values, display presents/completions, and
      completed `mesawlegl`/`mesaglsmoke` clients.
- [x] **Per-file handle scope.** PRIME import creates a *new* handle in the
      importing file; `GEM_CLOSE` drops only the file's reference, never the
      global BO. Current proof 2026-06-12: `proof-gpu-validate.log` has
      `gbmtest` PRIME import and `virgltest: dmabuf-resource-import ok`;
      `proof-drmabitest.log` has `DRM_PRIME_VIRTGPU_RESOURCE.valid`.

---

## 5. Validation strategy

Runtime-trace, never declare a path dead from source alone (see
`xv6-os-runtime.md` repo memory). Scale validator runs to the change: kernel
DRM/virtgpu → `drmabitest` (+ owning validator + gate); compositor/client →
affected runtime validator + gate; rootfs/assets → gate alone. Build only the
narrowest targets (§7 build matrix); never rebuild `world`/toolchain for
validation.

Validator checklist for a milestone:

- [x] `drmabitest` — every ioctl with known-good and error inputs,
      Linux-matching errno/struct output (focused `--virtgpu-only` for
      blob/virtgpu checks). Fresh proof 2026-06-12:
      `build-x86_64/proof-drmabitest.log` runs the full suite and
      `--virtgpu-only`, reaches `drmabitest: end` twice, and has zero
      `FAIL`/`mismatch`/panic markers.
- [x] libdrm conformance — `modetest`, `kmscube`, `drm_info` against
      `/dev/dri/card0`. Fresh proof 2026-06-12:
      `build-x86_64/proof-upstream-drm-tools.log` records
      `DRMINFO-CARD0-PASS`, `DRMINFO-RENDER-PASS`,
      `MODETEST-CONNECTORS-PASS`, `MODETEST-PLANES-PASS`, and
      `KMSCUBE-RENDER-PASS` with virtio_gpu/virgl renderer output.
- [x] Mesa bring-up — stock Mesa virgl through `gpu-validate`, `kmscube`,
      `virgl-kms-validate.sh` (direct KMS GBM/EGL), `mesawlegl`,
      `mesaglsmoke`. Fresh proof 2026-06-12:
      `build-x86_64/proof-gpu-validate.log`,
      `build-x86_64/proof-virgl-kms-validate.log`, and
      `build-x86_64/proof-virgl-kms-validate.png` show GBM/PRIME,
      linux-dmabuf, virgl submits/fences, direct KMS EGL, nonblank
      1280x800 rendered output, and completed `mesawlegl`/`mesaglsmoke`.
- [x] Blob/zero-copy — non-GL guest blob path: `GETPARAM(RESOURCE_BLOB)==1`,
      fail-closed `HOST_VISIBLE=0`. Fresh proof 2026-06-12:
      `proof-drmabitest.log` reports `RESOURCE_BLOB=1`, `HOST_VISIBLE=0`,
      and skipped host-visible blob probes on both `card0` and `renderD128`.
- [x] Compositor end-to-end — Weston + WebKit/GL apps; compare trace shape +
      on-screen output + framebuffer samples, never counters alone. Fresh
      proof 2026-06-12: `proof-gpu-validate.log` records Weston and GL client
      completion, `proof-webkit-virgl-gpu-validate.log` records accelerated
      WebKit WebGL ready/render/complete titles, and the mandatory gate frame
      is `build-x86_64/perf-video-gate/perf-video-frame.png`.
- [x] Regression guard — fail-closed counters, caps/behavior agreement.
      Fresh proof 2026-06-12: `proof-gpu-validate.log` records invalid-submit
      rejection, bad-submit isolation, OpenGL-submit backend separation
      `status=PASS`, and fail-closed Hyper-V/Nouveau diagnostic matrices;
      `proof-drmabitest.log` covers the current DRM ioctl error matrix.
- [x] **§8-style fullscreen-video gate (MANDATORY after any GPU/DRM/desktop
      change).** `expect scripts/gpu/perf-video-gate.expect`: the offline
      1280×800@60 H.264 clip (`rootfs-overlay/share/webkit/perf-*.mp4|html`)
      must play fullscreen at default resolution with `speed ≥ 0.9×`,
      `dropPct < 10`, zero `virtio_failures`/`virtio_timeouts`/panics, plus a
      mid-playback in-guest framebuffer capture. Any stutter, resolution
      downgrade, or fault fails the whole milestone. Latest pass 2026-06-12:
      `RESULT pass fps=59.8 speed=1.004 decodedFPS=59.8 dropPct=0.00
      advanced=15.34`, `build-x86_64/perf-video-gate/perf-video-frame.ppm/.png`
      extracted by the harness. The harness runs against a temporary copy of
      the image with optional host D-Bus startup removed, preserving the video
      gate as a kernel/DRM/WebKit baseline rather than a host-program smoke.
      Matrix wrapper: `scripts/gpu/perf-video-gate-matrix.sh`.

Host/boot caveats: use headless `DISPLAY_MODE=nographic` boots for kernel/DRM
init checks; the GTK `gl=es` path is for actual virgl present validation.
QEMU 9.0.2 rejects classic virgl + blob (`blobs and virgl are not compatible
(yet)`); the launcher auto-disables blob for `-gl` devices and wires
`blob=true,hostmem=…` + shared memfd RAM for non-GL `virtio-gpu`
(`QEMU_VIRTIO_GPU_BLOB=auto`).

---

## 6. Remaining work queue (§13)

Ordered roughly by value; none of these may regress the §5 video gate. The
2026-06-11 active closure queue is complete. New open boxes below are
kernel/user ABI work, not product-porting work.

**Current priority (2026-06-12): Linux GUI ABI + X11 first.** The next active
work should strengthen kernel/user ABI compatibility for ordinary Linux GUI
programs, especially X11/XWayland process, socket, shm, input, selection,
window-management, and optional GLX/MIT-SHM paths. Treat large applications
as probes only after a smaller ABI reproducer exists. Live-YouTube QoS and
Chromium remain useful stress lanes, but they are lower priority than X11 ABI
coverage unless they expose a concrete minimal kernel ABI defect.

**Evidence rules (2026-06-12).** Every runtime step records a durable visual
artifact (raw `.ppm` + reviewer `.png` under `build-x86_64/<step>-evidence/`
or the per-harness output dir) alongside logs/metrics: baseline desktop,
after-launch, after-input, after-exit frames where applicable. Non-visual ABI
work marks screenshots `N/A` and points at the console metric plus the
mandatory same-image gate frame. "Desktop icon only" is negative evidence.
When a host GUI app fails, first reduce it to the smallest Linux ABI probe
that reproduces the missing behavior; change or shrink host libraries/programs
as needed so the work stays focused on xv6 kernel ABI compatibility rather
than app-specific packaging.

- [ ] **1. X11 MIT-SHM data path — active next ABI item.**
      The current XCB proof only queries MIT-SHM; it does not yet prove the
      Linux shared-memory path that real X11 toolkits use for image transport.
      Add or extend a tiny host-built XCB/XShm probe that performs
      `shmget`/`shmat`/`shmctl` lifecycle, `X_ShmAttach`,
      `X_ShmPutImage` or shared pixmap drawing, visible framebuffer change,
      keyboard input, WM_DELETE exit, and cleanup. If SysV SHM support is
      missing or partial, implement the minimal Linux-compatible kernel/user
      ABI and prove it with the focused probe before trying another large app.
      *Done when:* `HOSTX11-SHM-PASS` (or equivalent) records launch/input/exit
      screenshots plus log under `build-x86_64/host-x11-shm-proof/`, and the
      mandatory video gate passes on the same image.
- [ ] **1b. X11 DRI3/Present/GLX fd-passing path — active after MIT-SHM.**
      Prove the X11 accelerated presentation ABI with a minimal GLX/EGL-on-X11
      probe before returning to Chromium. Required surfaces: Xwayland DRI3
      extension discovery, PRIME/DRM fd passing over AF_UNIX, Present event
      delivery, sync/fence behavior when exposed, visible animation, input,
      clean teardown, and no regression in `drmabitest`/video gate. Missing
      behavior becomes a focused kernel/user ABI fix, not a browser-specific
      workaround.
  - Deferred browser stress backlog: Live-YouTube QoS is no longer an active
        compatibility blocker. Keep the existing tooling
        (`scripts/gpu/webkit-qos-report.py`, `webkit-cadence-report.py`,
        `webkit-youtube-smoothness-report.sh`) for later confidence runs, but
        only reopen it as ABI work if a short reproducer shows a kernel/user
        defect. Previous facts remain: DNS + typed navigation passed, defaults
        are GStreamer-GL sink + `WEBKIT_GST_MAX_AVC1_RESOLUTION=480P`, and the
        observed residual was `avdec_h264` QoS jitter rather than a present
        stall.
- [x] **2. virgl async-timeout/EIO spiral — closed for active queue
      2026-06-11 (long soak deferred).** Root cause: the
      `virtio_gpu_present_no_drain=1` path waited on the source fence only
      when `newest_fence <= src_submit_fence`; fixed to `>=` in
      `virtio_gpu_copy_resource_to_scanout()`/`..._to_resource()`. Focused GL
      Maze maximize reproducer now completes with zero async/EIO markers
      (`changed_pixels=680477/1024000`); two 1800 s soaks passed
      (`build-x86_64/virgl-async-soak/`).
  - [x] Deferred confidence moved out of the active queue: 3 consecutive
        ≥30 min soaks + 10 gate runs are nightly-class stress evidence, not
        Linux GUI ABI blockers (`VIRGL_SOAK_SECONDS=60` for smoke).
- [x] **3. OOM victim attribution — closed 2026-06-11.** Badness scored from
      lock-free live RSS (`mm_rss_pages`); no `vm_rlock()` in the scan path;
      OOM kill no longer double-frees `thread_group`. Metric: three 768 MB
      boots selected `python3.12` with `rss_pages≈150 k`;
      `OOMTEST-DONE oom=1 done=1 survived=1`. Screenshot N/A; same-image gate
      frame recorded.
- [x] **4. Typed-URL/Enter navigation harness — closed 2026-06-11.**
      `scripts/gpu/webkit-typed-url-enter.expect` + `keyinject`; required a
      real `/dev/kbd` write-path kernel fix. Three consecutive fresh-image
      `TYPEDURL-PASS` runs; evidence
      `build-x86_64/webkit-typed-url/typed-url-human-button.ppm/.png`.
- [x] **5. Unified client decorations — closed for local C clients
      2026-06-11 (NetSurf skipped).** Shared `xv6_titlebar.{c,h}` +
      `xv6_titlebar_gl.{c,h}` own hit testing, actions, and drawing for all
      six local C clients (`filemgr`, `peanutgb`, `glsmoke`, `glmaze`,
      `mesaglsmoke`, `mesawlegl`); zero per-client titlebar definitions
      remain. Min/max/close framebuffer matrix passes for all six
      (`build-x86_64/titlebar-control-matrix/*-summary.tsv`); gate green.
  - [x] Long-term decoration decision made 2026-06-12: keep decorations out
        of the kernel ABI path. Local in-tree C clients keep the shared
        `xv6_titlebar` helper because that is already proven; imported Linux
        GUI probes should use their toolkit/X11/XWayland/client decoration
        paths. Do not spend priority on a libdecor port, toytoolkit rebase, or
        compositor-side `xdg-decoration` unless a small Linux ABI probe
        requires it. NetSurf/GTK keeps its toolkit control row.
- [x] **6. Host-visible zero-copy blob — closed as optional/host-blocked
      2026-06-12.** QEMU 9.0.2 rejects classic virgl+blob at startup;
      rutabaga/virglrenderer refuses mappable `HOST3D` blobs (`create=-5`);
      the kernel therefore correctly keeps `GETPARAM(HOST_VISIBLE)=0` and the
      userspace probe skips host-visible mapping instead of claiming support.
      Solid proof is split across the current `proof-drmabitest.log`
      (`RESOURCE_BLOB=1`, `HOST_VISIBLE=0`, guest blob creates and
      host-visible skips on `card0`/`renderD128`), the audit's direct
      rutabaga host-refusal log, and the current same-image
      `perf-video-gate` pass (`fps=59.8`, `dropPct=0.00`). Future capable
      backend reopen condition: init probe succeeds, `HOST_VISIBLE=1`,
      mapped-blob round-trip passes, and the gate stays at or above the
      transfer-model FPS.
- [x] **7. Linux GUI ABI probes (§10.5) — X11-first kernel-focused backlog
      closed for current scope 2026-06-12.** See the
      dedicated checklist below. The importer and host apps are diagnostic
      pressure tests for Linux process, file, socket, memory-management,
      input, DRM, Wayland, and X11 ABI coverage. New progress: the focused
      proof harness is closed and four representative imported apps have
      fresh proof:
      IDLE/X11 (`HOSTIDLE-X11-PASS`, verifier summary
      `build-x86_64/host-gui-proof-verify/host-idle-x11-proof-summary.tsv`)
      and the embedded-runtime Python Wayland REPL (`HOSTPYREPL-PASS`,
      verifier summary
      `build-x86_64/host-gui-proof-verify/host-python-repl-proof-summary.tsv`)
      plus an imported host GTK/Wayland app (`HOSTGTK-SMOKE-PASS`,
      verifier summary
      `build-x86_64/host-gui-proof-verify/host-gtk-smoke-proof-summary.tsv`)
      and imported host WLEGL smoke (`HOSTWLEGL-SMOKE-PASS`,
      verifier summary
      `build-x86_64/host-gui-proof-verify/host-wlegl-smoke-proof-summary.tsv`).
      The X11-first follow-up is now a toolkit-free host XCB probe,
      `/bin/host-x11-abi-smoke`, which directly exercises Xwayland
      map/configure, AF_UNIX/XCB event delivery, MIT-SHM extension query,
      selection ownership, pixmap-backed drawing, keyboard input, and
      WM_DELETE_WINDOW client-message exit:
      `HOSTX11ABI-SMOKE-PASS launch_changed_pixels=254485
      input_changed_pixels=191080 exit_changed_pixels=254485` with verifier
      summary
      `build-x86_64/host-gui-proof-verify/host-x11-abi-smoke-proof-summary.tsv`
      and screenshots/logs under `build-x86_64/host-x11-abi-smoke-proof/`.
      Mandatory gate on the rebuilt image passed with
      `xv6-perf-video:RESULT pass fps=59.8 speed=1.004 decodedFPS=59.8
      dropPct=0.00` for the earlier imported-app batch and
      `xv6-perf-video:RESULT pass fps=29.7 speed=0.995 decodedFPS=29.7
      dropPct=1.35` after the final XCB probe image refresh. The gate harness now
      uses a temporary image with `weston-session`-only startup so optional
      host D-Bus probes cannot contaminate the kernel/video ABI baseline.
      Chromium remains useful as a later stress probe, but it is not the
      deliverable; if it exposes only host-library packaging drift, reduce or
      replace it with a smaller ABI reproducer. Reopen this checklist only
      when a host GUI probe identifies a concrete unclosed Linux ABI gap.
- [x] **8. Optional ABI completeness — closed for current scope 2026-06-11.**
      `kcmp(KCMP_FILE)` proven on both DRM nodes (dup fds equal, separate
      opens non-equal, `-EBADF`/`-EINVAL` honest); fbdev x86_64 layout audit
      recorded. Follow-up `sg_table` diagnostics closed 2026-06-12 with
      `TTM-SG-TABLE-PASS`; Mesa may re-enable `-Dallow-kcmp` at next
      port-config refresh.
- [x] **9. Housekeeping — closed 2026-06-11.** Empty leftover dirs
      `ports/xv6-gbm/src/` and `ports/mesa/src/src/gallium/winsys/virgl/xv6/`
      deleted; no related untracked entries remain. Current unrelated
      `ports/netsurf/src` dirt is intentionally skipped from this queue.

---

## 7. Build matrix (keep iteration cheap)

Never rebuild `world` or the toolchain for these items — reuse
`build-toolchain-x86_64/` and build the narrowest target, then `image`.
`image` is mandatory after any kernel/user/ports rebuild — QEMU happily boots
a stale `xv6.bin`/`fs.img` pair.

| Change touches | Minimal rebuild |
|---|---|
| kernel source | `cmake --build build-x86_64 --target kernel image -j$(nproc)` |
| `user/` programs | `cmake --build build-x86_64 --target user image -j$(nproc)` |
| `ports/wayland/src/*` (desktop.c, clients) | `cmake --build build-x86_64/ports --target port-wayland -j$(nproc)` + `image` |
| Weston source/protocol | `port-weston-clean` + `port-weston` + `port-wayland` + `image` |
| nested-submodule port source | `port-<name>-clean` + `port-<name>` + `image` (stamps miss nested edits) |
| rootfs assets / `scripts/image/*` | `image` only |
| expect harnesses (`tmp/*.expect`), launch scripts | no rebuild |

---

## 8. Upstream convergence (§10) — done

Principle: **signature, not fork** — the only xv6 divergence is an upstream
build option or the `DETECT_OS_XV6` platform define; no replacement
libraries, no source patches, no runtime monkey-patching, no private ioctl
winsys.

- [x] **Task 1 — stock Mesa/GBM substrate.** `virgl_xv6_winsys.c` and
      `xv6-gbm` deleted; stock `virgl_drm_winsys` + `gbm_dri` bind through
      standard `DRM_IOCTL_VIRTGPU_*` + GEM-dumb/PRIME. Commits: super
      `e404aa8`, kernel `512fac7`, ports `7954144`.
- [x] **Task 2 — missing libraries added, patches dropped.** New ports:
      `libudev`, `libinput`, `libseat`, `libevdev`, `hwdata`,
      `xkeyboard-config`, upstream Weston; gtk3/libepoxy/drm_info patches
      deleted; libdrm ≥2.4.134.
- [x] **Task 3 — Weston is the sole compositor (one-way cutover).**
      `wlcomp.c` deleted; `desktop.c` execs `/bin/weston`; all validators +
      gate pass through Weston.
- [x] **Task 4 — WebKit/Skia crutches deleted.** `xv6memshim.c` removed
      (glibc AVX2 `mem*` works with kernel AVX/YMM enabled; the Skia
      RIP-patcher masked GPU-context loss from the old private winsys).

Tooling caveats kept: HMP `mouse_move` is dropped for virtio-tablet — use
guest-side `mouseinject`; the HW cursor is invisible in framebuffer captures
by design; xv6 `sh` has no `export`/`>>`, redirects need a space.

### Desktop-session defect closures (§10.4, all fixed with runtime proof)

- [x] Client-drawn titlebars for all non-toytoolkit clients (2026-06-11).
- [x] Desktop launcher labels match targets (2026-06-10).
- [x] MiniBrowser launch, local content, live-YouTube visible playback
      (2026-06-11) — includes the `link`/`linkat` MAXPATH ABI fix (kernel
      `889a39a`), OOM kill-path fix, GStreamer-GL sink default, and typed-URL
      canonicalization.
- [x] Visible cursor black-box fixed — `shm_pool_resize()` preserves bytes
      across remap (`alpha_nonzero=254`).
- [x] Weston panel task tabs + reachable minimize (private
      `weston_desktop_shell` protocol extension).

---

## 9. Linux GUI ABI probes (§10.5)

Goal: prove that unmodified Linux GUI stacks can drive xv6 through Linux ABI
surfaces as normal guest processes. Host GUI programs are **probes**, not
ports: when a large application fails, classify the failure first. Kernel ABI
gaps become kernel/user ABI work with a minimal reproducer and proof; host
library packaging conflicts or app policy choices should be reduced, swapped,
or documented without turning the plan into a product-specific port.

A probe counts only when it launches from the guest desktop or supervised
session, maps a visible Weston/XWayland window, accepts input, exits cleanly,
and has log + framebuffer proof. A failed probe is useful only when it points
to a concrete Linux ABI gap or justifies replacing the probe with a smaller
one.

**Priority order (2026-06-12):** X11/XWayland ABI coverage first, then other
Linux GUI ABI probes. Keep the existing Wayland, GL/EGL, GTK, and
embedded-runtime proofs as representative coverage, but spend new effort on
small X11 probes before returning to Chromium. Use Chromium only as a stress
probe for unresolved ABI gaps, not as the next app to port.

**X11 ABI proof:** IDLE/Tk proves a real toolkit stack, and the
`host-x11-abi-smoke` XCB probe proves the smaller ABI surface directly:
map/configure/destroy, keyboard and pointer input, MIT-SHM discovery,
pixmap/image transfer, selection ownership, WM_DELETE_WINDOW, and clean
Xwayland teardown. Each future X11 probe must use the same proof rule:
visible XWayland window, input, clean exit, logs, screenshots, and same-image
gate.

- [x] **Importer.** `scripts/image/import-host-gui.sh` stages
      executable/interpreter/library closure under `/opt/host-gui/<id>/` with
      manifest hashes; guest Wayland/Mesa/DRM/Weston runtime libraries are
      skipped so the guest graphics stack is never shadowed.
- [x] **IDLE/Tk X11 checkpoint — closed 2026-06-12.** Weston built with
      `xwayland=true`; Xwayland 24.1.6 + xkbcomp + XKB data staged;
      PyInstaller IDLE exposed as `/bin/host-idle-x11`. Root cause of the
      invisible-window phase: xv6 returned `-EOPNOTSUPP` for AF_UNIX
      `sendto(NULL)`/`recvfrom()`, breaking the XCB/XWM handshake;
      `lwip_port/sys_socket.c` now routes them to the Unix socket
      read/write paths. Fresh proof after the 2026-06-12 image refresh:
      `HOSTIDLE-X11-PASS launch_changed_pixels=565922
      input_changed_pixels=3755 exit_changed_pixels=566005` with a visibly
      mapped `XV6-IDLE-X11-PROOF` window, `4+5` → `9` input frame, clean
      Ctrl+Q exit, and `HOSTGUI-PROOF-VERIFY-PASS app=host-idle-x11`
      (`build-x86_64/host-idle-x11-proof/`,
      `build-x86_64/host-gui-proof-verify/host-idle-x11-proof-summary.tsv`).
      The same rebuilt image passed the mandatory gate:
      `xv6-perf-video:RESULT pass fps=59.9 speed=1.002
      decodedFPS=59.9 dropPct=0.00 advanced=15.23`
      (`build-x86_64/perf-video-gate/run.log`).
      Known benign: Xwayland GLAMOR falls back to software; xkbcomp keymap
      warnings are non-fatal.
- [x] **Focused XCB/X11 ABI smoke — closed 2026-06-12.**
      `scripts/image/host-x11-abi-smoke.c` is a tiny host-built XCB program
      imported as `/bin/host-x11-abi-smoke` with a direct launcher. It avoids
      toolkit policy and proves the kernel-facing X11 ABI surface directly:
      Xwayland AF_UNIX/XCB connection, window map/configure, MIT-SHM
      extension discovery (`mit_shm_present=1`), selection ownership
      (`status=PASS`), pixmap-backed drawing, pointer focus, keyboard input,
      WM_DELETE_WINDOW client-message delivery, and clean process exit.
      Fresh proof:
      `HOSTX11ABI-SMOKE-PASS launch_changed_pixels=254485
      input_changed_pixels=191080 exit_changed_pixels=254485`; screenshots
      and log are under `build-x86_64/host-x11-abi-smoke-proof/`
      (`host-x11-abi-smoke-launch.png`,
      `host-x11-abi-smoke-input.png`,
      `host-x11-abi-smoke-exit.png`, `run.log`), with verifier summary
      `build-x86_64/host-gui-proof-verify/host-x11-abi-smoke-proof-summary.tsv`
      (`HOSTGUI-PROOF-VERIFY-PASS app=host-x11-abi-smoke`). Same rebuilt
      image gate:
      `xv6-perf-video:RESULT pass fps=29.7 speed=0.995
      decodedFPS=29.7 dropPct=1.35 advanced=14.95`, `GATE-PASS`.
- [x] **Wayland Chromium stress probe — deferred behind X11 ABI work.**
      Chrome-for-Testing 148 staged at
      `/opt/host-gui/wayland-chromium/`. Linux ABI gaps already closed on
      this lane: multi-VMA `mprotect`, Chrome/Crashpad `prctl` set, AF_UNIX
      `SO_PASSCRED`/`SCM_CREDENTIALS`, socklen copyout, tmpfs mkdir mode,
      LP64 `msg_controllen`/`cmsg_len` + `SCM_RIGHTS` validation,
      `MAP_ANONYMOUS` ignoring fd, Linux `dev_t` layout for `st_rdev`
      stat/mknod, `/proc/sys/fs/inotify/*`, and absolute-path `openat()`
      ignoring `dirfd` (kernel commit `d70128d`, user regression commit
      `2fd46fc`, harness `scripts/audit/linuxsyscallabitest.expect`, proof
      `LINUXSYSCALLABITEST-PASS` in
      `build-x86_64/linuxsyscallabitest/run.log`). Current state: Chrome
      reaches GDK/Wayland/Mesa virgl, both zygote execs, and resource
      loading, but maps no browser surface (desktop-only screenshots are
      negative evidence under
      `build-x86_64/wayland-chromium-supervisor-{diag,low-noise}/`).
      Supervised launch knobs: `host_chromium=1`, `host_chromium_backend=`,
      `host_chromium_multiprocess=1`, `host_chromium_extra_flags=`,
      `host_chromium_surface_trace=1`, `chrome_lifecycle_trace=1`,
      `chrome_syscall_trace=1`. In-band procfs reads block in this state —
      use nonblocking pid probes or kernel-side traces. Fresh diagnostic
      2026-06-12: single-process Wayland reaches GDK/Wayland, guest
      Mesa/virgl, `wl_display_connect`, registry roundtrips, and then reports
      `GLib-GObject: cannot register existing type 'AtkObject'` before any
      Weston surface map; the screenshot remains desktop-only. Treat that as
      host-library closure drift unless a smaller reproducer shows a kernel
      ABI defect. Reopen only after the X11 MIT-SHM and X11 DRI3/Present/GLX
      probes above are closed, or earlier if Chromium yields a smaller
      kernel-facing Linux ABI reproducer with clearer proof.
- [x] **Embedded-runtime app (host Python REPL) — closed 2026-06-12.**
      GTK variant remains blocked on `cannot register existing type
      'GdkPixbuf'` (toolkit-runtime packaging bug), so the passing proof uses
      a toolkit-free `wl_shm` + embedded Python 3.12 client launched via
      `/bin/host-python-repl`. Final proof:
      `scripts/gpu/host-python-repl-proof.expect` boots a fresh copy of the
      image, launches the imported app, verifies Wayland map + `Python ready`
      + `host-python-repl: eval 6*7`, captures baseline/launch/input frames,
      and verifies real Escape exit through `keyinject key esc` (the process
      is absent from `HOSTPYREPL_AFTER_ESC_PS_DONE`; `keyinject` now maps
      `esc`/`escape` to key code 1). Fresh result after the 2026-06-12 image
      refresh:
      `HOSTPYREPL-PASS launch_changed_pixels=368256
      input_changed_pixels=207 exit_changed_pixels=368256`, with non-black
      phase verification
      (`HOSTGUI-PROOF-VERIFY-PASS app=host-python-repl`,
      input phase nonblack pixels `1023522`) and screenshots/logs under
      `build-x86_64/host-python-repl-proof/`
      (`host-python-repl-launch.png`, `host-python-repl-input.png`,
      `host-python-repl-exit.png`, `run.log`;
      verifier summary:
      `build-x86_64/host-gui-proof-verify/host-python-repl-proof-summary.tsv`).
- [x] **Wayland-native toolkit app proof — closed for imported GTK
      2026-06-12.** `scripts/image/host-gtk-smoke.c` is a host-built
      GTK/Wayland app imported as `/bin/host-gtk-smoke`. The initial black-box
      failure was a launcher/toolkit packaging issue: the generated shell
      wrapper used unsupported guest-shell constructs, and decorated GTK
      windows aborted while loading stock titlebar pixbuf resources. The
      closed lane now uses `scripts/image/host-gtk-smoke-launcher.c` to set
      Wayland env and exec the bundled loader directly, and the smoke window
      is explicitly undecorated so it does not require GTK CSD icon resources.
      Fresh same-image proof:
      `HOSTGTK-SMOKE-PASS launch_changed_pixels=126656
      input_changed_pixels=618 exit_changed_pixels=126656`; log evidence
      shows `host-gtk-smoke: ready`, typed text progression through
      `host-gtk-smoke: changed text= xv6`, and clean Escape exit. Screenshot
      evidence is under `build-x86_64/host-gtk-smoke-proof/`
      (`host-gtk-smoke-launch.png`, `host-gtk-smoke-input.png`,
      `host-gtk-smoke-exit.png`, `run.log`), with non-black verifier summary
      `build-x86_64/host-gui-proof-verify/host-gtk-smoke-proof-summary.tsv`
      (`HOSTGUI-PROOF-VERIFY-PASS app=host-gtk-smoke`; input phase nonblack
      pixels `1023080`). Same rebuilt image gate:
      `xv6-perf-video:RESULT pass fps=60.0 speed=1.001 decodedFPS=60.0
      dropPct=0.00 advanced=15.24`, `GATE-PASS`.
- [x] **GL/EGL/Wayland imported app proof — closed 2026-06-12.**
      `scripts/image/host-wlegl-smoke.c` is a minimal host-built xdg-shell
      Wayland/EGL/GLES2 app imported as `/bin/host-wlegl-smoke`. Earlier
      `host-eglgears-wayland` and `host-es2gears-wayland` attempts remain
      negative client evidence: they reached guest Mesa/virgl but produced
      black boxes or bad-rendering/async-timeout markers. The closed
      representative uses generated xdg-shell protocol bindings, a direct
      launcher (`scripts/image/host-wlegl-smoke-launcher.c`), and a
      deterministic file-control repaint path because injected pointer events
      were not delivered to the undecorated EGL client. Fresh proof:
      `HOSTWLEGL-SMOKE-PASS launch_changed_pixels=169856
      input_changed_pixels=166400 exit_changed_pixels=169856`; log evidence
      shows `xv6-mesa: wayland selecting drm with virgl`,
      `host-wlegl-smoke: egl ready 1.5 vendor=Mesa renderer=virgl (...)`,
      first blue frame, control repaint to red, and clean exit. Screenshot
      evidence is under `build-x86_64/host-wlegl-smoke-proof/`
      (`host-wlegl-smoke-launch.png`, `host-wlegl-smoke-input.png`,
      `host-wlegl-smoke-exit.png`, `run.log`), with verifier summary
      `build-x86_64/host-gui-proof-verify/host-wlegl-smoke-proof-summary.tsv`
      (`HOSTGUI-PROOF-VERIFY-PASS app=host-wlegl-smoke`). Same rebuilt image
      gate: `xv6-perf-video:RESULT pass fps=60.2 speed=1.002
      decodedFPS=60.2 dropPct=0.00 advanced=15.24`, `GATE-PASS`.
- [x] **Focused host-GUI proof harness — closed 2026-06-12.**
      `scripts/gpu/host-gui-proof-verify.py` now rejects missing logs, failure
      markers, black/near-black captures, wrong dimensions, and unchanged
      phase pairs. The app harnesses leave `before/launch/input/exit` PPM/PNG
      bundles, diff PNGs, and TSV summaries. Fresh same-image evidence:
      `HOSTGUI-PROOF-VERIFY-PASS app=host-idle-x11` and
      `HOSTGUI-PROOF-VERIFY-PASS app=host-python-repl`, plus
      `HOSTGUI-PROOF-VERIFY-PASS app=host-gtk-smoke`, with summaries under
      `build-x86_64/host-gui-proof-verify/`. New imported apps, including the
      deferred Chromium lane, must pass this verifier before they can count as
      supported.
- [x] **Done when:** four representative imported apps pass the validation
      rule — (1) Wayland-native toolkit app (GTK smoke now passes),
      (2) GL/EGL/Wayland app (WLEGL smoke now passes),
      (3) embedded-runtime Python GUI app,
      (4) X11/Tk app (IDLE is the first); each with launch/input/exit
      screenshots, per-app logs, and the mandatory gate green on the same
      image.

---

## 10. Out of scope / explicitly separate

- **Hyper-V DXG / GPU-PV** (`fb_dxg_present.c`) — tracked in
  `GPU_REMAINING_GAPS.md`; stays fail-closed; not the Linux DRM ABI.
- **Nouveau/DDA real hardware** (`fb_nouveau.c`) — separate passthrough
  effort.
- **RISC-V graphics** — out of scope.
- **Full desktop environment (GNOME/Plasma)** — a platform-services project
  (D-Bus, PAM, polkit, audio, session/display managers, portals), not a
  compositor swap; none of those services exist on the target.

---

## 11. Summary

The Linux DRM ABI convergence work is **done**: real `dma_fence` core,
per-file GEM handles + PRIME, KMS atomic with present-driven vblank and real
out-fences, the standard `DRM_IOCTL_VIRTGPU_*` UAPI, fail-closed blob
plumbing, and a shmem-backed allocator — with stock Mesa/GBM/libdrm and
upstream Weston running on it unmodified. The §5 fullscreen-video gate must
be re-run after any GPU/DRM/desktop change; validate with trace shape +
on-screen output + framebuffer samples, never counters alone. Open work is
the deferred backlog in §6 and the Linux GUI ABI probe track in §9.
