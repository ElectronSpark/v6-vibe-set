# Active Work Plan — Full History (append-only archive)


---

# Snapshot before the 2026-07-04 compaction rewrite (branch codex/host-linux-abi-shell-port-ff)

# Active xv6 Work Plan

Last updated: 2026-07-04 (appended Q2 `wl_shm` root cause/fix plus
launch-only PASS after the post-system-tray-isolation blocker; materialized on branch
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
| M7 | `presentedFPS` (60fps video) | Chromium-video reducer | 44.2 with REAL GL + ordered pageflip 2026-07-04 (arc: 36.8 baseline -> 42.9 ordered flip -> 44.2 real GL; dropPct 29.6, decode 61.5). Remaining ceiling: software-blit scanout (P2 step 3); real-GL kprofile runs need KPROFILE_SECONDS=90 | >= 55 |
| M8 | Idle-desktop host CPU | `ps -o pcpu= -p <qemu pid>` 3 samples, 30s+ after desktop ready, no apps launched | Borderline RED in the Q1 attribution run: 10s host-thread deltas were 106.3% then 101.2%, lifetime `ps pcpu` 128->119%; CPU was on the six vCPU threads, not GTK/virgl/helper threads. Track as R7c/M8 vCPU/KVM idle-cadence follow-up; do not claim Q1 makes M8 green. | < 100% |
| M9 | Chromium window visible | `KDE_SMOKE_REDUCER=chromium-video KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1` | PASS 2026-07-04 on the DEFAULT path with REAL hardware GL (Mesa extension ladder runtime-validated: zero missing-GL fatals, zero software fallbacks, GPU errors 0). Earlier same-day: wl_shm fix PASS | PASS |

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
- Q5 = R5 flake-rate reduction: ROOT CAUSE FOUND AND FIXED 2026-07-04
  (kernel free-before-TLB-shootdown in mm teardown/madvise deferred
  release; full record in the R5 lane). Gate battery passed, M4 1883.
  Remaining Q5 work is statistical closure only (accrue 30+ clean
  attempt-1 KWin launches across future batteries), after which R6 step 2
  (Q7) unblocks. Historical context: the family was the main gate polluter
  and blocked the 2026-07-04 post-KDE-ABI-closure M9 retry
  after the single allowed rerun. It blocked R6 step 2
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
  Follow-up `wl_shm` root cause/fix on 2026-07-04 supersedes that blocker:
  the post-system-tray-isolation Q2 archive's KWin log contained
  `wl_global_create: implemented version for 'wl_shm' higher than interface
  version (2 > 1)`, while Chromium later reported `No wl_shm object` and
  `Failed to initialize Wayland platform`. Inspection showed the staged
  libwayland protocol XML/server build implements `wl_shm` v2, but imported
  KWin/KWayland objects also export generated `wl_shm_interface` symbols; the
  smallest repair keeps libwayland-server's server-side `wl_shm` and
  `wl_shm_pool` protocol metadata private to `wayland-shm.c` when registering
  and creating shm resources, so compositor-local generated symbols cannot
  preempt the metadata used by `wl_display_init_shm()`.
  A new no-Chromium registry reducer/probe now gates this class before
  Chromium: archive
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T141143Z-kde-wayland-registry-wl-shm-pass/`
  passed with `wl_compositor=1`, `wl_shm=11 wl_shm_version=2`,
  `shm_bound=1`, `shm_buffer=1`, `wl_seat=12`, `xdg_wm_base=6`, and
  `zwp_linux_dmabuf_v1=43`. The single allowed Q2 launch-only retry then ran
  with the exact bundled/software GL unsets:
  `env -u KDE_SMOKE_CHROMIUM_BUNDLED_GL -u KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE -u MESA_EXTENSION_OVERRIDE -u LIBGL_ALWAYS_SOFTWARE -u GALLIUM_DRIVER -u MESA_LOADER_DRIVER_OVERRIDE -u LIBGL_ALWAYS_INDIRECT QEMU_AUDIO_BACKEND=none KDE_SMOKE_REDUCER=chromium-video KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1 scripts/gpu/kde-plasma-desktop-smoke.expect`.
  Archive:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T141345Z-q2-chromium-launch-only-wl-shm-fixed-pass/`.
  Result: PASS `status_code=0`
  `KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video ... chromium_launch_only=1`.
  The pre-Chromium registry gate again passed (`wl_shm=11`, `shm_bound=1`,
  `shm_buffer=1`), Chromium launch evidence reports
  `launcher_wayland_platform_fail=0`, the sampler reports `samples=63
  duration_ms=18000 status=PASS`, and the archived Chromium log has no
  `No wl_shm object` or `Failed to initialize Wayland platform`; Chromium now
  only warns that it binds `wl_shm` v1 while v2 is available and proceeds to
  perf-video. GPU evidence stayed on the accelerated path: KWin and host EGL
  reported Mesa/virgl on `D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)`, the
  browser env retained `LIBGL_DRIVERS_PATH=/lib/dri:/usr/lib/x86_64-linux-gnu/dri`,
  `MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu`, and `GALLIUM_DRIVER=virgl`, with
  no `llvmpipe`/`swrast`/software or bundled-GL markers. Launch-only perf-video
  telemetry recorded `presentedFPS=39.8` / `dropPct=24.25`; that is useful M7
  evidence but not a full non-launch-only M7 pass.
- Q6 = R2 (PCID corruption reducer, then guarded default retry).
- Q7 = DONE 2026-07-04: ordered-pageflip default flip landed with the
  same-session A/B battery (see P2 lane record); M7 36.8 -> 42.9. P2
  step 3 (>=55) remains blocked on the software-blit present path and Q2.
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
2026-07-04 Q2 RUNTIME VALIDATION — first default-path M9 PASS with real
hardware GL (user direction confirmed: real GL, no software fallback):

Staging verification (offline): mesa/src HEAD `fb2724503` (webgl-compat)
with the full ladder implemented; sysroot `libgallium-26.2.0-devel.so`
carries all six GL_ANGLE_*/GL_CHROMIUM_* strings and `libEGL.so.1.0.0`
carries `EGL_ANGLE_create_context_webgl_compatibility`; the LIVE fs.img
serves the same fresh copies from /lib (LIBGL_DRIVERS_PATH puts /lib/dri
first; /opt/host-gui/wayland-chromium/lib holds only the trace preload —
no LD shadowing; the kde-runtime overlay's /usr/lib copies are second in
every search path).

M9 launch-only gate (default Chromium path, guest Mesa/virgl, NO bundled
GL, NO extension override): `status_code=0` DONE, post-evidence
`status=PASS reason=launch-only`, ZERO `missing GL_*` fatals, ZERO
`kFatalFailure`, ZERO `--use-gl=disabled` fallback relaunches,
`gpu_init/config/exit_error_count=0`, `launcher_crash_seen=0`. The
passthrough command decoder accepted guest Mesa/virgl real GL for the
first time. Archive:
`20260704T192000Z-q2-mesa-angle-ladder-launch-only-first-default-path-pass`.
(M9 scoreboard: PASS on the DEFAULT path.)

Full chromium-video kprofile with real GL + ordered pageflip default:
first attempt hit a NEW
measurement caveat — real-GL startup pays a one-time virgl->D3D12
shader-compile + heavier cold-paging cost (read_page_ms 46.6s in that
window), so the 40s kprofile budget expired ~2s into playback; archived
`20260704T193500Z-q2-real-gl-video-kprofile-window-short-rerun-needed`
(rule: real-GL kprofile video runs need
KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE_SECONDS=90). The 90s rerun covered the
full window: `presentedFPS=44.2 decodedFPS=61.5 dropPct=29.62
speed=0.954 advanced=14.33`, ZERO GL fatals/fallbacks across 75s of real
GL. M7 arc today: 36.8 (baseline) -> 42.9 (ordered pageflip) -> 44.2
(real GL + ordered pageflip); the remaining gap to >=55 is the
software-blit scanout path (P2 step 3 / P3-M7 findings). Archive:
`20260704T195500Z-q2-real-gl-full-video-m7-44fps`.

R5 accrual through the Q2 battery: 9/9 clean attempt-1 KWin launches (running total since the R5 TLB fix).

Chromium binary ground truth (offline probe of chrome-linux64 150.0.7871.24):
the validating command decoder is DEAD in this build ("Ignoring request for
the validating command decoder. It is not supported on this platform.") —
passthrough is the only functional decoder, so the Mesa extension ladder was
the only real-GL route. The binary's checked extension set matches the
implemented ladder exactly; bundled ANGLE (GL/GLES/Vulkan/SwiftShader
backends compiled in) stays disabled/symlinked to guest Mesa in the image.

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

Q2 `wl_shm` closure 2026-07-04: the old archive's KWin log showed
`wl_global_create: implemented version for 'wl_shm' higher than interface
version (2 > 1)`, which explains why normal Chromium clients saw no usable
`wl_shm` global. The Wayland source/build implements `wl_shm` v2; the failure
was process-local ELF symbol interposition from imported KWin/KWayland
generated protocol metadata. `wayland-shm.c` now uses private server-side
`wl_shm`/`wl_shm_pool` interface metadata for `wl_display_init_shm()`, bind,
and shm-pool resource creation. The new no-Chromium
`KDE_SMOKE_REDUCER=kde-wayland-registry` gate passed in
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T141143Z-kde-wayland-registry-wl-shm-pass/`:
it enumerated `wl_compositor=1`, `wl_shm=11 wl_shm_version=2`, `wl_seat=12`,
`xdg_wm_base=6`, `zwp_linux_dmabuf_v1=43`, bound `wl_shm`, observed ARGB/XRGB
formats, and created a 1x1 `XRGB8888` shm buffer (`shm_bound=1`,
`shm_buffer=1`). The required single Q2 launch-only retry then passed in
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T141345Z-q2-chromium-launch-only-wl-shm-fixed-pass/`
using the exact `env -u ...` command above: pre-Chromium registry gate PASS,
`launcher_wayland_platform_fail=0`, Chromium sampler PASS (`samples=63`,
`duration_ms=18000`), no `No wl_shm object`, and no `Failed to initialize
Wayland platform`. GPU evidence remained clean Mesa/virgl/D3D12 with Chromium
env `/lib/dri`, `GALLIUM_DRIVER=virgl`, and
`MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu`; no software or bundled GL markers
were present. This makes M9 green on the launch-only gate. M7 is still not
closed: the launch-only page reached perf-video and reported
`presentedFPS=39.8`, below target and not from a full non-launch-only reducer.

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

Status 2026-07-04: ROOT CAUSE FOUND AND FIXED — kernel free-before-TLB-shootdown
in the mm teardown/madvise deferred-release batches (see the dated record at
the end of this lane). Statistical closure accrues across future KDE runs.
Previous: classified for the M1/R7c gate; root cause was bounded-open as a
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

2026-07-04 R5 ROOT CAUSE FOUND + FIX LANDED (offline forensics, zero
exploratory VM boots; five subagent passes over archives + sources):

Evidence chain (all offline):

1. Full-archive signature mining (72 crash-bearing dirs, superseding the
   46-dir inventory): ONE corruption family across ~24 instruction sites in
   11 modules; ASLR-invariant file_off identifies repeat sites (libQt5Core
   0xdbc9f x12, libkwin 0x1fe3d4 x6, libc 0x9fff4 x4, libstdc++ 0xbb91d
   x4, ...). 7-10% per KWin launch, attempt-1/cold-cache only, retry clean;
   two crashes inside ld.so itself. Rates: 53/769 history dirs; 3/29 KWin
   starts in the egltrace-noweston loop.
2. Disassembly forensics against the staged guest libs: EVERY poisoned slot
   is a NULL-expected runtime global in the zero-fill TAIL of the last
   file-backed page of a library's RW PT_LOAD segment (KWin::effects,
   std::__new_handler, GLPlatform::s_platform, fontconfig lazy FcMutex*,
   ICU gDataDirectory, a Q_GLOBAL_STATIC QMutex d_ptr). Poison content
   verified != file bytes and != zeros -> recycled-frame residue (ld.so
   path scratch "/usr/lib/x86_64-" repeats, env/cmdline fragments
   "kde_smok"/"ssibilit"/"_requir", live &_rtld_global pointers).
   Exception: libQt5Core 0x30fb85 is a distinct unchecked-nullptr bug in
   KWin::LibinputBackend (Connection::create() failure ->
   moveToThread(nullptr)) — payload-side, secondary.
3. Default-config mechanism audit: PCID off, cr3-noflush off,
   ext4_read_page_direct off in all failing runs; no user PTE carries
   PTE_G; futex has the wrong failure mode; all fill paths that serve
   those pages zero the tail. => the frame was corrupted AFTER a correct
   install: freed to the allocator while translations to it remained.

ROOT CAUSE (kernel mm/vm.c): free-before-shootdown in the deferred-release
batches. `__vma_clear_range` (munmap/exec/mremap teardown) released anon
frames via `page_free_anon_batch` whenever its 256-entry defer array
overflowed MID-LOOP, but performed the TLB shootdown only AFTER the loop.
`__vm_madvise_dontneed` was worse: both its mid-batch and per-segment
releases preceded the single final flush. Any VMA/madvise range >1MB
therefore returned frames to the buddy allocator while other CPUs held
stale TLB translations. The owning process legitimately reuses the VA range
(munmap->mmap, MADV_DONTNEED heap reuse), so a sibling thread WRITES
through the stale translation onto a frame already reallocated to another
process — during KDE attempt 1 that is kwin's freshly-faulted private-ELF
.bss-tail pages (cold-cache load storm = allocator churn + multithreaded
helpers tearing down >1MB maps). Attempt 2 is quiet -> clean. Matches
every archived signature including the poison content.

FIX (kernel, default-on correctness change): track the un-shot-down span
(`flush_from`) and call `vm_remote_sfence_range` for the cleared span
BEFORE every mid-batch/per-segment `page_free_anon_batch`, restoring the
code's own documented two-phase invariant ("Phase 2 (after TLB flush)
releases refs"). The madvise per-segment flush REPLACES the old final
flush (no added cost for single-segment madvise; first version stacked
them and inflated M4 to 4817 — refactored same-session). The madvise
mid-batch flush drops/re-takes the pgtable spinlock around the IPI-ack
wait (waiters spin with IRQs off and could never ack).

LATENT bugs found by the same audits — recorded, NOT fixed (all dormant
behind `vma_file_hugepage_collapse_enabled()` == 0, vm.c:2792; they BLOCK
ever re-enabling hugepage collapse):

- `__vma_clear_range` hugepage branch frees the whole 2MB folio when an
  unmap edge lands INSIDE the huge mapping (no coverage check).
- `vm_mprotect` hugepage branch rewrites the whole 2MB PTE protection on
  partial coverage; `__vm_madvise_dontneed` has no hugepage check at all.
- `page_free_anon_batch` frees everything as order-0 — a deferred 2MB
  folio would corrupt the buddy allocator.
- Whole-folio COW leaks one ref when the old folio's head page is
  unmapped.

Gate battery with the fix (all PASS):

- Nographic: forktest rc=1 (known signature), clonetest rc=0, cowtest
  rc=0; boot probe 7/8 clean (one unreproduced stall after service spawn,
  FIFO-driven boot — watch for recurrence).
- KDE desktop-interaction active-sample x3: attempt 1 hit the KNOWN
  artifact/visible-timeout harness flake class (17 prior archives; KWin
  itself clean) — archived
  `20260704T170500Z-r5-tlb-fix-kde-gate-known-artifact-timeout-rerun-needed`;
  rerun DONE `status_code=0` but M4 4817 (the double-flush, above) —
  archived `20260704T172500Z-r5-tlb-fix-kde-active-sample-pass-m4-high`;
  final run with the refactor DONE `status_code=0`,
  `konsole_wait_ms=1883` (BETTER than the 1958 scoreboard value),
  `first_visible_ms=11471`, zero kwin markers — archived
  `20260704T174500Z-r5-tlb-fix-kde-active-sample-pass-m4-1883`.
- KWin launched clean on attempt=1 in 3/3 KDE boots with the fix.

Statistical closure: baseline flake is 7-10% per KWin launch; every future
KDE battery accrues evidence (target: 30+ attempt-1 launches with zero
R5-class crashes). Track attempt-1 crash markers in every subsequent
archive. NOTE: this fix plausibly also feeds R3 (rcu_head_cache
double-free symptoms) and explains why the 2026-07-02 cr3-noflush trial
"reproduced the KWin corruption" (noflush widens the same stale-TLB
window) — watch both lanes' rates.

Secondary follow-ups spawned by R5 forensics:

- KWin LibinputBackend nullptr deref (crash site 5) — payload bug
  (Connection::create() failure unchecked); triage alongside R9 input
  work; kernel not implicated.
- Crash-dump tooling gap: /core.PID ELF cores are generated in-guest but
  never extracted; no symbolizer exists; the kwin fatal dump could also
  print the faulting VA's PTE/frame page-struct state. Cheap additions if
  R5-class evidence is needed again.


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

2026-07-04 P2 step 2 DEFAULT FLIP LANDED (Q7, unblocked by the R5 root-cause
fix): `virtio_gpu_ordered_page_flip` is now default-ON via
`virtio_gpu_ordered_page_flip_enabled()` in `kernel/virtio_gpu_scanout.c`
(absent token = on; `=0/no/false/off` restores the drain-to-fence path;
one-line revert = call site swap back to `virtio_gpu_cmdline_enabled`).

Same-session A/B battery (same binary, all PASS, zero KWin crash markers in
every run — R5 statistical accrual now 5/5 clean attempt-1 launches since
the fix):

- Default-on KDE active-sample: cmdline verified WITHOUT the token,
  `status_code=0`, `konsole_wait_ms=2144`, `first_visible_ms=12311`.
  Archive: `20260704T181500Z-q7-ordered-pageflip-default-on-kde-active-sample-pass`.
  (The 2026-07-03 promotion attempt failed 0/2 at this exact gate — both
  were R5-class KWin startup crashes, now root-caused/fixed; this pass
  confirms R5 was the sole blocker.)
- Explicit default-off control: cmdline verified WITH
  `virtio_gpu_ordered_page_flip=0`, `status_code=0`,
  `konsole_wait_ms=2122`, `first_visible_ms=13165` — statistically
  indistinguishable from default-on; the opt-out works.
  Archive: `20260704T183000Z-q7-ordered-pageflip-default-off-control-pass`.
- Chromium-video kprofile with default-on: full-window measurement
  `presentedFPS=42.9 decodedFPS=60.7 dropPct=26.43 speed=0.973
  advanced=14.86` vs the same-day default-off baseline
  `presentedFPS=36.8 dropPct=52.4` — +17% presented FPS, drops HALVED,
  727 page flips in the window vs 475 (all still software_blit — the
  remaining ceiling). Post-evidence `status=FAIL
  reason=launch-evidence-missing` is the documented kprofile-mode
  bookkeeping. Archive:
  `20260704T185000Z-q7-ordered-pageflip-default-on-chromium-video-m7-42fps`.

M6 guard: direct-KMS A/B (100 -> 118-125 FPS) was step 1, archived
2026-07-02; unchanged by this flip (same code path, now default).

P2 remaining: step 3 = M7 >= 55, still capped by the software-blit present
path + Q2 GL decision (see M7 findings + P3 lane); the ordered default is
necessary-but-not-sufficient for it.

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

Q0 in-VM validation (2026-07-04, two chromium-video kprofile runs):

- Attempt 1 exposed a Q0 follow-on harness gap: nothing waits for the
  kprofile job (kprofile prints its whole report only at exit), so with the
  new 40s budget the harness reached sync/teardown first and
  `/kde-chromium-launch-probe.log` was empty (0 bytes host-side AND in-image
  via debugfs). With the old 24s budget the report only landed by luck inside
  the sampler window. Archived:
  `20260704T143700Z-q0-kprofile-wait-gap-empty-probe-log-fail`.
- Fix: `wait_chromium_video_kprofile_done` in the smoke harness — polls
  `grep exec_ms <probe log>` (kprofile's final output line; short command for
  the serial line limit) with a `kprofile_seconds+30` deadline, called between
  `wait_chromium_video_sampler_done` and `capture_fbstat_stats`.
- Attempt 2 (wait-fix) PASSED the measurement: full 10.5KB kprofile report,
  8 wait polls, no wait-timeout warn, video played to `ended` (media 16.0s).
  First VALID full-window M7 numbers (see Scoreboard M7). Q0 fix status:
  timeout budget GOOD (window fully covered; `kprofile_timeout_hit=1` is
  EXPECTED in browser workloads — Chromium never exits on its own, kprofile
  always SIGTERMs at the deadline); pgroup labels GOOD; readahead counter
  GOOD and meaningful (9602/33694 fills = 28.5% via readahead; 71.5% of
  fills still synchronous at ~0.86ms/call, `read_page_ms=29016` — P3
  hypothesis reconfirmed with a trustworthy counter).
- REMAINING Q0 DEFECT (offline fix, no VM needed): `cpu_busy_ms=654934` /
  `cpu_total_ms=1042089` exceed wall*6CPUs (~205k) by ~5.1x. Root cause:
  `sched.c scheduler_yield` increments `busy/total_ticks` on EVERY scheduler
  invocation, not per timer tick (the in-code comment "every call is one
  timer tick" is false under load). No constant HZ converts these to time;
  only the busy/total RATIO is meaningful (62.8% this run). Fix kprofile to
  print `cpu_busy_ratio_pct` + raw invocation counts, or rename fields to
  `*_sched_ticks` and drop the ms conversion.
- Jiffies-loss caveat reconfirmed: kernel `elapsed_ms=34201` vs 40000ms wall
  = ~14.5% BSP tick loss under load (matches the Scoreboard rule).
- Parser follow-up (offline): the updated perf-video page emits
  `PERF-VIDEO metrics/RESULT` lines (no `tick` lines); post-evidence parsed
  `tick_count=0`/`result_fail_count=1` and missed rvfc fields. Teach the
  post-evidence parser the new format against the archived run.
- Archived: `20260704T144500Z-q0-kprofile-wait-fix-first-valid-m7-window`.

## P3 — Ext4 Read-Path Serialization (active perf lane)

Status 2026-07-04: first slice LANDED gated (user reprioritized it ahead of
the queue). Original hypothesis from the M7 archive: Chromium
startup/playback stalls are dominated by synchronous ext4 page fills under
the global per-fs lock (`ext4_pcache_read_page_ms=29981`,
`pages_filled=38686`, no useful readahead coverage before the Q0 counter
fix).

2026-07-04 P3 first slice IMPLEMENTED + GATED (opt-in, default OFF):
`ext4_read_page_direct=1` kernel cmdline gate. `ext4fs_read_page_direct`
(kernel `lwext4_port/ext4fs_file.c`) fills a single pcache page without
holding the esb mutex across the device wait: block mapping resolved under
`ext4fs_lock`, direct BIO submitted, lock released before `bio_await` —
the same two-phase pattern as `ext4fs_file_prefault`/
`ext4fs_submit_readahead`. Read-after-write coherence: dirty file data lives
in the lwext4 bcache (`ext4fs_pcache_write_page` dirties bcache blocks), so
the direct disk read happens ONLY when the block has no bcache entry; a
cached uptodate block is memcpy'd under the lock (no device wait), anything
else falls back to the locked `ext4fs_fill_page_from_ref`. Handles the
read_folio per-page fallback (pcn->data mid-folio) via folio-offset
arithmetic; holes zero-fill; EOF tails zeroed. The disabled `read_folio`
direct path's stale-data hazard is avoided by the bcache check (that path
had none).

Gate battery (flag ON, all PASS):

- Nographic: `forktest` rc=1 known table-exhaustion signature, `clonetest`
  rc=0, `cowtest` rc=0; cold sequential read healthy (4004 readahead pages,
  7 single fills, 16MB in 396ms); boot itself pages every ELF through the
  new path. Note: guest kprofile needs absolute exec paths (`/bin/bash`),
  and guest /bin/sh does not glob.
- Chromium-video kprofile A/B vs same-day flag-OFF baseline
  (`20260704T144500Z-q0-kprofile-wait-fix-first-valid-m7-window`), identical
  workload (33,693 vs 33,700 fills):
  `ext4_pcache_read_page_ms` 29,016 -> 14,469 (-50.1%; 0.86 -> 0.43
  ms/fill), `ext4_lookup_lock_wait_ms` 2,570 -> 1,232 (-52%),
  `ext4_fault_ms` 2,492 -> 848 (-66%), browser start (first chrome log ->
  PERF-VIDEO start) 10.63s -> 6.17s (-42%). Video unchanged within noise
  (presentedFPS 36.8 -> 35.7, dropPct 52.4 -> 51.0, decodedFPS ~61) —
  confirms M7's ceiling is the present path (P2 step 3), not I/O. No crash
  classes. Archive:
  `20260704T151500Z-p3-ext4-read-page-direct-chromium-kprofile-ab-pass`.
- KDE desktop-interaction active-sample (flag ON): `status_code=0` DONE,
  `first_visible_ms=11131` (M5 PASS), `konsole_wait_ms=2133` (M4 within the
  recorded noise band), `first_nonzero_ms=8541`, no crash classes. Archive:
  `20260704T153000Z-p3-ext4-read-page-direct-kde-active-sample-pass`.

Next P3 steps:

1. Default-flip `ext4_read_page_direct=1` per Guardrails: same-session
   default-on vs explicit default-off control, KDE active-sample + chromium
   launch-only guard, revert-ready one-liner. Needs user approval to
   promote.
2. Second slice (optional): batch the remaining non-sequential single-page
   fills (executable page-in pattern; the direct path halves their cost but
   batching would cut their count) — e.g. widen fault readahead below the
   64MB `vm_file_fault_ra_min_bytes` floor or feed fault-around windows
   into `submit_readahead`.

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

---

# ARCHIVED SNAPSHOT: active-work-plan.md as of 2026-07-08 (pre-compaction)

Archived verbatim before the 2026-07-08 rewrite that recut the live plan
around unfinished work. All lane histories, evidence chains, and verdicts
below remain authoritative history.

# Active xv6 Work Plan

Last updated: 2026-07-07 (current N8 branch has a completed Qt5Multimedia
opt-in A/B probe, plus a full desktop-interaction kprofile PASS after bounded
guest mouse and clean `LD_BIND_NOW` removal:
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000224Z-n8-guest-input-full-interaction-kprofile`.
It was `direct-launch-only=0`, active sample, bounded guest input
(`input_source=guest`, `monitor_path=disabled`, `host_cursor_sync_ms=0`,
bounded coordinates), hover PASS (`first_changed_ms=923`, diff 300), and
app/kprofile PASS (`konsole_wait_ms=1507`, `kprofile_timeout_hit=0`,
userpc stored=631/drop=0). KVM + virgl stayed real-GL with no software
fallback, and `LD_BIND_NOW` is absent from source, refreshed fs image, and
exact smoke tmp image. Newer N3/R9 guest-mouseinject proof
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T005222Z-n3-guest-mouseinject-kprofile-proof`
validates normal desktop reducers in
`scripts/gpu/kde-plasma-desktop-smoke.expect`: forced guest input,
monitor path disabled, host cursor sync off, guest cursor mode, pre-hover
taskbar/desktop settle, hover/app launch PASS, no lingering QEMU, and clean
diff check. Later user inspection still saw the cursor leave the VM; root
cause split is smoke harness vs normal launcher. The smoke harness now
records guest-only pointer evidence (`input_policy=guest-only`,
`pointer_injection=guest-mouseinject`, `host_cursor_sync=disabled`,
`qemu_monitor_input=0`; HMP/QMP input diagnostic-only), while
`scripts/launch/launch-gui.sh` defaults `QEMU_GTK_CURSOR_MODE=guest` and
`QEMU_GTK_SHOW_CURSOR=off` with overrides preserved. Post-patch validation
PASS
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T024621Z-n3-n8-guest-cursor-postpatch-kprofile`
supersedes invalid kprofile attempt `20260707T024333Z` (`probe_rc=1`):
pre/post QEMU scans empty; command proves `-enable-kvm`, virtio-vga-gl,
GTK `show-cursor=off`, no `virtio_gpu_host_cursor_only=1`; renderer is real
virgl (D3D12 NVIDIA GeForce RTX 4060 Laptop GPU), software fallback envs
unset; guest-only proof has `input_source=guest`, `input_policy=guest-only`,
`pointer_injection=guest-mouseinject`, `host_cursor_sync=disabled`,
`qemu_monitor_input=0`; bounded `/bin/mouseinject 11016 64223 0` on
1280x800/workarea 0,0-1279,799 hit guest pixel 215,783; hover PASS
`first_changed_ms=1048`; app/kprofile PASS (`konsole_wait_ms=2816`,
app-launch `elapsed_ms=4159`, `kprofile_elapsed_ms=3616`,
`kprofile_timeout_hit=0`, userpc 1052/0);
crash scan 0, scoped diff-check passed, scratch image deleted/no `*.img`.
M9 Chromium launch-only guard also passed at
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000512Z-n8-m9-chromium-launch-only-real-gl`:
`browser_seen`, render/drm fds, `gpu_init_error_count=0`, `fault_count=0`,
real GL env/renderer held, and no lingering QEMU. Offline branch scouts say to
avoid glibc/ELF-loader surgery: the loader hotspot is real and repeatable but
too ABI-sensitive; follow-up hwcaps/`LD_LIBRARY_PATH` probes were
measurement-only. XDG path-pruning A/B delta was
limited to `XDG_DATA_DIRS`/`XDG_CONFIG_DIRS` defaults relative to its pre-edit
control, but the broader dirty worktree/scoped files still contain prior N8
helper/harness edits, so do not read raw `git diff` breadth as XDG-only; preedit
archive
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T001509Z-n8-xdg-paths-preedit-direct-launch-kprofile`
and pruned archive
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T002116Z-n8-xdg-paths-pruned-direct-launch-kprofile`.
It reduced lookup churn (ENOENT cold/warm 2816/2396 -> 2658/2228,
`ext4_lookup_enoent` 2124/1848 -> 1973/1681,
`kubuntu-default-settings` 73/70 -> 0/0, `/usr/local/share` 2/2 -> 0/0,
local 97/94 -> 7/1) but did not improve readiness beyond noise:
`konsole_wait_ms` 1389/1298 -> 1399/1317, prompt 1541/1613 -> 1511/1558,
loader share stayed ~43-46%, and top symbols remained dynamic-loader work.
Post-verifier archive-local cheap evidence in the pruned archive:
`post-verify-rootfs-refresh.log` (rootfs-refresh rc=0),
`post-verify-qemu-pgrep.log` (no `qemu-system`; an initial self-match wrapper
attempt is retained as `post-verify-qemu-pgrep.selfmatch-superseded.log`),
`post-verify-xdg-rg.log` (old defaults absent/current defaults present), and
`post-verify-git-diff-check.log` (scoped `git diff --check`).
Loader/env measurement-only matrix used a temporary hook that was applied and
reverted exactly (`/tmp/xv6-worker-h/worker-h-temp-hook-only.diff`), restored
touched-file diff, passed user and rootfs-refresh builds, and left no lingering
QEMU. Valid KVM + virgl real-GL/no-software-fallback archives are
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T011159Z-n8-loader-env-control-direct-launch-kprofile`,
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T011412Z-n8-loader-env-hwcaps-mask0-direct-launch-kprofile`,
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T011905Z-n8-loader-env-ldpath-opt-last-direct-launch-kprofile`,
and
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T012101Z-n8-loader-env-ldpath-last-hwcaps-mask0-direct-launch-kprofile`;
the visible-timeout
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T011622Z-n8-loader-env-ldpath-opt-last-direct-launch-kprofile`
was invalid and rerun. Decision: do not promote hwcaps masking or
`LD_LIBRARY_PATH` reorder as an N8 responsiveness fix; current evidence says
M4 is dynamic-loader relocation/symbol work proper, not XDG, hwcaps, or simple
`/opt` path search. Offline loader-category attribution is now complete with
99.59-100% symbolization: relocation + lookup/hash proper dominate, while
mmap/open/path search is secondary, so the next N8 step stays offline/source
level until there is a concrete change to validate.
Branch decision: accept XDG pruning only as a minor reversible lookup-churn
cleanup, not an M4 responsiveness fix; continue N8 with deeper loader
relocation/lookup attribution or targeted probe design, then boot only to
validate a concrete change. Libimobiledevice no-device shim A/B also closed
no-promote: it removed libssl/libcrypto from Konsole maps but did not improve
readiness. N8 static scout decision: Konsole direct closure is ~134 objects,
~227,504 relocations, ~43,056 undefined dynsyms; top contributors include
Qt5Widgets 23,205, Qt5Quick 20,618, gallium 18,275, crypto 18,081, Qt5Qml
12,089, KIOWidgets 5,370, konsoleprivate 4,833, Qt5Multimedia 3,485, Solid
3,488. N8 NewStuff opt-in SONAME-stub initial + repeat A/B completed with no
default flip: exact 8-symbol KNS/KNSCore ABI surface from libkonsoleprivate
only, real NewStuff maps replaced by shim maps, and KVM+virgl real GL held.
Review says default env-absent paths are safe/no persistent rootfs effect, but
the C `set_kde_env` gate is probe-wide when
`KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`; the harness currently scopes staging
to Konsole-only direct-launch. Repeat weakly supports opt-in candidate only:
warm wait/prompt/direct/kprofile improved
1414/1674/2551/2049 -> 1297/1547/2396/1823, but cold was mixed and
openat/ENOENT worsened. Qt5Multimedia gated probe review (2026-07-07): keep
as scratch opt-in; no-go/revert is not warranted, but adjust before stronger
validation or promotion. Its gate is cleaner than NewStuff: when
`KDE_APP_LAUNCH_PROBE_QTMM_STUB=1`, only the forked Konsole child/subtree gets
the shim `LD_LIBRARY_PATH`; the parent `LD_LIBRARY_PATH` is unchanged, siblings
(terminal/dolphin/kate/kwrite/chromium) do not inherit it, and Chromium-only
or sample paths return before this path. Env-absent defaults have no
persistent rootfs/LD path, staging writes only the temp image, and
`LD_BIND_NOW` removal is unrelated/default no-shim. Proof is enough for a
narrow opt-in candidate only: six `Qt_5` QMedia imports from
`libkonsoleprivate`, no Qt5MultimediaWidgets, six-export SONAME shim, and A/B
archives
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T035129Z-qtmm-control-direct-launch-kprofile`
/
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T035327Z-qtmm-stub-direct-launch-kprofile`
show cold/warm direct deltas -803ms/-303ms with timeout 0. Weaknesses: shim
is still `/tmp`-sourced/built, load-only and not call-safe, call logging was
readiness-window/capped only, and full uncapped maps are missing. Next:
convert QtMM shim to a repo-reproducible opt-in fixture, then repeat/control
with full maps and final post-teardown call log before active-sample,
durable, or default consideration; keep NewStuff as weaker/mixed opt-in.
No glibc/ELF-loader surgery or default flips; always require kprofile metrics
and KVM + virgl real GL/no software fallback.
No commit/push yet. 07-04 compaction baseline: R5
root-caused + fixed, P2 ordered-pageflip default landed, Q2 real-GL
runtime-validated with the first default-path M9 PASS, P3 ext4 slice landed
gated, N2 ext4 default promotion attempted but NOT accepted, N3 R9 coordinate
and visible cursor probes passed, and N4/P1 cpumask+CR0/CR0-only attempts
stopped/reverted with no landing. All verbose evidence chains moved to the
history file and git log; this file holds current status, queue, rules, and
compact lane conclusions only.)

2026-07-07 new-session handoff: user is launching a new session; this is the
handoff state. Audit found no live QEMU/kprofile/KDE smoke processes, so no
running VM cleanup is currently needed. Preserve the dirty workspace; no
commit/push unless explicitly requested. Qt5Multimedia repo-reproducible
opt-in fixture work is partially staged but incomplete: untracked
`scripts/image/build-qtmm-shim.sh`, `scripts/image/xv6-qtmm-shim.c`, and
`scripts/image/xv6-qtmm-shim.map`; `scripts/gpu/kde-plasma-desktop-smoke.expect`
has QtMM staging/final-call-log/full-map support; last pointer
`build_x86_64_last_control_archive.path` points to
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T043906Z-n8-qtmm-repro-control-direct-launch-kprofile`;
later qtmm-repro-control runs are control-only failures/status 8; no matching
stub repeat or `*qtmm-final-call-log.txt` was found. Current authoritative
next action: first audit/finish-or-discard the partial QtMM fixture state,
then produce a repo-owned reproducible opt-in QtMM shim fixture and rerun
control/stub direct-launch kprofile with full uncapped maps and final
call-log. Requirements: KVM+virgl real GL/no software fallback, timeout 0,
userpc drops 0 when collected, app probe PASS, no crashes, no lingering QEMU;
keep all shims opt-in, with no default flips and no glibc/ELF-loader surgery.
Cleanup context: big cleanup already reclaimed ~459.6 GiB, but old generated
leftovers are cleanup candidates only after preserving current evidence
(known examples: old guest-mouseinject history image, old clock-domain smoke
image dir, `build-x86_64/qtmm-shim-check`).

2026-07-07 orchestrator audit (this session, decision recorded): the QtMM
repo-reproducible fixture is ALREADY materially in place, not merely partial.
Repo-owned `scripts/image/xv6-qtmm-shim.c` (6 `Qt_5` QMedia exports, raw
syscalls/`nostdlib`, per-call `/tmp/xv6-qtmm-shim-calls.log` logging tagged
`risk=unsafe_load_only`), `scripts/image/xv6-qtmm-shim.map`
(exactly 6 globals, `local:*`), and reproducible
`scripts/image/build-qtmm-shim.sh` (`SOURCE_DATE_EPOCH=0`,
`--build-id=none`, `--no-undefined`, SONAME + forbidden-NEEDED guards,
sha256/readelf/nm proof emit) all exist. The harness
`scripts/gpu/kde-plasma-desktop-smoke.expect` already wires
`stage_qtmm_stub_shim_if_enabled` (builds via the repo helper, stages to
`/opt/xv6-kde-abi-libs/qt5multimedia-shim/libQt5Multimedia.so.5`), full
uncapped maps (`KDE_INTERACTION_DIRECT_LAUNCH_FULL_MAPS_LOG`), and the
post-teardown final call log (`*.qtmm-final-call-log.txt`,
`kde_direct_launch_qtmm_call_log`). ROOT CAUSE of the 4 interrupted
`20260707T04{2949,3305,3727,3906}Z-n8-qtmm-repro-control` runs: all
`status_code=8` = `sample_desktop-interaction-visible_1-artifact-timeout`
(all-black fb `nonzero=0` at the visible-sample stage) = Failure Mode 19
visible/artifact-timeout flake, NOT a fixture defect — they carried the
flaky `desktop-interaction-latency` visible reducer instead of
direct-launch-only mode. Decision: fixture audited essentially complete;
next is offline build/validate of the fixture, then a control+stub
DIRECT-LAUNCH-ONLY kprofile A/B (skip the flaky visible stage) with full
maps + final call-log, requiring real GL, timeout 0, userpc drop 0, app
PASS, no crash, no lingering QEMU. CONFIRMED PASSING RECIPE (from the
035327Z stub / 035129Z control that predate this fixture):
`KDE_SMOKE_INTERACTION_DIRECT_LAUNCH_ONLY=1`, `direct_launch_repeats=2`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, `input_source=guest`,
`QEMU_GPU=virtio-vga-gl-primary`, `USE_KVM=1`,
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_APP_LAUNCH_PROBE_QTMM_STUB` 0=control / 1=stub.

2026-07-07 A/B run progress (this session): offline fixture build GO
(deterministic sha256 `c0f2b707ce14c2bca305d164842cf8597d47cabe1468703575a5147e6138e77a`,
SONAME `libQt5Multimedia.so.5`, 6 `Qt_5` exports, no DT_NEEDED,
`git diff --check` clean, `.c`<->`.map` 1:1). CONTROL arm
(`QTMM_STUB=0`, direct-launch-only) PASSED: archive
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T0529Z-qtmm-repro-control-direct-launch-kprofile-fixturev2`
(status_code=0 DONE, real GL D3D12/virgl/renderD128 no-software, both
direct-launch probes `result=PASS probe_rc=0` cold/warm 18180/15341ms,
full-maps + `qtmm-final-call-log status=ABSENT` correct for no-shim, zero
crash markers). STUB attempt 1 CRASHED at session-readiness BEFORE
Konsole/shim reached: `pid 68 wireplumber: exception 13 (#GP) rip=0x7fffff666364`,
status_code=3 `kde-session-ready-crash`; shim built OK (repo-helper sha256
matched, staged) and real GL held, but no full-maps/per-probe output.
Classification: shim-INDEPENDENT session-startup crash (wireplumber/PipeWire
does not load the Konsole-scoped shim `LD_LIBRARY_PATH`; control + prior
035327Z stub both passed clean); `rbx=0x302d7265626d756c` = ASCII `"lumber-0"`
= R5-family foreign-bytes-in-register signature shape => candidate
CONTINUOUS-R5-WATCH datapoint, not a QtMM regression. Evidence preserved:
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T0532Z-qtmm-repro-stub-direct-launch-kprofile-fixturev2-SESSION-CRASH`.
Bounded stub rerun PASSED (crash was an independent flake): archive
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T0536Z-qtmm-repro-stub-direct-launch-kprofile-fixturev2`
(status_code=0 DONE, both direct-launch probes `result=PASS probe_rc=0`,
real GL held, no crash markers, no lingering QEMU).
A/B RESULT — DURABLE-CANDIDATE PROOF ACHIEVED (repo-reproducible fixture,
all gates met). konsole_wait_ms cold/warm: control 5920/2885 -> stub
2094/2032 (delta -3826ms ~65% cold, -853ms ~30% warm); bash-prompt
6357/3166 -> 2210/2365; kprofile_elapsed 16720/13102 -> 9553/9907;
kprofile_cpu_busy_ms 105194/86923 -> 65673/67201; userpc stored
3869/3053 -> 1743/1869, dropped 0 in all four; kprofile_timeout_hit=0 all;
sys_openat 5917/5060 -> 4902/4776 (ext4 ENOENT +237 both, minor). MAP
CONTRAST PROVEN: control maps real `/usr/lib/x86_64-linux-gnu/libQt5Multimedia.so.5`
+ `libpulse.so.0`/`libpulsecommon-16.1.so`; stub maps only
`/opt/xv6-kde-abi-libs/qt5multimedia-shim/libQt5Multimedia.so.5` with real
Qt5Multimedia + entire libpulse/pulsecommon closure ABSENT (unique .so
219 -> 208, -11 objects). CALL-SAFETY: stub final-call-log `status=ABSENT`
cold+warm => shim is LOAD-ONLY (6 fake QMedia bodies never invoked; ABI risk
not exercised in this path). Provenance PASS (repo-helper build/stage logs,
sha256 `c0f2b707...` match, SONAME + 6 exports + no NEEDED). Both arms real
GL (virgl/renderD128/D3D12 NVIDIA, no software fallback), app PASS, zero
crash markers, no lingering QEMU. REPEAT-N SWEEP (N=3/temperature) OVERTURNS THE 1x1 COLD WIN. Ran 2 more
interleaved runs per arm (all real GL D3D12/virgl, 0 software hits, DONE,
0 crash markers, no lingering QEMU). Archives:
`20260707T0548...T0552Z-qtmm-repro-sweep-{stub,control}-rep{2,3}-fixturev2`.
konsole_wait_ms cold/warm matrix (control rep1 5920/2885 was a first-boot
cold-cache OUTLIER): control cold [5920,1504,1593] median 1593, warm
[2885,1420,1199] median 1420; stub cold [2094,1897,1495] median 1897, warm
[2032,1187,1553] median 1553. MEDIAN delta stub-control = +304ms cold,
+133ms warm (stub marginally SLOWER); excluding the first-boot control
outlier, control cold mean 1548 vs stub 1696. CONCLUSION: the dramatic
-3826ms 1x1 cold "win" was a cold-cache artifact of the control's first
boot; once host-cache state is controlled, the QtMM load-only stub shows NO
reliable konsole_wait_ms improvement (within run-to-run noise, if anything
marginally slower). DECISION: the repo-reproducible opt-in QtMM fixture is
VALIDATED and SAFE (deterministic build, map replacement proven, load-only
/ ABI-risk-not-exercised, all gates green) and stays as a repo-owned opt-in
via `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1`, but it is NOT a demonstrated M4
responsiveness win and is NOT promoted to active-sample/default. QtMM thus
JOINS the other single-library trims (XDG, hwcaps, libimobiledevice,
NewStuff) that reduce map/lookup churn but do not move readiness beyond
noise — reinforcing that the M4 bottleneck is dynamic-loader
relocation+lookup/hash proper across the whole Qt/KF5 closure, not any one
trimmable library. NewStuff remains the weaker/mixed opt-in; no further
single-library-stub trims are worth pursuing as M4 fixes without new
evidence. The stub-attempt-1 wireplumber #GP (ASCII `"lumber-0"` in rbx) is
filed as a CONTINUOUS-R5-WATCH datapoint at `...-fixturev2-SESSION-CRASH`.
No commit/push, no default flips, dirty worktree preserved.

2026-07-07 ld.so.cache A/B (this session) — VERDICT: correctness/cleanliness
win, NOT an M4 responsiveness lever. Grounding: the image ships NO
`/etc/ld.so.cache` and NO multiarch `/etc/ld.so.conf`, forcing ld.so into
linear dir probing. Added an OPT-IN gated `ldconfig` step to
`scripts/image/make-rootfs.sh` (`XV6_ROOTFS_LDSOCACHE=1`, default OFF,
post-overlay/pre-mkfs; bakes multiarch `ld.so.conf.d` + `ld.so.cache`,
emits entry-count evidence) — DEFAULT NOT FLIPPED, `make-rootfs.sh` change
left dirty/uncommitted. Built a cached treatment image (1352 entries incl
Qt5/KF5) and ran control(no-cache) vs treatment(cached) direct-launch-only
kprofile, N=2/arm, all real GL / PASS / no crash / no lingering QEMU;
default fs.img restored to no-cache after. RESULT: cache IS consulted
(`/etc/ld.so.cache` ENOENT 6->0) and cut syscalls structurally — sys_openat
-17%/-23% (cold/warm), ext4_lookup_enoent -36%/-51% (4551->3790 cold,
3996->2543) — reproducible both reps/both phases. BUT konsole_wait_ms FLAT:
control cold 1488/1492 warm 1189/1298 vs treatment cold 1498/1493 warm
1178/1192 (deltas within noise). Why: openat time saved is only ~50-67ms of
kernel `sys_openat_ms` out of ~1200-1500ms wait; userpc CPU rollup unchanged
(relocation+lookup_hash ~37%, mmap_open/path-search 1-3% and flat). Residual
ENOENT is `LD_LIBRARY_PATH` probing `/opt/xv6-kde-abi-libs` first (146
probes) which the cache cannot short-circuit; trimming LD_LIBRARY_PATH would
recover it but is also syscall-cleanliness, not readiness. Archives:
`20260707T17{1835,1942}Z-ldsocache-control-rep{1,2}` /
`20260707T17{2337,2447}Z-ldsocache-treatment-rep{1,2}`; cache evidence in
`kde-plasma-desktop-smoke-history/ldsocache-experiment-evidence/`. LANE
CONCLUSION: ld.so.cache joins single-library trims + XDG + hwcaps as a
map/syscall-churn cleanup with NO konsole_wait_ms effect — EMPIRICALLY
confirming the M4 bottleneck is loader relocation+symbol-hash-lookup CPU
over the ~219-object Qt/KF5 closure, not I/O or path search. The only
remaining lever that touches that CPU cost is zygote/preload (fork WITHOUT
execve so relocations are inherited COW) — but konsole is not a
kdeinit-loadable module (`libkdeinit5_konsole.so` absent), so it needs
konsole rebuilt as a loadable module or a bespoke preloader (high effort,
uncertain payoff), or relinking the closure (infeasible for prebuilt distro
libs). Cheap guardrail-respecting M4 levers are now EXHAUSTED. Optional
follow-up: ship ld.so.cache (+ LD_LIBRARY_PATH trim) as a default via the
full battery IF a syscall-cleanliness win is wanted on its own merits — but
it is not a responsiveness fix. No commit/push, no default flips, dirty
worktree preserved.

2026-07-07 GAP-TO-LINUX ATTRIBUTION (offline, from existing ldsocache
archives) — MAJOR REFRAME: the M4 launch cost is NOT dominated by userspace
loader compute; it is dominated by KERNEL VFS/ext4-lookup + page-fault cost.
Evidence from the direct-launch kprofile counter dumps (control warm/cold):
`sys_openat_ms=566/643` over `sys_openat_calls=4369/4559` = ~130-141us PER
openat (~50-100x native's ~1-3us), i.e. ~566ms of kernel openat time = ~40%
of the 1189ms warm konsole_wait (`sys_openat_lookup_ms=285/341`,
~65-75us/lookup — the ext4/lwext4 directory-walk + ENOENT-probe path).
Plus `vm_file_faults=40140/40469` page faults per launch, of which only
~3134 hit ext4 (`ext4_fault_calls`, `ext4_fault_ms=28-39` = tiny I/O), so
~37k are MINOR (cached) faults whose per-fault guest trap+handler cost is
uncounted. `sys_poll_*_ms` (poll_blocking 24k-113k ms) is BLOCKING wall
time (idle waiting), NOT CPU — excluded. Warm phase breakdown: konsole
spends 0->754ms just reaching PTY-open (pre-PTY Qt/KF5/loader+fault storm)
out of 1189ms. CRITICAL METHOD NOTE: the prior "~half ld-linux
relocation/lookup" attribution was USER-PC sampling = USERSPACE-ONLY; ticks
landing in kernel mode (openat, fault handler) are not user-PC-sampled, so
that breakdown UNDERCOUNTS kernel VFS/fault time. True picture: a large
share of wall-time is kernel VFS/ext4-lookup + minor-fault handling that
runs ~50-100x costlier per-op than native Linux, amplified by the launch's
~4.4k opens + ~40k faults. THIS is why xv6 (~1.2s warm) is 4-6x slower than
a native Linux VM (~0.3s) at the SAME loader work over the SAME closure:
not the loader algorithm, not loader config, but per-syscall/per-fault
guest cost. LANE PIVOT: the M4 responsiveness lever is now firmly the
KERNEL syscall/VFS/fault path — ext4/lwext4 openat-lookup cost per call
(P3 territory, partially landed) and the minor-page-fault handler cost
(M2/M3 syscall/TLB territory), and/or cutting the storm size (fewer opens:
LD_LIBRARY_PATH trim + cache; fewer faults: prefault/zygote).
MICROBENCH QUANTIFIED (nographic, KVM; getpid_ns 1659-1914 matches M2
1.65-1.94us, tlb_amplification 3075/3094 @1024pg matches M3 band => bench
sane): per-bare-syscall ~1.7us; per-minor-fault ~3-5us central (bounded
[1.7us bare-trap, 8.8us = measured ext4_fault_ms 28/3186]; no direct
per-fault microbench exists in-repo -- mmaptest/mmapbigfile are correctness
only). Storm arithmetic per warm launch (~37,283 minor faults = 40469-3186):
openat 4369x130us = 566ms (measured) + minor-faults 37283x(3-5us) =
112-186ms + ext4 I/O 28ms = KERNEL SUBTOTAL ~706-780ms = 59-66% of the
1189ms warm konsole_wait (>55% even at the 1.7us low bound), and that
EXCLUDES all other launch syscalls (read/close/stat/mmap, each ~1.7us +
TLB-amp). Same storm on native Linux ~30-60ms (dcache openat ~1-5us, minor
fault ~0.5-1us), so xv6's kernel-side EXCESS ~650-720ms is the MAJORITY of
the ~890ms gap (1189-300). VERDICT: kernel syscall+fault cost is FIRST-ORDER
-- the dominant reason xv6 app-launch is 4-6x slower than native. Single
biggest amplifier: openat at ~130us/call (~26-130x native), 566ms alone, of
which sys_openat_lookup_ms=285ms is ext4/lwext4 directory-walk+ENOENT probe
(P3/ext4 lane). Userspace loader compute is at most secondary (~400-480ms
residual, much itself syscall/fault wait). HIGHEST-LEVERAGE M4 TARGETS, in
order: (1) openat/VFS ext4-lookup cost per call (P3 read-path already cut
fault I/O ~50%; the LOOKUP path -- dir-walk + negative-lookup caching -- is
the next slice); (2) minor-page-fault handler cost (M2/M3 trap/TLB/pcache
radix path); (3) shrink the storm (LD_LIBRARY_PATH trim to cut ~4.4k->fewer
opens; prefault/zygote to cut faults). Loader-config levers (ld.so.cache,
single-lib trims) are CLOSED as non-responsive. No commit/push, no default
flips, dirty worktree preserved.

2026-07-07 M4 SLICE IN PROGRESS (kernel, gated, NOT yet validated) —
negative-dentry-cache honor fix. Source scout found: xv6 VFS already has a
full positive+negative dcache (`kernel/kernel/vfs/dcache.c`, committed
8299114) with seq-based invalidation wired into all 8 VFS mutations, BUT the
consumer `vfs_ilookup` (`kernel/kernel/vfs/inode.c:642`) only early-returns
on 0/-ENOMEM, so a negative HIT (`-ENOENT`) falls through and re-walks ext4
under the per-mount esb mutex anyway -- the cache DETECTS but does not AVOID
(explains why warm ext4_lookup_enoent stays ~3685). Fix (UNCOMMITTED kernel
working tree, gate `vfs_neg_dcache=1` DEFAULT OFF): honor the `-ENOENT`
negative hit + `sb->valid` guard on the negative branch. Adversarial review
= NO-GO until 2 gate-ON false-negative blockers fixed: (B1)
`__vfs_dcache_bump_dir_seq` called AFTER `vfs_iunlock` in all 8 mutation
sites -> commit-visible-before-bump window returns stale negatives; fix =
move bump inside the lock (also fixes a pre-existing positive-stale bug).
(B2) procfs/sysfs/devtmpfs expose names without bumping seq -> stale
`/proc/<pid>` negatives; fix = per-sb no-neg-dcache flag, skip store+honor
for synthetic sb. Plus should-fix: xv6fs_lookup caches transient block-read
errors as ENOENT (ext4 clean). Blocker fixes being applied; then RE-REVIEW
before boot, then gated A/B (microbench + warm konsole kprofile: expect
ext4_lookup_enoent down, vfs_lookup_negative_hits up, sys_openat_lookup_ms
~285->~110, konsole_wait_ms ~-170ms) + regression battery (fork/clone/cow
nographic + KDE active-sample). FIRST M4 lever targeting real kernel
critical-path time. No commit/push, no default flips.
VALIDATION RESULT (2026-07-07): re-review returned GO but the boot DISPROVED
it — gate-ON (`vfs_neg_dcache=1`) PANICS at boot:
`kernel/kernel/proc/thread.c:468 init_entry: exec /bin/init failed` (exec
returned -1) ~14s uptime, before KDE. Gate-OFF (`vfs_neg_dcache=0`, SAME
kernel) booted clean (crash=0, baseline counters ext4_lookup_enoent
~3986/3693, konsole_wait 1491/1192) -- so the bump-inside-lock reorder is
safe, but HONORING negatives yields a FALSE NEGATIVE for /bin/init (a
boot-critical existing file reported missing). Failure class the two reviews
missed: boot/mount/early-VFS ordering (a negative cached before the file is
visible, or across a mount, honored later). Archives:
`20260707T192438Z-negdcache-control-rep1` (gate-off, qtquick-accel-policy
FAIL but crash=0 -- likely GL flake, counters valid) and
`20260707T192626Z-negdcache-treatment-rep1` (gate-on, launch-crash, 7 crash
markers, PANIC init_entry). Default remains SAFE (gate default-OFF, no flip).
Root-cause in progress; classify QUICK-FIX (invalidate negatives across
mount / don't cache against not-ready root) vs DEEP model gap => if deep,
PARK negative-honor and keep only the bump-inside-lock fix (which is a
genuine pre-existing positive-cache-stale race fix and passed gate-OFF).
No commit/push, no default flips.
ROOT CAUSE = QUICK-FIX (sentinel overload, NOT stale cache): `..` symlink
resolution. `__vfs_dcache_lookup` returned `-ENOENT` for FOUR cases
(bad-args, ".", "..", genuine negative hit); the gate made `-ENOENT`
load-bearing, so honoring the ".." sentinel broke ".." resolution -> the
`/lib64/ld-linux-x86-64.so.2 -> ../lib/...` symlink failed -> exec failed ->
init panic. FIX (landed in kernel working tree): dcache.c returns `-EAGAIN`
for the 3 sentinel/fall-through cases so `-ENOENT` UNIQUELY means genuine
negative hit; single caller vfs_ilookup treats -EAGAIN as a miss. Nographic
re-verify: gate-ON now boots to userspace (root:/# shell, no PANIC),
gate-OFF clean. KDE A/B RE-RUN (fixed kernel, real virgl/D3D12 GL held,
LIBGL_ALWAYS_SOFTWARE=0): treatment `crash=0` (fix holds under full desktop).
MECHANISM PROVEN (deterministic) BUT NO RELIABLE M4 WIN (N=2, corrected):
ext4_lookup_enoent warm is DETERMINISTICALLY 194 in treatment (both reps
identical) vs control 3685/5236 = -95%; sys_openat_lookup_ms warm 338/330 ->
238/285 = ~-70..-100ms consistently. BUT konsole_wait_ms did NOT reliably
improve: cold control 1496/1488 vs treatment 1299/1600 (mean delta -42ms
with a +-300ms spread; the rep2 -197ms did NOT reproduce -- rep3 was
+112ms), warm control 1195/1190 vs treatment 1190/1186 (flat). The N=1
-197ms "win" was a lucky draw, same 1x1 trap as the QtMM cold-cache artifact.
Archives `20260707T195026Z/195127Z-negdcache-{control,treatment}-rep2` and
`.../rep3`. KEY REFINEMENT: the saved openat/lookup kernel time (~100ms,
real) is LARGELY OFF the serial critical-path-to-shell-ready -- it happens
in parallel across konsole's many threads/processes -- so cutting it does
NOT proportionally cut konsole_wait_ms. This corrects the gap-attribution
takeaway: sys_openat_ms being ~40% of total launch TIME does NOT mean it is
40% of the serial critical path; much of the syscall/fault cost is parallel.
So the neg-dcache honor JOINS ld.so.cache/XDG/single-lib trims: a real,
deterministic structural syscall-churn reduction with NO reliable
konsole_wait_ms effect. The change itself is a correct, valuable VFS fix
(the negative cache was built but non-functional; plus the bump-inside-lock
fixes a pre-existing positive-stale race), worth keeping as gated opt-in, but
NOT an M4 lever and NOT default-promoted (also blocked by the eviction/
no-flush residual: lookup_seq resets to 0 on inode eviction, dcache not
flushed on evict/unmount). NOTE: all 4 runs failed qtquick-accel-policy
(plasmashell QtQuick -> SOFTWARE backend) while the GPU renderer stayed real
virgl/D3D12; does not affect the konsole QWidget metric, identical both arms;
appeared on this kernel but not earlier ldso runs (likely host-WSL-GL drift,
separate issue). BIGGER IMPLICATION FOR M4: since even the kernel-syscall
lever doesn't move konsole_wait_ms, the SERIAL critical path to shell-ready
(relocation of first-needed libs + Qt init before PTY open, ~0-754ms pre-PTY
window) is the real limiter -- reducing PARALLEL syscall/fault cost won't
help; only cutting the SERIAL relocation/init chain (zygote/preload, or
fewer serial dependencies) would. No commit/push, no default flips.

2026-07-07 PIVOTAL MEASUREMENT (LD_DEBUG=statistics on konsole main pid,
real KVM+virgl GL) -- THE LOADER IS NOT THE M4 BOTTLENECK; Qt/KF5 APP INIT
IS. glibc runtime-linker stats for konsole main (219 .so, warm, reproduced
within 3%): total startup in dynamic loader = 57.7M TSC cycles @2688MHz =
~21.5ms; time for relocation 32.2M cyc = ~12ms (40,885 symbol relocs, 34,472
= 84% from lookup cache, 141,292 relative relocs = 182k eager; ~45k
JUMP_SLOT PLT relocs deferred by lazy binding); time to load objects ~8.5ms.
=> ld.so pre-main total ~22ms = ~3% of the ~754ms pre-PTY serial window.
The remaining ~730ms (~97%) is Qt/KF5 APPLICATION INIT after the loader
hands control to main() and before konsole opens the PTY (Qt plugin dlopen,
QML/scenegraph + EGL/Wayland/virgl context bring-up, DBus, theming/icons/
fonts, KConfig/KIO). THIS OVERTURNS THE ENTIRE N8 PREMISE: every lever this
session (XDG, hwcaps, single-lib stubs incl QtMM, ld.so.cache, neg-dcache
lookup honor) targeted the dynamic LOADER, which is only ~3% of the serial
path -- so of course none moved konsole_wait_ms. The earlier "~half ld-linux
relocation/lookup" user-PC reading was AGGREGATE across all threads/procs +
plugin-dlopen-during-Qt-init (PC in ld.so but charged to app time), NOT the
~22ms serial pre-main relocation. Archive of the LD_DEBUG run + stats files
in scratchpad; fs.img restored (konsole wrapper removed), no lingering qemu,
real GL held (virgl D3D12 NVIDIA). NEW M4 DIRECTION: the target is Qt/KF5
init reduction on konsole's SERIAL pre-PTY path. Next measurement: break
down the ~730ms Qt-init window (serial syscalls openat/read/dlopen [sys_openat_ms
was ~566ms/launch -- likely mostly Qt-init config/plugin/font opens, serial],
minor faults, DBus/poll waits, vs compute) to find the biggest serial
component. Candidate levers (all app/framework-level, NOT loader): prune Qt
plugin scanning/QT_PLUGIN_PATH, cut config/theme/icon/font file opens
(caching), reduce DBus round-trips, or -- since each xv6 openat is ~130us
(~50-100x native, ext4-lookup-bound) and Qt init does thousands SERIALLY --
reduce per-openat/per-read kernel cost or the serial op COUNT. This is the
first correct localization of the true M4 bottleneck. No commit/push, no
default flips.

2026-07-07 QT-INIT SERIAL DECOMPOSITION (kernel-accounted pre-PTY poll
attribution + dlopen/wayland trace, real virgl GL, instrumentation
non-perturbing: warm konsole_wait 1420 == shipped median) -- THE M4
BOTTLENECK IS COMPOSITOR/VIRGL ROUND-TRIP WAIT, and it is THE SAME ROOT
CAUSE AS M7. Warm pre-PTY window ~984ms split:
konsole_prepty_poll_total_ms=595 (~60%) = BLOCKING WAIT on round-trips:
Wayland compositor roundtrips 408ms (14 calls, ~29ms each -- native is <1ms;
each is guest->QEMU->host-D3D12->reply virgl latency; konsole cannot create
its window / open the PTY until xdg-surface configure returns) + QDBus
roundtrips 187ms (22 calls, ~8.5ms). dlopen plugin storm 149ms (~15%, 64
dlopens) dominated by IMAGE-CODEC plugins a terminal never needs: kimg_avif
45ms(!), kimg_exr 11ms, kimg_jxl 9ms, kimg_heif/raw ~2-5ms => avif/jxl/exr/
heif/raw ~70-100ms; plus KDEPlasmaPlatformTheme 16ms, qt-wayland-egl 10ms,
breeze 8ms. Residual ~23% = Qt/KF5 compute + initial-closure reloc (~22ms) +
minor faults. The ~566ms/launch sys_openat_ms is WHOLE-LAUNCH multi-process,
runs largely PARALLEL and is mostly OFF this serial path (explains the
neg-dcache null result). VERDICT: the biggest M4 lever is the SAME as M7 --
reduce virgl round-trip latency and/or the count of synchronous Wayland
roundtrips in Qt-Wayland client init (GPU/compositor path, N1/M7 territory,
NOT the loader which is confirmed ~15% and off the WAIT path). SECONDARY
CHEAP WIN: prune the imageformats plugin scan for konsole (avif/jxl/exr/heif/
raw ~70-100ms reclaim, zero function loss for a terminal). M4 lever ladder,
final: (1) virgl round-trip latency / fewer synchronous Wayland roundtrips
(~41%, hard, unifies with M7); (2) DBus roundtrip reduction (~19%); (3)
imageformats-plugin prune (~10%, cheap/safe/measurable); loader levers CLOSED
(~3% initial + ~15% dlopen, and dlopen is mostly the imageformats storm =
lever 3). No commit/push, no default flips.

2026-07-07 VIRGL/COMPOSITOR LANE OPENED (unifies M4 app-launch + M7 video
under one root cause: guest<->host GL round-trip cost). Session artifacts
COMMITTED (kernel 0cf817d gated neg-dcache; top fd38f5e ld.so.cache gate +
findings; 3 commits ahead of origin, UNPUSHED). The ~408ms/14-roundtrip
Wayland WAIT is the top M4 lever; FIRST diagnostic (in progress, offline
from the existing wayland-trace): disambiguate WHY each roundtrip is ~29ms --
(H-A) raw virgl round-trip latency (guest->QEMU->host-D3D12->reply; = N1
lane's host-GL-retire back-pressure) vs (H-B) KWIN FRAME-CLOCK GATING (kwin
dispatches Wayland client events only at its ~30Hz frame boundary, so each
sync waits ~1 frame ~= 29-33ms; matches the parked "KWin frame-clock
hypothesis"). The 29ms ~= one 30Hz frame is suggestive of H-B. Fixes differ:
H-A -> kernel virtio-gpu/virgl round-trip path (hard, N1); H-B -> make kwin
service Wayland protocol events off the frame clock / immediate socket
dispatch (kwin event-loop, potentially far more tractable and would speed
EVERY app launch + interaction). Next: confirm H-A vs H-B, then scope the
matching fix. Cheap parallel win still available: imageformats-plugin prune.
No commit/push beyond the 3 landed, no default flips.

2026-07-07 ROOT CAUSE FOUND (decisive, reproduced in both current archives)
= VERDICT H-B, NOT H-A. Client-side per-phase Wayland trace: pure protocol
roundtrip (registry/globals wl_display.sync) = ~1ms (transport FAST; raw
virgl round-trip latency REFUTED). The slow waits are ONLY the
frame/repaint-scheduled ones: first xdg-configure 12-71ms, frame-callback
58-139ms. Presents during bring-up are SECONDS apart, not a 30Hz clock.
SMOKING GUN in kde-session-kwin.log (both negdcache rep2 arms):
`kwin_scene_opengl: Creating the OpenGL rendering failed: "Could not create
gbm device"` then `"Could not initialize egl"`; `kwin_wayland_drm: Atomic
Mode Setting disabled on GPU /dev/dri/card0 because of cursor offset issues
in virtual machines`; `kwin_wayland_drm: Failed to find a working setup for
new outputs!`. So KWIN RUNS ON A DEGRADED NON-ATOMIC/NON-VSYNC DRM OUTPUT
PATH: output-side GBM/EGL init fails + atomic modeset disabled (VM
cursor-offset heuristic), so kwin's RenderLoop has NO present-completion/
vblank clock and free-runs on a coarse fallback timer -- every newly-mapped
surface waits several idle->schedule->render->present cycles (68-136ms) for
its first frame. THIS is the true bottleneck and it UNIFIES M4 (each konsole
init = ~14 frame-gated roundtrips), M7 (no vsync/present clock), R9 (the
atomic-disable trigger), and general interaction latency. LEVERS ordered:
(1) fix kwin's output DRM/KMS path so it gets a real present clock --
investigate WHY "Could not create gbm device"/"Could not initialize egl"
(kwin output GBM/EGL backend on the guest virtio-gpu DRM node) AND the VM
cursor-offset atomic-disable (virtio-gpu cursor plane, R9); atomic modeset +
proper vblank speeds EVERYTHING. Lives in the guest virtio-gpu DRM/KMS/GBM/
EGL present+cursor path (kernel + mesa) + possibly a kwin RenderLoop
fallback-tick fix. (2) interim: give kwin RenderLoop a ~60Hz software present
tick instead of the coarse fallback. (3) cheap parallel: imageformats prune
(~70-100ms). RESIDUAL to settle with a kwin-SIDE trace (next boot): pure
scheduling (fix RenderLoop) vs per-repaint virgl-fence stall (fix kernel
virgl fence) -- LD_PRELOAD kwin_wayland timestamping libwayland-server
dispatch + xdg_surface.configure + wl_surface.frame sends +
RenderLoop::scheduleRepaint + virtio-gpu pageflip/fence completion. NET
SESSION RESULT: "desktop feels slow" is now a precise UNIFYING root cause --
kwin has no vsync/present clock because the guest virtio-gpu DRM output path
(GBM/EGL/atomic/cursor) is degraded; ALL prior loader/syscall/lookup lanes
were off-target. Redirects+unifies N1/M7/R9/M4 into ONE lane: the guest
virtio-gpu DRM present+cursor path. No commit/push beyond the 3 landed, no
default flips.

2026-07-07 PRESENT-CLOCK LANE DIAGNOSIS COMPLETE (opus worker, 1 boot,
fs.img restored, no lingering qemu, real GL held: kwin recovered to virgl
D3D12 on its 3rd output attempt). Q1a GBM/EGL fail chain: TRANSIENT
first-attempt failure, recovers on retry -- root cause is `drmGetDevice2`
returning no device info (node=-1; xv6 DRM node exposes no PCI/sysfs
topology), so Mesa uses a fragile metadata-less fallback (local mesa patch,
driver name `virtio_gpu` at kernel fb_drm_core_kms.c:617) that loses a
readiness race on attempt 1-2. DRM ioctl surface is NOT the gap (GET_CAP
incl DUMB/PRIME/atomic fb_drm_core_kms.c:694, CREATE/MAP/DESTROY_DUMB
fb_drm_dispatch.c:670, PRIME :518, ADDFB2+modifiers). Fix = provide
drmGetDevice2 metadata (PCI bus/vendor/device) for card0. Q1b atomic-disable:
image kwin is 5.27.11; `isVirtualMachine()` driver-name match disables AMS
UNCONDITIONALLY in 5.27 (the CURSOR_PLANE_HOTSPOT-cap rescue is 6.x-only;
NO force-atomic env exists in 5.27) -- while the kernel ALREADY accepts the
hotspot cap (drm_core.c:148, commit 1c2113c), exposes PRIMARY+CURSOR planes
(fb_drm_core_kms.c:360-362), delivers hotspot (fb_drm_kms_properties.c:475),
advertises DRM_CLIENT_CAP_ATOMIC (drm_core.c:145). So atomic is unreachable
on kwin 5.27 regardless; ALSO the kernel atomic path queues NO
DRM_MODE_PAGE_FLIP_EVENT (fb_kms_atomic.c:441-689) while legacy does
(:388-434) -- must be fixed before atomic is ever viable. Q2 TRACE VERDICT:
SCHEDULE-DOMINATED. kwin (legacy path confirmed: 32 PAGEFLIP, 26 CURSOR, 0
ATOMIC ioctls) has per-present cost only 0.4-1.7ms damage / 5-27ms full,
but inter-repaint idle gaps 30-530ms (2-32 vblanks) exactly when new
surfaces appear; legacy flip-complete is delivered SYNCHRONOUSLY in the
ioctl with a grid-snapped <=16ms-stale timestamp (fb_kms_atomic.c:403-434),
so RenderLoop free-runs with no real phase. BONUS AMPLIFIER: CURSOR ioctls
block up to 32ms (synchronous virtio_gpu_user_set_cursor) = direct hover
latency. IMPLEMENTATION SLICES (ordered): (1) pace legacy flip-complete
events to the synthetic-vblank edge (defer queue+notify to the next 60Hz
tick using existing gpu_kms_vblank_period_ns fb_drm_core_kms.c:1071,
gpu_kms_sample_vblank_locked :1274, gpu_drm_event_queue_locked/notify_read
:1194/:1164 + a periodic flush) => real phase-accurate present clock for
kwin RenderLoop; GATED default-off, then A/B konsole_wait_ms + frame-callback
latency. (2) add PAGE_FLIP_EVENT queuing to gpu_drm_mode_atomic (mirror
:388-434) -- contained, unblocks future kwin 6.x atomic. (3) async cursor
updates so MODE_CURSOR/CURSOR2 never block 32ms => hover latency win.
Start slice 1. No commit/push beyond the 4 landed, no default flips.
SLICE 1 IMPLEMENTED + REVIEWED (2026-07-07, kernel working tree,
UNCOMMITTED): gate `virtio_gpu_vblank_paced_flip` DEFAULT OFF; legacy
DRM_MODE_PAGE_FLIP_EVENT delivery deferred to the next synthetic-vblank
edge via sched_timer -> workqueue callback (kthread context; fb_state.lock
spinlock-safe); event carries phase-accurate edge seq/ts; flip work itself
unchanged; kvmalloc/arm-failure paths fall back to synchronous delivery;
counters kms_flips_paced_total / kms_paced_delay_us_total /
kms_paced_dropped_total APPENDED at end of fb_gpu_stats (append-only ABI).
Adversarial review = GO: UAF-free teardown (owner re-resolved by monotonic
id under fb_state.lock; gpu_fops_release clears drm_event_file + drains
before kvfree), timer arm atomic (no double-delivery), no interaction with
ordered-pageflip, timestamps monotonic, gate-off byte-identical runtime.
Review fixes applied: F2 stats fields moved to struct END (mid-struct
insertion would have overflowed the OLD in-image fbstat by 24 bytes on
copyout and shifted all later offsets); F1 kms_paced_dropped_total on the
(unreachable-today) drop paths since one silent lost flip-complete =
permanently frozen kwin (sched_timer queue_work-failure drop documented,
sched_timer.c untouched). Nographic boot-check PASS both gate states.
STALE-BINARY HAZARD NOTED: FB_GPU_GET_STATS copies out sizeof(kernel
struct) unconditionally (fb_device_ioctl.c:2530) -> the appended fields
still write 24 bytes past OLD guest binaries in plain-stats mode; the
direct-launch reducer is SAFE (fbstat sample-current dispatches at
main():1051 BEFORE the GET_STATS call :1097; capture_fbstat_stats not
invoked in direct-launch-only), but VIDEO-lane reducers call plain fbstat
(capture_fbstat_stats :15036/:17480) -> REBUILD user tools + refresh image
before any fbstat-stats-based lane runs on this kernel. KDE A/B in flight:
control vs QEMU_APPEND_EXTRA=virtio_gpu_vblank_paced_flip=1, comparing
konsole_wait_ms + run.log page-flip cadence (engagement proof: paced =>
vsync-spaced flips) + crash greps; M6 under gate reads ~60 by design.
SLICE 1 VERDICT (traced A/B, 2 boots, fs.img restored, no lingering qemu)
= MECHANISM WORKS, NOT A RESPONSIVENESS WIN, VERDICT (ii). ENGAGEMENT
UNAMBIGUOUS: all kwin flips carry DRM_MODE_PAGE_FLIP_EVENT (flags=0x1);
gate ON submit->event-read median 17016us with 25/28 in 16336-17771us =
exactly one synthetic vblank (gate OFF: ~9.6ms); kwin provably gates its
flip pipeline on the event (event-read -> next flip submit in 445-518us);
burst cadence quantized to ~one-vblank+repaint. EFFECT ON GAPS: NONE --
inter-flip gap median 91.2ms OFF vs 94.2ms ON, >300ms gaps 9/31 vs 8/27,
multi-second idles persist BOTH arms. ROOT INSIGHT: the 30ms-10s gaps are
DAMAGE-ARRIVAL gaps (kwin idles because no client committed anything), not
a missing kwin clock; kwin 5.27 legacy RenderLoop = repaint-on-damage,
flip-event-gated under continuous damage. konsole_wait: paced arm cold
slightly better / WARM consistently ~+250ms WORSE across both A/Bs --
mechanistically plausible cost (+7ms/round-trip event delay x tens of
launch round trips). DECISION: keep `virtio_gpu_vblank_paced_flip` as
OPT-IN vsync-semantics infrastructure (correct paced cadence, may matter
for M7/tearing), DO NOT promote (no first-frame win + small warm cost).
NEXT LEVERS (from the same traces): (a) client-side commit latency after
frame-callback receipt (konsole side; co-enable client+kwin traces in one
boot to split); (b) ASYNC CURSOR = highest-confidence kernel win -- CURSOR
ioctl blocks the kwin compositor thread 17-36ms in EVERY traced run (30.7/
35.7/17.0/32.3ms across 3 sessions; synchronous virtio_gpu_user_set_cursor)
=> slice 3 now promoted to CURRENT ACTION, direct hover-latency lever;
(c) plugin/imageformats prune still queued (~70-100ms dlopen). No
commit/push, no default flips; slice-1 kernel diff stays uncommitted
pending the lane's consolidated landing decision.
SLICE 3 (ASYNC CURSOR) IMPLEMENTED + REVIEWED GO (2026-07-08, kernel
working tree, UNCOMMITTED): gate `virtio_gpu_async_cursor` DEFAULT OFF.
Root cause of the 17-36ms compositor-thread stalls = cursor IMAGE upload's
fenced TRANSFER_TO_HOST_2D on the ctrl queue inside the ioctl
(virtio_gpu_user_set_cursor -> virtio_gpu_resource_transfer_2d_fenced,
virtio_gpu_resource.c:732); cursor MOVES were already async on the cursor
virtqueue. Design: single-slot latest-wins coalescing (caller pixels
memcpy'd into a fixed 64x64 in-struct slot before ioctl returns), max_active
=1 workqueue worker runs the fenced upload off-thread into the existing
16-slot rotating cursor resource ring (fence intact = R5-safe), leaf
cursor_async_lock, sync fallback when gate off/wq missing. Counters
cursor_async_{submits,coalesced,errors}_total APPENDED at end of
fb_gpu_stats. Adversarial review GO: lost-kick protocol airtight
(clear-flag-then-recheck under the same lock), no host-reads-rewritten
resource, no deadlock/IRQ violation, gate-OFF byte-identical, no slice-1
interaction. Non-gating follow-ups: fix misleading max_active comment (the
single-instance guarantee actually comes from single-work-item + manager
dispatch throttle, min_active clamps to 2 threads), remove dead
cursor_async_inflight field. Nographic boot-check PASS both gates.
VALIDATION IN FLIGHT: hover A/B (full desktop-interaction reducer,
first_changed_ms, gate off/on + kwin CURSOR-ioctl duration trace, expect
17-36ms -> <1ms) + r9-cursor-visible-probe gate-ON as the mandatory
correctness gate. Stale-fbstat hazard: only `fbstat sample-current` is safe
on this kernel; plain stats mode needs a rebuilt fbstat.
SLICE 3 VALIDATION VERDICT (2026-07-08, 4 boots incl 1 FM19 rerun):
CORRECTNESS GO, PERFORMANCE NOT DEMONSTRATED. Gate-ON full session clean:
cursor_async submits/coalesced/errors = 10/0/0 with kms_cursor_uploads=10
(100% of image uploads asynced, zero failures), crash grep 0 both arms,
real GL held (kwin virgl D3D12), R9 cursor-visible probe PASS gate-ON
(evdev/libinput ABS samples, cursor-plane traces at 0,0/640,400/1279,799,
cursor_upload_visible=1, no KWin crash) -- async path does not corrupt/
freeze/garble. BUT hover medians gate-ON ~40% SLOWER (1544 vs 1090ms, n=1
each; konsole_wait cold wash 1504 vs 1493) -- cannot call regression at n=1
in a degraded environment, but NO win demonstrated. Arithmetic: only ~10
uploads/session x 17-36ms = 170-360ms total spread across a session, so the
theoretical hover win was small. CURSOR-ioctl histogram not collected (no
ioctl-timing shim in tree; the alloc-trace preload is a malloc tracer --
the prior traces used worker-built shims). Archives:
`20260708T032034Z-asynccursor-off-hover`, `20260708T032442Z-asynccursor-on-hover`,
FM19-discarded `20260708T031515Z-...-attempt1-visible-timeout`, R9 probe
`build-x86_64/r9-cursor-visible-probe-history/20260708T032452Z`.
ENVIRONMENT FINDING (measurement-validity blocker + real degradation):
plasmashell EGL broken environment-wide SINCE 20260707T1924Z -- `egl:
failed to create dri2 screen` -> QtQuick software fallback (the
qtquick-accelerated-path-policy FAIL label on every run since, both arms,
all campaigns) while kwin keeps real virgl GL. Same kernel passed before
19:24Z => host-WSL-GL drift, NOT a kernel change. Adds large hover-path
variance + is itself a responsiveness hit. RECOMMEND: `wsl --shutdown` /
host GL reset before further measurement campaigns; re-baseline after.
2026-07-08 EGL-REGRESSION BISECT UPDATE: host reboot + WSL update did NOT
cure it (postreboot rebaseline `20260708T041535Z`: 8 dri2 fails, 16
violations, kwin real GL, konsole_wait 2280/1050, crash 0). KERNEL
EXONERATED by verified-swap bisect: pre-neg-dcache kernel 934cf6f (27MB,
swapped in with sha-verified backup/restore, archive
`20260708T135432Z-bisect2-prenegdcache-kernel`) STILL fails 8/16 => the
neg-dcache/vblank/async-cursor commits are NOT the cause. (First bisect
attempt `20260708T135122Z` was INVALID -- harness hardcodes the kernel path
at kde-plasma-desktop-smoke.expect:6 and ignores KERNEL env; lesson
recorded.) FAILURE SHAPE: failing runs show plasmashell dri2 fail x4 +
`Failed to initialize EGL display 3001`, then a LATER instance succeeds
with identical GL_EXTENSIONS to passing runs => same first-attempt
readiness-race class as kwin's GBM/EGL retry (mesa drmGetDevice2
metadata-less fallback). Remaining changed artifact at the 19:24Z boundary:
fs.img REBUILT 17:52Z (fresh mkfs => different inode/block layout =>
shifted cold-read timing, persists across reboots). HYPOTHESIS TEST IN
FLIGHT: rebuild fs.img again + one boot (`*-imglayout-rebuild-egl-test`);
if dri2 failures vanish/change => image-layout-sensitive startup race
confirmed; durable fix = make plasmashell/Qt-Wayland EGL init robust
(retry/readiness gate in the session launcher) and/or the drmGetDevice2
metadata fix from the kwin lane (removes the fragile mesa fallback for ALL
clients). If still 8 fails => deeper persistent guest/host state, continue
triage.
HYPOTHESIS CONFIRMED + ENVIRONMENT RESTORED (2026-07-08): fresh fs.img
rebuild => `20260708T135835Z-imglayout-rebuild-egl-test` status DONE,
qtquick violations 0, plasmashell dri2 fails 0 (real GL restored),
konsole_wait 1492/1194 (healthy band), crash 0, no lingering qemu. VERDICT:
the plasmashell EGL regression was an IMAGE-LAYOUT-SENSITIVE first-attempt
EGL-init race (fresh mkfs shifts inode/block layout => cold-read timing =>
mesa's fragile drmGetDevice2-metadata-less fallback loses its readiness
race deterministically for that layout; persists across reboots because
it's baked into the image). Not kernel (bisect-exonerated), not host.
IMPLICATIONS: (1) every fs.img rebuild is a dice-roll on this race until
the fragile path is fixed -- the DURABLE fix is the drmGetDevice2 device-
metadata support in the guest DRM node (Q1a of the kwin lane) and/or an
EGL-init retry/readiness gate in the session launcher; treat a sudden
qtquick-accel-policy FAIL streak after an image rebuild as THIS signature
(rebuild again to confirm, don't chase kernels). (2) Measurements between
20260707T1924Z and 20260708T1358Z carry plasmashell-software contamination
(symmetric per A/B; konsole QWidget metrics largely unaffected).
(3) Environment is now CLEAN for the queued campaigns: async-cursor hover
re-A/B (n>=2), client-commit-chain trace, imageformats prune.
DELIBERATE tree delta kept: user/programs/fbstat/fbstat.c +4 lines printing
the cursor_async counters (needed because old fbstat would be overflowed by
the grown stats struct). LANE PATTERN NOTE: three kernel slices (neg-dcache,
vblank pacing, async cursor) all mechanically proven, none moved user-facing
latency -- remaining identified levers are client-commit chain (konsole
render latency after frame-callback), imageformats prune, and now FIXING THE
plasmashell EGL/dri2 environment. Slices stay gated default-OFF; land as
opt-in infrastructure.

Single-plan rule: this is the only live plan file. Verbose pre-compaction
records (including the full 2026-07-04 pre-rewrite plan) are preserved
append-only in
`docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`
("the history file"). Durable mechanisms/workflows live in
`.github/skills/`. Every proof run is archived under
`build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/` (or the
lane-specific proof dirs) — archive names cited below are relative to that.
Branch note: this plan lives on `codex/host-linux-abi-shell-port-ff`;
sibling copies on `codex/host-linux-abi-shell-port` /
`origin/codex/kde-qt-wayland-bringup` predate the 07-04 round.

## Mission

Make the KDE/Chromium desktop on x86_64 KVM+virgl stable and responsive,
with the current primary target being desktop responsiveness that approaches
a comparable Linux VM on the same host/class. Correctness lanes (P0 freeze,
R5 corruption) are fixed or closed; smoke PASS alone is not acceptance. The
remaining work is kprofile-driven app-launch/input responsiveness,
performance (M7 video path, M2 syscall cost, M8 idle CPU), user-visible input
(R9), and statistical closure.

## Scoreboard — Measurements and Goals

Move one metric without regressing the others. Verify the booted cmdline in
every run log (`grep 'x86 kernel cmdline'`).

Measurement validity rules:

- The guest drops ~14% of BSP timer ticks under 6-vCPU desktop load
  (kprofile wall 40000ms vs kernel uptime 34201ms, reconfirmed twice).
  Single-run `*_ms` deltas under load are +/-15% unless cross-checked
  against a monotonic wall reference. Candidate fix (open, N7): compensate
  lost BSP ticks via TSC or let any CPU advance jiffies.
- A kprofile run with `kprofile_timeout_hit=1` AND an incomplete measured
  window is truncated — do not read scoreboard metrics from it. (For
  browser workloads timeout_hit=1 alone is EXPECTED — Chromium never exits;
  judge window completeness by the PERF-VIDEO RESULT/`advanced=` line.)
- Real-GL Chromium video runs need
  `KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE_SECONDS=90`: first-boot virgl->D3D12
  shader compile + cold paging eats the default 40s window.
- kprofile `cpu_busy/total_ms` are scheduler-invocation counts scaled by
  HZ, NOT wall time (scheduler_yield runs per reschedule, ~5x tick rate
  under load); only the busy/total RATIO is meaningful. pgroup fields are
  process-group-only (zygote children escape). kprofile's exec takes
  absolute paths only; guest /bin/sh does not glob (use /bin/bash).
- Plasma responsiveness work MUST invoke kprofile as the metric source after
  every responsiveness iteration/fix/trace, and compare toward Linux-like
  behavior on a comparable same-host/class VM rather than merely beating old
  xv6 smoke thresholds. Acceptance records kprofile elapsed/timeout,
  app-launch latency, Konsole shell readiness, pre-PTY timing, phase-log
  `konsole_wait_ms`, `cpu_busy/total_ms` ratio,
  `pgroup_cpu_runtime_ms`, `sys_poll`/`sys_ppoll`/`sys_openat`,
  `sys_poll_blocking`, `sys_poll_wait_notify`, app-probe PASS, eventfd fd
  attribution and user-PC/module attribution when relevant, interaction
  coverage such as hover/app launch, and mandatory KVM + virgl + real GL proof.
  Software/llvmpipe fallback is invalid. Visual proof supplements the metrics;
  subjective observation alone is not acceptance.

Audio: host audio is NOT a readiness prerequisite. `pactl` probes are
default-off (`kde_pactl_probe=1` to re-enable); non-audio gates run with
`QEMU_AUDIO_BACKEND=none`.

| # | Metric | How measured | Current (2026-07-07) | Goal |
|---|--------|--------------|----------------------|------|
| M1 | YouTube-freeze survival | P0 repro recipe, 15 min, 3 runs | 3/3 responsive clean (2026-07-03 battery); P0 closed | 3/3 clean |
| M2 | Guest `getpid_ns` | `syscalltlb 2000` nographic | accepted baseline 1.65-1.94us (P1 2a/2b landed); N4 stopped/reverted after clean CR0-only rerun failed at 1973/2102/1925/2185ns | < 1.5us (requires a different P1 approach) |
| M3 | `tlb_amplification_ns` (1024 pg) | same | accepted baseline noisy 2.7-3.7us band; clean CR0-only N4 rerun failed at 4504ns | 0 on default boot |
| M4 | `konsole_wait_ms` | KDE desktop-interaction/kprofile | N=3 shipped-default direct-launch kprofile (2026-07-07): konsole_wait_ms warm median 1420 (~1310 excl first-boot cold-cache outlier), cold median 1593 (~1549 excl); all gates green (real GL, timeout 0, userpc drop 0, app PASS, no crash); user-PC attribution ~half ld-linux (lookup_hash 17-20% + relocation 17-18%), ~12% libc, ~16-20% Qt5, loader-share ~63-66% of symbolized loader/libc/xv6, 219-`.so` closure, ~4.3-4.5k openat/~3.7k ENOENT per launch; top symbols `_dl_new_hash`/`resolve_map`/`check_match` | converge toward comparable Linux VM behavior |
| M5 | `first_visible_ms` | same | 6473 in clean N8 LD_BIND_NOW-off proof; direct-launch prompt 1508/1429 | < 15000 |
| M6 | `mesakmsgl` direct-KMS FPS | pageflip A/B | 118-125 ordered, now DEFAULT-ON | done (was: ordered default) |
| M7 | `presentedFPS` (60fps video) | chromium-video kprofile, KPROFILE_SECONDS=90 | 51.0 default after total-reaper redesign; native_present_credit remains 0 and native-present is NOT solved. This is not the current app-launch bottleneck | >= 55 (N1) |
| M8 | Idle-desktop host CPU | 10s `/proc/$pid/stat` utime+stime delta on a GL-pipeline boot (non-GL boots never present — invalid for M8) | UNMEASURED-VALID: the 2026-07-05 44%/57-64% readings were doubly invalid (non-GL boots with zero presentation AND poll flags since fully reverted for interactive hangs). Historical band 85-130%. Re-measure with the documented GL recipe on shipped defaults | < 100% |
| M9 | Chromium window visible | chromium-video launch-only reducer | PASS (2026-07-07 M9 launch-only guard): `browser_seen`, render/drm fds, `gpu_init_error_count=0`, `fault_count=0`, real GL env/renderer, no lingering QEMU | PASS (holds; guard in every battery) |

Fork-safety gate for any syscall/scheduler/TLB/mm change: `forktest`
(rc=1 "fork claimed to work N times!" = known exhaustion signature),
`clonetest` rc=0, `cowtest` rc=0, same boot.

## Current Plasma Responsiveness / kprofile Status (2026-07-07)

- Current accepted N8 workflow proof is the full desktop-interaction kprofile
  PASS at
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000224Z-n8-guest-input-full-interaction-kprofile`.
  It ran with `direct-launch-only=0` as an active sample, using bounded guest
  `/bin/mouseinject` for normal input (`input_source=guest`,
  `monitor_path=disabled`, `host_cursor_sync_ms=0`, bounded coordinates).
  Hover passed (`first_changed_ms=923`, diff 300); app/kprofile passed with
  `konsole_wait_ms=1507`, `kprofile_timeout_hit=0`, and userpc stored=631,
  dropped=0. KVM + virgl real GL held with no software fallback, and
  `LD_BIND_NOW` is absent from source, refreshed fs image, and the exact smoke
  tmp image.
- Current N3/R9 guest-mouseinject proof is archived at
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T005222Z-n3-guest-mouseinject-kprofile-proof`.
  `scripts/gpu/kde-plasma-desktop-smoke.expect` now forces normal desktop
  reducers to guest input (`input_source=guest`), `monitor_path=disabled`,
  `host_cursor_sync=0`, `QEMU_GTK_CURSOR_MODE=guest`, and
  `QEMU_GTK_SHOW_CURSOR=off`; HMP/QEMU monitor mouse movement is
  diagnostic-only and disabled for normal reducers. Pre-hover waits for
  taskbar/desktop settle by default: 3000ms plus 3 stable framebuffer samples.
  PASS proof: `status_code=0`, KVM + virgl real GL/no software fallback,
  `cursor_owner=guest-forced-normal-interaction`, bounds
  `framebuffer=1280x800 workarea=0,0-1279,799`, settle
  `elapsed_ms=4711 stable_count=3`, `/bin/mouseinject 11016 64223 0`,
  `host_cursor_sync_enabled=0`, `host_cursor_sync_ms=0`,
  `status=changed first_changed_ms=986`, app launch PASS with
  `konsole_wait_ms=1508`, `kprofile_timeout_hit=0`, userpc stored=602
  dropped=0, no lingering QEMU, and clean diff check. Follow-up after user
  visual inspection split the issue into smoke harness vs normal launcher:
  normal desktop-interaction/direct-launch smoke paths are guest-only pointer
  injection with `input_policy=guest-only`,
  `pointer_injection=guest-mouseinject`, `host_cursor_sync=disabled`, and
  `qemu_monitor_input=0`; monitor/HMP/QMP input is diagnostic-only.
  `scripts/launch/launch-gui.sh` now defaults `QEMU_GTK_CURSOR_MODE=guest`
  and `QEMU_GTK_SHOW_CURSOR=off` while preserving overrides. Post-patch full
  validation PASS:
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T024621Z-n3-n8-guest-cursor-postpatch-kprofile`.
  Earlier `20260707T024333Z` was invalid kprofile usage (`probe_rc=1`) and
  superseded. Proof: pre/post QEMU scans empty; qemu command had
  `-enable-kvm`, `virtio-vga-gl`, GTK `show-cursor=off`, and no
  `virtio_gpu_host_cursor_only=1`; renderer real virgl (D3D12 NVIDIA
  GeForce RTX 4060 Laptop GPU); software fallback envs unset; guest cursor
  proof `input_source=guest`, `input_policy=guest-only`,
  `pointer_injection=guest-mouseinject`, `host_cursor_sync=disabled`,
  `qemu_monitor_input=0`; bounded `/bin/mouseinject 11016 64223 0`,
  framebuffer 1280x800, workarea 0,0-1279,799, guest pixel 215,783; hover
  PASS `first_changed_ms=1048`; app/kprofile PASS
  (`konsole_wait_ms=2816`, app-launch `elapsed_ms=4159`,
  `kprofile_elapsed_ms=3616`, `kprofile_timeout_hit=0`, userpc 1052/0);
  crash scan 0; scoped diff-check passed for
  `scripts/gpu/kde-plasma-desktop-smoke.expect` and
  `scripts/launch/launch-gui.sh`; scratch image deleted/no `*.img` in
  archive. The live N8 responsiveness bottleneck remains
  Konsole/app-launch readiness/loader work, not mouse bounds.
- Chromium M9 launch-only guard PASS is archived at
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000512Z-n8-m9-chromium-launch-only-real-gl`.
  It saw the browser plus render/drm fds, `gpu_init_error_count=0`,
  `fault_count=0`, real GL env/renderer held, and no lingering QEMU.
- XDG path-pruning A/B experiment delta was limited to `XDG_DATA_DIRS` and
  `XDG_CONFIG_DIRS` defaults relative to its pre-edit control. Caveat: the
  same scoped files in the current dirty worktree also carry prior N8
  helper/harness edits, so the raw worktree is not an XDG-only patch. Preedit:
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T001509Z-n8-xdg-paths-preedit-direct-launch-kprofile`;
  pruned:
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T002116Z-n8-xdg-paths-pruned-direct-launch-kprofile`.
  It reduced cold/warm ENOENT totals 2816/2396 -> 2658/2228 and
  `ext4_lookup_enoent` 2124/1848 -> 1973/1681. Specific churn wins:
  `kubuntu-default-settings` 73/70 -> 0/0, `/usr/local/share` 2/2 -> 0/0,
  local 97/94 -> 7/1. Readiness did not improve beyond noise:
  `konsole_wait_ms` 1389/1298 -> 1399/1317, prompt 1541/1613 -> 1511/1558;
  loader share stayed ~43-46% and top symbols remained dynamic-loader work.
  Post-verifier cheap evidence was archived in the pruned directory:
  `post-verify-rootfs-refresh.log` (rootfs-refresh rc=0, `fs.img` refreshed),
  `post-verify-qemu-pgrep.log` (no lingering `qemu-system`; superseded
  self-match pgrep attempt retained separately), `post-verify-xdg-rg.log`
  (old XDG defaults absent/current defaults present), and
  `post-verify-git-diff-check.log` (scoped `git diff --check`).
- Offline branch scouts concluded to avoid glibc/ELF loader surgery. The
  loader hotspot remains real and repeatable, but too ABI-sensitive for the
  next branch. Follow-up hwcaps/`LD_LIBRARY_PATH` probes were
  measurement-only; IFUNC remains only a possible measurement probe if useful.
- Loader/env measurement-only matrix completed with no permanent hook,
  default, or env change: the temporary hook was applied and reverted exactly
  from `/tmp/xv6-worker-h/worker-h-temp-hook-only.diff`, touched-file diff was
  restored, user and rootfs-refresh builds passed, and no QEMU lingered.
  Valid KVM + virgl real-GL/no-software-fallback archives: control
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T011159Z-n8-loader-env-control-direct-launch-kprofile`;
  hwcaps-mask0
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T011412Z-n8-loader-env-hwcaps-mask0-direct-launch-kprofile`;
  ldpath-opt-last
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T011905Z-n8-loader-env-ldpath-opt-last-direct-launch-kprofile`;
  ldpath-last+hwcaps
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T012101Z-n8-loader-env-ldpath-last-hwcaps-mask0-direct-launch-kprofile`.
  Invalid visible-timeout archive, rerun passed:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T011622Z-n8-loader-env-ldpath-opt-last-direct-launch-kprofile`.
  Control: wait 1297/1196, prompt 1510/1431, CPU ratio .546/.563, pgroup
  839/845, userpc 437/0 and 525/0, loader share .416/.465, ext4 ENOENT
  1980/1679, openat 2467/2355, `/opt` rows 145/145, glibc-hwcaps 66/66.
  hwcaps-mask0: wait 1395/1317, prompt 1608/1545, loader .429/.430, ext4
  ENOENT 2020/1679, `/opt` 145/145, glibc-hwcaps 66/66; no improvement and
  masking did not reduce hwcaps path rows here. ldpath-opt-last: wait
  1489/1187, prompt 1600/1428, loader .415/.427, ext4 ENOENT 1793/1430,
  openat 2288/2106, `/opt` rows 28/0, glibc-hwcaps 84/90; cuts lookup churn
  but cold readiness worsened, no stable M4 win, loader remains top module.
  ldpath-last+hwcaps: wait 1402/1051, prompt 1538/1301, loader .414/.433,
  ext4 ENOENT 1726/1428, openat 2177/1956, `/opt` rows 20/0, glibc-hwcaps
  84/90; best warm number, but small repeat count/no clean cold win, still
  loader top.
- Offline loader-category attribution completed without a VM boot, commit, or
  push. `scripts/gpu/kprofile-userpc-phase-attribution.py` now preserves
  existing rows and adds `category=<...>` on `direct-launch-phase-symbol`
  rows plus `direct-launch-phase-loader-category` rollups. Verifiers:
  `python3 -m py_compile scripts/gpu/kprofile-userpc-phase-attribution.py`,
  `git diff --check -- scripts/gpu/kprofile-userpc-phase-attribution.py`, and
  `git diff --no-index --check` because the script is untracked. Reports:
  `n8-loader-category-attribution-01-cold.txt` and
  `n8-loader-category-attribution-02-warm.txt` in each valid loader/env
  archive above. Compact LTP/category matrix: control cold 388 samples
  (loader/libc/xv6 169/37/2; relocation 65, 16.75%; lookup 63; mmap/open 14),
  control warm 472 (232/56/2; lookup_hash 95, 20.13%; relocation 82;
  mmap/open 13), hwcaps cold 389 (177/46/2; lookup_hash 71, 18.25%;
  relocation 67; mmap/open 5), hwcaps warm 441 (198/64/1; relocation 78,
  17.69%; lookup 72; mmap/open 5), ldpath cold 388 (174/43/0; relocation 66,
  17.01%; lookup 60; mmap/open 16), ldpath warm 380 (177/37/5; relocation 69,
  18.16%; lookup 62; mmap/open 4), combined cold 383 (162/51/1; relocation 77,
  20.10%; lookup 58; mmap/open 2), combined warm 369 (171/42/1;
  relocation/lookup tie 69, 18.70%; mmap/open 6). Backing symbols include
  `_dl_new_hash`, `do_lookup_x`, `check_match`, `resolve_map`,
  `elf_machine_rela_relative`, `elf_dynamic_do_Rela`,
  `_dl_map_object_from_fd`, `strcmp`, and `__memset_avx2_unaligned_erms`.
  Symbolization coverage was 99.59-100%.
- Libimobiledevice no-device shim A/B is complete and no-promote. ABI scout
  was safely small: `libKF5Solid.so.5.115.0` imports exactly 10 `Base`
  symbols from `libimobiledevice-1.0.so.6`
  (`idevice_event_subscribe`, `idevice_event_unsubscribe`,
  `idevice_get_device_list`, `idevice_device_list_free`, `idevice_new`,
  `idevice_free`, `lockdownd_client_new`, `lockdownd_client_free`,
  `lockdownd_get_device_name`, `lockdownd_get_value`). A temporary opt-in
  no-device shim was built/used and the repo hook reverted; saved artifacts:
  `/tmp/xv6-worker-o/worker-o-temp-fsimg-hook.patch`,
  `/tmp/xv6-worker-o/xv6-libimobiledevice-nodevice-shim.c`, `.map`, and
  `libimobiledevice-1.0.so.6`. Verifiers passed: rootfs-refresh,
  `git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect`,
  `python3 -m py_compile scripts/gpu/kprofile-userpc-phase-attribution.py`,
  readelf SONAME/libc-only NEEDED/exactly-10-exports proof, no lingering
  QEMU, KVM + virgl real GL in run logs and qtquick accel policy, no software
  fallback, and clean crash-marker scan. Archives: control
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T020310Z-n8-libimobiledevice-control-direct-launch-kprofile`;
  shim
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T020424Z-n8-libimobiledevice-nodevice-shim-direct-launch-kprofile`.
  Compact metrics:
  | run | wait | prompt | timeout | userpc | CPU | pgroup | openat | ENOENT | loader | categories |
  |---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
  | control PASS | 1983/1208 | 2199/1519 | 0/0 | 579/0, 499/0 | .533/.546 | 1229/857 | 2617/2368 | 1979/1681 | 38.35%/38.90% | reloc/lookup/mmap 78/75/9, 66/64/11 |
  | shim PASS | 2085/1211 | 2302/1521 | 0/0 | 606/0, 482/0 | .524/.572 | 1301/849 | 2584/2306 | 1961/1678 | 42.99%/47.20% | reloc/lookup/mmap 82/93/8, 81/76/7 |
  Control maps had imobiledevice+ssl+crypto; shim maps had the imobiledevice
  shim and ssl/crypto gone. Shim call evidence:
  `kde-session-plasma-child.log` showed `idevice_event_subscribe`,
  `idevice_get_device_list result_count=0`, and
  `idevice_device_list_free`; Konsole logs showed the shim loaded.
- NewStuff opt-in SONAME-stub initial + repeat/control are complete
  (2026-07-07), gated only by `KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`; no
  default flip. Review: env-absent paths are default-safe with no persistent
  rootfs effect, and default desktop/Plasma/Chromium normal paths are
  unaffected. Caveat: C-side `set_kde_env` in
  `scripts/image/kde-app-launch-probe.c` is probe-wide when the env is set,
  while `scripts/gpu/kde-plasma-desktop-smoke.expect` currently scopes staging
  and export to the Konsole-only direct-launch probe. The shim is staged from
  `/tmp/xv6-newstuff-shim/` into the tmp image; current minimal patch is
  `/tmp/xv6-newstuff-shim/newstuff-probe-gate.patch` touching
  `scripts/image/kde-app-launch-probe.c` and
  `scripts/gpu/kde-plasma-desktop-smoke.expect`. If promoted, add
  repo-owned shim source/map/build,
  tighten or clearly document the Konsole-only gate, and do not commit whole
  dirty files blindly.
  ABI audit: exactly 8 direct KNS/KNSCore imports, all from
  `libkonsoleprivate.so.1`; `konsole` and `libkonsoleapp` import none.
  Required shadow SONAMEs are `libKF5NewStuffWidgets.so.5` and
  `libKF5NewStuffCore.so.5`; the shim exports only `KNSWidgets::Button` ctor,
  `setConfigFile`, `dialogFinished`, `staticMetaObject`, and
  `KNSCore::EntryInternal` `name`/`installedFiles`/`uninstalledFiles`/`status`.
  ABI risk remains: fake `staticMetaObject`, no real QObject construction,
  vtable, or destructor coverage, no-op methods, and empty Qt returns.
  Previous and repeat runs did not call-cover risky bodies because shim call
  log was absent or explicitly absent-expected.
  Initial archives:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T030649Z-n8-newstuff-control-direct-launch-kprofile`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T030825Z-n8-newstuff-stub-direct-launch-kprofile`;
  warm direct/kprofile improved 3153/2541 -> 2363/1968 while cold moved only
  slightly positive. Repeat archives: control
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T031947Z-n8-newstuff-repeat-control-direct-launch-kprofile`;
  stub
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T032139Z-n8-newstuff-repeat-stub-direct-launch-kprofile`.
  Repeat wait/prompt/direct/kprofile: cold 1776/1995/2710/2220 ->
  1747/1857/2847/2209 (deltas -29/-138/+137/-11); warm
  1414/1674/2551/2049 -> 1297/1547/2396/1823
  (deltas -117/-127/-155/-226). `kprofile_timeout_hit=0` and userpc dropped=0
  in all four; stored samples control 546/557, stub 528/548. CPU/pgroup was
  roughly flat to slightly better, but openat/ENOENT worsened: control
  2594/1975 and 2343/1703; stub 2819/2220 and 2601/1923. Loader categories
  were mixed: warm relocation/mmap 91/14 -> 70/9, but lookup 83 -> 95; cold
  neutral/mixed.
  Real GL held (KVM, virgl D3D12 NVIDIA RTX 4060, no llvmpipe/softpipe,
  fallback envs unset). Maps show control real `libKF5NewStuff*.so.5` and
  treatment shim only; warm control had Qt5Qml/Qt5Quick maps, warm stub did
  not in the capped snapshot, and full uncapped live maps were not collected;
  `maps-artifact-limitation.txt` was added. Treatment archive has shim
  checksum/readelf/SONAME/export proof plus explicit call-log
  absent-expected. Crash scan clean, no lingering QEMU, no scratch images.
  Decision: repeat weakly supports keeping NewStuff as an opt-in promotion
  candidate, but does NOT justify durable/default promotion. Warm improvement
  repeated but smaller; cold was mixed; VFS/open counts worsened. Before
  further promotion, tighten the C gate to Konsole-only or document it
  clearly.
- Qt5Multimedia gated probe review (2026-07-07): keep as scratch opt-in; no
  default flip, and no-go/revert is not warranted, but adjust before stronger
  validation or promotion. Gate
  `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` is cleaner than NewStuff: the shim
  `LD_LIBRARY_PATH` is scoped to the forked Konsole child/process subtree, the
  parent `LD_LIBRARY_PATH` is unchanged, terminal/dolphin/kate/kwrite/chromium
  siblings do not inherit it, and Chromium-only/sample paths return before
  this path.
  NewStuff remains probe-wide when armed via `set_kde_env`. Env-absent paths
  have no persistent default/rootfs LD path; staging writes only the temp
  image. Startup `LD_BIND_NOW` removal is unrelated/default no-shim.
  A/B archives:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T035129Z-qtmm-control-direct-launch-kprofile`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T035327Z-qtmm-stub-direct-launch-kprofile`;
  cold direct/helper/kprofile/wait/prompt moved
  3821/3733/3030/2335/2448 -> 3018/2962/2239/1776/1994; warm moved
  2960/2876/2357/1630/2053 -> 2657/2593/2071/1418/1736; timeout stayed 0 and
  real KVM + virgl GL held. Load-only symbols/version/SONAME are enough for
  observed imports: exactly six `Qt_5` QMedia imports from
  `libkonsoleprivate.so.1`, no direct `libQt5MultimediaWidgets`, six exports,
  SONAME `libQt5Multimedia.so.5`, no DT_NEEDED, checksum
  `93ec68fb8cde4eca15638aa6a8341b06e78f21a3acf6ce063ea5eb8af4229018`.
  Weaknesses: shim is still only `/tmp` source/built; durable opt-in needs
  repo-owned source plus version script, reproducible build/stage, and
  archived source/map/build/checksums/readelf proof. It is not call-safe:
  fake `QMediaPlayer` is not a `QObject`, `QMediaContent` layout/ref state is
  uninitialized, and later media calls/`deleteLater`/signal-slot paths are
  high-risk/no-go. Call logging was limited to the readiness window/capped
  output; next repeat must archive the final call log after teardown. Proof is
  sufficient for a narrow opt-in candidate, not durable/default; missing full
  uncapped maps must prove Qt5Multimedia/libpulse/tail absence in stub and
  real presence in control. Next action: convert QtMM shim to a
  repo-reproducible opt-in fixture, then repeat/control with full maps and
  final call-log before any broader active-sample or durable consideration;
  keep NewStuff as the weaker/mixed opt-in candidate.
- Branch decision: accept XDG pruning as a minor reversible lookup-churn
  cleanup, not an M4 responsiveness fix; do not promote hwcaps masking or
  `LD_LIBRARY_PATH` reorder, libimobiledevice/crypto-chain trimming, or
  NewStuff by default as an N8 responsiveness fix yet. LD path-last may stay a
  future minor lookup-churn
  cleanup candidate only after stronger repeat/control, but current evidence
  says the M4 bottleneck is dynamic-loader relocation + lookup/hash proper,
  Qt/KF5/QML/KIO or Konsole-private contributors, not XDG, hwcaps, simple
  mmap/open/`/opt` path search, or the libimobiledevice crypto chain.
  Static scout decision: Konsole direct closure is ~134 objects, ~227,504
  relocations, and ~43,056 undefined dynsyms; top contributors include
  libQt5Widgets ~23,205, libQt5Quick ~20,618, libgallium ~18,275, libcrypto
  ~18,081, libQt5Qml ~12,089, libKF5KIOWidgets ~5,370,
  libkonsoleprivate ~4,833, libQt5Multimedia ~3,485, and libKF5Solid ~3,488.
  NewStuff remains an opt-in promotion candidate only after initial + repeat
  A/B: real NewStuff maps were replaced by shim maps with the exact 8-symbol
  KNS/KNSCore surface, warm improvement repeated but smaller, cold was mixed,
  and openat/ENOENT worsened. Review also found the env-armed C gate is
  probe-wide and the ABI risk was not call-covered. QtMM is a cleaner gated
  scratch opt-in: `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` scopes the shim
  `LD_LIBRARY_PATH` to the forked Konsole child/process subtree, leaves the
  parent `LD_LIBRARY_PATH` and sibling apps untouched, and has no persistent
  default/rootfs LD path when absent. Its A/B replaced real Qt5Multimedia/Pulse
  maps with the six-symbol shim for Konsole direct-launch, direct time improved
  by -803ms cold / -303ms warm, `kprofile_timeout_hit=0`, and real KVM +
  virgl GL held. Do not default-promote it yet: the shim is `/tmp`-only, map
  snapshots are capped, final post-teardown call logs are missing, and
  load-only coverage is not call-safe.
  KIO direct edge is entangled/no-go; Solid/crypto no-go follows the prior
  libimobiledevice shim result; Gallium/GL no-go because real GL is mandatory.
  Next N8 action is to convert QtMM to a repo-reproducible opt-in fixture,
  then repeat/control with full maps and final call-log before active-sample,
  durable, or default consideration; keep NewStuff as the weaker/mixed opt-in
  candidate.
  No glibc/ELF-loader surgery or default flips; keep
  kprofile, KVM + virgl real GL, and no software fallback as validation gates.
  Do not spend the next branch on generic GPU/hover/syscall work unless a
  guardrail fails. Mouse/cursor follow-up does not change this: next action is
  still kprofile-driven Konsole/app-launch responsiveness with KVM + virgl
  real GL; manual/user inspection can relaunch via
  `scripts/launch/launch-gui.sh` using guest cursor defaults. No commit/push
  yet.
- Background N8 proofs still relevant for context: clean `LD_BIND_NOW` direct
  replay
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260706T231950Z-n8-ld-bind-now-off-clean-startup-direct-launch-kprofile`
  superseded the stale-startup-contaminated
  `20260706T225715Z-n8-ld-bind-now-off-direct-launch-kprofile`; attribution
  fix proof remains
  `build-x86_64/kde-plasma-desktop-smoke-history/20260706T182619Z-n8-exec-opened-path-userpc-maps-validation/`;
  clock-domain proof remains
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-clock-domain-uptime-20260706-1538`.
  Existing commit-hygiene caveat:
  `scripts/image/konsole-wayland-event-trace-preload.c` is currently
  untracked, so later commit hygiene must account for it before relying on
  CMake/rootfs dependencies in a commit.
- Retired/background N8 evidence, including older artifacts, raw tables,
  flaky/failed attempts, reducer details, and raw attribution rows, belongs in
  the history file and archived proof directories, not this live plan.

## Work Order — Current Queue (recut 2026-07-07 addendum)

The 07-03 Q1-Q7 queue is RETIRED: Q1 landed, Q2 runtime-validated (real
GL), Q5 root-caused+fixed, Q7 landed. New queue; current next action is
N8:

- N8 = Linux-like Plasma responsiveness (CURRENT NEXT ACTION,
  kprofile-driven): current branch starts from the full desktop-interaction
  PASS
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000224Z-n8-guest-input-full-interaction-kprofile`
  plus the Chromium M9 real-GL guard PASS
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000512Z-n8-m9-chromium-launch-only-real-gl`.
  Bounded guest input, hover, app launch, clean `LD_BIND_NOW` absence, userpc
  no-drop storage, KVM + virgl real GL, and no lingering QEMU are proven.
  XDG path pruning is accepted only as minor reversible lookup-churn cleanup:
  its A/B experiment delta is XDG-defaults-only relative to the pre-edit
  control, while the wider dirty helper/harness files still include prior N8
  edits. It reduced ENOENT/local/default-settings misses but did not move
  `konsole_wait_ms` beyond noise, and loader share stayed ~43-46%. The pruned
  archive now includes post-verifier static/rootfs/no-lingering-QEMU evidence.
  Completed measurement-only hwcaps/`LD_LIBRARY_PATH` matrix was
  no-go/no-promotion: hwcaps masking did not help, LD path-last cut lookup
  churn but did not produce a stable M4 win, and loader remained the top
  module. Offline loader-category attribution now says relocation plus
  lookup/hash proper dominate, while mmap/open/path search is secondary.
  Libimobiledevice no-device shim A/B was also no-go/no-promotion: ABI surface
  was exactly 10 imported `Base` symbols, the temporary shim removed
  libssl/libcrypto from Konsole direct-launch maps, but cold readiness
  worsened and warm stayed flat. Do not pursue libimobiledevice/crypto-chain
  trimming as an M4 fix unless new evidence appears. NewStuff opt-in initial +
  repeat A/B are complete, no default flip: the gate is
  `KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`, direct Konsole ABI surface is exactly
  8 KNS/KNSCore imports from `libkonsoleprivate.so.1`, real NewStuff maps were
  replaced by shim maps, and real GL held. Initial warm
  wait/prompt/direct/kprofile moved 1914/2131/3153/2541 ->
  1403/1642/2363/1968; repeat control/stub archives are
  `20260707T031947Z-n8-newstuff-repeat-control-direct-launch-kprofile` and
  `20260707T032139Z-n8-newstuff-repeat-stub-direct-launch-kprofile`, with warm
  1414/1674/2551/2049 -> 1297/1547/2396/1823 but mixed cold
  1776/1995/2710/2220 -> 1747/1857/2847/2209 and worse openat/ENOENT.
  Review found env-absent paths default-safe/no persistent rootfs effect, but
  the env-armed C gate is probe-wide while the harness scopes it to
  Konsole-only direct-launch; ABI risk remains uncalled because the shim call
  log is absent/absent-expected. Treat NewStuff as opt-in candidate only, not a
  durable/default promotion. Qt5Multimedia review conclusion: keep as gated
  scratch opt-in; no-go/revert is not warranted, but adjust before stronger
  validation or promotion. `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` is cleaner than
  NewStuff because the shim `LD_LIBRARY_PATH` is scoped to the forked Konsole
  child/subtree; parent `LD_LIBRARY_PATH` is unchanged; terminal, dolphin,
  kate, kwrite, and chromium siblings do not inherit it; Chromium-only/sample
  paths return earlier; env-absent default/rootfs paths are clean; and staging
  writes only the temp image.
  Startup `LD_BIND_NOW` removal is unrelated/default no-shim. A/B archives
  `20260707T035129Z-qtmm-control-direct-launch-kprofile` and
  `20260707T035327Z-qtmm-stub-direct-launch-kprofile` show cold/warm direct
  deltas -803ms/-303ms, timeout 0, real GL held, and load-only ABI proof for
  the six observed QMedia imports. Not durable/default yet: shim source/build
  is still `/tmp` only, repo-owned source/version script and reproducible
  build/stage are missing, maps are capped, final post-teardown call log is
  missing, and fake `QMediaPlayer`/`QMediaContent` state makes later media,
  `deleteLater`, or signal-slot paths high-risk/no-go. Next N8 action is to
  convert QtMM into a repo-reproducible opt-in fixture and repeat/control with
  full maps proving Qt5Multimedia/libpulse/tail absence in stub and real
  presence in control plus final call-log before broader active-sample or
  durable consideration; keep NewStuff as the weaker/mixed opt-in. KIO is
  entangled, Solid/crypto no-go by prior shim, and Gallium/GL no-go because
  real GL is mandatory. Do not do glibc or ELF-loader surgery or default flips
  next; boot only to validate a concrete change. Acceptance loop: every
  responsiveness iteration/fix/trace uses kprofile, preserves durable raw logs
  and app/hover proof as relevant, requires app probe PASS,
  `kprofile_timeout_hit=0`, virgl renderer/no software fallback, user-PC
  samples without drops when collected, maps/module attribution when relevant,
  and no regression of `cpu_busy/total_ms`, `pgroup_cpu_runtime_ms`, R5/R3
  watches, or GPU fallback rejection. Keep `poll_stuck_trace=1`
  diagnostic-only unless collecting stuck-poller evidence. No commit/push yet.

- N1 = M7 present path (P2 step 3): the last M7 blocker. All flips are
  software_blit copies. Decision slice result 2026-07-04: route (a) is
  fail-closed on this host. Runnable QEMU is
  `/usr/bin/qemu-system-x86_64` Debian 9.0.2. It advertises classic
  `virtio-gpu-pci`, `virtio-gpu-gl-pci`, `virtio-vga`,
  `virtio-vga-gl`, and `vhost-user-gpu-pci`, but no
  rutabaga/gfxstream/cross-domain device. `virtio-gpu-rutabaga-pci,help`
  says "Device not found"; classic `virtio-gpu-gl-pci,help` fails with
  `undefined symbol: qemu_egl_display`. `/dev/udmabuf`, `/dev/kvm`, and
  `/dev/dxg` exist; no `/dev/dri/renderD*` exists. Host has
  `libvulkan_gfxstream.so` and `libvirglrenderer.so.1`, but no usable
  `rutabaga_gfx_ffi` surfaced. Non-GL `virtio-gpu-pci` supports
  `blob`, `hostmem`, and `max_hostmem`, and a dry-run can form a memfd
  command with `blob=true,hostmem=32M,max_hostmem=32M`; the
  `virtio-vga-gl-primary` + blob lane still fail-closes with
  `QEMU rejects virgl + blob ("blobs and virgl are not compatible")`.
  Landed launcher detection only: `QEMU_BIN=/path/to/qemu-system-x86_64`
  selects the probed binary, and
  `QEMU_GPU=virtio-gpu-rutabaga{,-primary}` or
  `QEMU_GPU=virtio-vga-rutabaga-primary` requires blob/hostmem/
  max_hostmem, `/dev/udmabuf`, hardware host DRI, the rutabaga device,
  `gfxstream-vulkan`, `cross-domain`, `wsi`, and memfd RAM; it refuses
  classic virgl or software fallback. No M7 run was attempted because
  the route cannot dry-run to device args. Kernel route is also not ready
  to implement blindly: creatable capset admission currently supports
  VIRGL/VIRGL2 only (DRM is query-only), not upstream gfxstream/
  cross-domain. Next kernel task, after a capable host/QEMU exists:
  define and wire `VIRTIO_GPU_CMD_SET_SCANOUT_BLOB`, carry blob resource
  format/stride/modifier metadata through the fb/KMS present path, add
  gfxstream/cross-domain capset admission only with a real contract, and
  add native/blob present counters.
  ATTRIBUTION SLICE DONE 2026-07-04/05 — the M7 causal chain is NAMED:
  (1) "software_blit" is a classification label (the not-nouveau-native
  bucket, fb_kms_atomic.c:411); blit_bytes is a pre-branch nominal
  counter; the hot path is genuinely zero-copy (copy_ticks=0) and
  per-frame fence waits are zero. The old software-blit-ceiling theory
  is DEAD.
  (2) ROOT (H1): host GL retire back-pressure — guest GL submits stall
  in virtio_gpu_async_make_room when the depth-32 ctrl ring fills,
  waiting on QEMU/WSL-D3D12 used-ring retirement (268-354 stalls/run,
  1.5-2.2s total; stalls only on submit_3d, never flush).
  (3) CONVERTER (H2): the stalled ctx_submit HOLDS the single shared
  op_lock across its stall (virtio_gpu_user.c:338->413), so KWin's
  page-flip present (op_lock(PAGE_FLIP), virtio_gpu_scanout.c:1516)
  blocks behind it: bo_present_virtio avg 8.6ms/present (vs 1.45ms
  unblocked "last" value) -> flip-complete late -> KWin frame callback
  ~45Hz -> Chromium paced to ~44fps, dropPct ~28. Cadence is mono-modal
  ~22.7ms (throughput limiter), not vsync-beat bimodal.
  Evidence: `20260704T231500Z-kprofile-video-current-default-baseline`
  fbstat/qemu-trace + the 44.2/42.9 archives; full chain in the
  attribution report (history file/git). An instrumented run with all
  four perf flags was TRACE-PERTURBED to 5.3fps (Failure Mode 9;
  archived `20260705T003500Z-n1-instrumented-run-trace-perturbed-*`) —
  its structural reads (make_room_depth_max pinned at 32, retire sums
  >> lock_wait) are consistent; use submit_trace ALONE if re-run.
  N1 IMPLEMENTATION SLICE — PARTIAL LANDING 2026-07-05:
  (a) LANDED: async ring depth 32 -> 60 (60 is the hard descriptor-table
  ceiling: ctrl queue NUM=256 descs, slots use 8 + n*4). Validated by the
  same-binary control video run (clean full window, presentedFPS=44.8)
  and the corrected-defaults KDE gate (DONE, M4 2263 / M5 11218).
  Depth alone moves M7 only marginally (44.8 vs 43.9-44.2 band).
  (b) BLOCKED, default-OFF: the op_lock unlocked-wait retry loop
  (`virtio_gpu_submit_unlocked_wait`, mechanism landed opt-in) hit
  `PANIC thread_queue.c:213 tq_remove: queue is empty` in 2/2 default-on
  GUI runs: releasing op_lock across the make-room stall allows MULTIPLE
  concurrent waiters on the used-ring wait queue, and the
  virtio_gpu_wait_for_used sleep/wake path implicitly assumed at most
  one waiter (it only ever ran under op_lock). Bisect conclusive: the
  opt-out control with depth 60 + poll defaults ran clean. Archives:
  `20260705T010000Z-n1n5-defaults-kde-active-sample` (panic),
  `20260705T012000Z-n1n5-video-unlocked-wait-on` (panic),
  `20260705T014000Z-n1n5-video-unlocked-wait-off-control` (clean 44.8),
  `20260705T021500Z-n1n5-corrected-defaults-kde-active-sample` (DONE).
  N1 UNLOCKED-WAIT: THREE ATTEMPTS, LANE PARKED 2026-07-05 with the full
  rework scope now mapped. Attempt log (all archived):
  (1) default-on: PANIC tq_remove — concurrent completion_init on the
  shared g->async_wait; FIXED by the waiter-serialize mutex (landed).
  (2) opt-in: kernel exception in mutex_lock — MY BUG: the new mutex was
  never mutex_init'ed (this kernel's mutex_t/tq_t REQUIRES init —
  zero-init leaves broken list heads; op_lock inits at
  virtio_gpu_scanout.c:837). FIXED (init landed). The earlier
  "pending_completion torn pointer" theory was WRONG — the sync path is
  verifiably q->lock-disciplined (audited: submit_internal sets/clears
  pending_completion under spin_lock_irqsave(&q->lock)).
  (3) opt-in with both fixes: NO kernel panic, but kwin_wayland #GP at
  libc.so.6 file_off 0x9fff4 — a CANONICAL R5-family site
  (pthread_mutex_lock+4). Mechanism hypothesis: the retry loop performs
  the first-ever async REAP without op_lock held
  (make_room -> reap_completed); a racy reap can signal completion
  early / double-process a used-ring entry, so the HOST DMAs into guest
  frames already recycled -> foreign bytes in another process's fresh
  pages (the R5 corruption shape WITHOUT the R5 kernel bug). Cannot yet
  exclude a first residual R5-class observation, but the timing (first
  working unlocked-wait run after 17+ clean launches) points at the
  change. Archive `20260705T040000Z-n1-mutexinit-unlocked-wait-optin-video`.
  ATTEMPT 4 (2026-07-05): the single-consumer reap discipline was
  IMPLEMENTED and adversarially REVIEWED before any boot — the review
  returned NO-GO for unlocked-wait and convicted three blockers the
  implementation had missed, saving the VM run:
  - B1 (FIXED, landed): virtio_gpu_submit_mixed_async is a THIRD reaper
    (retire + plain async_count-- on ctx_submit's own attach path); now
    routed through the reap mutex with a q->lock'd decrement.
  - B2 (REDESIGN REQUIRED): the reap loop consumes used elements it
    cannot map — including id 0, every SYNC command's descriptor head.
    That discard was only safe because op_lock historically excluded
    reap/sync concurrency. An unlocked reaper steals sync completions ->
    5s timeouts + spurious context failures (exposure amplified by
    submit depth-for-reason default 1). Fix direction: sync commands
    through ring slots, or completion-by-response-content instead of
    used-idx occupancy.
  - B3 (REDESIGN REQUIRED): abort_all from an unlocked make_room can
    free slots mid-fill/mid-post of an op_lock'd poster -> descriptors
    published over freed memory -> host DMA corruption (the exact class
    under investigation). Fix needs a claim/fill handshake or abort
    taking op_lock — which inverts op_lock->reap order from sync-drain
    callers; not a one-liner.
  - RISK: wait_progress detects progress by used-ring OCCUPANCY, not
    MOVEMENT; any concurrent consumer erases the evidence and a healthy
    queue can be aborted after the 5s limit. Fix: snapshot-compare
    used->idx.
  LANDED from attempt 4 (default-safe hardening, battery green:
  nographic PASS, KDE DONE M4 2027/M5 11785, zero panics):
  async_reap_serialize on all three reapers + abort; reserve/reap/abort
  count+claim transitions under q->lock; tear-proof slot release
  (body-wipe first, RELEASE-store pending last); all mutexes
  initialized. These closed latent races that exist even in default
  mode.
  2026-07-05 ATTEMPT 5 — B2/B3 REDESIGN LANDED (kernel 043e88a), M7
  JUMPED TO 51 IN DEFAULT MODE; UNLOCKED-WAIT STILL NO-GO:
  Redesign (subagent-implemented, adversarially reviewed GO with 4
  findings incorporated): TOTAL reaper (sync in-flight record
  sync_inflight/sync_done/sync_stale under q->lock; only id==0 is a
  sync completion; unknown ids warn-consumed; mixed_async's bespoke
  third reaper deleted — sync post/wait shared via
  virtio_gpu_sync_post/sync_wait_done); sync posts PARK while
  sync_stale>0 (no desc[0,3) rewrite while device owes a stale element
  — closes misattribution AND double-execution); slot state machine
  FREE->CLAIMED->POSTED->FREE + ABANDONED quarantine (abort abandons
  only POSTED, never frees device-reachable memory; reaper frees
  ABANDONED on used-element arrival and records fences monotonically
  into last_fence); movement-based progress (used->idx +
  async_retire_seq snapshots; reap+recheck before abort); async
  capacity clamped to negotiated qsize ((qsize-8)/4).
  Battery: probe 5/5 PASS default boot; kde-ready DONE clean.
  A/B (chromium-video): CONTROL (default mode, unlocked-wait OFF)
  presentedFPS=51.0 decodedFPS=61.4 dropPct=9.07 — UP from the
  43.9-44.8 band; the default-mode redesign itself (total reaper, no
  bulk-swallow, shared sync path) plus the N5 poll promotion moved M7
  ~+7fps. TREATMENT (virtio_gpu_submit_unlocked_wait=1) FAILED
  session-liveness-before-chromium: GLOBAL desktop stall at t~147s —
  every polling thread parked simultaneously (poll-stuck dumps show
  68-73s parks all starting together), NO panic/corruption/refused
  lines. Hypothesis: op_lock convoy — a sync waiter (fenced sync
  deferred by virgl behind ongoing async retires) holds op_lock through
  repeated fresh 5s movement windows (review finding #8: no cumulative
  deadline on sync_wait_done), blocking every present. Memory-safe but
  a liveness regression. EXCLUDE unlocked_wait=1 runs from R5 closure.
  2026-07-05 ATTEMPT 6 (kernel a199ed6): cumulative wait-window cap
  LANDED — every movement-renewal loop (sync park, sync wait, both
  drains, make_room) now bounds TOTAL wall time at one
  virtio_gpu_irq_wait_ms window; movement renews the retry, never the
  deadline. In default mode this exactly restores the historical
  single-window failure deadline. Battery green (probe 5/5, kde-ready
  DONE). Treatment rerun: NO permanent stall, no panic/abort/refused —
  but STILL NO-GO: session limps (WaylandEventThr parked 130s, 72s
  park clusters during Chromium launch) and perf-video never reports
  start (FAIL chromium-video-perf-start-missing). With the harness's
  irq_wait_ms=60000, each wedge decision under unlocked-wait costs up
  to a 60s bounded op_lock hold, and something under unlocked-wait
  still makes a sync completion go genuinely missing (root cause NOT
  found — candidates: a lost sync_done signal race the review missed,
  or virgl withholding id-0 behind foreign async streams).
  VERDICT: lane PARKED as diminishing-returns — the default-mode
  redesign already moved M7 44->51 and the remaining gap to 60 is
  host-retire (H1) bound; two post-redesign attempts failed on
  liveness. Reopen conditions: (a) root-cause the missing sync
  completion from archive n1ab-unlocked-a6.log (scratchpad), AND
  (b) cut the interactive wedge-decision cost (per-context sync budget
  or irq_wait_ms tiering) so one missing completion cannot cost 60s of
  op_lock.
- N2 = P3 promotion: attempted 2026-07-04, NOT accepted. The default-on
  guarded battery had static/build/nographic PASS, one KDE active-sample
  PASS, explicit-off control PASS after one known visible-timeout flake,
  and M9 launch-only PASS, but an extra default-on active-sample rerun hit
  `kwin_wayland` #GP in `libQt5Core.so.5` (`kde-session-ready-crash`).
  The post-stop cold-cache A/B did not reproduce that R5-class KWin/QtCore
  signature, but stopped on direct-read ON run 5 with a new kernel page
  fault (`cr2=0x1aafdd193 err=0x2 rip=0xffff80000039a34c`, RIP resolving
  into kernel `_rodata`) after four clean ON runs, one clean OFF run, and
  one known OFF visible-timeout flake. Read-only fault mapping classifies
  that as corrupted control flow into `.rodata`: ASCII bytes decoded as a
  bogus write, not NX; the `sig_trampoline` line is a symbolization
  artifact, and the direct-read tie is still correlational. Kernel
  `04b1ee2` landed opt-in fault/direct-read diagnostics plus a narrow
  ON-only transient compound-node fallback guard. Two debug-ON KDE
  active-sample runs with `ext4_read_page_direct=1
  ext4_read_page_direct_debug=1` did not recur the kernel fault or trip an
  invariant (`20260704T191028Z-n2-direct-read-debug-on-run1-artifact-timeout`,
  `20260704T191406Z-n2-direct-read-debug-on-run2-pass`). The gate remains
  default-OFF; no promotion retry until a diagnostic repeat explains the
  fault or a fresh promotion battery has zero R5-class/kernel crashes.
  Optional second slice still exists after promotion: batch remaining
  non-sequential single-page fills (executable page-in; ~71% of fills).
- N3 = R9 cursor out-of-range (user-visible) + the KWin LibinputBackend
  nullptr payload bug found by R5 forensics (same input area). Harness
  and seat plumbing are fixed; coordinate delivery is now classified and
  has a passing contract. Root cause for the `20260704T195436Z` failure
  was the probe's default RUNPATH preferring the host
  `/usr/lib/x86_64-linux-gnu` libinput/libudev stack; udev enumeration was
  empty and libinput failed monitor/seat setup. Root cause for the
  `20260704T200703Z` coordinate failure was QEMU injection, not kernel
  scaling/storage, evdev, or libinput classification: HMP `mouse_move`
  delivered legacy PS/2 relative samples (`flags=0`, repeated/clamped
  127,127) and zero ABS samples. The strict HMP-selected rerun
  `20260704T202206Z` proved `mouse_set 3`/`info mice` selected
  `QEMU Virtio Tablet (absolute)` but HMP still produced only PS/2
  relative samples. Current harness therefore keeps HMP `info mice` as
  routing evidence, adds a QMP socket, and injects raw 0..32767 tablet
  coordinates with `input-send-event` absolute X/Y pairs. Probe PASS is
  now strict: zero evdev ABS or zero libinput absolute samples fails with
  an explicit reason. Dry-run `20260704T202724Z` PASS; real R9
  `20260704T202747Z` PASS with `/dev/mouse flags=1`, `evdev_abs_samples=10`,
  `libinput_abs_samples=5`, and `result=PASS reason=coordinate_samples`.
  Visible cursor-plane probe `scripts/gpu/r9-cursor-visible-probe.expect`
  then passed in guest cursor mode at
  `build-x86_64/r9-cursor-visible-probe-history/20260704T205234Z-r9-cursor-visible-probe`:
  dry-run proves `show-cursor=off` and no
  `virtio_gpu_host_cursor_only=1`; QMP `input-send-event` remains the
  coordinate proof; `/dev/mouse flags=1`, evdev ABS and libinput absolute
  samples are present; cursor-plane traces land at 0,0 / 640,400 /
  1279,799 with no 2x/out-of-range transform; no panic/KWin crash/
  LibinputBackend nullptr marker was seen. Framebuffer/host capture does
  not prove visible cursor pixels because the GTK hardware cursor overlay is
  outside the QEMU framebuffer capture path. Regression guards passed and
  were archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T210241Z-r9-visible-desktop-interaction-pass`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T205909Z-r9-visible-chromium-launch-only-pass`.
  2026-07-07 follow-up proof
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T005222Z-n3-guest-mouseinject-kprofile-proof`
  validated the normal KDE desktop-interaction reducers in
  `scripts/gpu/kde-plasma-desktop-smoke.expect`: forced guest input,
  disabled monitor path/host cursor sync, guest cursor mode, diagnostic-only
  HMP movement, and pre-hover settle (3000ms plus 3 stable framebuffer
  samples). PASS proof included KVM + virgl real GL/no software fallback,
  bounded pointer frame/work area, `/bin/mouseinject 11016 64223 0`, hover
  `status=changed first_changed_ms=986`, app launch PASS with
  `konsole_wait_ms=1508`, `kprofile_timeout_hit=0`, userpc stored=602
  dropped=0, no lingering QEMU, and clean diff check. N8 remains limited by
  Konsole/app-launch readiness and loader work, not mouse bounds.
  Remaining N3 work is the later/secondary KWin LibinputBackend nullptr
  payload unless a startup/input crash reproduces under the visible probe.
- N4 = P1 steps 2c/2d (cpumask atomics skip, CR0.TS shadow) for M2 <1.5us:
  ATTEMPTED + STOPPED/REVERTED 2026-07-04. Full cpumask+CR0 built but
  nographic stopped at `forktest` timeout
  (`build-x86_64/desktop-bottleneck-profile/20260704T213831Z-n4-p1-runtime-verification-nographic/`).
  The cpumask half is the likely culprit (sticky/over-inclusive fanout into
  fork/COW/TLB synchronous shootdowns); CR0.TS shadow is lower suspicion but
  was still insufficient. Cpumask was backed out; CR0-only passed functional
  nographic twice, but failed acceptance metrics, so it was reverted. N4/P1
  is not landed; next action requires a different approach.
- N5 = M8 idle cadence: MAJOR LEAD LANDED 2026-07-04/05. The 4,500/s
  poll-timeout churn is a KERNEL POLICY ARTIFACT: every blocking poll is
  sliced into 10ms rescan iterations (POLL_RESCAN_MS=10,
  vfs_syscall.c:5051) because the notify-backed full-wait fast path is
  default-OFF (`poll_notify_full_wait`, plus separate
  `af_unix_poll_notify_full_wait`; gates at vfs_syscall.c:3957-3985).
  Proof: rescan==timeout+ready exactly; mean timed wait 10.0006ms;
  ~43 slices per blocking poll whose real dwell is ~135ms; each expiry
  pays a DOUBLE full fd-set walk (kqueue rescan + vfs poll scan),
  converting idle-halt ticks into busy-rescan ticks.
  A/B with the existing flags ON (video kprofile, same window):
  timeouts 351,838 -> 29,062 (-92%), rescans -92%, notify mean wait
  9.94ms -> 23.3ms (real deadlines), app blocking behavior unchanged,
  M7 unchanged (44.0), zero crash markers. Archives:
  `20260704T231500Z-*-baseline` vs
  `20260705T001500Z-poll-notify-full-wait-video-ab-92pct-collapse`.
  The flags also have prior 07-01 KDE passes.
  N5 PROMOTION LANDED 2026-07-05: both gates default-ON in kernel code
  (opt-outs `poll_notify_full_wait=0` / `af_unix_poll_notify_full_wait=0`).
  Validated within the N1/N5 battery: nographic fork/clone/cow PASS,
  clean full video window (44.8fps), corrected-defaults KDE
  active-sample DONE (M4 2263 / M5 11218, zero crash markers) — the two
  battery panics were bisected to the (now default-off) N1
  unlocked-wait change, NOT the poll flip (the clean control ran with
  poll defaults ON). M8 idle spot-check is the remaining payoff
  measurement (next battery). Residual 366/s = fd classes still
  requiring rescan + real deadlines; re-attribute only if M8 stays red.
  2026-07-05 M8 PAYOFF + PARTIAL REVERT: both-flags idle measured 44%,
  but a real interactive session then HUNG (Wayland clients
  unresponsive; dbus client auth timeout) — consistent with a missed
  AF_UNIX readiness notify; the injected-input batteries had not caught
  it (timer traffic masks lost socket wakeups). AF_UNIX half reverted
  to default-off (kernel e6e3dab); shipped defaults re-measured 57-64%
  — M8 stays GREEN; KDE gate on the reverted kernel DONE clean.
  2026-07-05 FULL REVERT: the GLOBAL half then also froze a real
  interactive desktop (AF_UNIX already off; user A/B with both flags
  forced off restored the known-slow-but-working baseline, video
  unaffected in both). BOTH poll notify full-wait gates are back to
  default-OFF/opt-in. The 92% churn reduction is real but UNSHIPPABLE
  until the notify delivery hooks (eventfd/pipe/timerfd/kqueue wakeup
  paths) are audited for complete transition coverage and validated
  INTERACTIVELY (Failure Mode 24a — injected-input batteries pass while
  interactive sessions hang). N5 lane REOPENED with that audit as the
  path; M8 also needs a first VALID measurement (GL-pipeline boot —
  the 07-05 readings were taken on non-GL boots that never present).
  Additional methodology lesson: M8/interactive checks must use the
  documented GL recipe (QEMU_GPU=virtio-vga-gl-primary ...); the
  default non-GL virtio-gpu-primary path shows the boot gradient and
  never presents KWin output (own issue — track separately if the
  non-GL path is meant to work).
  2026-07-05 AUDIT + PRODUCER FIXES: consumer side (kqueue wait path)
  audited SAFE — triple level re-poll (register-time ops->event
  kqueue.c:1143, wait-entry rescan :1310, post-wait __vfs_poll_scan
  vfs_syscall.c:5133); timeout=-1 full-wait = tq_wait with no timer
  (kqueue.c:1497), so any lost PRODUCER notify = freeze-forever.
  Producer audit found 3 lost-notify defects; all 3 FIXED:
  (1) timerfd.c timer-IRQ deferral: on queue_work failure the old code
      cleared work_pending AND set armed=false — dropped the notify and
      permanently killed repeating timers. Now: wq==NULL (boot) drops
      cleanly; queue_work-failure leaves notify_pending/armed intact
      (the running worker's re-check loop consumes them) and clears
      only work_pending so the next expiry re-attempts.
  (2) pipe.c blocking write: with the ring full, write() parked in
      __pipe_wait_reader WITHOUT ever firing the EVFILT_READ knote
      (the only notify was at end-of-write, unreachable while
      blocked) — a poll-only reader deadlocked against the blocked
      writer. Now the writable==0 branch fires
      vfs_file_knote_notify(read_file, EVFILT_READ) (writer_lock
      fdup protocol, notify outside the lock) before waiting.
  (3) vfs_syscall.c inotify_emit_locked: only the FIRST matching
      watcher's fd was knote-notified per event; 2nd+ inotify fds
      polling the same inode never woke. Now an inotify_notify_set
      (bounded 8, pointer-deduped, overflow logged) collects ALL
      queued watcher files; callers fire the whole set outside the
      global lock.
  Reducer: /bin/poll-notify-probe (scripts/image/poll-notify-probe.c,
  staged into the image) — 3 tests under poll_notify_full_wait=1 with
  15s watchdogs. Pre-fix kernel: pipe-blocking-write FAIL (reproduced
  the deadlock exactly); timerfd passes pre-fix (its defect is a rare
  queue_work race, kept as regression cover); inotify passes pre-fix
  only because truncate/write/close emit a multi-event cascade and the
  first watcher exits between events (single-event gap still real).
  Post-fix: probe RESULT=PASS 3/3 with BOTH gates forced ON, and
  RESULT=PASS 3/3 on a default (gates-OFF) boot — failing-then-passing
  reducer complete. (Probe note: pipe reader must treat read()==0 as
  EOF-after-writer-exit, not error.)
  2026-07-05 ROUND 1 INTERACTIVE: STILL FROZEN ("no response") — the
  three producer fixes were necessary but not sufficient.
  STUCK-POLLER DIAGNOSTIC (landed, on whenever poll_notify_full_wait=1):
  notify-backed full waits with timeout<0 or >=5s register a park entry
  (pid/comm/fd classes captured in the poller's own context, unix paths
  included); any other blocking poller dumps entries parked >10s
  (re-dump every 30s so frozen-forever is distinguishable from
  wake-and-repark). Live KDE dumps isolated the culprit: KWin's
  libinput-connec thread, poll(-1) on {eventfd, epoll-fd}, parked in
  ONE episode from t=39s for the whole session — input dead, rendering
  alive (KWin/plasmashell main loops never appeared: healthy).
  Reducer tests 4 (poll parked ON an epoll fd, pipe producer) and
  5 (cross-process eventfd) both PASS gates-ON → generic
  epoll-propagation and eventfd links are sound.
  ROOT CAUSE #4 (THE interactive killer), FIXED in dev/evdev.c:
  kqueue attach/notify LIST MISMATCH. knote_read/write_attach prefers
  the per-open FILE knote list whenever f->ops->poll exists; evdev
  installs evdev_file_ops (with .poll) via cdev.ops.open_file, so
  epoll/poll knotes for /dev/input/eventN land on the FILE list. But
  evdev's producer notify() only called cdev_knote_notify(&st->cdev)
  — the CDEV list, which stays empty. Input readiness therefore NEVER
  produced a kqueue wakeup; default mode was saved by epoll's 20ms
  rescan, gate-ON full wait froze input forever. Fix: evdev_client
  keeps its open vfs_file (set in open_file, protected by st->lock);
  notify() snapshots client files under st->lock (vfs_fdup) and fires
  vfs_file_knote_notify(EVFILT_READ) after unlock (kqueue_wait holds
  kq->lock while calling ops->poll which takes st->lock — notifying
  under st->lock would ABBA). AUDIT THE SAME MISMATCH ELSEWHERE: any
  cdev whose open_file installs poll-bearing file ops but whose
  producer only calls cdev_knote_notify (check ps2kbd/ps2mouse generic
  cdev wrapper path, ttys).
  Related audit findings (separate lane, rescan-masked today, NOT the
  gate killer): PTY slave-side readiness is structurally un-notifiable
  (pty_pair has no slave file pointer; tty_input commit points
  tty.c:403/421/378 and pty_slave_hangup only tq_wakeup) — must be
  fixed before pts fds could ever be flagged notify-backed; signalfd is
  a stub (poll always 0); unconnected AF_UNIX DGRAM sendto delivery is
  unimplemented (sendto rejects addresses, sendmsg ignores msg_name).
  2026-07-05 ROUND 2 VALIDATION + PROMOTION (kernel b231117): the
  evdev-fixed gates-ON KDE session ran with working input across
  multiple real interaction bursts (stuck-poller telemetry: the
  libinput thread woke on every burst; no thread re-froze), and the
  user moved work forward on that basis. BOTH GATES ARE DEFAULT-ON
  again (opt-out poll_notify_full_wait=0 / af_unix_poll_notify_full_wait=0).
  Validation chain on the promoted kernel: 5-test poll-notify-probe
  PASS on a default boot (gates active by default, diagnostic armed);
  kde-ready smoke DONE clean. CAVEAT: the desktop-interaction-latency
  visibility reducer FAILED IDENTICALLY with gates ON and OFF from the
  agent's headless shell (no screendump artifacts were ever written) —
  an environment limitation, not a flip regression; treat that reducer
  as runnable only from a display-attached session. The stuck-poller
  diagnostic stays active whenever the gate is on (zero cost
  otherwise) and is the standing tripwire for any remaining
  lost-notify class: `poll-stuck:` on serial names the fd classes.
  NEXT for N5: M8 idle-cadence payoff measurement on a GL boot
  (expected large drop in poll-timeout churn / idle wakeups) — DEFERRED
  per user (2026-07-05): interactive freeze is gone but the desktop
  still "responds slowly" → the standing R7/M4 perf lane is now the
  priority, N1 unlocked-wait redesign in progress.
  PTY SLAVE NOTIFY LANDED (kernel d88108b): pty_pair.slave_files[4]
  registry; master-write → slave EVFILT_READ + echo → master notify;
  master-close hangup → raw_wait wake + slave POLLHUP notify; last
  slave close → master EOF notify. pts fds remain rescan-class (NOT
  notify-backed) until the ioctl-driven readability transitions
  (termios canon/raw flips, TIOCSTI-style injection) are audited; the
  notify already wakes kqueue waiters instantly instead of at the next
  10ms rescan boundary (keystroke latency win).
- N6 = R2 PCID stale-TLB lane: RESOLVED 2026-07-04 — retest DONE, lane
  retired as a corruption lane, default stays OFF for perf reasons.
  (a) Safety: offline audit (GO) verified every noflush-specific hazard is
  covered (trapframe slot has its own invlpg; ASID recycle is
  generation-flushed; the only anon-free paths are the R5-fixed ones) and
  re-verified the 07-02 crash signatures as the R5 recycled-frame family.
  Opt-in retest on the R5-fixed kernel (`x86_pcid=1 x86_cr3_noflush=1`,
  both tokens + `max ASID = 4095` verified per boot): nographic
  fork/clone/cow PASS, and 3/3 KDE active-sample DONE with ZERO
  KWin/corruption markers — the 07-02 trial corrupted within 2 runs, so
  the "PCID corruption" is CONFIRMED to have been the R5
  free-before-shootdown bug. Archives
  `20260704T221000Z/222000Z/223000Z-n6-pcid-noflush-retest-kde{1,2,3}`.
  (b) Perf: same-session nographic A/B — tlb_amplification roughly HALVED
  (256/512/1024pg: 1479/1781/4085 -> 911/770/2364ns) but getpid_ns
  unchanged; and the KDE battery shows a consistent desktop REGRESSION:
  M4 2697-2867 (vs 1883-2144 band) and M5 15570-16742 (3/3 above the
  15000 goal). Mechanism: with PCID active every page/range shootdown
  degrades to a full global flush (invlpg cannot cross PCIDs;
  vm_remote_sfence_page forces CR4.PGE toggles), so desktop
  COW/fault shootdown traffic pays more than the syscall path saves.
  VERDICT: keep PCID/noflush default-OFF. Future re-evaluation condition:
  implement INVPCID-based per-PCID single-page/range flushes (CPUID
  check + fallback), then rerun this exact A/B; only promote if M4/M5
  hold within noise. R5 closure accrual from this battery: +3 (12/30+).
- N7 = timer tick loss: LANDED 2026-07-05 (kernel 4bda525).
  Discovery during implementation: the sched_timer wheel was ALREADY
  TSC-driven (sched_timer_refresh_ms absolute-ms expiry) — sleep_ms/
  timerfd/tq deadlines never lost time; the 14% loss bit ONLY the
  get_jiffs() consumers (uptime, poll/ppoll deadline arithmetic,
  itimer bookkeeping, lwip timers, cache aging), which ran slow and
  diverged from the wheel clock. Fix: get_jiffs() derives ms from the
  calibrated TSC (rounded mult, ~0.2ppm vs the wheel) behind an
  advance-only CAS-max clamp (monotonic across CPUs, one CAS/ms, no
  locks); BSP tick accounts the full elapsed span; kstats v9 adds
  timer_bsp_ticks_total + timer_jiffies_tsc_comp_ms_total. Gate:
  TSC>=1MHz AND (InvTSC bit OR hypervisor bit — QEMU does not
  advertise InvTSC; first battery caught the gate disabling the fix,
  boot line is the proof: '[x86] jiffies: TSC-compensated
  (mult=1624)'). Opt-out timer_tsc_jiffies=0 for A/B. Adversarially
  reviewed (GO; 3 RISKY fixes incorporated). Battery: probe 5/5 PASS,
  kde-ready DONE clean. Note for measurement lanes: guest uptime and
  all jiffies-based rates now run ~16% faster under load than old
  archives — do not compare raw jiffies-derived counters across the
  boundary without normalizing.
- Continuous: R5 statistical closure (9/30+ clean attempt-1 KWin launches
  accrued; count every future battery), R3 recurrence watch (rcu_head
  double-free may share the R5 root cause — one `slab_alloc: repairing
  corrupt freelist cache='rcu_head_cache'` line was seen 07-04 pre-R5-fix).
FS-churn attribution 2026-07-05 (vfs_trace_all=1 video boot, 4,477
opens traced): the ~370 opens/s from the kprofile window is MOSTLY
MEASUREMENT MACHINERY — kde-process-probe /proc scans (684) + the
harness samplers/kde-session scripts (826) dominate; among real
desktop processes kwin_wayland leads (1,399) and its churn is
repeated GL/GLX dlopen SEARCH-PATH PROBING (~180 opens across 26
rounds of libGLX.so.1/libGL.so.1 over 10+ path variants — consistent
with ext4_lookup_enoent being 86% of driver lookups). In a live
session without the harness the background churn is far lower.
VERDICT: not the interactive-slowness culprit; keep as a minor
optimization note (dlopen path-scan caching or a slimmer ld search
path for kwin would cut the ENOENT storms).
NEW LANE P0-PREEMPT (2026-07-05, THE systemic desktop-slowness root
cause — supersedes per-subsystem latency lanes for R7):
MEASURED: wake-to-run trace (kde_wake_to_run_trace=<ms> +
wake_to_run_trace_all=1, gate-cache aliasing bug fixed in
kde_ready_trace.c — each cmdline gate now has its own cache) showed
kernel threads (rcu_cb/N pinned, tty_input) taking 50-500ms routinely
and 1.4s in clusters from wakeup to first run on a live desktop.
AUDIT (verified with file:line): the kernel is FULLY COOPERATIVE in
kernel mode. NEEDS_RESCHED is set by ticks/IPIs/wakeups but honored
ONLY at return-to-user (trap.c:977) and the idle loop
(start_kernel.c:229). Kernel-mode trap epilogue (trap.c:2469-2474)
irets straight back; zero cond_resched sites exist. Any long syscall
or kthread batch holds its CPU until voluntary yield. VERIFIED-OK:
wakeup enqueue + idle kick + IPIs (sched.c:426-596), sti;hlt idle
race-free, priorities, tick preemption of USER mode. AMPLIFIER for
the 1.4s clusters: printf = synchronous UART busy-wait under global
pr.lock with IRQs off (printf.c:135, uart.c:261-273) — log bursts
serialize CPUs machine-wide.
FIXES LANDED (kernel f89b60b, battery green: probe 5/5, kde-ready
DONE, zero assertions): (1) IRQ-exit kernel preemption behind
kernel_preempt=1 default-on; (2) async console (klog ring + drain,
panic-synchronous fallback) behind console_async=1 default-on;
(3) preemption-safe per-CPU asserts: 11 rwsem/mutex/semaphore debug
asserts sampled mycpu()->spin_depth with IF=1 — with migration now
possible at any IF=1 instruction they could read ANOTHER CPU's
counter and false-panic (first battery caught it: rwsem assert in
kded5 rseq user-return); spin_depth_snapshot() samples under
push_off; wakeup-path assertion wrapped; scheduler_yield preamble
pinned. STILL OPEN in this lane: cond_resched checkpoints (belt and
braces), wake-list re-placement (minor), RISKY-3 accounting inflation
(each IRQ-exit preempt re-runs __do_timer_tick + counts a tick —
utilization/EEVDF slice skew, correctness OK), RISKY-4 console
cross-stream ordering (smoke scripts keying on kernel-vs-app serial
ordering may flake; console_async=0 to bisect). A/B flags:
kernel_preempt=0, console_async=0.
2026-07-05 POST-LANDING VERDICT: wake-to-run tails collapsed (1.4s
clusters gone; residual periodic ~200-400ms pairs on rcu_cb/0 +
tty_input, follow-up: FIFO-class placement has no idle-pull;
open item). BUT the USER reports 'improvement is not obvious' for the
seconds-scale hover/tooltip/menu latency.
2026-07-06 KPROFILE REFINEMENT: after QtQuick GL fallback rejection,
active visibility sampling, and prompt-safe `poll-stuck:` gating, the
corrected kprofile artifacts identify the current user-visible bottleneck
as Konsole shell readiness/app-launch wait, especially pre-PTY
poll/ppoll waits. The earlier DRM/frame-clock/ghost-frame idea remains a
parked visual-cadence suspicion, not the active top bottleneck. GPU/fence/
input chains remain guardrails, and no native-present credit is implied.
Recommended execution order: (1) N8 Linux-like Plasma responsiveness
(Konsole/poll readiness, kprofile-driven); (2) N1 M7 present-path work
only if the video/FPS lane is resumed; (3) N5/M8 idle payoff after the
responsiveness bottleneck is reduced; (4) N2 retry ONLY after its fault
diagnosis gate; (5) N3 residual LibinputBackend nullptr; (6) a NEW P1
approach for M2 <1.5us (N4 cpumask/CR0.TS is dead: the cpumask half
stalls forktest, CR0-only missed targets — do not re-apply the saved
patches; find a different cost).

- Parked: CR-3 (%fs selector reload semantics), CR-7 (starve-probe RCU),
  CR-9 (timer 1-jiffy boundary race, needs timerfd reducer); latent
  hugepage-path bugs (list in the R5 lane — they BLOCK re-enabling
  `vma_file_hugepage_collapse_enabled`); Chromium GL conformance depth
  beyond the validated ladder (only if real workloads hit gaps); KDE
  session-stability residuals (see that lane).

## Work Style — Time Budget and Batching (binding)

1. CODE-FIRST: finish the whole code slice (source read end-to-end,
   hypothesis in the lane, fix + reducer + gated probes) BEFORE any VM
   boot. VM runs validate completed slices.
2. BATCH GATES: one battery validates a batch of independently-revertable
   changes: nographic fork/clone/cow + one KDE active-sample pass + one
   Chromium launch-only guard + M8 spot-check. Bisect only on failure.
3. OFFLINE FIRST: parsers/classifiers/forensics run against the existing
   archives (700+ smoke runs, profile dirs) — never a VM boot for a parser
   change. The 07-04 R5 root-cause (5 subagent passes over archives +
   sources, zero exploratory boots) is the reference example.
4. REUSE BOOTED VMS: plan the probe list before booting; collect
   everything in one session. Budget <=2 boots per lane slice + the final
   battery.
5. EXCEPTION: genuinely run-variant work (flake statistics, corruption
   repro, freeze repro) may use as many runs as the evidence requires.
6. Track the implementation-vs-validation split per lane update; if
   validation dominates twice in a row, stop and re-plan.
7. ORCHESTRATE, DO NOT DO: the handoff agent is an ORCHESTRATOR. Decompose
   each slice into jobs and spawn one subagent per job (source
   read-through, implementation, offline forensics, gate runs, log
   triage); run independent jobs concurrently; keep own context for
   synthesis and decisions. Subagents return conclusions/diffs, never flip
   defaults, never push.
8. COMMIT PERIODICALLY: at every verified checkpoint and before ending,
   commit repo + submodules (kernel, ports, ports/mesa/src, user)
   deepest-first with lane-scoped messages, then update parent pointers.
   Never push without explicit user approval.

## Known Failure Modes — Do Not Repeat

Check BEFORE declaring any gate failed or hypothesis confirmed.

1. Wrong session lane: `host_chromium=1` is weston-only; launch Chromium
   in KDE via `/bin/wayland-chromium <url>` with
   `XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root WAYLAND_DISPLAY=wayland-0`.
2. Passive-visible-detector flake: always set
   `KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`.
3. Silently dropped kernel flags: every run log must show the intended
   tokens in `x86 kernel cmdline` (and `vm_asid_init: max ASID` for PCID).
4. Serial console truncates ~55-60 chars, drops trailing `&`, splits
   output mid-token. Stage script files or debugfs-inject; keep commands
   short.
5. Single-sample misdiagnosis: hangs need multiple timed GDB samples plus
   an executed (not merely echoed) guest liveness command.
6. Nographic gates do NOT validate VM/TLB/scheduler correctness (the PCID
   flip passed nographic then corrupted KWin). GUI-scale gates required;
   audits need a failing-then-passing reducer.
7. Harness label vs app truth: judge by post-evidence/status files and
   guest logs, not wrapper labels.
8. Stale runtime: check kernel/fs.img mtimes vs the running QEMU.
9. Trace volume perturbs timing: thresholded/role-scoped flags only in
   timing runs.
10. Debugcon timestamps are per-boot TSC — never cross-boot wall clock.
11. Starvation-probe lines alone are not freeze proof while liveness
    commands execute.
12. Background Chromium intentionally in freeze repros; short commands.
13. >60-char/multi-line serial commands can wedge bash in
    quote-continuation (recover with a lone closing quote).
14. QEMU gdbstub wedges after a killed gdb ("target is running") —
    restart the VM; always detach; sample against kernel.elf (xv6.bin is a
    bzImage).
15. Desktop-smoke PASS with `chromium=-1` says nothing about M9; judge
    Chromium only by the chromium reducer or manual launch + guest logs
    (`debugfs -R 'cat /host-gui-wayland-chromium.log' build-x86_64/fs.img`).
16. Uncommitted behavior-affecting diffs are handover landmines: commit,
    revert, or list them in this plan.
17. KPROFILE-mode bookkeeping: in `KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE=1`
    runs, `status=FAIL reason=launch-evidence-missing` is EXPECTED
    (kprofile replaces the launch-evidence stream) — not a playback
    failure. Read the kprofile log + PERF-VIDEO lines instead.
18. Serial wait-markers must not appear in the echoed command itself (a
    grep for `MARKER` matches the echo and fires early — killed a cowtest
    run twice). Emit markers via a variable: `M=XX; cmd; echo ${M}RES=$?`.
19. Artifact/visible-timeout harness flake class
    (`*-artifact-timeout`, visible-parser/helper timeouts): 17+ prior
    archives; KWin/desktop healthy. Archive + rerun once.
20. konsole-shell-ready-timeout flake: konsole launches but the ready
    marker never appears (45s timeout, zombie) -> M4 unmeasured. Rerun
    once; escalate only if frequency rises.
21. When host WSLg PulseAudio is down AND a pactl probe is explicitly
    enabled, the guest pactl fault cascades into `kde-session-ready-crash`
    (recovery: `wsl --shutdown` from Windows). Default runs skip pactl.
22. HMP `mouse_move` delivers legacy PS/2 RELATIVE samples regardless of
    `mouse_set` selecting the absolute virtio tablet. Inject absolute
    coordinates ONLY via QMP `input-send-event` (R9 lesson; two probe
    runs burned).
23. Guest probes must pin RUNPATH/LD_LIBRARY_PATH to the intended guest
    libs: a probe's default RUNPATH preferred the host
    /usr/lib/x86_64-linux-gnu stack and silently broke udev/libinput
    enumeration.
24a. Injected-input reducers do NOT prove interactive responsiveness:
    the AF_UNIX poll-notify default passed the desktop-interaction
    battery yet hung a real interactive session (timer traffic masks
    lost socket wakeups). Wakeup-semantics changes need an interactive
    (human or QMP raw-input) check before default promotion.
24. Single-waiter wait-queue invariants: paths that historically ran
    under a big lock (e.g. virtio_gpu_wait_for_used under op_lock) may
    implicitly assume at most ONE waiter on their tq; allowing
    concurrent waiters panics `tq_remove: queue is empty`
    (thread_queue.c:213). Audit tq usage before lock-scope reductions.
    Also: a harness `prompt-sync-timeout` label can MASK a kernel panic
    — always grep the archived run.log for PANIC/IPI_REASON_CRASH
    before classifying as harness flake.

## Guardrails

- NO un-gated default flips (kernel cmdline defaults, launcher env,
  image/session config). Diagnostics are opt-in default-off. A default may
  change only with: same-session A/B + explicit-off control, a plan entry
  naming the lane, and the regression battery. (Executed examples:
  ordered-pageflip 07-04 PASS; PCID 07-02 FAIL-and-reverted.)
- Regression battery after ANY behavior change: KDE active-sample smoke +
  Chromium launch-only no-worse-than-scoreboard + M4/M5/M8 within noise.
  New failure class = revert first.
- Handover hygiene: `git status` at top level AND every submodule
  (kernel, ports, ports/mesa/src, user); every behavior-affecting diff
  committed, reverted, or listed here. Current dirty-by-design: NONE (tree
  clean as of the 07-04 compaction; `ports/xz/src` if dirty is unrelated —
  do not revert).
- Closed lanes — do NOT reopen without new evidence: scheduler
  wake-to-run latency, futex key/timeout drift, poll/kqueue/AF_UNIX/
  eventfd/pipe primitives, guest cursor upload, raw PTY setup, tiny
  Wayland frame delivery, D-Bus/eventfd readiness, renderer admission
  (R8), inotify FIONREAD (R7b).
- One compile/VM lane at a time; `pgrep -af qemu-system` first; never
  launch QEMU with a trailing `&` (use background task machinery).
- Default-off diagnostic knobs available: `rcu_head_trace=1` (R3 owner
  history), `sched_starve_probe=1` (P0 freeze telemetry),
  `rq_identify_linear_scan=1` (R7a control), `ext4_read_page_direct=1`
  (P3, pending promotion), `kde_pactl_probe=1`, `kde_network_status_sni=1`,
  `kde_plasma_systemtray=1`, `kde_pre_kwin_libinput_probe=1`,
  `KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE` (diagnostic-only),
  `WAYLAND_CHROMIUM_BUNDLED_GL=1` (emergency/diagnostic only — never the
  solution), kwin alloc/loader trace knobs.

## Lanes — Compact Status

### P0 — Chromium/YouTube freeze: CLOSED (2026-07-03)

Structural idle-pull fix landed (kernel `ef2dab6` line); M1 battery 3/3
responsive, probe-silent replay 3. Durable repro recipe for future M1
replays: boot the KDE image
(`DISPLAY_MODE=gtk USE_KVM=1 QEMU_GPU=virtio-vga-gl-primary QEMU_INPUT=virtio
QEMU_NET=1 QEMU_APPEND='root=/dev/disk0 video=1280x800 netsurf=0 webkit=0'`),
then from serial:
`XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root WAYLAND_DISPLAY=wayland-0
/bin/wayland-chromium https://www.youtube.com/` backgrounded, 15-min
liveness (`ALIVE_n` commands must EXECUTE). Evidence:
`build-x86_64/yt-mainpage-freeze-repro/`.

### P1 — Syscall overhead (M2/M3): N4 stopped/reverted

2a (FS_BASE cache) + 2b (trapframe direct) landed; accepted baseline M2
1.65-1.94us and M3 noisy 2.7-3.7us. N4 attempted 2026-07-04 but did not
land. Full patch = cpumask hot-path skip + CR0.TS shadow; it built, then
nographic stopped at `forktest` timeout in
`build-x86_64/desktop-bottleneck-profile/20260704T213831Z-n4-p1-runtime-verification-nographic/`.
Analysis: cpumask sticky/over-inclusive behavior likely caused fork/COW/TLB
synchronous shootdown fanout/stall; CR0.TS shadow is lower suspicion.

Recovery: cpumask half backed out. CR0-only passed functional nographic in
`build-x86_64/desktop-bottleneck-profile/20260704T215704Z-n4-p1-cr0only-nographic-gate-resync/`
but missed acceptance (`getpid_ns=1739`, M3=4818). Clean rerun
`build-x86_64/desktop-bottleneck-profile/20260704T220417Z-n4-p1-cr0only-clean-nographic-gate/`
again passed functional nographic but failed metrics: M2
1973/2102/1925/2185ns, M3 1024-page 4504ns. CR0-only patch reverted; final
repo state was clean; N4/P1 is not landed. Saved patches:
`/tmp/n4-p1-current-20260704T215021Z.patch`,
`/tmp/n4-p1-cr0only-final-20260704T220032Z.patch`,
`/tmp/n4-p1-cr0only-current-20260704T220245Z.patch`, and artifact copy
`build-x86_64/desktop-bottleneck-profile/20260704T220417Z-n4-p1-cr0only-clean-nographic-gate/saved-cr0only.patch`.
Next action: choose a different P1 approach that accounts for fork/COW/TLB
fanout risk; do not mark N4 passing from these runs. CR-3 (%fs selector
reload) remains the latent ABI follow-up.

### P2 — Ordered page flip: steps 1-2 DONE, step 3 = N1

Step 1 direct-KMS A/B: 100 -> 118-125 FPS. Step 2 default flip LANDED
2026-07-04 (kernel `69310a3`, default-aware helper in
`virtio_gpu_scanout.c`; opt out `virtio_gpu_ordered_page_flip=0`):
default-on KDE pass (M4 2144), explicit-off control pass (M4 2122), video
36.8 -> 42.9. Archives `20260704T181500Z/183000Z/185000Z-q7-*`.
Step 3 (zero-copy present, M7 >= 55) is N1: every flip is still
`software_blit` with `native_present_credit=0` (fbstat), the sole
remaining M7 ceiling. N1 route (a) now has fail-closed launcher selectors
for rutabaga/gfxstream/blob plus a `QEMU_BIN` override, but this host
cannot run them: no hardware `/dev/dri/renderD*`, no rutabaga QEMU
device, and broken classic `*-gl` device help. Kernel blob resources and
host-visible mmap already exist, but creatable capsets are VIRGL/VIRGL2
only; scanout-blob/native-present and gfxstream/cross-domain admission are
future slices once a capable host route is available.

### P3 — Ext4 read-path serialization: first slice LANDED gated; N2 stopped

`ext4fs_pcache_read_page` held the per-mount esb mutex across device
waits, serializing all readers/faulters (0.86ms/fill, ~29s per video
window). Fix (kernel `2dee9b7`, gate `ext4_read_page_direct=1` default
OFF): resolve mapping under the lock, direct BIO, release before
`bio_await`; bcache-coherent (any cached block falls back/copies under
lock — dirty data lives in the lwext4 bcache via the write path).
A/B same workload: read_page_ms 29016 -> 14469 (-50%), lookup lock wait
-52%, ext4_fault_ms -66%, browser start 10.63s -> 6.17s (-42%); video
unchanged (present-path ceiling); fork/clone/cow + KDE battery green.
Archive `20260704T151500Z-p3-ext4-read-page-direct-chromium-kprofile-ab-pass`.

N2 guarded promotion attempt 2026-07-04: source flip made omitted token
default-on and explicit `ext4_read_page_direct=0` default-off control, then
ran the acceptance battery. Static/build PASS (`git diff --check`,
`git -C kernel diff --check`, kernel build). Nographic fork safety PASS:
`forktest` rc=1 known exhaustion signature, `clonetest` rc=0, `cowtest`
rc=0, boot cmdline had no direct-read token. KDE active-sample default-on
PASS with no direct-read token
(`20260704T181959Z-n2-ext4-direct-default-on-kde-active-sample-pass`;
M4 3518, M5 13420). Explicit-off control with
`QEMU_APPEND_EXTRA=ext4_read_page_direct=0` had one known visible-timeout
flake (`20260704T182300Z-n2-ext4-direct-explicit-off-kde-visible-timeout-rerun-needed`)
then PASS
(`20260704T182457Z-n2-ext4-direct-explicit-off-kde-active-sample-control-pass`;
M4 2365, M5 10015, cmdline proved token present). M9 launch-only default
real-GL guard PASS with software/bundled GL env unset
(`20260704T182703Z-n2-ext4-direct-default-on-chromium-launch-only-m9-pass`;
GPU/init/GL request errors 0, no `--use-gl`/ANGLE fallback args).
However, an extra default-on KDE active-sample rerun produced
`KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash`: `kwin_wayland`
#GP in `/usr/lib/x86_64-linux-gnu/libQt5Core.so.5`, no direct-read token
in cmdline
(`20260704T183012Z-n2-ext4-direct-default-on-kde-kwin-gp-stop`). Because
the N2 risk list includes KWin/ld.so recycled-byte corruption, the battery
is not clear/safe. The source flip was reverted before commit; current
state remains default-OFF with opt-in `ext4_read_page_direct=1`. Next
diagnostic is crash-focused cold-cache A/B (3-5 default-on vs explicit-off
KDE active-sample runs plus KWin fault/core/PTE or frame evidence if the
#GP recurs). Optional second slice = batch the remaining non-sequential
single-page fills (executable page-in pattern, ~71% of fills).

Post-stop cold-cache crash-focused A/B 2026-07-04 used the reverted
default-OFF source with explicit tokens in both arms
(`QEMU_APPEND_EXTRA=ext4_read_page_direct=1` vs `=0`) and
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, `QEMU_AUDIO_BACKEND=none`, with
software/bundled GL fallback env unset. Result before stop: ON PASS x4
(`20260704T183909Z-n2-ab-direct-read-on-run1-pass`,
`20260704T184121Z-n2-ab-direct-read-on-run2-pass`,
`20260704T184327Z-n2-ab-direct-read-on-run3-pass`,
`20260704T184741Z-n2-ab-direct-read-on-run4-pass`), OFF PASS x1
(`20260704T184534Z-n2-ab-direct-read-off-run1-pass`), OFF known
visible-timeout flake x1
(`20260704T185037Z-n2-ab-direct-read-off-run2-visible-timeout`), then ON
run 5 stopped on a new kernel page fault
(`20260704T185323Z-n2-ab-direct-read-on-run5-kernel-page-fault-stop`):
`*** KERNEL PAGE FAULT: cr2=0x1aafdd193 err=0x2
rip=0xffff80000039a34c`, backtrace line
`sig_trampoline.S:29: sig_trampoline+250691`, `Core: 2`, idle thread,
panic at `kernel/arch/x86_64/irq/trap.c:2023`. Symbol resolution maps the
RIP into kernel `_rodata`, not a normal function body. No A/B archive
matched the prior R5 KWin/QtCore signature (`kwin_wayland` #GP at
`libQt5Core.so.5` file offset `0xdbc9f`, bad pointer
`0x2d34365f3638782f`). Classification: not an R5 recurrence, but the
direct-read arm produced a new kernel fault before the recommended 5x5
could complete, so N2 default promotion remains blocked and default-OFF is
the required state.

Read-only fault mapping resolved the stop more precisely: RIP
`0xffff80000039a34c` is image offset `0x39a34c` inside `.rodata`
(`_rodata` range `0xffff80000035e000..0xffff8000003b7000`), adjacent to
ASCII strings near `Operations` / `TEST: synchronize_rcu()`. Decoding
those bytes yields a bogus write, matching `cr2=0x1aafdd193 err=0x2`
(supervisor write to a non-present low/user-looking address), so this is
corrupted control flow into read-only data, not an NX fault or real
`sig_trampoline` execution. `rbp=0xbefc6cc0` was outside the expected
kstack, so the unwind is secondary/bad; idle context is real but not a
root cause. No stack/register evidence currently places the CPU in
ext4/pcache/bio.

Diagnostic commit kernel `04b1ee2` (`kernel: add n2 direct-read fault
diagnostics`) makes a repeat actionable without changing defaults:
unrecoverable x86 kernel #PF now prints current task, CR3/page-table
identity, full registers, PTE walks for CR2/RIP/RSP/RBP in active/kernel/
current spaces, instruction bytes when mapped, stack words when RSP is on
the current kstack, a `.rodata` execution classifier, and an ext4 direct
read ring dump if enabled. `ext4_read_page_direct_debug=1` adds an opt-in
64-entry direct-read ring plus invariant checks around `bio_add_folio()`
and `bio_await()`. The same commit also adds an ON-only guard that falls
back for transient compound-folio node metadata while direct-read is
enabled; this does not affect default boots because
`ext4_read_page_direct` remains default-OFF.

Bounded diagnostic validation 2026-07-04 used:
`QEMU_APPEND_EXTRA='ext4_read_page_direct=1 ext4_read_page_direct_debug=1'`,
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, `QEMU_AUDIO_BACKEND=none`, and
the software/bundled GL fallback env unset. Kernel build PASS
(`cmake --build build-x86_64 --target kernel -j2`). Run 1 archived
`20260704T191028Z-n2-direct-read-debug-on-run1-artifact-timeout`: known
artifact-timeout class, no kernel #PF/panic/invariant dump markers, cmdline
proved both direct-read tokens. Run 2 archived
`20260704T191406Z-n2-direct-read-debug-on-run2-pass`: status code 0 /
`KDE-PLASMA-DESKTOP-SMOKE-DONE`, cmdline proved both direct-read tokens,
and log search found no `KERNEL PAGE FAULT`, `kernel-pf-context`,
`ext4-direct-read`, invariant, or panic markers. No explicit-off control
was run in this diagnostic slice because the ON-arm fault did not recur in
the two bounded ON runs.

### Q2 / R8 — Chromium real GL: DONE (validated 2026-07-04)

History: "window not visible" was never renderer admission (exonerated);
a real Mesa EGL attr-order bug was fixed (`5e3f4bebe`); the default-path
blocker was Chromium 150's passthrough decoder requiring the ANGLE
extension ladder. Direction settled by the user: do not skip GPU
acceleration. Current accepted GPU path is classic KVM/virgl real GL; no
software/bundled fallback.
Implementation (all in `ports/mesa/src`, HEAD `fb2724503`):
`xv6_angle_passthrough.c` + GLES dispatch for
`GL_ANGLE_robust_client_memory`, `GL_CHROMIUM_bind_generates_resource`,
`GL_ANGLE_client_arrays`, `GL_ANGLE_request_extension`;
`GL_CHROMIUM_copy_texture` (blit/shader paths, `mesacopytexture`
reducer); robust uniform length fix; context-gated
`GL_ANGLE_webgl_compatibility` via
`EGL_ANGLE_create_context_webgl_compatibility` (`mesaanglepassthrough`
reducer); `GL_KHR_debug` pre-existing.
Runtime validation 2026-07-04: image staging verified (fresh Mesa in
/lib, /lib/dri first, no LD shadowing); M9 launch-only PASS on the
default path — zero missing-GL fatals, zero `--use-gl=disabled`
fallbacks, GPU errors 0 (archive
`20260704T192000Z-q2-mesa-angle-ladder-launch-only-first-default-path-pass`);
full video 44.2 presentedFPS / 29.6% drops across 75s of real GL
(archive `20260704T195500Z-q2-real-gl-full-video-m7-44fps`).
Ground truth: Chrome 150's validating decoder is DEAD (runtime-refused),
so the ladder was the only real-GL route; bundled ANGLE stays
disabled/symlinked to guest Mesa. The earlier bundled-ANGLE failure class
is understood (guest Mesa exposes zero pbuffer EGL configs; surfaceless
works) — irrelevant while the default path holds.
Session-stability fixes that unblocked the Q2 gates (landed 07-04 by the
parallel round): wl_shm interface-version interposition fix in
`wayland-shm.c` + `kde-wayland-registry` reducer; KDE ABI closure for
libinput/libudev (gesture-event + `udev_*@LIBUDEV_183` symbols, input-seat
enumeration in the shim); pactl removed from readiness path;
plasmashell system tray + artificial network SNI now opt-in
(`kde_plasma_systemtray=1`, `kde_network_status_sni=1`) after the tray
`compactRepresentationItem` KCrash isolation.
OPEN residuals (park unless they block a gate): plasmashell system-tray
crash root cause (tray stays opt-in), one `xkbcomp` #GP class, one
liveness roundtrip race archive, GL conformance depth beyond the ladder.

### R2 — PCID stale-TLB: CLOSED as a corruption lane (2026-07-04)

The "PCID corruption" WAS the R5 free-before-shootdown bug: signatures
re-verified same-family, all noflush-specific hazards audited covered, and
the opt-in retest on the fixed kernel ran 3/3 clean KDE batteries where the
07-02 trial corrupted within 2 (see N6 in the Work Order for the full
record + archives). PCID/noflush stays default-OFF on perf grounds: M3
amplification halves but desktop M4/M5 regress ~30% because page-level
shootdowns degrade to global flushes under PCID. Reopen only as a PERF
lane behind INVPCID-based per-PCID flush support.

### R3 — rcu_head_cache double-free: bounded-open, watch

5/5 clean trace-enabled provoke runs; `rcu_head_trace=1` diagnostic
staged default-off. May share the R5 root cause — correlate any recurrence
(one pre-R5-fix `slab_alloc: repairing corrupt freelist` line seen
07-04) with both lanes.

### R4 — pactl fault: CLOSED (payload-side, classified)

libpulse stack-probe/logging-recursion crash family; pactl removed from
the readiness path (default-off probe). Reopen only with a deterministic
reducer or kernel-corruption coupling.

### R5 — KWin startup corruption: ROOT-CAUSED + FIXED (2026-07-04)

One corruption family (24 sites, 11 libs, 7-10%/launch, attempt-1
cold-cache only): NULL-expected globals in the zero-fill tail of each
library's last RW file-backed page poisoned with recycled-frame residue.
Root cause: free-before-TLB-shootdown in `__vma_clear_range` mid-batch
overflow and `__vm_madvise_dontneed` (frames returned to the allocator
while stale translations existed; VA reuse let sibling threads scribble on
reallocated frames). Fix (kernel `67d7b5c`): shoot down the cleared span
before every early `page_free_anon_batch`; madvise per-segment flush
replaces the final flush; pgtable spinlock dropped around IPI-ack waits.
Battery green; M4 1883. Full forensics chain in the history file + git.
Statistical closure: 9/30+ clean attempt-1 launches accrued — count every
future battery's KWin launches.
LATENT hugepage bugs recorded, NOT fixed (all dormant behind
`vma_file_hugepage_collapse_enabled()==0`, and BLOCK re-enabling it):
partial-range 2MB over-free in `__vma_clear_range`; mprotect whole-2MB
protection bleed; madvise missing hugepage check; `page_free_anon_batch`
order-0-only; whole-folio COW head-page ref leak.
Secondary: KWin LibinputBackend nullptr deref (payload, -> N3);
crash-dump tooling gap (/core.PID never extracted, no symbolizer; fatal
dump could print the faulting VA's PTE/frame state).
Watch item: one unreproduced nographic boot stall (1/8, post-service
spawn) noted 07-04.

### R7 — Desktop responsiveness composite: a/b DONE, c partial, M8 = N5

R7a O(1) rq-assertion landed (11-15% of cycles removed; control knob
`rq_identify_linear_scan=1`). R7b inotify FIONREAD ABI fix landed and
closed (M8 345-512% -> ~85-105%; `linuxsyscallabitest inotify-fionread`
reducer). R7c timer_tick fastpath first slice landed; remaining
`__sched_timer` contention is bursty sub-jiffy expiry/insertion, not a
stuck owner. M8 debt is now attributed host-side to vCPU/KVM idle wake
cadence with guest PCs in `arch_idle_halt` (= N5, relates to N7 tick
work).

### R9 — Cursor out-of-range: OPEN (= N3)

User-visible: pointer does not track the host mouse. Triage order: (1)
EVIOCGABS absinfo vs the virtio tablet's 0..32767 (source audit says the
raw path normalizes to 0..65535 — verify at runtime); (2) raw ABS values
at screen edges vs host pointer; (3) cursor-plane transform under
`virtio_gpu_host_cursor_only=1`. Probe machinery is now harness-fixed and
source-only: `scripts/gpu/r9-cursor-contract-probe.expect`
(startup-injected `/r9-run.sh`, debugfs polling, QMP absolute injection after
`phase=armed`) — do NOT drive the probe over the interactive serial shell.
Dry-run `20260704T200628Z` passed with `qemu-dry-run.txt` after the R9
runner inherited the KDE ABI library path. Real probe `20260704T200703Z`
reached `armed_pid=57`, sent all five HMP monitor moves, and passed discovery:
udev enumerated event0/event1 (`count=2`) and libinput assigned `seat0`
(`r9_libinput_status rc=0 errno=0 reason=ready fd=6`), but HMP produced only
relative PS/2 samples: libinput reported 18 events with
`absolute_samples=0`, evdev collected no raw ABS samples, and `/dev/mouse`
clamped/repeated `flags=0 x=127 y=127`.

Coordinate slice result 2026-07-04: HMP routing was the failure layer.
Strict rerun `20260704T202206Z` selected `Mouse #3: QEMU Virtio Tablet
(absolute)` with HMP `mouse_set 3`, then still produced only PS/2 relative
samples and failed strictly with `reason=no_evdev_abs_samples`. The fixed
harness now opens `qemu-qmp.sock` and sends QMP `input-send-event` absolute
axis events in the virtio tablet's raw 0..32767 range, while retaining HMP
`info mice`/`mouse_set` logs as routing evidence and killing the spawned QEMU
process group on finish. Dry-run `20260704T202724Z` PASS; real R9
`20260704T202747Z` PASS: `/dev/mouse flags=1`; event1 EV_ABS samples
0/0, 32768/32768, 65535/65535, 16384/49150, 49150/16384; libinput absolute
samples 0/0, 640/400, 1279.980/799.988, 320/599.976, 959.961/200; summary
`evdev_abs_samples=10 mouse_samples=5 libinput_abs_samples=5 result=PASS
reason=coordinate_samples`.

Visible cursor-plane slice result 2026-07-04: new harness
`scripts/gpu/r9-cursor-visible-probe.expect` PASS at
`build-x86_64/r9-cursor-visible-probe-history/20260704T205234Z-r9-cursor-visible-probe`.
The run uses guest cursor mode (`qemu-dry-run.txt` has `show-cursor=off`)
with no `virtio_gpu_host_cursor_only=1`, preserves
`qemu-qmp-command.log` for `input-send-event` absolute X/Y injection and
`qemu-monitor-command.log` for `info mice` evidence only, and keeps the
strict coordinate contract alive (`/dev/mouse flags=1`,
`evdev_abs_samples=10`, `libinput_abs_samples=5`). Cursor traces prove
`virtio_gpu: cursor upload visible` and injected cursor transforms
0/0 -> 0/0, 32768/32768 -> 640/400, 65535/65535 -> 1279/799 with
`cursor_transform_out_of_range=0`. Crash-marker scan stayed clean for
panic/KWin/LibinputBackend nullptr signatures. `r9-visible-evidence.txt`
records `capture_visibility=NOT_PROVEN`: QEMU framebuffer captures do not
prove the GTK hardware cursor overlay, and this headless harness has no
deterministic host-window capture path. Guard runs after the harness-only
change passed and were archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T210241Z-r9-visible-desktop-interaction-pass`
(desktop-interaction-latency active sample, direct launch PASS) and
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T205909Z-r9-visible-chromium-launch-only-pass`
(chromium-video launch-only PASS). The visible cursor gate is closed; keep
the KWin LibinputBackend nullptr payload here as later/secondary unless this
probe reproduces a startup/input crash. Do not reopen image injection, seat
plumbing, or kernel signed-16 storage without new contradictory evidence.

Follow-up 2026-07-06: user visual inspection still saw the cursor leave the
VM window, so the prior guest-input/visible probes were not sufficient for
normal desktop-interaction acceptance. The smoke harness now forces bounded
guest `/bin/mouseinject` against the settled framebuffer/workarea, disables
normal monitor/host cursor sync, and keeps monitor diagnostics opt-in only.
Focused KVM+virgl real-GL/no-software-fallback validation PASS:
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-guest-input-20260706.tar.gz`
with `input_source=guest`, `cursor_owner=guest-forced-normal-interaction`,
`monitor_path=disabled`, `host_cursor_sync_enabled=0`, `pointer-bounds PASS`,
bounded guest pixel coordinates, `/bin/mouseinject` command proof, and
`host_cursor_sync_ms=0`. No commit/push yet.

## Verification Gates

- Static: `git diff --check` in every repo level.
- Build: `cmake --build build-x86_64 --target kernel -j$(nproc)`; after
  user/rootfs changes also `--target user` then `--target rootfs-refresh`;
  ports: `cmake --build build-x86_64/ports --target port-<name> -j2`.
- Runtime battery (see Work Style rule 2). KDE stability claims need
  `timeout 900+ scripts/gpu/kde-plasma-desktop-smoke.expect` with
  `KDE_SMOKE_REDUCER=desktop-interaction-latency` and active sample.
- Reducers available: chromium-video (+`KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1`,
  +`KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE=1`), kde-ready, pre-kwin-libinput,
  kde-wayland-registry, kde-wayland-liveness/client-liveness,
  desktop-interaction-latency; nographic: forktest/clonetest/cowtest,
  syscalltlb, wakestorm, linuxsyscallabitest, mesacopytexture,
  mesaanglepassthrough (host-staged).
- Archive every proof run to
  `build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/`
  (exclude `*.img`, delete the scratch image after copying).

## Evidence Archives

- `build-x86_64/kde-plasma-desktop-smoke-history/` — all KDE/Chromium runs
  (key 07-04 entries: `*-q0-kprofile-wait-fix-first-valid-m7-window`,
  `*-p3-ext4-read-page-direct-*`, `*-r5-tlb-fix-*`,
  `*-q7-ordered-pageflip-*`, `*-q2-mesa-angle-ladder-*`,
  `*-q2-real-gl-full-video-m7-44fps`, `*-n2-ext4-direct-*`).
- `build-x86_64/yt-mainpage-freeze-repro/` — P0/M1 replays.
- `build-x86_64/syscall-tlb-proof/`, `build-x86_64/pageflip-ordered-ab-proof/`,
  `build-x86_64/desktop-bottleneck-profile/`,
  `build-x86_64/r5-kwin-startup-classification/`.
- Cleanup note 2026-07-04: deleted stale ignored `build_x86_64/` (~68G) and
  18 archived scratch `*.fs.img` files from old proof-history directories
  (~149M). Preserved `build-x86_64/fs.img`,
  `build-x86_64/kde-plasma-desktop-smoke/kde-plasma.fs.img`, current Q2
  real-GL evidence, N2 diagnostics, N3 R9 cursor/visible evidence, and the
  robust P1 nographic archive. Top/kernel/user/ports/mesa were clean after
  cleanup.
- Cleanup note 2026-07-07: removed 56 files and 25 dirs, reclaiming
  493,419,025,760 bytes (~459.6 GiB). Preserved `build-x86_64/fs.img`,
  `build-x86_64/kde-plasma-desktop-smoke/kde-plasma.fs.img`,
  `rootfs-generated-overlays`, `host-gui-runtime`, `kde-noble-plasma`, current
  20260707 proof dirs/logs/screenshots, and named evidence dirs. Old history
  scratch images and extra mouseinject `.fs.img` files are gone; no tracked
  source `D` entries.
- `docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md` —
  full pre-compaction plans (2026-07-02 and 2026-07-04 snapshots) with
  every evidence chain.

## Commit Policy

Commit PERIODICALLY at every verified checkpoint (completed slice, passing
gate, before a risky change, before ending). Lane-scoped messages with a
lane reference; never bundle unrelated changes. Submodules deepest-first
(`ports/mesa/src` -> `ports`; `kernel`; `user`), then the top-level pointer
update in the same checkpoint. `git status` at top level AND every
submodule at handover. NEVER push without explicit user approval.

---

# ARCHIVED SNAPSHOT: active-work-plan.md as of 2026-07-10 (pre-rewrite)

# Active xv6 Work Plan

Last updated: 2026-07-08 (recut around unfinished work; the full pre-rewrite
plan is archived verbatim in
`docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`
under "ARCHIVED SNAPSHOT ... 2026-07-08").

Single-plan rule: this is the only live plan file. Verbose lane histories,
evidence chains, and superseded verdicts live append-only in the history
file. Every proof run is archived under
`build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/` (or
lane-specific proof dirs). Branch: `codex/host-linux-abi-shell-port-ff`.

## Mission

Make the KDE/Chromium desktop on x86_64 KVM+virgl stable and responsive,
approaching a comparable Linux VM on the same host class. GPU acceleration
is mandatory: real KVM+virgl GL only; software/llvmpipe/bundled fallback
invalidates a run. kprofile is the recurring metric after every
responsiveness iteration.

## THE BOTTLENECK (settled 2026-07-07/08 — do not relitigate)

Konsole shell-readiness is ~1.2-1.5s vs ~0.3s native. Fully decomposed:

- Dynamic loader = ~3% serial (~22ms; LD_DEBUG-proven). ALL loader/config
  levers CLOSED: XDG prune, hwcaps, LD path, single-lib stubs (QtMM,
  NewStuff, libimobiledevice), ld.so.cache, negative-dcache honor — each
  mechanically proven, none moved konsole_wait_ms.
- Kernel syscall/VFS cost (openat ~130us, ~40k faults/launch) is mostly
  PARALLEL, off the serial critical path — cutting it (neg-dcache -95%
  ENOENT) did not move konsole_wait_ms.
- ~60% of the serial pre-PTY window is BLOCKING WAIT on frame-gated Wayland
  round-trips (xdg-configure/frame-callback) + QDBus; ~15% Qt plugin dlopen
  (mostly imageformats a terminal never needs); ~23% Qt/KF5 init compute.
- The Wayland waits are NOT a missing kwin clock (vblank-paced flip events
  engage perfectly, gaps unchanged): kwin 5.27 RenderLoop is
  repaint-on-damage; the gaps are DAMAGE-ARRIVAL gaps. LOCALIZED 2026-07-08
  (U2, paired konsole+kwin trace): each 58-139ms frame-callback wait is ~96%
  kwin SCHEDULE latency (damage/commit seen -> repaint start), ~4% kwin repaint
  ioctl, and ~0% konsole render (client commits then re-blocks instantly,
  write_to_poll_us~0). So the remaining serial lever is kwin's damage->repaint
  scheduling latency (NOT the client commit chain, which is exonerated), plus
  round-trip COUNT as a secondary axis.

## UNFINISHED WORK — priority queue

- U-AUDIO (guest audio backend — STACK MAPPED + PRAGMATIC NULL-SINK FIX
  IMPLEMENTED-AND-REFUTED 2026-07-10; honest requirement spec recorded, gate
  default OFF, NOT committed, fs.img left pristine). USER-VISIBLE SYMPTOM: the
  YouTube stall (M1/M7) is a chromium audio-renderer init failure
  (media/audio/pulse/pulse_util.cc "pa_operation is nullptr" flood +
  PipelineStatus::AUDIO_RENDERER_ERROR); currently worked around by the
  committed `--disable-audio-output` gate (M7), which bypasses PulseAudio
  entirely. STACK MAP (end-to-end, NOT the gap where earlier notes feared):
  (1) KERNEL DRIVER EXISTS — `kernel/kernel/virtio_snd.c` (1173 lines, real
  VIRTIO_SND PCM: SET_PARAMS/PREPARE/START/STOP/tx-completion) binds QEMU's
  `virtio-sound-pci` and `kernel/kernel/dev/ossaudio.c` (3561 lines) publishes
  BOTH an OSS frontend (/dev/dsp, major 14) AND an ALSA char ABI (/dev/snd/
  pcmC0D0p + controlC0, major 116, SNDRV_PCM_IOCTL_HW_REFINE/HW_PARAMS/
  SW_PARAMS/PREPARE/START/SYNC_PTR all decoded). Boot banner confirms:
  "audio: registered OSS /dev/dsp and ALSA /dev/snd/pcmC0D0p". So the feared
  "no kernel sound driver / write virtio-snd = big lane" is ALREADY DONE — the
  kernel is NOT the gap. (2) USERSPACE ships in full: pipewire, wireplumber,
  pipewire-pulse, real pulseaudio daemon, ALSA tools, spa-0.2/{alsa,support}
  plugins (libspa-alsa.so + libspa-support.so=null-audio-sink). `scripts/image/
  kde-plasma-session-child.c` run_audio_services() starts pipewire ->
  wireplumber -> pipewire-pulse and gates on the pipewire-0 + pulse/native
  sockets (runtime: "phase=socket status=PASS pipewire_core=1 pulse_socket=1").
  `scripts/image/stage-kde-runtime.sh:678` bakes an ALSA sink
  "alsa_output.xv6_virtio" (factory api.alsa.pcm.sink, api.alsa.path="hw:0",
  flags=[nofail]) into pipewire.conf; `rootfs-overlay/usr/share/alsa/alsa.conf`
  routes ALSA default PCM -> file plugin -> /dev/dsp. (3) QEMU: `scripts/launch/
  run-qemu.sh` builds `-audiodev <backend> -device virtio-sound-pci,...` when
  QEMU_AUDIO=virtio (default) and QEMU_AUDIO_BACKEND!=none; all non-audio gates
  (incl. the M7 youtube harness) run QEMU_AUDIO=none => NO virtio-sound device
  present, so the kernel /dev/snd has no host sink behind it.
  THE PRECISE GAP (measured, 2 boots): the pipewire ALSA sink registers only
  FLAKILY and, when it does, is SUSPENDED and errors on stream start (no host
  backend under QEMU_AUDIO=none). Baseline runtime evidence:
  plasmashell logs "org.kde.plasma.pulseaudio: No object for name
  alsa_output.xv6_virtio.monitor" (zero sinks). BUT the real gap is DEEPER than
  sink availability: a server-side support.null-audio-sink, even made the
  pactl-confirmed **Default Sink**, does NOT fix chromium — its PulseAudio
  OUTPUT STREAM still fails ("services/audio/output_device_mixer_impl.cc
  MixableOutputStream: Error during independent playback" -> pa_operation
  nullptr -> AUDIO_RENDERER_ERROR, presented 0.02fps, video never decodes).
  So the failure is in the pipewire-pulse <-> chromium-libpulse PLAYBACK-STREAM
  path itself, independent of which sink is default. (A libpulse `paplay` probe
  to localize server-side-vs-chromium-specific was attempted but the diagnostic
  boots got too slow to land the serial command within budget; paplay to the
  null sink appeared to HANG in an early attempt, weakly corroborating a
  server-side stream stall — NOT conclusive.) IMPLEMENTED (gated, default OFF,
  byte-identical when unset, compile-clean cc -O2 -Wall -Wextra, NOT committed,
  source left in kde-plasma-session-child.c; fs.img restored to pristine): gate
  `kde_audio_null_sink=1` / env KDE_AUDIO_NULL_SINK — before pipewire starts,
  writes /dev/shm/kde-config/pipewire/pipewire.conf.d/50-xv6-null-sink.conf
  declaring a support.null-audio-sink node "xv6_null_output" (media.class
  Audio/Sink), and after pulse is up runs `pactl set-default-sink
  xv6_null_output`; logs to kde-audio-status.log (phase=null-sink WROTE/DEFAULT).
  VERDICT: the pragmatic userspace null-sink hypothesis is REFUTED for chromium
  — it does create a valid default sink (fixes the plasma "No object" widget
  warning + gives non-chromium apps a sink) but does NOT make chromium play
  audio. The M7 `--disable-audio-output` gate remains the only working
  YouTube-playback lever. HONEST REQUIREMENT SPEC to get real chromium audio
  (next lane, in rough increasing cost): (a) localize the pulse stream failure
  with a `paplay`/`pw-play` probe on a fast/quiet host (my probe.sh + robust
  expect are in build-x86_64/chromium-youtube-m7/20260710T005044Z-audio-pulse-
  stream-probe/ ready to re-run) — if paplay also fails/hangs it is a
  pipewire-pulse server bug (try the shipped REAL pulseaudio daemon +
  module-null-sink instead of pipewire-pulse for the chromium socket); if
  paplay works it is chromium format/latency negotiation (tune the null sink
  audio.format/rate/period or chromium's requested params). (b) For audible
  host output (not just silent): run with QEMU_AUDIO=virtio (real
  virtio-sound-pci) so /dev/snd has a backend, then debug why spa-alsa hw:0 open
  is flaky (kernel ALSA HW_REFINE/HW_PARAMS constraints spa-alsa can accept +
  the WSLg-pulse host caveat, op-caution 21). Archives:
  build-x86_64/chromium-youtube-m7/20260710T002803Z-yt-nullsink-audio-on/ (null
  sink alone, chromium 0.02fps), 20260710T004004Z-yt-nullsink-default-audio-on/
  (null sink as Default Sink, chromium STILL 0.02fps — decisive), and the probe
  scaffold dir above. See M1/M7.
- U-TOOLTIP (taskbar-icon tooltip slow + unstable — DECOMPOSED + BOTH LEVERS
  IMPLEMENTED AND BOOT-VALIDATED 2026-07-09, gates default OFF; residual =
  default-on promotion): USER PAIN ("hovering a taskbar icon -> tooltip above
  it takes noticeably long, latency unstable run-to-run") reproduced with a
  new hoverprobe TOOLTIP protocol (see Instruments; tip ROI 135,710,140,50
  above the Kate taskbar icon, hover-no-click; full-frame PPM proves a genuine
  "Kate / Advanced Text Editor" tooltip). ROOT (config FIRST, binary-verified):
  plasma-framework 5.115 ToolTipArea reads plasmarc [PlasmaToolTips] Delay,
  compiled default 700 ms (libcorebindingsplugin.so disasm readEntry("Delay",
  0x2bc); enabled=(Delay>0) so Delay<=0 DISABLES tooltips — never use 0); the
  image ships NO plasmarc, so 700 ms show-delay = 74% of the 945 ms baseline
  median. DECOMPOSITION (n=12/event/boot, 7 boots, all gates green, 0 FM19):
  baseline tooltip_open med 948.5/942.9 p90 973/1006 max 1093/1124 sd 50/59 =
  700 config delay + ~250 ms render (tooltip QML update + kwin damage->repaint
  schedule, U2 wall ~3-4x67ms cadence); tooltip_close ~260 ms all arms; A->B
  switch (dialog visible) 233 -> ~110 ms when Delay=50. VARIANCE ATTRIBUTED:
  repeats tight everywhere (sd 44-80); the instability is the FIRST tooltip of
  the session = ToolTipDialog QML cold instantiation, HIDDEN at Delay=700
  (~+150ms) but EXPOSED + jittery at Delay=50 (first 994/2331 ms vs ~470
  repeats) — plus occasional close-side outliers (kwin schedule jitter, known
  wall, quantified not chased). FIX (two gated levers, default OFF,
  byte-identical without tokens; sources edited but NOT committed):
  (1) kde-session.c cmdline `kde_plasma_tooltip_delay=<1..10000>` writes
  /dev/shm/kde-config/plasmarc Delay override (engagement line "plasma tooltip
  delay override Delay=50 ms" in run.log); (2) kde-plasma-session-child.c
  cmdline `kde_tooltip_prewarm=1` (env KDE_TOOLTIP_PREWARM=1) extends the
  kickoff-prewarm double-forked worker (now started if EITHER gate is on;
  kickoff-only portion skipped when its own gate is off) to hover the taskbar
  icon once after the taskbar-ready gate (knobs KDE_TOOLTIP_PREWARM_ICON_ABS_X/
  _Y/_DWELL_MS default 11000,64200/2500) then move away — ~3.4 s worker time at
  login, tooltip auto-hides, pristine; (3) harness A/B hook
  KDE_SMOKE_PLASMA_TOOLTIP_DELAY_MS stages /etc/xdg/plasmarc into the PER-RUN
  fs.img copy (KConfig system cascade). A/B VERDICT (Delay=50 + prewarm, two
  reps): med 945 -> 486/482 (-49%), p90 ~990 -> 541/530 (-46%), worst-case
  2331 -> 607/747, first-of-session ~1.1s -> 486/747 ms; prewarm-only at
  Delay=700 = NULL (first 1105 — the 700ms delay already masks the cold cost;
  prewarm pays off only once the delay is cut); Delay=50 alone leaves the
  first-tooltip jitter (994/2331). Residual ~480 ms floor = U2 kwin
  damage->repaint schedule latency (settled, do not relitigate). Staged into
  committed fs.img via debugfs (kde-session, kde-plasma-session-child,
  hoverprobe — all gated/inert by default; backups in the tooltip worker's
  scratchpad); gate-OFF equivalence proven by boot6 baseline-identical stats
  on the new binaries. PROMOTION to default-on (residual): needs the standard
  regression battery + owner sign-off (same posture as kickoff prewarm); the
  natural default candidate is Delay~200-300 + prewarm if 50 ms feels too
  eager for real users. Archives: 20260709T205408Z-tooltip-ab-summary (+ 7 run
  archives listed inside). See M12.
- U-KICKOFF (start-menu open/close — DECOMPOSED + LEVER PROVEN 2026-07-09;
  PRODUCT-SIDE PREWARM IMPLEMENTED + BOOT-VALIDATED 2026-07-09, gate default
  OFF; only residual is the default-on promotion):
  USER PAIN ("opening/closing the KDE start menu takes SECONDS") reproduced and
  split with a new hoverprobe menu-mode (menu-body ROI + open/close protocol;
  see Instruments). ROOT: the pain is the COLD FIRST Kickoff activation of a
  session = 2254/1892ms (n=2, matches the reported 1394-2517ms); every WARM
  repeat-open in the same session is ~400-500ms and close is ~180-420ms. So the
  ~2s is a ONE-TIME QML-compile + app/recents model-population cost, NOT a
  per-open cost and NOT compositor cadence (co-enabled kwin ioctl-trace shows
  the compositor mostly idle during the cold open; PAGE_FLIP dur ~5ms). The
  residual warm ~400ms floor is the already-settled U2 kwin damage->repaint
  schedule latency (do not relitigate). ATTACK CHOSEN = session-start PREWARM
  (open Kickoff once during login, wait for the model build, close) — cheapest
  lever that works whether the cold cost is compile or model-population. A/B
  (hoverprobe _PREWARM gate, do_hover=0 so iter1 = user's first click): first
  open 2254/1892 -> 498/540ms, -75% (~1550ms), all gates green (real virgl/
  D3D12 GL, qtquick PASS, crash 0, no lingering qemu, full-frame PPM confirms
  genuine Kickoff open). See M11 + archive 20260709T190655Z-menu-prewarm-ab-
  summary (+ ...T190352Z-...-treatment-rep1). PRODUCT INTEGRATION DONE
  2026-07-09 (realization (a), input-injection in the product session): the
  prewarm now lives in `scripts/image/kde-plasma-session-child.c` (not the test
  tool). After services start, `kickoff_prewarm_start()` DOUBLE-FORKS a worker
  (setsid, reparented to init — never blocks/steals the session-child's
  waitpid(plasmashell), no zombie); the worker (i) opens /dev/mouse + /dev/fb0,
  (ii) readiness-gates on the bottom-left launcher ROI going non-black + hash-
  stable (borrowed taskbar-settle heuristic, FB_GPU_SCANOUT_READ), (iii) abs16-
  clicks the Kickoff launcher (px23,783) with OPEN-VERIFY-AND-RETRY — polls the
  menu-body ROI (100,330,200,200) for a change and re-clicks (up to 8x) because
  Kickoff is NOT interactive the instant the corner paints (a single early
  click at ~8s is swallowed; the retry is REQUIRED — the first no-retry build
  logged opened=0 and the user's first open stayed cold at 2583ms), (iv) dwells
  4000ms for the QML/model build, (v) dismisses with an empty-desktop click and
  self-verifies pristine (menu-ROI hash back to the closed baseline) + dumps
  /kde-plasma-kickoff-prewarm-after.ppm as proof. Gate default OFF: cmdline
  `kde_kickoff_prewarm=1` or env KDE_KICKOFF_PREWARM=1 (tunables
  KDE_KICKOFF_PREWARM_{DWELL,SETTLE,GATE,OPEN_TIMEOUT}_MS + _ICON_ABS_X/Y).
  BOOT VALIDATION (3 boots, real KVM + virgl/D3D12 GL, qtquick PASS, crash 0,
  no lingering qemu): gate ON — worker logs gate READY@7.6s, open OPEN
  attempt=1@11.1s, DONE opened=1 pristine=1@16.1s; USER's first Kickoff open
  (hoverprobe menu-mode _DO_HOVER=0 _PREWARM=0, so iter1 = first real click)
  = 493ms vs gate-OFF 2164ms cold same config = -77% (~1670ms), matching the
  test-tool A/B (498/540ms). Gate OFF is byte-behaviour-clean (no
  kickoff-prewarm lines, no flag in cmdline). Pristine screenshot verified
  (wallpaper+icons+panel, no menu). PROMOTION TO DEFAULT-ON (residual, needs
  owner sign-off): the injected click is not a provably ~0 cost (~2s of prewarm
  work at login, though nearly free within the ~6.2-6.6s M5 first-visible
  window), so per the guardrails it stays opt-in until the standard same-
  session A/B + regression battery (KDE active-sample, Chromium launch-only,
  M4/M5/M8 within noise) run green with sign-off. kwin/plasmashell are PREBUILT
  (C++ patches off the table); a plasmashell scripting/DBus toggle (realization
  (b)) was not needed. NOTE (not yet measured, cheaper still if
  it works): persistent QML disk cache (qmlcachegen at image build / prepopulate
  ~/.cache/*qmlcache) would cut only the compile half and needs plasmashell env
  (QML_DISK_CACHE) proven enabled — deferred; prewarm subsumes it and needs no
  image rebuild.
- U1 (KERNEL PART DONE 2026-07-08 — metadata hypothesis retired): the
  drmGetDevice2 device-metadata surface is ALREADY implemented+committed in
  the kernel (`kernel/vfs/sysfs/*`: `/sys/dev/char/226:0|226:128/device/{vendor
  0x1af4,device 0x1050,subsystem_vendor,subsystem_device,revision,class
  0x030000,uevent,subsystem->/sys/bus/pci,drm/}`). Re-verified on the current
  image via `drmgpuprobe`: `drmGetDevices2=1`, `bus=0`(DRM_BUS_PCI),
  `nodes=0x5`(primary+render), render node type=2, vendor 0x1af4/device 0x1050;
  also drm_info shows `Device: PCI 1af4:1050` for both nodes
  (proof-upstream-drm-tools.log 2026-06-12). The task's metadata-less-fallback
  evidence (node=-1, "failed to retrieve device information") is stale
  (virgl-kms-seconds-validate.log 2026-06-06, PREDATES the sysfs build-out);
  current mesa source no longer contains that fallback string. Proof archived:
  `kde-plasma-desktop-smoke-history/20260708T150347Z-u1-drmgetdevice2-validation/`.
  IMPLICATION: the residual image-layout EGL race is NOT a metadata problem
  (sysfs is kernel-generated, deterministic, image-independent — always
  present). REMAINING U1 = the secondary lever only: EGL-init retry / readiness
  gate in the session launcher (userspace, not kernel). (Out-of-scope note:
  drmgpuprobe's standalone gbm_bo_create fails EINVAL on USE_WRITE|LINEAR; the
  mesa EGL/platform_drm gbm path is unaffected and holds real GL.)
  RESIDUAL IMPLEMENTED 2026-07-08 (OFFLINE, compile-clean, boot-validation
  pending): userspace EGL/DRI2 readiness gate before plasmashell exec.
  ROOT CAUSE from the pass/fail pair (kwin logs are decisive): FAIL run
  (`...20260707T192438Z-negdcache-control-rep1`) kwin's FIRST output init
  fails — `kwin_scene_opengl: "Could not create gbm device"` +
  `"Could not initialize egl"` + `kwin_wayland_drm: Failed to find a working
  setup for new outputs!` — then kwin RETRIES (2nd `No backend specified,
  automatically choosing drm`) and succeeds with virgl D3D12 GL; plasmashell,
  racing that same window, hits `failed to create dri2 screen` x4 +
  `Failed to initialize EGL display 3001` and drops to QtQuick software (Qt has
  no retry), only recovering when the whole session-child relaunches (2nd
  `graphics EGL_PLATFORM=` block at line 60 -> real GL). PASS run
  (`...20260707T171835Z-ldsocache-control-rep1`) kwin's first output init
  succeeds directly, no gbm/egl failure, plasmashell gets GL first try. So the
  race is on the shared mesa DRI2/virgl SCREEN-CREATION substrate on the render
  node (both GBM and Qt-Wayland clients fail it together in the FAIL run); it
  warms after the first successful creation (why kwin's retry and the 2nd
  plasmashell both succeed). PREDICATE CHOSEN: "can a fresh process create a
  DRI2/virgl EGL screen on renderD128 right now" — the exact failing op —
  realized by new helper `scripts/image/kde-egl-readiness-probe.c` (open render
  node -> gbm_create_device -> eglGetPlatformDisplay(GBM) -> eglInitialize ->
  eglChooseConfig -> eglCreateContext + surfaceless eglMakeCurrent +
  glGetString; deliberately NO gbm_bo_create per the out-of-scope note). Staged
  into the guest via new `stage_kde_egl_readiness_probe` in make-rootfs.sh
  (pkg-config egl/glesv2/gbm/gl against the guest sysroot, mirroring
  stage_kde_drm_probe; warn+skip if dev files absent). GATE: `kde-plasma-
  session-child` runs the probe in a retry loop (10x, 200ms backoff, 8s/attempt
  timeout) right before exec'ing plasmashell (scoped to plasmashell only, not
  kwin/session), emitting `egl-readiness-gate status=READY|RETRY|TIMEOUT` lines;
  it PROCEEDS ANYWAY after the budget (never wedges — plasmashell keeps its
  software fallback + KCrash restart net). DEFAULT OFF (opt-in): cmdline
  `kde_plasmashell_egl_ready_gate=1` or env `KDE_PLASMASHELL_EGL_READY_GATE=1`.
  Justification for default-off: the probe forks a fresh process that dlopens
  mesa, so the no-race path cost is NOT provably ~0 (violates the "default-ON
  only if ~0" rule + the no-un-gated-flip guardrail); promotion to default-on
  needs the standard same-session A/B with owner sign-off (same pattern as U3).
  COMPILE PROOF: `kde-egl-readiness-probe.c` built with the production pkg-
  config command (`cc -O2 -Wall -Wextra ... $(pkg-config --cflags --libs egl
  glesv2 gbm gl) -Wl,--allow-shlib-undefined`) against build-x86_64/sysroot,
  clean; `kde-plasma-session-child.c` rebuilt with its production command
  (`cc -O2 -Wall -Wextra`), clean; `make-rootfs.sh` `bash -n` clean. VALIDATION
  (next image rebuild, do not force): watch op-caution-2 signature. Acceptance =
  no-regression now (gate default-off => byte-behavior unchanged unless
  enabled) and, when the signature recurs, enable the gate and confirm the
  `egl-readiness-gate status=RETRY...->READY` line appears and plasmashell then
  gets real GL (no software fallback). NOTE: the race is image-layout-dependent
  and cannot be forced on demand, so a green boot does not by itself exercise
  the retry — the gate is a standing guard whose engagement is only observable
  if/when the layout reproduces the race.
- U2 (M4 serial lever) — MEASURED 2026-07-08 (one boot, both traces
  co-enabled; LARGER HALF = kwin-schedule latency, ~96%): co-enabled the
  konsole wayland event trace (KDE_APP_LAUNCH_PROBE_KONSOLE_WAYLAND_EVENT_TRACE
  =1 + LOADER_TRACE=1) AND the kwin ioctl-trace shim (kde_kwin_ioctl_trace=1)
  in ONE direct-launch boot (20260708T162206Z-guiperf-phase3-u2-split; DONE,
  crash 0). SPLIT of the 58-139ms frame-callback-wait class (konsole poll(fd=3)
  blocks; n=6 in-band, median 77ms, samples 66/67/75/79/79/107ms):
    - kwin SCHEDULE latency (commit/damage seen -> repaint start) = 73.8ms = 95.8%
    - kwin REPAINT cost (PAGE_FLIP ioctl duration, median)        =  3.2ms =  4.2%
    - konsole RENDER latency (frame-callback recv -> next commit) = ~0.0ms =  0.0%
  Method: konsole poll(fd=3) elapsed_us = time the client is BLOCKED waiting
  for the compositor's frame-done (kwin side); write_to_poll_us (commit->
  re-poll gap) = 0 median, 1/31 nonzero (max 159us) => the konsole client
  commits and IMMEDIATELY re-blocks, so client render is NOT a serial lever.
  kwin repaint ioctl (PAGE_FLIP) is cheap (~3ms). The wait is dominated by
  kwin's schedule latency; the PAGE_FLIP cadence gap median (~67ms) matches the
  ~77ms wait, i.e. kwin services damage on a ~67ms cadence rather than a
  vsync-tight one. FINDING (updates THE BOTTLENECK): the earlier "client commit
  chain (konsole render after frame-callback) is the remaining serial lever"
  hypothesis is CONTRADICTED — konsole render ~0; the lever is kwin
  DAMAGE-ARRIVAL -> REPAINT-START scheduling latency. NEXT M4 ACTION (attack the
  larger half): attack kwin RenderLoop schedule latency — the damage->repaint-
  start delay. Most likely mechanism: kwin gates the next repaint on the prior
  frame's present-completion (flip-done) event, which is paced slowly on xv6
  (ties to M7 presentedFPS=51 and the U5 atomic flip-complete event path). The
  round-trip-COUNT axis (Qt-Wayland init) remains secondary but is a count, not
  a per-wait cost — the per-wait cost is now firmly kwin-schedule.
  ROOT-CAUSED 2026-07-08 (OFFLINE re-analysis of the U2-split archive; design in
  scratchpad kwin-schedule-latency-design.md): the 67ms cadence / 73.8ms schedule
  latency is COMPOSITING-COST bound, NOT clock/timer-anchor bound. The DRM
  PAGE_FLIP ioctl does the ENTIRE present synchronously inside the ioctl
  (gpu_kms_present_fb -> fb_blit_from_bo_format, fence returned but IGNORED;
  fb_drm_kms_properties.c:734,804,814) and BLOCKS 10-26ms per flip in the
  konsole-launch bursts (trace: fb111 dur=20887us, fb120 22405us, fb132 25980us —
  the "3.2ms median" is diluted by idle single-flips). That inflates kwin
  RenderLoop expectedCompositingTime above the 16.67ms refresh, so scheduleRepaint
  cannot land n=1 vblank and re-rounds to n~=3-4 (50-67ms); frame-callback wait =
  one in-flight present (~20ms) + one next cadence (~47ms) ~= 67ms. (a)/(b)/(c)
  verdict: mechanism is (a) timer-schedules-out but ROOT is cost, not stale
  timestamp — the legacy flip-complete carries a grid-snapped submit-time ts <= now
  (fb_drm_core_kms.c:1274-1331) that schedules only 1 vblank out, so phase is not
  the driver. RECONCILES the vblank-paced null A/B: that gate deferred the
  completion EVENT + fixed its phase but kept the present SYNCHRONOUS, so it left
  expectedCompositingTime (hence gaps) unchanged -> ELIMINATES the entire
  timestamp/clock-anchor hypothesis family. (c) event-read starvation is NOT
  confirmable: the trace has ZERO EVTREAD/FLIP_COMPLETE lines despite flags=0x1 on
  every flip because kwin INHERITED the DRM fd (fd=21) across fork/exec so the
  child-side shim never saw the /dev/dri open (path_is_dri/mark_dri_fd) -> read
  tracking blind. BLIND SPOT CLOSED 2026-07-08 (OFFLINE, shim-only, build+host-
  smoke proven, NO boot): the ioctl-trace shim now seeds DRM fds two ways so
  read()/EVTREAD tracking no longer depends on having seen the open —
  (1) classify-on-first-ioctl (primary, robust): the ioctl wrapper marks any fd
  receiving a DRM-type ('d'=0x64 _IOC type) ioctl as a DRM fd permanently, so an
  inherited fd is classified from its first DRM ioctl (broader than the four
  decoded ops; zero scan, zero startup cost, works without procfs); (2) a
  constructor /proc/self/fd readlink scan (fds 0..255) that seeds fds already
  open at preload time — verified viable because this guest's procfs resolves fd
  symlinks for char devices to "/dev/DEVNAME" and the DRM nodes register as
  "dri/card0"/"dri/renderD128" so readlink yields "/dev/dri/..." and path_is_dri
  matches (kernel/kernel/vfs/procfs/inode.c:875-928,
  kernel/kernel/dev/fb/fb_init_panic.c:31,51). A phase=seed banner
  (proc_seeded_fds + classify_on_first_ioctl=active), per-fd DRMFD_SEEDED lines
  (via=proc|ioctl), and proc_seeded_fds/ioctl_seeded_fds in the summary let the
  next A/B confirm closure from the log alone. Host smoke: a pipe fd unseen by
  the open wrappers produced NO EVTREAD before its first DRM ioctl, was seeded by
  it (DRMFD_SEEDED via=ioctl), then a subsequent read produced EVTREAD
  FLIP_COMPLETE; a non-DRM (FIONREAD) ioctl left its fd untracked. kwin is
  PREBUILT (kde-runtime overlay; no ports/ kwin, make-rootfs builds only the
  screenshot probe) => kwin C++ patches OFF the table; levers are kernel + env
  only. RECOMMENDED FIRST SLICE (kernel, gated virtio_gpu_async_present=1 default
  OFF): submit the present to the virgl ring and RETURN from PAGE_FLIP in ~us, queue
  DRM_EVENT_FLIP_COMPLETE on fence-retire instead of inline -> drops per-cycle
  compositing time 10-26ms -> <2ms so the RenderLoop paces at 1 vblank. VALIDATION
  targets: PAGE_FLIP loaded duration_us 10-26ms -> <2ms; cadence gap ~47-67ms ->
  ~16-20ms; konsole frame-callback wait 66-107ms -> 16-20ms; schedule median
  73.8ms -> ~16-20ms; correctness risk = torn/half present (double-buffer + hold BO
  ref to fence), so screenshot-diff guardrail is load-bearing. Composes with (does
  not replace) vblank-paced pacing: pacing fixed event TIMING, this fixes event
  COST.
  SLICE IMPLEMENTED 2026-07-08 (OFFLINE, compile-proof clean, NO boot/build —
  VM+build lanes owned by the crash root-cause worker): gate
  `virtio_gpu_async_present=1` (DEFAULT OFF, cached-cmdline pattern like
  async_cursor). Legacy DRM_IOCTL_MODE_PAGE_FLIP with PAGE_FLIP_EVENT now (gate
  ON) pins the FB's BO via fb_bo_get_owned, snapshots all present params, hands
  the blit to a new single-threaded "fb-present" workqueue and returns in ~us;
  the worker runs the identical present (shared helpers
  gpu_kms_present_lookup_params + gpu_kms_present_blit, factored byte-for-byte
  out of gpu_kms_present_fb), drops the BO ref only after the fenced blit
  returns (host done reading — R5), samples the REAL completion-time vblank
  seq/ts, and queues DRM_EVENT_FLIP_COMPLETE + clears the single in-flight slot
  in ONE fb_state.lock section (no spurious -EBUSY on an instant next flip; ring
  insertion order = submission order). Overlapping flip while one is in flight
  => -EBUSY (legacy DRM pending-flip semantics; also the tearing/R5 guard — a
  flip targeting the still-scanned-out BO is never accepted; kwin
  double/triple-buffers per fb111/120/132 trace alternation so this is a safety
  net, not a hot path). EAGAIN ring backpressure guard unchanged and still runs
  BEFORE accept. Teardown: worker addresses the owner by id and re-resolves
  under fb_state.lock (paced-flip pattern) — fd close mid-present is a benign
  drop, BO ref cannot leak (worker always puts). Every failure path delivers or
  falls back sync (queue_work failure = undo claim + sync present;
  gate OFF/no-wq/lookup-fail = sync path byte-identical). COMPOSITION with
  virtio_gpu_vblank_paced_flip: both ON => worker completes the present, then
  paces the completion event to the next synthetic edge AFTER real completion
  (new gpu_drm_pace_completed_flip_by_id; kvmalloc/arm failure => immediate
  delivery, never dropped). Atomic-commit path deliberately left synchronous:
  it shares gpu_kms_present_fb but its out-fence cancel/arm lifecycle is tied to
  present success inside the ioctl (shared cost NOT low) and kwin 5.27 never
  uses AMS in VMs (U5); revisit with kwin 6.x. Files (kernel submodule):
  fb_drm_kms_properties.c (helpers + slot + worker + accept),
  fb_kms_atomic.c (:399 accept hook), fb_drm_core_kms.c (deliver/pace-by-id
  helpers), fb_init_panic.c (wq init), fb_common.c (workqueue.h include),
  inc/dev/fb.h (now 5 END-appended stats: present_async_submits_total,
  present_async_complete_total, present_async_fallback_sync_total,
  present_async_bo_hold_max_us, present_async_errors_total).
  ADVERSARIAL-REVIEW FIX-UP 2026-07-08 (OFFLINE, compile-proof clean, NO
  boot/build): applied the GO review's two should-fixes + nit to the
  uncommitted slice. (SF#2) blit failure in the worker no longer delivers a
  success-looking completion: it captures gpu_kms_present_blit's return and on
  nonzero STILL delivers DRM_EVENT_FLIP_COMPLETE (deliberate liveness — kwin
  must never freeze) but does NOT advance current_kms_fb_id, does NOT bump the
  success/lane counters or present_async_complete_total, and bumps a NEW
  END-appended counter present_async_errors_total (5th async stat). (SF#3) the
  accept-time ring check now RESERVES a completion slot: a single global
  fb_state.lock-guarded reservation record holds a per-owner COUNT of ring
  slots owed to pending async completions; gpu_drm_event_queue_locked rejects
  any NON-FLIP_COMPLETE enqueue (VBLANK/QUEUE_SEQUENCE) once (used+reserved)
  would exceed the 16-deep ring, so a burst can never displace the completion
  kwin blocks on, while a FLIP_COMPLETE uses the full ring to consume its
  reserved slot (the only completion class produced for the flipping owner
  while the gate is on). A single count (not a per-owner field) is used because
  the owner struct lives outside this slice's editable files and the
  single-CRTC/single-compositor invariant means only one owner reserves at a
  time (a second owner degrades to unreserved). Reservation is released on
  EVERY terminal path — queue_work-failure fallback, non-paced worker
  completion, paced timer callback (rides pf->owns_async_reservation), and the
  paced immediate-deliver fallbacks — so it never leaks or double-releases
  (unreserve is idempotent past zero) and needs no owner-teardown drain hook
  (it is not stored in the owner; the worker/timer always run). (NIT)
  present_async_complete_total comment clarified: counts completed presents,
  not delivered events. Files touched: fb_drm_core_kms.c (reservation record +
  3 helpers + queue headroom + timer-cb release + pace-by-id reserved param),
  fb_drm_kms_properties.c (slot.reserved, worker blit-result branch, release,
  accept acquire), inc/dev/fb.h. COMPILE PROOF: dev/fb module.c TU rebuilt
  with the production -Wall -Werror command (scratchpad .o, in-tree untouched),
  clean; git -C kernel diff --check clean. RMFB pre-existing-bug check: NONE — RMFB only
  unpins (TTM moves here are metadata-only, ttm_metadata_only_moves; backing
  pages are freed only at dead&&refs==0 in fb_bo_put), and both sync+async
  presents hold an fb_bo_get_owned ref across the blit. COMPILE PROOF
  (translation-unit trick, production commands from
  build-x86_64/kernel/build/compile_commands.json, -Wall -Werror): dev/fb
  module.c TU and virtio_gpu.c TU both clean, outputs to scratchpad only
  (in-tree .o untouched); git -C kernel diff --check clean. VALIDATION SPEC
  (conductor, single boot A=baseline B=virtio_gpu_async_present=1, both traces
  armed kde_kwin_ioctl_trace=1 + KONSOLE_WAYLAND_EVENT_TRACE=1; close the
  EVTREAD blind spot first — mark any fd receiving DRM_IOCTL_MODE_* as DRI in
  the shim): (1) engagement: fbstat present_async_submits_total ==
  complete_total > 0, fallback_sync_total ~0; (2) PAGE_FLIP loaded duration_us
  10-26ms -> <2ms; (3) PAGE_FLIP gap_us cadence ~47-67ms -> ~16-20ms; (4)
  konsole frame-callback elapsed_us 66-107ms -> 16-20ms; kwin schedule median
  73.8ms -> ~16-20ms; M4 konsole_wait warm expected real drop; (5) GUARDRAILS
  (mandatory): screenshot-diff metric unchanged (torn/half-present = the
  tearing risk this design carries), virgl/D3D12 real GL both arms, qtquick 0
  violations, crash grep 0, forktest rc=1/clonetest rc=0/cowtest rc=0 same boot,
  M5/M8 within noise, EVTREAD submit->read deltas present in B; (6) kill
  criterion: duration_us drops but gap_us/frame-callback do not -> residual is
  GL-composite cost or event-read starvation, pivot to EVTREAD data; (7) after
  the solo gate is proven, A/B the composed mode (async_present=1 +
  vblank_paced_flip=1): expect edge-paced events with the cadence win retained.
  A/B VERDICT 2026-07-08 (gated ASYNC-PRESENT, DECISIVE — MECHANISM WIN + KILL
  CRITERION TRIGGERED, responsiveness NULL): CONTROL x2 (kde_kwin_ioctl_trace=1)
  vs TREATMENT x2 (+virtio_gpu_async_present=1), full desktop-interaction reducer
  (guiperf phase-1 recipe), kwin ioctl-trace shim armed both arms, both tokens
  verified in booted cmdline. Kernel rebuilt from committed d052c5d; fs.img
  refreshed via `ninja rootfs-refresh` (correct overlay set baked by cmake:
  host-gui:webkit-media:kde-runtime:gameboy-roms:gpup-umd); debugfs-verified
  kwin_wayland + kwin-ioctl-trace-preload.so + /bin/dcachetest baked. 4 boots,
  crash 0, no lingering qemu, tree clean. Archives:
  `kde-plasma-desktop-smoke-history/20260708T200049Z-asyncpresent-control-rep1`,
  `...T200250Z-...-control-rep2`, `...T200444Z-...-treatment-rep1`,
  `...T200637Z-...-treatment-rep2`.
  EVTREAD BLIND SPOT CONFIRMED CLOSED: shim banner shows classify-on-first-ioctl
  seeding fds 19/20/21 (DRMFD_SEEDED via=ioctl); 117-119 EVTREAD +
  117-119 FLIP_COMPLETE lines captured in EVERY run (vs ZERO in the U2-split
  archive). proc_seeded_fds=0 (constructor scan seeded nothing here), ioctl
  classify is the mechanism that fired.
  A/B TABLE (control r1/r2 -> treatment r1/r2):
    - engagement PROOF (shim): PAGE_FLIP duration_us MEDIAN 925/1017 -> 73/64;
      loaded-burst MAX 24398/22961us (18-24ms class) -> 650/425us; loaded-burst
      top5 18.3-24.4ms -> 156-650us. Async completion path proven: EVTREAD
      since_submit_us MEDIAN 531/585 -> 3056/2396 (completion now delivered on
      fence-retire ~2.4-3ms AFTER the ~64us flip return, not inline). 1:1
      PAGE_FLIP:FLIP_COMPLETE, every flip ret=0 errno=0 => present_async_errors
      effectively 0 (fbstat counters NOT capturable in this reducer, same as U4;
      the shim duration collapse + 1:1 completion is the engagement proof of
      record).
    - PAGE_FLIP loaded duration 10-26ms -> <2ms: MET/EXCEEDED (treatment
      loaded-burst max 425-650us, well under 2ms).
    - cadence gap_us (active 10-120ms) MEDIAN 32574/34345 -> 37021/33135;
      p90 71126/72414 -> 72683/62816. UNCHANGED (target 16-20ms NOT MET).
    - konsole frame-callback wait / kwin schedule (inferred from cadence, which
      is unchanged): NOT MET.
    - konsole_wait_ms WARM 1191/1192 -> 1186/1202 (dead flat); COLD 1774/1507 ->
      1506/1505 (control r1 1774 is the high outlier; within the settled cold
      band + op-caution-4 noise). NO M4 win.
    - hover first_changed_visual_ms MEDIAN 448/460 -> 442/416 (overlapping, noise).
    - M5 first_visible_ms 10454/6573 -> 6292/6493 (control r1 outlier; treatment
      inside the 6.2-6.6s band; within noise, no regression).
    GUARDRAILS ALL GREEN: real GL virgl(D3D12) both arms, qtquick 0 violations,
    crash 0, no image-layout race (the single `failed to create dri2 screen` is
    the host GTK/EGL warning at run.log:6 pre-guest-boot, present in CONTROL too;
    guest plasma-child dri2 fail = 0 both arms, op-caution-2 NOT triggered, no
    rebuild needed). TEARING GUARDRAIL PASS: direct-launch-diff = 1001730
    (byte-identical) in ALL FOUR runs = the settled baseline; input-diff 0 in
    both treatment reps; treatment vs control direct-launch screenshots
    pixel-identical in layout (only the wall-clock differs) — NO new visual
    corruption class (no torn/garbled frames).
  VERDICT: the synchronous-present cost IS on the PAGE_FLIP ioctl and this slice
  removes it cleanly and safely (24ms->0.65ms, no tearing, no errors) — but that
  cost is NOT what gates the RenderLoop cadence. This is EXACTLY kill-criterion
  (6): duration_us drops ~99% while gap_us / frame-callback / konsole_wait / hover
  are all unchanged within noise (honest n=2). CONTRADICTS the plan's OFFLINE
  root-cause hypothesis that the 67ms cadence is expectedCompositingTime-bound
  (synchronous flip inflating it) — with per-flip present now ~64us ioctl +
  ~3ms fenced completion (both << 16.67ms vblank), the cadence did not tighten,
  so expectedCompositingTime is NOT the cadence driver. PIVOT (EVTREAD data
  captured): the residual damage->repaint schedule latency is bound by something
  OTHER than present ioctl cost — candidates are kwin RenderLoop scheduling /
  damage-arrival timing, client GL render/glFinish on the plasmashell context, or
  vblank-event phase/count. Event-read starvation is NOT the cause (117:117
  completions, all read back; median delivery ~2.4-3ms). PROMOTION: NONE — no
  responsiveness win, so no default-on justification (keep gated default-OFF,
  same posture as vblank-paced and async-cursor). The correctness/tearing result
  is clean, so the gate is a safe standing opt-in; the composed-mode A/B (step 7,
  async_present + vblank_paced) is now moot for responsiveness since the solo
  cadence null removes the premise, and is deferred unless the pivot reopens it.
- U3 (DONE 2026-07-08 — opt-in gate landed, A/B PASS all criteria): Qt
  imageformats prune for konsole. Gate
  `KDE_APP_LAUNCH_PROBE_QT_MINIMAL_IMAGEFORMATS=1` (default 0 = byte-identical)
  adds `stage_qt_minimal_imageformats_if_enabled` to the smoke expect: debugfs
  -w removes the 18 exotic kimg_*.so from the PER-RUN fs.img copy (committed
  image untouched), keeps libqsvg/libqjpeg/libqico/libqgif; evidence `ls` in
  qt-minimal-imageformats-stage.log. NOTE: QT_PLUGIN_PATH scoping was analyzed
  and REJECTED — it is additive, cannot prune (design doc §2a). A/B (loader
  trace ON both arms, direct-launch konsole-only kprofile): dlopen count
  64->28 cold AND warm (exactly as designed); imageformats dlopen time cold
  70.8->4.0ms (-66.8), warm 56.4->4.3ms (-52.1); total dlopen time cold
  140->62ms, warm 117->50ms; kimg_* trace lines 36->0; shell-ready+bash-prompt
  fire both arms; konsole_wait 1305/1486 (ctl) vs 1192/1387 (prune) — within
  band, NOT the judge; screenshot diff metric identical (1001730) both arms
  (svg icons intact); virgl/D3D12 GL both; qtquick-policy 0 violations; crash
  grep 0. Control first attempt hit FM19 visible-timeout (crash grep 0),
  rerun-once passed. Archives:
  `kde-plasma-desktop-smoke-history/20260708T151810Z-u3-imageformats-control/`,
  `...T152010Z-u3-imageformats-pruned/`,
  `...T151611Z-u3-imageformats-control-attempt1-fm19-visible-timeout/`.
  Verdict: mechanism proven, deterministic win ~55-67ms of serial dlopen off
  the launch path. Promotion path (later, needs owner sign-off): bake the
  prune into make-rootfs (or ship the qt.conf per-app variant, design §2c);
  keep gate opt-in until the M4 lever stack (U2) decides what is worth
  compounding.
- U4 (PERF VERDICT DONE 2026-07-08 — engagement proven, NO hover win at n=2):
  async-cursor hover re-A/B at n=2 vs n=2 on a clean environment (all gates
  green: qtquick 0 violations, virgl/D3D12 GL, crash 0). Full desktop-
  interaction reducer, kwin ioctl-trace shim armed both arms. ENGAGEMENT PROOF
  (kwin ioctl shim, decisive): CURSOR2 (cursor-image upload) max duration
  collapses 27113/32164us (baseline OFF) -> 834/8557us (async ON); cursor
  sub-1ms 47/49 -> 49/49(rep1). This IS the 17-36ms->~0 upload the gate
  promised, now measured directly off the compositor ioctl path. HOVER RESULT:
  hover_median_visual_ms baseline {475.5, 487.0} vs async {491.5, 458.5} —
  fully overlapping, NO separation. konsole warm {1192,1205} vs {1191,1184};
  first_visible {6335,6541} vs {6170,6632} — all within noise. VERDICT: the
  cursor-upload cost is real and async-cursor eliminates it, but that cost is
  NOT on the hover-visual-latency critical path (the ~450-490ms hover floor is
  frame-pacing/sample-interval bound, not cursor-upload bound). So at n=2 vs
  n=2 in a clean environment async-cursor shows NO hover responsiveness win —
  within noise. Correctness GO stands (unchanged); promotion to default-on is
  NOT justified by a responsiveness win (keep opt-in). fbstat cursor_async
  counters were not capturable in the desktop-interaction reducer (kde-fbstat
  log came back "missing guest_path"); the shim CURSOR2 collapse is the
  engagement proof of record. Archives: guiperf phase1-baseline-rep1/rep2
  (20260708T161135Z/T161429Z), phase2-asynccursor-rep1/rep2
  (20260708T161715Z/T161936Z).
  STAGING APPLIED 2026-07-08 (was READY-TO-APPLY): the three kwin-ioctl-trace
  hooks below are now landed and boot-proven engaged — make-rootfs.sh builds
  kwin-ioctl-trace-preload.so into the abi-libs dir; kde-session.c
  spawn_kwin_child prepends it to the child LD_PRELOAD gated on
  kde_kwin_ioctl_trace=1 (default off, composes with alloc-trace/compat);
  smoke.expect plumbs guest/host kwin_ioctl_trace_log (set vars, globals,
  debugfs-rm, dump_optional_guest_artifact, cleanup rm). Image rebuilt with the
  full overlay set (host-gui:webkit-media:kde-runtime:gameboy-roms:gpup-umd);
  shim + kde-session injection + U1 egl-readiness-probe all verified baked in.
  - Reusable kwin ioctl-timing LD_PRELOAD shim: NOW REPO-OWNED (2026-07-08),
    replacing the three throwaway per-worker versions. Sources:
    `scripts/image/kwin-ioctl-trace-preload.c` +
    `scripts/image/build-kwin-ioctl-trace.sh` (deterministic build modeled on
    build-qtmm-shim.sh: SOURCE_DATE_EPOCH=0, -fno-ident, -Wl,--build-id=none,
    ffile-prefix-map, proof file with sha256/readelf -d/nm -D). Justified
    flag delta vs qtmm: this is a real interposer that calls libc+dlfcn, so it
    links libc/libdl (`-ldl`) instead of -nostdlib/-nostartfiles/-nodefaultlibs
    and has no version-script/-soname (an LD_PRELOAD object needs neither an
    exported-symbol allowlist nor a SONAME). Forbidden-DT_NEEDED check is
    inverted: libc/libdl/ld-linux REQUIRED, any Qt/KF5/drm/pulse/glib NEEDED
    fails the build; the proof also asserts the 5 interposer symbols are
    exported.
  - What it captures: wraps libc ioctl() (dlsym RTLD_NEXT, variadic single-arg
    forward) and CLOCK_MONOTONIC-stamps DRM_IOCTL_MODE_PAGE_FLIP /
    _MODE_ATOMIC / _MODE_CURSOR / _MODE_CURSOR2 — one line/op with op name, fd,
    duration_us, gap-since-last-same-op_us, ret/errno, plus flags+fb_id for
    PAGE_FLIP and flags for ATOMIC (struct fields read per the uabi/drm.h
    *_compat layouts; ioctl numbers + the few structs defined locally with a
    comment referencing kernel/kernel/inc/uabi/drm.h:60,70,81,82). Also tracks
    fds opened on /dev/dri/* (open/open64/openat/openat64 wrap; close wrap
    clears) PLUS inherited/pre-existing DRM fds the open wrappers never saw —
    seeded by classify-on-first-ioctl (any DRM-type 'd'=0x64 ioctl marks its fd)
    and a constructor /proc/self/fd scan (see U2 "BLIND SPOT CLOSED"); banner
    reports proc_seeded_fds + classify_on_first_ioctl=active, per-fd DRMFD_SEEDED
    lines record which seeder fired. Wraps read() ONLY on tracked fds to emit
    EVTREAD lines for DRM_EVENT_FLIP_COMPLETE/VBLANK with a since-last-submit_us
    delta (the submit/completion pairing that made the vblank-pacing A/B
    decisive).
    Env knobs: KWIN_IOCTL_TRACE_LOG (default /kde-kwin-ioctl-trace.log),
    KWIN_IOCTL_TRACE_MIN_US (default 0). Constructor banner + best-effort
    destructor summary (per-call lines are authoritative; kwin is SIGKILLed so
    the summary must not be relied on). Safety: __thread recursion guard,
    static buffers (no hot-path malloc), single per-line write() to the
    O_APPEND fd (crash-safe under SIGKILL, no interleave < PIPE_BUF), errno
    save/restore, fail-open to passthrough on every dlsym/log failure.
  - Compile/proof evidence (host cc, scratchpad out-dir): builds warning-clean
    under -Wall -Wextra; sha256 reproducible across two builds
    (2ee023457472fe04e0254b9dc6bae74dbb7475d0d9bbf1a87b8c68216282ffb6); NEEDED
    = libc.so.6 + ld-linux-x86-64.so.2 only; exports open/open64/openat/
    openat64/ioctl/read/close. Host functional test (no /dev/dri on the build
    host, so the EVTREAD/read path is code-review-verified only): two
    PAGE_FLIP ioctls logged fb_id=42/43 with gap_us=15125 across a 15ms sleep;
    ATOMIC flags=0x201; CURSOR/CURSOR2 logged; a non-DRM FIONREAD ioctl and a
    regular-file read/write passed through byte-exact (not logged); MIN_US=1e6
    gated every op line while keeping banner+summary.
  - Staging hook for the harness (READY-TO-APPLY, NOT YET APPLIED — the smoke
    expect and kde-session.c are owned by other in-flight workers; apply these
    when their changes land):
    1. `scripts/image/make-rootfs.sh` (next to the kwin-alloc-trace stanza at
       ~:971): build the .so into the abi-libs dir `${dir}`:

       ```sh
       if [[ -f "${REPO_ROOT}/scripts/image/kwin-ioctl-trace-preload.c" ]]; then
           "${cc_bin}" -O2 -Wall -Wextra -fPIC -shared \
               -o "${dir}/kwin-ioctl-trace-preload.so" \
               "${REPO_ROOT}/scripts/image/kwin-ioctl-trace-preload.c" -ldl
       fi
       ```

       (build-kwin-ioctl-trace.sh remains the standalone deterministic
       proof/repro path; make-rootfs mirrors its sibling's inline build.)
    2. `scripts/image/kde-session.c` spawn_kwin_child (mirror the alloc-trace
       block at :794-846): add
       `#define KWIN_IOCTL_TRACE_PRELOAD "/opt/xv6-kde-abi-libs/kwin-ioctl-trace-preload.so"`
       and `#define KWIN_IOCTL_TRACE_LOG "/kde-kwin-ioctl-trace.log"`, gate on
       `cmdline_has_flag("kde_kwin_ioctl_trace=1") && access(...,R_OK)==0`,
       prepend KWIN_IOCTL_TRACE_PRELOAD to the child LD_PRELOAD (composes with
       the alloc-trace/compat preloads the same way), and
       `setenv("KWIN_IOCTL_TRACE_LOG", KWIN_IOCTL_TRACE_LOG, 1)` for the child
       only (unset after spawn).
    3. `scripts/gpu/kde-plasma-desktop-smoke.expect` (mirror alloc-trace
       plumbing): `set guest_kwin_ioctl_trace_log "/kde-kwin-ioctl-trace.log"`
       and `set kwin_ioctl_trace_log "$outdir/kde-kwin-ioctl-trace.log"`; add
       `kde_kwin_ioctl_trace=1` to the kernel cmdline for the trace arm; pull
       it out with `dump_optional_guest_artifact $guest_kwin_ioctl_trace_log
       $kwin_ioctl_trace_log` and add both to the pre-run debugfs rm + cleanup
       lists.
  - Re-A/B verdict remains PENDING the VM lane; the shim is the instrument the
    re-run will use to compare submit->flip-complete pacing across the
    async-cursor and vblank-paced-flip arms. Three workers converged on this
    exact shim shape; it is now singular and reproducible.
- U5 (kernel, small) — IMPLEMENTED 2026-07-08, BUILD-CLEAN, VALIDATION
  PENDING: atomic commits now queue DRM_MODE_PAGE_FLIP_EVENT completions,
  mirroring the legacy path. `fb_kms_atomic.c` gpu_drm_mode_atomic: (a) new
  pre-present -EAGAIN ring-backpressure guard (scoped to non-TEST_ONLY +
  event flag + has_new_fb; same stats bumps as legacy :388-397); (b) on a
  successful non-TEST_ONLY commit that flips the sole CRTC (has_new_fb),
  emit exactly one DRM_EVENT_FLIP_COMPLETE with the sampled vblank
  seq/timestamp, crtc=GPU_DRM_CRTC_ID(1), and the ioctl's user_data —
  queued under fb_state.lock, notified after unlock. Respects the
  virtio_gpu_vblank_paced_flip gate (routes through gpu_drm_page_flip_paced,
  same kvmalloc-fail fallback-to-sync). Single boolean gate => no
  per-plane double-emit; TEST_ONLY never emits; flag-absent path is
  behaviorally unchanged. Teardown drain (gpu_fops_release ->
  gpu_drm_event_release_stale_locked) already covers atomic-queued events
  (shared owner->drm_events ring). Build proof: translation unit module.c
  (#includes fb_kms_atomic.c) compiled with the exact production command
  from compile_commands.json (-Wall -Werror) to scratchpad, clean; the
  in-flight build-x86_64 xv6.bin was NOT touched (no full cmake link ran,
  by design, to protect a concurrent KDE A/B boot). `git -C kernel diff
  --check` clean. VALIDATION (conductor, do not run yet): extend the
  legacy event read-back pattern in drmiftest.c:1668-1717 (ordered reads,
  user_data match, crtc_id==1, monotonic sequence, ring-full EAGAIN,
  overflow drain) to the ATOMIC ioctl, submitting via drmabitest.c:575-589
  atomic_commit_props() with flags|=DRM_MODE_PAGE_FLIP_EVENT + user_data
  set; nographic drmiftest/drmabitest run asserts one FLIP_COMPLETE per
  commit with matching user_data. GUEST TESTS IMPLEMENTED 2026-07-08
  (compile-clean, guest-run still PENDING): added to drmiftest.c ONLY
  (pragmatic choice — it already carries BOTH the legacy page-flip event
  ring template AND full atomic plumbing: real dumb-FB present + plane
  prop-id discovery, so a-e fit one fd/one file; drmabitest.c left
  untouched). New `check_kms_atomic_flip_events()` (called from main after
  check_kms_fb) + helpers `atomic_flip_commit()` (drm_mode_atomic_compat
  variant carrying flags + user_data) and bounded `drain_drm_events()`.
  Asserts: (a) one FLIP_COMPLETE per event-flagged real commit, user_data
  match, crtc_id==1, strict-monotonic sequence; (b) TEST_ONLY+flag => 0
  events; (c) flag-absent => 0 events; (d) fill the ring
  (DRM_XV6_EVENT_QUEUE_CAPACITY=16), next event commit returns -EAGAIN
  with kms_atomic_commits unchanged (proves pre-present), then all 16
  drain in order; (e) legacy PAGE_FLIP interleaved with atomic commits
  stays ordered+monotonic on the shared ring. Event layout read from
  uabi/drm.h drm_event_vblank_compat; reads non-blocking (empty ring =>
  read()<0) so no nographic hang; teardown drains ring + RMFB +
  DESTROY_DUMB. COMPILE PROOF: clean under the exact production command
  from build-x86_64/user-native-check/compile_commands.json (-Wall, and
  additionally -Werror) — the only available user compile db. NOTE: that
  (stale, 2026-06-30) db surfaces a PRE-EXISTING cross-header hard error
  (drmiftest.c includes vfs/fcntl.h while uabi/drm.h pulls uabi/fcntl.h;
  both define struct flock) that halts at the includes before any test
  code; not introduced here (no include added). A working guest drmiftest
  binary was built 2026-07-06 against these same headers, so the
  authoritative guest build resolves it; proof obtained by compiling a
  scratchpad copy with the redundant vfs/fcntl.h include dropped (O_RDONLY/
  O_RDWR, the only fcntl symbols drmiftest uses, come from uabi/fcntl.h).
  If the conductor's guest build DOES hit the fcntl conflict, the one-line
  unblock is to drop that redundant include from drmiftest.c. A KDE check
  would confirm no regression under kwin 5.27 (still legacy path) and
  readies kwin 6.x/AMS.
  Prerequisite for any future kwin 6.x/atomic modeset work (kwin 5.27
  disables AMS unconditionally in VMs; no env override).
  GUEST VALIDATION ATTEMPTED 2026-07-08 (first-ever guest run of the current
  drmiftest; kernel side clean, matrix line UNREACHED — validator drift):
  - Kernel builds+boots clean with U5 in five nographic boots (archives
    20260708T19*Z-u5u6-fix-*); the authoritative guest drmiftest build does
    NOT hit the fcntl conflict (predicted above; no include change needed).
  - drmiftest runs required kernel+test fixes to progress, all applied:
    (1) KERNEL ABI FIX (committed-code gap, fixed this pass):
    `drm_core_auth_magic` gained a correct "caller must be master" gate on
    2026-06-22 (1c2113c) but WITHOUT Linux's counterpart implicit grant, so a
    lone client could NEVER self-auth (GET_MAGIC->AUTH_MAGIC fails -EACCES on
    an idle device — on Linux the first opener becomes master at open). New
    `drm_core_master_open()` (drm_core.c, decl drm_core.h, called from
    gpu_open_file_common in fb_drm_dispatch.c): first opener of a PRIMARY
    node (only; legacy/render files are born authenticated) becomes
    master+authenticated iff none exists; released on close via the existing
    drm_core_release_file; a compositor's later SET_MASTER (same-owner
    cookie) is idempotent. KDE-validated same day (kwin still acquires
    master; real-GL session green — see gates below).
    (2) TEST REFRESHES (drmiftest.c, stale vs committed June kernel ABI):
    GETPLANERESOURCES now returns 2 planes (cursor plane added 2026-06-17
    d8903ff; old assert demanded exactly 1) and legacy DRM_IOCTL_MODE_ADDFB
    is an implemented shim (2026-06-06 385c40d; old assert demanded
    fail-closed) — both updated to assert the current contract.
  - RESULT (20260708T194027Z-u5u6-fix-ng-final2, desktop=0 so drmiftest can
    hold mastership): check_common + check_primary now PASS end-to-end incl.
    `kms_in_formats_blob_matrix ... status=PASS`,
    `kms_primary_scanout_format_mod_matrix ... status=PASS`,
    `kms_primary_scanout_actual_format_matrix ... status=PASS`, and
    `primary ok mode=1280x800`; then check_kms_fb FAILS on further stale
    fail-closed asserts. `kms_atomic_flip_event_matrix` is UNREACHED: main()
    gates check_kms_atomic_flip_events behind check_kms_fb.
  - REMAINING (needs a dedicated drmiftest validator-refresh pass by its
    owner — out of scope for the kernel crash-fix lane, and each iteration
    costs an image rebuild + boot): check_kms_fb still asserts pre-June
    fail-closed contracts for (at minimum) CURSOR BO (cursor-from-BO now
    implemented), CREATEPROPBLOB (user property blobs now implemented),
    CRTC_QUEUE_SEQUENCE (now functional AND queues a DRM_EVENT_VBLANK that
    must be drained before the page-flip event-order reads; the test's
    vblank_source_matrix additionally asserts deltas on
    kms_crtc_queue_sequence_rejects/_noevent_rejects — counters the current
    kernel NEVER increments — and hard-codes
    `crtc_queue_sequence_fail_closed=PASS` in the matrix text), and SETGAMMA
    (zero-size LUT now succeeds as a no-op). SETPLANE/GEM_FLINK/GEM_OPEN/
    SYNCOBJ_EVENTFD fail-closed asserts are also implemented-in-kernel now
    and need auditing. Until that refresh, U5's guest matrix stays pending;
    the U5 kernel emit path itself is exercised indirectly by the KDE run
    (legacy path unchanged, no regression).
  - VALIDATOR REFRESH DONE 2026-07-08 (drmiftest owner lane; drmiftest.c only,
    user submodule, uncommitted). U5's `kms_atomic_flip_event_matrix` is now
    REACHED and PASSES in a nographic guest boot (archive
    20260708T220649Z-u5-drmiftest-refresh):
      drmiftest: kms_atomic_flip_event_matrix event_per_commit=PASS
      test_only_noevent=PASS flag_absent_noevent=PASS
      ring_full_eagain_pre_present=PASS overflow_drain=PASS
      legacy_atomic_interleave=PASS crtc_id=1 capacity=16 status=PASS
    Per-assert refresh (old expectation -> kernel truth, file:line):
    - CURSOR BO: was "unexpectedly enabled" fail-closed -> cursor-from-BO is
      implemented (fb_drm_kms_properties.c:1364 -> gpu_kms_upload_cursor_from_bo
      :444). Now creates a dedicated 64x64 dumb BO, asserts upload succeeds +
      kms_cursor_uploads bumps, and that an over-max dim (>FB_GPU_CURSOR_MAX_DIM
      =64, fb.h:64) is still rejected.
    - CREATEPROPBLOB: was fail-closed -> implemented
      (fb_drm_kms_properties.c:148 gpu_drm_mode_createblob). Asserts create
      returns a nonzero id + DESTROYPROPBLOB accepts it; unknown-blob destroy
      still rejected (-ENOENT, :227).
    - CRTC_QUEUE_SEQUENCE: was "unexpectedly enabled" -> functional
      (fb_drm_core_kms.c:1764); queues one DRM_EVENT_VBLANK with the resolved
      target sequence / crtc_id=1 / user_data. Now asserts success, DRAINS the
      queued VBLANK (else it corrupts the following page-flip event-order
      reads), and keeps fail-closed on bad crtc (-EINVAL :1776) and bad flags
      (bumps kms_crtc_queue_sequence_bad_flags :1779). Dropped the two
      vblank_source_matrix delta asserts on kms_crtc_queue_sequence_rejects /
      _noevent_rejects — the kernel declares (fb.h:1081,1083) but NEVER
      increments those counters; matrix text updated
      (crtc_queue_sequence_functional/event_drained instead of fail_closed).
    - SETGAMMA: was "unexpectedly enabled" -> zero-size LUT succeeds as a no-op
      (fb_drm_core_kms.c:1830-1835); asserts success + bad crtc still rejected.
    - GEM_FLINK/GEM_OPEN (check_dumb, render fd): were fail-closed -> global
      names implemented (fb_drm_dispatch.c:486/498, fb_bo_shmem_dmabuf.c:1967/
      2005). Asserts FLINK returns a nonzero name, OPEN resolves it to a handle
      of matching size, and name 0 rejected.
    - SETPLANE: AUDITED, no drift — the primary plane still returns -EOPNOTSUPP
      (fb_drm_kms_properties.c:630); the existing asserts hold as-is.
    - SYNCOBJ_EVENTFD: AUDITED — implemented (fb_syncobj_prime_virtgpu.c:1170)
      but correctly rejects a non-eventfd fd (the syncobj fd passed) with
      -EINVAL; only the misleading message was corrected (behavior unchanged).
    Newly-exposed (test never reached these before; refreshed to the real
    committed contract, all now PASS):
    - WAIT_VBLANK: absolute WAIT_VBLANK(N) returns reply.sequence == the counter
      at the N-th edge (>= N), i.e. exactly N when crossing from below — it does
      NOT overshoot to N+1. Old assert required >= 42 for a request of 41;
      corrected to >= 41 (the real absolute-wait contract).
    - vblank_source_matrix: the old assert compared kms_vblank_sequence against
      crtc_sequence_sample. Those are NOT one monotonic scale: CRTC_GET_SEQUENCE
      returns the synthetic-time estimate (inflated during the idle WAIT_VBLANK
      block), while the display-present path OVERWRITES kms_vblank_sequence with
      the present count (fb_scanout.c:315), so the display-correlated sequence
      can read LOWER than an earlier idle synthetic sample (observed 22 vs 41).
      Refreshed to the coherent display-correlated contract: synthetic==0,
      display_correlated==1, source flags coherent, kms_vblank_sequence ==
      display_last_complete, and display_last_complete advanced. FINDING
      (pre-existing, non-U5): kms_vblank_sequence is non-monotonic across the
      synthetic-idle -> display-present transition (overwrite, not max) — ties
      to the recorded M4/M7/R9 "kwin has no vsync/present clock" root cause.
    - check_kms_sync_file_in_fence_matrix: the kernel waits in-ioctl on a
      pending sync_file in-fence via an unbounded dma_fence_wait(-1)
      (fb_drm_kms_atomic_props.c:534) and rejects NONBLOCK atomic, so there is
      NO non-blocking path; the old single-threaded submit-then-signal pattern
      DEADLOCKED. Refreshed to drive the pending in-fence commit from a forked
      child while the parent waits for the pending wait to register then
      SYNCOBJ_SIGNALs (dma_fence_signal in place on the shared fence,
      fb_syncobj_prime_virtgpu.c:860 area), mirroring check_syncobj_*_wakeup.
      Now PASS (pending_waits_delta=1, pending_wakeups_delta=1, refs balanced).
  - RESIDUAL (does NOT gate U5; blocks only the final `drmiftest: ok`):
    check_fence_callback_lifecycle_matrix reports fence_objects_live_delta=-1
    with ALL functional callback assertions passing (added=2/fired=1/removed=1/
    late=1/errors=0, fence_fd_live_delta=0). A fence object live at the matrix
    baseline is reaped within its window; not conclusively attributed —
    candidates are a pre-existing lazy fence-object free first exposed now, or a
    child-teardown side effect of the new fork() in the refreshed sync_file
    matrix. Needs one follow-up boot to attribute (deferred: this pass already
    used its boot budget). All drift up to and including U5's matrix is
    validated PASS.
- U6 (IMPLEMENTED + VALIDATED 2026-07-08; crash root-caused H2, see below):
  closed the dcache
  eviction/no-flush gaps. These fix a LIVE default-build bug: the positive
  dcache is ungated, so the stale-positive / child_sb UAF on unmount+remount
  was reachable with the gate OFF. Two unconditional fixes:
  - FIX A (generation uniqueness): global monotonic counter
    `__vfs_dcache_alloc_seq()` (atomic, first value 1) now seeds every
    `inode->lookup_seq` (inode.c:65) and backs every dir-seq bump
    (dcache.c `__vfs_dcache_bump_dir_seq`, RELEASE store). No two incarnations
    of the same (sb,ino) can ever share a parent_seq, so a stale entry can
    never satisfy the `parent_seq==seq` honor-check across eviction/reload or
    sb-address reuse. Removes gap 1 + parent side of gap 3.
  - FIX B (lifetime): `__vfs_dcache_invalidate_sb(sb)` (dcache.c) unlinks every
    entry with parent_sb==sb||child_sb==sb per-bucket under the bucket spinlock,
    frees AFTER unlock (store's discipline). Called under the sb WLOCK, before
    each `fs_type->ops->free(sb)` at fs.c mount-fail path (no-op there), and
    (placed before the `vfs_superblock_unlock(sb)` that immediately precedes the
    free) in vfs_unmount, __vfs_final_unmount_cleanup, vfs_unmount_lazy
    immediate path. Guarantees no freed sb is referenced by any surviving entry
    -> positive/negative honor derefs (child_sb->valid / parent_sb->valid)
    touch live memory only. Removes gap 2 + child side of gap 3.
  - Files: kernel/vfs/dcache.c, inode.c, fs.c, vfs_private.h (kernel submodule).
    Design deviations logged: design's "place at 1202/1412/1548" is AFTER the
    wlock is dropped; flush moved to before each `vfs_superblock_unlock(sb)` to
    keep it strictly under the wlock per the race proof. Design's free-loop
    `list_entry_del_init` (non-RCU, nonexistent) dropped — `_safe` iterator
    latches next before free.
  - Reducer: user/programs/dcachetest/dcachetest.c (user submodule,
    auto-discovered) — mount tmpfs /mnt -> creat /mnt/f -> stat (positive) ->
    umount -> remount -> stat /mnt/f MUST fail; plus 50x distinct-name loop
    re-statting prev name; plus 200x mount/umount churn UAF sentinel. Prints
    `RESULT=PASS/FAIL test=<name>`, exits 0 iff all pass. (xv6 userspace has no
    errno; asserts on sign of return. UAF surfaces as boot-harness panic.)
  - Review: adversarial review GO (2026-07-08). Two non-gating notes applied
    source-only: store-site comment documenting the child_sb==parent_sb
    current-tree fact (invalidate_sb does not rely on it), and invalidate_sb
    header corrected — lookups run WITHOUT the sb rlock (inode.c:664) and are
    safe via bucket spinlock + store-exclusion + free-after-return only.
  - Build status: kernel build DEFERRED to the conductor's consolidated
    validation batch (per coordinator; VM lane was busy the whole slice).
    Reducer compile-VERIFIED standalone with the exact user-program flags
    (host x86_64-linux-gnu-gcc, clean). `git -C kernel diff --check` clean.
  - VALIDATION CRASH ROOT CAUSE (2026-07-08, verdict H2 — PRE-EXISTING bugs
    exposed, NOT introduced by U6): the first validation boot
    (20260708T154610Z-u5u6-validation-nographic) crashed during
    remount_loop/mount_churn: `vfs_iput: warning: inode 2 iput with
    ref_count=0` x5 then ASSERTION fs.c "Superblock refcount underflow" in
    vfs_superblock_put from worker_thread. U6's diff provably touches no
    inode/sb refcount (only integer seq values + dcache entry unlink/free
    under the bucket spinlock); dcachetest is simply the FIRST test ever to
    churn tmpfs mount/umount/remount, exposing two default-path bugs:
    (1) `__vfs_evict_unused_inodes` (fs.c, sole caller vfs_unmount) evicted
    and FREED backendless (tmpfs) inodes at ref_count==1. For backendless fs
    idle-cached inodes sit at ref 0, so ref 1 is a LIVE reference — here the
    deferred fput of the just-closed file (close -> __vfs_fput_call_rcu ->
    call_rcu -> vfs_iput_wq workqueue -> vfs_fput -> vfs_inode_put_ref).
    Unmount freed inode+sb; the deferred fput then iput a freed/reused inode
    (the ref_count=0 warnings) and vfs_superblock_put a freed sb -> underflow
    assert. FIX: skip backendless inodes with ref_count>=1 in both the
    pre-lock and authoritative post-lock checks (matches
    tmpfs_unmount_begin's existing ref_count>0 policy). Unmount now reports
    -EBUSY instead of corrupting.
    (2) `vfs_mount` violated its documented failure contract (header:
    "on failure releases mountpoint inode lock and superblock lock") on all
    early-failure returns incl. `__vfs_turn_mountpoint` -EBUSY: it returned
    with the caller-held sb wlock + ilock still held; vfs_mount_path only
    unlocks on success and immediately vfs_iput(mountpoint) -> "cannot hold
    superblock write lock" assertion -> all-core IPI crash (reproduced boot
    2 when a mount raced the now-EBUSY unmount). FIX: `fail_unlock` path in
    vfs_mount releasing ilock + sb wlock (wholding-guarded) on every
    runtime-reachable early failure; plus 4 tmpfs_smoketest.c call sites
    fixed that unlocked unconditionally (would double-unlock on failure
    under the documented contract).
    SUPPORTING FIX (drain, so transient EBUSY does not surface to userspace):
    vfs_umount_path split into `__vfs_umount_path_once` + bounded drain of
    the deferred-fput pipeline on -EBUSY: up to 4 rounds of `rcu_barrier()`
    (guarantees the close's RCU callback has QUEUED the fput work) +
    new `flush_workqueue(vfs_get_deferred_iput_wq())` (waits until the
    work RAN; new API in proc/workqueue.c with `running_works` tracking,
    polls pending==0 && running==0 with scheduler_yield, kicks the manager)
    + retry. Genuinely-busy mounts still return -EBUSY. An earlier
    yield-only drain variant was proven insufficient on boot 2 (16 yields
    burned <1ms without the worker running; the wakeup chain
    queue_work->manager->worker needs real scheduling slack).
    Files: kernel/vfs/fs.c, kernel/vfs/vfs_syscall.c, kernel/proc/workqueue.c,
    kernel/inc/proc/workqueue.h, kernel/inc/proc/workqueue_types.h,
    kernel/vfs/tmpfs/tmpfs_smoketest.c.
  - VALIDATION RESULTS (archives under
    build-x86_64/kde-plasma-desktop-smoke-history/):
    20260708T191657Z-u5u6-fix-nographic (KDE session up, default cmdline),
    20260708T192632Z-u5u6-fix-ng-nodesktop, and
    20260708T194027Z-u5u6-fix-ng-final2 (the latter two with desktop=0,
    the last on the fresh fs.img): all three boots show
    `RESULT=PASS test=remount_basic`, `RESULT=PASS test=remount_loop_50x`,
    `RESULT=PASS test=mount_churn_200x`, `dcachetest: ALL PASS`, `DCRES=0`;
    zero `iput with ref_count` warnings, zero underflow/ASSERTION/
    IPI_REASON_CRASH; forktest FKRES=1 ("fork claimed to work N times!"
    exhaustion-OK), clonetest CLRES=0, cowtest CWRES=0. Drain behavior:
    ~251 transient one-round EBUSY retries across ~302 umounts, all
    converged (the `vfs_unmount: remaining inodes=2` lines are the per-first-
    attempt diagnostic). KDE direct-launch regression run
    (20260708T194657Z-guiperf-u5u6-crashfix-regression, guiperf recipe,
    validates the full kernel change set incl. drm_core_master_open under
    the real compositor): status DONE (status_code=0), real GL — kwin
    renderer `virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`, no
    llvmpipe/software fallback, no `failed to create dri2 screen` — qtquick
    violations 0 (threaded GL scenegraph, no software backend), direct-launch
    cold+warm `status=PASS`, konsole_wait_ms 1394 (cold) / 1196 (warm) —
    inside the settled 1.2-1.5s band — crashes 0 (no
    KCrash/SIGSEGV/Segmentation in crash capture or session logs), no
    lingering qemu. After validation, neg-dcache default-on can be
    reconsidered (substrate now in place).
- U7 (M7, parked; reopen-(a) FORENSICS DONE 2026-07-08 OFFLINE — verdict
  SUPERSEDE, do not reopen for unlocked-wait): native present / unlocked-wait.
  Writeup: scratchpad u7a-sync-completion-forensics.md. n1ab-unlocked-a6.log is
  GONE; surviving console copy build-x86_64/n1ab-unlocked-stall-archive.log has
  NO kernel printfs (debugcon-only), so (i)/(ii) is unresolvable from evidence
  and is settled by source audit + a decisive-experiment spec. (i) LOST-SIGNAL
  RACE = LOW/not root cause: audited every sync_done/sync_inflight/sync_stale
  transition in virtio_gpu.c (reap :3642/:3661-3683, sync_wait_done :2643,
  sync_post :2571, IRQ :2283) — sync_done set under q->lock+RELEASE / read
  ACQUIRE, reinit under q->lock before each reap, single-reaper via
  async_reap_serialize, FM24 closed (sync uses PRIVATE per-call `done`; shared
  async_wait single-waiter via async_wait_serialize). No live lost-wakeup
  window. (ii) VIRGL WITHHOLDS id-0 BEHIND FOREIGN ASYNC = LEADING:
  submit_mixed_async (:2798) posts id-0 with foreign async still in the FIFO
  ctrl ring; unlocked-wait's op_lock release (virtio_gpu_user.c:449-479) admits
  cross-context interleaving default mode forbids, so id-0 sits behind a
  slow/blocked foreign fence; head-of-line FIFO retire + the op_lock-holding
  sync waiter (irq_wait_ms=60000, wait_window_ms :2509) => one withheld id-0 =
  60s op_lock hold = every present parks = the simultaneous 67-73s all-thread
  stall in the archive. OVERLAP vs async-present (U2): async-present STRICTLY
  DOMINATES — it does NOT remove the op_lock convoy (present-copy still
  op_lock+drain+sync) but moves it to the fb-present workqueue, so a withheld
  id-0 delays ONE flip event instead of freezing kwin's event loop => the
  global-park failure that killed unlocked-wait cannot recur, and M7 paces on
  vblank. RECOMMENDATION: close U7 as superseded if the U2 async-present A/B
  lands M7>=55; only reopen the residual if async-present is validated and
  M7<55 (via workqueue/pacing, NOT op_lock release). Reopen cond (b) (per-ctx
  sync budget / irq_wait_ms tiering) is the only salvageable piece and is
  ORTHOGONAL — it also bounds a stuck workqueue present, so do (b) as a general
  robustness fix decoupled from unlocked-wait. M7=51.
  U7-PREMISE MEASURED 2026-07-09 (VM lane, 2 boots, kernel 3254ce6/repo
  6a3f67c): chromium-video kprofile A/B (KPROFILE_SECONDS=90, USE_KVM,
  virtio-vga-gl-primary, audio none) — CONTROL "PERF-VIDEO RESULT pass
  fps=51.2 speed=1.000 presentedFPS=51.2 decodedFPS=62.1 dropPct=9.63
  advanced=15.23" vs TREATMENT (virtio_gpu_async_present=1, token verified
  x3 in booted cmdline) "PERF-VIDEO RESULT pass fps=51.0 speed=0.998
  presentedFPS=51.0 decodedFPS=61.1 dropPct=7.54 advanced=15.37". Both
  windows COMPLETE (advanced ~= runMs 15s); both arms real virgl/D3D12 GL,
  crash 0, gpu_init/config/exit_error_count=0, fault_count=0; kms_page_flips
  897 vs 917, page_flip_events 1:1, all software_blit. dropPct improved
  (9.63->7.54) but presentedFPS is FLAT at ~51. VERDICT: async-present is
  landed+validated AND M7<55 => the recorded reopen condition for the U7
  RESIDUAL (workqueue/pacing, NOT op_lock release) is now formally met;
  presentedFPS ~51 is NOT present-ioctl-cost bound (consistent with the U2
  kill-criterion cadence null). CAVEAT: in-image fbstat does not print
  present_async_* (fields exist in fb.h:1770-1779 only; fbstat.c has no
  printf for them — only cursor_async), so per-run counter engagement is
  not capturable in-guest; engagement proof of record remains the
  2026-07-08 U2 shim A/B (PAGE_FLIP 925-1017us -> 64-73us median, 1:1
  FLIP_COMPLETE) + this run's cmdline token + absence of the
  "async-present workqueue unavailable" fallback print. Archives:
  `kde-plasma-desktop-smoke-history/20260709T151220Z-m7-asyncpresent-control/`,
  `...T152900Z-m7-asyncpresent-treatment/`.
  U7-RESIDUAL ATTRIBUTED + FIX IMPLEMENTED 2026-07-09 (OFFLINE, kernel edits
  UNCOMMITTED, TU compile-proof clean, NO boot — gate DEFAULT OFF; A/B deferred
  to conductor). ATTRIBUTION (mined the fresh U7 A/B archives + qemu traces +
  fbstat): the 51fps ceiling is NOT (a) present-ioctl-cost bound (async-present
  already collapsed the flip ioctl 24ms->0.65ms with presentedFPS flat 51.2->
  51.0), NOT (b) software_blit-bandwidth bound (fbstat control blit_bytes
  3678208000 over 898 full_blits = EXACTLY 1280x800x4 = 4,096,000 B/blit, ~200
  MB/s at 51fps — orders below memcpy BW; copy_ticks=0 per the settled
  attribution), and NOT (c) make_room/retire back-pressure during video (qemu
  trace steady state is a clean 1:1:1 `ctx_submit ctx0x2 size 4336` /
  `res_flush 1280x800 res {0x5,0xe,0x4}` (triple-buffered) / `fence_resp`, and
  fence_ctrl==fence_resp==965 => no ring-fill backlog; the H1 make_room stalls
  were the konsole-LAUNCH finding, ring depth already 60). ROOT CAUSE (confirmed
  from source): kwin has NO free-running vsync clock — the flip-complete event
  carries the DISPLAY-CORRELATED vblank (gpu_kms_sample_vblank_locked overwrites
  kms_vblank_sequence with display_last_complete, fb_drm_core_kms.c:1412; fbstat
  control kms_vblank_display_correlated=1, seq=898=display_last_complete). kwin
  5.27's RenderLoop schedules next-repaint at lastPresentationTimestamp +
  16.67ms; with lastPresentationTimestamp = REAL completion time (present
  round-trip ~3ms per U2 EVTREAD + repaint + wakeup), that per-frame overhead is
  ADDED to every interval, sliding the loop to ~19.6ms = 51fps (matches the
  recorded "mono-modal ~22.7ms throughput limiter, not vsync-beat bimodal",
  ~19.6ms post total-reaper). async-present and vblank_paced_flip both left this
  intact (vblank_paced still chains off real completion — "next edge PAST real
  completion") => both measured M7-null. This is the recorded M4/M7/U5 "kwin has
  no vsync/present clock" root cause, now confirmed as the M7 ceiling.
  FIX (highest-leverage, matches task candidate (c)-opt1): new gate
  `virtio_gpu_present_clock_60hz=1` (DEFAULT OFF; composes with + REQUIRES
  virtio_gpu_async_present=1; no-op when async off => sync flip path
  byte-identical). In the async-present worker, AFTER the blit completes and the
  R5-critical BO ref is dropped (blit -> fb_bo_put -> [arm completion]), the
  flip-complete event's seq/ts is taken from a free-running, wall-clock-anchored
  60Hz grid (new gpu_kms_present_clock_next_locked: monotonic +1/present, edges
  16.67ms apart, self-healing snap-forward past missed edges = a dropped frame,
  correct vsync) INSTEAD of the real-completion clock, and delivered at that grid
  edge via the existing tested gpu_drm_pace_completed_flip_by_id timer/
  reservation/never-drop path (helper returns base=(grid_seq-1, grid_ts-period)
  so pace's internal +1 lands exactly on the grid edge). kwin then paces off a
  stable 60Hz grid instead of a sliding real-completion chain => grid-locks at
  16.67ms = 60fps. SAFETY: changes only the event's CLOCK, not its ordering vs
  the blit (armed after blit+put), so NO new tearing/UAF over async-present;
  gpu_kms_sample_vblank_locked is still called for its side effects (keeps
  kms_vblank_sequence coherent for fbstat/WAIT_VBLANK/CRTC_GET_SEQUENCE), clock60
  overrides only the delivered seq/ts. SUPERSEDES vblank_paced_flip's completion
  timing when both set. WHY higher-leverage than alternatives: retire-batching
  wouldn't help (no video ring backlog), direct-scanout skip-copy is
  architecturally fail-closed on this host (all kms_present_reject_*=898,
  native_present_credit=0, no DDA/nouveau/D3D12-bind) AND the copy isn't the
  limiter (copy_ticks=0). Files (kernel submodule): fb_drm_core_kms.c (gate
  reader + free-clock helper), fb_drm_kms_properties.c (worker: clock60 route +
  one-shot "present-clock 60Hz engaged" banner, printed OUTSIDE the lock),
  inc/dev/fb.h (2 END-appended stats: present_clock60_events_total,
  present_clock60_snap_total). COMPILE PROOF: dev/fb module.c TU rebuilt with the
  production -Wall -Werror command from build-x86_64/kernel/build/
  compile_commands.json to scratchpad (in-tree .o untouched), clean; `git -C
  kernel diff --check` clean. ADVERSARIAL SELF-REVIEW (no blockers): (R1) base_ts
  = anchor+(target_seq-1)*period >= anchor >= 1 > 0, no underflow, predict()
  never early-returns 0; (R2) target_seq>=1 always => base_seq>=0, delivered seq
  monotonic across snaps (wall clock monotonic); (R3) worst-case pace re-reads
  now >= target_ts => delay clamps to 1ms (rare, benign); (R4) grid statics +
  stats touched only under fb_state.lock; (R5) monotonic_ns()-under-lock has
  precedent (gpu_drm_page_flip_paced:1575); (R6) printf moved out of the lock
  (no printf-under-fb_state.lock precedent existed); single-threaded fb-present
  wq => `announced` static race-free; (R7) reservation lifecycle UNCHANGED (rides
  the timer, released by timer_cb; exactly one release, same as vblank_paced);
  (R8) no early delivery vs blit => tearing guardrail holds; (R9) M6 mesakmsgl
  under clock60+async would read ~60 (same documented vsync tradeoff as
  vblank_paced; A/B uses chromium-video, not M6). A/B SPEC (conductor, M7 recipe,
  DO NOT BOOT until VM lane frees): CONTROL = async-present only
  (`virtio_gpu_async_present=1`), TREATMENT = +`virtio_gpu_present_clock_60hz=1`;
  chromium-video kprofile KPROFILE_SECONDS=90, USE_KVM, virtio-vga-gl-primary,
  audio none; verify both tokens in booted cmdline (op-caution 1). ENGAGEMENT:
  grep run.log for `present-clock 60Hz engaged` (one-shot banner) in TREATMENT,
  absent in CONTROL. PRIMARY: presentedFPS 51 -> target >=55 (goal), ideally
  ~60; decodedFPS unchanged ~61; dropPct expected to fall further. DECISIVE
  KILL/CONFIRM (fbstat, if capturable, else infer): if presentedFPS rises toward
  60 with present_clock60_snap_total << events_total => CLOCK hypothesis CONFIRMED
  (limiter was the missing free vsync clock). If presentedFPS stays ~51 AND
  snap_total ~= events_total => residual is HOST PRESENT THROUGHPUT (worker
  blit+round-trip routinely > 16.67ms), NOT the clock — pivot to host-retire, do
  NOT relitigate the clock. GUARDRAILS (mandatory, same as async-present A/B):
  real virgl/D3D12 GL both arms, qtquick 0 violations, crash grep 0, no
  image-layout race, screenshot direct-launch-diff = settled 1001730 (tearing
  guard — clock change must not corrupt frames), M4/M5/M8 within noise,
  fork/clone/cow signatures same boot. PROMOTION: only on a proven presentedFPS
  win with owner sign-off + full regression battery (keep default-OFF until then,
  same posture as all prior M7 gates).
  A/B VERDICT — MEASURED 2026-07-09 (conductor, pushed kernel fcb0ebe + fbstat
  present_clock60 printf added so counters ARE capturable; chromium-video kprofile
  90s, USE_KVM, virtio-vga-gl-primary, audio none; CONTROL=async-present,
  TREATMENT=+present_clock_60hz; both windows complete advanced~15, real virgl/
  D3D12 GL both arms, crash 0, fault/init/exit error counts 0, FM17 launch-
  evidence-missing bookkeeping expected). ENGAGEMENT PROVEN: TREATMENT run.log
  banner "present-clock 60Hz engaged" (absent in CONTROL), both tokens x3 in
  cmdline. RESULT: presentedFPS 52.3 (CONTROL) -> 53.4 (TREATMENT), +1.1fps
  (+2.1%); dropPct 6.50 -> 6.22; decodedFPS ~61-63 unchanged. DECISIVE COUNTERS
  (fbstat): present_clock60 events_total=913 (== kms_page_flip_events, i.e. ALL
  session flips routed through the clock60 path) snap_total=58 = 6.35% << events.
  READ: the clock ENGAGES and PACES a clean 60Hz grid, and the async worker KEEPS
  UP with it (snap only 6.4% — it is NOT the limiter). Yet presentedFPS moved only
  +1.1 and did NOT rise toward 60; goal >=55 NOT met. This is the enumerated third
  case (worker keeps up AND FPS stays sub-60): the clock hypothesis is confirmed as
  a MECHANISM (free vsync grid works, minor real win) but REFUTED in MAGNITUDE — the
  U7-RESIDUAL prediction that a clean completion clock would grid-lock kwin to ~60fps
  does not hold. Since a clean 60Hz completion clock that the worker feeds on time
  still yields only 53.4fps present, the dominant M7 ceiling is DOWNSTREAM host/kwin
  present throughput (kwin RenderLoop does not convert clean completion timestamps
  into 60fps repaint scheduling), NOT the completion-clock timestamp. PIVOT
  (honest): host-retire / present-throughput; do NOT relitigate the completion
  clock. Gate `virtio_gpu_present_clock_60hz=1` stays DEFAULT-OFF (real but
  sub-threshold +2.1% win; no promotion). Also ran hoverprobe under the treatment
  tokens (M10 check): hover_in 203.5 / hover_out 157.8 / click 284.0 ms median,
  WITHIN NOISE of the ~205/193/250-312 baseline — no interaction win from the clock
  (kwin damage->repaint schedule latency dominates, per U2). Archives:
  20260709T164918Z-m7-clock60-control, 20260709T170251Z-m7-clock60-treatment,
  20260709T170502Z-m10-hoverprobe-clock60-treatment. U7-RESIDUAL: clock-60 lever
  EXHAUSTED (measured, sub-threshold); residual ceiling reassigned to host present
  throughput.
- U8 (M8) — MEASURED 2026-07-08 (FIRST VALID reading; GOAL MET, large
  margin): idle-desktop host-CPU on a real-GL boot with the N5 poll-notify
  full-wait gates default-ON (evdev fix). Recipe: `launch-gui.sh USE_KVM=1
  QEMU_GPU=virtio-vga-gl-primary` (detached, serial captured). Validity gates
  all green: real virgl via WSL D3D12 (NVIDIA) — virgl capsets ready, virgl
  3D scanout, renderD128 = "virgl render node OpenGL", 0 llvmpipe/swrast;
  presentation ACTIVE (8 `virtio_gpu: page-flip present` lines — boot/settle
  burst then a t=80s burst; kwin 5.27 is repaint-on-damage so a fully static
  idle screen then stops flipping, which is the genuine idle condition);
  session-ready `KWin and plasmashell are running`; 0 crash markers; booted
  cmdline has 0 N5 opt-out tokens (poll_notify_full_wait / af_unix_...=0),
  confirming both gates ON. METHOD: `/proc/<qemu-pid>/stat` utime+stime delta
  over three consecutive 10s windows, CPU%=delta_ticks/(10*CLK_TCK)*100,
  CLK_TCK=100. READINGS: 13.7% / 13.6% / 13.9%, MEAN 13.7%. Cross-check: a
  4th window's process-stat delta (13.1%) equals the sum of all 31
  per-thread deltas (13.2%) — /proc/PID/stat correctly aggregates the 6 vCPU
  threads, so 13.7% is directly comparable to the historical thread-summed
  85-130% band. ps lifetime pcpu 24.6% (amortizes the boot+settle spike;
  instantaneous idle ~13-14%). VERDICT vs GOAL <100%: PASS with wide margin;
  vs historical 85-130% band: FAR BELOW — this is the N5 payoff (idle no
  longer busy-spins poll rescans; vCPUs halt at idle). 1 boot, clean
  shutdown, no lingering qemu. Archive:
  `kde-plasma-desktop-smoke-history/20260708T201747Z-u8-m8-idle-cpu-gl/`
  (run.log, gl-presentation-proof.txt, m8-readings.txt, debugcon.log,
  qemu-proc-stat-final.txt). NOTE: the 07-05 44%/57-64% readings are
  superseded — they were non-GL boots that never presented; this is the
  first reading on a GL boot with confirmed active presentation.
- U9 (N2) — FORENSICS DONE 2026-07-08 (OFFLINE; scratchpad
  u9-rodata-fault-forensics.md): the `cr2=0x1aafdd193 err=0x2
  rip=0xffff80000039a34c` fault is CORRUPTED CONTROL FLOW INTO `.rodata`
  (err=0x2 = supervisor WRITE; RIP genuinely in `_rodata` 0x35e000..0x3b7000;
  the executed rodata bytes decode to a stray write at cr2 — cr2 and the
  `sig_trampoline` symbol are second-order symptoms of executing data, not the
  origin). Frame `0xbefc6cc0` is INSIDE CPU-2's idle kstack (0xbefc0000+32KB),
  so the corruption sits on the idle thread's own control-flow (saved
  return-addr / a sched or IRQ pointer) — a stray write from elsewhere.
  ROOT CAUSE (named, plausible): `rcu_head_cache` free-list / callback
  corruption — the R3 lane's unroot-caused double-free/UAF under fd-teardown
  load. The slab free list is intrusive (next-ptr at obj offset 0 = the
  rcu_head's own `->next`; mm/slab.c:601,634), so a double-free/UAF-write of a
  slab rcu_head corrupts it; a corrupted head reaching `func(data)`
  (lock/rcu.c ~866) or `call_rcu` scribbling through a bad free-list ptr
  (rcu.c:806-810) = the wild transfer seen. CORROBORATION: R3's archived
  double-free is `cpu=2` on `rcu_cb/2` (SAME core), clustered on socket/fd
  teardown, and `repairing corrupt freelist cache='rcu_head_cache'` was seen
  07-04 (slab.c:624). The heavy fd/inode churn funnels through the deferred
  fput path (close->call_rcu->workqueue->vfs_fput; vfs_syscall.c:911-946,
  file.c:134). AUDIT: `ext4_read_page_direct` itself is CLEAN
  (ext4fs_file.c:686/89/186 — only local bio_alloc/add_folio/await/release, NO
  call_rcu/rcu_head; DMA target pinned page_ref>=2 + io_in_progress; transient
  compound nodes explicitly fall back, :754) — it is a CORRELATIONAL ACCELERANT
  (−50% latency raises the pre-existing bug's recurrence rate), not the
  corrupter (the R3 double-frees also hit the direct-read-OFF arm). U6/R5
  cross-ref: NEITHER retroactively explains it — the crash kernel (18:53,
  pre-04b1ee2) ALREADY had the R5 stale-TLB fix (67d7b5c), and U6's fix fires
  only on unmount (absent from a KDE run; the runtime LRU evictor
  vfs_evict_lru_inodes is ref==0-guarded). Both are SIBLINGS in the same
  call_rcu/stale-frame lifetime family; they narrow the surface but leave the
  R3 door (best match) open. RETRY: N2 CAN be retried as a DIAGNOSTIC-ARMED
  battery (not a blind flip) — the corrupter is whole-system and direct-read-
  independent, so it should not block direct-read indefinitely. BATTERY (spec
  in scratchpad §6): static/build + nographic safety gate now ALSO asserting
  `dcachetest RESULT=PASS`, then the cold-cache A/B with EVERY arm armed
  `ext4_read_page_direct_debug=1 rcu_head_trace=1` (ON) / `=0 rcu_head_trace=1`
  (OFF), complete the interrupted 5x5 and keep accruing to zero faults, all
  §5 guardrails green (real-GL, qtquick 0, crash-grep 0 incl.
  KERNEL PAGE FAULT/__slab_obj_put/repairing corrupt freelist/rcu_head_trace:).
  If it recurs, 04b1ee2's full reg/PTE/instr/.rodata-classifier dump +
  rcu_head_trace owner-history decides rcu-corruption (→ becomes an R3 root-
  cause, N2 stays behind it) vs an ext4-direct-read-ring correlation (→ kill
  promotion) vs neither (→ R5-residual/sched scribble). Standing rec: add
  `rcu_head_trace=1` (default-off, ~0 cost) to EVERY future KDE battery to
  finally root-cause R3.
  BATTERY EXECUTED 2026-07-09 (diagnostic-armed 5x5 complete; NO default
  flip — decision stays with conductor): kernel d052c5d rebuilt no-op
  (binary current), tree clean. NOGRAPHIC SAFETY GATE PASS
  (`20260709T014010Z-n2-diag-safety-gate`, cmdline ext4_read_page_direct=1
  rcu_head_trace=1): dcachetest x3 rc=0 (ALL-PASS==rc0), forktest rc=1
  exhaustion, clonetest rc=0, cowtest rc=0; crash grep clean (the
  `vfs_unmount: remaining inodes=2` console flood during dcachetest mount
  churn is the expected U6 deferred-fput EBUSY-retry diagnostic, NOT a
  fault). COLD-CACHE A/B (desktop-interaction reducer, active-sample,
  audio=none, software-GL unset, KVM+virtio-vga-gl-primary; ON arm
  `ext4_read_page_direct=1 ext4_read_page_direct_debug=1 rcu_head_trace=1`,
  OFF arm `=0 rcu_head_trace=1`; tokens verified in every booted cmdline;
  debug ring arms silently, dumps only on fault, so token==armed):
  ON 5/5 PASS (`20260709T014901Z/T015302Z/T020047Z/T020459Z/T020909Z-n2-
  diag-on-run1..5`; konsole_wait warm 1202-1313 cold 1498-1613,
  first_visible 6301-8541, real-GL virgl(D3D12) all, qtquick 0 violations,
  crash grep 0, direct-launch-diff 1001730 all five).
  OFF 4/5 PASS (`...T015106Z/T015451Z/T020245Z/T021055Z-n2-diag-off-run1/
  2/3/5`; same guardrails green) + 1 CRASH: OFF run4
  (`...T020704Z-n2-diag-off-run4-kwin-gp-r5class-crash`)
  kde-session-ready-crash = kwin_wayland USERSPACE #GP
  (`pid 59 kwin_wayland: exception 13 (#GP) rip=0x7ffffd8545e5 err=0x0`)
  with rbx=0x2d34365f3638782f — the BYTE-IDENTICAL R5-class bad pointer
  from 07-04 (little-endian ASCII "/x86_64-", path bytes read as a
  pointer). Kernel side of that run: ZERO kernel faults, ZERO
  rcu_head_trace anomalies, ZERO slab/double-free/freelist lines — the
  armed kernel diagnostics saw nothing, so this corruption event is
  userspace-visible-only (recycled-byte/ld.so family), not an observed
  kernel rcu_head event. FM19 reruns (not counted, both crash-grep-clean):
  on-run1 attempt1 visible-timeout (`...T014604Z-...-attempt1-fm19-
  visible-timeout`), on-run3 attempt1 prompt-sync-timeout
  (`...T015654Z-...-attempt1-fm19-prompt-sync-timeout`).
  VERDICT: (1) the 07-04 `.rodata` KERNEL page fault did NOT recur across
  the full armed 5x5 (12 KDE boots incl reruns + 1 gate) — the direct-read
  kill criterion did NOT trigger, and the ON arm is 5/5 clean including
  the run-5 slot that faulted on 07-04. (2) The battery is NOT fully green:
  the R5-class kwin #GP recurred on the CONTROL arm (direct-read OFF) —
  strict §5 "full matrix clean" promote criterion NOT met. (3) Attribution:
  a fault-family recurrence with direct-read disabled is further
  EXCULPATION of ext4_read_page_direct (accelerant-not-corrupter thesis
  reinforced); and rcu_head_trace silence during the event weakens the
  R3-rcu path as the explanation for THIS class, pointing at the
  recycled-byte/mapping corruption family instead. RECOMMENDATION
  (conductor decides): do NOT flip yet; either (a) accrue one more
  OFF-heavy armed battery to separate "R5-class background rate" from the
  promotion signal, or (b) explicitly decouple N2 promotion from the
  direct-read-independent R5-class userspace bug (documented recurring
  since 07-04 without the token) and promote on the 5/5-clean ON evidence;
  either way prioritize root-causing the kwin #GP using the fresh archive
  (it now has kde-exception-mem reg/memory dumps). Boots used: 17 total =
  1 gate PASS + 4 gate harness-shakeout boots (guest-console lessons:
  op-caution 7 confirmed + typeahead-flush addendum) + 10 counted A/B + 2
  FM19 reruns; exceeded the <=14 budget by 3, all on gate shakeout.
  Hygiene: no lingering qemu, tree clean except this plan update.
- U9b (R5-class kwin #GP) — OFFLINE FORENSICS DONE 2026-07-09 (0 boots;
  scratchpad r5class-kwin-gp-forensics.md). SITE: libQt5Core.so.5 +0x3075e5
  = `QObjectPrivate::connectImpl` `mov 0x8(%rbx),%rdi` (base 0x7ffffd54d000,
  IDENTICAL to the 07-04 run => low-entropy/deterministic ASLR). rbx =
  0x2d34365f3638782f = "/x86_64-" (ld.so search-path scratch slice),
  byte-identical to 07-04; a DIFFERENT insn site than the 07-04
  QMutex::unlock @0xdbc9f but the SAME recycled-frame poison family (single
  8-byte pointer slot overwritten; rdi/r12/r14 heap ptrs healthy). Both
  sightings share phase: KWin attempt-1, cold-cache, session bring-up.
  MECHANISM: same kernel frame-recycling family as 07-04, at residual rate
  after the 67d7b5c ordering fix (verified present: munmap/madvise/mremap all
  flush-before-free). The cpumask-miss path (trap entry clears vm->cpumask,
  trap.c:2009, but keeps the user TLB — no CR3 switch) is SELF-HEALING with
  PCID OFF (this run: `max ASID=0`): userret does an unconditional MOV-CR3
  full-flush (trampoline.S:36) and kernel user-mem access is software-walk +
  direct-map (copyout walk/walkaddr), so neither user-mode nor kernel-mediated
  stale writes corrupt. => residual is one of two kernel-silent frame-recycling
  micro-windows offline analysis cannot separate: (a) shootdown-vs-concurrent-
  fault / lock-drop timing residual, or (b) COW refcount under-count under the
  fork storm. Host/virgl DMA and pure-userspace UAF are DISFAVORED (poison is
  guest ld.so path text; 07-04 residue was in kernel-zero-filled .bss tails).
  PROMOTION HAZARD (record): do NOT enable `x86_pcid=1`+`x86_cr3_noflush=1`
  before fixing cpumask completeness (in-kernel CPUs holding the user CR3) —
  noflush userret (trampoline.S:35, bit63 set) REOPENS the shootdown-miss into
  a live corruptor. DECISIVE EXPERIMENT (<=1 boot, OFF arm): default-off knobs
  `anon_free_poison=1` (canary-fill anon frames at free) +
  `anon_fault_verify=1` (assert zero-or-canary at anon/.bss fault-in install;
  log pa/vm/pid/va+bytes on mismatch) + `vm_shootdown_cpumask_audit=1`
  (log CPUs whose live CR3==pagetable but absent from vm->cpumask). verify
  fires with path bytes => confirms+localizes kernel free-then-write (cpumask
  audit picks (a) vs COW (b)); verify silent + #GP recurs => exonerates
  frame-recycling, pivot to userspace/host. Also add faulting-VA PTE/frame
  dump to the kwin fatal handler.
  EXECUTED 2026-07-09 (9 boots; kernel edits UNCOMMITTED in submodule; full
  detail scratchpad r5class-kwin-gp-forensics.md par.8-9). Knobs implemented
  (cached-parse, default OFF; canary=0xA5A5A5A5<<32|pfn per frame):
  page.h/page.c/vm.c + x86 vm.c audit. ITERATION 1 (anon-only poison,
  run1b): 28 verify fires = FALSE POSITIVES root-caused to slab intrusive
  freelists (__slab_make writes obj-next PAs into PAGE_TYPE_SLAB pages;
  __slab_destroy frees via the unpoisoned __page_free path; signature
  own-page-PA @obj_size offset). FIX: poison extended to ALL page types at
  every allocator sink (__page_free, __page_ref_dec order-0, anon batch) +
  free-site attribution ring (pfn->freeing pid/name/sink/page-type/jiffies,
  printed on hit). Both nographic gates PASS (dcachetest x3 rc0, fork/clone/
  cow expected rc, 0 false fires, 0 crash markers). RESULT (4 armed OFF-arm
  KDE boots: run2 PASS 2 hits, run3 PASS 3 hits, run4 FM19-artifact-timeout
  0 hits, run5 FM19-prompt-sync 3 hits; crash-greps all clean; no #GP
  recurrence): 8 TRUE kernel write-after-free events, all the SAME invariant
  signature — EXACTLY 9 zero bytes at page offset 0x0-0x8 (8-byte NULL word +
  1 low byte) written 3-73 jiffies AFTER free into frames freed by
  worker_thread pid 43/44 (total-reaper) via page_free sink, PAGE_TYPE_ANON =
  the REAPER-FREED THREAD/KSTACK COMPOUND (thread_create/__kstack_arrange
  place struct thread+utrapframe+sched_entity on the kstack alloc;
  thread.c:387). Victims: kwin_wayland x5, plasmashell, kbuildsycoca5,
  pipewire heap faults at session bring-up. MECHANISM VERDICT: neither (a)
  nor (b) — a THIRD mechanism (c): post-mortem stale-struct-thread write
  (small {ptr,flag}-shaped zero store) after the total-reaper frees the
  thread page; R3-adjacent deferred-teardown lifetime family; bring-up
  thread churn explains the attempt-1 phase lock. Not (b): freeing context
  is reaper page_free, and single-threaded victims hit. Not (a): payload is
  a kernel-struct store, not a user write; audit MISSes (126-128/run,
  capped) are the pervasive benign trap-entry state = (a) PRECONDITION only,
  PCID-OFF self-healing per par.4(2) — promotion gate stands. CAVEAT: all 8
  payloads are zeros (invisible without canary; fault-fill re-zeroes) — the
  NONZERO path-byte writer behind the actual #GP did not recur in 4 armed
  boots (~1-in-5 OFF-arm residual rate; honest null), but family (c) is the
  only demonstrated writer into recycled anon-fault frames = prime suspect.
  NEXT LANE (0 boots to start): audit stale struct-thread users post-reap
  (tq/sched-entity refs, futex/signal, p->lock users, proctab lookups);
  search key = the 9-byte {NULL-ptr + zero-byte} store; then rerun this
  battery — verify silent => R5 family closed. Knobs stay opt-in (full
  poison = 4K memset per free; run4/5 FM19 timeouts may include this cost).
  ROOT-CAUSED + FIXED + VALIDATED 2026-07-09 (6 boots; kernel edits
  UNCOMMITTED; full chain scratchpad r5class-kwin-gp-forensics.md §10). The
  family-(c) "stale struct-thread" hypothesis REFINED away — the writer is
  the KERNEL VM (kvmalloc-large) path, found via the search key: (D1)
  kvm_munmap freed frames BEFORE its TLB flush (free-before-shootdown, the
  67d7b5c class, unfixed on the kernel-VM path; kernel/mm/vm.c), and (D2)
  kernel PTEs are GLOBAL (PTE_G) so they survive user-mode CR3 reloads,
  while vm_remote_sfence*(kernel_vm) only shot kernel_vm->cpumask = CPUs
  currently IN the kernel — a CPU in user mode kept a stale kernel-VA
  translation indefinitely and later wrote through it into the freed/
  recycled frame. Matches every observable: kvm VAs are page-aligned (page
  offset 0x0 in 8/8 hits); the 9-byte store is a kvmalloc-object {u64;u8}
  header clear by a thread that migrated to a stale-TLB CPU; environ/
  cmdline/D-Bus/sockbuf kvmalloc buffers carry "/usr/lib/x86_64-linux-gnu"
  path text (the "/x86_64-" #GP poison, 07-04+07-09); 3-73 jiffies = TLB
  persistence; kernel-silent (TLB hit never faults); attempt-1/cold-cache =
  max kvmalloc churn; worker_thread pid 43/44 = frequent FREER of the
  recycled ANON frames (red herring for the writer). FIX (kernel,
  uncommitted): arch/x86_64/mm/vm.c vm_remote_sfence/_page target
  get_cpu_active_mask() for is_kernel VMs; kernel/mm/vm.c new
  __kvm_unmap_range_flush_free() — batched clear(locked) -> UNLOCK ->
  flush local+all-remote -> free, vma_free only after all spans flushed;
  used by kvm_munmap + both kvm_mmap rollbacks; new VMA_FLAG_KVM_DYING
  guards double-kvfree across the batching. LOCK LESSON (cost 1 boot):
  vm_wlock(kernel_vm) is a SPINLOCK — IPI-sync-wait inside it deadlocks
  bring-up (fix iteration 1, gate 20260709T140233Z hang); the IPI must run
  with the lock dropped. VALIDATION: safety gate PASS
  (20260709T141330Z-u9b-kvmfix-safety-gate2; dcachetest x3 rc0, fork/clone/
  cow expected, crash grep 0, 0 verify fires) + 4 armed OFF-arm KDE runs
  (20260709T142025Z/T142237Z/T142451Z/T142652Z-u9b-kvmfix-off-run1..4):
  ALL PASS, ZERO anon_fault_verify hits 4/4 (pre-fix 8 hits/4 runs;
  P(0|~2/run)~3e-4), real-GL both, qtquick 0, crash greps 0, konsole_wait/
  first_visible inside the armed-baseline band (no shootdown-cost
  regression). U9b CLOSED as "demonstrated writer channel eliminated";
  HONEST RESIDUAL: the #GP itself (~1-in-5 OFF-arm) not re-observed in 4
  runs — consistent but not conclusive for the tail; PCID promotion hazard
  (user-vm cpumask completeness) STANDS, this fix covers the kernel VM
  only. Diagnostics knobs remain opt-in.
- U10 (N3 residual): KWin LibinputBackend nullptr payload bug.
- U11 (P1/M2 <1.5us): needs a NEW approach; cpumask/CR0.TS is dead.
- U12: push the pending commits (currently ~7 ahead of origin) — needs
  explicit user approval.

## LANE STATUS ROLL-UP (2026-07-08 end-of-execution)

U1 DONE (metadata retired + gated EGL readiness probe). U2 DONE-measured:
kwin schedule latency ~96% of frame-callback waits; async-present slice
implemented+reviewed+A/B'd -> MECHANISM WIN (flip ioctl 24ms->0.65ms, no
tearing) but RESPONSIVENESS NULL (cadence/konsole_wait/hover flat, n=2) =
kill criterion; the ~33-67ms cadence is NOT present-cost bound. Residual
bottleneck is INSIDE prebuilt kwin 5.27's RenderLoop scheduling +
damage-arrival behavior (kwin unpatched: prebuilt binary, no ports build).
Kernel-side leverage on M4 is now essentially exhausted: four mechanically
proven slices (neg-dcache, vblank pacing, async cursor, async present) all
metric-null. Remaining M4 ideas are framework-level (kwin 6.x with AMS via
U5's atomic events; or session-level compositor alternatives) — outside
current guardrails. U3 DONE (dlopen 64->28, gated). U4 DONE (engagement
proven, no hover win; shim repo-owned + EVTREAD fixed). U5 DONE (kernel +
tests; guest matrix pending drmiftest validator refresh — enumerated).
U6 DONE (H2: two pre-existing umount/mount bugs found+fixed by the
reducer; dcache substrate correct). U7 CLOSED-SUPERSEDED by async-present
(id-0-withheld convoy now bounded to one deferred event; per-context sync
budget survives as orthogonal robustness item). U8 DONE-measured 2026-07-08:
M8 idle host CPU = 13.7% mean on a real-GL boot with N5 gates default-ON
(first VALID reading; goal <100% MET with wide margin; far below the 85-130%
historical band = the N5 poll-notify full-wait payoff). U9-U11 parked per
recorded conditions. U12 (push, ~9 commits ahead) awaits explicit user
approval.

## Landed opt-in gates (all default-OFF unless noted)

- `vfs_neg_dcache=1` — negative-dcache honor; -95% ENOENT, no M4 effect.
  Correct VFS fix (consumer bug + bump-inside-lock race); see U6 before
  default. Kernel 0cf817d + sentinel fix.
- `virtio_gpu_vblank_paced_flip=1` — flip-complete events paced to the
  synthetic vblank edge; engagement proven; no gap/first-frame win; small
  warm cost (~+250ms/launch). Vsync-semantics infrastructure. 1661795.
- `virtio_gpu_present_clock_60hz=1` — COMMITTED (kernel fcb0ebe), A/B MEASURED
  2026-07-09 (SUB-THRESHOLD, stays default-OFF). Requires+composes with
  async_present; delivers the async flip-complete on a free-running phase-locked
  60Hz grid (decoupled from real completion time) to give kwin a stable vsync
  clock — targeted the M7 ceiling. No-op when async off (sync path byte-identical).
  Supersedes vblank_paced completion timing when both set. RESULT: engaged (banner
  + events_total=913/snap_total=58) and paced, but presentedFPS only 52.3->53.4
  (+2.1%, goal >=55 NOT met) => clock is a MINOR real contributor, not the M7
  limiter; residual ceiling reassigned to host/kwin present throughput. See
  U7-RESIDUAL A/B VERDICT.
- `virtio_gpu_async_cursor=1` — cursor image uploads off the compositor
  thread (17-36ms/upload); correctness GO (R9 probe PASS). PERF VERDICT DONE
  (U4 2026-07-08): engagement PROVEN via kwin ioctl shim (CURSOR2 upload max
  27-32ms -> 0.8-8.5ms), but NO hover win at n=2 vs n=2 clean (medians overlap,
  within noise) — upload cost is not on the hover critical path. Keep opt-in;
  no default-on justification. 1661795.
- `XV6_ROOTFS_LDSOCACHE=1` (make-rootfs) — bakes multiarch ld.so.conf +
  ld.so.cache (1352 entries); -17..23% openat, -36..51% ENOENT, no M4
  effect. Parity-with-Linux cleanliness candidate.
- `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` / `KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`
  — repo-reproducible SONAME-stub A/B fixtures; NOT responsiveness wins.
- `KDE_APP_LAUNCH_PROBE_QT_MINIMAL_IMAGEFORMATS=1` (smoke expect) — prunes
  18 exotic kimg_* imageformats plugins from the per-run fs.img copy;
  konsole dlopen 64->28, ~55-67ms serial dlopen cut, svg/jpeg/ico/gif kept,
  icons intact (U3 A/B 2026-07-08).
- Default-ON (validated): ordered pageflip, poll-notify full-wait (both
  gates, post evdev/pipe/inotify/timerfd producer fixes), TSC jiffies,
  kernel_preempt, console_async, ring depth 60, total-reaper redesign.

## Instruments

- `hoverprobe` (`user/programs/hoverprobe/hoverprobe.c`, repo-owned guest
  tool, DONE + baselined 2026-07-09) — accurate end-to-end Plasma
  responsiveness instrument. ONE guest process injects pointer input
  (/dev/mouse absolute16, guest-only policy, same struct mouse_event as
  /bin/mouseinject) AND polls a SMALL region of interest via
  FB_GPU_SCANOUT_READ (fbstat's sample-current primitive) in a tight loop, so
  t_inject and t_first_change share one CLOCK_MONOTONIC. Times hover_in,
  hover_out, click from the pre-injection timestamp to the first readback whose
  ROI FNV hash differs from the reference captured just before injection
  (reference = un-hovered desktop for hover_in, hovered state for hover_out,
  hovered-not-pressed for click). ROI derived in pixels from the icon abs16
  coord using the harness mapping; centred, screen-clamped. The DRM cursor
  plane is NOT composited into the primary scanout the ioctl reads, so the ROI
  reflects highlight/press feedback, not the cursor sprite. Args (all optional,
  default taskbar-left target 11000,64200 / neutral centre 32768,32768):
  `hoverprobe [icon_x icon_y away_x away_y iters rw rh settle_ms timeout_ms
  calib_samples]`. Emits one line/event/iter
  (`hoverprobe event=.. t_inject_ms=.. t_first_change_ms=.. latency_ms=..
  sampler_period_ms=.. samples=.. result=CHANGED|TIMEOUT rect=..`), a
  per-event median/min/max summary, and a sampler_calibration line reporting
  the measured readback resolution. Compile-proof clean under the production
  user command (-Wall -Wextra). Harness hook: `KDE_SMOKE_HOVERPROBE=1` runs it
  in the desktop-interaction-latency reducer right after taskbar-settle (new
  `run_hoverprobe_instrument` + `hoverprobe` guest-script kind, archived via
  the interaction-helper capture); `KDE_SMOKE_HOVERPROBE_ONLY=1` finishes right
  after (lean focused run); knobs KDE_SMOKE_HOVERPROBE_{ITERS,RW,RH,SETTLE_MS,
  TIMEOUT_MS,AWAY_X,AWAY_Y,CALIB} + icon via
  KDE_APP_LAUNCH_PROBE_KPROFILE_HOVER_X/Y. Default OFF = byte-identical (no
  flip). MEASURED RESOLUTION: ~4-5ms per small-rect readback at idle (goal
  <=5ms MET on the calibration floor), inflating to ~13-16ms while kwin
  actively composites (scanout-read ioctl serialises behind kwin's synchronous
  present). Baselines: `kde-plasma-desktop-smoke-history/
  20260709T162636Z-hoverprobe-instrument-baseline` (chromium-launcher target;
  click 4/6, 2 timeouts because launching chromium does not idempotently
  repaint the ROI) and `...20260709T162836Z-hoverprobe-launcher-clean-click`
  (Kickoff-launcher target; click 6/6 clean). See M10. FINDING: the true
  hover-in latency is ~205ms, not the ~450-490ms the retired coarse
  framebuffer-diff sampler reported (that floor was the SAMPLER's serial
  round-trip, not the desktop) — this instrument resolves it to ~4-5ms.
  MENU-MODE EXTENSION (2026-07-09, U-KICKOFF): args 11-17 add a SECOND ROI over
  the popup BODY (distinct from the click target) + a click-launcher->open /
  click-empty-desktop->close protocol with per-iter cold/warm labelling, an
  optional prewarm pass (arg 17), and a skip-hover flag (arg 16, so iter1 is a
  genuine COLD Kickoff activation). New env knobs KDE_SMOKE_HOVERPROBE_MENU_PX_X/
  _PX_Y/_RW/_RH/_ITERS + _DO_HOVER + _PREWARM (all default 0/1 = byte-identical
  when menu ROI unset). Emits event=menu_open/menu_close lines with
  temperature=first|repeat + a menu_prewarm line; run_hoverprobe_instrument
  also pulls a full-frame proof PPM (/kde-plasma-menu-open-proof.ppm) captured
  while the menu is open. Compile-clean -Wall -Wextra. CAUTION: the hover/click
  phase (do_hover=1) CLICKS the same launcher and thereby PREWARMS Kickoff, so
  menu iter1 is only cold with _DO_HOVER=0. Staged into fs.img via debugfs
  (native ELF at sysroot/bin/hoverprobe rebuilt with build_host_user_program
  flags) — no rootfs rebuild needed.
  TOOLTIP-PROTOCOL EXTENSION (2026-07-09, U-TOOLTIP): arg 18 menu_protocol
  (0=click default byte-identical, 1=hover) reuses the menu ROI as a TIP ROI
  above a taskbar icon: OPEN = move onto the icon (no click) -> first tip-ROI
  change (tooltip appears; show-delay + QML + kwin schedule), CLOSE = move
  away -> ROI reverts (hide); args 19,20 = optional icon B abs16 for an A->B
  SWITCH phase (hover A until tooltip up, move straight to B while the dialog
  is visible; per-icon cold + visible-path timer). Emits event=tooltip_open/
  tooltip_close/tooltip_switch with temperature=first|repeat and summaries
  with median/p90/min/max/STDDEV (variance is the deliverable); dumps
  /kde-plasma-tooltip-proof.ppm on iter 1. Env knobs
  KDE_SMOKE_HOVERPROBE_MENU_PROTOCOL/_ICON2_X/_ICON2_Y; helper timeout
  240s->480s; use _DO_HOVER=0 so iter 1 is the genuine first tooltip of the
  session. Compile-clean -Wall -Wextra; staged into fs.img via debugfs.
  Companion gated config stage: `KDE_SMOKE_PLASMA_TOOLTIP_DELAY_MS=<ms>` writes
  /etc/xdg/plasmarc [PlasmaToolTips] Delay into the per-run fs.img copy
  (system-cascade override; Delay<=0 would disable tooltips and is rejected).

## Operational cautions (bite-you-again class)

1. Harness hardcodes the kernel at `kde-plasma-desktop-smoke.expect:6`
   (`$build/kernel/build/kernel/xv6.bin`) and IGNORES `KERNEL` env — bisect
   by sha-verified swap/restore of that file.
2. `qtquick-accelerated-path-policy` FAIL streak right after an fs.img
   rebuild = the image-layout EGL-init race signature (plasmashell dri2
   fail x4 + "EGL display 3001", kwin GL fine). Rebuild the image again to
   confirm; do NOT chase kernels or the host. NOT a metadata problem
   (U1 kernel part done; sysfs metadata always present) — durable fix is
   the EGL-init retry/readiness gate in the session launcher (U1 residual).
3. Stale-fbstat ABI hazard: FB_GPU_GET_STATS copies out sizeof(kernel
   struct); guest tools older than the header get overflowed in plain-stats
   mode. `fbstat sample-current` is safe. Rebuild guest tools with the
   image before fbstat-stats lanes. Stats struct is append-only ABI.
4. kprofile validity: timeout_hit=0 (except browser), userpc dropped=0,
   real-GL proof, booted cmdline grep, active sample; single-run *_ms
   deltas are ±15% (N=1 "wins" have burned us twice: QtMM cold, neg-dcache
   cold); guest drops ~14% BSP ticks under load (N7 fixed get_jiffs; don't
   compare raw jiffies across that boundary).
5. Failure Mode 19 (visible/artifact/prompt-sync timeout, black fb, zero
   crash markers): archive + rerun once. ALWAYS grep run.log for
   PANIC/IPI_REASON_CRASH/tq_remove first — labels can mask panics.
6. Injected-input batteries do NOT prove interactive responsiveness
   (FM 24a); wakeup-semantics changes need an interactive check.
7. Serial console: ~55-60 char truncation, no globbing in guest /bin/sh,
   absolute paths for kprofile exec, markers via variable
   (`M=XX; cmd; echo ${M}RES=$?`). ADDENDUM 2026-07-09 (cost 4 boots): the
   guest shell FLUSHES TYPEAHEAD around prompt redraw — input sent while
   it is printing is silently EATEN; only send from a proven-idle prompt
   (flush expect buffer, send \r, require a NEW prompt). Kernel printf
   floods (e.g. dcachetest's vfs_unmount EBUSY-retry lines) byte-interleave
   with shell echo, so live sentinel matching fails: write test rc's to a
   guest file and poll it with `cat` from idle prompts instead.
8. One VM lane at a time; `pgrep -af qemu-system` before and after; never
   trailing `&`; delete scratch images; no lingering QEMU.
9. Subagent run-workers must WAIT SYNCHRONOUSLY on their own runs (bounded
   poll loops) — monitors that outlive the turn cause premature yields.
10. The full failure-mode list (24 items) and diagnostic-knob inventory are
    preserved in the history file — consult before declaring any gate
    failed.

## Guardrails (binding)

- NO un-gated default flips. A default changes only with same-session A/B
  plus explicit-off control, a plan entry, and the regression battery (KDE
  active-sample, Chromium launch-only, M4/M5/M8 within noise). New failure
  class => revert first.
- CODE-FIRST / OFFLINE-FIRST / BATCH GATES / REUSE BOOTED VMS (<=2 boots
  per slice); adversarial review before boot for kernel changes; boot
  before believing (reviews have both caught blockers AND missed one only
  a boot found).
- ORCHESTRATE: decompose into subagent jobs; subagents never flip defaults
  or push.
- Commit at verified checkpoints (repo + submodules deepest-first); NEVER
  push without explicit user approval.
- Closed lanes — do not reopen without new evidence: scheduler wake-to-run,
  futex drift, poll/kqueue/AF_UNIX/eventfd/pipe primitives, guest cursor
  upload, raw PTY setup, tiny Wayland frame delivery, D-Bus/eventfd
  readiness, renderer admission, inotify FIONREAD, PCID/noflush (needs
  INVPCID), loader/ELF surgery, single-library stub trims as M4 levers.

## Scoreboard (2026-07-08)

| # | Metric | Current | Goal |
|---|--------|---------|------|
| M1 | YouTube-freeze survival | 3/3 clean; P0 closed. UPGRADE 2026-07-09: YouTube not only survives but PLAYS -- with `--disable-audio-output` (gated, see M7) the watch page decodes VP9 video at ~28-29fps (visual proof: player shows Big Buck Bunny frames). Prior "video never plays / 0.02fps" was an audio-renderer-init failure stalling the combined pipeline, not a freeze. | hold |
| M2 | getpid_ns | 1.65-1.94us | <1.5us (U11) |
| M3 | tlb_amplification 1024pg | 2.7-3.7us | 0 |
| M4 | konsole_wait_ms | warm 1184-1205ms, cold 1406-1500ms (2026-07-08 clean guiperf N=4, all gates green); bottleneck LOCALIZED (U2): kwin damage->repaint SCHEDULE latency = ~96% of each 58-139ms frame-callback wait, konsole render ~0%, kwin repaint ioctl ~4%; imageformats prune opt-in (U3 DONE, ~55-67ms dlopen cut) | Linux-like (~0.3s) |
| M5 | first_visible_ms | ~6.2-6.6s (clean guiperf N=4) | <15000 |
| M6 | mesakmsgl FPS | 118-125 (ordered default); ~60 by design under vblank-paced gate | done |
| M7 | presentedFPS | CLOCK-60 A/B MEASURED 2026-07-09 (chromium-video kprofile 90s, complete windows advanced~15, real virgl/D3D12 GL both arms, crash 0, FM17 launch-evidence-missing bookkeeping expected): CONTROL (async-present) presentedFPS=52.3 dropPct=6.50; TREATMENT (+virtio_gpu_present_clock_60hz=1) presentedFPS=53.4 dropPct=6.22. Clock ENGAGED (run.log banner "present-clock 60Hz engaged", both tokens x3 in cmdline) and PACES (fbstat present_clock60 events_total=913==all session flips, snap_total=58 = 6.4% << events => async worker keeps up with the 60Hz grid). MOVE IS DIRECTIONAL BUT SUB-THRESHOLD: +1.1fps (+2.1%), dropPct -0.28; goal >=55 NOT met and FPS did NOT rise toward 60. Because snap<<events (worker feeds a clean 60Hz grid) yet present still caps ~53, the U7-RESIDUAL prediction (clean completion clock => ~60fps) is REFUTED in magnitude: dominant M7 ceiling is DOWNSTREAM host/kwin present throughput, NOT the completion-clock timestamp. Gate `virtio_gpu_present_clock_60hz=1` stays default-OFF (small real win, sub-threshold). Prior 51.2/51.0 async-present A/B superseded by this pushed-kernel re-read (52.3/53.4). PIVOT: host-retire/present-throughput. YOUTUBE vs LOCAL DELTA (2026-07-09, manual KDE/kwin lane WITH net, 60fps video aqz-KE-bpKQ, real virgl GL, DHCP up IP 10.0.2.15, crash 0, 2 boots reproducible): the chromium-video kprofile harness CANNOT do YouTube (it hard-sets QEMU_NET=0 at smoke.expect:672, and PERF-VIDEO RESULT is emitted only by the local perf-video.html JS, not a real YouTube page) -> drove wayland-chromium manually from serial and measured presented fps = delta(fbstat kms_page_flip_events)/wall-time. RESULT: YouTube presented ~0.02 fps in two 60s steady windows (t0/t1/t2 flips 52/53/54; idle desktop ~0) vs LOCAL FILE 52.3-53.4 fps -- i.e. YouTube video does NOT reach sustained video-rate playback here (compositor presents ~1 frame/min; page loads then goes near-static). decodedFPS/dropPct N/A for YouTube (no JS injection; Stats-for-nerds too fragile). Exact blocker NOT isolated (chromium media log unrecoverable: guest `/` writes don't persist to image, tmpfs-log serial dump failed under load; GL-display monitor screendump yields no PPM) -- candidates: codec (VP9/AV1) / autoplay-not-triggered / buffering stall / consent wall. Consistent with the 2026-07-03 M1 replays which proved YouTube LIVENESS (no freeze) but never smooth decode. Archive: build-x86_64/chromium-youtube-m7/20260709T211902Z-yt-presentfps/ (ARCHIVE-NOTE.txt + driver snapshot scripts/gpu/chromium-youtube-presentfps.expect). YOUTUBE BLOCKER ISOLATED + FIXED 2026-07-09 (diagnosis worker, 3 boots, real KVM+virgl GL, DHCP up IP 10.0.2.15, crash 0, no lingering qemu): the ~0.02fps is NOT codec/autoplay/consent/network. OFFLINE codec inventory: the shipped chrome is "Chrome for Testing" v150.0.7871.24 with ALL decoders statically linked (dav1d/AV1, libvpx/VP9, ffmpeg/H.264+AAC, opus, vorbis) -- codec present, ELIMINATED. VISUAL truth via `fbstat ppm-current` (reads the GL scanout; monitor screendump is blank on virtio-vga-gl -- that is the prior worker's "no PPM"): the full YouTube watch page RENDERS (title "Big Buck Bunny 60fps 4K", sidebar thumbnails, metadata, Sign in) so NOT a consent wall; media CDN reached (console log rr4---sn-gvbxgn-tvf6.googlevideo.com) so NOT network -- but the PLAYER REGION IS PURE BLACK. ROOT CAUSE (chromium stderr, recovered from ROOT /host-gui-wayland-chromium.log which DOES persist -- prior worker only tried tmpfs /dev/shm+/tmp): PipelineStatus::AUDIO_RENDERER_ERROR x8 + media/audio/pulse/pulse_util.cc:395 "pa_operation is nullptr" flooding, and ZERO video/codec/demuxer errors. This guest has no working chromium audio backend; YouTube's element carries an Opus audio track, so the audio renderer fails to init and Chromium's RendererImpl fails the WHOLE combined audio+video pipeline -> video never decodes -> black player 0.02fps. The local baseline plays because perf-1280x800-60fps.mp4 is VIDEO-ONLY (ffprobe: single h264 stream, no audio) so no audio renderer is built (&mute=1 does NOT help -- mute still builds the renderer). FIX (in-stack, NO build gap): `--disable-audio-output` routes audio to a guaranteed-init null sink so the pipeline proceeds. VALIDATED end-to-end: argv_31=--disable-audio-output, AUDIO_RENDERER_ERROR 0, pulse-nullptr 0, no video errors, presented 28.88/28.46 fps over two 57s windows, and the player VISUALLY shows Big Buck Bunny frames (vs black). ~28-29fps (SW VP9 720p60, single-process --disable-gpu, no HW video decode) is genuine playback ~1400x the broken 0.02 and below local 52-53 (audio-less H.264, lighter SW decode) -- the residual video<->local gap is DECODE-COST (SW VP9 vs SW H.264), NOT a blocker. IMPLEMENTED GATED (default OFF, no default flip, NOT committed): scripts/image/wayland-chromium-launcher.c reads env WAYLAND_CHROMIUM_DISABLE_AUDIO_OUTPUT=1 -> appends --disable-audio-output (byte-identical when unset; emits the same argv the validated boot injected via WAYLAND_CHROMIUM_EXTRA_FLAGS); compile-clean cc -O2 -Wall -Wextra. Promotion note: real audio (pulse/ALSA sink for chromium) is a separate larger effort; until then this gate is the YouTube-playback lever. Archives: build-x86_64/chromium-youtube-m7/20260709T232300Z-yt-fix-disable-audio-output/ (FIXED: video plays + ARCHIVE-NOTE + driver snapshot) and ...T231147Z-yt-visual-evidence-b2/ (BROKEN repro: black-player shot + audio-error log). YOUTUBE 28.9 vs LOCAL 52-53 GAP — OFFLINE ATTRIBUTION 2026-07-09 (analysis worker, opus; VM lane owned by audio worker's kde_audio_null_sink boot so boots SPECCED not run): the gap is CLIENT-SIDE FRAME-SUPPLY bound, NOT compositor/present bound. Decisive prior-data argument: the present path demonstrably sustains 51-53 fps (local H.264 file, decodedFPS 62 > presentedFPS 51-53 = present is the ceiling); YouTube's 28.9 sits BELOW that proven ceiling, so the compositor is STARVED of frames — the limiter is upstream frame production (decode -> in-process software-GL composite -> wayland commit), not kwin/present (consistent with the async-present + clock-60 nulls: present cost never gated FPS). Within frame-supply the dominant suspect is SOFTWARE VP9 decode as run by chromium's bundled libvpx in the constrained launch config: wayland-chromium-launcher.c:211-215 forces `--single-process --disable-gpu --in-process-gpu --no-zygote`, so libvpx VP9 decode threads share ONE process scheduler with software-GL compositing + rasterization + (on YouTube) the DASH/MSE/JS main thread, and chromium's libvpx VP9 threading is further capped by the stream's tile-column count (~1-2 for 720p). CAVEAT (honest): raw VP9 720p decode is NOT intrinsically slow on this host class (host ffmpeg native-vp9 decodes 720p60 at >300 fps single-thread on the same i7-12650H), so the effective ~29 fps loss is more likely single-process CPU contention + chromium thread/tile caps than pure codec math — the codec-vs-page split needs one boot to quantify. No decodedFPS was EVER captured for YouTube (getVideoPlaybackQuality needs JS injection; no --vmodule media log was set), which is why the split was never nailed. LEVERS (spec'd, assets built, ready-made gates found — see build-x86_64/chromium-youtube-m7/decode-attribution-assets/SPEC.md): (BOOT1, DECISIVE) built controlled 1280x720@60 video-only VP9 (.webm) + H.264 (.mp4) from the same source clip; run BOTH through chromium via perf-video.html?asset=... (KDE_CHROMIUM_URL; harness accepts custom asset, logs a benign limitation but runs) — it reports decodedFPS AND presentedFPS with NO network/audio/MSE confounders, cleanly attributing the gap to codec-decode vs page-overhead. (BOOT2, LEVER, no code change) env WAYLAND_CHROMIUM_MULTIPROCESS=1 (launcher.c:211) DROPS --single-process/--disable-gpu/--in-process-gpu/--no-zygote => tests whether killing single-process contention lifts YouTube toward 52; RISK: multiprocess may destabilize virgl/software-GL bring-up (that is WHY single-process is default) — exploratory, roll back on GL/crash regression. Also add WAYLAND_CHROMIUM_EXTRA_FLAGS="--vmodule=*/media/*=2" to persist decoder+per-frame timing to ROOT /host-gui-wayland-chromium.log. (BOOT3, cheap confirm) YouTube vq=medium(480p) vs hd720 — if 480p jumps toward 52, decode-CPU-bound confirmed by the resolution knob alone. LEVER (a) prefer-H.264-on-YouTube HONEST VERDICT: there is NO Chromium CLI flag that forces avc1 — YouTube picks codec from MediaSource.isTypeSupported()/MediaCapabilities; only h264ify-style JS override (reject webm/vp9) works, but --single-process/--no-zygote loads no extensions so --load-extension is unreliable (adding a JS-injection path = build change). Flags that do NOT work: --disable-accelerated-video-decode (no codec effect), "--disable-features=VP9*" (VP9 is a built-in codec, not a Feature toggle). itag 298 (720p60 H.264) exists for aqz-KE-bpKQ, so IF forced, H.264 720p60 is available and should match ~52. VERDICT/BEST LEVER: binding constraint = client-side VP9 decode + in-process contention (NOT kwin/present, NOT a blocker — 28.9 fps is genuine playback). Best DIAGNOSTIC lever = BOOT1 local-VP9-vs-H.264 A/B (decisive, offline-buildable, prepared). Best PRODUCT lever depends on BOOT1: if VP9 decodes ~52 locally -> WAYLAND_CHROMIUM_MULTIPROCESS is the win (page/contention was the cost); if VP9 decodes ~29 locally -> codec is the cost and only H.264-forcing (JS-injection, build change) or lower-res recovers it. Assets+SPEC: build-x86_64/chromium-youtube-m7/decode-attribution-assets/ (perf-vp9-1280x720-60fps.webm sha 7241079b..., perf-h264-1280x720-60fps.mp4 sha 9d1476b6..., SPEC.md w/ injection + all 3 boot recipes). | >=55 (U7) |
| M8 | idle host CPU | 13.7% mean (13.7/13.6/13.9, 3x10s /proc/stat delta) on a real-GL boot, N5 gates default-ON — FIRST VALID reading 2026-07-08 (U8); far below the 85-130% historical band = N5 poll-notify payoff | <100% (MET) |
| M9 | Chromium visible | PASS guard | hold |
| M11 | Kickoff (start-menu) open/close latency (hoverprobe menu-mode, menu-body ROI, in-process CLOCK_MONOTONIC) | BASELINE + PREWARM A/B 2026-07-09 (U-KICKOFF, real virgl/D3D12 GL, qtquick PASS, crash 0, n=2/arm, full-frame PPM confirms genuine Kickoff open). USER PAIN REPRODUCED + DECOMPOSED: COLD first-open (never opened in session) = 2254/1892ms (matches the reported 1394-2517ms); WARM repeat-open (same session) = median ~400-500ms (340-670ms); menu CLOSE ~180-420ms. Cold excess ~1500-1850ms is a ONE-TIME-per-session cost = QML component compile + Kickoff app/recents model population (KIO/DBus), NOT compositor cadence (kwin ioctl-trace: compositor mostly idle during the cold open, PAGE_FLIP dur ~5ms). Warm ~400ms floor = U2 kwin damage->repaint SCHEDULE latency + fade frames (known-hard, do not relitigate). ATTACK — session-start PREWARM (gated, open Kickoff once at login, wait for model build, close): user's first open 2254/1892 -> 498/540ms (-75%, ~1550ms), into the warm band. Prewarm one-time cost ~2000ms real, paid at session start (tunable wait). Gate default OFF; product promotion path below. Old "open 1394-2517ms / close 415-963ms" from the retired coarse full-frame sampler is partly artifact (same lesson as M10 hover) BUT the cold-open pain is REAL (~2s) and prewarm is the lever. PRODUCT-PREWARM BOOT A/B 2026-07-09 (U-KICKOFF, gate kde_kickoff_prewarm=1 in kde-plasma-session-child, hoverprobe _DO_HOVER=0 _PREWARM=0 so iter1=user's first real click, real virgl/D3D12 GL, qtquick PASS, crash 0, no lingering qemu, 3 boots): gate ON user first-open=493ms (worker DONE opened=1 pristine=1, pristine screenshot verified) vs gate OFF cold=2164ms same config = -77% (~1670ms), matching the test-tool prewarm A/B (498/540ms). Gate default OFF; promotion-to-default-on pending standard battery + owner sign-off. | approach Linux VM |
| M12 | Taskbar tooltip latency (hoverprobe tooltip protocol: hover -> first tip-ROI pixel; median/p90/max/stddev, n=12/boot) | BASELINE + DECOMPOSITION + GATED FIX A/B 2026-07-09 (U-TOOLTIP, real virgl/D3D12 GL, qtquick PASS, crash 0, 7 boots, 0 FM19). BASELINE (Delay=700 default): open med 948.5/942.9 p90 973.3/1005.8 max 1092.6/1123.7 sd 50.3/58.5 ms; close ~262; A->B switch ~233. DECOMPOSED: 700 ms plasmarc [PlasmaToolTips] Delay compiled default (74%, binary-verified in libcorebindingsplugin.so; image ships no plasmarc) + ~250 ms render (U2 kwin schedule wall) + first-of-session QML cold cost (the JITTER source: hidden at Delay=700, exposed at Delay=50: first 994/2331 vs ~470 repeats). FIX gated default-OFF: kde_plasma_tooltip_delay=50 + kde_tooltip_prewarm=1 -> open med 486.1/482.2 (-49%), p90 541.1/530.3 (-46%), max 606.7/747.1 (worst-case -68% vs delay-only 2331), first == repeats, sd 44.4/79.2; switch ~110. Prewarm-only at Delay=700 NULL (700ms delay masks cold cost). Residual ~480 ms = U2 kwin damage->repaint schedule floor (do not relitigate). Promotion pending battery + sign-off. Archive 20260709T205408Z-tooltip-ab-summary. | approach Linux VM |
| M10 | hoverprobe latency (input-inject -> first ROI pixel change, in-process shared CLOCK_MONOTONIC) | NEW BASELINE 2026-07-09 (U-HP, real KVM+virgl/D3D12 GL, N=6/event, crash 0): hover_in median ~205-234ms (min 176-189), hover_out median ~193-205ms (min 149-159), click median ~250-312ms (launcher-toggle arm 6/6 clean min 24ms). Sampler resolution (256 idle small-rect readbacks) mean 5.5-6.4ms, min 4.0-4.4ms, max ~19-23ms (per-sample inflates to ~13-16ms while kwin composites — scanout-read ioctl serialises behind kwin's synchronous present). SUPERSEDES the retired coarse framebuffer-diff hover metric whose ~448-490ms floor "measured the sampler, not the desktop": true hover-in is ~2x faster (~205ms) at ~4-5ms resolution. CLOCK-60 CHECK 2026-07-09 (hoverprobe under M7 treatment tokens async-present+present_clock_60hz, banner engaged, real GL, crash 0, N=6): hover_in median 203.5ms (185.5-401.4), hover_out 157.8ms (147.5-194.9), click 284.0ms (269.9-286.9, 4/6). WITHIN NOISE of baseline — no clear interaction-latency win from the 60Hz clock; consistent with U2 (hover/click latency dominated by kwin damage->repaint SCHEDULE latency ~96%, not present cadence). | approach Linux VM |

Fork-safety gate for any syscall/scheduler/TLB/mm change: forktest (rc=1
exhaustion signature OK), clonetest rc=0, cowtest rc=0, same boot.

---

# A1 frame-supply/EGL/media-probe history displaced on 2026-07-10

The following A1 lane narrative was moved verbatim from the live
`docs/active-work-plan.md` during compact-plan maintenance. The live plan
retains only the current verdict, decisive evidence, and immediate next step.

- A1 (EGL init cleared; multiprocess metric-null; frame-supply forensics next):
  spawn ownership and debugcon freshness are REPAIRED; codec is NOT the
  constraint. Refreshed fs.img `21fd6df...` contains launcher `a52dc3a...` with
  both gate strings verified. The runner now has explicit fail-closed arms,
  treatment live-GPU-process proof, EGL/software rejection, guest PPMs,
  crash-aware artifacts, and exact cleanup. Existing multiprocess b2 remains
  diagnostic only: GPU-process EGL_BAD_ATTRIBUTE/no-config failure was
  confounded by missing audio-disable. First approved C1 attempt was a
  pre-boot harness null (0 boots): debugfs helper staging returned rc=0 with
  its normal stderr banner/allocation output, but Tcl `exec` catch classified
  it as FAIL; artifact
  `build-x86_64/chromium-youtube-m7/20260710T040814Z-a1-c1-mp0-audio1`, QEMU
  none. Staging now handles normal debugfs stderr with exit status preserved,
  verifies helper bytes/mode/type, and passes its reducers; independently
  resume-approved with 0 boots consumed. Actual C1 boot #1 reached clean
  KVM/virgl + KDE, then stopped before Chromium because runner Tcl evaluated
  regex `[0-9]` as command substitution. Artifact
  `build-x86_64/chromium-youtube-m7/20260710T041712Z-a1-c1-mp0-audio1`;
  crash grep clean, QEMU none, 1 boot consumed. T1 was not run; slice closed.
  All Tcl `-re` sites are now audited; numeric and nonnumeric marker reducers
  pass. Fresh C1 then stopped deterministically before Chromium because fbstat
  emitted `virgl_bo_presents 4` while the parser accepts only `bo_presents`;
  artifact
  `build-x86_64/chromium-youtube-m7/20260710T042512Z-a1-c1-mp0-audio1`.
  KVM/virgl/OpenGL proofs pass; crash grep clean, QEMU none, 1 boot consumed
  in this slice. T1 was not run; slice closed. Parser now accepts exact current
  C1/producer keys `kms_page_flip_events` + `virgl_bo_presents`; missing,
  conflicting, and stale-key reducers pass, independent review approves
  resume, and QEMU remains none. Fresh C1 then completed both 60s windows on
  real KVM/virgl but is DIAGNOSTIC-ONLY/NULL: 28.81/30.14 presented fps (BO
  rates equal), player deltas 178515/178513; artifact
  `build-x86_64/chromium-youtube-m7/20260710T043626Z-a1-c1-mp0-audio1`.
  Final fail-closed artifact validation found `/chrome_debug.log` absent: the
  launcher passes `--enable-logging=stderr` and redirects child stderr into
  authoritative `/host-gui-wayland-chromium.log`, so the separately required
  file cannot be produced by this launch contract. Crash grep clean, owned
  process group and exact QEMU none; 1 boot consumed, T1 skipped, slice
  closed. Launcher-log contract is now repaired and independently RESUME
  APPROVED: the canonical launcher log is required and must prove redirect,
  start, argv, and child-exec; `/chrome_debug.log` and launch-stdio are optional
  and gated when present, while crash/GPU/software marker scans remain armed
  across retained logs. Reducers pass and QEMU remains none. The next fresh
  C1 returned code 13/EOF before root and is a pre-boot NULL: its apparent
  debugcon was stale, byte-identical with the same mtime as the prior `043626Z`
  artifact, so QEMU never opened a fresh log. Artifact
  `build-x86_64/chromium-youtube-m7/20260710T045013Z-a1-c1-mp0-audio1`;
  crash grep clean, owned process group and exact QEMU none, T1 skipped.
  Spawn ownership and debugcon freshness are now fixed and independently
  RESUME APPROVED: direct PID=PGID=SID spawn, synchronous reap plus owned
  cleanup, and stale/missing/empty debugcon rejection. Reducers pass and exact
  QEMU is none. Fresh C1 then completed both windows but is again
  DIAGNOSTIC-ONLY/NULL: flips imply 28.67/29.54fps and player deltas were
  178514/178465; artifact
  `build-x86_64/chromium-youtube-m7/20260710T050439Z-a1-c1-mp0-audio1`.
  KVM/virgl/OpenGL, control argv, audio-disable/canonical-log, crash,
  fresh-debugcon, owned-cleanup, and exact-QEMU gates were clean. Final t2
  rejected a one-count admission-vs-success drift within one locked stats
  snapshot (`bo_presents 6199` vs `virgl_bo_presents 6198`), so no canonical
  C1 metric; 1 boot consumed, T1 skipped, slice closed. Independent NO-BOOT
  review found the backend-authority parser still accepted duplicate same-value
  `virgl_bo_presents` rows although the producer emits exactly one. The fix is
  independently RESUME APPROVED: virgl requires exactly one
  backend-authoritative `virgl_bo_presents`; same-value and different-value
  duplicates both reject.
  The locked snapshot can legitimately show generic BO admission one ahead of
  backend success, so the backend success counter is authoritative. Exact C1
  audit and supported backends are unchanged; reducers pass and QEMU is none.
  Fresh C1 is now canonical PASS on real KVM/virgl: 28.59/30.21fps with
  player deltas 178515/178515; artifact
  `build-x86_64/chromium-youtube-m7/20260710T052459Z-a1-c1-mp0-audio1`.
  All argv/audio-disable, OpenGL, canonical-log, artifact, crash, fresh-debugcon,
  owned-cleanup, and exact-QEMU gates were clean, but this is N=1 only. T1 boot
  #2 stopped pre-Chromium and is NULL: an asynchronously interleaved/corrupted
  `opengl_submit_backend_separation_matrix` row triggered the fail-closed
  missing-proof gate, so there is no treatment metric; artifact
  `build-x86_64/chromium-youtube-m7/20260710T052928Z-a1-t1-mp1-audio1`.
  Crash grep and cleanup were clean and QEMU is none after two boots.
  Independent NO-BOOT rejected the first idle-resampling draft: returning the
  first retryable error could mask hard duplicate/conflict evidence later in
  the same buffer. The hard-precedence correction is now independently RESUME
  APPROVED: mixed hard/retryable and genuine single-row inconsistent reducers
  pass, the real T1 corruption classifies retryable, and the bounded one-shot
  resample policy is preserved. QEMU is none. Fresh T1/T2 are deterministic
  treatment NULLs before measurement; artifacts
  `build-x86_64/chromium-youtube-m7/20260710T054648Z-a1-t1-mp1-audio1`
  and `.../20260710T054803Z-a1-t2-mp1-audio1`. Both proved exact
  multiprocess argv (audio-disable present; single-process/disable-gpu/
  in-process-gpu/no-zygote absent), real KVM+virgl/OpenGL submit, fresh
  debugcon, and canonical logging. External GPU-process initialization then
  repeated EGL_BAD_ATTRIBUTE/no-config/CollectGraphicsInfo failure and exit
  (2 attempts T1, 3 T2), so live role windows, FPS, player deltas, and final
  frame/artifact gates have no evidence. Crash/software scans and cleanup were
  clean; exact QEMU is none. Canonical C1 remains N=1 at 28.59/30.21fps.
  Focused offline forensics now gives a VERY-HIGH-CONFIDENCE root cause: the
  current Mesa `fb2724503` lineage forked before and lost semantic fix
  `5e3f4bebe` (`merge-base --is-ancestor` rejects it). Current
  `eglcontext.c` validates `EGL_CONTEXT_OPENGL_NO_ERROR_KHR` inline while the
  context still has its default ES 1.0 version; Chromium orders NO_ERROR=true
  before its later CLIENT_VERSION=3/2 attributes, producing the exact repeated
  ES3+ES2 `EGL_BAD_ATTRIBUTE`. Historical `20260703T125656Z` traced
  `bad_attr=0x31b3`; applying `5e3f4bebe` immediately cleared EGL init in the
  `20260703T131336Z` follow-up. This is
  userspace pre-DRI parsing, not kernel/fd/permission/platform ABI. T1/T2 and
  current fs.img carry identical `/lib/libEGL.so.1.0.0` SHA-256
  `0e6bae97edd65392e8426b897ba86410c5518a891482c64c5796bf508d8ed040`
  (build ID `618e02ae805290e556d15c3b6a52c25a173a576a`). The minimal current-lineage
  fix is now implemented/staged: `eglcontext.c` records NO_ERROR while parsing,
  validates against the final client version with BAD_ATTRIBUTE-before-BAD_MATCH
  precedence, and leaves the WebGL/ANGLE block byte-identical. The exact 9-case
  reducer matrix (Chromium-order ES3/ES2, reverse controls, ES1 negatives, and
  debug/robust precedence) passes 9/9 against rebuilt Mesa EGL 1.5. Focused
  `port-mesa -j2` and narrow `rootfs-refresh -j2` pass. Sysroot and extracted
  image `/lib/libEGL.so.1.0.0` now hash
  `33d7c039f7e524e28a3828ab409b063aa49f69597d24f64f0b696e00a8b67e50`
  with build ID `f7a617e82cd93384b3ccb2a0cb1c26dddda2a1da`; refreshed fs.img is
  `4e2aeeb0613d8c63c8b713f2bbab265943f3cfa71423a50dcfe9b69e83e505f2`.
  Chromium's libEGL/libGLES links resolve to guest Mesa. Independent no-boot
  review BOOT APPROVED the minimal semantic/error-precedence diff, unchanged
  WebGL/ANGLE block, 9/9 matrix, build/stage hashes, and symlink chain; QEMU
  remained none. Post-fix T1/T2 are deterministic pre-measurement NULLs;
  artifacts
  `build-x86_64/chromium-youtube-m7/20260710T062229Z-a1-t1-post-egl-fix-mp1-audio1`
  and `.../20260710T062337Z-a1-t2-post-egl-fix-mp1-audio1`. Scratch extraction
  proves both boots used fixed libEGL `33d7c039...` (and launcher `a52dc3a...`).
  Exact multiprocess/audio-disable argv, real KVM+virgl/OpenGL, and no software
  fallback passed, but GPU init still emitted bad-attribute/no-config/
  CollectGraphicsInfo/exit counts 4/4/2/2 (T1) and 6/6/3/3 (T2). Failure was
  before semantic GPU-role windows, so FPS, BO rates, player deltas, and final
  frames are NULL. Canonical logs and fresh debugcon passed; non-GPU-init crash
  scans, owned cleanup, and exact-QEMU-none were clean. VERDICT: the 9/9 reducer
  plus semantic fix is insufficient and falsified as the complete runtime
  explanation. Do not run more FPS retries first; next is focused
  runtime-vs-reducer forensics of Chromium's actual EGL attribute list and
  loaded path/library. Focused NO-BOOT forensics confirms the retained logs
  cannot supply either fact: fixed image hash/symlink contents are NOT proof of
  the external GPU child's mapped provider because Chrome 150 dlopens EGL and
  the failed PIDs have no retained maps. Ranked hypotheses are: (1) a
  runtime-vs-reducer display/API/attribute mismatch, led by a concrete lineage
  delta — the historical external-GPU success used `EGL_PLATFORM=surfaceless`,
  `GALLIUM_DRIVER=virgl`, `MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu`, and explicit
  DRI/GBM paths, while post-fix T1/T2 leave these unset; (2) another EGL/Mesa
  object or namespace; (3) a different current context attribute, including an
  API/extension-dependent ANGLE attribute. Display/config failure is lower
  ranked because Chromium reports `eglCreateContext` BAD_ATTRIBUTE before the
  later no-config fallback. The required default-OFF arm is now
  implemented/staged in `chromium-youtube-presentfps.expect`,
  `chromium-egl-trace-preload.c`, and Mesa `eglcontext.c`:
  `YT_EGL_FORENSICS=1` is valid only with MP=1/audio=1, activates preload +
  Mesa gates only for that arm, stops after the GPU-init verdict, and emits no
  FPS claim. The preload caps EGL/provider rows at 32/PID and records first-call
  `dladdr`, mapped provider + filtered live maps, role/full argv/env, and exact
  display/init/bind/choose/create/getError attrs. Mesa is default-silent and
  logs failure-only rows capped at 16/PID with bound API, platform/version/
  extension gates, config/share, exact ordered attrs, first rejected attr,
  final parsed state, and error. The driver enumerates/extracts every per-PID
  trace plus canonical Mesa/launcher/dynamic-role evidence, resolves provider
  symlinks, and records provider SHA/build ID while preserving crash,
  fresh-debugcon, owned cleanup, and exact-QEMU gates. Focused `port-mesa`,
  preload/launcher/probe staging, narrow `rootfs-refresh`, deepest/top diff
  checks, regular+forensic/negative arm reducers, cap/provider/attr/artifact
  reducers, and an actual 32-row preload cap control pass; QEMU is none.
  Final fs.img is `bb5b004b90ea7759b09a19626151952f4c1163c04b25b1bd1f718c3ca71f5c87`;
  sysroot=image libEGL is `2649ab5656e15062555a2470913949e1381925af9a50099fe2203d38db2f4c45`
  (build ID `53a769ca2087120dc04464efb175c4d116f1774d`); build=image preload is
  `793ee618d18cc2f9b1210788a2452545a0790625a92983eb9f31f1c2d9cfcbf5`
  (build ID `1350acbdd78af81bb2594fffeeee18d780d79cfc`). Independent adversarial
  NO-BOOT review REJECTED boot: the preload conflated core
  `eglGetPlatformDisplay` (`EGLAttrib *`) with EXT
  `eglGetPlatformDisplayEXT` (`EGLint *`) and cross-falls back between
  ABI-incompatible signatures; several wrappers read output pointers after
  failed calls; mutex use is fork-unsafe, provider-budget admission races,
  filtered maps can truncate silently, and unavailable provider build IDs do
  not fail closed. All review blockers are now fixed and staged: core and EXT
  use distinct signatures/resolution/wrappers with no cross-ABI fallback;
  failed output calls log textual `unavailable` without dereference;
  PID-aware/atfork-reset state is child-safe; the provider row is reserved
  deterministically before concurrent EGL rows; `/proc/self/maps` streams by
  bounded lines with explicit per-line/total/truncation state; and the driver
  requires an even-length lowercase hexadecimal provider build ID (16..128
  digits), rejecting unavailable/nonhex/short/odd IDs. Exact no-boot reducers
  pass: direct plus `eglGetProcAddress` core `EGLAttrib` preserves
  `0x1122334455667788`, EXT `EGLint` preserves `0x55667788`; PROT_NONE poison
  outputs survive with all availability fields false; a child EGL call while
  the parent holds the diagnostic lock exits cleanly; a 64-thread race yields
  exactly 32 capped rows, one provider row reserved as row 1; 26k split VMAs
  report honest `maps_stream_status=truncated`, `maps_total_bytes=1048576`,
  `map_found=0`; and the default-OFF control emits no log. Forensic, regular
  MP=1, regular MP=0, and invalid-arm Tcl reducers pass/reject as intended.
  Narrow `rootfs-refresh` passes; build=overlay=extracted-image preload is
  `267828f658f73243e47a709aa84ddf51540dc7afe68e77f727a41d3b95057bcc`
  (build ID `944ce6249f85b2ce422a4baf59cc75773c27e887`); refreshed fs.img is
  `dfcde24e55a219f9960f8d52d80fea99a30c852c012bc810108034694c271da3`.
  Deepest/top diff checks pass and exact `/proc` QEMU count is zero.
  Independent adversarial re-review BOOT APPROVES the corrected arm: core/EXT
  ABI separation and failure-output poison safety are exact; fork reset,
  race/provider-row cap, honest maps truncation, and build-ID fail-closed tests
  pass; Mesa remains default-silent/failure-only; staging hashes above match
  the extracted image; and regular MP=0/MP=1 arms remain silent and isolated.
  Artifact extraction, crash/fresh-debugcon/owned-cleanup, and exact-QEMU gates
  remained green. Exactly one MP=1/audio=1 forensic boot ran; artifact
  `build-x86_64/chromium-youtube-m7/20260710T074122Z-a1-egl-forensic-boot`.
  Overall artifact validation is NULL because preload EGL/provider proof and
  provider artifacts are missing, but Mesa's failure-only trace is decisive:
  OpenGL ES is bound; a DRM-platform EGL 1.5 display is initialized; and the
  exact ES3 then ES2 lists (`0x3098=3/2,0x30fb=0,0x33ac=0,0x34f8=0,EGL_NONE`)
  both first reject `0x34f8=0`
  (`EGL_CONTEXT_HARDENED_ANGLE=FALSE`) with `EGL_BAD_ATTRIBUTE`. This proves a
  different runtime attribute and falsifies the old no-error explanation plus
  the immediate surfaceless/API-mismatch hypothesis. Provider identity remains
  unresolved: Chrome's handle-specific `dlopen`/`dlsym` bypassed preload
  interposition, while dynamic maps were empty and the guest manifest's `wc`
  invocations failed. The run makes no FPS claim; crash scan, fresh debugcon,
  owned cleanup/reap, and exact-QEMU-none were clean. Focused NO-BOOT semantics
  now completes that reducer/fix: ANGLE revision 2 gives
  `EGL_CONTEXT_HARDENED_ANGLE=TRUE` real shader-hardening behavior, while Mesa
  DRI2 only has the older WebGL compatibility bit and no hardened context or
  compiler policy. VERDICT: withdraw the incomplete
  `EGL_ANGLE_create_context_webgl_compatibility` advertisement, define the
  revised `0x34f8` token, and do not fake TRUE semantics. The pre-patch
  softpipe contract run fails 2 checks (advertised=1 and legacy WebGL-only
  FALSE succeeds); the same exact ES3/ES2 runtime-order lists reproduce
  `EGL_BAD_ATTRIBUTE`. Post-patch the new 14-case matrix passes 14/14 across
  exact/reverse/base/TRUE/invalid/extension-disabled/API/precedence cases; the
  prior 9-case no-error matrix remains alongside it. Default diagnostics emit
  zero trace rows; the enabled control emits 12 bounded failure rows. Focused
  `port-mesa`, narrow `rootfs-refresh`, deepest/top diff checks, and exact
  QEMU-none pass. Sysroot=image libEGL is
  `0e97ab6cd81ec7e3a3969ffc26618a855718b6a8aeff8288d3b95947c5f20027`
  (build ID `fab08fd4014873f3b422b5d411b9fc96381d8e65`); overlay=image reducer is
  `6c2d6810e3ddc5bc748e93075f70ce171fd7b6bdd67e10c4f2feb7106e3803a5`
  (build ID `3522fb0be3135153ae4dd0f2e14cd770249cf18e`); fs.img is
  `e2485574df101eced090f61fb4d6bfd577fa14f11c42da1dc020efccfe41ed13`.
  Chromium's libEGL/libGLES symlinks still resolve to guest Mesa. Independent
  adversarial NO-BOOT review BOOT APPROVES the revision-2 withdrawal. EGL
  extension strings have no revision negotiation: the revised name promises
  both tokens, so retaining the old advertisement while accepting only FALSE
  or rejecting TRUE would remain dishonest. Chromium appends both `0x33ac`
  and `0x34f8` only when that extension name is present; withdrawal therefore
  makes it omit both attributes instead of weakening hardening semantics. The
  reviewer confirms the 14/14 matrix plus existing 9/9 no-error matrix,
  default-silent tracing, exact staging identities/symlinks, deepest/top diff
  checks, and QEMU-none. Canonical post-withdrawal treatment is now N=2 PASS
  on real KVM+virgl: T1 `20260710T081853Z-a1-t1-post-withdrawal-mp1-audio1`
  measured 28.72/28.55 presented+virgl-BO fps with player deltas
  178514/178505; T2
  `20260710T082353Z-a1-t2-post-withdrawal-mp1-audio1` measured 29.27/29.30
  with deltas 178512/178514 (artifacts under `chromium-youtube-m7/`). Both
  proved exact multiprocess/audio-disable argv, external GPU init with zero
  bad-attribute/no-config/CollectGraphicsInfo/init-exit markers, live semantic
  GPU roles in both windows, no software/crash marker, canonical artifacts,
  fresh debugcon, owned reap/cleanup, and exact-QEMU-none. Thus withdrawal
  clears the EGL-init blocker, but the four-window mean is 28.96fps: the
  multiprocess lever is METRIC-NULL versus the established 28.9 and far below
  52. Each regular log retains one non-process-fatal `missing
  GL_ANGLE_webgl_compatibility` clue; the GPU role remains live, while exact
  EGL extension/`0x33ac`/`0x34f8` omission is unavailable with tracing OFF.
  Canonical post-withdrawal control is now N=2 PASS: C1
  `20260710T052459Z-a1-c1-mp0-audio1` measured 28.59/30.21fps with player
  deltas 178515/178515; C2
  `20260710T083653Z-a1-c2-post-withdrawal-mp0-audio1` measured 26.43/27.95
  presented+virgl-BO fps with deltas 178515/178515. C2 proved exact
  control/audio-disable/forensics-off argv, real KVM+virgl/OpenGL submit,
  canonical frames/logs, fresh debugcon, artifact validation, clean
  crash/GPU-init/audio/software scans, owned reap/scratch cleanup, and exact
  QEMU-none. Final four-window A/B is control 28.295 vs treatment 28.960:
  +0.665fps/+2.35%. VERDICT: multiprocess is METRIC-NULL, supplies no useful
  frames, and remains far below 52. The EGL revision-2 closure stands. Focused
  OFFLINE frame-supply forensics now finds a concrete context-admission blocker:
  T1/T2 destroy 12/11 passthrough contexts because Mesa reports
  `GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS=192` above Chromium's fixed 64, plus one
  WebGL context per run for missing `GL_ANGLE_webgl_compatibility`. Chromium's
  paths call `Destroy(true)` and return fatal per context, so a live GPU role is
  not proof of usable compositor/raster/WebGL contexts. MP nevertheless engages
  real GPU buffer handoff (about two virtio submits, one dma-buf
  reservation/import, and matching sync-file activity per flip versus control's
  about one submit and no dma-buf sync), but still supplies only ~29fps. An
  urgent measurement hole prevents attributing that exact cadence: `vq=hd720`
  is only a soft request, and no retained evidence proves selected resolution,
  source cadence, decoded frames, or rVFC cadence; a 30fps Auto rendition is a
  plausible but UNPROVEN hypothesis. Exact local 1280x720@60 H.264/VP9 already
  decoded ~60.9/60.6 and presented 52.7/53.8, closing codec and the ~29fps
  present wall. NEXT: add a default-OFF bounded YouTube probe (20x1s plus rVFC,
  quality/current source, decoded+dropped frames, buffering/state, and long-task
  totals), run N>=2, and fail closed unless it proves 1280x720 at ~16.7ms source
  cadence. Then use a scratch image retaining Chromium's matching bundled ANGLE
  and test MP with `--use-gl=angle --use-angle=gl` over real Mesa/virgl; require
  the bundled provider, no software fallback, and zero context-admission fatals.
  Do not re-advertise revision-2 ANGLE semantics from raw Mesa and do not apply a
  getter-only 192->64 clamp (GLES 3.1 requires at least 96).
  The default-OFF measurement-hole closure is now implemented/staged:
  `YT_MEDIA_PROBE=1` is strict 0/1 and valid only with MP=1/audio=1 and EGL
  forensics OFF; normal arms retain their launch flags. An inert MV3 extension
  runs in MAIN world only under its exact `--load-extension` arm so YouTube's
  selected/available-quality APIs are accessible. It emits a parse-stable,
  capped 24-row artifact: start, exactly 20 one-second samples, media summary,
  rVFC summary, and done. Samples cover dimensions, redacted/limited source
  identity (scheme+host only; path/query/token removed), current time/rate,
  paused/ended, quality, ready/network state, buffered-ahead, VPQ and WebKit
  decoded/dropped totals, rVFC callbacks/presented frames, and long-task
  count/duration; rows are <=1000 bytes and total evidence <=24KB. The host
  parser independently requires stable 1280x720, active progress, rVFC media
  delta/callback/presented evidence, and median 15.0-18.5ms cadence with >=60%
  near 16.7ms before either FPS window. Missing/inconsistent/injected evidence,
  30fps cadence, wrong dimensions, or unavailable rVFC fail closed; a complete
  but unproven source is DIAGNOSTIC-ONLY with no FPS/perf claim. Real
  KVM+virgl/OpenGL, no-software, semantic GPU-role, crash, fresh-debugcon,
  artifact, owned-reap/cleanup, and exact-QEMU gates remain armed. Tcl and JS
  reducers pass the 60/30fps, dimensions, missing-API, redaction/bounds, arm,
  injection, and parser matrices; focused `rootfs-refresh -j2`, deepest/top
  diff checks, and exact-QEMU-none pass. Overlay=image hashes are manifest
  `ac282262...`, library `bffe9813...`, probe `315bdb77...`; fs.img is
  `16606551945699b90bb1ca6ff829ff17a0cfb6aaa1a77ffcc607694ec453e45e`.
  Submitted for independent adversarial review before N=2 with:
  `for n in 1 2; do out="build-x86_64/chromium-youtube-m7/$(date -u
  +%Y%m%dT%H%M%SZ)-a1-media-probe-t${n}-mp1-audio1"; env
  YT_MULTIPROCESS=1 YT_DISABLE_AUDIO_OUTPUT=1 YT_EGL_FORENSICS=0
  YT_MEDIA_PROBE=1 YT_WINDOW_SECONDS=60 YT_OUTDIR="$out" timeout 900 expect
  scripts/gpu/chromium-youtube-presentfps.expect || break; done`.
  Independent adversarial review PREBOOT REJECTS the media probe: nonpositive
  or regressing cadence can fail open; console rows lack extension provenance
  and MAIN-world output is page-spoofable; JS and host classifiers diverge;
  exact-key, counter, summary/API/quality/long-task consistency are missing
  and the cadence histogram is too coarse; finite/range/monotonic/byte caps and
  helper provenance are incomplete. Arm-contract, staging, and cleanup checks
  passed and exact QEMU remains none. No N=2 boot is authorized; bounded
  correction plus independent re-review is pending. The bounded correction is
  now implemented/staged, with the host Tcl parser as the sole semantic
  classifier and JavaScript limited to raw facts. Every rVFC interval slot is
  reconciled (callbacks=intervals+1, histogram/count/sum/media/presented
  deltas), and any invalid, duplicate/nonpositive, or regressing interval makes
  source proof NULL; the adversarial 1080x0ms+120x16.667ms case is rejected.
  Chrome INFO-console evidence must have the exact pinned extension ID
  `edfilgocpdgbkehcgdillfgnnhclphol`, `probe.js` source/line agreement, and a
  per-run 32-hex nonce. Exact row keys/order plus finite grammar, byte/range,
  monotonic counter, quality/source, VPQ/WebKit, long-task, and optional-API
  consistency are fail-closed. Static mutations reject raw/page/mixed/full
  spoofs, wrong nonce/source/line, schema/order/numeric/counter/summary faults;
  paused, short-progress, insufficient-presented/media, wrong-dimension, 30fps,
  and missing-rVFC cases remain diagnostic NULLs. V8 syntax/reducer and the
  corrected static arm/parser suite pass; focused rootfs refresh, byte-identical
  debugfs extraction, JSON and deepest/top diff checks, and exact-QEMU-none pass
  without a boot. New overlay=image hashes are manifest
  `569762f5539d406de7736a12932a0ab9ad937c4044dd523cf443cddf63018898`, library
  `2fde2692bb276d62b5a3e07a32e9bdf0e6ac70be0b3217bfbc8d8dd912ce137c`, probe
  `c3c207ca2fa8f958cdfd6ba524e621705b594d594dbd11df4e463d9681d0c23a`; fs.img is
  `fc98539aab2b9b98af86a28f9039d690175ca2ddf5109589dcf8377d4b6c7fca`.
  Independent adversarial NO-BOOT still rejects the corrected probe: direct
  mutations that regress intermediate `rvfc_presented` or `rvfc_media_time`
  samples still parse/source-prove PASS against a zero-regression summary, and
  impossible `median_ms=1` with every interval in `b15_18p5` is accepted as
  DIAGNOSTIC instead of INVALID. The shipped suite, hashes, image, pinned
  extension-ID, and QEMU-none checks pass, but bounded per-sample monotonicity
  plus callback/summary reconciliation and histogram/median consistency must
  be fixed and independently re-reviewed. N=2 remains unauthorized. This
  final parser correction is now implemented/staged: all 20 samples enforce
  callback/presented/media range and monotonic shape, exact zero-callback
  sentinels, no state change without a callback, retained-last-valid state, and
  an 8x presented-jump ceiling. Observed invalid/pair-invalid/regression/
  duplicate-or-nonincreasing facts must reconcile with sufficient summary
  counters; zero or insufficient summaries reject. The recorded cadence median
  must also fall within the histogram-rank feasible interval. Shipped mutations
  cover intermediate presented/media regressions, duplicates and invalids,
  no-callback changes, sentinel/range/jump violations, 8x+1, and the impossible
  1ms median; valid 15.000/18.500ms, exact 8x, and available zero/one-callback
  boundaries pass. ON/OFF Tcl suites and V8 syntax/reducer pass, as do
  deepest/top diff checks and exact-QEMU-none. Assets and image were unchanged:
  manifest `569762f5539d406de7736a12932a0ab9ad937c4044dd523cf443cddf63018898`,
  library `2fde2692bb276d62b5a3e07a32e9bdf0e6ac70be0b3217bfbc8d8dd912ce137c`,
  probe `c3c207ca2fa8f958cdfd6ba524e621705b594d594dbd11df4e463d9681d0c23a`,
  fs.img `fc98539aab2b9b98af86a28f9039d690175ca2ddf5109589dcf8377d4b6c7fca`.
  Independent adversarial re-review remains NO-BOOT for only two false
  rejects: retained-last-valid media is discarded after an invalid callback
  despite sufficient `media_invalid`/`interval_invalid` reconciliation, and
  three-decimal median quantization mishandles true 14.9996/18.5004ms values
  at the open-bin edges. The prior three fail-opens now reject; full suites,
  staged hashes, diff checks, and exact-QEMU-none pass. Bounded retained-state
  reconciliation plus precision/tolerance fixtures and independent re-review
  remain pending; N=2 is unauthorized. Both false rejects are now corrected:
  an unchanged retained `rvfc_media_time` is accepted only when
  `media_invalid` covers every advanced invalid callback and
  `interval_invalid` also covers the first next-valid no-pair slot. The exact
  cb60-valid -> cb61-invalid/retained -> cb62-next-valid fixture reconciles
  1 media-invalid + 2 interval-invalid, parses valid, and remains diagnostic
  NULL; coherent zero-media-invalid and one-interval-invalid variants reject.
  Three-decimal median feasibility now uses its exact half-ULP interval while
  histogram bins remain authoritative, with a regression-free histogram-sum
  bound preventing rebinning. Thus true 14.9996->15.000 in `b14_15` and
  18.5004->18.500 in `b18p5_20` parse valid but cannot source-prove, while
  rebinned-near60 spoofs reject. Full ON/OFF Tcl suites, V8 syntax/reducer,
  deepest/top diff checks, and exact-QEMU-none pass. Assets/image are unchanged
  at manifest `569762f5539d406de7736a12932a0ab9ad937c4044dd523cf443cddf63018898`,
  library `2fde2692bb276d62b5a3e07a32e9bdf0e6ac70be0b3217bfbc8d8dd912ce137c`,
  probe `c3c207ca2fa8f958cdfd6ba524e621705b594d594dbd11df4e463d9681d0c23a`,
  fs.img `fc98539aab2b9b98af86a28f9039d690175ca2ddf5109589dcf8377d4b6c7fca`.
  Independent final re-review is pending; N=2 remains unauthorized.
  Independent final NO-BOOT re-review now BOOT APPROVES exactly two N=2 boots
  with `YT_MULTIPROCESS=1 YT_DISABLE_AUDIO_OUTPUT=1 YT_MEDIA_PROBE=1
  YT_EGL_FORENSICS=0`. Review confirms the retained-invalid/no-pair coverage,
  open-edge half-ULP controls, and anti-rebin spoof rejects; the prior three
  fail-opens (intermediate presented/media regressions and impossible median)
  remain closed. Exact INFO-console extension-ID/`probe.js`/line/nonce
  provenance, schema/counter/API checks, ON/OFF Tcl suites, V8 reducer,
  unchanged asset+fs identities above, deepest/top diff checks, and
  exact-QEMU-none all pass. Each boot remains fail-closed and yields no FPS or
  PERF-VIDEO claim unless the probe proves stable 1280x720, active progress,
  clean ~16.7ms rVFC cadence, and sufficient presented/media deltas; honest
  diagnostic NULLs still count toward N=2 attempts, not performance evidence.
  The exactly-two authorized media-probe boots are complete and both formal
  driver artifacts are INVALID/NULL (`untrusted-marker-envelope`, no FPS or
  PERF-VIDEO claim):
  `chromium-youtube-m7/20260710T102211Z-a1-media-probe-t1-mp1-audio1`
  and `.../20260710T102429Z-a1-media-probe-t2-mp1-audio1`. The failure is a
  helper ABI bug, not missing probe output: guest `/bin/grep` supports only
  `grep pattern [file ...]`, so its `-q`/`-E` operands became the pattern and
  the real patterns became filenames. Each canonical launcher log nevertheless
  retains exactly 24 correctly ordered, pinned-extension/`probe.js`/line/nonce
  rows and passes direct strict host-parser replay as valid but
  `source_proven=0`. Both runs selected stable `640x360`, quality `medium`,
  source `blob:https://www.youtube.com/<redacted>`; available levels included
  `hd720` through `hd2160`. T1/T2 rVFC medians were 33.333/33.334ms with
  0/451 and 0/428 near-60 intervals, presented deltas 574/575, active progress
  19.840/20.317s, VPQ/WebKit decoded+dropped deltas 595+35/608+50, and long
  tasks 15/2.139s and 15/2.869s. Thus the N=2 diagnostic explains the ~29fps
  supply: YouTube Auto selected 360p30, not the requested 720p60. Both boots
  proved KVM+virgl/OpenGL submit, live GPU role, matching image assets, fresh
  debugcon, zero software/fatal markers, owned reap/cleanup, and exact-QEMU
  none. The helper ABI bug is CLOSED: the live generator stages exactly one
  option-free `/ytmediaprobe.sh`, and every generated media-arm helper is
  checked grep-option-free/pgrep-free before staging. Actual guest `grep`,
  complete/failure/timeout/nonce reducers, ON/OFF static suites, and final
  independent adversarial NO-BOOT review all PASS. Exact T1/T2 replay now
  yields valid 24-row envelopes but remains `SOURCE_PROVEN=0` at stable
  640x360 and 33.333/33.334ms, so it is still diagnostic NULL with no FPS
  credit; do not spend a helper-only rerun. No quality-force selector exists.
  NEXT: implement the bounded default-OFF `hd720` selector and independently
  review it before authorizing any new boot or N>=2 attribution.
