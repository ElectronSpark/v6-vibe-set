# Linux DRM / GPU Graphics ABI Compatibility Plan

Last updated: 2026-06-12 (rewritten as a checklist; the full per-item
narratives, gap analyses, and closure evidence live in git history and
`docs/linux-drm-abi-audit.md`).

**Status: the core DRM-ABI convergence work is complete for this host; the
2026-06-11 §13 active closure queue is closed.** Phases 0–6 are landed and
committed, all validators pass, stock Mesa/GBM/libdrm and upstream Weston
drive the kernel through the standard Linux UAPI, and the runtime crutches
are deleted. Remaining work is deferred backlog (live-YouTube decode QoS,
long soaks, optional host-visible zero-copy, and the §10.5 host-GUI track).

**VM re-verification 2026-06-12 (current image):**

- [x] `drmabitest` full suite under virgl — `DRMABI-KCMP-PASS card0=1
      renderD128=1`, zero FAIL/mismatch lines; caps=behavior held (virgl boot
      reports `RESOURCE_BLOB=0` and blob create honestly fails `-EOPNOTSUPP`).
- [x] Headless non-GL `virtio-gpu` blob probe — `GETPARAM(RESOURCE_BLOB)=1`,
      fail-closed `HOST_VISIBLE=0`, real guest blob creates on
      card0/renderD128, host-visible probe `skipped=1`, nonzero `fb0:sample`.
- [x] §8 step-7 fullscreen-video gate — latest post-image run
      `expect scripts/gpu/perf-video-gate.expect` on 2026-06-12:
      `RESULT pass fps=56.5 speed=1.001 decodedFPS=56.5 dropPct=0.12
      advanced=15.25`,
      `__WEBKIT_API_SMOKE_DONE_0__`.
- [x] GUI-session boot log clean — `dma_fence: selftest ok`, card0 +
      renderD128 registered, virgl capsets 1+2, Weston desktop with 17
      entries, zero async-timeout/EIO/panic markers (Xwayland GLAMOR software
      fallback matches the §10.5 known state).

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

- [ ] **Host-visible zero-copy blob (optional, host-blocked).** Kernel side is
      code-complete and fail-closed; needs a host backend that accepts
      mappable HOST3D blobs (§6 item 6).
- [ ] **`sg_table`-equivalent scatter-list abstraction.** Only needed if a
      real DMA-capable importer (IOMMU/device DMA, second GPU, v4l) ever
      consumes an imported buffer. Low priority for pure-sysmem QEMU.
- [ ] **Multi-CRTC / hotplug / overlay planes.** KMS objects are static
      singletons; sufficient for the single virtio scanout. Out of scope until
      a multi-head target exists.
- [x] **fbdev struct-layout audit (x86_64).** Closed 2026-06-11:
      `sizeof(fb_var_screeninfo)==160`, `sizeof(fb_fix_screeninfo)==80`, all
      audited offsets match Linux.
- [x] **`kcmp` syscall.** `KCMP_FILE` implemented for same-process fd
      comparison, covered by `drmabitest`.

---

## 4. Standing correctness invariants

Keep these when touching the code:

- [ ] **Caps = behavior.** Never advertise a `GET_CAP`/`GETPARAM` capability
      whose ioctl path returns `-EOPNOTSUPP`; flip the cap in the same change
      that implements the feature.
- [ ] **Fail-closed, honest errno.** Unimplemented paths return the Linux
      errno a real driver would; never fake success to pass a validator.
- [ ] **Fences must signal.** Any exported fence/out-fence must be backed by a
      real completion; a never-signaling fd hangs a compositor.
- [ ] **Per-file handle scope.** PRIME import creates a *new* handle in the
      importing file; `GEM_CLOSE` drops only the file's reference, never the
      global BO.

---

## 5. Validation strategy

Runtime-trace, never declare a path dead from source alone (see
`xv6-os-runtime.md` repo memory). Scale validator runs to the change: kernel
DRM/virtgpu → `drmabitest` (+ owning validator + gate); compositor/client →
affected runtime validator + gate; rootfs/assets → gate alone. Build only the
narrowest targets (§7 build matrix); never rebuild `world`/toolchain for
validation.

Validator checklist for a milestone:

- [ ] `drmabitest` — every ioctl with known-good and error inputs,
      Linux-matching errno/struct output (focused `--virtgpu-only` for
      blob/virtgpu checks).
- [ ] libdrm conformance — `modetest`, `kmscube`, `drm_info` against
      `/dev/dri/card0`.
- [ ] Mesa bring-up — stock Mesa virgl through `gpu-validate`, `kmscube`,
      `virgl-kms-validate.sh` (direct KMS GBM/EGL), `mesawlegl`,
      `mesaglsmoke`.
- [ ] Blob/zero-copy — non-GL guest blob path: `GETPARAM(RESOURCE_BLOB)==1`,
      fail-closed `HOST_VISIBLE=0`.
- [ ] Compositor end-to-end — Weston + WebKit/GL apps; compare trace shape +
      on-screen output + framebuffer samples, never counters alone.
- [ ] Regression guard — fail-closed counters, caps/behavior agreement.
- [ ] **§8-style fullscreen-video gate (MANDATORY after any GPU/DRM/desktop
      change).** `expect scripts/gpu/perf-video-gate.expect`: the offline
      1280×800@60 H.264 clip (`rootfs-overlay/share/webkit/perf-*.mp4|html`)
      must play fullscreen at default resolution with `speed ≥ 0.9×`,
      `dropPct < 10`, zero `virtio_failures`/`virtio_timeouts`/panics, plus a
      mid-playback in-guest framebuffer capture. Any stutter, resolution
      downgrade, or fault fails the whole milestone. Latest pass 2026-06-12:
      `RESULT pass fps=56.5 speed=1.001 decodedFPS=56.5 dropPct=0.12`.
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
2026-06-11 active closure queue is complete; open boxes below are deferred
backlog, not blockers.

**Evidence rules (2026-06-12).** Every runtime step records a durable visual
artifact (raw `.ppm` + reviewer `.png` under `build-x86_64/<step>-evidence/`
or the per-harness output dir) alongside logs/metrics: baseline desktop,
after-launch, after-input, after-exit frames where applicable. Non-visual ABI
work marks screenshots `N/A` and points at the console metric plus the
mandatory same-image gate frame. "Desktop icon only" is negative evidence.

- [ ] **1. Live-YouTube smoothness (decode QoS) — deferred backlog.**
      Residual jitter is `avdec_h264` QoS drops (39 over ~65 s, lateness to
      ~200 ms), not a present stall. Defaults applied: GStreamer-GL sink +
      `WEBKIT_GST_MAX_AVC1_RESOLUTION=480P`. Tooling ready:
      `scripts/gpu/webkit-qos-report.py`, `webkit-cadence-report.py`,
      `webkit-youtube-smoothness-report.sh`. DNS + typed navigation proven
      (`scripts/net/dnsdiag.expect` PASS;
      `build-x86_64/webkit-typed-url/typed-url-www-youtube-*.png`). One 60 s
      run hit `TYPEDURL-FAIL webkit-crash` (page-process instability, not
      DNS). *Done when:* a ≥300 s real watch-page soak shows ≤5 QoS
      drops/min, max lateness <100 ms, every adjacent host-visible
      player-crop pair changed, and the gate passes on the same image;
      evidence under `build-x86_64/webkit-youtube-smoothness-<date>/`.
- [x] **2. virgl async-timeout/EIO spiral — closed for active queue
      2026-06-11 (long soak deferred).** Root cause: the
      `virtio_gpu_present_no_drain=1` path waited on the source fence only
      when `newest_fence <= src_submit_fence`; fixed to `>=` in
      `virtio_gpu_copy_resource_to_scanout()`/`..._to_resource()`. Focused GL
      Maze maximize reproducer now completes with zero async/EIO markers
      (`changed_pixels=680477/1024000`); two 1800 s soaks passed
      (`build-x86_64/virgl-async-soak/`).
  - [ ] Deferred confidence: 3 consecutive ≥30 min soaks + 10 gate runs
        (nightly-class, non-blocking; `VIRGL_SOAK_SECONDS=60` for smoke).
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
  - [ ] Long-term decoration decision still open: libdecor port vs toytoolkit
        rebase vs server-side `xdg-decoration`; NetSurf keeps its GTK control
        row until that decision changes.
- [ ] **6. Host-visible zero-copy blob — optional / host-blocked.** QEMU
      9.0.2 rejects virgl+blob at startup; rutabaga refuses mappable HOST3D
      (`create=-5`); Alpine proves the transfer model suffices (66–73 FPS).
      *Optional done when:* on a capable backend the init probe succeeds,
      `GETPARAM(HOST_VISIBLE)=1`, `drmabitest --virtgpu-only` passes a
      mapped-blob round-trip, and the gate passes at ≥ transfer-model FPS;
      evidence under `build-x86_64/host-visible-blob-evidence/`.
- [ ] **7. Host GUI importer (§10.5) — complete-support backlog.** See the
      dedicated checklist below.
- [x] **8. Optional ABI completeness — closed for current scope 2026-06-11.**
      `kcmp(KCMP_FILE)` proven on both DRM nodes (dup fds equal, separate
      opens non-equal, `-EBADF`/`-EINVAL` honest); fbdev x86_64 layout audit
      recorded. Conditional leftover: `sg_table` abstraction if a real DMA
      importer appears. Mesa may re-enable `-Dallow-kcmp` at next port-config
      refresh.
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

## 9. Host GUI programs as native guest processes (§10.5)

Goal: an x86_64 Linux GUI binary from the host runs **inside xv6** as a
normal guest process; fix missing Linux ABI surface in xv6 rather than adding
app-specific shortcuts. A host app counts as supported only when it launches
from the guest desktop, maps a visible window under Weston, accepts input,
exits cleanly, and has log + framebuffer proof.

**Priority order (2026-06-12):** X11/XWayland checkpoint first; Chromium is
the deferred follow-up lane.

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
      read/write paths. Final proof: `HOSTIDLE-X11-PASS` with a visibly
      mapped `XV6-IDLE-X11-PROOF` window, `4+5` → `9` input frame
      (`diff-ae.txt = 3755` changed pixels), clean Ctrl+Q exit
      (`build-x86_64/host-idle-x11-proof/`); same-image gate passed.
      Known benign: Xwayland GLAMOR falls back to software; xkbcomp keymap
      warnings are non-fatal.
- [ ] **Wayland Chromium — deferred follow-up (not a passing proof).**
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
      use nonblocking pid probes or kernel-side traces. *Next step:* decode
      the remaining zygote/startup stall after resource loading; require a
      mapped browser surface + visible navigation screenshot before counting
      Chrome as supported.
- [ ] **Embedded-runtime app (host Python REPL).** GTK variant blocked on
      `cannot register existing type 'GdkPixbuf'` (toolkit-runtime packaging
      bug); in-progress replacement is a toolkit-free `wl_shm` + embedded
      Python 3.12 client launched via `/bin/host-python-repl`.
- [ ] **Wayland-native toolkit app proof** (e.g. imported
      `eglgears_wayland`; dry-run staged, runtime proof pending).
- [ ] **Focused host-GUI proof harness** that fails on "desktop icon only"
      captures and leaves `before/after-launch/after-input/after-exit`
      PPM/PNG bundles per app.
- [ ] **Done when:** four representative imported apps pass the validation
      rule — (1) Wayland-native toolkit app, (2) GL/EGL/Wayland app,
      (3) embedded-runtime Python GUI app, (4) X11/Tk app (IDLE is the
      first); each with launch/input/exit screenshots, per-app logs, and the
      mandatory gate green on the same image.

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
the deferred backlog in §6 and the host-GUI support track in §9.
