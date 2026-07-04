# Active xv6 Work Plan

Last updated: 2026-07-04 (appended Q2 launch-only post-system-tray-isolation
result on top-level `81f9e5b`; materialized on branch
codex/host-linux-abi-shell-port-ff as a copy of the canonical plan; appended
"M7 / Present-Path + Measurement-Validity Findings (2026-07-04, OFFLINE)" near
the P2 lane. NOTE: the canonical plan also lives on
codex/host-linux-abi-shell-port and origin/codex/kde-qt-wayland-bringup;
reconcile before treating this -ff copy as authoritative).
Prior: 2026-07-03 (added Code Review + in-VM Runtime Audit; recut Q1
as review-informed; kernel+user Q1 commits landed).

Single-plan rule: this is the only live plan file. The full pre-rewrite plan
with every evidence chain through 2026-07-02 is preserved append-only at
`docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`
(referred to below as "the history file"). Durable mechanisms and workflows
live in `.github/skills/`; this file holds current status, steps,
measurements, and goals only.

## Mission

Make the KDE/Chromium desktop on x86_64 KVM+virgl stable and responsive.
Three lanes, in priority order:

- P0 (correctness): fix the Chromium/YouTube userspace-starvation freeze.
- P1 (performance): eliminate the per-syscall overhead behind slow
  Plasma/Konsole launches.
- P2 (performance): promote the ordered page-flip fix behind Chromium video
  frame dropping.

## Scoreboard — Measurements and Goals

Every change must move one of these without regressing the others. Measure
with the exact commands in each lane's steps; verify the booted cmdline in
every run log (`grep 'x86 kernel cmdline'`).

Measurement validity caveat (2026-07-04): under saturated desktop/KDE
kprofile load, BSP-driven jiffies under-reported wall time by about 14%.
Treat single-run `*_ms` deltas under desktop load as +/-15% unless they
are cross-checked against a monotonic wall reference. Any kprofile run with
`kprofile_timeout_hit=1` is truncated and must not be used for M7/M4/M5/M8
scoreboard movement.

GUI audio note (2026-07-04): host audio is no longer a readiness
prerequisite. KDE starts PipeWire/Pulse and records `/kde-audio-status.log`,
but `pactl` sink/source enumeration is default-off because archived faults put
that crash in the optional libpulse helper path. Re-enable only for direct
audio diagnostics with `kde_pactl_probe=1` or
`KDE_PACTL_READINESS_PROBE=1`. Explicit audio tests still need a live WSLg
audio path; non-audio KDE/Chromium gates may run with `QEMU_AUDIO_BACKEND=none`.

| # | Metric | How measured | Current (2026-07-03) | Goal |
|---|--------|--------------|----------------------|------|
| M1 | YouTube-freeze survival | P0 repro recipe, 15 min observation, 3 runs | R7c/P0 replay battery is 3/3 responsive clean runs; one additional replay attempt was R5-invalid after KWin crashed/restarted; R5 is now classified as historical intermittent KWin startup crash noise for this gate | 3/3 clean, guest shell responsive |
| M2 | Guest `getpid_ns` | `syscalltlb 2000` in nographic guest | 1.63-1.76us after the P1 FS_BASE cache; independently re-verified 2026-07-03: 1.65-1.94us typical, one noisy run 2.3-3.5us | < 1.5us with gate default-on; stretch < 800ns |
| M3 | `tlb_amplification_ns` (1024 pg) | same | noisy band 2.7-3.7us default across three 2026-07-03 verification runs (2690/3375/3734); treat the earlier ~2.6us vs 3.38us delta as run noise, not an FS_BASE regression | 0 on default boot |
| M4 | `konsole_wait_ms` | KDE desktop-interaction reducer | 1958ms independent verification pass 2026-07-03 (was 1888ms R8 pass; the 2386ms P1-gate reading was noise — M4 is genuinely under target) | < 2000ms (Linux same-host ref: 260-510ms) |
| M5 | `first_visible_ms` | same | 12972ms independent verification pass 2026-07-03 (8632-14623ms band) | < 15000ms |
| M6 | `mesakmsgl` direct-KMS FPS | pageflip A/B recipe (P2 step 1) | 100 baseline / 118-125 ordered | ordered default with no desktop regression |
| M7 | `presentedFPS` (60fps video) | Chromium-video reducer (currently blocked) | 27.2 | >= 55 |
| M8 | Idle-desktop host CPU | `ps -o pcpu= -p <qemu pid>` 3 samples, 30s+ after desktop ready, no apps launched | Borderline RED in the Q1 attribution run: 10s host-thread deltas were 106.3% then 101.2%, lifetime `ps pcpu` 128->119%; CPU was on the six vCPU threads, not GTK/virgl/helper threads. Track as R7c/M8 vCPU/KVM idle-cadence follow-up; do not claim Q1 makes M8 green. | < 100% |
| M9 | Chromium window visible | `KDE_SMOKE_REDUCER=chromium-video KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1` | After system-tray isolation, Q2 gets past initial and delayed session liveness and launches the Chromium browser process, but fails before renderer/GPU admission on `No wl_shm object` / `Failed to initialize Wayland platform`; no Chromium surface evidence yet | PASS |

Fork-safety gate for any syscall/scheduler/TLB change: `forktest`,
`clonetest`, `cowtest` all pass in the same boot (`forktest` `rc=1` with
`fork claimed to work N times!` is the known table-exhaustion expectation,
not a failure).

## Current Round (2026-07-03) — Work Order

This is the active queue, RECUT 2026-07-03 after the overnight round
closed R7a/R7b, landed the R7c first slice and P1 2a/2b, finished the
M1 replay battery, classified R4/R5, and moved the R8 frontier to the
Chromium-passthrough GL extension gap. Completed items live in their
lane sections as status records; do not re-execute them.

Active queue, in order:

- Q1 COMMIT SWEEP — LANDED 2026-07-04 through top-level `d60c203`
  (`rootfs: stage KDE desktop runtime overlays`). Q0 commits are present:
  top-level `56d2ee5`, kernel `ef2dab6`, user `d14a2ca`. Focused status
  checks show the top level, `kernel`, `user`, `ports`, and `ports/mesa/src`
  clean; stale dirty-inventory notes below are historical/superseded.
  Non-VM rootfs-refresh checkpoint after the `.stamp` leak fix passed:
  `bash -n`, `git diff --check`, CMake configure, and `rootfs-refresh`;
  debugfs proof showed `/.stamp` absent and `/opt/xv6-kde/README.txt`
  present. No VM boot and no push were part of this checkpoint.
- Q2 = R8 GL implementation: user decision is resolved — pursue
  hardware-accelerated default Chromium via Mesa/virgl passthrough-required
  semantics. Bundled/software fallback remains diagnostic/emergency only, not
  the solution. Offline Mesa first slice was implemented and independently
  reviewed/built, but is not runtime-validated and not full conformance: it
  adds/repairs first-slice GLES2 Gallium advertisement/dispatch/query support
  for `GL_ANGLE_robust_client_memory`,
  `GL_CHROMIUM_bind_generates_resource`, `GL_ANGLE_client_arrays`, and
  `GL_ANGLE_request_extension`; adds `xv6_angle_passthrough.c`, GLES-only
  ANGLE dispatch, requestable-extension empty-list semantics,
  `CLIENT_ARRAYS` false and bind-generates true getters, and robust
  get/readpixels/texture-upload wrappers with error-output hygiene.
  `GL_KHR_debug` already existed. Mesa commit
  `a7eeaa2afca042d58d32de9314558e2639057f8c` now adds GLES-only
  `GL_CHROMIUM_copy_texture` advertisement/dispatch plus shader/blit-based
  copy/conversion paths. Mesa commit `fec4c3be56be` fixes robust uniform
  length results. Mesa commit `fb27245039c5` adds context-specific
  `GL_ANGLE_webgl_compatibility` support via
  `EGL_ANGLE_create_context_webgl_compatibility`; the Wayland
  `mesaanglepassthrough` reducer now passes a staged-library host run rc 0
  covering normal-context absence, WebGL-context presence, and robust uniform
  length PASS. Offline verification passed:
  `git -C ports/mesa/src diff --check`,
  `git -C ports diff --check`,
  `cmake --build build-x86_64/ports --target port-mesa -j2`,
  `cmake --build build-x86_64/ports --target port-wayland-mesacopytexture-install -j2`,
  staged-library host `mesacopytexture` rc 0 on non-software renderer
  `D3D12 (Intel(R) UHD Graphics)`,
  `cmake --build build-x86_64/ports --target port-wayland-mesaanglepassthrough-install -j2`,
  and staged-library host `mesaanglepassthrough` rc 0. Rootfs/image proof
  passed 2026-07-04 offline/no-QEMU on `build-x86_64/fs.img` (mtime
  `2026-07-04 05:33:02 -0400`): debugfs/extraction found `/lib/libGLESv2.so.2.0.0`,
  `/lib/libEGL.so.1.0.0`, `/lib/libgallium-26.2.0-devel.so`,
  `/bin/mesacopytexture`, and `/bin/mesaanglepassthrough`; extracted image
  strings/symbols contain `GL_CHROMIUM_copy_texture`,
  `CopyTextureCHROMIUM`, `CopySubTextureCHROMIUM`,
  `GL_ANGLE_webgl_compatibility`,
  `EGL_ANGLE_create_context_webgl_compatibility`, and
  `GetUniformfvRobustANGLE`. The Chromium image symlinks
  `/opt/host-gui/wayland-chromium/chrome-linux64/libEGL.so` and
  `libGLESv2.so` point to `/lib`, the Chromium launcher puts `/lib` before
  `/usr/lib`, and the KDE/Chromium GL driver paths put `/lib/dri` before the
  KDE overlay DRI directory. Overlay caveat is bounded, not blocking this
  Q2 image proof: the generated KDE overlay has Ubuntu Mesa 25.2.8 GLX/Gallium
  and DRI/GBM copies, and the final image retains GLX/Gallium/DRI/GBM pieces
  under `/usr/lib/x86_64-linux-gnu`, but it has no overlay
  `libEGL.so*`, `libGLESv2.so*`, or `libgbm.so*`; a GLX/Xwayland path may
  still load the overlay Mesa, while the Chromium GLES/EGL Q2 path resolves to
  the staged `/lib` Mesa. Runtime gate attempts 2026-07-04 used the default
  Mesa path with
  `QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=chromium-video KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1`
  and no bundled/software GL override. Both attempts failed before Chromium
  launch with `KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry` after
  `kwin exited before Wayland socket status=32512`; follow-up log inspection
  reclassified this as a deterministic KWin loader ABI failure, not historical
  R5: `/usr/lib/x86_64-linux-gnu/libkwin.so.5` required
  `libinput_event_get_gesture_event@LIBINPUT_0.20.0` while
  `/opt/xv6-kde-abi-libs/libinput.so.10` pointed at the local `/lib`
  libinput shim, which had unversioned `Base` exports and no gesture-event
  symbol. Archives:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T094240Z-q2-chromium-video-launch-only-r5-flake/`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T094451Z-q2-chromium-video-launch-only-r5-rerun-flake/`.
  Rerun stopped after the allowed retry. The rerun
  `kde-chromium-video-post-evidence.log` ends `status=FAIL reason=not-launched`;
  `/host-gui-wayland-chromium.log`, `/chrome_debug.log`,
  `/kde-chromium-launch-probe.log`, and the sampler logs are missing guest
  artifacts. Therefore the old missing-extension fatal is not observed in these
  runs, but only because Chromium never launched. Offline/no-QEMU repair on
  2026-07-04 changed `scripts/image/make-rootfs.sh` so the KDE ABI-libs
  directory stages the generated KDE runtime `libinput.so.10` copy when
  present, while keeping the old local shim fallback for non-KDE images.
  `cmake --build build-x86_64 --target rootfs-refresh -j2` passed; debugfs
  extraction from `build-x86_64/fs.img` proved
  `/opt/xv6-kde-abi-libs/libinput.so.10` is a regular 344320-byte file
  hash-identical to
  `rootfs-generated-overlays/kde-runtime/usr/lib/x86_64-linux-gnu/libinput.so.10.13.0`
  and different from `sysroot/lib/libinput.so.10.0.0`. `objdump -T` on the
  extracted image library shows `libinput_event_get_gesture_event` at
  `LIBINPUT_0.20.0`, and `readelf -V` shows the `LIBINPUT_0.20.0` version
  definition. Host-side `ldd -r` with the extracted library first resolved
  `libinput.so.10` to the extracted copy and reported no
  `libinput_event_get_gesture_event` unresolved symbol. Post-repair runtime
  gate 2026-07-04, default Mesa path/no bundled or extension overrides,
  `QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=chromium-video
  KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1 scripts/gpu/kde-plasma-desktop-smoke.expect`,
  archive
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T095905Z-q2-post-libinput-repair-chromium-launch-only/`,
  still failed before KDE readiness/Chromium launch with
  `KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry`. This is not the
  repaired `libinput_event_get_gesture_event@LIBINPUT_0.20.0` miss and not a
  Chromium/Mesa-extension result: `kde-session-kwin.log` now shows
  `/opt/xv6-kde-abi-libs/libinput.so.10: undefined symbol:
  udev_device_get_udev, version LIBUDEV_183`; post evidence ends
  `status=FAIL reason=not-launched`, with Chromium process counts zero and
  `chromium_mesa_extension_override=""`. Per the loader-recurrence guardrail,
  no rerun was attempted. Offline/no-QEMU repair on 2026-07-04 changed
  `scripts/image/make-rootfs.sh` so `/opt/xv6-kde-abi-libs/libudev.so.1` is
  copied from the generated KDE runtime when present, while preserving the
  `/lib/libudev.so.1` fallback for non-KDE images. Root cause: the staged KDE
  `libinput.so.10.13.0` requires `udev_device_get_udev@LIBUDEV_183`, but the
  local xv6 `/lib/libudev.so.1.0.0` shim lacks that symbol; the KDE runtime
  `libudev.so.1.7.8` provides it and only adds already-present `libcap.so.2`
  as a direct dependency. Verification passed: `bash -n`, `rootfs-refresh`,
  debugfs extraction from `build-x86_64/fs.img` showed `/opt/xv6-kde-abi-libs`
  now contains regular `libinput.so.10` and `libudev.so.1` files; hashes match
  the KDE overlay copies and differ from the local shims; `objdump -T` on the
  extracted `libudev.so.1` shows `udev_device_get_udev@LIBUDEV_183`; extracted
  `libinput.so.10` still requires `libudev.so.1`; and `LD_LIBRARY_PATH` with
  the extracted `/opt` libs first made `ldd -r` report no unresolved/not-found
  entries. `/bin/mesacopytexture` and `/bin/mesaanglepassthrough` remain in
  the refreshed image. Post-KDE-ABI-closure runtime gate 2026-07-04, default
  Mesa path/no bundled, software, or extension overrides,
  `QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=chromium-video
  KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1 scripts/gpu/kde-plasma-desktop-smoke.expect`,
  consumed the single allowed historical-flake rerun and still failed before
  KDE readiness/Chromium launch with
  `KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash`. Both attempts show
  `kwin_wayland` `#PF` at `rip=0x7ffffd85cb85` in
  `/usr/lib/x86_64-linux-gnu/libQt5Core.so.5`, no `undefined symbol`,
  `LIBINPUT`, or `LIBUDEV` loader failure, and post evidence
  `chromium_mesa_extension_override=""` plus `status=FAIL reason=not-launched`.
  Therefore the old Chromium missing-extension fatals remain unobserved only
  because Chromium did not launch. Archives:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T101045Z-q2-post-kde-abi-closure-chromium-launch-only-r5-flake/`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T101153Z-q2-post-kde-abi-closure-chromium-launch-only-r5-rerun/`.
- Q3 = R9 cursor out-of-range triage (user-visible; triage chain in the
  R9 note below).
- Q4 = P1 steps 2c/2d (cpumask atomics skip, CR0.TS shadow) — M2 is at
  1.65-1.94us vs the <1.5us goal; these two cuts are the remaining
  identified fixed costs. Implement both together, one gate battery.
- Q5 = R5 flake-rate reduction: the KWin startup crash family is now the
  main gate polluter and blocked the 2026-07-04 post-KDE-ABI-closure M9 retry
  after the single allowed rerun. Root-cause or bound it; it blocks R6 step 2
  and the next meaningful Q2/M9 runtime classification. Offline R5 preflight
  added 2026-07-04: `scripts/gpu/kde-abi-closure-preflight.sh` extracts
  `build-x86_64/fs.img` into a temp root, validates KWin/libkwin,
  plasmashell/libKF5Solid, `/opt/xv6-kde-abi-libs`, and the
  libinput/libudev/libevdev/libmtdev/libwacom closure with image-like
  `LD_LIBRARY_PATH`, `readelf -V`, `objdump -T`, and `ldd -r`, and currently
  passes for `libinput_event_get_gesture_event@LIBINPUT_0.20.0`,
  `udev_device_get_udev@LIBUDEV_183`, and
  `udev_enumerate_scan_subsystems@LIBUDEV_183`. `/bin/kde-libinput-probe` is
  already staged and directly exercises
  `udev_new`, `libinput_udev_create_context`, and
  `libinput_udev_assign_seat("seat0")`; the no-KWin reducer below now runs it
  before the first KWin launch and exits immediately after the probe.
  Offline R5 diagnostic harness patch added 2026-07-04:
  `kde-session.c` has a default-off pre-KWin hook enabled by
  `kde_pre_kwin_libinput_probe=1` or `KDE_PRE_KWIN_LIBINPUT_PROBE=1`; it runs
  `/bin/kde-libinput-probe --r9-cursor-contract --timeout-ms 1`, writes
  `/kde-pre-kwin-libinput.log`, and emits a compact serial status before the
  first KWin launch. `kde-session.c` also accepts
  `kde_pre_kwin_libinput_probe_only=1` for a pre-KWin probe-only exit.
  `kde-plasma-desktop-smoke.expect` now supports
  `KDE_SMOKE_REDUCER=kde-ready`, `KDE_SMOKE_REDUCER=pre-kwin-libinput`, and
  host env
  `KDE_SMOKE_PRE_KWIN_LIBINPUT_PROBE=1`, preserves the pre-KWin log artifact,
  and no longer fails on intermediate `KWin startup attempt=N failed, retrying`
  lines. Offline verification passed: C syntax-only, `git diff --check`,
  `port-wayland-session-install`, `rootfs-refresh`, and debugfs extraction
  showing updated `/bin/kde-session` strings plus staged
  `/bin/kde-libinput-probe`. No runtime gate result is claimed; an attempted
  Expect syntax check (`expect -n`) was not a dry run and was killed.
  Runtime R5 diagnostic 2026-07-04 ran exactly one KDE-ready pre-KWin probe
  with
  `QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=kde-ready KDE_SMOKE_PRE_KWIN_LIBINPUT_PROBE=1 scripts/gpu/kde-plasma-desktop-smoke.expect`;
  Chromium was not launched. The default command left fresh artifacts in
  `build-x86_64/kde-plasma-desktop-smoke/`; compact manual history copy
  (scratch fs image excluded) is
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T104003Z-r5-pre-kwin-libinput-kde-ready/`.
  Result: FAIL `kde-session-ready-crash` (`status_code=3`). The preserved
  `kde-pre-kwin-libinput.log` reports probe `result=PASS` but classifies the
  libinput setup as `assign_seat_failed` (`r9_libinput_status rc=-1 errno=93
  reason=assign_seat_failed`), not `ready fd=N`. `run.log` confirms
  `kde_pre_kwin_libinput_probe=1`, then `launching KWin attempt=1`; there are
  no attempt-2 or retry markers before the harness failure. KWin crashed with
  `#PF cr2=0x8 rip=0x7ffffd85cb85` in
  `/usr/lib/x86_64-linux-gnu/libQt5Core.so.5` at file offset `0x30fb85`,
  resolving against the staged QtCore to `QObject::moveToThread(QThread*)+0x15`.
  Chromium guest artifacts are placeholder `missing guest_path=...` files, so
  this is a pre-Chromium/KWin result and strengthens the
  `LibInput::Connection::create(session)` null-return hypothesis.
  Follow-up reducer/fix 2026-07-04: the no-KWin reducer first reproduced the
  blocker without launching KWin:
  `QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=pre-kwin-libinput scripts/gpu/kde-plasma-desktop-smoke.expect`
  failed with `status_code=8`, `r9_udev_enumerate_summary subsystem=input
  count=0`, Ubuntu/systemd libudev log `udev: failed to create the udev
  monitor`, and `r9_libinput_status rc=-1 errno=93
  reason=assign_seat_failed`. The compact baseline copy is
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T110313Z-r5-pre-kwin-libinput-probe-only-before-shim/`.
  Root cause was the KDE ABI override pairing Ubuntu `libinput.so.10` with
  Ubuntu `libudev.so.1`; that path tripped xv6's missing
  `NETLINK_KOBJECT_UEVENT` support. The smallest fix was to keep Ubuntu
  `libinput.so.10`, restore `/opt/xv6-kde-abi-libs/libudev.so.1` to the xv6
  shim, export the shim's missing common symbols including
  `udev_device_get_udev@LIBUDEV_183`, and teach the shim about
  `/dev/input/event0` and `/dev/input/event1` as initialized `input` devices
  with `ID_INPUT=1`, `ID_INPUT_KEYBOARD=1`/`ID_INPUT_MOUSE=1`, `ID_SEAT=seat0`,
  `WL_SEAT=default`, and `seat` tags. Verification passed:
  `git diff --check`, `git -C ports diff --check`,
  `cmake --build build-x86_64/ports --target port-libudev -j2`,
  `readelf -Ws build-x86_64/sysroot/lib/libudev.so.1.0.0` showing
  `udev_device_get_udev`, `udev_list_entry_get_value`, and
  `udev_device_has_tag` at `LIBUDEV_183`, C syntax-only checks for
  `kde-libinput-probe.c` and `kde-session.c`, and
  `cmake --build build-x86_64 --target rootfs-refresh -j2`.
  `scripts/gpu/kde-abi-closure-preflight.sh` still passes, with
  `udev_device_get_udev@LIBUDEV_183` provided by `/lib/libudev.so.1.0.0`.
  `debugfs` on `build-x86_64/fs.img` confirms
  `/opt/xv6-kde-abi-libs/libudev.so.1` is now a symlink to
  `/lib/libudev.so.1` while `/opt/xv6-kde-abi-libs/libinput.so.10` remains
  the regular Ubuntu/KDE runtime copy.
  Post-fix no-KWin verification with the same reducer passed:
  `status_code=0`, archive
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T110434Z-r5-pre-kwin-libinput-probe-only-shim-ready/`.
  `run.log` has `pre-kwin-libinput-probe-only status=PASS rc=0` and no KWin
  launch marker. The probe now enumerates both input devices and reaches
  `r9_libinput_status rc=0 errno=0 reason=ready fd=6`; `r9_summary` ends
  `result=PASS`. This closes the deterministic `assign_seat_failed` pre-KWin
  blocker. Follow-up KWin-enabled KDE-ready verification after the
  libudev/input seat fix passed with Chromium out of scope:
  `QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=kde-ready KDE_SMOKE_PRE_KWIN_LIBINPUT_PROBE=1 scripts/gpu/kde-plasma-desktop-smoke.expect`.
  Archive:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T111041Z-r5-kde-ready-post-libudev-input-seat-fix/`.
  Status is `status_code=0`
  `KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=kde-ready`; the probe again reports
  `r9_libinput_status rc=0 errno=0 reason=ready fd=6` and
  `r9_summary ... result=PASS`. `run.log` shows virgl render-node setup,
  `launching KWin attempt=1`, `Xwayland is running`, and
  `KWin and plasmashell are running`; there are no attempt-2/retry markers.
  `kde-session-kwin.log` and `kde-plasma-kwin-crash-regression.txt` are empty,
  so the previous `QObject::moveToThread(QThread*)+0x15`/`#PF` signature did
  not reproduce in this single readiness run. Chromium/M9 remains untested.
  Q2 launch-only retry 2026-07-04 then ran exactly once with default
  Mesa/virgl and explicit unsets for bundled/software GL knobs:
  `env -u KDE_SMOKE_CHROMIUM_BUNDLED_GL -u KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE -u MESA_EXTENSION_OVERRIDE -u LIBGL_ALWAYS_SOFTWARE -u GALLIUM_DRIVER -u MESA_LOADER_DRIVER_OVERRIDE -u LIBGL_ALWAYS_INDIRECT QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=chromium-video KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1 scripts/gpu/kde-plasma-desktop-smoke.expect`.
  Archive:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T111839Z-q2-chromium-launch-only-kde-preflight-kwin-timeout/`.
  Result: FAIL `status_code=8`
  `KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-preflight-kwin-timeout`, not a Chromium
  result. `run.log` reached virgl render-node setup, `Xwayland is running`,
  and `KWin and plasmashell are running`, but KDE did not stay ready:
  `kde-preflight-runner.status` remained `status=waiting`, the last readiness
  probe showed `kwin=0 plasmashell=0`, and
  `kde-session-plasma-child.log` has `/usr/bin/plasmashell: symbol lookup
  error: /usr/lib/x86_64-linux-gnu/libKF5Solid.so.5: undefined symbol:
  udev_enumerate_scan_subsystems, version LIBUDEV_183`, followed by
  `The Wayland connection broke`. KWin did not emit the previous `#PF`
  signature: `kde-plasma-kwin-crash-regression.txt` is empty. Renderer evidence
  still proves default GPU acceleration (`virgl (D3D12 (NVIDIA GeForce RTX
  4060 Laptop GPU))`, Mesa `26.2.0-devel (git-fec4c3be56)`); Chromium logs are
  missing guest-path placeholders, post evidence ends `status=FAIL
  reason=not-launched`, and there is no new `GL_CHROMIUM_copy_texture`,
  WebGL/ANGLE, or robust-uniform Chromium evidence because Chromium never
  launched.
  Follow-up Solid/libudev ABI repair 2026-07-04: the Q2 launch-only retry had
  moved past the earlier libinput seat blocker but then lost plasmashell during
  preflight because `libKF5Solid.so.5` required
  `udev_enumerate_scan_subsystems@LIBUDEV_183`. The local xv6 libudev shim now
  exports that symbol at `LIBUDEV_183`, returns local subsystem list entries
  for `/sys/class/drm` and `/sys/class/input` when the matching device nodes
  exist, and resolves those subsystem syspaths to minimal `udev_device`
  objects with Linux-shaped `sysname=drm/input` and `subsystem=subsystem`.
  Offline preflight was broadened to seed `/usr/bin/plasmashell` and
  `/usr/lib/x86_64-linux-gnu/libKF5Solid.so.5`, so this class is caught before
  boot. A sidecar read-only ABI check corroborated the repair: staged
  `libudev.so.1` exports `udev_enumerate_scan_subsystems@LIBUDEV_183`,
  shim-first `ldd -r` for plasmashell/libKF5Solid/kwin_wayland reports no
  unresolved `udev_*` symbols, and a static closure walk from
  plasmashell/libKF5Solid/kwin_wayland found `closure_objects=156` and no
  unresolved shim imports.
  Verification passed: `git diff --check`, `git -C ports diff --check`,
  `bash -n scripts/gpu/kde-abi-closure-preflight.sh`,
  `cmake --build build-x86_64/ports --target port-libudev -j2`,
  `readelf -Ws build-x86_64/sysroot/lib/libudev.so.1.0.0` and image
  extraction both show `udev_enumerate_scan_subsystems@@LIBUDEV_183`, and
  `cmake --build build-x86_64 --target rootfs-refresh -j2` passed. The
  refreshed image still has `/opt/xv6-kde-abi-libs/libudev.so.1 -> /lib/libudev.so.1`.
  Expanded offline preflight now passes with
  `closure_objects=164 versioned_undefs=39332`.
  No-Chromium KDE-ready verification passed with
  `QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=kde-ready KDE_SMOKE_PRE_KWIN_LIBINPUT_PROBE=1 scripts/gpu/kde-plasma-desktop-smoke.expect`;
  archive:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T112932Z-kde-ready-post-udev-scan-subsystems/`.
  The run reported `KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=kde-ready`, KWin and
  plasmashell running, no KWin crash regression, and the pre-KWin libinput
  probe still passing. One Q2 launch-only retry then ran with default
  Mesa/virgl and explicit unsets for bundled/software GL knobs:
  `env -u KDE_SMOKE_CHROMIUM_BUNDLED_GL -u KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE -u MESA_EXTENSION_OVERRIDE -u LIBGL_ALWAYS_SOFTWARE -u GALLIUM_DRIVER -u MESA_LOADER_DRIVER_OVERRIDE -u LIBGL_ALWAYS_INDIRECT QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=chromium-video KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1 scripts/gpu/kde-plasma-desktop-smoke.expect`.
  Archive:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T113038Z-q2-chromium-launch-only-post-udev-scan-subsystems/`.
  Result: FAIL `chromium-video-launch-evidence-FAIL`, not a KDE/Solid loader
  failure. `kde-preflight-runner.status` is `status=PASS`, KWin logged virgl
  on `D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)`, and the Chromium sampler
  passed (`samples=63`, `duration_ms=18000`), with process evidence showing
  the browser launched under `GALLIUM_DRIVER=virgl`,
  `MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu`, `/lib/dri`, and `/lib/libgbm.so.1`.
  The strict launch evidence still failed because
  `/host-gui-wayland-chromium.log`, `/chrome_debug.log`, and capture status
  were missing; the sampled browser later became a zombie. There are no
  `undefined symbol`, `LIBUDEV`, `libKF5Solid`, `symbol lookup`, KWin `#PF`,
  `PANIC`, or fatal page fault markers in the archived logs.
  Follow-up on 2026-07-04 fixed the harness/launcher contract that caused
  the first Q2 failure to be ambiguous: `/bin/wayland-chromium` now writes the
  canonical `/host-gui-wayland-chromium.log` and `/chrome_debug.log` paths,
  keeps `/tmp` compat symlinks, emits strict launch markers/argv/env evidence,
  and the launch probe appends process evidence without reaping the child
  during liveness checks. The smoke harness now falls back to the compat `/tmp`
  logs if the root paths are absent, and future launch-only post-evidence marks
  capture as `SKIP reason=launch-only` instead of treating the intentionally
  absent capture as noise. After `rootfs-refresh`, one Q2 launch-only retry
  with all bundled/software GL env vars unset still failed:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T114635Z-q2-chromium-launch-only-launcher-log-fix-wayland-refused/`.
  This retry narrowed the remaining blocker to KDE/Wayland session liveness,
  not Chromium GL startup: launcher evidence is real
  (`launcher_log=1 launcher_marker=1 launcher_child_exec=1 launcher_url=1`)
  and Chromium exits 1 after `Failed to connect to Wayland display:
  Connection refused`, while `kde-session-plasma-child.log` records
  plasmashell KCrash and `The Wayland connection broke. Did the Wayland
  compositor die?`. Preflight still passes, KWin/host EGL still use accelerated
  virgl on `D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)` with Mesa
  `26.2.0-devel`, the sampler sees only the browser role
  (`renderer_seen=0 gpu_seen=0`) plus crashpad, and no Solid/libudev/KWin #PF,
  PANIC, or fatal page fault markers appear. The current Q2 blocker is
  therefore a compositor/session refusal before Chromium can create Wayland
  surfaces or renderer/GPU roles.
- Q6 = R2 (PCID corruption reducer, then guarded default retry).
- Q7 = R6 step 2 retry (P2 ordered-pageflip default flip) after Q5.
- Tracked follow-up after Q1: R7c/M8 vCPU/KVM idle-cadence work now has
  owner evidence; do not treat it as a Q1 commit blocker, but do not claim
  M8 green. Parked: R3 (bounded-open with diagnostic in place), M7
  (blocked on Q2).

Lane bullets (context for the queue above):

- R1/P0: CLOSED for this round — structural H-d fix landed, 3/3
  responsive M1 replays, R5 classified for the gate. The stale CR-1 caveat
  is resolved by the landed kernel follow-up (`ef2dab6`); do not reopen P0
  from older plan text.
- R2 = P1 step 1 (remaining PCID stale-TLB hole: GUI-hot-path audit,
  corruption reducer, fix, then the guarded default-flip retry per the
  2026-07-02 failed-attempt note).
- R3 = `rcu_head_cache` double-free: bounded-open (5 provoke runs, no
  trigger; owner-tagging diagnostic staged).
- R4 = classified: pactl/libpulse assertion/log recursion crash family
  at KDE session start; not PCID-specific and not kernel fault-path
  evidence. Default smoke remains fail-fast.
- R5 = KWin startup SIGSEGV family: classified historical-intermittent
  for the M1 gate; flake-rate reduction is Q5.
- R6 = P2: step 1 DONE; step 2 default flip blocked by R5 flake (0/2);
  step 3 (M7) blocked by Q2.
- R7 = responsiveness composite: R7a DONE (O(1) assertion), R7b CLOSED
  (inotify FIONREAD ABI fix; M8 345-512% -> ~85-105%), R7c first slice
  DONE. The Q1 M8 owner evidence points at vCPU/KVM idle wake cadence
  while guest PCs are mostly `arch_idle_halt()`, not an R7b spinner or
  idle-pull hot loop.
- R8 = Chromium window not visible: renderer admission EXONERATED after
  the overnight diagnostic chain; real Mesa attr-order bug found+fixed
  (video now plays); remaining default blocker is Chromium passthrough's
  required ANGLE extension set — decision is Q2. M9 tracks it.
- R9 (NEW 2026-07-03, user-reported during VM verification): cursor
  movement out of range in the live GUI session — pointer position does
  not track the host mouse correctly (moves beyond/short of the actual
  screen position). Not yet triaged. Suspect chain: QEMU virtio-tablet
  advertises `abs_x=0..32767 abs_y=0..32767` (boot log `virtio_input:
  initialized ... abs_x=0..32767`), kernel evdev forwards ABS events via
  `/dev/input/event0/1`, and the compositor (KWin/libinput) must scale
  absolute coordinates to the 1280x800 mode. Verify in order: (1) the
  absinfo min/max the kernel's evdev EVIOCGABS reports to userspace
  matches 0..32767 (a wrong/stale max means libinput scales wrong);
  (2) raw ABS_X/ABS_Y event values at the screen edges (guest-side
  evdev dump) vs host QEMU pointer; (3) whether the boot used
  `virtio_gpu_host_cursor_only=1` / cursor-compat flags and whether the
  hardware-cursor plane position transform applies the same scale.
  Also check any 07-03 changes touching input/evdev before blaming QEMU.
  Note: KDE smoke interaction latency metrics use monitor-injected input
  and guest-side sampling, so M4/M5 remain valid even with this bug.
  2026-07-04 offline follow-up: source/archive triage found the virtio
  tablet raw `0..32767` path already normalized to the guest
  `0..65535` `/dev/mouse`/evdev/libinput contract, with no archived
  EVIOCGABS/raw-event/transformed-coordinate evidence proving a 2x range
  mismatch. Added and staged a default-off `/bin/kde-libinput-probe`
  R9 cursor-contract diagnostic (`--r9-cursor-contract` or
  `KDE_LIBINPUT_PROBE_R9_CURSOR=1`) that records EVIOCGABS, raw evdev,
  `/dev/mouse`, and libinput transformed-coordinate samples without
  changing launcher/session defaults.
  2026-07-04 runtime probe attempts still did not produce cursor-contract
  evidence. First artifact:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T040145Z-r9-cursor-contract-probe/`
  reached KWin/plasmashell, confirmed cmdline
  `virtio_gpu_host_cursor_only=1` and
  `virtio_gpu_cursor_rgba_compat=1`, registered
  `virtio_input` abs `0..32767`, and accepted five QEMU `mouse_move`
  commands, but probe output was absent and `r9-lines.txt` was empty
  (likely serial marker interleaving/long serial command path). Second
  artifact:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T040926Z-r9-cursor-contract-probe-rerun/`
  booted KDE and registered input, then injected `/r9-run.sh` but timed out
  reacquiring an interactive shell before invoking it; no `r9_*` lines and
  no monitor moves were captured. Conclusion: R9 runtime evidence is still
  missing, and these failures diagnose the harness command path, not the
  cursor. Do not run a third VM in this slice. Next attempt must avoid the
  interactive root shell entirely (startup/service-triggered injected
  script or existing guest-agent artifact path), write `/r9-probe-output.log`
  and `/r9-probe.status`, retrieve them via debugfs after shutdown, and
  inject pointer moves only after a script-generated armed marker.
  Worker AF added reusable source-only harness
  `scripts/gpu/r9-cursor-contract-probe.expect`: it copies the fs image,
  injects startup-triggered `/r9-run.sh`, polls `/r9-probe.status` with
  read-only debugfs, and sends monitor `mouse_move` events only after
  `phase=armed`. Offline/no-VM verification passed via the harness dry-run,
  generated guest script syntax checks, debugfs image checks, and QEMU command
  construction; runtime cursor evidence remains intentionally uncollected in
  this slice.

## Code Review — Uncommitted Kernel Diff (2026-07-03)

COMPACTED 2026-07-04. A read-only review of the ~27-file kernel Q1 commit
scope produced CR-1..CR-10. Full original findings + the nographic/Q1 batch
archive evidence are in the history file
`docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`.
Current status:

- FIXED in-tree (landed with the Q1/kernel commits): CR-1 (`eevdf_idle_pull`
  now treats remote `nr_running>=1` as candidate work; CR-10 idle-pull
  cooldown stamped only after a real pull folded in), CR-2 (`ARCH_SET_FS`
  and `clone(CLONE_SETTLS)` reject non-canonical FS base -> `-EPERM`, with a
  `linuxsyscallabitest tls-reject` reducer; CR-4 `push_off` wrap folded in),
  CR-5/CR-6 (starve/timer probe hooks early-return on a cached gate;
  per-CPU storage cache-line aligned; rq snapshot buffer static not
  IRQ-stack), CR-8 (idle loop asserts IF=0 before the halt check).
- STILL OPEN — tracked, not fixed:
  - CR-3: dropping the entry-time FS_BASE `rdmsr` re-sync breaks user
    `mov %fs` selector reloads (stale `ARCH_GET_FS`; hurts Wine-style
    runtimes). P1 latent ABI follow-up.
  - CR-7: starve-probe reads remote `current_se->thread`/wake_list lock-free
    from IRQ — safe today only via RCU-deferred frees; undocumented
    invariant, becomes a UAF if that path is ever preemptible. Only
    reachable with `sched_starve_probe=1` (default-off). Fix: remote
    `rq_lock` or explicit `rcu_read_lock`.
  - CR-9: timer lock-free fast path advances `current_tick` while idle -> a
    timer armed for the next jiffy can lose a 1-jiffy boundary race
    (spurious `timer_add -1` via `timerfd_settime`, or <=1ms-late expiry).
    Needs a focused timerfd reducer.
- Non-blocking cleanup (do NOT gold-plate): 4th open-coded `FIONREAD`
  0x541B -> shared uabi header; two hand-rolled hash tables (`rq.c`,
  `rcu.c`) duplicating kmemleak's; dual ad-hoc lock-hold instrumentation
  (`rq.c`+`timer.c`); x86-only IPI probe hooks (no riscv parity); ~120
  lines of duplicated chrome-unix trace printf in `sys_socket.c`.
- M8 caveat from the Q1 batch: idle host CPU is borderline-RED and traced to
  the six vCPU/KVM threads sitting in `arch_idle_halt()`/`kvm_vcpu_block`,
  NOT an idle-pull loop or userspace spinner. M8 is a tracked R7c follow-up;
  commit messages must not claim M8 green.

## Runtime Audit — Scoreboard Reproduction (2026-07-03, in-VM)

Independent in-VM audit of the scoreboard. Two passes were run: an earlier
pass while WSLg host PulseAudio was DOWN (all GUI metrics blocked), and a
later pass after audio recovered (GUI metrics reproduced). This section is
historical for the audited build (`xv6.bin` 16:11; kernel HEAD `07efc74`)
and predates the later Q0 correction commits recorded in the Work Order
above. Harness scripts (new): `scripts/audit/plan-audit-nographic.expect` +
`plan-audit-battery.sh` — non-interactive (single in-guest script, one
sentinel), avoiding the bracketed-paste `[?2004h` regex fragility that
hangs interactive drivers.

Build & boot: the audited tree compiled clean and booted clean (no panic) —
none of CR-1..CR-10 manifested as a build/boot break or a nographic-path
crash. At audit time CR-1 needed the GUI-load 1+1 shape and CR-2 needed a
non-canonical `arch_prctl`; later Q0 fixes landed and the current CR status
is the compact Code Review section above.

Nographic (no audio/desktop dependency):

- **M2 getpid_ns: REPRODUCES.** 3 clean boots (npages=64): 1.69 / 1.94 /
  2.02 us vs plan's "1.65-1.94us typical" — sits at the top of the band.
  Still RED vs <1.5us goal, as documented.
- **M3 tlb_amp@1024pg: REPRODUCES.** 2.12 / 3.19 / 3.51 us vs plan's
  2.7-3.7us band. Nonzero vs goal 0, as documented.
- **Fork-safety: REPRODUCES / PASS.** forktest rc=1 (`fork claimed to
  work N times!` known exhaustion), clonetest rc=0, cowtest rc=0, no
  panic/fault/coredump.

KDE desktop (after host audio recovered — 2 runs, `reducer=desktop-interaction-latency`
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`):

- **M5 first_visible_ms: REPRODUCES / PASS.** 9617 / 10022 ms vs plan's
  8632-14623 band, both < 15000 goal.
- **M4 konsole_wait_ms: REPRODUCES / PASS on retry.** Run 2 = 1759 ms
  (< 2000 goal, matches documented 1958 ms). Run 1 hit a NEW, previously
  undocumented intermittent: konsole launched (fork+exec 30 ms, PTY fds
  seen) but its `/dev/shm/xv6-konsole-shell-ready` marker never appeared
  (45 s timeout) and konsole went zombie -> `kde-app-launch-latency-probe-output-fail`.
  Retry passed clean, so it is a flake, not a regression — but it is
  NOT in the plan's known-flake list and should be added to the R5-class
  triage set (konsole-shell-ready-timeout).
- **M8 idle host CPU: corroborates BORDERLINE-RED.** ~123% sampled during
  the interaction phase (10 s utime+stime delta, HZ=100). Not a pure-idle
  window (reducer interacts then shuts down), but consistent with the
  plan's own borderline 101-128% and above the <100% goal.
- **M9 Chromium visible: NOT DIRECTLY RE-TESTED** (needs the separate
  `chromium-video` reducer; the desktop-interaction runs did not exercise
  it). Plan already documents M9 FAIL on the default path.
- **M1 (3x15min replays), M6/M7 (GPU FPS): NOT ATTEMPTED** — expensive /
  GPU-FPS specific; queue as follow-ups.

Environmental note (was the whole-audit blocker earlier): when WSLg host
PulseAudio is DOWN, the guest `pactl` takes a fatal page fault inside
`libpulsecommon`/`libpulse` and the KDE smoke fails at
`kde-session-ready-crash` — ALL 7 GUI-gated rows (M1/M4/M5/M6/M7/M8/M9)
become unreproducible via that one cascade. Recovery is host-side
`wsl --shutdown` from Windows (not possible from inside WSL). The plan
lists audio only as a side "Known Issue," not as a gating prerequisite —
worth stating in the Scoreboard that those rows require a live audio path.
Harness gap: `KDE_SMOKE_TOLERATE_OPTIONAL_PACTL_READY_CRASH=1` does NOT
rescue that run — a generic fatal-page-fault arm matches the pactl
signature before the pactl-tolerant arm.

## Work Style — Time Budget and Batching (added 2026-07-03)

The 2026-07-02/03 overnight round spent the large majority of wall time
on VM boots and gate reruns and only a fraction on code work — including
~40 R8 diagnostic micro-iterations that each re-ran a full Chromium-video
VM cycle for parser-sized changes. These rules are binding:

1. CODE-FIRST: before any VM boot, finish the code-side work for the
   current lane slice — read the relevant source end-to-end, write the
   hypothesis into the lane section, and implement the full slice
   (fix + reducer + any needed probes, all gated/default-off). A VM run
   is for validating a completed slice or capturing evidence you proved
   you cannot get offline — not for exploring one variable at a time.
2. BATCH GATES: one gate battery may validate MULTIPLE changes when each
   change is independently revertable (separate commits or separate
   gates/flags). Run the battery once per batch: nographic
   fork/clone/cow, one KDE active-sample pass, one Chromium launch-only
   guard, M8 spot-check. Only bisect with extra VM runs if the batch
   battery fails.
3. OFFLINE FIRST for analysis tooling: post-evidence parsers, log
   classifiers, and histogram scripts MUST be developed and tested
   against the existing archives (66+ KDE smoke runs, 24+ profile dirs,
   yt-replay dirs) — never validated by booting a fresh VM. A parser
   change alone never justifies a VM run.
4. REUSE BOOTED VMS: a single booted KDE VM supports many probes (serial
   commands, debugfs log extraction from the live image, GDB
   attach-sample-detach rounds, /proc snapshots). Plan probe lists
   BEFORE booting; collect everything in one session. Budget guideline:
   <= 2 VM boots per lane slice (1 capture + 1 validation) plus the
   final batch battery.
5. EDGE-CASE EXCEPTION: timing-sensitive or state-dependent work (freeze
   reproduction, R5 flake hunting, PCID corruption, anything where the
   bug shape depends on boot-to-boot variance) may use as many runs as
   the evidence requires — that is what the archives-and-reruns
   machinery is for. Everything else follows rules 1-4.
6. Track the split: each lane status update should note roughly how much
   of the session went to implementation vs validation. If validation
   dominates two updates in a row, stop and re-plan the batch.
7. ORCHESTRATE, DO NOT DO: the handoff agent is an ORCHESTRATOR, not a
   worker. For every lane slice, decompose the work into specific,
   independently-scoped jobs and spawn a subagent per job (source
   read-through, reducer/probe implementation, offline archive parsing,
   gate execution, log triage). Run independent jobs concurrently (one
   message, multiple spawns). The orchestrator keeps its own context for
   planning, synthesis, cross-checking subagent results, and decisions —
   it does not burn context doing a job a subagent could do. A subagent
   returns a conclusion/diff, not a file dump. Give each subagent the
   exact plan section(s) it needs and the binding rules; never let a
   subagent flip a default or push.
8. COMMIT PERIODICALLY: at every natural checkpoint (a verified slice, a
   passing gate, before a risky change, and before ending a round) commit
   the whole repo AND every submodule, deepest-first, per the Commit
   Policy below. Do not accumulate a large uncommitted tree — that debt
   already cost a full re-diagnosis (Known Failure Mode 16). Never push
   without explicit user approval.

Recommended execution order for the next agent: the Q1-Q7 queue at the
top of this section. The 2026-07-02 order (R7a -> R7b -> R8 -> R5 -> M1
-> R3/R4 -> R7c -> R2 -> R6) is COMPLETE except where folded into Q1-Q7.

After R1-R5: continue P1 step 2 (fixed-cost cuts a-d) and step 3 (gated
default flip), then the remaining plan lanes in priority order.

## Known Failure Modes — Do Not Repeat

Every one of these burned real VM runs or produced a wrong conclusion in
past rounds. Check this list BEFORE declaring any gate failed or any
hypothesis confirmed.

1. Wrong session lane: `host_chromium=1` is consumed ONLY by the weston
   `desktop.c`; the KDE image ignores it. Launch Chromium in KDE via
   `/bin/wayland-chromium <url>` from the serial console with
   `XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root WAYLAND_DISPLAY=wayland-0`.
   `scripts/gpu/chromium-youtube-smoothness.expect` boots the weston lane
   and its inner 420s timeout kills QEMU before this image reaches
   Chromium — do not use it for KDE freeze repros.
2. Passive-visible-detector flake: KDE desktop-interaction runs WITHOUT
   `KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1` can time out on a visibly
   healthy desktop. Three runs were wasted and a gate was wrongly declared
   "blocked" this way. Always set it; before declaring any KDE gate
   failure, verify the failure is not a known flake class (this one, the
   `rcu_head_cache` double-free, known KWin startup #GP flakes).
3. Silently dropped kernel flags: expect/env quoting has eaten
   `QEMU_APPEND` tokens. Every run log must show the intended tokens in
   `x86 kernel cmdline` and, for PCID work, `vm_asid_init: max ASID`.
4. Serial-stream artifacts: the console truncates long input lines
   (~55-60 chars) and drops trailing `&`; output lines can split mid-token
   (a `max ASID = 4095` line once read as `409`). Keep guest commands
   short or stage script files in the image; verify anomalous grep
   results against the raw log before acting on them.
5. Single-sample misdiagnosis: a quiet serial log or one GDB snapshot is
   not a freeze. A boot phase was once misread as the freeze point.
   Require multiple GDB samples over time plus a guest liveness probe
   (short `echo ALIVE_<n>` that must execute, not merely echo) before
   classifying a hang.
6. Nographic gates do not validate VM/TLB/scheduler correctness: the PCID
   default flip passed every nographic gate and then corrupted KWin within
   two KDE runs. Any TLB/VM/scheduler behavior change needs GUI-scale
   gates, and audit conclusions need a failing-then-passing reducer — an
   audit that "found and fixed the hole" without a reducer is unproven.
7. Harness label vs app truth: top-level harness FAIL/timeout labels have
   disagreed with post-evidence status files repeatedly. Judge runs by
   post-evidence/status files and guest logs, not the wrapper label alone.
8. Stale runtime: QEMU keeps running the old kernel/image after a rebuild;
   `-kernel` boots and fs.img are independently stale-able. Check binary
   and image mtimes against the running process before trusting a capture.
9. Trace volume changes timing: per-event traces (every-wake, futex+IPC
   combos) pushed app readiness past probe timeouts. Timing runs use
   thresholded/role-scoped flags only.
10. Debugcon timestamps are TSC-calibrated per boot; do not compare them
    across boots as wall clock.
11. Transient starvation-probe lines are not freeze proof while guest
    liveness commands still execute. Treat them as leading evidence only;
    H-a/H-b/H-c classification requires the liveness failure plus timed GDB
    samples at that point.
12. Foreground Chromium launched from a long serial line can make the shell
    unavailable without proving a system freeze. The YouTube repro must use
    short commands, background Chromium intentionally, and prove independent
    liveness commands execute or fail after that launch.
13. Multi-line or >60-char commands sent to the guest serial console get
    truncated and can wedge bash in quote-continuation (prompt echoes but
    nothing executes — looks exactly like the P0 freeze). Recover by
    sending a lone closing quote. Build guest scripts with multiple short
    `echo '...' >> /file` appends, or debugfs-inject before boot.
14. The QEMU gdbstub can wedge into persistent "Cannot execute this
    command while the target is running" errors after a gdb process is
    killed mid-session (e.g. by `timeout`). Breakpoint+commands sampling
    works on the FIRST clean session; if it wedges, restart the VM rather
    than retrying connect cycles. Always `detach` before exit; prefer
    short-lived attach-sample-detach rounds for PC histograms.
15. Smoke-harness PASS does not prove app visibility: the
    desktop-interaction reducer's `chromium=-1` means Chromium was NEVER
    exercised in that run. A passing desktop gate says nothing about M9.
    Judge Chromium health only via the chromium-video reducer or a manual
    launch with guest-log evidence (`/host-gui-wayland-chromium.log` and
    `/chrome_debug.log` inside fs.img, readable live via
    `debugfs -R 'cat /host-gui-wayland-chromium.log' build-x86_64/fs.img`).
16. Uncommitted behavior-affecting diffs are landmines for the next agent:
    the `wayland-chromium-launcher.c` `WAYLAND_CHROMIUM_AUTO_GL_FLAGS`
    default flip sat uncommitted and undocumented while the user hit
    "Chromium window not visible", costing a full re-diagnosis. At handover
    time, every behavior-affecting uncommitted diff must be either
    reverted, committed with a lane reference, or listed in this plan with
    its justification.

## R8 — Chromium Window Not Visible (renderer-admission continuation)

COMPACTED 2026-07-04. The full 44-record diagnostic chain (renderer/zygote
admission, Mojo/fd-lifecycle parsers, bootstrap-peer traces, callsite/proc
probes, bundled-ANGLE and extension-override experiments, every archive path)
lives append-only in the history file
`docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`. Do NOT
re-run those probes; only the current conclusion and Q2 implementation ladder
matter.

Conclusion (settled):
- Renderer admission is EXONERATED — the window-not-visible symptom was never a
  renderer/zygote/KWin-surface-delivery bug. A real Mesa EGL attr-order bug was
  found and fixed (`ports/mesa/src`, committed `5e3f4bebe`); video now decodes
  and presents.
- The remaining DEFAULT-path blocker is Chromium's passthrough command decoder
  requiring an ANGLE/Chromium extension ladder that guest Mesa/virgl does not
  provide, checked in sequence: `GL_ANGLE_robust_client_memory` ->
  `GL_CHROMIUM_bind_generates_resource` -> `GL_CHROMIUM_copy_texture` ->
  `GL_ANGLE_client_arrays` -> `GL_ANGLE_webgl_compatibility` ->
  `GL_ANGLE_request_extension` -> `GL_KHR_debug`. The 2026-07-04 M7 run
  reconfirmed this class on the full video path (GPU proc fatal "missing
  GL_ANGLE_webgl_compatibility").
- `MESA_EXTENSION_OVERRIDE` (default-off knob
  `KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE`) only walks the fatal forward to
  the next missing token — advertising names without the semantics/entrypoints
  is not a fix. Keep it diagnostic-only.
- Default-off `WAYLAND_CHROMIUM_BUNDLED_GL=1` uses Chrome's preserved bundled
  ANGLE from an alternate hardlink root and PASSES the M9 launch-only gate via
  software fallback (`--use-gl=disabled`), but is NOT an M7 or hardware-GL fix
  (`presentedFPS~=31`, `dropPct~=26`). It is a visibility workaround/probe only;
  the default Chrome root still points at guest Mesa and fails the same class.

Q2 direction is resolved: do not skip GPU acceleration; implement the required
passthrough semantics in Mesa/virgl so default Chromium remains
hardware-accelerated. Bundled/software fallback is diagnostic/emergency only,
not the solution. Offline Mesa first slice is implemented and independently
reviewed/built, but not runtime-validated and not full conformance: it adds the
first-slice GLES2 Gallium advertisement/dispatch/query support for
`GL_ANGLE_robust_client_memory`, `GL_CHROMIUM_bind_generates_resource`,
`GL_ANGLE_client_arrays`, and `GL_ANGLE_request_extension`, with
`xv6_angle_passthrough.c`, GLES-only ANGLE dispatch, requestable-extension
empty-list semantics, `CLIENT_ARRAYS` false and bind-generates true getters,
and robust get/readpixels/texture-upload wrappers with error-output hygiene.
`GL_KHR_debug` already existed. Mesa commit
`a7eeaa2afca042d58d32de9314558e2639057f8c` now adds GLES-only
`GL_CHROMIUM_copy_texture` advertisement/dispatch plus shader/blit-based
copy/conversion paths for Chromium's offline reducer surface. Mesa commit
`fec4c3be56be` fixes robust uniform length results. Mesa commit
`fb27245039c5` adds context-specific `GL_ANGLE_webgl_compatibility` support via
`EGL_ANGLE_create_context_webgl_compatibility`, and the Wayland
`mesaanglepassthrough` reducer passes a staged-library host run rc 0 covering
normal-context absence, WebGL-context presence, and robust uniform length PASS.
Verification
passed with `git -C ports/mesa/src diff --check`,
`git -C ports diff --check`,
`cmake --build build-x86_64/ports --target port-mesa -j2`,
`cmake --build build-x86_64/ports --target port-wayland-mesacopytexture-install -j2`,
and staged-library host `mesacopytexture` rc 0 on non-software renderer
`D3D12 (Intel(R) UHD Graphics)`, plus
`cmake --build build-x86_64/ports --target port-wayland-mesaanglepassthrough-install -j2`
and staged-library host `mesaanglepassthrough` rc 0. No QEMU/VM runtime proof
yet.
`mesacopytexture` remains the focused offline proof surface: it keeps missing
extension/proc as rc 77, rejects software renderers, broadens
conversion/error/level/subcopy coverage, adds guarded external EGLImage/OES and
destination-format roundtrip fixtures, checks destination BASE/MAX_LEVEL
invariance for level-1 copies, expects rectangle targets to fail with
`GL_INVALID_ENUM` when unsupported, and aligns same-texture/same-level subcopy
with ANGLE's invalid-operation validation. Unsupported GL/EGL capabilities still
SKIP instead of false-failing.
No QEMU/VM runtime proof yet. Next: refresh the rootfs, verify the actual image
library choice, and run the batched runtime Chromium gate unless another
offline gap is found, especially around overlay Mesa copies under
`rootfs-generated-overlays/kde-runtime`. M9 tracks the default Chromium proof;
M7 stays blocked on this AND on the P2-step-3 zero-copy present path (see P3 /
M7 findings).

KDE/Wayland liveness update (2026-07-04): the current evidence splits two
blockers. First, the Q2 archive
`20260704T121552Z-q2-chromium-launch-only-session-ready-pactl-gp/` failed in
KDE readiness because `pactl` took a user-space `#GP` at
`rip=0x7fffff694449` with no CR2 and a text-shaped non-canonical `rdi`
(`common-1`). KWin/plasmashell/Xwayland still reached PASS immediately after
that crash, so `pactl` is not the Plasma/session-death cause. It is now
removed from the readiness-critical path by default: the session child still
starts PipeWire/wireplumber/pipewire-pulse and records
`/kde-audio-status.log`, but the old `pactl list short sinks/sources` checks
are skipped unless `kde_pactl_probe=1` or `KDE_PACTL_READINESS_PROBE=1` is set.
If re-enabled, treat the crash as a direct libpulse/ABI diagnostic, not as a
KDE readiness gate failure.

Second, the Plasma/Wayland teardown remains a real, separate blocker. The
older no-Chromium reducer archive
`20260704T121419Z-kde-wayland-liveness-late-roundtrip/` proved the failure
class: initial KDE readiness, the first hard liveness gate, and the standalone
Wayland seat probe all passed; five seconds later `/kslc.sh` failed roles with
`kwin=0 plasmashell=0 xwayland=0`, no `wayland-*` socket, and rc 2. The plasma
child wrapper captured
`plasmashell exited status=1 lifetime_ms=7589 immediate=1` after KCrash
recursion, `Bad file descriptor`, and downstream `Failed to create wl_display
(Connection refused)`. KWin still logged the default accelerated renderer
`virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))` / Mesa 26.2, with no KWin
fatal marker in that archive.

Post-fix verification: after `rootfs-refresh`, the no-Chromium liveness
reducer passed in
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T122805Z-kde-wayland-liveness-pactl-skipped-pass/`
with `status_code=0`, `kwin=1 plasmashell=1 xwayland=1`, a successful
Wayland roundtrip, and `kde_audio_status ... pulse_socket=1` followed by
`phase=pactl status=SKIPPED`. That shows `pactl` no longer blocks readiness
and the liveness reducer can pass with default virgl/Mesa acceleration.

The required single Q2 retry then ran with all bundled/software GL overrides
unset and is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T123120Z-q2-chromium-launch-only-pactl-skipped-wayland-refused/`.
It moved past the old `pactl` blocker and past pre-Chromium liveness
(`kwin=1 plasmashell=1 xwayland=1`, Wayland roundtrip PASS), but failed later
as `KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-launch-evidence-FAIL`
(`status_code=8`). The launch log has real Chromium evidence
(`launcher_log=1 launcher_marker=1 launcher_child_exec=1 launcher_url=1`) and
then Chromium exits 1 after `Failed to connect to Wayland display:
Connection refused` / `Failed to initialize Wayland platform`. The plasma
child log from the same run shows the remaining session blocker directly:
`KCrash: Application Name = plasmashell`, `The Wayland connection experienced
a fatal error: Bad file descriptor`, and
`plasmashell exited status=1 lifetime_ms=10132 immediate=0`. Post evidence saw
the browser plus crashpad only (`browser_seen=1 renderer_seen=0 gpu_seen=0`)
and no Chromium user fault. Host EGL/GBM GLES3 smoke still passed on
`GALLIUM_DRIVER=virgl`, `MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu`; KWin and
the host smoke both report
`virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))` with Mesa
`26.2.0-devel`. One kernel anomaly remains in that Q2 run:
`slab_alloc: repairing corrupt freelist cache='rcu_head_cache' ...`; correlate
any recurrence with the bounded-open R3 owner-history diagnostics.

Follow-up diagnostics on 2026-07-04 keep the classification on the session
side, not Chromium. The harness now preserves `/kde-plasmashell-crash-capture.log`,
per-label liveness logs, optional `/core.PID` artifacts, a delayed
pre-Chromium liveness gate, and a no-Chromium
`kde-wayland-client-liveness` reducer. Build refresh passed after those changes.
The first synthetic-client attempt exposed only the known long serial-command
truncation hazard and is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T130109Z-kde-wayland-client-liveness-start-timeout-harness-command/`.
After moving client control into `/kwcl.sh`, the same reducer failed before
launching the synthetic Wayland client, at delayed liveness:
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T130310Z-kde-wayland-client-liveness-delayed-plasmashell-kcrash/`.
The vanilla no-Chromium liveness reducer also now catches the same class at its
late roundtrip:
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T130425Z-kde-wayland-liveness-late-plasmashell-kcrash/`.

Crash evidence from those runs: initial liveness and the standalone Wayland seat
probe pass, then the delayed/late liveness returns rc 2 after KCrash recursion 2
for `plasmashell`, fatal Wayland `Bad file descriptor`, and wrapper exit records
around 7.2-7.6s lifetime. The plasma log shows notification/system-tray QML
warnings immediately before the crash, including StatusNotifierItem registration
and a system-tray `compactRepresentationItem` null read. Passive crash capture
records `config capture=1`, the plasmashell pid, live `/proc` snapshots through
the failure, render-node fds such as `/dev/dri/renderD128`, and `wait_done`
with raw exit status 256. `KDE_DEBUG=1` did not bypass KCrash in the targeted
disable-KCrash run, and core capture remains opt-in. Do not spend another Q2
proof run until this no-Chromium delayed plasmashell crash is fixed or a stronger
stack/core path is enabled.

2026-07-04 follow-up: the crash capture path now programs the intended
plasmashell debug environment when requested (`KDE_DEBUG=1`,
`KCRASH_DUMP_ONLY=1`, `KCRASH_AUTO_RESTARTED=1`), logs the `prctl`/`setrlimit`
result, dumps the child environment, and the harness preserves KCrash metadata
INI files when present. A targeted core run with
`KDE_SMOKE_PLASMASHELL_DISABLE_KCRASH=1 KDE_SMOKE_PLASMASHELL_CORE=1` still did
not yield a `/core.PID`; it is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T132515Z-kde-wayland-liveness-kcrash-core-diagnostic/`.
It did, however, force the fault out of DrKonqi and captured a kernel-side
plasmashell exception dump: `cr2=0x8`, `rip=0x7ffffe51496f` in
`/usr/lib/x86_64-linux-gnu/libQt5WaylandClient.so.5`, with
`org.kde.plasma.private.systemtray.so`, `org.kde.plasma.systemtray.so`, and
`org.kde.plasma.notifications.so` loaded in the VMA set. No guest or host core
was present after the run.

The narrow component isolation points at Plasma's StatusNotifier/system-tray
startup path. A no-Chromium SNI-off control
(`QEMU_APPEND_EXTRA='kde_network_status_sni=0'`) passed once at
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T132233Z-kde-wayland-liveness-sni-off-pass/`,
but a rebuilt default with the artificial network SNI provider disabled still
hit the same fatal plasmashell KCrash after the system-tray
`compactRepresentationItem` null read:
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T132953Z-kde-wayland-liveness-sni-default-off-kcrash-fail/`.
That means the artificial SNI item is a destabilizer, not the complete root
cause. The session now disables the artificial network SNI provider by default
and keeps it opt-in with `kde_network_status_sni=1`.

A reversible applet isolation knob then omitted only
`org.kde.plasma.systemtray` from
`plasma-org.kde.plasma.desktop-appletsrc`. With
`QEMU_APPEND_EXTRA='kde_plasma_systemtray=0'`, the no-Chromium liveness reducer
passed at
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T134223Z-kde-wayland-liveness-systemtray-off-pass/`:
initial and late liveness returned rc 0, KWin kept `wayland-0` alive, and GPU
evidence stayed on the default virgl/Mesa path
(`OpenGL renderer string: virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`,
`GALLIUM_DRIVER=virgl`, `MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu`). That run
still logged a recoverable plasmashell KCrash and ended with two plasmashell
processes, so the system tray applet should be treated as the fatal-liveness
trigger rather than proof that all plasmashell crash causes are gone. The
session config now omits the system tray by default and keeps it opt-in with
`kde_plasma_systemtray=1`.

Two no-override default reruns after making tray/SNI opt-in did not reproduce
the fatal system-tray KCrash, but also did not produce a clean default pass:
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T133810Z-kde-wayland-liveness-default-tray-off-roundtrip-race/`
failed because the first liveness `wl_display_sync` timed out while KWin was
running and roles were already PASS, and
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T133950Z-kde-wayland-liveness-default-tray-off-xkbcomp-gp/`
failed before liveness on an existing-class `xkbcomp` #GP. Treat those as
separate startup-race/guest-userspace blockers. Q2 remains out of scope until
a default or explicitly documented no-Chromium liveness pass is used as the
gate for the next Chromium run.

Q2 post-system-tray-isolation launch-only gate 2026-07-04 ran exactly once on
top-level `81f9e5b` with all bundled/software GL knobs unset:
`env -u KDE_SMOKE_CHROMIUM_BUNDLED_GL -u KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE -u MESA_EXTENSION_OVERRIDE -u LIBGL_ALWAYS_SOFTWARE -u GALLIUM_DRIVER -u MESA_LOADER_DRIVER_OVERRIDE -u LIBGL_ALWAYS_INDIRECT QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=chromium-video KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1 scripts/gpu/kde-plasma-desktop-smoke.expect`.
Archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T134822Z-q2-chromium-launch-only-systemtray-isolated-launch-evidence-fail/`.
Result: FAIL `status_code=8`
`KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-launch-evidence-FAIL`.
Preflight, initial liveness, and delayed pre-Chromium liveness all passed with
KWin/plasmashell/Xwayland alive and Wayland roundtrips rc 0; no KCrash,
Wayland Bad FD, KWin `#PF`, PANIC, fatal page fault, or loader-symbol
regression appeared. Default acceleration stayed intact: KWin and host EGL
reported Mesa/virgl on `D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)`, fbstat
reported `backend_opengl_submit=1` / `opengl_submit_credit=1`, and Chromium
process env used `GALLIUM_DRIVER=virgl`, `MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu`,
`LIBGL_DRIVERS_PATH=/lib/dri:/usr/lib/x86_64-linux-gnu/dri`, with
`MESA_EXTENSION_OVERRIDE` and bundled GL unset. Chromium launcher evidence is
real (`launcher_log=1 launcher_marker=1 launcher_child_exec=1 launcher_url=1`):
the browser process sampled through the 18s window and later became a zombie,
but Chrome failed Wayland platform setup with `No wl_shm object` followed by
`Failed to initialize Wayland platform`. Post evidence reports
`browser_seen=1 renderer_seen=0 gpu_seen=0`, no DRM/render fd, no Wayland
surface/protocol evidence, and no Chromium ANGLE/WebGL/`GL_CHROMIUM_copy_texture`
or robust-client clue because Chromium did not reach that layer. The next Q2
blocker is therefore Wayland global advertisement/delivery for `wl_shm`, not
the former system-tray liveness crash or the old missing-extension class.

## R7 — Desktop Responsiveness Composite (new lane)

Why this lane exists: after the P0 idle-pull fix the user still reported a
slow desktop. A 2026-07-02 profiling session (GDB PC-sampling of all vCPUs
+ guest `/proc/<pid>/stat` window diffs + kstats windows; evidence in
`build-x86_64/desktop-bottleneck-profile/20260702T2310Z/`) measured a
three-layer composite. Headline: QEMU burns 345-512% host CPU with a
visually idle desktop (M8), and a single Konsole launch window shows 363k
ioctls + 307k polls + 700k copyin/copyout in 4.5s (idle baseline ~920
ioctl/s).

### R7a — O(1) spinlock rq-assertion (kernel, ~11-15% of all cycles)

Finding: `__spinlock_assert_not_rq_object()` in
`kernel/kernel/lock/spinlock.c` calls `rq_identify_object()`
(`kernel/kernel/proc/rq.c` ~L176) — a linear scan over
`cpu_possible_count() x PRIORITY_MAINLEVELS` = 6 x 64 = 384 pointer
compares — on EVERY `spin_acquire`, `spin_release`, `spin_trylock`, AND
`spin_holding` (and `spin_holding` is itself called inside `spin_acquire`,
so an acquire/release pair pays the scan 3-4x). Pre-existing since the
April repo lift; PC samples put 11-15% of all cycles in this scan.

Fix direction (small, mechanical):
1. In `rq_global_init()` (`proc/rq.c` ~L236) the rq_percpu array is ONE
   contiguous `kvmalloc` of `sizeof(struct rq_percpu) x
   cpu_possible_count()` stored in `rq_percpu_data`. Record
   `[rq_percpu_data, rq_percpu_data + size)` once. Individual `struct rq`
   objects are registered via `rq_register` — audit where those live
   (`__eevdf_rqs[cls][cpu]` static arrays in `sched_eevdf.c` and
   equivalents in other sched classes); record those ranges too (they are
   per-class contiguous arrays, so a handful of range pairs total).
2. Replace the scan in `__spinlock_assert_not_rq_object()` with: pointer
   outside all recorded ranges -> return immediately (the 99.999% case);
   inside a range -> keep the precise scan for the error report.
3. Alternative acceptable form: compile-time or cmdline-gated
   (`spinlock_rq_assert=0` default) if range bookkeeping is deemed risky.
   Do NOT silently delete the assertion — it exists to catch rq objects
   being passed to raw spin ops.
Gates: kernel build; `git -C kernel diff --check`; nographic boot +
forktest/clonetest/cowtest; KDE desktop-interaction pass
(`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`) with M4/M5 compared A/B against
a same-session control run; a 15-round GDB PC-sample histogram
(attach-sample-detach, resolve via `build-x86_64/kernel/kernel.elf`, NOT
xv6.bin which is a bzImage) showing `rq_identify_object` gone from the
top-10; M8 re-measured.

2026-07-02 R7a status: implemented the O(1) default path in `rq.c` as an
exact `struct rq *` registry populated by `rq_register()` plus a cheap
address-envelope reject. This matches the current layout: `rq_percpu_data`
contains the per-CPU rq lock and rq pointers, not embedded `struct rq`
objects, so the earlier range-only sketch would have misclassified rq locks.
The old linear scan is still available only with the default-off diagnostic
boot flag `rq_identify_linear_scan=1` for same-binary control runs. Proofs:
kernel build and diff checks passed; nographic forktest/clonetest/cowtest
passed at
`build-x86_64/desktop-bottleneck-profile/20260702T234500Z-r7a-final-nographic-gate/`;
default O(1) KDE desktop interaction passed at
`build-x86_64/kde-plasma-desktop-smoke-history/20260702T235020Z-r7a-o1-default-kde-pass/`
(`first_visible_ms=13667`, `konsole_wait_ms=2089`); Chromium launch-only
M9 improved from the scoreboard failure to PASS at
`build-x86_64/kde-plasma-desktop-smoke-history/20260702T235218Z-r7a-o1-chromium-launch-only-pass/`
(CAUTION 2026-07-03: that single PASS did not hold — every subsequent
launch-only run the same night, including the launcher-stock-default,
R7b, and R7c guards, reclassified as the known chrome-crash-regression;
treat the 235218Z result as a flake/classification artifact, and treat
M9 as FAILING on the default path until the Q2 GL decision lands);
the 15-round GDB PC histogram has 90 samples and no `rq_identify_object`
hits at
`build-x86_64/desktop-bottleneck-profile/20260702T235428Z-r7a-o1-gdb-pc-histogram/`.
The `rq_identify_linear_scan=1` control hit the known R5 KWin #GP class
before yielding M4/M5 metrics, archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260702T234710Z-r7a-linear-scan-control-kwin-gp-fail/`.
M8 was remeasured after a quiet KDE-ready window and remains high
(`252%`, `267%`, `279%`) at
`build-x86_64/desktop-bottleneck-profile/20260702T235608Z-r7a-o1-m8-idle-cpu/`,
so R7b/R7c remain necessary for the idle-CPU milestone.

2026-07-03 R7a current-tree revalidation: audited the live dirty kernel and
confirmed the recorded O(1) assertion path is still present: `rq_register()`
populates an exact `struct rq *` lookup table plus an address-envelope reject,
and `rq_identify_linear_scan=1` remains the default-off same-binary control
path. Fresh current-state checks passed after later R7/R8 work:
`cmake --build build-x86_64 --target kernel -j2`,
`cmake --build build-x86_64 --target kernel-sparse -j2`
(`failures=0, errors=0`, only the existing lock-context warnings), and
`git -C kernel diff --check`. A fresh nographic fork-safety gate is archived at
`build-x86_64/desktop-bottleneck-profile/20260703T111753Z-r7a-current-nographic-gate/`:
boot cmdline verified `desktop=0 video=1280x800 netsurf=0`, `forktest`
returned the expected table-exhaustion `rc=1`, and `clonetest`/`cowtest`
returned `rc=0`. This does not reopen the already-closed R7a desktop/GDB/M8
evidence; it preserves the R7a gate on the current worktree before moving on.

### R7b — Qt/GLib FIONREAD event-loop spinners (userspace-visible, kernel
semantics suspected)

Finding: with the desktop visually idle, `kactivitymanager` spins
persistently at ~90% of a core; after any app-launch probe, leftover
`kate`/`kwrite` processes spin at ~90% CPU forever; during launch windows
`kded5` and `plasmashell` each burn ~6x Konsole's own CPU (20k vs 3.2k
ticks in 4.5s). Spin shape (PC-sampled to libc `poll+0x4d` and
`ioctl+0x3d`; GDB breakpoint histogram on `vfs_ioctl`): a tight
`poll -> ioctl(FIONREAD=0x541b, on AF_UNIX sockets, f_kind=CUSTOM) ->
write` cycle at ~80k ioctl/s system-wide. 576/634 sampled ioctls were
FIONREAD. These spinners multiply R7a/R7c because every iteration enters
lock-heavy kernel paths.

Already exonerated in isolation (do not re-test blindly): peer-closed
socketpair gives Linux-consistent `POLLIN|POLLHUP` + `FIONREAD=0` +
`recv()=EOF`; bare empty eventfd does not poll ready.

Root-cause direction (in order):
1. Identify the exact fd: reboot the GDB-stub VM, reproduce (boot KDE, run
   `/bin/kde-app-launch-probe`, wait for leftover kate/kwrite spin), then
   on a FRESH gdb session set the breakpoint on `unix_file_ioctl` and log
   `cmd`, `sk->proc_ino`, `sk->type`, `sk->peer`, peer ring
   `nread/nwrite`, `shutdown_flags` (a probe script exists at
   `/tmp/fionread-probe*.gdb` from the session; rebuild it if gone).
   Cross-reference `proc_ino` against `ls /proc/<pid>/fd` readlinks
   (python3 `os.readlink` works in-guest) to find both endpoints.
2. Compare the triple (poll revents, FIONREAD result, recv/read result)
   for that exact socket state against Linux. Prime suspects, in order:
   a. POLLOUT always-ready on a peer whose ring is full or half-closed
      (the `write` in the spin cycle suggests a write-watch GSource that
      never becomes unwritable or never succeeds);
   b. FIONREAD returning stale/nonzero while `recv` returns EAGAIN (Qt's
      `bytesAvailable()` loop);
   c. POLLIN asserted for a condition `read` does not consume (e.g.
      SIGIO/OOB/credentials edge, or readiness derived from the wrong
      ring in `unix_poll`).
   Read `unix_poll`/`unix_file_ioctl`/ring accounting in
   `kernel/kernel/vfs/unix_socket.c` side by side with the observed state.
3. Write the failing-then-passing reducer: a C or python guest program
   that reproduces the exact socket state and asserts the Linux triple.
   Fix the kernel semantics; the reducer plus a KDE run where
   kactivitymanager sits at ~0% CPU and no leftover kate/kwrite spinners
   remain (check with two `/proc/[0-9]*/stat` snapshots 10s apart) is the
   gate. M8 must drop substantially (spinners were worth ~3 cores).
Note: also check why kate/kwrite never exit after the probe (may be the
same spin blocking their teardown, or a separate orphaning bug — classify
while there).

2026-07-03 R7b status: first ABI fix landed, but the full idle-CPU gate is
not yet closed. Fresh GDB evidence corrected the initial AF_UNIX-only read:
the dominant idle loop also hits inotify fds. In the pre-fix KDE boot
`build-x86_64/desktop-bottleneck-profile/20260703T000338Z-r7b-fionread-gdb/`,
hot syscall sampling showed `kactivitymanage`, `kded5`, and `plasmashell`
cycling through `poll` and `ioctl(FIONREAD=0x541b)` on inotify descriptors.
The focused return probe captured queued inotify events but the kernel
returned `-ENOTTY`: `kded5` had 35 events / 1320 bytes, `kactivitymanage`
had 513 events / 22428 bytes, and `plasmashell` had 1 event / 40 bytes
queued. A Linux host control for the same empty/ready/drained sequence
returned `FIONREAD=0`, then the exact queued event byte count, then `0`
after read.

Fix: `kernel/kernel/vfs/vfs_syscall.c` now gives inotify files an
`.ioctl` handler for `FIONREAD` (`0x541B`) that returns the queued
`struct linux_inotify_event` record byte count, capped at `INT_MAX`, while
leaving other inotify ioctls at `-ENOTTY`. `linuxsyscallabitest` now has a
focused `inotify-fionread` reducer and the broader VFS-number test covers
empty, ready, read, and drained inotify `FIONREAD` states.

Proofs so far: `git diff --check`, `git -C kernel diff --check`, and
`git -C user diff --check` passed; the `kernel`, `user`, and
`rootfs-refresh` CMake targets passed with only existing warning noise. The
refreshed-image focused reducer passed at
`build-x86_64/desktop-bottleneck-profile/20260703T005521Z-r7b-inotify-fionread-refresh/`
(`R7B-INOTIFY-FIONREAD-ABI-PASS`). KDE desktop-interaction first hit the
known R5 KWin #GP startup class, archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T005720Z-r7b-inotify-kde-kwin-gp-rerun-needed/`;
the active-sample rerun passed at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T005930Z-r7b-inotify-kde-active-sample-pass/`
with `first_visible_ms=10453`, `konsole_wait_ms=1290`, and direct-launch
`elapsed_ms=2356`. Chromium launch-only first hit the same KWin startup
class, archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T010046Z-r7b-inotify-chromium-launch-kwin-gp-rerun-needed/`;
the rerun exercised Chromium and its launch sampler passed, but final
post-evidence remained the existing R8 `chrome-crash-regression` class
(`EGL_BAD_ATTRIBUTE` GPU context failure, no renderer), archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T010245Z-r7b-inotify-chromium-launch-only-crash-regression/`.
Post-fix quiet-KDE confirmation is now in hand. The first custom proof
run, `build-x86_64/desktop-bottleneck-profile/20260703T010829Z-r7b-postfix-m8-spinners/`,
booted the same cmdline as the smoke pass, reached KDE readiness, and
collected immediate quiet-desktop host CPU samples of `121%`, `113%`,
and `106%` after the 30s settle; the post-app samples were `113%`,
`110%`, and `110%`. That showed the large pre-fix spinner was gone but
also that the first minute of desktop settle was still noisy.

The closure run,
`build-x86_64/desktop-bottleneck-profile/20260703T012042Z-r7b-postfix-late-settle-m8-spinners/`,
used the same boot contract, waited 60s after KDE readiness before the
idle samples, then waited 20s after `/bin/kde-app-launch-probe` before
the post-app `/proc` window. It passed with M8 below target:
`94.0%`, `89.9%`, `86.0%` idle and `88.2%`, `87.1%`, `86.2%`
post-app. `procstat-summary.txt` shows no persistent R7b spinners:
idle deltas over 10s were `kactivitymanage=8`, `kded5=14`,
`plasmashell=18` ticks; post-app deltas over 10s were
`kactivitymanage=7`, `kate=60`, and `kwrite=16` ticks
(`HZ=1000`, so each is far below a 90% CPU spin). R7b's inotify
`FIONREAD` ABI fix is therefore validated and closed; remaining desktop
CPU/performance work continues in R7c and the separate R8 Chromium
renderer-admission lane.

### R7c — `__sched_timer` global-lock decontention (kernel, ~20-27% of
cycles when loaded)

Finding: the top contended lock in PC samples is `__sched_timer+64`
(`spin_acquire` wait-loop lines 53/61 dominate; sampled lk value confirms).
All 6 CPUs at 1000Hz `timer_tick` plus `sched_timer_set` on every context
switch serialize on one global lock. This is the same contention already
named in the P0 freeze signature (H-c family), so fix it inside the P0
lane rules (scheduler changes need GUI-scale gates, Known Failure Mode 6).

Fix direction: shard the sched timer — per-CPU `timer_root` with per-CPU
locks (each CPU arms/expires its own timers; cross-CPU timer adds go
through the target CPU's root), or split tick-advance from timer-wheel
mutation so `timer_tick` takes the lock only when timers are actually due
(`next_tick` early-out is already read before the lock — verify and extend
the lock-free window). Smallest-change-first per P0 step 4 policy.
Gates: same battery as R7a plus the P0 probe (`sched_starve_probe=1`
silent through a KDE pass) and one wakestorm `guihd` run; then M1 replays
since this touches the freeze lane directly.

2026-07-03 R7c first-slice status: implemented the smallest-change
`timer_tick()` decontention in the x86 timer root
(`kernel/arch/x86_64/timer/timer.c`): non-expiry ticks now advance
`current_tick` with an atomic compare/exchange and return without taking the
global timer lock; the locked path remains responsible for timer expiry and
list mutation, with `next_tick`/`current_tick` published through atomics.
This avoids serializing all six LAPIC ticks on `__sched_timer` when no
scheduler timer is due, without changing timer insertion/removal semantics.
Validation so far: `git diff --check`, `git -C kernel diff --check`,
`git -C user diff --check`, `cmake --build build-x86_64 --target kernel
-j2`, and `cmake --build build-x86_64 --target kernel-sparse -j2` passed
(Sparse still reports the repo's existing lock-context warning set, but
`failures=0, errors=0`). The nographic fork-safety gate passed at
`build-x86_64/desktop-bottleneck-profile/20260703T034450Z-r7c-sched-timer-fastpath-nographic-gate/`
with `sched_starve_probe=1` on the boot cmdline: `forktest` returned the
expected table-exhaustion `rc=1`, `clonetest` and `cowtest` returned `rc=0`,
and no starvation-probe or crash markers were found. KDE desktop interaction
with active sampling and `sched_starve_probe=1` passed and stayed probe-silent
at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T035604Z-r7c-sched-timer-fastpath-kde-active-sample-pass/`
(`first_visible_ms=10935`, `konsole_wait_ms=1605`, no panic/fault/double-free
markers). The first wakestorm `guihd` attempt was only a harness prompt-sync
miss before the reducer started, archived at
`build-x86_64/desktop-bottleneck-profile/20260703T034558Z-r7c-sched-timer-fastpath-wakestorm-guihd/`.
The rerun started and completed the reducer under the accepted
`status=SIGNAL rc=2` contract at
`build-x86_64/desktop-bottleneck-profile/20260703T034857Z-r7c-sched-timer-fastpath-wakestorm-guihd-rerun/`;
it had `completed_rounds=60`, `unfinished_threads=0`, no crash markers, and
clean QEMU cleanup. It did emit one starvation-probe snapshot
(`probe_snapshots_delta=1`), but `idle_needs_resched_delta=0` and the snapshot
showed `sched_timer_owner=-1 sched_timer_hold_ms=0`, so it is not the P0
freeze signature. The required Chromium launch-only regression guard did not
improve M9 but also did not introduce a new failure class: archive
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T040048Z-r7c-sched-timer-fastpath-chromium-launch-only-existing-crash-regression/`
has launch evidence PASS, sampler PASS, and the existing R8
`chrome-crash-regression` / `EGL_BAD_ATTRIBUTE` GPU context loop with no
renderer. M8 is the blocker: two late-settle runs completed without crash
markers, but host `ps -o pcpu` stayed above the current scoreboard window.
Archives
`build-x86_64/desktop-bottleneck-profile/20260703T040116Z-r7c-sched-timer-fastpath-late-settle-m8-spinners/`
and
`build-x86_64/desktop-bottleneck-profile/20260703T040527Z-r7c-sched-timer-fastpath-late-settle-m8-spinners-rerun/`
reported idle samples `122%/118%/115%` then `131%/126%/122%`, and post-app
samples `125%/126%/127%` then `132%/133%/133%`. Their procstat summaries do
not show the old R7b 90%-CPU spinner pattern (`kactivitymanage` idle deltas
35/36 ticks over 10s, `kded5` 50/44, `plasmashell` 38/42), so the elevated
host CPU needs a fresh PC histogram or host-side attribution rather than an
R7b reopen. A first idle-only GDB PC histogram against the R7c first slice is
archived at
`build-x86_64/desktop-bottleneck-profile/20260703T043034Z-r7c-idle-gdb-pc-histogram-minimal/`.
The harness exited cleanly (`harness-rc=0`,
`status=PASS reason=r7c-idle-gdb-window-complete`), with 15 successful
attach/sample/detach rounds and 90 CPU samples; the failure-marker scan only
matched the literal `timeout` wrapper command. Host CPU in that GDB window was
below the M8 target (`90.1%/89.3%/88.6%` before sampling and
`87.8%/87.3%/86.7%` after sampling), so this run did not reproduce the two
earlier high-M8 late-settle windows. Corrected histogram parsing (extract the
CPU symbol between `symbol=` and `lock_symbol=`; the first parser accidentally
counted lock targets as CPU symbols) shows 65/90 samples in
`arch_idle_halt` (`start_kernel + 1474`, `x86.h:207`), 12/90 in the
`spin_acquire` wait loop (`spinlock.c:53/61`) on `__sched_timer+64`, 2/90 in
`timer_add` (`timer.c:823`) on the same timer root, and 17/90 total samples
whose lock pointer decoded inside `__sched_timer`. The contention is now
bursty rather than always-hot (`sched_timer_lock_samples`: round 03=1, 04=3,
05=3, 11=5, 13=1, 14=4), but it has not fallen out of the hot set. The most
recent owner-aware control is archived at
`build-x86_64/desktop-bottleneck-profile/20260703T044617Z-r7c-owner-aware-gdb-histogram/`.
That harness also exited cleanly
(`status=PASS reason=r7c-owner-gdb-window-complete`) and, with the same idle
KDE shape, again stayed below M8: host CPU was
`79.4%/79.4%/78.9%` before the GDB loop and `77.7%/77.4%/77.1%` after it.
The owner-aware histogram has 40 attach/sample/detach rounds and 240 CPU
samples: 191/240 in `arch_idle_halt`, 17/240 in `spin_acquire`, 3/240 in
`timer_add`, and 23/240 samples whose lock argument decoded inside
`__sched_timer`. The lock owner pointer needed the same trampoline-alias
translation as `cpuid_from_tp()` (`0xfffffffffffee000 + cpu*0xc0`), after
which owners were spread across CPUs and mostly caught in LAPIC/timer
interrupt edges or insertion/wakeup code (`lapic_write` 7 locked-owner
rounds, `timer_add` 3, `scheduler_wakeup` 1, `lapic_read` 1). In the
corrected add-on rounds (`26..40`) every sampled locked owner had
`hold_jiffies=0`, so this evidence controls the remaining bursts as
sub-jiffy expiry/insertion contention, not a multi-jiffy stuck owner or a
stable high-M8 idle condition. Treat this as a host-side control for the two
earlier 120-130% M8 samples; do not do a risky callback-outside-lock move or
timer sharding unless high M8 reproduces with a matched owner sample or a
long-hold owner appears. Do not close or commit R7c yet: the P0 M1 replay
battery is still required because this touches the freeze lane.

2026-07-03 Q1 host-thread attribution update: high/borderline M8 reproduced
after the review fixes and now has host-side owner evidence at
`build-x86_64/desktop-bottleneck-profile/20260703T192636Z-q1-review-fixes-host-thread-m8-attribution/`
(`status=PASS reason=q1-host-thread-m8-attribution-collected`). The two idle
10s windows measured 106.3% and 101.2% one-CPU equivalent. All nontrivial CPU
ticks were on the six vCPU threads, with sampled endpoints mostly in
`kvm_vcpu_block` or runnable; QEMU main/GTK/GLib/D-Bus/dconf/worker/virgl
helper threads were 0%. Combined with the Q1 GDB owner run showing most guest
PCs in `arch_idle_halt()`, this tracks the remaining M8 debt to vCPU/KVM idle
wake cadence or sub-jiffy timer/interrupt churn. It is a follow-up lane, not a
Q1 commit blocker, as long as Q1 commits keep the M8 caveat explicit.

2026-07-03 R7c/P0 M1 replay 1: archived at
`build-x86_64/yt-mainpage-freeze-repro/20260703T045729Z-r7c-m1-replay1-watch-input/`.
The first launch attempt in that archive failed before guest boot because
PulseAudio was unavailable on the host (`attempt1-pulseaudio-fail-*`); it is
not guest evidence. The rerun used the same step3b watch-page input harness
with `QEMU_AUDIO=none QEMU_AUDIO_BACKEND=none`, verified
`sched_starve_probe=1` on the booted kernel cmdline, launched Chromium to
the watch URL, injected monitor input, and exited cleanly (`harness-rc=0`,
`STATUS.txt` = `PASS/live`). All 15 liveness probes executed
(`ALIVE_0` through `ALIVE_14`; `liveness-proof.txt` has 30 send/result
lines), `failure-markers.txt` is empty, `post-qemu-processes-check.txt` is
empty, the scratch image was removed, and top/kernel/user `git diff --check`
all returned 0. This is a responsiveness pass, but not a probe-silent pass:
the run emitted 7 `sched_starve_probe` snapshots / 56 lines while the shell
continued to respond. The probe summary found no nonzero `rq_hold_ms`,
nonzero `sched_timer_hold_ms`, or stale timer/IPI-age match, so the snapshots
look like live load/backlog telemetry rather than the historical hard-freeze
signature. Monitor screendumps produced the known `Error: no surface`
limitation and no PPM files, so pass/fail is based on serial liveness and
marker scans. Count this as 1/3 responsive R7c/P0 M1 replays; at the time
two more 15-minute replays and the R5 classification gate remained.

2026-07-03 R7c/P0 M1 replay 2: first attempt archived at
`build-x86_64/yt-mainpage-freeze-repro/20260703T052430Z-r7c-m1-replay2-watch-input/`
is useful live/no-freeze telemetry but does NOT count as clean M1 evidence:
KWin hit the known R5 startup crash/retry (`kwin_wayland` #GP and
`status=11`). It still executed all `ALIVE_0` through `ALIVE_14` probes,
verified `sched_starve_probe=1`, exited with `harness-rc=0`, cleaned QEMU
and the scratch image, and passed top/kernel/user `git diff --check`. Its
3 starvation-probe snapshots / 24 lines had no nonzero rq/sched-timer hold
or stale timer/IPI-age match. Replacement replay 2 rerun 1 is archived at
`build-x86_64/yt-mainpage-freeze-repro/20260703T054444Z-r7c-m1-replay2-rerun1-watch-input/`
and counts as 2/3 responsive R7c/P0 M1 replays: no KWin/R5/crash markers,
all 15 ALIVE probes executed, boot cmdline verified `sched_starve_probe=1`,
QEMU and scratch cleanup were clean, kernel-source-newer check was empty,
and top/kernel/user `git diff --check` all returned 0. This replacement was
probe-noisy but not freeze-shaped: 2 probe snapshots / 16 lines, with no
nonzero rq/sched-timer hold or stale timer/IPI-age match. The QEMU monitor
still reported the known `Error: no surface` on all 8 screendumps, so the
classification is based on serial liveness and marker scans. At that point,
R7c/M1 still needed one more clean 15-minute replay and the R5 classification
gate; both are now complete.

2026-07-03 R7c/P0 M1 replay 3: archived at
`build-x86_64/yt-mainpage-freeze-repro/20260703T060505Z-r7c-m1-replay3-watch-input/`
and counts as responsive replay 3/3. The run used the same watch-page input
harness with `QEMU_AUDIO=none QEMU_AUDIO_BACKEND=none`, verified
`sched_starve_probe=1` on the booted kernel cmdline, reached KDE without the
R5 KWin startup crash, launched Chromium to the watch URL, injected all
monitor-input phases, and executed `ALIVE_0` through `ALIVE_14`. It exited
cleanly (`harness-rc=0`, `STATUS.txt` = `PASS/live`), `failure-markers.txt`
is empty, `post-qemu-processes-check.txt` is empty, the scratch image was
removed, kernel-source-newer check was empty, and top/kernel/user
`git diff --check` all returned 0. This replay was probe-silent
(`sched_starve_probe_events=0`, `sched-starve-probe-lines.txt` empty) and
had no Chrome SIGTERM lines in the marker scan. Monitor screendumps again
hit the known `Error: no surface` limitation on all 8 attempts, so pass/fail
is serial-liveness/marker based. The R7c/P0 replay battery is now 3/3
responsive. The later R5 inventory archived at
`build-x86_64/r5-kwin-startup-classification/20260703T062939Z-r5-archive-inventory/`
classified the replay-2 first attempt as historical intermittent KWin startup
noise rather than a P0 freeze recurrence, so the M1/R7c clean replay gate is
closed on the three R5-clean responsive runs.

## R3 — rcu_head_cache Double-Free (new lane)

Symptom: `__slab_obj_put: double free cache='rcu_head_cache'` via
`rcu_cb_kthread -> slab_free`, whole-machine `IPI_REASON_CRASH`. Two
occurrences on unrelated flag sets (archives `20260701T113143Z`,
`20260702T005734Z`); both clustered around socket/fd teardown load. See
`xv6-kernel-locking-rcu` skill for the signature and triage rule.

Steps:

1. Audit rcu_head users for double `call_rcu` or direct-free-plus-RCU-free
   races; the slab diagnostic prints cache/object/slab/index/state.
2. If the audit finds the path: fix minimally, prove with a targeted RCU
   churn stress under socket/fd teardown load.
3. If inconclusive: land a gated owner-tagging diagnostic on
   `rcu_head_cache` (record alloc/free callers), run KDE smoke until it
   triggers, then fix. If it cannot be provoked in >= 5 KDE smoke runs,
   document as bounded-open with the diagnostic in place.

2026-07-03 R3 diagnostic prep: read the RCU locking skill and audited the
current `kernel/lock/rcu.c` callback lifecycle against the archived crash
signatures. The strongest signatures are the same class:
`20260701T113143Z-af-unix-full-wait-slab-double-free-fail/` shows
`rcu_cb/0 -> rcu_invoke_callbacks -> slab_free` on an allocated RCU head, and
`20260702T005734Z-desktop-interaction-ordered-pageflip-rcu-double-free-fail/`
prints the concrete slab diagnostic
`double free cache='rcu_head_cache' obj=0x000000009e299fc8 ... cpu=2` before
the `rcu_cb/2` panic path. Root cause is still unproven, so added a default-off
owner table rather than patching allocator behavior: boot flag
`rcu_head_trace=1` records lifecycle history for slab-allocated
`call_rcu(NULL, ...)` heads only (allocation caller, enqueue caller, callback
function/data, invoke CPU/time, free CPU/time) and emits bounded
`rcu_head_trace:` lines for reuse-while-live, duplicate enqueue,
invoke-without-queued, duplicate free, or table-full cases. Embedded RCU heads
are intentionally not tracked by this diagnostic because the observed panic is
from `rcu_head_cache`.

Validation for the diagnostic: `cmake --build build-x86_64 --target kernel
-j2`, `cmake --build build-x86_64 --target kernel-sparse -j2`
(`failures=0, errors=0`, existing lock-context warning family only),
`git -C kernel diff --check`, top-level `git diff --check -- docs/active-work-plan.md`,
and `git -C user diff --check` passed. A trace-enabled nographic fork-safety
gate is archived at
`build-x86_64/desktop-bottleneck-profile/20260703T112411Z-r3-rcu-head-trace-nographic-gate/`:
boot cmdline verified `desktop=0 video=1280x800 netsurf=0 rcu_head_trace=1`,
`forktest` returned expected `rc=1`, `clonetest` and `cowtest` returned `rc=0`,
and no `rcu_head_trace` anomaly, slab double-free, panic, or assertion lines
were emitted. Next R3 step: run a KDE smoke/Chromium or desktop-interaction
reproducer with `QEMU_APPEND_EXTRA='rcu_head_trace=1'`; if the intermittent
double-free recurs, the new `rcu_head_trace:` line should identify the prior
owner history of the `rcu_head_cache` object immediately before the slab
assertion.

2026-07-03 R3 trace-enabled KDE provoke attempt 1/5: archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T112855Z-r3-rcu-head-trace-kde-desktop-interaction/`.
The command used `QEMU_AUDIO_BACKEND=none`,
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, and
`QEMU_APPEND_EXTRA='rcu_head_trace=1'`. The current run status file reports
`status_code=0`, `stage=final`, and
`KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=desktop-interaction-latency`; `run.log`
verifies the booted cmdline includes `rcu_head_trace=1`. Runtime searches over
the current run files found no `rcu_head_trace:` anomaly, `__slab_obj_put`,
double-free, assertion, `IPI_REASON_CRASH`, panic, fatal page fault, coredump,
or runtime `status=FAIL` lines. Desktop visibility was
`first_visible_ms=15078`, `first_nonzero_ms=10598`; the direct-launch probe
passed with `elapsed_ms=2225`, `probe_rc=0`, and `konsole_wait_ms=1405`. This
attempt did not reproduce the R3 double-free. Note for future readers: this
archive also contains stale `host-wrapper-output.log` and
`host-wrapper-exit-code.txt` from the pre-existing mutable smoke directory;
judge this attempt by the current `run.log`,
`kde-desktop-interaction-latency.log`, and status files.

2026-07-03 R3 bounded-open result: completed four additional trace-enabled
KDE desktop-interaction provoke attempts after clearing the mutable smoke
scratch directory before each run:
`20260703T113417Z-r3-rcu-head-trace-kde-desktop-interaction-attempt2/`,
`20260703T113614Z-r3-rcu-head-trace-kde-desktop-interaction-attempt3/`,
`20260703T113819Z-r3-rcu-head-trace-kde-desktop-interaction-attempt4/`, and
`20260703T113959Z-r3-rcu-head-trace-kde-desktop-interaction-attempt5/`. All
four booted with `rcu_head_trace=1` verified in `run.log`, reported
`status_code=0` and
`KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=desktop-interaction-latency`, and
passed the direct-launch probe. Metrics were: attempt 2
`first_visible_ms=8852`, `first_nonzero_ms=5066`, `elapsed_ms=2047`,
`konsole_wait_ms=1507`; attempt 3 `first_visible_ms=8861`,
`first_nonzero_ms=5798`, `elapsed_ms=2144`, `konsole_wait_ms=1509`;
attempt 4 `first_visible_ms=9656`, `first_nonzero_ms=8805`,
`elapsed_ms=2100`, `konsole_wait_ms=1520`; attempt 5
`first_visible_ms=8736`, `first_nonzero_ms=6437`, `elapsed_ms=2137`,
`konsole_wait_ms=1497`. Runtime searches over each current run's `run.log`,
`kde-desktop-interaction-latency.log`, and status files found no
`rcu_head_trace:` anomaly, `__slab_obj_put`, double-free, assertion,
`IPI_REASON_CRASH`, panic, fatal page fault, coredump, or runtime
`status=FAIL` lines. Together with attempt 1, R3 now has 5/5 clean
trace-enabled KDE provoke attempts without reproducing the intermittent
`rcu_head_cache` double-free. Per the R3 plan, leave the default-off
`rcu_head_trace=1` diagnostic in place and treat the lane as bounded-open
rather than root-caused; any future recurrence should be archived immediately
and correlated with the emitted owner-history line.

## R4 — pactl Fatal Page Fault (new lane)

Symptom: `pactl: fatal page fault cr2=... err=0x6` at KDE session start;
observed 2026-07-02 (`20260702T144252Z-...-kde1-pactl-fault-fail/`) and
historically (`20260701T205346Z` in the history file). Unowned bug class.

Steps:

1. Classify from the archived faults: userspace bug in the imported payload
   (bad address pattern inside pactl/pulse libs) vs kernel fault-path bug
   (mapping that should exist, COW/demand-paging edge, or corruption class).
2. If kernel-side: reduce and fix with its own gate. If payload-side:
   document here and close the lane.

Status: classified and closed for current kernel-gate interpretation. As of
2026-07-04 the KDE session no longer invokes `pactl` on the readiness-critical
path by default; it records PipeWire/Pulse socket status in
`/kde-audio-status.log` and logs `phase=pactl status=SKIPPED`. Treat future
`pactl` crashes as optional-helper/libpulse diagnostic evidence unless the run
explicitly enabled `kde_pactl_probe=1` or `KDE_PACTL_READINESS_PROBE=1`.

2026-07-03 R4 classification: inspected the pactl-fail archives from
2026-06-27 through 2026-07-02, including
`20260630T080116Z-egl-trace-pactl-session-ready-crash/`,
`20260630T203603Z-desktop-interaction-konsole-nosample-pactl-gp-fail/`,
`20260630T213944Z-desktop-interaction-copyin-fast-enabled-pactl-gp-fail/`,
and `20260702T144252Z-pcid-noflush-default-kde1-pactl-fault-fail/`.
The crash is not PCID-specific: the same signature exists in older
`vm_asid_init: max ASID = 0` runs as well as the later ASID=4095 default-flip
attempt. The detailed VMA traces put `rip` in
`/usr/lib/x86_64-linux-gnu/pulseaudio/libpulsecommon-16.1.so` at file offset
`0x305c0` (`pa_log_levelv_meta`), with `cr2/rsp` just below the stack VMA and
`fault-va ... vma=(none)`. Disassembly shows this is the function's large-frame
stack probe (`sub $0x1000,%rsp; orq $0,(%rsp)`) entering the guard page during
recursive PulseAudio logging/assertion, not a missing text/data mapping or a
normal demand-paging hole. The related #GP variant resolves to
`pa_ioline_unref` / assertion logging and carries text-shaped bad pointer
fragments such as `/x86_64-` and `common-1`. Archive scans found no panic,
slab double-free, `IPI_REASON_CRASH`, kernel assertion, or other kernel-crash
marker in the pactl-fail runs. Classification: historical intermittent
pactl/libpulse helper crash, payload-side for gate purposes. Reopen only with
a direct pactl/PulseAudio reducer if it becomes deterministic or user-visible
audio failure, or if future evidence couples it to kernel corruption markers.

2026-07-04 update: the Q2 archive
`20260704T121552Z-q2-chromium-launch-only-session-ready-pactl-gp/` reproduced
the related `#GP` form at `rip=0x7fffff694449` with no CR2 and non-canonical
`rdi=0x312d6e6f6d6d6f63` (`common-1`-shaped). KWin/plasmashell/Xwayland still
reached PASS immediately after the `pactl` crash, proving this helper crash is
not the Plasma/Wayland teardown cause. The follow-up Q2 archive
`20260704T123120Z-q2-chromium-launch-only-pactl-skipped-wayland-refused/`
skipped `pactl`, reached Chromium launch, and failed later on the separate
plasmashell KCrash / Wayland `Connection refused` blocker.

## R5 — KWin Startup SIGSEGV in `QMutex::unlock()` (new lane)

Symptom: `kwin_wayland` crashes during KDE session startup with SIGSEGV
(status=11) at `rip=0x7ffffd628c9f` in `/usr/lib/x86_64-linux-gnu/libQt5Core.so.5`.
`addr2line` resolves the crash to `QMutex::unlock()`. The `kde-session`
wrapper detects the failure, retries, and KWin succeeds on attempt 2; the
desktop then reaches a usable state. Observed 2026-07-02 immediately after
the P0 idle-pull/atomic-halt fix landed. Relationship to P0 fix is
unconfirmed — possible regression (scheduler/futex/timing interaction),
pre-existing flake now unmasked by changed idle/wakeup timing, or
independent payload-side race.

Steps:

1. Reproduce with the standard KDE launch and capture: (a) whether the
   crash is deterministic or flaky, (b) whether it occurs on the pre-P0-fix
   kernel (requires a controlled revert/build of the idle-pull/atomic-halt
   changes), (c) a guest core dump or at least the full KWin stderr/stdout
   leading to the crash.
2. Classify: kernel-side (futex, scheduler wakeup, memory corruption) vs
   payload-side (Qt/KWin race). A `QMutex::unlock()` crash strongly suggests
   an invalid mutex pointer or memory corruption; if it reproduces pre-fix,
   treat as independent payload bug and close the lane. If it is fix-
   dependent, audit the futex/wakeup paths touched by the P0 changes.
3. If kernel-side: reduce to a minimal repro (synthetic futex/mutex stress
   under idle churn) and fix. If payload-side: document and close the lane.

Status: classified for the M1/R7c gate; root cause remains bounded-open as a
separate KWin/Qt object-pointer corruption lane. 2026-07-02: the "Chromium
window not visible" half of the user report is CONFIRMED SEPARATE from this
lane and from the P0 fix — it has its own lane now (R8 above) with the fresh
evidence and continuation direction.

2026-07-03 R5 classification inventory: archived at
`build-x86_64/r5-kwin-startup-classification/20260703T062939Z-r5-archive-inventory/`.
The inventory searched KDE smoke `run.log` files and YouTube replay
`raw-serial.log` files for KWin exception/status markers. It found 102 marker
lines across 46 archives: 43 historical archives and 3 current-day archives.
Historical hits predate the P0/R7c timer and idle-pull work and include the
same `QMutex::unlock()`/text-shaped bad-pointer family already recorded in the
history file (`20260630T233955Z...`, `20260701T062300Z...`,
`20260701T160948Z...`, plus varied libQt/libKWin/page-fault signatures back
to 2026-06-27). The current-day hits were only the two R7b rerun-needed
startup failures and the R7c replay-2 first attempt; the same-day clean
controls in `current-run-marker-check.tsv` show zero KWin markers for the
R7b reruns, R8/R7c guards, and the three clean M1 replays
(`replay1`, `replay2-rerun1`, `replay3`). All
`kde-plasma-kwin-crash-regression.txt` files were empty, so the raw run logs
are the authoritative evidence. Classification for M1/R7c: historical
intermittent KWin startup crash family, not a deterministic P0/R7c regression
and not a freeze recurrence. The invalid replay-2 first attempt stays excluded;
the three R5-clean responsive replays are enough for the M1 clean gate.

## P0 — Chromium/YouTube Freeze (solution)

Established facts (evidence in the history file and
`build-x86_64/yt-mainpage-freeze-repro/`):

- Pre-existing bug; A/B-exonerated from the noflush fix (pre-fix kernel
  froze identically).
- Shape: Chromium fully launches, then userspace stops — serial TTY echoes
  but bash never executes; no panic; kernel alive.
- Signature: ~50 user threads in `R` state (plasmashell, kwin, dbus, bash)
  while 3-4 of 6 CPUs sit halted-idle; busy CPUs sample in
  `__do_scheduler_wakeup` -> `rq_trylock_two` retry loops and `__sched_timer`
  global lock contention (`timer_tick`/`sched_timer_set`).
- Deterministic repro: boot the KDE image
  (`DISPLAY_MODE=gtk USE_KVM=1 QEMU_GPU=virtio-vga-gl-primary QEMU_INPUT=virtio
  QEMU_NET=1 QEMU_GDB=1 QEMU_APPEND='root=/dev/disk0 video=1280x800 netsurf=0
  webkit=0' bash scripts/launch/run-qemu.sh x86_64
  build-x86_64/kernel/build/kernel/xv6.bin <scratch fs.img>`), then from the
  serial console:
  `XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root WAYLAND_DISPLAY=wayland-0
  /bin/wayland-chromium https://www.youtube.com/`.
  The original long-line/foreground captures froze 2-6 min after Chromium
  starts, but 2026-07-02 corrected short-command/background launches have
  stayed live through 15-18 minute liveness windows. Treat the repro itself
  as open until an independent `ALIVE_N` command stops executing. Keep guest
  serial commands short: the console truncates long lines (~55-60 chars) and
  drops trailing `&`.

Working hypotheses, exactly one of which the diagnostic must select:

- H-a: idle CPUs miss wake IPIs — classic idle `hlt` race (NEEDS_RESCHED
  checked with interrupts enabled before `hlt`, wake IPI arrives in the
  window and is consumed before `hlt`).
- H-b: rq lock convoy — `__do_scheduler_wakeup`'s trylock+backoff retry
  loop livelocks under contention; wakeups never complete, runnable threads
  never enqueue.
- H-c: `__sched_timer` global-lock convoy — every CPU's tick fights one
  lock; with Chromium-scale timers the hold time starves wakeup progress.

Steps:

1. Read `.github/skills/xv6-kernel-freeze-triage/SKILL.md` and
   `.github/skills/xv6-kernel-process-scheduler/SKILL.md`.
2. Add a behavior-free starvation probe, default off, cmdline
   `sched_starve_probe=1`: from the boot CPU timer path, every ~2s, when
   (runnable user threads > 16 AND halted CPUs > 1), print per-CPU state
   (halted, `NEEDS_RESCHED`, current task), each rq lock owner and
   approximate hold time, wake-list depth per CPU, `__sched_timer` lock
   owner, and last wake-IPI send/receive per CPU. Print nothing outside the
   starvation condition. Gate: kernel builds; nographic boot clean; KDE
   desktop-interaction reducer passes with the probe enabled but silent.
   Status 2026-07-02 (resolved): the earlier visible-timeout retries
   (`20260702T052435Z-...retry1/`, `20260702T052639Z-...retry2/`) were the
   known passive-visible-detector flake — the reruns lacked
   `KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`. With active sampling the gate
   PASSED with the probe enabled and fully silent (0 probe lines):
   `20260702T142422Z-sched-starve-probe-active-sample-silent-pass/`
   (`first_visible_ms=22580`, `konsole_wait_ms=4182`, no
   panic/fault/coredump). Step 2 is COMPLETE.
   Bonus evidence already in hand from retry2: during a heavily loaded KDE
   startup the probe fired transiently and recorded the H-a signature —
   idle CPUs with `halted=1 needs_resched=1` (cpu2/cpu3) while
   plasmashell/kded5 were runnable, with `rq_owner=-1 rq_hold_ms=0
   wake_depth=0` and `sched_timer_owner=-1 sched_timer_hold_ms=0` ruling
   out H-b and H-c in those samples. Treat H-a as the leading hypothesis
   entering step 3; the freeze-time capture must still confirm it.
3. Reproduce with the recipe, probe enabled. The probe output must
   discriminate H-a (idle CPU with `NEEDS_RESCHED` set or wake-IPI sent but
   still halted), H-b (rq lock held/contended across samples with waiting
   wakers), H-c (`__sched_timer` lock owner changing but held in every
   sample).
   Status 2026-07-02: first corrected YouTube repro attempt stayed live and
   is inconclusive, not a hypothesis result:
   `build-x86_64/yt-mainpage-freeze-repro/20260702T152613Z-p0-step3-sched-starve-probe-kde-yt-rerun/`.
   Actual YouTube launch and `ALIVE_R0`-`ALIVE_R6` executed; no freeze-time
   probe lines, so no H-a/H-b/H-c selection yet. Second corrected attempt
   also stayed live for 17m23s with `ALIVE_0`-`ALIVE_8` executing:
   `build-x86_64/yt-mainpage-freeze-repro/20260702T154607Z-p0-step3-sched-starve-probe-kde-yt-shortserial/`.
   It produced transient H-a-shaped probe lines while still live; do not
   classify them as freeze proof. Repro-drift audit found the older freeze
   archives used long foreground Chromium serial lines and lack exact replay
   metadata, so the next attempt must archive QEMU dry-run, kernel/fs mtimes,
   launcher state, and Chromium argv/env/YouTube evidence before the liveness
   window. Metadata-complete rerun also stayed live for 18m45s with
   `ALIVE_0`-`ALIVE_14` executing and YouTube manifest evidence archived:
   `build-x86_64/yt-mainpage-freeze-repro/20260702T161703Z-p0-step3-sched-starve-probe-kde-yt-metadata/`.
   A repeat metadata-complete run likewise stayed live for 18m03s with
   `ALIVE_0`-`ALIVE_14` executing:
   `build-x86_64/yt-mainpage-freeze-repro/20260702T164220Z-p0-step3-sched-starve-probe-kde-yt-repeat/`.
   R1 remains open because no corrected run has produced a freeze-time
   liveness failure or timed GDB samples; continuing to rerun the same recipe
   is low-yield unless the user wants more samples.
   Unblocking guidance (2026-07-02, from the original capture session —
   execute 3a/3b/3c in order, they are independent of each other):
   3a. REPLAY THE UNCORRECTED RECIPE — it froze 2/2. The original captures
       ran Chromium in the FOREGROUND of the serial bash (the trailing `&`
       and redirect were eaten by serial truncation; see Known Failure
       Modes item 4). That form is not wrong for reproduction; only its
       liveness check was wrong ("bash never executes" was expected — bash
       was foreground-blocked on Chromium). Replay the exact archived
       long-line foreground form, and move liveness OUT of that bash:
       judge freeze by (i) QEMU monitor screendump hash progression,
       (ii) timed GDB samples (thread states, R-count vs halted CPUs),
       (iii) probe lines. The kernel-side signature in the original
       captures (~50 R threads, 3-4/6 CPUs halted) was real regardless of
       bash being busy.
       Status 2026-07-02: foreground replay stayed live/inconclusive for
       16m02s; HMP screendump had no surface, but timed GDB and 56 probe
       lines showed only transient queue imbalance with fresh ticks and
       free/short-held locks. No H-a/H-b/H-c/H-d selection:
       `build-x86_64/yt-mainpage-freeze-repro/20260702T174221Z-p0-step3a-foreground-replay/`.
   3b. ADD THE MISSING INTERACTION INGREDIENT — the user's original freeze
       came from `launch-gui.sh` with live GUI interaction (host GTK
       window focused, grab-on-hover, mouse moving/clicking during page
       load), not from a quiet autostarted page. Inject guest input during
       the load window using the KDE smoke qemu-monitor/mouseinject
       machinery (hover/scroll over the page, a few clicks). Also try the
       watch-page-with-autoplay URL in addition to the main page — video
       decode adds the thread/wake load the quiet main page lacks.
       Status 2026-07-02: watch-page/autoplay replay with staged short
       launcher and monitor mouse/scroll/click injection stayed live for
       ~15m35s; `ALIVE_0`-`ALIVE_14` executed. Probe lines again showed
       transient imbalance with fresh ticks and free locks, but no true
       freeze and no H-a/H-b/H-c/H-d selection:
       `build-x86_64/yt-mainpage-freeze-repro/20260702T180847Z-p0-step3b-input-watch-replay/`.
   3c. DECOUPLE THE H-a FIX FROM FREEZE REPRODUCTION — transient
       H-a-shaped windows (`halted=1 needs_resched=1` with runnable user
       threads) now appear in THREE independent contexts (retry2 KDE
       startup, both live YouTube runs). That is a real bug with real
       latency cost (S2) even if the hard freeze has an extra trigger.
       Two parallel moves: (i) code-audit the idle path — locate the
       NEEDS_RESCHED/IPI-pending check relative to `hlt` (around
       `CPU_SET_HALTED` in start_kernel.c); if interrupts are enabled
       between the check and `hlt`, the race window exists structurally
       and can be fixed on code evidence alone; (ii) write a synthetic
       wake-storm reducer (user program: 200+ threads in cross-CPU
       futex/pipe wake chains with forced idle churn) that measures
       max wake-to-run stalls and idle-CPUs-with-needs_resched incidence;
       make it the failing-then-passing gate for the H-a fix, with the
       YouTube replay (3a/3b) as validation rather than discovery.
       If the structural audit confirms the window, the M1 gate becomes:
       reducer stall metric collapses post-fix + probe silent in 3
       replays of the 3a/3b recipe + KDE pass.
       Status 2026-07-02: probe upgrade and `/bin/wakestorm` were added and
       staged. Bounded pre-fix runs completed cleanly but produced no
       starvation snapshots, so this is a negative characterization, not a
       failing reducer signal:
       `build-x86_64/yt-mainpage-freeze-repro/20260702T173313Z-p0-step3c-wakestorm-prefix/`.
       A redesigned H-d-oriented default reducer also completed cleanly and
       probe-silent (`status=PASS`, `worker_runs=1280/1280`); its stronger
       form timed out before printing a summary, so that is a reducer
       termination bug, not H-d evidence:
       `build-x86_64/yt-mainpage-freeze-repro/20260702T190153Z-p0-step3c-wakestorm-hd-proof/`.
       After fixing the reducer wait/wakeup bug, both default and strong
       forms completed with `status=PASS`, full worker-run counts, and
       `probe_snapshots_delta=0`; this is a reliable negative, not a
       pre-fix red reducer signal:
       `build-x86_64/yt-mainpage-freeze-repro/20260702T192717Z-p0-step3c-wakestorm-hd-fixed-proof/`.
       Read-only audit concluded current `wakestorm hd` likely misses the
       probe predicate by alternating between too many active workers
       (`halted <= 1`) and too few runnable workers (`runnable_user <= 16`).
       Next reducer iteration should mimic GUI partial pressure: continuous
       small wake bursts, reactors/leaves, and idle churn that keeps
       `runnable_user > 16` and `halted > 1` across a 2s sample window.
       The resulting `wakestorm guihd 240 60 3200 32` mode completed
       cleanly with high reactor/leaf activity but stayed probe-silent
       (`status=PASS`, `probe_snapshots_delta=0`), so nographic synthetic
       load still does not provide the required pre-fix red reducer signal:
       `build-x86_64/yt-mainpage-freeze-repro/20260702T195316Z-p0-step3c-wakestorm-guihd-proof/`.
       The same `guihd` reducer run inside a ready KDE session with audio
       required/present also completed probe-silent (`status=PASS`,
       `probe_snapshots_delta=0`, `ALIVE_0`-`ALIVE_11` and `ALIVE_FINISH`
       executed). Current status: P0 has transient GUI probe evidence but
       no reproducible freeze-time capture and no red reducer, so step 4
       is not authorized:
       `build-x86_64/yt-mainpage-freeze-repro/20260702T200225Z-p0-step3c-wakestorm-guihd-kde-proof/`.
   3d. STRUCTURAL FINDINGS ALREADY IN HAND (2026-07-02 code read) — the
       idle loop in `kernel/kernel/start_kernel.c` (~L205) is:
       `scheduler_yield(); intr_on(); CPU_SET_HALTED();
       arch_wait_for_interrupt(); CPU_CLEAR_HALTED(); intr_off();`.
       Two consequences:
       (i) The classic pre-hlt race window EXISTS (`intr_on()` before
       `hlt`; a reschedule IPI landing in that window is consumed and the
       CPU halts anyway) — but with the periodic 1000 Hz LAPIC tick each
       miss costs at most ~1ms, so this alone CANNOT explain probe samples
       showing `halted=1 needs_resched=1` persisting across 2s-spaced
       samples. Fix it, but do not expect it to be the whole bug.
       (ii) The persistent form implies a NEW hypothesis H-d: the halted
       CPU wakes on every tick, runs `scheduler_yield()`, finds ITS OWN rq
       empty (`__sched_pick_next` returns NULL because the runnable
       threads are enqueued on other CPUs' queues), and re-halts — i.e., a
       wake-placement/load-balancing gap (`rq_select_task_rq` piling
       tasks onto busy CPUs, no idle steal), not a lost IPI. The probe's
       `wake_depth=0 rq_owner=-1` on the halted CPUs fits H-d.
       Next probe upgrade to discriminate: log per-CPU rq task counts
       (nr_queued per rq) in the starvation snapshot. H-d evidence =
       R threads concentrated on 1-2 rqs while halted CPUs' rqs are empty.
       H-d fix shape: make `rq_select_task_rq` prefer idle CPUs
       (`CPU_FLAG_HALTED`/active-mask aware) or add idle-entry work
       stealing in the idle loop before halting. Also verify idle CPUs
       actually receive the periodic tick (per-CPU LAPIC arming) — a
       tickless-idle CPU would convert the 1ms race into an unbounded one.
4. Apply the minimal fix for the selected hypothesis only:
   - H-a: close the idle race — re-check `NEEDS_RESCHED` with interrupts
     disabled and enter halt with `sti;hlt` atomically (or equivalent).
   - H-b: bound the retry loop — after N failed trylock rounds, fall back to
     a blocking ordered two-lock acquisition; keep the fast path unchanged.
   - H-c: reduce hold time or shard the timer lock per CPU; smallest change
     first.
   Status 2026-07-02: STEP 4 IMPLEMENTED on structural selection. The code
   audit completed the root-cause selection that reproduction could not:
   H-d is confirmed as a structural hole — an empty EEVDF class rq clears
   its ready bit (`rq_clear_ready` in `proc/rq.c` / `sched_eevdf.c`), so
   `pick_next_rq()` never selects the EEVDF class on a fully idle CPU and
   Mode B idle balance (`__eevdf_idle_balance`, reachable only from
   `__eevdf_pick_next_task`) can never run there; Mode A periodic balance
   runs from `task_tick`, which `scheduler_yield()` skips for idle
   threads; and the `on_rq`/`on_cpu` wakeup fast paths in
   `__do_scheduler_wakeup` bypass `select_task_rq`, so fast sleep/wake
   tasks pile onto their previous CPUs. A halted-idle CPU therefore had NO
   mechanism to acquire queued work. The H-a pre-hlt window also existed
   structurally (`intr_on()` before `hlt`), tick-bounded at ~1ms.
   Fixes landed (all kernel-submodule dirty, uncommitted):
   - `kernel/proc/sched_eevdf.c`: new public `eevdf_idle_pull(int cpu)` —
     idle-entry work stealing with a lock-free surplus precheck, local
     rq_lock + existing `__eevdf_idle_balance` (1ms cooldown intact).
     OPEN (Code Review CR-1): the surplus precheck fires only on remote
     `nr_running>=2`, but a running task is excluded from `nr_running`, so
     the 1-hog+1-waiter shape (`nr_running==1`) is never pulled even
     though `__eevdf_idle_balance` would take it. This is the exact H-d
     shape — the fix may be incomplete. Verify with a 1+1 repro (pin a hog
     plus one waiter to one CPU, idle the rest, watch for pull) before
     committing P0 done. Also CR-10: the 1ms cooldown is stamped on entry
     before trylock, so a contended attempt suppresses retries.
   - `kernel/start_kernel.c`: idle loop now calls `eevdf_idle_pull()`
     before halting and only halts when `!NEEDS_RESCHED()`.
   - `arch/x86_64/inc/x86.h`: new `arch_idle_halt()` = `sti; hlt; cli`
     (atomic enable+halt closes the lost-wakeup window);
     `arch/riscv/inc/riscv.h`: `wfi` + brief SIE toggle equivalent.
   Validation so far: kernel builds; nographic battery passed
   (`build-x86_64/yt-mainpage-freeze-repro/20260702T203404Z-p0-step4-idlepull-nographic-validation/`:
   wakestorm `hd` and `guihd` status lines emitted, forktest/clonetest/
   cowtest green — note the synthetic reducer was never red, so its
   metrics are equivalence checks only: hd `max_wake_to_run_us` 13956 vs
   13312/11479/9194 pre-fix samples, within run noise). KDE
   desktop-interaction gate PASSED with `sched_starve_probe=1` fully
   silent and no panic/fault/coredump
   (`build-x86_64/kde-plasma-desktop-smoke-history/20260702T203404Z-p0-step4-idlepull-atomichalt-kde-pass/`,
   `first_visible_ms=25143`, `konsole_wait_ms=6372`, both within the
   historical noise band).
5. Validate M1: three 15-minute repro runs with the fix, guest shell
   responsive throughout (periodic `echo ALIVE_<n>` probes execute), no
   panic/RCU/coredump markers. Then fork-safety gate, then one KDE
   desktop-interaction pass. Archive everything under
   `build-x86_64/yt-mainpage-freeze-repro/<UTC>-<label>/`.
   Status 2026-07-02: STILL OPEN — the fork gate and one KDE pass are done
   (see step 4 status), but the three 15-minute YouTube replay runs (3a/3b
   recipe, out-of-band liveness) have not been run post-fix. G1/G2 close
   only after those replays plus one post-fix replay with the probe
   silent. Per the exit-gate relaxation: if the hard freeze never
   reproduced pre-fix, 3/3 clean replays plus the structural selection
   above are sufficient to close G1.
   New 2026-07-02: M1 validation was additionally gated on R5 (KWin startup
   SIGSEGV). A KWin crash/restart during an M1 replay invalidates that
   individual replay because it is no longer a clean desktop session.
   2026-07-03 update: R7c/P0 now has 3/3 responsive clean 15-minute
   watch-page/input replays (`20260703T045729Z...replay1...` and
   `20260703T054444Z...replay2-rerun1...` and
   `20260703T060505Z...replay3...`). A separate replay-2 first
   attempt (`20260703T052430Z...replay2...`) stayed shell-live for all
   15 probes but is invalid for clean M1 because KWin crashed/restarted
   during startup. The R5 inventory
   (`build-x86_64/r5-kwin-startup-classification/20260703T062939Z-r5-archive-inventory/`)
   classified that attempt as historical intermittent KWin startup noise, so
   the replay battery now closes M1/R7c on the three clean responsive runs.
2026-07-02 default-flip attempt (FAILED, reverted — do not retry without a
corruption reducer): a first audit pass found and fixed one real cross-PCID
hole — `vm_remote_sfence_page()` used a bare local `invlpg` under the
kernel PCID, missing the user-PCID entry on the local CPU
(`kernel/arch/x86_64/mm/vm.c`; fix kept: global flush when PCID active,
matching the remote handler policy). The remote IPI handlers
(CR4.PGE toggle), kernel-VA `invlpg` (global pages), and the trampoline
TRAPFRAME `invlpg` (user PCID loaded) were verified correct. With defaults
flipped on: nographic gates PASSED
(`syscall-tlb-proof/20260702T144803Z-default-flip-attempt-gates/`,
ASID=4095 with no flags, amp~0, forktest/clonetest/cowtest green,
`x86_pcid=0` opt-out verified) but the KDE GUI gate failed twice in a row:
`20260702T144252Z-pcid-noflush-default-kde1-pactl-fault-fail/` (pactl
SIGSEGV) and decisively
`20260702T144605Z-pcid-noflush-default-kde2-kwin-gp-corruption-fail/` —
kwin_wayland #GP with the historical text-fragment pointer signature
(`rdi=0x7269757165725f65` = "e_requir"). Conclusion: at least one more
stale-TLB/PCID hole exists beyond the fixed one. Defaults reverted to
opt-in; reverted-default KDE pass confirmed
(`20260702T144803Z-pcid-optin-reverted-default-kde-pass/`).
Step 1 below is therefore still open; the next audit targets are the paths
NOT exercised by nographic boots but hot under KWin: pcache/reclaim page
stealing, COW fault downgrade, mprotect batching in `vma_validate`,
hugepage collapse/split, and `vm_asid` recycling generation coverage. The
audit must produce a REDUCER that reproduces the KWin corruption class
under `x86_pcid=1` before any new default attempt.

Steps:

1. PCID promotion safety audit (this unblocks default-on for M2/M3):
   enumerate every TLB-invalidation path — `vm_remote_sfence`, munmap/
   mprotect shootdowns, exec teardown, pagetable free, `vm_asid` recycling —
   and prove each invalidates across ALL PCIDs that may cache the mapping
   (INVPCID all-context, per-PCID INVPCID, or an asid-generation bump that
   `usertrapret` already honors). The historical KWin/Qt corruption is
   expected to be one missed cross-PCID invalidation; find it by audit, then
   build a reducer that hits it (mprotect/munmap on one CPU while a sibling
   thread on another CPU touches the same VA under a different cached PCID).
2. Fixed-cost reductions, each behind its own gate and measured with M2:
   a. Cache user FS_BASE per thread: write MSR only when the value changes
      (update cache in `arch_prctl` and context switch); drop the per-syscall
      `rdmsr`.
   b. Eliminate the second trapframe copy (entry stack -> utrapframe) by
      pointing the syscall path at a single authoritative frame.
   c. Skip the 4 cpumask atomics when the same thread resumes on the same
      CPU with no shootdown generation change.
   d. Replace the CR0.TS read-modify-write with a per-CPU CR0 shadow.

   2026-07-03 step 2a status: implemented and gated, with no default flips.
   x86 now treats `trapframe->tp` as the authoritative user FS base:
   `ARCH_SET_FS` writes `MSR_FS_BASE` and updates a per-CPU
   `user_fs_base`/valid cache, `ARCH_GET_FS` copies out the trapframe value,
   ptrace/coredump report the trapframe value, and `usertrapret` writes
   `MSR_FS_BASE` only when the per-CPU cached value differs. The syscall/trap
   entry path no longer reads `MSR_FS_BASE` on every userspace entry. This is
   valid for the current x86 port because CR4.FSGSBASE is not enabled, so
   userspace FS-base changes go through `arch_prctl` or clone TLS.

   Validation: `git diff --check`, `git -C kernel diff --check`, and
   `cmake --build build-x86_64 --target kernel -j2` passed. The nographic
   fork-safety/M2 gate is archived at
   `build-x86_64/desktop-bottleneck-profile/20260703T144944Z-p1-fsbase-cache-nographic-gate/`
   with boot cmdline `root=/dev/disk0 desktop=0 video=1280x800 netsurf=0
   webkit=0 glsmoke=0`, `status=PASS`, `forktest rc=1` with the known
   table-exhaustion signature, `clonetest rc=0`, and `cowtest rc=0`.
   `syscalltlb 2000` reported `getpid_ns` 1639/1674/1759/1634 and
   amplification 509/792/1549/3375 ns for 64/256/512/1024 pages. The KDE
   desktop-interaction gate with `KDE_SMOKE_REDUCER=desktop-interaction-latency`
   and `KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1` passed at
   `build-x86_64/kde-plasma-desktop-smoke-history/20260703T145751Z-p1-fsbase-cache-kde-desktop-interaction-pass/`
   (`status_code=0`, `first_visible_ms=8632`, `first_nonzero_ms=7808`,
   direct launch PASS `elapsed_ms=3078`, `konsole_wait_ms=2386`). The
   `konsole_wait_ms` value is above the M4 target and should be treated as a
   rerun/watch item, not as a new failure class; prior same-day KDE passes
   stayed below 2000ms. An unreduced default smoke reached guest-agent PASS
   but failed wrapper final capture because `/kdi.sh` was not staged, archived
   at
   `build-x86_64/kde-plasma-desktop-smoke-history/20260703T145537Z-p1-fsbase-cache-default-agent-final-capture-fail/`.
   The M9 guard did not improve Chromium and did not introduce a new failure
   class: the default launch-only guard stayed in the known R8 robust-client
   crash class at
   `build-x86_64/kde-plasma-desktop-smoke-history/20260703T150438Z-p1-fsbase-cache-chromium-launch-only-default-known-r8-robust-client-fail/`;
   an attempted `WAYLAND_CHROMIUM_BUNDLED_GL=1` control logged
   `WAYLAND_CHROMIUM_BUNDLED_GL="(unset)"` in the launcher and is archived as
   env-not-applied/known-R8 at
   `build-x86_64/kde-plasma-desktop-smoke-history/20260703T151142Z-p1-fsbase-cache-chromium-bundled-gl-env-not-applied-known-r8-fail/`.

   2026-07-03 step 2b status: implemented and gated where the existing
   lanes were executable, with no default flips. The x86 SYSCALL path now
   writes directly into the current thread's authoritative `utrapframe`
   `struct trapframe` instead of building a second stack frame and copying it
   in `usertrap_syscall()`. `usertrapret()` publishes the higher-half
   trapframe VA in the per-CPU `syscall_trapframe` slot next to the syscall
   stack top; `syscall_entry` reads that slot after `swapgs`, saves the user
   rsp in `syscall_scratch`, stores the syscall register frame directly, then
   switches to the per-thread syscall stack and calls `usertrap_syscall(tf)`.
   The IDT/alltraps path is unchanged and still copies its stack trapframe
   into the utrapframe. The first direct-frame attempt dereferenced the
   current thread/trapframe through the low kernel-stack mapping while still
   on user CR3 and failed immediately with a #DF at first userspace syscall;
   it is archived at
   `build-x86_64/desktop-bottleneck-profile/20260703T151746Z-p1-trapframe-direct-nographic-gate/`.
   The fixed version uses only the per-CPU higher-half `syscall_trapframe`
   pointer before the CR3 switch.

   Validation: `git diff --check`, `git -C kernel diff --check`, and
   `cmake --build build-x86_64 --target kernel -j2` passed before the runtime
   gates. The robust nographic fork-safety/M2 gate passed at
   `build-x86_64/desktop-bottleneck-profile/20260703T152723Z-p1-trapframe-direct-nographic-gate-robust/`
   with boot cmdline `root=/dev/disk0 desktop=0 video=1280x800 netsurf=0
   webkit=0 glsmoke=0`, `expect_rc=0`, `status=PASS`, `forktest rc=1` with
   the known table-exhaustion signature, `clonetest rc=0`, and `cowtest
   rc=0`. `syscalltlb 2000` reported `getpid_ns`
   1665/1704/1672/1728 and amplification 208/795/1664/2943 ns for
   64/256/512/1024 pages. The KDE desktop-interaction active-sample gate
   passed at
   `build-x86_64/kde-plasma-desktop-smoke-history/20260703T153013Z-p1-trapframe-direct-kde-desktop-interaction/`
   (`status_code=0`, `first_visible_ms=10585`, `first_nonzero_ms=6667`,
   direct launch PASS `elapsed_ms=2164`, `konsole_wait_ms=1512`).

   The M9 Chromium launch-only guard stayed in the known R8 robust-client
   crash class and is archived at
   `build-x86_64/kde-plasma-desktop-smoke-history/20260703T153232Z-p1-trapframe-direct-chromium-launch-only-guard/`:
   the sampler completed PASS and Chromium was exercised, but post-evidence
   ended `status=FAIL reason=chrome-crash-regression` with
   `missing GL_ANGLE_robust_client_memory`, no network-restart/no-connection
   loop, no GPU-init/config/exit errors, no GL request errors, no kernel
   faults, and process evidence showing browser/gpu/zygote but no renderer.
   This is no worse than the current M9 baseline and not a new trapframe
   regression. The M8 late-settle guard did not yield a usable measurement:
   `20260703T153418Z-p1-trapframe-direct-late-settle-m8-spinners/` hit the
   known R5 KWin startup crash family (`kwin_wayland` coredump, KWin retry)
   before a prompt timeout, and the one allowed rerun at
   `20260703T153914Z-p1-trapframe-direct-late-settle-m8-spinners-rerun/`
   failed during readiness on `pid 50 kwin_wayland: exception 13`. Both M8
   archives removed generated `*.fs.img` scratch images and left no QEMU
   running; treat M8 as invalid/blocked by the known R5 startup class for
   this step, not as a P1 step 2b regression.
3. Default-flip `x86_pcid=1 x86_cr3_noflush=1` only after: step 1 audit
   complete with reducer proof, M2/M3 pass, fork-safety gate, three KDE
   desktop-interaction passes, one Chromium-launch smoke, AND the P0 repro
   recipe stays clean (scheduler timing changes interact with P0).

## P2 — Ordered Page-Flip Promotion (solution)

Established facts: `virtio_gpu_page_flip_resource()` synchronously drains to
the producer fence unless `virtio_gpu_ordered_page_flip=1`;
direct-KMS A/B improved 100.3 -> 118.4/125.1 FPS
(`build-x86_64/pageflip-ordered-ab-proof/20260702T005257Z/`); two clean KDE
passes with the flag; two intervening attempts failed with known GUI flake
classes; final video confirmation blocked by the Chromium renderer-admission
lane (see history file "Next Chromium Reducer"). The second clean pass is
archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T143007Z-p2-ordered-pageflip-second-kde-active-sample-pass/`:
booted cmdline verified `virtio_gpu_ordered_page_flip=1`, status
`status_code=0`/`KDE-PLASMA-DESKTOP-SMOKE-DONE`, active sampling enabled,
`first_visible_ms=9689`, `first_nonzero_ms=6038`, direct launch PASS
`elapsed_ms=2288`, `konsole_wait_ms=1612`, and no runtime failure markers in
the archived logs/status files.

Steps:

1. DONE 2026-07-03: second clean KDE desktop-interaction pass with
   `virtio_gpu_ordered_page_flip=1` (M6 guard: FPS >= baseline, desktop
   guards intact). Archive:
   `build-x86_64/kde-plasma-desktop-smoke-history/20260703T143007Z-p2-ordered-pageflip-second-kde-active-sample-pass/`.
2. Default-flip the ordered path with a matched default-off control in the
   same session; keep a revert-ready one-line change. 2026-07-03 status:
   attempted and reverted, still OPEN. The same-binary explicit default-off
   control passed at
   `build-x86_64/kde-plasma-desktop-smoke-history/20260703T143727Z-p2-default-flip-same-binary-default-off-control-pass/`
   with boot cmdline `virtio_gpu_ordered_page_flip=0`,
   `status_code=0`/`KDE-PLASMA-DESKTOP-SMOKE-DONE`, active sampling enabled,
   `first_visible_ms=8733`, `first_nonzero_ms=5611`, direct launch PASS
   `elapsed_ms=2112`, and `konsole_wait_ms=1513`. The default-on attempt and
   one allowed rerun had no `virtio_gpu_ordered_page_flip=0` cmdline token but
   both hit the known R5/KWin startup #GP class before desktop evidence:
   `20260703T143835Z-p2-default-flip-default-on-kde-r5-startup-rerun-needed/`
   and
   `20260703T144119Z-p2-default-flip-default-on-kde-r5-startup-second-fail/`.
   Because default-on produced 0/2 desktop passes while the explicit
   default-off control passed, do not promote this default yet. The kernel
   source and rebuilt kernel artifact are restored to the opt-in
   `virtio_gpu_ordered_page_flip=1` behavior.
3. After the renderer-admission lane unblocks, run the Chromium-video
   reducer for M7 (`presentedFPS >= 55`); only then close this lane.

## M7 / Present-Path + Measurement-Validity Findings (2026-07-04, OFFLINE)

Source: offline read of the existing archive
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T005605Z-chromium-video-kprofile-gui-measurement/`
(kprofile launch-probe, video post-evidence, host-gui chromium stderr,
fbstat, run.log). No VM boot — parser/log analysis only.

### 0. This run's verdict line is bookkeeping, not a playback failure
`kde-chromium-video-post-evidence.log` ends `status=FAIL
reason=launch-evidence-missing`. In `KDE_CHROMIUM_VIDEO_KPROFILE=1` mode the
kprofile output REPLACES the normal launch-evidence stream, so the harness
reports the evidence as missing. Video actually decoded and presented (below).
Do NOT read this as an M9/launch regression.

### 1. The kprofile measurement itself was TRUNCATED and unit-broken (fix OFFLINE before re-running M7)
- `--timeout-ms 24000` but the 15s measured window does not open until after
  browser start (~9.5s to first PERF-VIDEO log) + page `startupMs=8000`.
  SIGTERM at t+24s cut the run with only ~4.5s of the 15s window sampled
  (`kprofile_timeout_hit=1`; last tick `rvfcSpanMs=4199.800`). M7 CANNOT be
  read from this run. Set kprofile seconds >= start(~12s)+startupMs(8s)+
  runMs(15s)+margin ~= 40s (`KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE_SECONDS=40`).
- CPU units bug: `cpu_busy_ms=0 cpu_total_ms=0`. `busy_ticks/total_ticks` are
  1kHz scheduler ticks (`sched.c` scheduler_yield), but `kprofile.c`
  print_cpu_metrics divides by `timebase_freq` (TSC Hz) -> rounds to 0. Fix to
  use tick Hz (1000), not `timebase_freq`.
- pgroup useless for multiprocess Chromium (`WAYLAND_CHROMIUM_MULTIPROCESS=1`
  this run): `pgroup_processes_final=2 pgroup_cpu_runtime_ms=10` — zygote-
  spawned gpu/renderer processes escaped the profiled pgroup. Either track the
  whole descendant set or note pgroup numbers are browser-broker-only.
- `ext4_pcache_readahead_pages` is miswired: the counter is incremented in the
  **writepage** path (`ext4fs_file.c:1577`, `ext4fs_file_writepage`), not in
  the read/readahead submit path, so the reported "245" says nothing about read
  readahead coverage. Move the counter to the real readahead submit site.

### 2. ~14% timer-tick loss under load — a scoreboard-wide validity caveat
kprofile fired its timeout at 24,000ms wall (CLOCK_MONOTONIC via
`host_compat_uptime`) but kernel `uptime_ms` (`get_jiffs()`, `accounting.c:626`)
advanced only 20,603ms. jiffies are advanced by the **BSP only** at 1kHz
(`timer.c timer_tick_advance`), so the guest dropped ~14% of ticks while the
6 vCPUs were saturated. Consequence: EVERY `*_ms` kstat (M2/M3/M4/M5/M8 and all
kprofile ms fields) UNDER-reports by that factor under load, and kernel timers
(frame callbacks, poll timeouts, sched ticks) fire late — a real source of
frame-pacing jitter, not just a metrics artifact. Add to the Scoreboard
prerequisites: treat single-run `*_ms` deltas under desktop load as +/-15% and
cross-check against a monotonic wall reference. Candidate fix later:
TSC-compensate lost BSP ticks (or let any CPU advance jiffies).

### 3. M7 present-path ceiling: decode is NOT the bottleneck; software blit is
From the ~4.5s that was sampled: presented 130 frames over rvfcSpan 4199.8ms
~= 31 fps against a 60fps asset; decoded 280 / dropped 114 = 41% drop;
`rvfcAvgGapMs=32.6` (~ every-other-vsync cadence); one 543ms stall; repeated
`event:waiting` data-starvation stalls. Decode kept ~1.0x media rate — drops
are LATE-PRESENTATION drops. `kde-fbstat.log`: all 213 page flips
`software_blit`, `native_present_credit=0`; virtio-gpu blob was disabled at
launch ("virgl + blob are not compatible", run.log:6), so every frame is copied
through the software scanout path. This is the true M7 ceiling and it sits in
P2 step 3 territory (present path), independent of the Q2 GL decision. Even a
perfect GL context will not exceed the ~30fps two-vsync cadence without a
zero-copy present (blob/dmabuf scanout on a rutabaga/gfxstream-capable QEMU).

### 4. Dominant KERNEL cost this run: serialized ext4 page-fill (new perf lane)
`ext4_pcache_read_page_ms=29981` across 38,687 fills (~0.78ms/page, ~151MB) —
the single largest kernel cost in a 20.6s window, and `pages_filled=38686`
means readahead is effectively not amortizing anything. `ext4fs_pcache_read_page`
(`ext4fs_file.c:380-418`) takes the **global per-fs `ext4fs_lock(esb)`** and
fills synchronously, so demuxer reads, executable page-in
(`vm_file_faults=151311`, 37 execs) and all processes serialize on one lock
(`ext4_lookup_lock_wait_ms=2570` confirms contention). This is what pushed
browser start to ~10s and produced the mid-playback `waiting` stalls. The batch
machinery already exists for prefault (`ext4fs_submit_readahead` /
`ext4fs_folio_submit_direct`); the fix is to use it on the fault + pread paths
and drop the esb lock while BIOs are in flight. This attacks M4/M5 startup AND
M7 stalls together; propose as a new perf lane (P3?) or fold into P1.

### 5. Q2/R8 reconfirmation on the VIDEO path
`ContextResult::kFatalFailure: missing GL_ANGLE_webgl_compatibility` in the GPU
process (pid 292, host-gui log:107) fired right at playback start and coincides
with the 543ms stall. This is the same passthrough-ANGLE extension gap tracked
in R8/Q2, now shown to bite the full video path (not just launch-only), forcing
the page's canvas/HUD onto raster fallback during the measured window. Q2 is
now resolved toward Mesa/virgl passthrough-required semantics; this
reconfirmation remains the reason M7 is not trustworthy until the rest of the
extension ladder is implemented, the rootfs image is refreshed, and default
Chromium is runtime-validated.

### Cheap OFFLINE next actions (no VM boot; matches Work Style)
1. Fix kprofile: timeout budget (>=40s), CPU-units divisor, pgroup-descendant
   note, readahead-counter site. All testable against this archive.
2. Add the ~14% jiffies-loss caveat to the Scoreboard measurement rules.
3. File the ext4 read-path serialization as a tracked perf lane with the
   batch-reuse fix sketch above.
Only THEN spend one VM cycle to re-measure M7 with a valid window.

Q0 implementation note (2026-07-04): kprofile-video reducer clamps the
profiling timeout to at least 40s/dynamic startup+run+margin; kprofile now
prints CPU busy/total in scheduler HZ ticks and labels pgroup data as
process-group-only with no descendant tracking; the ext4 readahead counter is
fed by successful read readahead BIO submission instead of mmap writeback.
Validation remains offline-only until build/parser checks pass.

## P3 — Ext4 Read-Path Serialization (candidate perf lane)

Status 2026-07-04: OPEN-CANDIDATE, not yet scheduled ahead of Q1-Q7.
Hypothesis from the M7 archive: Chromium startup/playback stalls are dominated
by synchronous ext4 page fills under the global per-fs lock
(`ext4_pcache_read_page_ms=29981`, `pages_filled=38686`, no useful readahead
coverage before the Q0 counter fix). The likely implementation slice is to
reuse `ext4fs_submit_readahead` / `ext4fs_folio_submit_direct` for fault and
pread paths, and to avoid holding `ext4fs_lock(esb)` while waiting for BIO
completion. This should move M4/M5 startup and M7 stalls together, but it must
stay behind the current queue unless the user explicitly reprioritizes it.

## Guardrails

- NO un-gated default flips, anywhere: kernel cmdline defaults, launcher
  env-flag defaults (`wayland-chromium-launcher.c`), image/session config.
  Diagnostic experiments must be opt-in (env or cmdline gated, default
  off). A default may change only with: a same-session A/B, a plan entry
  naming the lane, and the regression gate below. The
  `WAYLAND_CHROMIUM_AUTO_GL_FLAGS` flip (Known Failure Mode 16) is the
  cautionary example.
- Regression gate after ANY change to the kernel, launcher, image, or
  session scripts: (1) KDE desktop-interaction smoke with
  `KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1` passes; (2) the M9 Chromium
  launch-only reducer produces a result no worse than the current
  scoreboard state; (3) M4/M5/M8 within noise of the scoreboard. A new
  FAIL class = revert first, investigate second.
- Handover hygiene: before ending a work round, `git status` at top level
  AND in the kernel/user submodules; every behavior-affecting uncommitted
  diff must be reverted, committed with a lane reference, or listed in
  this plan with justification. Diagnostic probes left in-tree must be
  default-off and listed.
- Closed lanes — do NOT reopen without new evidence (proof chains in the
  history file): scheduler wake-to-run latency (bounded 85ms), futex
  key/timeout drift, poll/kqueue/AF_UNIX/eventfd/pipe primitives, guest
  cursor upload, raw PTY setup, tiny Wayland frame delivery, D-Bus/eventfd
  readiness. (R7b targets a specific unix-socket FIONREAD/poll semantic
  under a reproducible spin, with a required failing reducer — that is
  new evidence, not a blind reopen of the primitives lane.)
- Known intermittent: `rcu_head_cache` double-free
  (`rcu_cb_kthread -> slab_free`); archive + rerun once; only implicate a
  change if frequency rises (see `xv6-kernel-locking-rcu` skill).
- Known intermittent (new 2026-07-03, in-VM audit): konsole-shell-ready
  timeout — konsole launches (fork+exec ok, PTY fds seen) but never writes
  `/dev/shm/xv6-konsole-shell-ready`, hits the 45s marker timeout, goes
  zombie -> `kde-app-launch-latency-probe-output-fail` (M4 unmeasured).
  Seen 1/2 audit runs; retry passed with `konsole_wait_ms=1759`. Rerun
  once before implicating a change; only escalate if frequency rises.
- Trace volume perturbs the timing it measures: use thresholded/role-scoped
  trace flags in timing runs (`kde_wake_to_run_trace=5`, never `=1` combined
  with futex/IPC traces).
- One compile/VM lane at a time; check `pgrep -af qemu-system` first; never
  launch QEMU with a trailing `&` (use async terminal mode).
- Historical dirty inventory (superseded 2026-07-04): earlier handoffs
  recorded `ports/xz/src` unrelated dirty state plus Q1 dirty-by-design
  changes in `kernel`, `ports/mesa/src`, `user`, and the top level. Q1 is
  now landed through top-level `d60c203`, and focused status checks show the
  top level, `kernel`, `user`, `ports`, and `ports/mesa/src` clean. Treat
  the old inventory as resolved history; new behavior-affecting diffs must
  be committed, reverted, or listed here with justification.

## Verification Gates

- Static: `git diff --check` and `git -C kernel diff --check`.
- Build: `cmake --build build-x86_64 --target kernel -j$(nproc)`; after
  user/rootfs changes also `--target user` then `--target rootfs-refresh`.
- Runtime: the lane-specific gates above; before claiming KDE stability,
  `timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect` with
  `KDE_SMOKE_REDUCER=desktop-interaction-latency`.
- BATCHING (see Work Style): one battery may cover several independently
  revertable changes; bisect only on battery failure. Parser/classifier
  changes are validated offline against archives, never with a VM boot.
- Archive every proof run: KDE smoke into
  `build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/` (exclude
  `*.img`, then delete the scratch image); other lanes into their proof
  directories listed below.

## Evidence Archives

- `build-x86_64/yt-mainpage-freeze-repro/` — P0 freeze captures.
- `build-x86_64/syscall-tlb-proof/` — P1 microbenchmark A/Bs.
- `build-x86_64/pageflip-ordered-ab-proof/` — P2 KMS FPS A/B.
- `build-x86_64/kde-plasma-desktop-smoke-history/` — all KDE smoke runs.
- `build-x86_64/desktop-bottleneck-profile/20260702T2310Z/` — R7 profiling
  evidence (GDB PC samples, ioctl histograms, /proc stat window diffs,
  spinner rip->library resolution inputs).
- `docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md` —
  every evidence chain, closed-lane proof, Chromium renderer-admission
  status, KASAN/KLOG/kmemleak status, and the kernel dedup queue as of
  2026-07-02.

## Commit Policy

Commit PERIODICALLY, not just at the end: at every verified checkpoint (a
completed slice, a passing gate, before a risky change) and always before
ending a round. Do not let the working tree accumulate a large uncommitted
diff (Known Failure Mode 16). Each commit is lane-scoped with a lane
reference in the message; never bundle unrelated changes.

Submodule discipline (kernel, ports, user): commit DEEPEST-FIRST, then
update the parent pointer in the same checkpoint so the superproject never
points at an unpushed/unrecorded submodule commit. Standard cadence:
`git -C kernel commit ...`, `git -C ports commit ...`, `git -C user
commit ...`, then `git add kernel ports user <top-level files> && git
commit`. Run `git status` at top level AND `git -C <sub> status` for each
submodule at handover; every behavior-affecting diff must be committed,
reverted, or listed in this plan with justification. NEVER push without
explicit user approval.

Q1 sweep closure (2026-07-04): the current Q1 commit sweep is no longer
pending; source/runtime/docs rootfs overlay work landed through `d60c203`.
Remaining CR items are tracked as follow-ups in the Code Review section, not
as blockers to committing the landed Q1 batch.
