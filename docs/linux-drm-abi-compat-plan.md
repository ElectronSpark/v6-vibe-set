# Linux DRM / GUI ABI Compatibility Plan

Last updated: 2026-06-28.

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

### Linux VM, WSL2, GLX, And Chromium Evidence Archive

Detailed Linux VM virgl/GBM controls, WSL2 host GPU/source-alignment proof,
Xwayland/GLX reducer history, KDE performance notes, and Chromium reducer
chronology from June 27-30, 2026 now live in
`docs/linux-drm-abi-compat-evidence-2026-06-27-30.md`.

Current distilled conclusions:

- Linux KVM+virgl exposes `/dev/dri/card0` and `/dev/dri/renderD128`; GBM and
  surfaceless EGL initialize with Mesa virgl. Host WSL2 GBM absence is a host
  limitation, not an xv6 guest regression.
- Linux same-Chromium controls prove the imported Chrome binary can launch in a
  virgl Linux guest, but the useful xv6-vs-Linux comparison is still role,
  render-node, and playback evidence rather than a complete Linux DevTools/FPS
  oracle.
- The Xwayland/GLX gap has been reduced to swap/Present pacing above raw kernel
  DRI3/fence/fd-passing basics. Do not patch KDE, Qt, KWin, Xwayland, Mesa, or
  Chromium for this lane.
- Chromium is currently a stress/regression probe. Recent evidence moved past
  deterministic RELA/loader corruption and into GPU-process selection/fallback
  plus frame-pacing behavior during video playback.

## Active Gaps

### 1. Xwayland GLAMOR / GLX Acceleration

Status: partially closed; parity remains open.

The stable KDE desktop smoke still uses the guarded Xwayland path. Focused
reducers can request `kde_xwayland_glamor=auto` and the xv6-owned Xwayland
wrapper maps that to `-glamor es` with GLX enabled. Evidence in the archive
shows GLX contexts and draw probes now pass, and reducer variants narrowed the
remaining gap to GLX/Mesa/Xwayland swap/MSC pacing rather than missing DRM
nodes, DRI3 fd passing, or fence starvation.

Current direction:

- Keep the default smoke stable and no-Weston.
- Use focused GLX/Present reducers for attribution only.
- Compare GLX/Xwayland/Mesa swap-path behavior before considering kernel
  behavior changes.
- Preserve upstream KDE/Qt/KWin/Xwayland/Mesa sources.

### 2. KDE Performance Parity

Status: open.

Linux KDE KVM+virgl remains the reference in
`docs/linux-kde-performance-baseline.md`. xv6 has working KWin/Plasma/virgl
bring-up and visible desktop proof, but GLX/Present pacing and Chromium video
presentation remain below the Linux controls.

Current direction:

- Treat FPS deltas as reducer targets, not broad desktop rewrites.
- Keep screenshots/logs for every GUI proof.
- Prefer kernel/libc/rootfs/harness fixes when evidence points below imported
  KDE/Qt/KWin/Xwayland/Mesa packages.

### 3. Chromium As Regression / Stress Probe

Status: launch path mostly contained; video performance still failing.

Recent Chromium evidence is archived through
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T002654Z-eglangle-opengles-fair-fps29-drop48`.
That run reached page playback without a kernel trap or Chromium INT3, but
failed the performance gate: `fps=29.2`, `presentedFPS=29.2`,
`decodedFPS=63.5`, `dropPct=47.65`. Chromium launched `egl-angle/opengles`
GPU processes that exited with `Requested GL implementation
(gl=none,angle=none)`, then continued with a `--use-gl=disabled` GPU process.

Current direction:

- Keep Chromium/Mesa upstream-clean.
- Use xv6-owned launch/runtime probes to compare GPU-process argv, EGL/ANGLE
  admission, renderer roles, and render-node activity.
- Keep the stable desktop default while using explicit Chromium GL flags only
  as reducers.
- Continue collecting video stalls as GPU-process restart, EGL admission,
  scanout/present pacing, or real memory/BO-growth evidence.

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
