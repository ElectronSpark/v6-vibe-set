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
build-x86_64/kde-plasma-desktop-smoke-history/20260626-061223-x11-glx-fps-timing-pass/
```

Result marker:

```text
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
```

Key evidence:

```text
Xwayland KDE wrapper: ... glamor=auto effective_glamor=es ... enable_glx=1
host-x11-egl-smoke: phase=glx-fps status=BEGIN mode=session display=:0
host-x11-egl-smoke: phase=start status=BEGIN mode=glx-fps
host-x11-egl-smoke: phase=gl_strings status=PASS api=glx vendor=Mesa renderer=virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)) gl_version=4.2 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.2
host-x11-egl-smoke: phase=glx_fps status=BEGIN mode=glx-fps ... direct_available=1 direct=1
host-x11-egl-smoke: phase=glx_fps_timing status=PASS result_status=PASS frames=31 event_total_ms=2.962 draw_total_ms=2979.471 swap_total_ms=2140.386 final_xsync_ms=237.682 max_swap_ms=94.047 max_draw_ms=411.108 max_event_ms=1.112 avg_swap_ms=69.045
host-x11-egl-smoke: phase=glx_fps_result status=PASS ... frames=31 elapsed_seconds=5.362771 fps=5.781 ... direct=1
host-x11-egl-smoke: phase=session_probe status=PASS mode=session probe_mode=glx-fps exit_status=0
chrome-drm-detail: virtgpu-context-create ... proc=Xwayland.real ... capset=2 ... ret=0
chrome-drm-detail: virtgpu-context-first-submit-execbuffer ... proc=Xwayland.real ... capset=2 ... ret=0
chrome-drm-detail: virtgpu-context-create ... proc=ld-linux-x86-64 ... capset=2 ... ret=0
chrome-drm-detail: virtgpu-context-first-submit-execbuffer ... proc=ld-linux-x86-64 ... capset=2 ... ret=0
```

Interpretation: the interactive serial-shell launch path was the reducer
blocker, not the GLX probe itself. The active FPS reducer now uses the
xv6-owned KDE session process to launch `/bin/host-x11-egl-smoke --glx-fps`,
which avoids KDE log noise corrupting typed shell commands. GLX starts, binds a
direct virgl context, records renderer strings, submits 3D work, and completes
the finite FPS loop. The added phase timing shows the low FPS is not event
drain (`2.962 ms` total); most time is split between pre-swap GL work
(`2979.471 ms`) and `glXSwapBuffers` (`2140.386 ms`, `69.045 ms` average).
The measured FPS is still far below the Linux baseline, so this closes the
GLX/FPS startup evidence gap but not performance parity.

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

Remaining success criteria:

- Improve GLX FPS toward the Linux baseline or explain the remaining delta with
  reduced evidence.
- QEMU trace shows Xwayland/client 3D submission comparable in shape and rate
  to the Linux baseline.
- Default KDE smoke stays green and remains nonblack/responsive.

### 2. KDE Performance Parity

Status: open after GLX FPS timing proof; FPS is recorded but low.

The current xv6 KDE desktop is visually correct and responsive, and focused
Xwayland GLX context/draw proof now works with automatic policy mapped to
effective `-glamor es`. The deterministic KDE/X11 FPS reducer now records a
direct GLX/virgl result of 31 frames over 5.362771 seconds, or 5.781 FPS. Its
timing reducer records negligible event-drain overhead and concentrates the
remaining delay in pre-swap GL dispatch plus `glXSwapBuffers`. It does not yet
match the Linux KDE baseline of 55.638 FPS.

Next evidence target: reduce or instrument the Xwayland/DRI3 Present and
syncobj notification path, especially the current
`DRM_IOCTL_SYNCOBJ_EVENTFD` `-EINVAL` from Xwayland, before making any kernel
behavior change. The current virtgpu fence trace still shows submitted and
responded fences matching 1:1, so raw virtgpu fence starvation is not proven.

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

Current focused reducer artifact:

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

Interpretation: xv6 currently validates some eventfd arguments before matching
Linux's syncobj-handle ordering. The next semantic step is kernel-side
`DRM_IOCTL_SYNCOBJ_EVENTFD` validation ordering: invalid syncobj handles should
return `ENOENT` before fd validation, and a valid syncobj with `fd=-1` should
return `EBADF` rather than the current `EINVAL`.

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
