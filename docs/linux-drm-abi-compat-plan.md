# Linux DRM / GUI ABI Compatibility Plan

Last updated: 2026-07-01.

This is the active handoff plan for Linux GUI/DRM ABI work in
`/home/es/xv6-os`. It intentionally keeps only current gaps and guardrails.
Long historical evidence trails live in git history, ABI audit docs, and run
artifacts.

## Current Direction

KDE Plasma is the primary desktop target. Chromium remains the main stress and
regression probe. Weston is explicitly skipped for current work: do not use it,
do not reconcile its source drift, and do not modify it unless the user
explicitly reopens that lane. KDE/Qt/KWin/Plasma source must stay
upstream-clean except for marker-only xv6 metadata. If KDE fails, reduce it to a
Linux ABI gap and prefer fixes in the kernel, libc/sysroot, rootfs data, or
build/staging wrappers.

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
- Skip Weston entirely unless explicitly requested.
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

### Diagnostic Controls

The canonical diagnostics status is now `docs/active-work-plan.md` under
`Current Evidence Snapshot / KASAN and kmemleak` and `Logging`. Keep detailed
KASAN/KLOG/KMEMLEAK artifacts there instead of duplicating them in this GUI
plan.

GUI-facing impact:

- Kernel submodule commit
  `12ce331 kernel: add diagnostic sanitizers and log controls` is the current
  diagnostics checkpoint.
- KASAN/KMEMLEAK/KLOG remain build-time and runtime disableable, so KDE,
  Chromium, GPU, network, audio, and disk smoke can run with diagnostics quiet
  or compiled out when measuring performance.
- `/proc/kmsg`, `/proc/kmemleak`, and `syslog(2)` are available for GUI
  reducer evidence without patching KDE, Qt, KWin, Xwayland, Mesa, or
  Chromium.

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
- Linux KVM+virgl Chromium-shape control
  `build-x86_64/linux-virgl-gbm-chromium-shape/20260630T015207Z/` passed on
  this host. It proves Linux virgl also exposes no Chromium-style pbuffer
  configs (`pbuffer=0`) while window and surfaceless ES2/ES3 contexts pass, so
  missing pbuffer configs are not by themselves an xv6-specific root cause.
- Updated Linux KVM+virgl Chromium-shape control
  `build-x86_64/linux-virgl-gbm-chromium-shape/20260630T063015Z-robust-access-context-attrs/`
  now tests Chrome-like robust-access, no-error, and priority context
  attributes. It matches the xv6 enhanced GBM preprobe:
  robust-access/no-reset returns `EGL_BAD_ATTRIBUTE`,
  robust-access/lose-context and the combined Chrome-like profile return
  `EGL_BAD_MATCH`, while plain ES2/ES3, no-error, and priority-high contexts
  pass. Treat these robust-context failures as virgl/Mesa/host behavior unless
  Chromium evidence proves Linux Chrome takes a different attribute path.
- Linux KWin/Wayland Chromium playback control
  `build-x86_64/linux-chromium-vm-control/20260630T050817Z-ubuntu-kwin-wayland-http-perf-no-gl-no-xwayland-summary-fixed/`
  passed only after omitting forced GL/ANGLE flags and disabling KWin Xwayland
  for the control run. Chromium produced stable GPU/renderer roles and sustained
  virgl submits. Forced `egl-angle/opengles` failed to admit stable GPU/renderer
  roles on Linux too, so it is no longer a clean kernel-only xv6 signal.

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

2026-06-30 Plasma interaction reducers now give a sharper responsiveness
baseline. The helper-based baseline at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T184842Z-desktop-interaction-helper-final-pass/`
showed tray open taking `3754ms` to first visible change and direct app-launch
stress taking `6929ms`, while hover sampling was too short to be useful.
After staging optional tray applets as marker-disabled rootfs data and showing
only the xv6 network SNI plus volume, the active-sampling run at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T190256Z-desktop-interaction-minimal-tray-active-sample/`
reduced tray open to `1884ms`, kept audio and network SNI registration alive,
and removed the recursive Qt Quick layout warnings from that path. A follow-up
timing-aware probe run at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T191127Z-desktop-interaction-launch-timing-zero-delay/`
proved that `kde-app-launch-probe` had been adding artificial delay: with
`KDE_APP_LAUNCH_PROBE_INTERLAUNCH_DELAY_MS=0`, direct launch fell from
`7241ms` to `4339ms`. Per-app logging showed fork/exec launch calls were
`1-2ms`; the remaining launch bottleneck was Konsole shell readiness
(`konsole_wait_ms=3408`), not process creation. Hover remains noisy but slow:
valid changed samples are still roughly `1.0-1.1s`, and some icons do not
change within a `1000ms` window.

Current direction:

- Treat FPS deltas as reducer targets, not broad desktop rewrites.
- Keep screenshots/logs for every GUI proof.
- Prefer kernel/libc/rootfs/harness fixes when evidence points below imported
  KDE/Qt/KWin/Xwayland/Mesa packages.
- For Plasma responsiveness, spot the bottleneck in metrics before changing
  behavior. The 2026-07-01 boundary-safe `vm_copy_present_skip_validate`
  recheck is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T014100Z-desktop-interaction-present-skip-boundary-safe-pass/`
  and the kprofile-enabled run is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T014341Z-desktop-interaction-present-skip-boundary-safe-kprofile-pass/`.
  The latter passed but measured direct launch as still dominated by path
  lookup: `sys_openat_ms=1556`, `sys_openat_lookup_ms=1224`,
  `vfs_lookup_driver_ms=530`, `vfs_dentry_inode_rlock_ms=368`,
  `vfs_inode_cache_ms=274`, while `vm_vma_validate_ms=385` after the VM-copy
  fast path. Treat `openat`/VFS lookup as the next Plasma launch bottleneck,
  not additional VM-copy validation work.
- The VM-copy fast path remains runtime disableable with
  `vm_copy_present_skip_validate=0`. The post-audit COW sanity log is
  `build-x86_64/vm-copy-present-skip-cowtest/run.log`; it contains
  `ALL COW TESTS PASSED`, although the wrapper RC marker was malformed, so add
  a cross-page COW/dirty reducer before broadening this optimization further.
- Hot-path kstats profiling is now opt-in through `kstatsctl(2)` and
  `kprofile`, so normal Plasma responsiveness runs do not pay the profiling
  counters/timers used to find the VFS bottleneck. The post-gating
  interaction proof with lower-left panel coverage is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T021319Z-desktop-interaction-panel-start-menu-pass/`.
  It passed with active framebuffer sampling and recorded:
  desktop visible `19896ms`, desktop hover changed samples
  `1221-2017ms` with one KWrite no-change outlier, lower-left panel hover
  `1199-1873ms`, start-menu open `2248ms`, start-menu close `1224ms`, tray
  open `2153ms`, tray close `1005ms`, and direct launch `5537ms`
  (`konsole_wait_ms=4171`, `KDE_APP_LAUNCH_PROBE_KPROFILE=0`).
- The VFS backend read-revive optimization remains runtime gated with
  `vfs_backend_read_revive=1`. The proof run archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T043347Z-desktop-interaction-vfs-read-revive-kprofile-pass/`
  passed with the flag enabled and reduced the direct-launch profiled path:
  `vfs_inode_cache_miss_revive_without_wlock` went from `4103` to `0`,
  `vfs_inode_cache_read_revive_success=3983`, dentry-inode time fell from
  `1068ms` to `470ms`, and `sys_openat_ms` fell from `1404ms` to `944ms`.
  The user-visible launch path improved but remains dominated by Konsole shell
  readiness (`4382ms`), so this is useful but not a complete Plasma
  responsiveness fix. Keep the flag default-off until parallel lookup/unlink/
  rename/LRU stress with KASAN/kmemleak or equivalent lifetime proof validates
  the changed inode-LRU invariant.
- `kstats` now has a size-aware `kstats2(2)` ABI for appended counters while
  legacy `kstats(2)` copies only the stable v1 prefix. The no-desktop ABI proof
  is archived at
  `build-x86_64/kstats-abi-proof/20260701T044849Z-clean/`; it records
  `kprofile` reading the appended read-revive counters and `clockbench getpid`
  passing through the legacy `SYS_kstats` path.
- Next evidence should split Konsole readiness into pty/session, Wayland
  surface, and shell-marker phases, and split hover delay into input injection,
  Plasma paint, and framebuffer readback timing.
- 2026-07-01 Konsole readiness tracing proved an unsafe poll full-wait
  admission path for AF_UNIX Wayland sockets. The failed trace at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T084230Z-desktop-interaction-konsole-ready-trace-parser-fail/`
  showed Konsole and its Wayland event thread entering infinite kqueue waits on
  `/dev/shm/xdg-runtime-root/wayland-0` and timing out after `45127ms`.
  Kernel AF_UNIX sockets are therefore kept on the 10ms poll rescan safety net
  while `poll_notify_full_wait=1` is enabled, until all AF_UNIX readiness
  transitions are proven notify-backed. The low-noise proof at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T085400Z-desktop-interaction-af-unix-rescan-low-noise-pass/`
  passed with `direct-launch=4847ms`, `konsole_wait_ms=3588`, tray
  open/close `1598/1111ms`, start-menu open/close `2108/1031ms`, network SNI
  registration, PipeWire sink/monitor readiness, and virgl DRM nodes registered.
- The plain `desktop-interaction` final-capture path now stages the xv6-owned
  `/kdi.sh` helper before boot. The previous Konsole-only Wayland trace run at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T102416Z-desktop-interaction-konsole-wayland-trace-agent-pass-final-capture-fail/`
  had already reached `KDE_SMOKE_AGENT_DONE status=PASS`, but then failed the
  outer final screenshot because `/kdi.sh` was missing. The proof run at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T103514Z-desktop-interaction-final-capture-helper-pass/`
  now passes with `status_code=0`, preserves `kde-plasma-final.ppm/png`,
  records `101` Konsole-gated `kde-wayland-unix` lines, and keeps the same
  health invariants: `/dev/dri/card0`, `/dev/dri/renderD128`, PipeWire Pulse
  sink/monitor readiness, network SNI registration, `kde_kwin_screenshot_probe
  result=PASS`, and no panic/exception. In that proof, Konsole exec remained
  cheap (`fork_elapsed_ms=8`, `exec_elapsed_ms=15`) while shell readiness still
  dominated (`konsole_wait_ms=5048`), so the next Plasma bottleneck remains
  readiness/wakeup after exec rather than process creation.
- A focused AF_UNIX SCM-only readiness reducer now exists in
  `/bin/kde-unix-socket-probe --scm-zero-readiness`. Linux host reference and
  xv6 guest proof both show the same ABI shape for stream socketpairs with
  `SO_PASSCRED` and zero-length `write(2)`/`sendmsg(2)`: send returns `0`,
  `poll(POLLIN)` returns `0`, and nonblocking `read(2)` returns `-1/EAGAIN`.
  The xv6 artifact is
  `build-x86_64/unix-socket-scm-zero-readiness/20260701T104416Z-xv6/`.
  This rules out the suspected stale zero-length SCM mark as the source of
  Konsole's repeated `poll-ready/read/read-eagain` cycles; continue looking at
  ordinary byte readiness, eventfd/Qt wake propagation, PTY/session readiness,
  or userspace futex waits.
- 2026-07-01 phase instrumentation is now staged in xv6-owned probes/harness:
  `kde-app-launch-probe` emits Konsole launch/marker/wrapper timing, and the
  desktop interaction reducer preserves `fbstat sample-current` open/info/
  alloc/readback/stats/total timing plus input-injection overhead fields. Static
  Tcl completeness, `git diff --check`, and C syntax checks passed, and
  `rootfs-refresh` rebuilt `build-x86_64/fs.img`.
- The durable Konsole phase proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T111406Z-desktop-interaction-konsole-phase-selfexec-pass/`.
  It passed with `status_code=0`, virgl DRM nodes, network SNI registration,
  and PipeWire Pulse sink/monitor evidence intact. The direct Konsole launch
  call stayed cheap (`launch_call_ms=13`), but PTY readiness appeared only after
  `5360-5361ms`, the shell wrapper started at `6453ms`, and the bash handoff
  was at `6645ms` from launch. The marker/write/open path itself is small
  (`wrapper_open_delta_ms=2`, `wrapper_before_exec_delta_ms=2`,
  `wrapper_marker_found_since_wrapper_ms=98`), so the current Plasma launch
  bottleneck is before the shell payload and should stay focused on
  Konsole/Qt/Wayland/PTY/session wakeup and admission timing.
- 2026-07-01 AF_UNIX full-wait opt-in is now a diagnostic only, not a promoted
  policy. The first proof with `poll_notify_full_wait=1
  af_unix_poll_notify_full_wait=1` reached the interaction/direct-launch lane
  but hit an intermittent allocator assertion in the RCU callback thread:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T113143Z-af-unix-full-wait-slab-double-free-fail/`.
  It preserved early GPU (`/dev/dri/card0`, `/dev/dri/renderD128`), audio
  registration, and NetworkManager shim evidence before
  `__slab_obj_put(): double free detected`. A rerun with an added slab
  cache/object diagnostic passed at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T113543Z-af-unix-full-wait-diagnostic-pass/`,
  but did not solve responsiveness: direct Konsole launch was `10721ms`
  (`konsole_wait_ms=8295`), PTY readiness was `6523-6524ms`, wrapper start was
  `8069ms`, start-menu open/close was `2043/1026ms`, tray open/close was
  `1704/1018ms`, and one desktop hover still reported no visual change. Keep
  AF_UNIX sockets on the rescan safety net by default; use
  `af_unix_poll_notify_full_wait=1` only for focused wakeup/regression proof,
  and treat the RCU/slab double-free as a separate stability lead to reproduce
  with the new diagnostic.
- 2026-07-01 default-off PTY and poll-fd attribution proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T114931Z-desktop-interaction-pty-poll-trace-pass/`.
  The desktop interaction reducer passed with all hover, lower-left panel,
  start-menu, tray, and direct-launch phases changed; `/dev/dri/card0`,
  `/dev/dri/renderD128`, NetworkManager shim, and PipeWire/Pulse sink+monitor
  evidence remained intact. The Konsole direct-launch split was
  `launch_call_ms=55`, `pty_ptmx_since_launch_ms=5364`,
  `pty_pts_since_launch_ms=5365`, `wrapper_start_since_launch_ms=7959`, and
  `bash_start_since_launch_ms=8358`. Kernel PTY setup is not the main delay
  once reached: `pty_ready_trace=1` recorded `ptmx-open` at guest `72853ms`,
  `pts-peer-fd` at `72874ms`, then the first wrapper `pts-write` at `75673ms`
  and `/dev/ptmx` poll readiness at `75682ms`. The new `konsole_ready_trace`
  fd classifier shows the pre-PTY wait is primarily Qt/Wayland/DBus admission
  and wake propagation through Wayland `socket:[146]`, QDBus `socket:[148]`,
  eventfd, and pipe waits. Do not patch PTY behavior from this run; use the
  next reducer to isolate the Qt/DBus/Wayland admission edge before changing
  poll or AF_UNIX policy.
- 2026-07-01 filtered Konsole IPC trace proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T121342Z-desktop-interaction-konsole-ipc-filtered-trace-pass/`.
  It passed with desktop visible at `18057ms`, start-menu open/close
  `2203/1028ms`, tray open/close `1854/1213ms`, virgl DRM nodes,
  NetworkManager SNI, and PipeWire/Pulse sink+monitor evidence intact. The
  trace confirms Konsole reaches the Wayland socket quickly and then spends the
  pre-PTY window in Wayland `socket:[146]`, QDBus `socket:[148]`, eventfd, and
  futex wake paths. Because trace volume inflated PTY readiness to `17140ms`
  and shell readiness to `24592ms`, use it for attribution only, not as a
  performance baseline.
- 2026-07-01 opt-in `kstats` poll-wait attribution now records overlapping
  fd-set exposure by fd class and ready/timeout outcome. The proof archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T123447Z-desktop-interaction-poll-wait-summary-kprofile-pass/`
  passed without verbose IPC trace distortion and measured direct launch at
  `6244ms` (`konsole_wait_ms=4935`, PTY at `3158-3161ms`). The profiled window
  reported `sys_poll_wait_timeout_ms=258656`,
  `sys_poll_wait_rescan_ms=259090`, `sys_poll_wait_notify_ms=242683`,
  `sys_poll_wait_eventfd_ms=204511`, and
  `sys_poll_wait_unix_ms=127594`, versus `sys_poll_wait_ready_ms=434`.
  These counters are opt-in through `kstatsctl(2)`/`kprofile`; use them to
  guide the next Qt/Wayland/DBus/eventfd wake reducer before changing global
  poll or AF_UNIX policy.
- 2026-07-01 opt-in Konsole pre-PTY `kstats` v5 counters now split the
  direct-launch wait before the first PTY boundary. The proof archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T125524Z-desktop-interaction-konsole-prepty-kstats-pass/`
  passed with direct launch `5484ms`, `konsole_wait_ms=3771`, first PTY at
  `2834-2836ms`, wrapper at `3821ms`, and bash at `4004ms`. The pre-PTY
  buckets reported `konsole_prepty_poll_total_ms=2930`,
  `konsole_prepty_poll_wayland_ms=1726`,
  `konsole_prepty_poll_pipe_ms=1639`,
  `konsole_prepty_poll_eventfd_ms=1198`,
  `konsole_prepty_poll_unix_other_ms=1203`,
  `konsole_prepty_poll_timeout_ms=2821`, and ready waits only `108ms`.
  `konsole_prepty_futex_wait_ms=49`, all woken, rules out futex waits as the
  main pre-PTY bottleneck in this run. Keep global poll/AF_UNIX behavior
  unchanged; the next evidence target is endpoint-level classification and a
  focused Wayland/Qt pipe/eventfd wake reducer.
- 2026-07-01 opt-in Konsole pre-PTY endpoint-combo `kstats` v6 proof is
  archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T134834Z-desktop-interaction-endpoint-combo-kprofile-pass/`.
  It passed with desktop visible at `19109ms`, hover changes at
  `1020-2469ms`, panel hover at `1115-1540ms`, start-menu open/close
  `2161/1140ms`, tray open/close `1687/1162ms`, and direct launch
  `5780ms` (`konsole_wait_ms=3975`). Konsole launch remained cheap
  (`launch_call_ms=54`), while PTY fds appeared at `3097-3099ms`, wrapper
  start at `4023ms`, marker at `4035ms`, and bash at `4169ms`. The v6
  counters cleanly split the pre-PTY poll exposure into `wayland_pipe_ms=1581`
  and `qdbus_eventfd_ms=1073`, with total `2771ms`, timeout `2661ms`, ready
  only `110ms`, and no remaining unclassified AF_UNIX combo exposure. This
  keeps the next fix target on the Qt/Wayland/DBus AF_UNIX plus pipe/eventfd
  notification edge, not exec, PTY setup, TTY ioctl, or futex behavior. Keep
  AF_UNIX on the rescan safety net until a focused reducer proves which
  readiness transitions are notify-complete.
- 2026-07-01 focused AF_UNIX/pipe/eventfd notify-mix reducer evidence is
  archived at
  `build-x86_64/unix-notify-mix-proof/20260701T135557Z-nogpu/`. The new
  opt-in `/bin/kde-unix-socket-probe --notify-mix` mode passed on Linux host
  and in an xv6 nographic guest booted with
  `poll_notify_full_wait=1 af_unix_poll_notify_full_wait=1 desktop=0`. Socket,
  pipe, and eventfd wake variants each returned at about `99-101ms`, matching
  the delayed writer. This means the simple mixed endpoint shape is
  notify-complete under the risky full-wait knobs; keep chasing the richer
  Qt/Wayland/DBus protocol or activation edge before promoting AF_UNIX full
  waits globally.
- 2026-07-01 Qt-style dispatch rearm reducer evidence is archived at
  `build-x86_64/qt-dispatch-mix-proof/20260701T140708Z-linux-host-final/`
  for the Linux host reference and
  `build-x86_64/qt-dispatch-mix-proof/20260701T141654Z-xv6-nographic/`
  for the xv6 nographic guest proof. The new opt-in
  `/bin/kde-unix-socket-probe --qt-dispatch-mix` mode covers
  `wayland-pipe-rearm`, `qdbus-eventfd-rearm`, and a combined
  Wayland/pipe/DBus/eventfd loop. It passed on Linux, then passed in xv6 with
  `poll_notify_full_wait=1 af_unix_poll_notify_full_wait=1 desktop=0`; the
  same guest run also passed `--notify-mix` and the default AF_UNIX probe.
  This rules out the direct Qt dispatcher self-wake/re-enter-poll shape as the
  remaining Konsole pre-PTY blocker. Keep AF_UNIX full waits diagnostic-only;
  next evidence should target DBus/Wayland activation or protocol ordering, or
  add per-wait wake-source accounting in `__vfs_poll_impl()` before changing
  global poll behavior.
- 2026-07-01 AF_UNIX full-wait edge reducer coverage now includes delayed
  peer byte write, peer close/EOF, `shutdown(SHUT_WR)` EOF, and an
  SCM_RIGHTS-bearing byte. Linux host reference is archived at
  `build-x86_64/af-unix-poll-edges-proof/20260701T150331Z-linux-host/`; the
  xv6 nographic full-wait proof is archived at
  `build-x86_64/qt-dispatch-mix-proof/20260701T150451Z-xv6-nographic/`. The
  xv6 run booted with
  `poll_notify_full_wait=1 af_unix_poll_notify_full_wait=1 desktop=0` and
  passed `--af-unix-poll-edges`, `--qt-dispatch-mix`, `--notify-mix`, and the
  default AF_UNIX probe. The edge waits returned in about `100-106ms`,
  matching the delayed child action, and SCM_RIGHTS `recvmsg()` returned a
  valid received fd. This closes the previously missing reducer coverage
  without justifying a kernel behavior change: keep AF_UNIX off the global
  notify-backed default and continue with richer DBus/Wayland activation or
  producer-origin wake attribution.
- 2026-07-01 timestamped Konsole kqueue-event trace proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T151001Z-desktop-interaction-kqueue-event-ms-pass/`.
  The desktop interaction reducer passed with `status_code=0`, virgl DRM nodes
  `/dev/dri/card0` and `/dev/dri/renderD128`, NetworkManager SNI, PipeWire/
  Pulse sink+monitor readiness, QEMU virgl activity (`ctx_submit=382`,
  `res_flush=144`, `fence_resp=382`), and no panic, fatal page fault,
  coredump, or exception. Trace volume inflated direct launch to `12180ms`,
  with Konsole shell readiness `9368ms`, PTY at `7081-7083ms`, wrapper at
  `9264ms`, and bash at `9781ms`, so use timing only for attribution. The new
  `ms=` field shows delivered kqueue events by target: eventfd `32`, Wayland
  socket `22`, QDBus socket `7`, pipe `2`, and `/dev/ptmx` `1`. After Konsole
  exec-done, the first Wayland socket kqueue event appears at `+1495ms`,
  QDBus/eventfd activity starts around `+2654ms`, PTY ioctls appear around
  `+6971ms`, and `/dev/ptmx` readiness arrives at `+9365ms`. This again argues
  against a raw missed-kqueue-wake fix; next work should add semantic
  DBus/Wayland capture around direct Konsole launch to identify which protocol
  message or activation step delays PTY creation.
- 2026-07-01 opt-in Konsole pre-PTY wake/readiness `kstats` v7 proof is
  archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T144316Z-desktop-interaction-kqueue-ready-v7-pass/`.
  A no-desktop ABI smoke first proved the appended fields are visible to guest
  `kprofile` at
  `build-x86_64/kprofile-v7-proof/20260701T144031Z-nographic-send-on-first-prompt/`.
  The desktop interaction reducer passed with GPU virgl, NetworkManager SNI,
  and PipeWire/Pulse sink+monitor evidence intact. It measured direct launch
  `5078ms` (`konsole_wait_ms=2364`), first PTY fds at `1508-1510ms`, wrapper
  start at `2333ms`, and bash at `2426ms`. Pre-PTY poll exposure was
  `1472ms`: Wayland `896ms`, pipe `836ms`, QDBus `575ms`, eventfd `572ms`.
  Timed rescans still consumed `1364ms`, but all `rescan_ready_*` counters
  were zero, while all observed ready buckets followed kqueue delivery
  (`kqueue_wake_ms=108`, `event_ready_ms=108`). This argues against a simple
  missed-notification timeout-rescan bug for this sample. Caveat: the v7
  counters classify post-wait readiness and can include writable readiness;
  they do not preserve exact `kqueue_wait()` event identities. The next fix
  target should therefore stay above raw poll notification, in DBus/Wayland
  activation/protocol ordering, or first add precise kqueue event identity
  tracing before changing global poll/AF_UNIX behavior.
- 2026-07-01 precise delivered-kevent trace proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T145100Z-desktop-interaction-kqueue-event-trace-pass/`.
  The new `konsole_ready_trace=1`-only `kde-ready-kqueue-event` line maps
  returned `kevent.udata` to the original `pollfd` and fd target. It is a
  trace hook, not a behavior change. The reducer passed with `/dev/dri/card0`,
  `/dev/dri/renderD128`, virgl renderer, NetworkManager SNI, and PipeWire/
  Pulse sink+monitor evidence intact. Trace volume inflated direct launch to
  `10143ms` (`konsole_wait_ms=7734`), so use timing only as distorted
  attribution evidence. Delivered events were eventfd `32`, AF_UNIX socket
  `30`, pipe `2`, and PTMX `1`, all `EVFILT_READ`; examples include the
  Wayland socket `socket:[146]`, `anon_inode:[eventfd]`, and `/dev/ptmx`.
  This confirms the hot Wayland/eventfd paths do return through kqueue and
  points the next fix away from a simple raw missed-wakeup theory, toward
  DBus/Wayland/Qt activation/protocol sequencing or, if that remains
  ambiguous, producer-origin tagging inside kqueue.
- The first instrumented desktop-interaction run reproduced the black desktop
  path before hover timing could be measured:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T050534Z-desktop-interaction-phase-instrumentation-visible-timeout/`.
  All 120 active `fbstat sample-current` reads returned
  `nonblack=0`, while per-sample timing showed readback itself was only about
  `56-96ms` and helper elapsed was usually about `420-770ms`; this is not a
  Tcl parser failure.
- The focused desktop-color wakeup control passed as a reducer at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T050800Z-desktop-color-wakeup-scanout-black-pass/`,
  but it warned `input-readback-no-visible-change`. With
  `virtio_gpu_scanout_read_diag=1` and `fb_present_sample_diag=1`, KWin/Plasma
  were alive and virgl page-flipped resources `4/5/14`, yet each
  resource-scanout sample and both pre/post-click KMS readbacks reported
  `sample_nonblack=0`. The next proof target is therefore the virtio-gpu/KMS
  page-flip/readback lineage for black-start runs before treating hover or
  Chromium FPS data from that boot as UI/compositor latency.
- The follow-up pageflip/copy and KMS-copy A/B runs narrowed the black-start
  lane without patching KDE/Qt/KWin/Mesa/Chromium. The pageflip-copy run at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T051713Z-desktop-interaction-pageflip-copy-validation-fallback-pass/`
  completed the interaction reducer only after
  `pageflip-copy validation failed ... src_nonblack=5 flip_nonblack=0`,
  proving that the flip resource can remain black while the source is
  nonblack. The forced KMS resource-copy run at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T052834Z-desktop-color-force-kms-resource-copy-kms-readback-black-pass/`
  proved the copy lane with `FB: virgl resource-copy present ...`, but KMS
  readback of the current framebuffer still returned `sample_nonblack=0`.
  With KMS readback skipped, the active-sampling proof at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T053346Z-desktop-interaction-force-copy-skip-kms-sample-current-pass/`
  became visible at `26538ms`, recorded nonblack interaction samples, and
  completed hover/panel/start-menu/tray/direct-launch phases. This makes the
  next kernel question narrower: distinguish stale KMS framebuffer readback and
  black flip-resource copies from the actually displayed persistent scanout,
  then choose a fallback that does not add per-frame copy cost to the normal
  FPS path.
- The readback-only black guard is now implemented in the kernel and archived
  at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T055023Z-desktop-interaction-readback-black-guard-pass/`.
  In the forced KMS resource-copy proof, `FB_GPU_SCANOUT_READ` first observed
  nonblack persistent scanout pixels, skipped an all-black diagnostic present
  overlay (`present-overlay-skip ... dst_nonblack=5 present_nonblack=0`), then
  preserved those pixels when the current KMS framebuffer readback was black
  (`preserve_virtio=1`). The interaction reducer passed with
  `first_visible_ms=1821`, network SNI registration, PipeWire/Pulse sink and
  monitor readiness, and `/dev/dri/card0` plus `/dev/dri/renderD128`
  registered. This fixes the screenshot/`fbstat sample-current` oracle for
  this stale-readback lane; it does not claim that the compositor's normal
  per-frame present path or Chromium video FPS is fixed.
- Linux VM interaction comparison is not yet a valid visual baseline on this
  host. `build-x86_64/linux-kde-interaction-proof/20260701T051942Z/` showed
  QEMU monitor `screendump` failing with `Error: no surface`, and
  `build-x86_64/linux-kde-interaction-proof/20260701T052423Z/` showed guest
  `grim` failing with `compositor doesn't support the screen capture protocol`.
  Both still provide a useful direct-launch reference: Linux Konsole readiness
  was `349-637ms`, while recent xv6 runs remain around `4-5s` from Konsole
  launch to the shell-wrapper marker. The Linux proof harness now treats a
  missing visual baseline as a failure instead of reporting a green
  `LINUX-KDE-INTERACTION-DONE` from direct launch alone.
- The Linux KVM+virgl interaction harness now mirrors xv6's lower-left panel
  hover and start-menu phases and can be invoked directly. The phase-only run
  archived at
  `build-x86_64/linux-kde-interaction-proof/20260701T121710Z-panel-start-menu-phase-only/`
  completed with `status_code=0` and
  `LINUX-KDE-INTERACTION-PHASE-ONLY visual-baseline-missing`; it proves the
  current Linux Konsole direct-launch reference is `338ms`, but all visual
  hover, panel, start-menu, and tray timings remain invalid because
  `screendump` produced no surface. Use this as a shell-readiness baseline
  only until a supported Linux screenshot backend is added.
- The Linux interaction harness now emits xv6-shaped Konsole direct-launch
  phase fields. The phase-only proof at
  `build-x86_64/linux-kde-interaction-proof/20260701T132550Z-konsole-wrapper-fd-token/`
  completed with `status_code=0`,
  `LINUX-KDE-INTERACTION-PHASE-ONLY visual-baseline-missing`, `/dev/dri/card0`,
  `/dev/dri/renderD128`, and virgl trace activity. It measured Linux Konsole
  launch call `0ms`, wrapper `/dev/pts/0` at `250ms`, shell start `270ms`,
  guest wait `390ms`, and host elapsed `588ms`; the serial marker also shows
  `parent_ptmx=ptmx`. This keeps the Linux reference sharply below xv6's
  latest `konsole_wait_ms=3771` and PTY-at-`2834-2836ms` proof, so the next
  xv6 work should stay on pre-PTY Qt/Wayland/DBus/eventfd/pipe endpoint
  classification before changing global poll or PTY behavior.
- The current Konsole-readiness profiler proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T061204Z-desktop-interaction-kprofile-poll-futex-proof/`.
  Kernel `kstats` ABI version 3 appends opt-in counters for `poll`, `ppoll`,
  blocking poll wait time, tty/pty ioctl buckets, and futex wait/wake time;
  `kprofile` prints them from `kstats2(2)`. The run passed the desktop
  interaction reducer with GPU nodes, PipeWire/Pulse sink+monitor, NetworkManager
  shim, and network SNI evidence intact. Konsole launch itself took only `10ms`,
  but the wrapper marker appeared after `3169ms` (`wrapper_start_since_launch_ms=3154`).
  The profiled window recorded `sys_poll_blocking_ms=10818` across `126`
  blocking waits and `sys_futex_wait_ms=3521`, while tty/pty ioctl buckets were
  effectively `0ms`. Treat the next Plasma responsiveness root-cause branch as
  Konsole/Qt/Wayland poll-futex scheduling or wakeup behavior around sockets,
  eventfds, and Wayland/DBus peers, not PTY setup or process creation. Add
  peer identity/queue detail for the sampled `socket:[146]` and `socket:[148]`
  waits before behavior-changing scheduler, poll, or futex patches.
- The peer-attribution and Konsole-process kqueue wait proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T064325Z-desktop-interaction-konsole-kqueue-sleep-pass/`.
  It passed the desktop interaction reducer with `/dev/dri/card0`,
  `/dev/dri/renderD128`, NetworkManager shim, and PipeWire/Pulse evidence
  intact. The direct Konsole path was slower under tracing:
  `konsole_launch_call_ms=8`, `konsole_wrapper_start_since_launch_ms=14280`,
  and `konsole_wait_ms=14998`. `kprofile` recorded
  `sys_poll_blocking_ms=55736`, `sys_ppoll_ms=16016`,
  `sys_futex_wait_ms=14225`, and `sys_openat_lookup_ms=11354`. The new
  `kde_kqueue_spin_trace_konsole_only=1` gate confines kqueue wait tracing to
  the Konsole process group; the `timeout=10` wake samples had median `10ms`,
  average `17.1ms`, p90 `35ms`, p99 `71ms`, max `95ms`, and `153/161`
  samples with `nready=0`. This points at timer/scheduler wake latency and
  empty timeout churn as the next evidence branch, while the extra trace
  overhead means this run should not be treated as a performance baseline.
- The first post-trace uninstrumented rerun crashed during KWin readiness and
  is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T064717Z-desktop-interaction-kwin-qtcore-gp-fail/`.
  It trapped in host `libQt5Core.so.5` at
  `QObjectPrivate::connectImpl(...)` with a pointer register containing the
  string fragment `/x86_64-`; treat this as a separate KWin/Qt memory
  corruption or reuse stability lead, not as responsiveness evidence.
- The default-off `poll_notify_full_wait=1` A/B proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T065304Z-desktop-interaction-poll-notify-full-wait-pass/`.
  With `vfs_backend_read_revive=1`, `kde_poll_summary=1`, and notify-backed
  poll sets allowed to sleep until their real timeout, the desktop interaction
  reducer passed with GPU nodes, NetworkManager SNI, and PipeWire/Pulse
  evidence intact. Direct Konsole launch fell to `6355ms`
  (`konsole_wait_ms=5204`) from the trace-heavy `26137ms`/`14998ms` path, and
  kprofile recorded `sys_poll_blocking_ms=13989`,
  `sys_futex_wait_ms=6201`, and `sys_openat_ms=1042`. The run is evidence
  that the unconditional 10ms `poll` rescan creates artificial wake churn for
  KDE's Wayland/DBus/eventfd paths; keep the knob default-off until more
  regression proof and a Chromium-video pass validate the broader policy.
- A follow-up audit found that using `.poll != NULL` as the notify-backed
  predicate was too broad: PTY/TTY fds can answer readiness queries without
  notifying every readiness transition. The kernel now uses an explicit
  `VFS_FILE_OPS_F_POLL_NOTIFY_BACKED` capability for audited fd families
  (AF_UNIX, eventfd, pipe, timerfd, kqueue, lwIP sockets, netlink, pidfd, and
  inotify) and leaves PTY/TTY, procfs, devices, and unknown `.poll` users on
  the periodic rescan path.
- The safer capability-gated proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T070514Z-desktop-interaction-poll-notify-flagged-pass/`.
  It passed the desktop interaction reducer with `poll_notify_full_wait=1`.
  Metrics: desktop visible `18413ms`; desktop hover Chromium/Dolphin/KWrite/
  Konsole `1158/2604/919/1075ms`; panel hover `984-1389ms`; start menu
  open/close `2044/948ms`; tray open/close `1652/998ms`; direct Konsole
  launch `6180ms` with `konsole_wait_ms=4980`. Kprofile recorded
  `sys_poll_ms=18510`, `sys_poll_blocking_ms=13636`,
  `sys_ppoll_ms=4979`, `sys_futex_wait_ms=6225`, and
  `sys_openat_ms=1176`. GPU nodes, NetworkManager SNI, and PipeWire/Pulse
  sink+monitor evidence remained intact, and the Konsole/Wayland/DBus poll
  summaries still show real event wakes on AF_UNIX/eventfd paths.
- The matched full-wait-off A/B proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T075456Z-desktop-interaction-poll-full-wait-off-ab/`.
  It reran the desktop interaction reducer with the same safe read-revive and
  KDE poll-summary knobs, but without `poll_notify_full_wait=1`. The reducer
  passed and preserved GPU nodes, NetworkManager SNI, and PipeWire/Pulse
  sink+monitor evidence with no panic, exception, or user fault. Metrics were
  worse on the direct Konsole path: launch `7796ms` with
  `konsole_wait_ms=6394`, `sys_poll_blocking_ms=17041`,
  `sys_ppoll_ms=6303`, and `sys_futex_wait_ms=7061`, while `sys_openat_ms`
  stayed comparable at `1150`. This strengthens the attribution that the
  capability-gated full-wait policy reduces artificial Konsole/Wayland/DBus
  wait churn, but it should remain default-off until a Chromium-video
  regression pass and broader notify-backed fd coverage prove it safe.
- The gated kqueue timer-dispatch instrumentation is archived first as a
  passive-sample miss at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T082054Z-desktop-interaction-kqueue-timer-trace-visible-timeout/`
  and then as a passing active-sample run at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T082442Z-desktop-interaction-kqueue-timer-trace-active-sample-pass/`.
  The passive run showed a visible host desktop and live virgl/KDE services,
  but lacked passive `FB: virgl resource-scanout sample` lines, so the active
  sampler is the authoritative artifact for this trace. The pass kept
  `poll_notify_full_wait=1` and `kde_kqueue_spin_trace=1` scoped to KDE/
  Konsole evidence. It measured direct Konsole launch `7362ms` with
  `konsole_wait_ms=5925`; the new timer fields observed `15` timed kqueue
  wakes, `4` timer-fired wakes, `4` empty timeout wakes, `max_dispatch_ms=5`,
  and `max_overrun_ms=5`. Thus the remaining Konsole readiness delay is not
  explained by raw scheduler timer dispatch latency. Treat the next
  responsiveness branch as Konsole/Qt/Wayland/DBus event/admission work, while
  leaving scheduler wake ordering and `poll_notify_full_wait` defaults alone.
- The first Chromium-video stress attempt with `poll_notify_full_wait=1` did
  not reach Chromium and is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T065932Z-chromium-video-poll-full-wait-wireplumber-gp-fail/`.
  It failed as `kde-session-ready-crash` after `wireplumber` hit a user `#GP`
  at `rip=0x7fffff61bfb1`; GPU nodes and PipeWire/Pulse readiness appeared
  before the failure, but Chromium post-evidence correctly reported
  `status=FAIL reason=not-launched`. Treat this as another startup
  memory-corruption/stability lead, not Chromium FPS evidence.

### 3. Chromium As Regression / Stress Probe

Status: forced-GL launch policy contained; normal-path renderer/media delivery
still failing.

Recent Chromium evidence in
`build-x86_64/kde-plasma-desktop-smoke/` reached page playback without a kernel
trap or Chromium INT3, but failed the video gate:
`presentedFPS=30.54`, `decodedFPS=59.52`, `dropPct=57.61`. Wayland frame
callbacks and commits were active, but the visible capture/scanout stayed
static. Chromium launched `egl-angle/opengles` GPU processes that exited with
`Requested GL implementation (gl=none,angle=none)`, then continued with a
`--use-gl=disabled` GPU process. EGL tracing saw process/render-node/ioctl
activity but no EGL entrypoints or `dlopen`, and Chrome produced only one
successful execbuffer, unlike Linux Chromium controls with thousands of submits.

Linux controls show that forced `egl-angle/opengles` can fail before stable
GPU/renderer admission on Linux too, and that a working run does not need those
flags. The xv6 launchers and harness now make this policy explicitly
disableable: `KDE_SMOKE_CHROMIUM_AUTO_GL_FLAGS=0` stages
`WAYLAND_CHROMIUM_AUTO_GL_FLAGS=0`, and the launcher then omits implicit
`--use-gl=egl-angle` / `--use-angle=opengles` arguments. Forced GL/ANGLE remains
available only as an explicit reducer by setting Chromium extra flags.

2026-06-30 focused GBM reducer evidence is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T042438Z-chromium-video-gbm-fps-27-2/`.
It used multiprocess Chromium, `EGL_PLATFORM=gbm`, GBM preprobe, and
`kde_xwayland_glamor=auto`. The harness status fix held: GBM status was only
`status=DONE`, while the copied GBM log contained
`host-egl-gbm-gl-smoke: phase=result status=PASS`; the sampler status was
`status=DONE`, while the sampler log contained
`kde_app_launch_probe chromium_sample_only=1 ... status=PASS`.

This run reached the local video page and failed on performance rather than
launch: `presentedFPS=27.2`, `decodedFPS=60.6`, `dropPct=58.23`,
`speed=0.949`. Capture proof was nonblack but mostly static
(`12/12` frames present, `3` unique hashes, `capture_changed_count=2`). Chrome
opened the render node and produced exactly one Chrome virtgpu execbuffer while
the desktop stack produced sustained virgl activity (`ctx_submit=511`). The
process evidence still showed no stable renderer role, three GPU-process roles,
two early GPU-process initialization exits, and repeated
`Requested GL implementation (gl=none,angle=none)` errors before the surviving
GPU process made the lone execbuffer submit.

A follow-up attempt to enable Chromium EGL preload tracing failed before
Chromium launch with a KWin session-readiness exception and is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T042705Z-egl-trace-kwin-ready-crash/`.
Do not treat that failed trace attempt as Chromium evidence; rerun EGL/process
tracing only with a startup-safe configuration.

The current enhanced GBM preprobe is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T022620-enhanced-gbm-context-attrs/`.
It proves the guest can create the same plain/no-error/priority ES3 contexts
that Linux can, and that robust-access context rejection matches the Linux VM
control above. The next Chromium reducer should therefore focus on the exact
Chrome GPU child launch path: role admission, `--gpu-preferences`, renderer
spawn, Wayland primary-surface buffer attach/commit, and sustained virtgpu
submits under the normal no-forced-GL policy.

The next startup-safe Wayland-debug reducer is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T043735Z-chromium-video-wayland-debug-kwin-died-perf-start-missing/`.
It kept EGL preload tracing disabled and added Wayland protocol evidence plus
fast process census. This run did not reach video playback: post-evidence failed
as `status=FAIL reason=perf-start-missing`. The durable signal is:

- GBM preprobe passed with `host-egl-gbm-gl-smoke: phase=result status=PASS`; the
  GBM status file remained completion-only (`status=DONE`).
- Sampler and fast census passed.
- Chromium launcher policy requested `use_gl="egl-angle"` and
  `use_angle="opengles"`, and the GPU-process command line still carried those
  values.
- The failing GPU process reported `Requested GL implementation
  (gl=none,angle=none)` and exited during initialization.
- Wayland debug saw surface creation but no delivery operations:
  `status=FAIL reason=no-primary-surface-commit`.
- No renderer role stabilized. Chrome had render-node fd evidence but produced
  no Chrome execbuffers in the post-evidence summary.
- Capture completed but all generated PPMs were header-only because scanout
  reads failed after KWin/Wayland teardown; `virtio_gpu: scanout-read` selected
  `resource=0 bound=0 present=0`.

Treat this as a GPU-preferences/admission or browser-to-child handoff problem
until Linux comparison proves otherwise. The scanout/capture failure is
downstream of compositor death in this run, not primary FPS evidence.

The normal-launch follow-up is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T052215Z-no-forced-gl-capture-status-timeout/`.
The top-level harness label was `chromium-video-capture-status-timeout`, but
the post-evidence file shows capture completed and the real app result was
`status=FAIL reason=perf-start-missing`.

Durable signal from that run:

- `chromium_auto_gl_flags=0`, `chromium_extra_flags=""`,
  `argv_use_gl_count=0`, `argv_use_angle_count=0`, and
  `WAYLAND_CHROMIUM_AUTO_GL_FLAGS="0"` prove the disable knob reached the guest.
- GBM preprobe passed with `host-egl-gbm-gl-smoke: phase=result status=PASS`;
  its status file stayed completion-only (`status=DONE`).
- There were no Chromium GL-request errors, GPU-process init/exit/config
  errors, INT3 traps, or page faults.
- Chromium reached one browser process, one GPU process, one utility process,
  and eight zygotes, but no stable renderer role. Renderer candidates existed,
  so the next comparison must distinguish role-detection gaps from real renderer
  lifecycle failure.
- The local video page emitted only `PERF-VIDEO phase ... name=before-src`; no
  playing/tick/result lines appeared.
- Chrome opened render-node state and produced seven Chrome execbuffers. That is
  more than the forced-GL failure path but still far below the passing Linux
  control.
- Wayland debug found an `xdg_toplevel` primary surface with one commit and no
  buffer attach, damage, frame callback, or buffer delivery:
  `status=FAIL reason=no-primary-surface-buffer`.
- Capture itself reported `status=DONE result=PASS samples=4`, with nonblack
  mostly static frames. The KMS readback hash stayed static, so visual capture is
  downstream evidence only until renderer/media delivery is fixed.

Treat the current normal-path gap as renderer/Mojo/media-load/surface-buffer
delivery before changing kernel behavior. The old `gl=none,angle=none` symptom
is now a forced-GL reducer symptom, not the main Chromium-video blocker.

The 2026-07-01 normal no-forced-GL fast-census run is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260701T014659Z-chromium-video-normal-fast-census-perf-start-missing/`.
It kept `chromium_auto_gl_flags=0`, passed the RELA preprobe
(`checked=35 skipped=5 missing=2 failed=0`), passed the GBM preprobe
(`host-egl-gbm-gl-smoke: phase=result status=PASS`), and admitted a Chromium
GPU process in fast census. It still failed as `chromium-video-perf-start-missing`:
the local page reached only `canplay-query-mp4-done`, no renderer role was
stable, the primary Wayland `xdg_toplevel` had one commit but no attach, damage,
frame callback, or buffer, and Chrome produced zero execbuffers. This is proof
that the current normal Chromium gap is before sustained GPU submission or
scanout throughput. Continue with renderer/Mojo/media-start/surface-buffer
handoff evidence before any DRM/GPU behavior change.

The latest full normal-launch run with RELA and enhanced GBM preprobes is
archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T063826Z-normal-gbm-perf-start-missing-sampler-timeout/`.
The top-level harness label was `chromium-video-sampler-completion-timeout`,
but post-evidence saw the sampler completion marker and identified the app
failure as `status=FAIL reason=perf-start-missing`.

Durable signal from that run:

- RELA preprobe passed (`CHROME_RELA_PROBE_CHAIN_RESULT checked=35 skipped=5
  missing=2 failed=0`) and GBM preprobe passed with the Linux-matching robust
  context result matrix.
- Normal launch policy stayed clean: `chromium_auto_gl_flags=0`,
  `argv_use_gl_count=0`, `argv_use_angle_count=0`, `EGL_PLATFORM=gbm`, and
  `WAYLAND_DEBUG=client`.
- Chromium had no loader assertion, INT3 trap, page fault, GL-request error, or
  GPU-process init/config/exit error.
- Process evidence reached browser, one GPU process, one utility process, eight
  zygotes, and zygote child/thread activity, but no `--type=renderer` argv or
  stable renderer role. The harness renderer-candidate summary was tightened
  after this run so zygote children no longer count as renderer candidates
  unless their argv or role explicitly says renderer.
- Media evidence stopped at one console event,
  `PERF-VIDEO ... name=before-src`; no playing/tick/result events appeared.
- Wayland evidence saw surface creation and an `xdg_toplevel` primary surface
  with one commit, but no primary buffer attach, damage, frame callback, or
  buffer delivery (`reason=no-primary-surface-buffer`).
- Chrome produced six execbuffers, far below the passing Linux control's
  thousands of submits.
- Capture reported eight nonblack frames, but scanout/KMS hashes were nearly
  static and no present samples were recorded, so this remains downstream
  evidence until renderer/media/surface delivery is fixed.
- `vfs_iput` duplicate-remove warnings appeared during Chrome child cleanup.
  Keep them as a VFS lifecycle reducer lead if they recur, but do not treat
  them as the media-start root cause without tighter child/file evidence.

The skip-canplay follow-up is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T064705Z-skip-canplay-no-perf-lines-sampler-timeout/`.
It used `skipCanPlay=1` in the local perf-video URL and zero capture samples, so
post-evidence passed in capture-skipped mode even though the top-level sampler
status still timed out.

Durable signal from that run:

- Bypassing `canPlayType` / MSE capability queries moved the page past the
  previous `before-src` stop. Console evidence reached `canplay-skip`,
  `fetch-begin`, `after-src`, `after-load-call`, `PERF-VIDEO start`,
  `before-play`, `after-play-call`, microtasks, `fetch-error`, and a timer
  tick.
- The normal `before-src` stall is therefore at least partly a media capability
  query issue, but the bypass did not make playback work. The video element
  stayed at `ready=0`, `net=3`, `currentSrc=(empty)`, `presented=0`,
  `decoded=0`, and `dropped=0`, with no playing/tick/rvfc/result events.
- The diagnostic `fetch()` was blocked by Chromium's `file://` CORS policy from
  `origin null`. Treat it as a script-progress marker only; the `<video src>`
  path still needs direct open/read/admission evidence.
- The tightened role detector showed no real renderer:
  `renderer_seen=0`, `renderer_candidate_seen=0`, and `renderer_pids=0`, with
  browser, GPU, utility, and zygote-role processes only.
- Wayland stayed blocked at no primary buffer delivery
  (`reason=no-primary-surface-buffer`), Chrome submitted only six execbuffers,
  and there were no Chromium INT3 traps or page faults.

Next reducer direction: compare Linux and xv6 on the local video file load and
browser-to-zygote renderer admission path. Look for the first divergence in
file open/read/fstat/mmap, renderer process argv (`--type=renderer`), Mojo/IPC
wakeup, and Wayland buffer attach. Do not patch EGL robustness or GBM context
handling from this evidence; Linux already rejects the same robust context
profiles.

For the file-load branch, use the xv6-owned trace knob
`chrome_media_fd_trace=1` first. It is disabled by default and logs only
Chrome-owned open/openat, fstat, lseek, read, and pread64 events for `.mp4` and
`perf-video.html` paths with `chrome-media-fd-trace:` prefixes.

The first media-FD trace run is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T065754Z-media-fd-trace-html-only-gpu-crash/`.
It failed top-level as `chromium-video-capture-status-timeout`; post-evidence
completed and reported `status=FAIL reason=chrome-crash-regression`.

Durable signal from that run:

- `chrome-media-fd-trace` proved Chromium opened
  `/share/webkit/perf-video.html`, fstat'd it, and read it via two `pread64`
  calls.
- No `chrome-media-fd-trace` event for
  `/share/webkit/perf-1280x800-60fps.mp4` appeared. The page still logged
  `src=file:///share/webkit/perf-1280x800-60fps.mp4` but
  `currentSrc=(empty)`, `ready=0`, no playing/tick/rvfc/result, and no renderer
  argv.
- Wayland still failed at no primary surface buffer, and Chrome produced only
  eleven execbuffers.
- This run exposed a GPU-process retry/crash path: pid 525 repeatedly logged
  `eglCreateContext ... EGL_BAD_ATTRIBUTE` with the Linux-matching
  GPU-preferences FNV `0xe9b76745`, despite the normal policy still omitting
  explicit `--use-gl` and `--use-angle`.

Treat the immediate branch as browser/renderer/media admission before MP4 open:
why does the page/renderer path not hand the video URL to Chromium's file/media
loader? The GPU-process crash is a correlated lead, but robust-context rejection
alone still matches Linux; compare the exact context attributes or process
admission sequence before patching EGL/GBM behavior.

The follow-up Chromium-only EGL-preload run is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T070912Z-chromium-egl-trace-no-egl-entrypoints-html-only/`.
It failed top-level as `chromium-video-sampler-completion-timeout`, but
post-evidence completed with `status=PASS reason=capture-skipped`. This is
usable evidence, not a KWin startup failure: the EGL preload was scoped to
Chromium and observed browser/GPU/zygote processes, render-node opens, DRM
ioctls, socketpair/SCM traffic, and GPU-process trace environment inheritance.

Critical result: the Chromium EGL trace saw no EGL call sites at all
(`egl_get_display=0`, `egl_initialize=0`, `dlsym=0`, `dlopen=0`,
`create_context=0`), while GPU init/config/exit errors and GL-request errors
were also zero. The run again opened/read only `perf-video.html`, never opened
the MP4, kept `currentSrc=(empty)` and `ready=0`, saw no renderer role, and
stopped Wayland at no primary surface buffer. Do not patch EGL robustness from
this run; the next reducer should follow Chromium child/IPC/media admission
before MP4 open.

The procfs live-cmdline work then exposed and fixed an exec race: during
`execve`, procfs could observe the new vm with the old argv snapshot addresses
and briefly return environment-looking bytes as `/proc/<pid>/cmdline`. Kernel
procfs now uses the last coherent snapshot while `exec_in_progress` is set, then
returns to live argv/environ copying after the exec snapshot is committed.
Regression proof:
`build-x86_64/proc-cmdline-rewrite-proof/20260630T074722Z/run.log` passed with
`PROC-CMDLINE-REWRITE-PROOF-PASS reason=live-cmdline`.

The Chromium payload trace then showed a stricter procfs visibility edge:
renderer launch packets reached the zygote with `--type=renderer` and seven SCM
fds, but the forked children still sampled as zygote-like. Linux exposes
Chromium renderer children through a longer setproctitle-style rewrite that can
extend beyond the original argv span into the original environment/title area.
xv6 procfs now mirrors that shape when the old argv boundary has been
overwritten. Linux host proof and xv6 proof both passed with the extended
reducer; the xv6 artifact is
`build-x86_64/proc-cmdline-rewrite-proof/20260630T104156Z/run.log`, where the
after-read includes
`proc-cmdline-live-long-probe ... --type=renderer marker=env-span`. Re-run the
Chromium payload/process reducer before treating the last zygote-looking child
set as definitely non-renderer.

A preflight-enabled Chromium phase trace attempt is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T075140Z-exec-phase-preflight-qtqml-timeout/`.
It failed before Chromium in the KDE preflight runner (`qtqml-running`), while
the standalone QtQML IFUNC probe itself passed. Treat it as preflight/harness
noise, not Chromium evidence.

The current Chromium phase-trace evidence is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T075530Z-exec-phase-chromium-sampler-timeout-preflight-off/`.
It disabled KDE preflight and kept normal no-forced-GL Chromium policy. Browser
exec completed in under a second and the GPU-process exec completed in about
1.3s, so the earlier "stuck in exec" symptom was the procfs race plus slow but
finite exec, not the current blocker. Chromium reached browser, GPU-process,
zygote, and utility roles, but still produced no renderer role and no MP4 open.
The page reached `PERF-VIDEO start` and `before-play`; `currentSrc` remained
empty and media counters stayed zero. The GPU process repeatedly logged
`eglCreateContext ES 3.0 failed with EGL_BAD_ATTRIBUTE`, Chrome produced no
execbuffers, and post-evidence reported
`status=FAIL reason=chrome-crash-regression`.

The harness has also been hardened so the Chromium sampler status file becomes
terminal as soon as the explicit sampler PASS line appears, before optional
fast/wait census helpers are joined. That prevents the top-level smoke label
from reporting a stale sampler timeout when the real post-evidence failure is
GPU/renderer/media admission.

`KDE_SMOKE_CHROMIUM_VIDEO_SAMPLES=0` now hard-skips both frame capture and the
generated Chromium process sampler. The sampler script emits explicit SKIP
evidence (`chromium_sample_only=0 ... status=SKIP reason=zero-samples` plus
`status=DONE result=SKIP reason=zero-samples samples=0 source=harness`), so
disabled sampler runs do not depend on shell `$?` behavior or timeout fallback.

Latest normal Chromium crash/process evidence was preserved at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T085857Z-chrome-crash-process-sampler-proof/`.
It was a Chromium child/network-service restart shape, not a kernel crash:
children exited status 0 after "15 seconds with no connection"; Chromium opened
and read `/share/webkit/perf-video.html`, but no MP4 open or stable renderer
role appeared. Kernel traces showed `MSG_CMSG_CLOEXEC` honoring the requested
recv flags.

The exact AF_UNIX primitive suspected from that trace now has a reducer and
passes on both Linux and xv6. `webkitabitest chromium-ipc` covers stream
`SCM_RIGHTS`, `recvmsg(MSG_DONTWAIT|MSG_CMSG_CLOEXEC)`, received-fd CLOEXEC and
file checks, `EPOLLONESHOT` disable after delivery, `EPOLL_CTL_MOD` rearm,
SCM stream barrier behavior, and final nonblocking `EAGAIN`. Linux control:
`build-x86_64/webkit-chromium-ipc-proof/linux-host-control/run.log`. xv6 proof:
`build-x86_64/webkit-chromium-ipc-proof/20260630T091216Z/run.log`.

The same focused reducer now covers the later tailtrace shape too:
`SOCK_SEQPACKET` bootstrap with a 52-byte `SCM_RIGHTS` packet, first
`EPOLLIN`, drained `EAGAIN`, Linux-style `shutdown(SHUT_WR)` EOF wake as
`EPOLLIN`, and peer-close EOF wake as `EPOLLIN|EPOLLHUP`. Linux host probe
confirmed those event masks before encoding them. xv6 proof:
`build-x86_64/webkit-chromium-ipc-proof/20260630T100535Z/run.log`, with
`PASS: Chromium-shaped stream SCM_RIGHTS epoll oneshot`,
`PASS: Chromium-shaped seqpacket SCM_RIGHTS bootstrap EOF`, and
`webkitabitest: 2 passed, 0 skipped, 0 failed`.

The focused reducer now also covers the `SO_PASSCRED` credential ping seen in
the child IPC traces. Linux host probing confirmed receiver-side `SO_PASSCRED`
on `SOCK_SEQPACKET` delivers an 11-byte payload with one `SCM_CREDENTIALS`
cmsg, then drains with `EAGAIN` and reports peer close as `EPOLLIN|EPOLLHUP`.
xv6 proof:
`build-x86_64/webkit-chromium-ipc-proof/20260630T101833Z/run.log`, status
`build-x86_64/webkit-chromium-ipc-proof/20260630T101833Z/status.txt`, with
`PASS: Chromium-shaped seqpacket PASSCRED bootstrap` and
`webkitabitest: 3 passed, 0 skipped, 0 failed`. Treat raw `SCM_RIGHTS`,
`SOCK_SEQPACKET` EOF, one-shot epoll, and `SCM_CREDENTIALS` as closed for the
current no-connection shape.

The later Chromium trace also showed a seqpacket child sending after local
`SHUT_RD` while its peer had `SHUT_WR`. Linux control confirmed that this is
valid: the send succeeds, the receiver sees `EPOLLIN` without `EPOLLHUP`, and
`recvmsg(MSG_DONTWAIT|MSG_CMSG_CLOEXEC)` returns the byte plus `SCM_RIGHTS`.
The reducer now encodes that edge too. xv6 proof:
`build-x86_64/webkit-chromium-ipc-proof/20260630T120551Z/run.log`, status
`build-x86_64/webkit-chromium-ipc-proof/20260630T120551Z/status.txt`, with
`PASS: Chromium-shaped seqpacket half-close SCM_RIGHTS` and
`webkitabitest: 4 passed, 0 skipped, 0 failed`. Treat this half-close SCM
surface as closed for the current no-connection shape.

The Chromium-video post-evidence parser now emits
`no_connection_child_summary=`. It is host-side evidence only and changes no
guest behavior. Replaying it against the latest crash/process artifact above
found five no-connection children, two NetworkService restarts, two
`network+child+utility` NetworkService attempts, and active IPC/epoll traffic
before clean status-0 exits. Treat the next boundary as child/Mojo admission or
protocol state above the raw fd-passing and one-shot epoll primitives.

Additional tailtrace evidence:
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T095134Z-chromium-tailtrace-child-ipc-capture-timeout/`.
The run timed out under very heavy tracing, but post-evidence showed browser,
GPU, zygote, and utility roles alive; no renderer role; no Chrome faults or
INT3 traps; no GPU/EGL init errors; no Chrome execbuffers; and six
no-connection children using AF_UNIX `SOCK_SEQPACKET` IPC/SCM before clean
status-0 exits. The exit-tail dump is opt-in via
`chrome_syscall_tail_trace=1`.

Latest Linux VM control after this reducer:
`build-x86_64/linux-chromium-vm-control/20260630T100838Z-ubuntu-kwin-virtual-http-perf-proof/`.
It passed under KWin virtual Wayland with virgl/D3D12, created renderer roles
by the third process sample, stabilized at one GPU process plus six renderers,
and decoded video (`decoded=960 dropped=0 currentTime=16`) despite forced
`--use-gl=egl` errors and eventual `--use-gl=disabled` fallback. The remaining
xv6 gap is therefore above raw AF_UNIX/epoll and not explained by Chromium GL
fallback alone; continue with Chromium child/Mojo admission or a gated payload
trace before behavior-changing kernel patches.

Payload trace knob for Chromium child-admission follow-up:
`chrome_unix_ipc_payload_trace=1`. It is off by default, bounded to the first
64 bytes of each traced Chromium AF_UNIX `sendmsg`/`recvmsg` payload, and
requires a Chromium-matching process. Pair it with `chrome_unix_ipc_trace=1`
and bounded syscall-tail tracing only for reducer runs; do not turn it into
default logging.

Fresh zero-sample admission proof after the fast-census gate fix:
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T093145Z-chromium-no-connection-summary-gate-fixed/`.
This run failed as `chromium-video-perf-start-missing`, not as a stale sampler
or census timeout. Post-evidence records `fast_census_required=0`, sampler
`result=SKIP reason=zero-samples`, capture `result=PASS samples=0`, no
renderer/GPU role, no media console lines, no Chrome execbuffers, no INT3/fault,
and no no-connection child in the shortened wait window. Keep using nonzero
process evidence for the next child/Mojo admission probe before changing kernel
socket, epoll, EGL, or DRM behavior.

Updated zero-sample harness/census proof:
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T124134Z-zero-sample-fast-census-harness-proof/`.
The harness now separates framebuffer capture disablement from process census
evidence: `KDE_SMOKE_CHROMIUM_VIDEO_SAMPLES=0` writes explicit sample-skip and
capture-skip status, while requested fast census still runs and produced
`chromium_fast_census_only=1 samples=241 ... status=PASS`. Strict mode labels
the wrapper `chromium-video-capture-skipped`, but post-evidence passed in
capture-skipped mode. The Chromium gap stayed unchanged under normal no-forced
GL policy: browser plus zygotes, no renderer/GPU process, no media events, no
Chrome execbuffers, no INT3/fault, and no no-connection child in this window.
Continue at Chromium child/Mojo admission after zygote startup before any
kernel behavior patch.

procfs renderer-visibility fix:

- Chromium's Linux control shows renderer children as `/chrome
  --type=renderer ...`, but those children can be forked from the zygote and
  have argv rewritten in live user memory after fork. xv6 procfs was returning
  the exec-time snapshot for `/proc/<pid>/cmdline`, which could hide real
  renderer roles from the harness.
- Kernel procfs now keeps the snapshot fallback but attempts a Linux-style live
  copy of saved argv/environ address ranges from the task vm.
- Proof:
  `build-x86_64/proc-cmdline-rewrite-proof/20260630T072619Z/` passed. The
  reducer rewrote its own argv storage and `/proc/self/cmdline` then reported
  `proc-cmdline-live-probe|--type=renderer|marker=live|...`.
- Extended proof:
  `build-x86_64/proc-cmdline-rewrite-proof/20260630T104156Z/run.log` passed
  after the reducer placed `--type=renderer` beyond the original argv span in
  the original environment/title area, matching Linux's longer Chromium
  setproctitle behavior.
- Re-run Chromium-video after the extended proof before making a
  renderer-admission kernel patch. If renderer roles appear, split the next
  work between media load/MP4 open and Wayland surface buffers. If they still
  do not appear, the child admission gap is real rather than procfs-only
  evidence loss.
- Initial post-procfs-fix Chromium-video run:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T072700Z-procfs-live-cmdline-chromium-no-renderer-no-gpu/`.
  The procfs reducer passed first, then the Chromium run kept normal GL policy
  (`AUTO_GL_FLAGS=0`) and used GBM/RELA preprobes plus `chrome_media_fd_trace=1`.
  Post-evidence passed in capture-skipped mode, but the process gap remained:
  no renderer, GPU process, utility process, or renderer candidates, and no
  cmdline truncation. Chrome had browser render-node fd evidence but zero Chrome
  execbuffers and no media FD trace lines, so it did not reach the previous
  HTML-open point. Browser/zygote wait samples point to early child IPC/admission
  rather than EGL/scanout/media decode.
- AF_UNIX `SOCK_SEQPACKET` fd-3 bootstrap mismatch found and fixed:
  Linux accepted a Chromium-like
  `socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC)`, `dup2(child, 3)`,
  8-byte `sendmsg()` bootstrap, `ppoll(fd=3)`, and `read(3)` round trip. xv6
  initially returned the right lengths but wrong bytes for the seqpacket case.
  Root cause: the lwIP-port `sys_sendmsg()` seqpacket path queued payload bytes
  only in packet metadata while the shared VFS `read()` path consumed the socket
  ring. `kernel/lwip_port/sys_socket.c` now writes seqpacket payloads into the
  ring and queues only packet boundaries as metadata. The host/Linux reducer,
  xv6 pre-fix failure, and xv6 post-fix PASS are all captured by
  `scripts/image/kde-unix-socket-probe.c`; post-fix guest output reports
  `socketpair-sendmsg-ppoll-fd3-seqpacket result=PASS`.
- Chromium after the seqpacket fix:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T132636Z-seqpacket-fd3-fixed-still-no-renderer/`.
  Chromium created the sandbox IPC thread and two zygotes, sent both 8-byte
  fd-3 seqpacket bootstrap payloads, and both zygotes exec'd successfully before
  waiting in `ppoll(fd=3)`. The run had no fault, INT3, loader assertion,
  GPU-init error, or GL-request error, but still produced no renderer or GPU
  process (`renderer_pids=0`, `gpu_process_pids=0`, `zygote_pids=2`). The next
  Chromium reducer should reduce perturbation and keep browser/thread dumps
  alive past zygote creation to identify why the browser never sends the
  renderer/GPU launch command.
- Lower-perturbation confirmation:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T133145Z-seqpacket-fix-low-perturbation-browser-read-zygote-ppoll/`.
  This run removed syscall-entry tracing and kept lifecycle, AF_UNIX IPC, and
  longer thread dumps. It preserved 1,488 fast-census lines before the wrapper
  timed out the sampler; post-evidence still showed no renderer/GPU
  (`browser_pids=2`, `zygote_pids=2`, `renderer_pids=0`,
  `gpu_process_pids=0`) and no Chromium fault, INT3, GL-request, or GPU-init
  errors. The useful new shape is browser/zygote synchronization: the browser
  repeatedly sampled as sleeping in a 4-byte read on fd 12, both zygotes sampled
  in `ppoll(fd=3)`, and the sandbox IPC thread remained in `poll()`. Continue
  with a focused zygote child-to-browser bootstrap/ack reducer around the
  control socket.

Linux comparison harness update:

- `scripts/gpu/linux-chromium-performance-proof.sh` is the closest
  KWin/Wayland Linux control. It now defaults to omitting Chromium
  `--use-gl`/`--use-angle` and launching KWin without `--xwayland`, matching the
  passing Linux baseline. Set `LINUX_CHROMIUM_USE_GL` and
  `LINUX_CHROMIUM_USE_ANGLE` explicitly only when testing forced-GL reducers.
- `scripts/gpu/linux-chromium-visible-youtube-proof.sh` has the same GL/ANGLE
  knobs and GPU-preference summary for the visible YouTube baseline.
- Passing KWin/Wayland proof:

```text
build-x86_64/linux-chromium-vm-control/20260630T050817Z-ubuntu-kwin-wayland-http-perf-no-gl-no-xwayland-summary-fixed/
status=PASS
kwin xwayland=0
chromium gl_flags=none
roles: gpu=1 renderer=6
measureFPS=30.2 rvfcFPS=41.947 playbackRate=0.998
virtgpu: ctx_submit=2734 fence_resp=2847
gpu_preferences fnv32=0xe9b76745
screenshot=missing
```

- Use the KWin/Wayland proof first when comparing the current xv6 Chromium-video
  failures:

```sh
bash scripts/gpu/linux-chromium-performance-proof.sh \
  "build-x86_64/linux-chromium-vm-control/$(date -u +%Y%m%dT%H%M%SZ)-ubuntu-kwin-wayland-http-perf-clean-baseline"
```

Harness reliability update:

- `scripts/gpu/kde-plasma-desktop-smoke.expect` now treats Chromium-video
  sampler/capture/GBM status files as completion evidence only when paired with
  explicit result lines or role evidence. The generated sampler, capture, and
  GBM scripts use synced temp-file-plus-rename status writes to avoid zero-byte
  status files after timeout/kill.
- The Chromium-video reducer has an explicit auto-GL switch:
  `KDE_SMOKE_CHROMIUM_AUTO_GL_FLAGS=0` disables implicit launcher GL/ANGLE flags
  for normal Linux-policy comparison. Set it to `1`, or provide explicit
  `KDE_SMOKE_CHROMIUM_VIDEO_EXTRA_FLAGS`, only for forced-GL reducers.
- The Chromium-video reducer also has an optional prelaunch Chrome RELA probe:
  `KDE_SMOKE_CHROMIUM_VIDEO_RELA_PREPROBE=1`. It is off by default. When
  enabled, the generated guest helper runs `/bin/chrome-rela-probe
  --chrome-chain` under `/bin/bash`, writes synced completion-only status, and
  requires explicit `CHROME_RELA_PROBE_CHAIN_PASS` / result lines rather than
  relying on shell `$?`.
- Current launch-only validation in `build-x86_64/kde-plasma-desktop-smoke/`
  passed with that RELA preprobe enabled: top-level status was
  `KDE-PLASMA-DESKTOP-SMOKE-DONE`, preprobe status was
  `status=DONE result=PASS source=chrome-rela-probe-chain`, and the preprobe
  log ended with `CHROME_RELA_PROBE_CHAIN_RESULT checked=35 skipped=5 missing=2
  failed=0`, `CHROME_RELA_PROBE_CHAIN_PASS`, and
  `kde_chromium_rela_preprobe result status=PASS`. The post-evidence file
  recorded `launcher_loader_seen=0`, `launcher_crash_seen=0`, and
  `status=PASS reason=launch-only`.
- The first full-run attempt with this probe was interrupted after exposing a
  guest `grep` option portability bug in the helper; the helper now avoids
  unsupported `grep -q`/extended-regexp options. Do not use that interrupted
  attempt as Chromium behavior evidence.
- Current pre-fix evidence was preserved at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T020952Z-before-harness-status-fix/`.
- A focused post-fix `angle/gles` reducer reached reliable sampler and capture
  evidence, then failed cleanly as `chromium-video-perf-start-missing` in
  `build-x86_64/kde-plasma-desktop-smoke/`: sampler reported
  `chromium_sample_only=1 ... status=PASS`, capture reported
  `status=DONE result=PASS`, and GBM preprobe reported `status=DONE` plus an
  explicit host EGL GBM PASS. The remaining signal was no perf console lines,
  no renderer role, no primary Wayland surface commit, no Chrome execbuffers,
  and a valid but black capture frame. QEMU still showed virgl activity
  (`ctx_submit=60`) from the desktop/GPU stack, but `chrome_count=0`.
- Linux and xv6 virtgpu `GETPARAM` shape now match. Linux control artifact
  `build-x86_64/linux-virgl-gbm-chromium-shape/20260630T114355Z/` passed with
  params 1..8 returning `1,1,0,0,0,0,6,0` and later unknown params returning
  `EINVAL`. xv6 GBM preprobe in
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T114645Z-chromium-getparam32-drm-trace-perf-start-missing/`
  reported the same matrix after the kernel was fixed to write only the low
  32 bits for `DRM_IOCTL_VIRTGPU_GETPARAM`, matching Linux/Mesa expectations.
  That xv6 run still failed as `chromium-video-perf-start-missing`, but the
  host GBM preprobe passed, Chromium created a GPU child, opened the render
  node, created virgl contexts, and submitted Chrome execbuffers. The page
  console stalled immediately after the MP4 `canPlayType()` probe and before
  fetch/video-src/playback, so the current blocker is above raw virtgpu
  capability probing.
- AF_UNIX seqpacket/fd3 bootstrap status after the latest reducer work:
  the earlier xv6 mismatch was real and is fixed. `SOCK_SEQPACKET sendmsg()`
  now writes payload bytes into the socket ring while separately preserving
  packet boundaries, so normal `read()` receives the bytes Linux exposes.
  `scripts/image/kde-unix-socket-probe.c` now also covers the Chromium-shaped
  child-to-browser fd3 ack path: parent sends the 8-byte bootstrap with
  `sendmsg()`, child reads it from fd 3, and parent receives a 4-byte ack with
  plain `read()`. Linux host proof and xv6 proof both pass stream,
  seqpacket, seqpacket-sendmsg, and seqpacket-`SO_PASSCRED` variants. The xv6
  artifact is
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T134008Z-unix-fd3-child-ack-reducer/kde-unix-socket-probe.log`.
- Current full-Chromium evidence after the fd3 reducer:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T134802Z-chromium-unix-rw-no-zygote-ack/`
  shows the browser sending both 8-byte zygote bootstraps and then blocking in
  a 4-byte `read(fd=12)`, with no observed child ack before the shortened
  observation ended. The follow-up
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T135450Z-chromium-lifecycle-child-loads/`
  proves the zygote children were cloned, moved their socket endpoints to fd 3,
  kept fd 3 across exec, and ran with `--type=zygote`, but the broad fd/mmap
  trace heavily perturbed startup and only saw them loading Chrome resources.
  Treat this as evidence that fd inheritance and reduced fd3 ack semantics are
  sound, while the remaining gap is full Chromium zygote post-exec progress and
  child-to-browser protocol ack under realistic startup.
- Low-perturbation follow-up:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T140304Z-low-noise-zygote-fd3-no-ack/`
  booted with `kasan=0 kmemleak=0 klog=0`, lifecycle tracing,
  AF_UNIX IPC payload tracing, and `chrome_unix_rw_trace=1`, without broad
  `chrome_fd_trace`. It reproduced the same shape: no forced GL/ANGLE flags,
  both zygotes exec'd as `--type=zygote`, the browser sent both 8-byte
  bootstrap packets and then entered `read(fd=12, 4)`, but no child fd3
  read/write ack or browser read-exit appeared. The reduced fd3 primitive is
  closed; the next target is the real zygote's post-exec startup path before
  it reaches the fd3 read.
- Full Chromium bounded child syscall/thread evidence:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T141931Z-chromium-zygote-loader-slow-before-fd3/`
  booted with `kasan=0 kmemleak=0 klog=0` and failed before video start. It
  shows the browser waiting in `read(fd=12, 4)` after the zygote bootstrap,
  while the zygote children continue slow loader/library-search progress and
  later sample as runnable in userspace. This makes AF_UNIX less likely than
  full Chromium startup/admission latency or a post-ack child/renderer handoff
  gap.
- Real Chrome zygote fd3 reducer:
  `scripts/image/chrome-zygote-fd3-probe.c` now reproduces the observed
  zygote socket shape outside the full browser. The Linux host control passed
  against the imported Chrome binary with 4-byte ack times around 311 ms
  (`--no-zygote-sandbox`) and 20 ms (plain). The xv6 artifact
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T142830Z-chrome-zygote-fd3-reducer-xv6-nogpu/`
  passed too, with ack times 6444 ms and 1617 ms. The nographic run had a
  malformed `QEMU_APPEND` and KDE started in the background, so keep the timing
  as approximate. The semantic result is still useful: real Chrome fd3
  bootstrap/ack works on xv6, but is much slower than Linux.
- New trace controls are off by default:
  `chrome_unix_rw_trace=1` logs AF_UNIX socket read/write activity for
  Chromium-like processes; broad `chrome_fd_trace=1` should be avoided for
  performance or timing proof because it logs high-volume library and resource
  file activity. The GBM preprobe harness marker is now the opaque
  `KCVEGLDONE`, so that path no longer depends on xv6 `sh` `$?` behavior.
- Harness status fix:
  zero-sample Chromium-video sampler helpers now capture grep output and test
  the captured string instead of using `if grep ...`, avoiding the xv6
  fake-exit-status class of failures. A local host sanity check of the
  generated zero-sample/no-census branch returns
  `status=DONE result=SKIP reason=zero-samples samples=0 source=harness`.
  The VM verification retry
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T140602Z-kde-ready-kwin-exception-before-chromium/`
  staged the fixed helper but failed before Chromium with a KWin exception, so
  keep it as KDE startup noise rather than Chromium evidence.
- Latest normal no-forced-GL child-admission evidence:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T154523Z-chromium-admission-no-forced-gl-capture-skipped/`.
  This run confirms the normal launcher omitted GL/ANGLE forcing
  (`argv_use_gl_count=0`, `argv_use_angle_count=0`) and failed before
  media/FPS: no stable GPU process, no renderer process, no EGL entrypoints, no
  MP4 open, no media perf events, and no Chrome execbuffers. The browser opened
  the render node and issued four DRM ioctls, but the surviving zygotes
  repeatedly ran at user RIP `0x7ffffe7eb9dd`. Treat the next target as
  browser-to-zygote/Mojo admission, not DRM present pacing.
- Current 2026-07-01 child-admission evidence:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T072012Z-chromium-admission-no-renderer-no-connection-crash/`.
  This run kept the normal Linux-policy launcher shape (`argv_use_gl_count=0`,
  `argv_use_angle_count=0`), skipped frame capture with
  `KDE_SMOKE_CHROMIUM_VIDEO_SAMPLES=0`, disabled KDE preflight, passed the RELA
  preprobe (`checked=35 skipped=5 missing=2 failed=0`), and enabled lifecycle,
  AF_UNIX IPC payload, AF_UNIX read/write, syscall-tail, and media-fd tracing.
  It is useful evidence even though the top-level label is
  `chromium-video-chrome-crash-regression`: the failure is Chromium child
  admission, not a kernel panic, Chromium INT3, or page fault.
- The July 1 run moved past the earlier no-GPU/no-utility shape but still did
  not reach renderer or media playback. Fast census saw browser, GPU, utility,
  and zygote roles (`browser_pids=3`, `gpu_process_pids=2`,
  `utility_pids=2`, `zygote_pids=9`) but still `renderer_pids=0`. Chromium
  opened and read `/share/webkit/perf-video.html`, never opened
  `perf-1280x800-60fps.mp4`, and emitted no `PERF-VIDEO` console lines. The
  post-evidence parser reported four no-connection children, including
  child+zygote-style exits and a `network.mojom.NetworkService` restart; the
  launcher also logged one GPU process exit with wait status `0x200`
  (`exit_code=512`, exit status 2).
- Regression guards remained intact in the same run: `/dev/dri/card0` and
  `/dev/dri/renderD128` registered, the NetworkManager shim acquired
  `iface=net0`, the network SNI registered `network-wired-activated`, and
  PipeWire Pulse reported `alsa_output.xv6_virtio` plus its monitor. Virtio-gpu
  activity was low (`ctx_submit=53`) compared with the clean Linux KWin/Wayland
  no-forced-GL comparator `20260630T050817Z`, which admitted GPU by sample 2,
  admitted five renderers by sample 3, stabilized at `gpu=1 renderer=6`, passed
  `VIDEO_RESULT`, and recorded `ctx_submit=2734`.
- Harness map capture knob:
  `KDE_SMOKE_CHROMIUM_PROCESS_FULL_MAPS=1` now requests full Chromium process
  maps from the xv6-owned probe and adds one detailed final process snapshot
  after fast census. It defaults off, so normal smoke logs stay compact.

Current direction:

- Keep Chromium/Mesa upstream-clean.
- Use xv6-owned launch/runtime probes to compare GPU-process argv, EGL/ANGLE
  admission, renderer roles, and render-node activity.
- Keep the stable desktop default while using explicit Chromium GL flags only as
  reducers. Normal Chromium-video comparison should keep
  `KDE_SMOKE_CHROMIUM_AUTO_GL_FLAGS=0`.
- Compare the xv6 normal-launch run against Linux controls by renderer process
  lifecycle, Chromium child/Mojo connection handoff, media/file load after
  `before-src`, Wayland buffer attach/commit, and sustained virtgpu submit
  activity before changing kernel behavior.
- Do not patch AF_UNIX stream `SCM_RIGHTS`, seqpacket bootstrap, fd3
  inheritance, or fd3 ack behavior from the latest trace alone; focused
  Linux/xv6 reducers for those primitives now pass, including a real Chrome
  zygote fd3 reducer. Continue above that layer unless a new trace produces a
  narrower failing socket/epoll reducer.
- For the next Chromium reducer, target the delta between isolated zygote ack
  success and full Chromium's slow/missing video start: child/Mojo admission
  after zygote ack, renderer role creation, media file open/read, and Wayland
  buffer delivery. Avoid broad `chrome_fd_trace=1` unless the target is
  specifically file descriptor churn.
- The next full-Chromium evidence run should reduce perturbation rather than
  patch behavior: keep normal no-forced-GL policy, zero capture samples, full
  process maps, and only lifecycle + AF_UNIX IPC payload + AF_UNIX read/write +
  media-fd tracing. If that exposes a concrete bad Mojo/seqpacket payload or
  child connection transition, reduce it in `webkitabitest chromium-ipc` before
  changing kernel socket, poll, futex, DRM, or EGL behavior.
- For forced `angle/gles` and GBM reducers, continue investigating
  GPU-process restart and EGL/ANGLE admission separately from the normal launch
  path.
- Specifically compare the `gpu_preferences` payload from the failing xv6 GPU
  process against the passing Linux VM control where Chromium uses its normal
  Wayland launch policy and still creates stable GPU/renderer roles.
- Run the next full Chromium-video reducer with
  `KDE_SMOKE_CHROMIUM_VIDEO_RELA_PREPROBE=1`. If the RELA preprobe passes and a
  later `elf_machine_rela_relative` loader assertion appears, treat static
  Chrome/dependency bytes and the prelaunch file view as clean in that boot and
  shift the reducer target to concurrent child exec, file-backed mmap/fault, or
  dynamic-linker mapping behavior under multiprocess pressure.
- Continue collecting video stalls as GPU-process restart, EGL admission,
  scanout/present pacing, or real memory/BO-growth evidence.

2026-07-01 loader/admission update:

- The low-noise census attempt archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T073450Z-chromium-low-noise-census-launcher-loader-regression/`
  is not video evidence. It used `POST_STRICT=0`, launched Chromium with
  no forced GL/ANGLE flags, and then the browser process exited `status=127`
  after the glibc loader reported
  `elf_machine_rela_relative: Assertion ... R_X86_64_RELATIVE`. No Chrome
  children were cloned, no media lines appeared, and post-evidence correctly
  classified the artifact as `status=FAIL reason=launcher-loader-regression`.
  GPU nodes, NetworkManager SNI, and PipeWire/Pulse evidence stayed intact.
- The standalone RELA proof
  `build-x86_64/chrome-rela-probe-proof/20260701T073850Z/run.log` then passed
  eight consecutive `/bin/chrome-rela-probe --chrome-chain` iterations:
  every iteration ended with
  `CHROME_RELA_PROBE_CHAIN_RESULT checked=35 skipped=5 missing=2 failed=0`
  and `CHROME_RELA_PROBE_CHAIN_PASS`. The generated 8 GiB proof image was
  removed after the run; the log and startup script remain.
- The next desktop admission run is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T074344Z-chromium-rela-clean-renderer-admission-chrome-crash/`.
  It enabled the RELA preprobe in the same KDE boot, and the preprobe passed
  before Chromium launch. This time there was no loader assertion:
  `launcher_loader_seen=0`, `rela_preprobe_summary ... status=PASS`.
  Chromium reached browser, zygote, GPU-process, and NetworkService roles;
  fast census saw `gpu_process_pids=1`, `utility_pids=1`, and `zygote_pids=6`.
  The AF_UNIX payload trace captured a renderer launch packet sent to the
  zygote (`--type=renderer`) and a subsequent child clone, but lifecycle/proc
  evidence still did not observe a stable renderer process
  (`exec_renderer=0`, `renderer_pids=0`), so treat renderer admission as
  requested but not proven stable.
- The same run moved the page past the old `before-src` stall using
  `skipCanPlay=1`: console output reached `canplay-skip`, `fetch-begin`,
  `after-src`, `after-load-call`, `PERF-VIDEO start`, `before-play`,
  `after-play-call`, microtasks, and `fetch-error`. The `<video>` path still
  never attached the media: `currentSrc=(empty)`, `ready=0`, `presented=0`,
  `decoded=0`, `dropped=0`. `chrome_media_fd_trace` saw only
  `/share/webkit/perf-video.html` open/fstat/pread64 events and no MP4 open.
  NetworkService crashed/restarted after two "15 seconds with no connection"
  children, and post-evidence classified the run as
  `status=FAIL reason=chrome-crash-regression`.
- Updated next reducer target: the current normal path is later than
  deterministic loader/RELA corruption and raw zygote IPC primitives, but still
  before media file open/playback. Focus on the browser-to-child/NetworkService
  handoff that should turn the renderer request into a stable renderer and
  initiate the `<video src=file://...mp4>` open. Avoid DRM/FPS patches until a
  run opens the MP4 or reaches frame presentation.
- The full-map, payload-heavy follow-up is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T080932Z-chromium-fullmaps-renderer-payload-no-connection-crash/`.
  It kept normal no-forced-GL policy, disabled KDE preflight, and enabled
  Chromium full maps, EGL preload tracing, AF_UNIX payload tracing, syscall
  tails, and thread dumps. The run failed as
  `status=FAIL reason=chrome-crash-regression`, but it is the strongest
  admission evidence so far: GPU-process role was present
  (`gpu_process_pids=1`), the GPU child opened `/dev/dri/renderD128`, Chromium
  exchanged 1292 traced IPC operations in the EGL trace, delivered 68
  `SCM_RIGHTS` messages carrying 123 fds, delivered credential-bearing
  `CHILD_PING` traffic, and the browser sent a `--type=renderer` launch packet
  with seven fds to zygote pid 189. The zygote received the 1508-byte renderer
  payload and the passed fds, then cloned child pid 393, which continued
  ChildIOT/Mojo traffic and received further `SCM_RIGHTS` fds. That rules out
  raw AF_UNIX fd/credential delivery as the current whole failure.
- The remaining blocker is now narrower: full Chromium still reports
  `exec_renderer=0`, `renderer_pids=0`, no MP4 open, no media perf lines, and
  three "15 seconds with no connection" children including NetworkService.
  Because Chromium zygote children may not exec a new image, do not treat
  `exec_renderer=0` alone as proof that no renderer was requested. The next
  reducer should follow the post-fork zygote child from the received renderer
  payload through argv/proctitle rewrite, initial-client-fd setup, Mojo channel
  readiness, and media URL handoff. Avoid more EGL, DRM, or FPS work until a
  renderer is stable enough to open the MP4 or emit media events.
- A strict GBM config sanity rerun is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T090602Z-chromium-video-egl-gbm-require-config-fail/`.
  It intentionally set `KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_REQUIRE_CONFIG=1` and
  failed before Chromium launch because virgl GBM exposed no ES RGB pbuffer
  configs: `egl_choose_config ... status=FAIL reason=no_config_required`,
  while render-node open, virtgpu GETPARAM, GBM device creation, EGL init, and
  configless/surfaceless extension probes were otherwise Linux-shaped
  (`pbuffer=0`, `window=250`, `surfaceless_es3_count=50`). Treat pbuffer
  absence as a recorded compatibility fact, not a hard gate for current
  Chromium admission.
- The post-AF_UNIX-rescan normal Chromium admission run is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T091217Z-chromium-post-afunix-rescan-mojo-no-renderer-crash/`.
  It kept `poll_notify_full_wait=1`, `vfs_backend_read_revive=1`, normal
  no-forced-GL launcher policy, RELA and GBM preprobes, zero video samples,
  full maps, IPC payload tracing, media fd tracing, syscall tails, and thread
  dumps. RELA passed (`checked=35 skipped=5 missing=2 failed=0`) and GBM
  passed with a configless/surfaceless ES3 context and renderer
  `virgl (D3D12 (Intel(R) UHD Graphics))`; the host GBM status file remained
  completion-only (`status=DONE`).
- The same run got farther than raw zygote bootstrap: lifecycle evidence saw
  `clone=149`, `clone_process=17`, `clone_thread=132`, `exec=11`,
  `clone_gpu=3`, `clone_utility=24`, `clone_zygote=76`, `exec_gpu=1`,
  `exec_utility=2`, and `exec_zygote=2`. Fast census saw browser, GPU,
  utility, and zygote roles (`browser_pids=3`, `gpu_process_pids=1`,
  `utility_pids=2`, `zygote_pids=9`), but still no stable renderer
  (`renderer_pids=0`, `exec_renderer=0`) and no media progress
  (`perf_count=0`, `playing_count=0`, `result_count=0`, no MP4 open in
  `chrome_media_fd_trace`, and no Chrome execbuffers). Four children hit
  Chromium's "15 seconds with no connection" watchdog, including one
  NetworkService restart; QEMU GPU work stayed tiny (`ctx_submit=67`,
  `res_flush=19`) compared with the Linux KWin Chromium-video baseline.
- Updated next reducer target: the browser/zygote path now proves enough
  AF_UNIX/SCM/CREDENTIALS delivery for `CHILD_PING`, FD-passed Mojo traffic,
  a GPU child, and NetworkService utility startup, but not enough for child
  connection establishment. Before a behavior patch, compare or trace the
  post-fork child handoff around `--initial-client-fd`, `SO_PEERCRED`,
  `shutdown()` half-close state, epoll/poll readiness, futex waits, and
  procfs/cmdline/proctitle updates for the no-connection children.
- The first extracted child-control reducer now lives in
  `user/programs/webkitabitest/webkitabitest.c` as
  `chromium-forked-ipc`. It creates a forked `SOCK_SEQPACKET|SOCK_NONBLOCK`
  pair, validates cross-process `SO_PASSCRED`/`SO_PEERCRED`, receives a
  `CHILD_PING`, passes a bootstrap payload plus `SCM_RIGHTS` fd with
  `MSG_CMSG_CLOEXEC`, rearms one-shot epoll across ACK, `shutdown(SHUT_WR)`,
  and final close, and verifies every message by payload, credentials, fd
  contents, and EOF. Linux control passed with
  `build-x86_64/sysroot/bin/webkitabitest chromium-forked-ipc`
  (`1 passed, 0 skipped, 0 failed`). xv6 guest proof passed in
  `build-x86_64/webkitabitest-forked-ipc/20260701T093559Z/run.log`
  (`WEBKITABITEST-FORKED-IPC-PASS`); the temporary copied 8 GiB test image was
  removed after preserving the log. This closes the minimal forked seqpacket
  credentials/fd/half-close shape as the direct Chromium no-connection cause.
  The next proof should trace a Chromium-specific layer that the reducer does
  not model: exact initial-client-fd selection, Mojo invitation acceptance,
  process role/proctitle transition, or task/thread scheduling after the
  zygote child receives its launch payload.
- The current low-capture child-admission proof is archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T100334Z-chromium-admission-fd3-ack-then-no-connection-crash/`.
  It kept normal no-forced-GL policy, zero frame samples, RELA and GBM
  preprobes, full process maps, lifecycle tracing, AF_UNIX IPC/payload/read-write
  tracing, media-fd tracing, clone/exec fdtable snapshots, syscall tails, and
  epoll tracing. It failed as
  `status=FAIL reason=chrome-crash-regression`, not a kernel crash. RELA passed
  (`checked=35 skipped=5 missing=2 failed=0`), GBM passed with virgl GLES on
  `/dev/dri/renderD128`, and capture was intentionally skipped
  (`samples=0`). The important new signal is that full Chromium's zygote fd3
  bootstrap and 4-byte ACK now succeeded in-run: both zygotes received the
  8-byte fd3 seqpacket payload, wrote the 4-byte ACK on fd 3, and the browser
  read returned 4 bytes on its side. Therefore the old "browser waits forever
  for fd3 ACK" branch is no longer the current blocker. Fast census reached
  browser, one GPU process, one utility process, and zygotes, but still no
  renderer processes; no MP4 open or media progress appeared. Two no-connection
  children remained: a `network.mojom.NetworkService` utility with render-node
  and shared-file arguments, and a zygote-shaped child with active ChildIOT/Mojo
  traffic and SCM fd receipt. The next Chromium proof should move past fd3
  bootstrap and trace exact child/Mojo connection admission: payload-derived
  role/proctitle transition, initial-client-fd or Mojo endpoint selection,
  thread scheduling after ChildIOT traffic, and the point where Linux creates
  stable renderer roles and opens the MP4. Avoid DRM/FPS patches until this
  path admits a stable renderer/media load.
- Harness note: `chromium_no_connection_child_summary` now reports
  `role_source` and `payload_roles` so a no-connection child whose lifecycle
  lines are absent can still show same-run payload-derived role bits. Payload
  roles are marked as `role_source=payload`, not as lifecycle evidence.

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
