# Linux DRM / GUI ABI Compatibility Plan

Last updated: 2026-06-26.

This is the active handoff plan for Linux GUI/DRM ABI work in
`/home/es/xv6-os`. It intentionally keeps only current gaps and guardrails.
Long historical evidence trails live in git history, ABI audit docs, and run
artifacts.

## Current Direction

KDE Plasma is the primary desktop target. Weston and Chromium remain regression
controls and stress probes, but new work should not modify Weston unless the
user explicitly asks. KDE/Qt/KWin/Plasma source must stay upstream-clean except
for marker-only xv6 metadata. If KDE fails, reduce it to a Linux ABI gap and
prefer fixes in the kernel, libc/sysroot, rootfs data, or build/staging
wrappers.

The current KVM/virgl DRM baseline is healthy enough to run KDE:

- `/dev/dri/card0` and `/dev/dri/renderD128` exist.
- KWin starts on DRM with virgl and reports OpenGL ES 3.1.
- Plasma, Xwayland, kactivitymanagerd, kded, and Konsole can run together.
- The KDE smoke proves Plasma/KWin/Xwayland/app process health, visible host
  screenshots, input response, Konsole launch, and Chromium launch. The raw
  KWin screenshot readback probe can still warn on low color detail even when
  the overall smoke passes.
- Linux KDE KVM+virgl baseline and xv6 comparison are recorded in
  `docs/linux-kde-performance-baseline.md`.

## Guardrails

- Do not push without asking.
- Do not touch Weston from this point unless explicitly requested.
- Keep KDE/Qt/KWin/Plasma upstream-clean.
- Treat imported GUI apps as probes, not deliverables.
- Prefer reducer-first debugging for Linux ABI mismatches.
- Do not use static PIDs/TGIDs/TIDs as evidence. Derive roles from argv,
  executable path, thread name, fd/socket graph, lifecycle, surface mapping, or
  SCM/credential traffic in the same boot.
- Use narrow builds:
  - Kernel-only: `cmake --build build-x86_64 --target kernel -j2`
  - Rootfs refresh after sysroot/rootfs/staging edits:
    `cmake --build build-x86_64 --target rootfs-refresh -j2`
  - Avoid broad `world`, toolchain, or aggregate image rebuilds unless the
    changed layer requires them.
- Every visual/runtime proof needs durable logs and screenshots where possible.
  If screenshot capture fails, record that explicitly.
- Keep binary payloads out of git. Stage host/package binaries through the
  build system or Dockerfile/sysroot construction, not the repository tree.

## Current Evidence

### KDE

Passing command:

```sh
timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Current artifact directory:

```text
build-x86_64/kde-plasma-desktop-smoke/
```

Key passing evidence:

```text
Xwayland KDE wrapper: EGL_PLATFORM=wayland GALLIUM_DRIVER=virgl MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu glamor=off
kde_app_launch_probe ... konsole=1 ... dolphin=1 ... chromium=1 ... status=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... konsole=1 ... chromium=1 ... status=PASS
kde_kwin_screenshot_probe result=FAIL detail=low-color-detail
KDE_SMOKE_AGENT_DONE status=PASS
KDE-PLASMA-DESKTOP-SMOKE-DONE
```

The final rootfs must keep `/usr/bin/Xwayland -> ../../bin/Xwayland`, so KWin
uses the xv6 Xwayland wrapper rather than a package binary copied by an overlay.

### Linux KDE Baseline

Baseline artifact:

```text
build-x86_64/linux-kde-virgl-baseline/20260621-123954/
```

Summary:

- QEMU KVM + `virtio-vga-gl`, Alpine 3.23.4, KDE/Plasma upstream packages.
- Renderer: `virgl (D3D12 (Intel(R) UHD Graphics))`.
- Xwayland/GLX control: `glxgears` at 55.638 FPS.
- QEMU monitor screendump failed with `Error: no surface`; log, renderer, FPS,
  and trace evidence are still valid.

See `docs/linux-kde-performance-baseline.md` for the full comparison table.

## Active Gaps

### 1. Xwayland GLAMOR / GLX Acceleration

Status: GLX context, draw, and FPS reducer evidence captured; performance
parity remains open.

The KDE default still guards Xwayland with `XV6_XWAYLAND_GLAMOR=off` for the
stable desktop smoke path. Focused acceleration runs now keep the requested
`kde_xwayland_glamor=auto` policy at the harness boundary while the xv6-owned
Xwayland wrapper maps that automatic policy to `-glamor es`, because the
unqualified Xwayland auto path chose no GLAMOR mode and disabled GLX before the
GL probe could create a context.

Historical no-Weston KDE X11/EGL failure evidence for unqualified auto:

```sh
KDE_SMOKE_REDUCER=x11-egl QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Failure artifact directory:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260625-230206-x11-egl-glamor-auto-glx-absent/
```

Result marker:

```text
KDE-PLASMA-DESKTOP-SMOKE-FAIL x11-egl-session-probe-FAIL exit_status=1
x11-egl-glx_query_extension-status-FAIL start=1 connect=1 gl=0 frame=0
```

Interpretation: KWin Wayland virgl reached OpenGL ES 3.1, but Xwayland's
unqualified auto path did not enable a usable GLX/GLAMOR mode. Xwayland logged
GL version 0/null and disabled GLAMOR before the direct X11 GLX probe could
select a visual; the host log also contained `glx_choose_visual missing`.

Current no-Weston KDE X11/GLX reducer evidence:

```sh
KDE_SMOKE_REDUCER=x11-egl QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 chrome_drm_ioctl_trace=1 chrome_drm_fence_trace=1 kde_smoke_require_chromium=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Artifact directory:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260625-231025-x11-egl-auto-effective-es-pass/
```

Key evidence:

```text
Xwayland KDE wrapper: ... glamor=auto effective_glamor=es ... enable_glx=1
Xwayland KDE wrapper: final argv argc=17 glamor=es ... +extension_GLX=yes
host-x11-egl-smoke: phase=gl_strings status=PASS api=glx vendor=Mesa renderer=virgl ... gl_version=4.2 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.2
host-x11-egl-smoke: phase=draw status=PASS frame=1 mode=glx-probe color=0
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-egl session_probe=PASS
```

Current no-Weston KDE X11/GLX FPS reducer outcome:

```sh
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 chrome_drm_ioctl_trace=1 chrome_drm_fence_trace=1 kde_smoke_require_chromium=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Artifact directory:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-044514-x11-glx-fps-trace-disabled-confirmation-pass/
```

```text
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=85 elapsed_seconds=5.024329 fps=16.918
event_total_ms=8.345 draw_total_ms=3469.444 swap_total_ms=1528.249 final_xsync_ms=12.367 avg_swap_ms=17.979
QEMU trace: ctx_submit=387 set_scanout=100 res_flush=100 fence_ctrl/fence_resp=387/387 res_create_3d=53
```

Phase-detail artifact after probe refinement:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-045322-x11-glx-fps-phase-detail-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=93 elapsed_seconds=5.039396 fps=18.455
draw_total_ms=3539.314 swap_total_ms=1475.927 final_xsync_ms=13.364 avg_swap_ms=15.870
gl_issue_total_ms=3537.151 gl_error_total_ms=2.163 max_gl_issue_ms=130.051 max_gl_error_ms=0.396
QEMU trace: ctx_submit=414 set_scanout=106 res_flush=106 fence_ctrl/fence_resp=414/414 res_create_3d=54
```

Interpretation: the interactive serial-shell launch path was the reducer
blocker, not the GLX probe itself. The active FPS reducer now uses the
xv6-owned KDE session process to launch `/bin/host-x11-egl-smoke --glx-fps`,
which avoids KDE log noise corrupting typed shell commands. GLX starts, binds a
direct virgl context, records renderer strings, submits 3D work, and completes
the finite FPS loop. Trace-disabled confirmation shows diagnostic trace logging
was materially depressing FPS, but the best recorded xv6 result is still far
below the Linux GLX baseline of 55.638 FPS. The refined phase timing shows the
draw bucket is not `glGetError`; it is dominated by existing GL issue calls
(`glViewport`/`glClearColor`/`glClear`). The next reducer should stay
diagnostic, such as `finish-before-swap`, `swap-only`, or a Present-only pacing
burst, before making behavior changes.

GLX FPS variant harness update:

```text
scripts/image/host-x11-egl-smoke.c
scripts/image/kde-session.c
scripts/gpu/kde-plasma-desktop-smoke.expect
```

The xv6-owned GLX FPS reducer now accepts
`kde_x11_egl_glx_fps_variant=baseline|finish-before-swap|swap-only|oml-queue3-swap-only|oml-queue-depth-swap-only`.
Invalid variants fail before a misleading baseline PASS.
`finish-before-swap` resolves and validates `glFinish` through the GLX
proc-address path. The legacy OML queue variant,
`oml-queue3-swap-only`, resolves `GLX_OML_sync_control`, preserves the default
queue depth 3, requires three completed swap-buffer counters in the Expect
success path, and records OML issued/completed/pending timing fields without
changing the default no-variant log shape. The dynamic OML variant,
`oml-queue-depth-swap-only`, accepts
`kde_x11_egl_glx_fps_oml_queue_depth=N` for validated depths 1..8.

Build and staging checks after the harness update:

```text
git diff --check && git -C kernel diff --check
PKG_CONFIG_PATH=/tmp/xv6-host-devpkgs/root/usr/lib/x86_64-linux-gnu/pkgconfig PKG_CONFIG_SYSROOT_DIR=/tmp/xv6-host-devpkgs/root cmake --build build-x86_64 --target host-gui-runtime -j2
cmake --build build-x86_64 --target rootfs-refresh -j2
```

`host-gui-runtime` passed with the known optional probe staging warnings, and
`rootfs-refresh` passed with the known unrelated format-truncation warnings in
other probes. A Tcl-only completeness check could not run because `tclsh` was
not installed; an attempted `expect -n` fallback launched the smoke script, was
interrupted, and is not counted as verification evidence.

Successful `swap-only` variant artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-051923-x11-glx-fps-swap-only-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=105 elapsed_seconds=5.020442 fps=20.914 variant=swap-only
draw_total_ms=23.988 swap_total_ms=4969.928 final_xsync_ms=15.734 avg_swap_ms=47.333
gl_issue_total_ms=23.979 gl_error_total_ms=0.009 swap_only_skipped_draw_frames=104
QEMU trace: ctx_submit=467 set_scanout=119 res_flush=119 fence_ctrl/fence_resp=467/467 res_create_3d=53
```

Interpretation: skipping the per-frame draw work after frame 1 raised FPS only
from the best baseline 18.455 to 20.914, while nearly all elapsed time moved
into `glXSwapBuffers`. This argues against `glViewport`/`glClearColor`/
`glClear` issue overhead as the sole remaining limiter and makes GLX
swap/Present pacing the stronger next suspect.

`finish-before-swap` did not produce a timing result in this cycle. Three
attempts were preserved:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-051641-x11-glx-fps-finish-before-swap-kwin-startup-crash/
build-x86_64/kde-plasma-desktop-smoke-history/20260626-051815-x11-glx-fps-finish-before-swap-session-probe-crash/
build-x86_64/kde-plasma-desktop-smoke-history/20260626-052032-x11-glx-fps-finish-before-swap-spinlock-session-crash/
```

The last run reached the session probe and selected
`glx_fps_variant=finish-before-swap`, but failed before `glx_fps_result` with
`spin_lock reentry`. Treat these as KDE/session stability artifacts, not GLX
timing evidence.

Present-only pacing reducer update:

```text
scripts/image/host-x11-dri3-present-smoke.c
scripts/image/host-x11-dri3-present-smoke-launcher.c
scripts/image/kde-session.c
scripts/gpu/kde-plasma-desktop-smoke.expect
scripts/image/stage-host-gui-runtime.sh
```

The xv6-owned DRI3/Present probe now has a KDE session-launched
`--present-fps` mode and `KDE_SMOKE_REDUCER=x11-present-fps`. The host GUI
staging path links the probe against versioned `libxcb-dri3.so.0` and
`libxcb-present.so.0`, avoiding missing development pkg-config files while
keeping imported packages clean. The DRI3 launcher honors the session log
environment for durable `host-gui-host-x11-egl-smoke.log` capture and retains
the standalone DRI3 log fallback.

Successful no-Weston Present-only artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-055522-x11-present-fps-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=105 elapsed_seconds=5.003096 fps=20.987 present_only=1 gl_context=0
issued=105 completed=105 outstanding=0
present_request_total_ms=1.533 request_check_total_ms=1793.989 flush_total_ms=16.994
event_wait_total_ms=3182.260 completion_total_ms=5000.230 avg_completion_ms=47.621 max_completion_ms=334.250
first_msc=8589934671 last_msc=455266534495 msc_delta=446676599824
dri3_drm_name=virtio_gpu dri3_drm_version=0.1.0
```

Interpretation: the raw X11 Present-only loop under KDE/Xwayland reaches nearly
the same rate as the GLX `swap-only` result (20.987 FPS vs. 20.914 FPS), with
no GL context or draw work and zero outstanding presents. This strengthens the
case that the remaining acceleration gap is in Xwayland/Present pacing or the
xv6-facing wait/check path around Present completions rather than GL draw issue
overhead alone.

Unchecked Present request variant artifact:

```sh
KDE_SMOKE_REDUCER=x11-present-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_present_fps_variant=unchecked kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-061902-x11-present-fps-unchecked-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=107 elapsed_seconds=5.005733 fps=21.375 present_only=1 gl_context=0
issued=107 completed=107 outstanding=0
variant=unchecked request_check_enabled=0
complete_copy=107 complete_flip=0 complete_skip=0 complete_suboptimal_copy=0 last_complete_mode=0
present_request_total_ms=1.676 request_check_total_ms=0.000 flush_total_ms=14.854
event_wait_total_ms=4979.203 completion_total_ms=5002.195 avg_completion_ms=46.749 max_completion_ms=311.131
dri3_drm_name=virtio_gpu dri3_drm_version=0.1.0
QEMU trace: ctx_submit=378 set_scanout=124 res_flush=124 fence_ctrl/fence_resp=378/378 res_create_3d=65
```

Interpretation: removing the per-frame `xcb_request_check()` eliminated the
1.79s request-check bucket from the baseline run, but did not materially raise
throughput: FPS moved only from 20.987 to 21.375 while nearly all elapsed time
moved into `event_wait_total_ms`. The reducer now points more strongly at
Present completion pacing or the X11 event-wait path than at checked-request
round trips. All 107 completions used Present complete mode copy, which is a
new useful split for the next pacing reducer.

Queue-depth Present request variant artifact:

```sh
KDE_SMOKE_REDUCER=x11-present-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_present_fps_variant=queue3-unchecked kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-064628-x11-present-fps-queue3-unchecked-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=211 elapsed_seconds=5.061021 fps=41.691 present_only=1 gl_context=0
issued=211 completed=211 outstanding=0
queue_depth=3 max_outstanding=3 drain_wait_total_ms=1.166 drain_waits=2
variant=queue3-unchecked request_check_enabled=0
complete_copy=211 complete_flip=0 complete_skip=0 complete_suboptimal_copy=0 last_complete_mode=0
present_request_total_ms=5.092 request_check_total_ms=0.000 flush_total_ms=15.941
event_wait_total_ms=5019.538 completion_total_ms=15170.651 avg_completion_ms=71.899 max_completion_ms=299.129
dri3_drm_name=virtio_gpu dri3_drm_version=0.1.0
QEMU trace: ctx_submit=414 set_scanout=86 res_flush=86 fence_ctrl/fence_resp=414/414 res_create_3d=65
```

Interpretation: allowing up to three unchecked Present requests in flight
raised the Present-only loop from ~21 FPS to 41.691 FPS while still draining to
`outstanding=0`. This weakens the theory that kernel event-wait latency alone
is the limiter: `event_wait_total_ms` still spans the finite run, but more
Present completions are amortized across the same wait budget. The remaining
gap to the Linux GLX baseline now looks more like single-outstanding
swap/Present pacing or queue-depth policy than raw GL draw issue overhead.
All completions were still Present complete mode copy; no skip/flip mode was
observed.

Dynamic Present queue-depth harness update and depth-6 artifact:

```text
The xv6-owned Present FPS reducer now accepts
kde_x11_present_fps_variant=queue-unchecked with
kde_x11_present_fps_queue_depth=N for validated depths 1..8, while preserving
queue3-unchecked as the default depth-3 queue variant.
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-074258-x11-present-fps-queue-depth6-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=265 elapsed_seconds=5.064653 fps=52.323 present_only=1 gl_context=0
variant=queue-unchecked issued=265 completed=265 outstanding=0
queue_depth=6 max_outstanding=6
complete_copy=265 complete_flip=0 complete_skip=0 complete_suboptimal_copy=0
present_request_total_ms=2.951 flush_total_ms=22.341 event_wait_total_ms=5013.991
completion_total_ms=30273.964
QEMU trace: ctx_submit=434 set_scanout=70 res_flush=70 fence_ctrl/fence_resp=434/434 res_create_3d=69
```

GLX OML queue-depth swap-only artifact after the stricter Expect success gate:

```sh
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=oml-queue3-swap-only kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-071919-x11-glx-fps-oml-queue3-swap-only-strict-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=173 elapsed_seconds=5.023627 fps=34.437 variant=oml-queue3-swap-only
oml_available=1 oml_queue_depth=3 oml_sbc_issued=173 oml_sbc_completed=173 oml_max_pending_sbc=3
oml_issue_total_ms=2571.465 oml_wait_total_ms=2368.488 oml_drain_wait_total_ms=0.671 oml_gl_flush_before_swap=1
draw_total_ms=6.332 swap_total_ms=2571.465 final_xsync_ms=2.066 avg_swap_ms=14.864
QEMU trace: ctx_submit=548 set_scanout=92 res_flush=92 fence_ctrl/fence_resp=548/548 res_create_3d=54
```

Interpretation: queueing GLX swaps through `GLX_OML_sync_control` raises the
swap-only GLX loop from 20.914 FPS to 34.437 FPS, and the stricter Expect gate
now proves the final result line is the OML variant with `oml_available=1`,
queue depth 3, nonzero issued/completed SBC equality, and
`oml_max_pending_sbc=3`. This partially closes the GLX gap without patching
Mesa/Xwayland/KDE, but it still trails the Present queue3 reducer's 41.691 FPS
and the Linux KDE GLX baseline of 55.638 FPS. The remaining delta is now
reduced to GLX/Mesa/Xwayland swap pacing or throttling above the xv6 kernel's
raw Present queue-depth behavior, not to a missing GLX context or a single
kernel fd-passing/DRI3 failure.

Dynamic GLX OML queue-depth depth-6 artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-074136-x11-glx-fps-oml-queue-depth6-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=222 elapsed_seconds=5.066718 fps=43.815 variant=oml-queue-depth-swap-only
oml_available=1 oml_queue_depth=6 oml_sbc_issued=222 oml_sbc_completed=222 oml_max_pending_sbc=6
oml_issue_total_ms=4683.573 oml_wait_total_ms=275.204 oml_drain_wait_total_ms=62.137
draw_total_ms=14.298 swap_total_ms=4683.573
QEMU trace: ctx_submit=627 set_scanout=83 res_flush=83 fence_ctrl/fence_resp=627/627 res_create_3d=54
```

Interpretation: deeper queueing moves both reducers substantially. GLX depth 6
exceeds the prior Present queue3 result, 43.815 FPS versus 41.691 FPS, but
still trails Present depth 6 at 52.323 FPS and the Linux GLX baseline of
55.638 FPS. This points at queue-depth policy plus residual
GLX/Mesa/Xwayland throttling above raw Present, not an obvious kernel fence
starvation bug.

Depth-8 Present queue-depth artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-075019-x11-present-fps-queue-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=280 elapsed_seconds=5.092740 fps=54.980
variant=queue-unchecked issued=280 completed=280 outstanding=0
queue_depth=8 max_outstanding=8
complete_copy=280 complete_flip=0 complete_skip=0 complete_suboptimal_copy=0
present_request_total_ms=3.033 flush_total_ms=26.943 event_wait_total_ms=5040.579
completion_total_ms=40567.763
QEMU trace: ctx_submit=438 set_scanout=66 res_flush=66 fence_ctrl/fence_resp=438/438 res_create_3d=60
```

Depth-8 GLX OML queue-depth artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-075214-x11-glx-fps-oml-queue-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=226 elapsed_seconds=5.066070 fps=44.611
variant=oml-queue-depth-swap-only
oml_available=1 oml_queue_depth=8 oml_sbc_issued=226 oml_sbc_completed=226 oml_max_pending_sbc=8
oml_issue_total_ms=4820.616 oml_wait_total_ms=151.675 oml_drain_wait_total_ms=55.420 oml_gl_flush_before_swap=1
draw_total_ms=19.518 swap_total_ms=4820.616 final_xsync_ms=0.250 avg_swap_ms=21.330
QEMU trace: ctx_submit=630 set_scanout=80 res_flush=80 fence_ctrl/fence_resp=630/630 res_create_3d=53
```

Interpretation: Present depth 8 reaches 54.980 FPS, essentially matching the
Linux GLX control at 55.638 FPS with raw Present copy completions. GLX depth 8
only improves slightly over depth 6, 44.611 FPS versus 43.815 FPS, while OML
wait shrinks further and issue/swap MSC remains about 4.82s of the 5s run.
Queue depth explains raw Present pacing, but the residual GLX gap is now above
raw Present and likely in GLX/Mesa/Xwayland OML swap issue, flush, or
throttling. The next reducer should split OML `glFlush` versus `swap_msc`
issue timing, not start from a speculative kernel patch.

GLX OML flush/swap split reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=oml-queue-depth-flush-swap-timing kde_x11_egl_glx_fps_oml_queue_depth=8 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect

build-x86_64/kde-plasma-desktop-smoke-history/20260626-081854-x11-glx-fps-oml-flush-swap-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=213 elapsed_seconds=5.085129 fps=41.887
variant=oml-queue-depth-flush-swap-timing
oml_available=1 oml_queue_depth=8 oml_sbc_issued=213 oml_sbc_completed=213 oml_max_pending_sbc=8
oml_issue_total_ms=4770.389 oml_wait_total_ms=202.580 oml_drain_wait_total_ms=65.749 oml_gl_flush_before_swap=1
oml_gl_flush_total_ms=7.406 oml_swap_msc_issue_total_ms=4762.982
draw_total_ms=19.464 swap_total_ms=4762.982 final_xsync_ms=3.898 avg_swap_ms=22.361
QEMU trace: ctx_submit=609 set_scanout=82 res_flush=82 fence_ctrl/fence_resp=609/609 res_create_3d=54 res_xfer_toh_3d=2
```

The new xv6-owned reducer variant exists in uncommitted code as
`oml-queue-depth-flush-swap-timing` and must be requested explicitly through
`QEMU_APPEND_EXTRA`; plain `KDE_SMOKE_REDUCER=x11-glx-fps` remains
backward-compatible after the audit adjustment. The residual GLX gap is not
explained by pre-swap `glFlush` cost: `glFlush` accounts for only 7.406 ms,
while `glXSwapBuffersMscOML` issue accounts for 4762.982 ms of the 4770.389 ms
OML issue bucket. This points at the GLX/Mesa/Xwayland swap/MSC
issue/throttling path above raw Present, not at a speculative kernel fence
starvation patch. Next evidence should compare the GLX/Xwayland/Mesa swap
request path or add a GLX non-OML/present-backed split, not alter kernel
behavior.

GLX OML issue-state timing reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=oml-queue-depth-issue-state-timing kde_x11_egl_glx_fps_oml_queue_depth=8 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect

build-x86_64/kde-plasma-desktop-smoke-history/20260626-083821-x11-glx-fps-oml-issue-state-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=134 elapsed_seconds=5.067326 fps=26.444
variant=oml-queue-depth-issue-state-timing
oml_available=1 oml_queue_depth=8 oml_sbc_issued=134 oml_sbc_completed=134 oml_max_pending_sbc=8
oml_issue_total_ms=2184.040 oml_wait_total_ms=1.413 oml_drain_wait_total_ms=29.694 oml_gl_flush_before_swap=1
oml_gl_flush_total_ms=3.287 oml_swap_msc_issue_total_ms=2184.040
oml_get_sync_before_total_ms=485.399 oml_get_sync_after_total_ms=2283.982
oml_issue_state_samples=134 oml_post_issue_sbc_completed_count=33 oml_post_issue_sbc_lag_max=5
oml_post_issue_msc_delta_total=80 oml_post_issue_msc_delta_max=2
draw_total_ms=9.882 swap_total_ms=2184.040 final_xsync_ms=1.746 avg_swap_ms=16.299
QEMU trace: ctx_submit=518 set_scanout=116 res_flush=116 fence_ctrl/fence_resp=518/518 res_create_3d=52 res_xfer_toh_3d=2
```

This probe is intentionally intrusive: two extra `glXGetSyncValuesOML` calls
per issued swap lowered FPS to 26.444, so do not compare its FPS directly to
the earlier depth8 performance runs. It still proves the state sampling path:
134 samples for 134 issued SBCs, 33 post-issue samples already had
`sbc >= issued_sbc`, max post-issue lag was 5 SBC, and total MSC delta around
issue was 80 with max 2. `glXGetSyncValuesOML` itself is expensive here
(`before=485.399 ms`, `after=2283.982 ms`), so the next reducer should be less
intrusive, such as sparse state sampling or sampling only after every Nth
issue, before drawing kernel conclusions. Still no speculative kernel patch:
fences remain 1:1 and raw Present depth8 remains the performance control.

Sparse GLX OML issue-state timing reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=oml-queue-depth-issue-state-timing kde_x11_egl_glx_fps_oml_queue_depth=8 kde_x11_egl_glx_fps_oml_issue_state_sample_interval=16 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect

build-x86_64/kde-plasma-desktop-smoke-history/20260626-130930-x11-glx-fps-oml-issue-state-sparse16-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=216 elapsed_seconds=5.076195 fps=42.552
variant=oml-queue-depth-issue-state-timing
oml_available=1 oml_queue_depth=8 oml_sbc_issued=216 oml_sbc_completed=216 oml_max_pending_sbc=8
oml_issue_total_ms=4091.230 oml_wait_total_ms=17.118 oml_drain_wait_total_ms=60.962 oml_gl_flush_before_swap=1
oml_gl_flush_total_ms=6.257 oml_swap_msc_issue_total_ms=4091.230
oml_get_sync_before_total_ms=602.750 oml_get_sync_after_total_ms=263.182
oml_issue_state_sample_interval=16 oml_issue_state_sampled_ratio=14/216
oml_issue_state_first_sample_frame=1 oml_issue_state_last_sample_frame=209
oml_issue_state_first_sample_sbc=1 oml_issue_state_last_sample_sbc=209
oml_issue_state_samples=14
oml_post_issue_sbc_completed_count=1 oml_post_issue_sbc_lag_max=4
oml_post_issue_msc_delta_total=5 oml_post_issue_msc_delta_max=1
QEMU trace: ctx_submit=630 set_scanout=90 res_flush=90 fence_ctrl/fence_resp=630/630 res_create_3d=54 res_xfer_toh_3d=2
```

The harness/probe now supports
`kde_x11_egl_glx_fps_oml_issue_state_sample_interval=N` /
`HOST_X11_EGL_GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL` for
`oml-queue-depth-issue-state-timing`. The dense/default interval is 1, the
validated range is 1..64, and the sparse Expect gate validates the exact sample
count plus first/last sample cadence. This run used interval 16 and produced
14 samples, matching `ceil(216/16)=14`, from frame/SBC 1 through 209.

Interpretation: sparse issue-state sampling is materially less intrusive than
the dense timing probe and roughly in line with the previous flush/swap
issue-time profile, while still below the Linux GLX baseline of 55.638 FPS. It
reinforces that `glXSwapBuffersMscOML`/swap pacing remains the main residual
gap: sampled post-issue state mostly lagged, with only 1 sampled post-issue SBC
already complete. The next reducer/fix should continue from
GLX/Mesa/Xwayland swap/MSC pacing evidence, not fd passing/DRI3 or pre-swap
`glFlush`. Audit caveats: the status file records the reducer, `run.log` does
not literally include the env assignment, `cmdline` has
`kde_smoke_require_chromium=1` followed by the later `=0` override, no Weston
matches were present, PNGs are nonzero, and some sidecar logs are zero-byte.

Swap-interval-0 plain GLX swap-only reducer artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-095514-x11-glx-fps-swap-interval0-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_swap_interval status=PASS requested=0 set_api=EXT before=1 after=0 ext_present=1 mesa_present=1 sgi_present=0
phase=glx_fps_result status=PASS frames=232 elapsed_seconds=5.046647 fps=45.971 swap_only_skipped_draw_frames=231 variant=swap-interval0-swap-only swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0 plain_swap_issue_total_ms=4965.974 plain_swap_avg_ms=21.405 plain_swap_max_ms=80.011
QEMU trace: ctx_submit=643 set_scanout=81 res_flush=81 fence_ctrl/fence_resp=643/643 res_create_3d=54 res_xfer_toh_3d=2
```

This is a diagnostic xv6-owned probe/harness change, not a KDE, Mesa, or
Xwayland source patch. Xwayland GLX accepted swap interval 0 through EXT, and
the MESA getter observed the interval move from 1 before the call to 0 after
it. Even with drawing skipped after the first frame and swap interval set to
0, ordinary `glXSwapBuffers` spent almost the entire run in swap
(`plain_swap_issue_total_ms=4965.974`) and produced 45.971 FPS. That is close
to, but still below, the Linux GLX baseline of 55.638 FPS and the raw Present
depth-8 result of 54.980 FPS. This narrows the remaining gap to plain
GLX/Xwayland swap path behavior above raw Present, not pre-swap draw work,
OML-only throttling, or missing DRI3/fd passing.

Swap-interval-0 plain GLX swap-only OML-state reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-oml-state-swap-only kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-103840-x11-glx-fps-swap-interval0-oml-state-strict-pass/
Verification: git diff --check, focused reducer PASS with stricter after=0 Expect gate
Earlier same C/rootfs payload before the gate-only Expect fix: static diff checks, host-gui-runtime, rootfs-refresh, reducer PASS
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_swap_interval status=PASS requested=0 set_api=EXT before=1 after=0
phase=glx_fps_result status=PASS frames=124 elapsed_seconds=5.006683 fps=24.767 variant=swap-interval0-oml-state-swap-only
swap_only_skipped_draw_frames=123
plain_oml_available=1 plain_oml_samples=124
plain_swap_issue_total_ms=2291.021 plain_swap_avg_ms=18.476 plain_swap_max_ms=86.867
plain_oml_get_sync_before_total_ms=275.566 plain_oml_get_sync_after_total_ms=1890.125
plain_oml_post_swap_sbc_delta_total=84 max=5
plain_oml_post_swap_msc_delta_total=58 max=2
plain_post_swap_xsync_total_ms=448.629 max=15.300
QEMU trace: ctx_submit=480 set_scanout=107 res_flush=107 fence_ctrl/fence_resp=480/480 res_create_3d=52 res_xfer_toh_3d=2
```

This variant is intentionally intrusive: it samples OML state before and after
every ordinary `glXSwapBuffers` and performs post-swap XSync, so its FPS should
not be compared directly to the prior non-sampling plain interval0 run. It
is now anchored by the final strict-pass artifact above, proving the durable
evidence path for ordinary swaps: swap interval 0 remained accepted with exact
after=0 verification, samples matched frames, post-swap OML state advanced
across the run, and virtgpu fences remained 1:1. The heavy after-swap
`glXGetSyncValuesOML` cost plus post-swap XSync cost explain much of the FPS
drop from the prior 45.971 FPS run. Continue with sparse/plain-swap state
sampling or GLX/Xwayland/Mesa swap-path attribution, not speculative
kernel/DRI3/fd-passing patches.

Sparse swap-interval-0 plain GLX swap-only OML-state reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-oml-state-sampled-swap-only kde_x11_egl_glx_fps_oml_issue_state_sample_interval=16 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-110829-x11-glx-fps-swap-interval0-oml-state-sampled16-pass/
Verification: static diff checks across root/kernel/user/ports, host-gui-runtime, rootfs-refresh, focused no-Weston reducer PASS on first run with no retry
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_swap_interval status=PASS requested=0 set_api=EXT before=1 after=0
phase=glx_fps_result status=PASS frames=210 elapsed_seconds=5.026245 fps=41.781 variant=swap-interval0-oml-state-sampled-swap-only
swap_only_skipped_draw_frames=209
plain_oml_available=1 plain_oml_samples=14 plain_oml_sample_interval=16 plain_oml_sampled_ratio=14/210 plain_oml_first_sample_frame=1 plain_oml_last_sample_frame=209
plain_swap_issue_total_ms=4224.962 plain_swap_avg_ms=20.119 plain_swap_max_ms=134.957
plain_oml_get_sync_before_total_ms=517.952 plain_oml_get_sync_after_total_ms=217.762
plain_oml_post_swap_sbc_delta_total=17 max=3
plain_oml_post_swap_msc_delta_total=7 max=1
plain_post_swap_xsync_total_ms=0.000 max=0.000
QEMU trace: ctx_submit=667 set_scanout=102 res_flush=102 fence_ctrl/fence_resp=667/667 res_create_3d=279 res_xfer_toh_3d=2
```

Sparse sampling avoids the dense sampler's per-frame post-swap XSync and
reduces observer cost: FPS returned to 41.781, much closer to the non-sampling
interval0 run at 45.971 than the dense sampler at 24.767, but still below
Linux GLX at 55.638 and raw Present depth8 at 54.980. The 14 samples match
`ceil(210/16)=14`, OML state advanced without regression, and virtgpu fences
remained 1:1. This keeps attribution above raw DRI3/Present/fd passing/fence
starvation; the remaining gap is ordinary GLX/Mesa/Xwayland swap pacing plus
sampled `glXGetSyncValuesOML` overhead. Next evidence should compare sparse
interval choices or GLX/Xwayland/Mesa swap-path attribution, not a speculative
kernel patch.

Sparse swap-interval-0 plain GLX swap-only post-swap-only OML-state reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-oml-state-sampled-after-swap-only kde_x11_egl_glx_fps_oml_issue_state_sample_interval=16 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-113134-x11-glx-fps-swap-interval0-oml-state-sampled-after16-pass/
Verification: static diff checks across root/kernel/user/ports, host-gui-runtime, rootfs-refresh, focused no-Weston reducer PASS on first run with no retry; the history copy had its copied kde-plasma.fs.img removed only from the copy
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_swap_interval status=PASS requested=0 set_api=EXT before=1 after=0
phase=glx_fps_result status=PASS frames=241 elapsed_seconds=5.011420 fps=48.090 variant=swap-interval0-oml-state-sampled-after-swap-only
swap_only_skipped_draw_frames=240
plain_oml_available=1 plain_oml_samples=16 plain_oml_sample_interval=16 plain_oml_sampled_ratio=16/241 plain_oml_first_sample_frame=1 plain_oml_last_sample_frame=241
plain_swap_issue_total_ms=4393.292 plain_swap_avg_ms=18.229 plain_swap_max_ms=76.994
plain_oml_get_sync_before_total_ms=0.000 plain_oml_get_sync_after_total_ms=566.516
plain_oml_before_first_sbc=0 plain_oml_before_last_sbc=0 plain_oml_after_first_sbc=0 plain_oml_after_last_sbc=239
plain_oml_post_swap_sbc_delta_total=0 max=0 plain_oml_post_swap_msc_delta_total=0 max=0
plain_oml_after_sbc_delta_total=239 max=19 plain_oml_after_msc_delta_total=72 max=6
plain_post_swap_xsync_total_ms=0.000 max=0.000
QEMU trace: ctx_submit=653 set_scanout=79 res_flush=79 fence_ctrl/fence_resp=653/653 res_create_3d=43 res_xfer_toh_3d=2
```

This is xv6-owned harness/probe evidence, not a KDE/Mesa/Xwayland source patch.
It removes the pre-swap OML observer from the sparse interval16 sampler and
keeps post-swap XSync at zero. FPS rose to 48.090, above the earlier sparse
before+after sampler at 41.781 and slightly above the non-sampling interval0
plain swap-only run at 45.971, but still below the Linux GLX baseline of
55.638 and raw Present depth8 at 54.980. That attributes much of the earlier
sparse sampler penalty to pre-swap `glXGetSyncValuesOML`; the residual gap
remains ordinary GLX/Mesa/Xwayland swap path behavior above raw Present, not a
reason for speculative kernel/DRI3/fd-passing patches. The log does not emit
max values for OML get-sync before/after, nor first/last MSC fields for plain
after-swap samples.

Sparse swap-interval-0 plain GLX swap-only post-swap-only OML-state interval64 reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-oml-state-sampled-after-swap-only kde_x11_egl_glx_fps_oml_issue_state_sample_interval=64 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
Pre-reducer KWin startup crash: build-x86_64/kde-plasma-desktop-smoke-history/20260626-114317-x11-glx-fps-swap-interval0-oml-state-sampled-after64-fail/
Passing retry: build-x86_64/kde-plasma-desktop-smoke-history/20260626-114435-x11-glx-fps-swap-interval0-oml-state-sampled-after64-pass/
Verification: runtime-only; no source/docs edits before run, no build needed; static diff checks across top/kernel/user/ports passed; one pre-reducer KWin startup crash preserved; one retry PASS; no QEMU/smoke process remained afterward
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
probe_glx_fps_variant=swap-interval0-oml-state-sampled-after-swap-only
probe_glx_fps_oml_issue_state_sample_interval=64
frames=218 elapsed_seconds=5.046774 fps=43.196 variant=swap-interval0-oml-state-sampled-after-swap-only
swap_only_skipped_draw_frames=217
swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0
plain_swap_issue_total_ms=4718.298 plain_swap_avg_ms=21.644 plain_swap_max_ms=61.096
plain_oml_available=1 plain_oml_samples=4 plain_oml_sample_interval=64 plain_oml_sampled_ratio=4/218 plain_oml_first_sample_frame=1 plain_oml_last_sample_frame=193
plain_oml_get_sync_before_total_ms=0.000 plain_oml_get_sync_after_total_ms=209.927
plain_post_swap_xsync_total_ms=0.000 plain_post_swap_xsync_max_ms=0.000
plain_oml_after_first_sbc=1 plain_oml_after_last_sbc=192
plain_oml_after_sbc_delta_total=191 max=66 plain_oml_after_msc_delta_total=55 max=19
QEMU trace: ctx_submit=617 set_scanout=82 res_flush=82 fence_ctrl/fence_resp=617/617 res_create_3d=54 res_xfer_toh_3d=2
```

This was a runtime-only use of the committed xv6-owned reducer, not a
KDE/Mesa/Xwayland source patch. Interval64 lowered after-swap OML samples to
4 and kept pre-swap get-sync plus post-swap XSync at zero, but FPS fell to
43.196: below interval16 after-only at 48.090, non-sampling interval0 at
45.971, raw Present depth8 at 54.980, and Linux GLX at 55.638. The remaining
gap therefore did not disappear with lower OML observer frequency. Continue
with GLX/Xwayland/Mesa swap-path attribution above raw Present; do not infer a
kernel DRI3/fd-passing/fence issue from this run.

The first GLX depth-8 attempt failed before the reducer due to the known KWin
startup crash class and is preserved separately:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-075110-x11-glx-fps-oml-depth8-kwin-startup-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
```

Treat this as a pre-probe/session stability artifact, not GLX timing evidence.

Two non-passing attempts before the final queue-depth pass were preserved:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-063946-x11-present-fps-queue3-unchecked-kwin-startup-crash/
build-x86_64/kde-plasma-desktop-smoke-history/20260626-064057-x11-present-fps-queue3-unchecked-outstanding-fail/
```

The first failed before the reducer due to the known KWin startup crash class.
The second contained a valid host probe result with `outstanding=0`, but the
Expect parser misread `max_outstanding=3` as the primary outstanding field; the
parser now matches the adjacent `issued completed outstanding` fields.

Default KDE regression artifact after the Present FPS harness:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-055748-default-kde-smoke-pass-after-present-fps-harness/
KDE-PLASMA-DESKTOP-SMOKE-DONE
KDE_SMOKE_AGENT_DONE status=PASS
Xwayland KDE wrapper: ... glamor=off effective_glamor=off ... enable_glx=0
kde_app_launch_probe ... konsole=1 ... dolphin=1 ... chromium=1 ... status=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... chromium=1 ... pipewire=1 ... status=PASS
```

The known raw KWin screenshot readback warning remained
`kde_kwin_screenshot_probe result=FAIL detail=low-color-detail`; the overall
default smoke still passed.

A same-command retry before the timing pass hit a known pre-probe KWin startup
crash signature and was preserved separately at:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-014657-x11-glx-fps-timing-kwin-crash/
```

Default KDE regression artifact after the session-probe change:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-053252-default-kde-smoke-pass-after-session-glx-fps/
KDE-PLASMA-DESKTOP-SMOKE-DONE
Xwayland KDE wrapper: ... glamor=off effective_glamor=off ... enable_glx=0
kde_app_launch_probe ... konsole=1 ... dolphin=1 ... chromium=1 ... status=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... chromium=1 ... status=PASS
KDE_SMOKE_AGENT_DONE status=PASS
```

Default KDE regression artifact after the GLX FPS variant harness update:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-052229-default-kde-smoke-pass-after-glx-variant-harness/
KDE-PLASMA-DESKTOP-SMOKE-DONE
Xwayland KDE wrapper: ... glamor=off effective_glamor=off ... enable_glx=0
kde_app_launch_probe ... konsole=1 ... dolphin=1 ... chromium=1 ... status=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... chromium=1 ... status=PASS
kde_kwin_screenshot_probe result=FAIL detail=low-color-detail
KDE_SMOKE_AGENT_DONE status=PASS
```

Remaining success criteria:

- Improve GLX FPS toward the Linux baseline or explain the remaining delta with
  reduced evidence.
- QEMU trace shows Xwayland/client 3D submission comparable in shape and rate
  to the Linux baseline.
- Default KDE smoke stays green and remains nonblack/responsive.

### 2. KDE Performance Parity

Status: open after GLX/Present queue-depth proof; FPS is improved but still
below the Linux KDE GLX baseline.

The current xv6 KDE desktop is visually correct and responsive, and focused
Xwayland GLX context/draw proof now works with automatic policy mapped to
effective `-glamor es`. The deterministic KDE/X11 FPS reducer now records a
direct GLX/virgl result of 93 frames over 5.039396 seconds, or 18.455 FPS, in
the phase-detail pass. The trace-disabled confirmation pass recorded 85 frames
over 5.024329 seconds, or 16.918 FPS, confirming the older trace-heavy FPS
numbers were materially depressed by diagnostic logging. xv6 still does not
match the Linux KDE GLX baseline of 55.638 FPS.

The latest phase-detail pass records `draw_total_ms=3539.314`,
`swap_total_ms=1475.927`, `avg_swap_ms=15.870`,
`gl_issue_total_ms=3537.151`, and `gl_error_total_ms=2.163`, so the draw bucket
is not `glGetError`; it is dominated by the existing GL issue calls. The
`swap-only` variant then reduced draw time to 23.988 ms over the run but still
recorded only 20.914 FPS with `swap_total_ms=4969.928`, strengthening the case
that GLX swap/Present pacing rather than clear-call issue overhead dominates
the remaining FPS gap. The Present queue3 reducer reached 41.691 FPS with three
requests in flight, while the GLX OML queue3 swap-only reducer reached
34.437 FPS with `oml_sbc_issued=173`, `oml_sbc_completed=173`, and
`oml_max_pending_sbc=3`. Parameterized depth-6 queueing moved Present to
52.323 FPS and GLX OML swap-only to 43.815 FPS; GLX depth 6 now exceeds the
prior Present queue3 result but still trails Present depth 6 and the Linux GLX
baseline. Depth-8 queueing then moved raw Present to 54.980 FPS, essentially
matching the Linux GLX control at 55.638 FPS, while GLX OML swap-only only
rose to 44.611 FPS and spent about 4.82s in OML issue/swap MSC time. Queue
depth now explains raw Present pacing; the residual gap sits above raw Present
in GLX/Mesa/Xwayland OML swap issue or throttling. The flush/swap split
variant recorded only 7.406 ms in pre-swap `glFlush` but 4762.982 ms in
`glXSwapBuffersMscOML` issue at depth 8. The issue-state timing variant proved
the sampling path with 134 samples for 134 issued SBCs, 33 post-issue samples
already complete, max post-issue lag 5 SBC, and MSC delta 80 total / 2 max, but
its two extra `glXGetSyncValuesOML` calls per issue were intrusive enough to
drop the run to 26.444 FPS. Sparse issue-state sampling at interval 16 then
recorded 216 frames at 42.552 FPS with 14/216 samples, only 1 sampled
post-issue SBC already complete, max lag 4 SBC, and MSC delta 5 total / 1 max.
The later `swap-interval0-swap-only` plain GLX run accepted swap interval 0 via
EXT, observed the MESA getter move from 1 to 0, and reached 232 frames at
45.971 FPS while ordinary `glXSwapBuffers` still consumed 4965.974 ms of the
run. That keeps the evidence on GLX/Mesa/Xwayland swap-path pacing above raw
Present rather than fd passing/DRI3, pre-swap draw work, OML-only throttling,
or a speculative kernel patch. The current virtgpu fence trace still shows
submitted and responded fences matching 1:1, so raw virtgpu fence starvation
remains unproven. The
remaining `DRM_IOCTL_SYNCOBJ_EVENTFD` probe returns Linux-shaped `ENOENT` for
`handle=0`.

Follow-up diagnostic artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-061409-x11-glx-fps-syncobj-eventfd-diag-pass/
```

Key diagnostic line:

```text
chrome-drm-detail: syncobj-eventfd owner=3:68 ret=-22 handle=0 flags=0x0 point=0 fd=-1 pad=0 fd_is_eventfd=-1 syncobj_exists=-1 state_has_fence=-1 reject_reason=invalid_args
```

Interpretation: the first Xwayland `DRM_IOCTL_SYNCOBJ_EVENTFD` call in this
run is an invalid/feature-probe-shaped request, not a real eventfd arm:
`handle=0` and `fd=-1`. The diagnostic patch intentionally preserves current
errno behavior.

Pre-fix focused reducer artifact:

```text
build-x86_64/drmabitest-syncobj-eventfd-validation-virgl-20260626-030504/
```

Command run inside a no-Weston virgl boot:

```sh
/bin/drmabitest --syncobj-eventfd-validation
```

Result summary:

```text
expect_status=0
rows=12
pass=6
fail=6
skip=0
```

Key rows:

```text
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd_minus1: handle=0 fd=-1 ret=-22 errno=22 linux_errno=2 status=FAIL
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd0: handle=0 fd=0 ret=-22 errno=22 linux_errno=2 status=FAIL
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_eventfd: handle=0 fd=5 ret=-2 errno=2 linux_errno=2 status=PASS
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_fd_minus1: handle=1 fd=-1 ret=-22 errno=22 linux_errno=9 status=FAIL
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_non_eventfd: handle=1 fd=6 ret=-22 errno=22 linux_errno=22 status=PASS
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_eventfd: handle=1 fd=5 ret=0 errno=0 linux_errno=0 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd_minus1: handle=0 fd=-1 ret=-22 errno=22 linux_errno=2 status=FAIL
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd0: handle=0 fd=0 ret=-22 errno=22 linux_errno=2 status=FAIL
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_eventfd: handle=0 fd=5 ret=-2 errno=2 linux_errno=2 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_fd_minus1: handle=2 fd=-1 ret=-22 errno=22 linux_errno=9 status=FAIL
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_non_eventfd: handle=2 fd=6 ret=-22 errno=22 linux_errno=22 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_eventfd: handle=2 fd=5 ret=0 errno=0 linux_errno=0 status=PASS
```

Interpretation: xv6 validated some eventfd arguments before matching Linux's
syncobj-handle ordering.

Post-fix focused reducer artifact:

```text
build-x86_64/drmabitest-syncobj-eventfd-validation-virgl-20260626-032843-kernel-order-pass/
```

Result summary:

```text
expect_status=0
rows=12
pass=12
fail=0
skip=0
```

Key rows:

```text
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd_minus1: handle=0 fd=-1 ret=-2 errno=2 linux_errno=2 status=PASS
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd0: handle=0 fd=0 ret=-2 errno=2 linux_errno=2 status=PASS
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_fd_minus1: handle=1 fd=-1 ret=-9 errno=9 linux_errno=9 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd_minus1: handle=0 fd=-1 ret=-2 errno=2 linux_errno=2 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd0: handle=0 fd=0 ret=-2 errno=2 linux_errno=2 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_fd_minus1: handle=2 fd=-1 ret=-9 errno=9 linux_errno=9 status=PASS
```

Interpretation: kernel-side `DRM_IOCTL_SYNCOBJ_EVENTFD` validation ordering now
matches the reducer: invalid syncobj handles return `ENOENT` before fd
validation, and a valid syncobj with `fd=-1` returns `EBADF`. The broader
queued eventfd waiter lifetime concern remains a separate follow-up that needs
its own reducer before behavior-changing cleanup.

Post-fix KDE X11/GLX FPS retry artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-033211-x11-glx-fps-after-syncobj-order-no-display-fail/
```

Result:

```text
KDE-PLASMA-DESKTOP-SMOKE-FAIL x11-glx-fps-session-probe-FAIL exit_status=1
host-x11-egl-smoke: phase=session_probe status=FAIL mode=session probe_mode=glx-fps exit_status=1 reason=no-display-candidate
chrome-drm-detail: syncobj-eventfd owner=3:66 ret=-2 handle=0 flags=0x0 point=0 fd=-1 pad=0 fd_is_eventfd=-1 syncobj_exists=0 state_has_fence=0 reject_reason=syncobj_missing
```

Interpretation: the KDE retry confirms the Xwayland invalid syncobj eventfd
probe now returns Linux-shaped `ENOENT`, but this run failed before GLX/FPS
measurement because the session probe could not open an authorized X display.
It should not be used as FPS evidence.

Session-probe auth diagnostic harness update:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-040250-x11-egl-auth-fix-glamor-auto-kwin-startup-crash/
build-x86_64/kde-plasma-desktop-smoke-history/20260626-040447-x11-egl-auth-diagnostics-glamor-off-glx-server-fail/
```

Results:

```text
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
pid 49 kwin_wayland: exception 13 (#GP General Protection) rip=0x7ffffcc2cd9d

host-x11-egl-smoke: diag xwayland_auth label=best pid=71 display=:0 auth_path=(unset) argv=/bin/Xwayland.real -glamor off ... :0 ...
host-x11-egl-smoke: phase=x11_preflight_candidate status=PASS display=:0 exit_status=0
host-x11-egl-smoke: phase=egl_initialize status=FALLBACK reason=eglInitialize egl_error=0x3001 error_name=EGL_NOT_INITIALIZED fallback=glx
host-x11-egl-smoke: diag glx_query_extension result=FAIL present=0 error_base=0 event_base=0
host-x11-egl-smoke: diag glx_client_strings result=PASS vendor=Mesa Project and SGI version=1.4
host-x11-egl-smoke: diag glx_server_strings result=FAIL vendor=(null) version=(null)
host-x11-egl-smoke: diag glx_fbconfigs result=FAIL count=0 ptr=(nil)
host-x11-egl-smoke: phase=session_probe status=FAIL mode=session probe_mode=glx-probe exit_status=1
```

Interpretation: the xv6-owned KDE session probe now records Xwayland auth
discovery, display-specific auth candidates, launch-time `XAUTHORITY`
selection, and GLX fallback phases in `host-gui-host-x11-egl-smoke.log`.
The glamor-auto run still failed before the probe because KWin crashed during
startup, so it is a startup artifact rather than GLX evidence. The glamor-off
control reached Xwayland and proves the current failing surface: no auth file is
present for `-glamor off`, X11 connect succeeds, EGL-on-X11 fails to initialize,
and GLX has client strings but no server extension/version/fbconfigs.

Success criteria:

- KDE smoke passes.
- X11 GL smoke FPS is recorded under xv6.
- QEMU trace counts and renderer logs are captured in a stable artifact.
- Weston/Chromium controls remain green.

### 3. Chromium As Regression / Stress Probe

Status: open only for current reproducible ABI gaps.

Do not reopen old Chromium issues unless a current run reproduces them. Current
Chromium work should focus on:

- Fullscreen YouTube: prove deterministic 720p fullscreen playback and cursor
  motion over a long enough capture window.
- VA/video decode: Mesa Gallium VA is staged, but hardware decode selection is
  not yet proven by Chromium diagnostics. Keep this as an open proof item.
- Normal launcher/minimal flags: continue only from current evidence that
  proves a Linux ABI surface, not from stale first-run/profile artifacts.
- X11 presentation timing should remain separate from native Wayland Chromium
  paths, especially when investigating `GetVSyncParametersIfAvailable()` or
  Xwayland/Present behavior.

Success criteria:

- Current harness artifacts include host HTTP evidence when relevant,
  role-derived Chromium process evidence, screenshot/frame evidence, and a
  reduced ABI explanation for any failure.
- Local video gate stays green after any GPU/DRM-visible change.

### 4. Audio ABI Completeness

Status: partial.

The current audio path is enough for GUI smoke:

- OSS `/dev/dsp` works.
- Minimal ALSA playback hardware enumeration works.
- libasound can play through the current default path.

Remaining gaps are future Linux ABI work only if a real program proves it needs
them:

- ALSA capture.
- mmap PCM.
- async notification.
- timer devices.
- mixer/control richness.
- channel-map/status completeness.

### 5. Kernel Static Analysis Debt

Status: open but not a KDE blocker.

`kernel-sparse` has pre-existing Hyper-V sparse parse/context warnings. Do not
block KDE/virgl work on them, but do run focused kernel builds and `diff
--check` for touched files.

## Regression Gates

Run these when their layer changes:

```sh
git diff --check
git -C kernel diff --check
git -C ports diff --check
cmake --build build-x86_64 --target kernel -j2
cmake --build build-x86_64 --target rootfs-refresh -j2
timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
REPO_ROOT=/home/es/xv6-os timeout 320 expect scripts/gpu/perf-video-gate.expect
```

Use Chromium/YouTube and focused X11 probes as targeted controls when the
changed surface can affect them.

## Closed For Current KVM/virgl Target

Keep these out of the active queue unless a new current regression reopens
them:

- Core DRM node bring-up.
- KMS modesetting for current virgl path.
- GBM/EGL/Mesa virgl bring-up for KWin.
- PRIME/dma-buf basics.
- syncobj/sync_file basics.
- vblank basics.
- cursor bring-up for the current GUI path.
- `drmabitest` baseline.
- NetSurf as a primary target.
- Old Weston desktop restoration work.
- Historical Chromium first-run/profile, `/dev/kbd`, `/dev/shm`, procfs maps,
  clone/pidfd, file-lock, stream EOF, and SCM fixes unless a fresh run
  produces a new minimal reducer.

## Out Of Scope

- Chromium sandbox/user namespace/seccomp work, unless explicitly requested.
- Hyper-V DXG/GPU-P native-present work.
- Nouveau/DDA real-hardware support.
- RISC-V graphics.
- Full desktop-service completeness unless reduced to a Linux ABI mismatch.
