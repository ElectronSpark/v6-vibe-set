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
  repaint-on-damage; the gaps are DAMAGE-ARRIVAL gaps — i.e. the client
  commit chain (konsole render latency after frame-callback) is the
  remaining serial lever, plus round-trip count.

## UNFINISHED WORK — priority queue

- U1 (durable environment fix, unblocks everything): implement
  drmGetDevice2 device metadata (PCI bus/vendor/device) for the guest DRM
  node so mesa stops using its fragile metadata-less fallback. This
  fallback loses a first-attempt readiness race that (a) caused the
  image-layout-dependent plasmashell EGL/software-fallback regression
  (root-caused 2026-07-08; any fs.img rebuild can re-roll it) and (b)
  causes kwin's first-attempt GBM/EGL failures. Kernel target:
  `fb_drm_core_kms.c` (driver name at :617) + whatever sysfs/ioctl surface
  drmGetDevice2 needs. Secondary/cheaper: EGL-init retry or readiness gate
  in the session launcher.
- U2 (M4 serial lever): client-commit-chain measurement — co-enable the
  konsole wayland event trace AND a kwin-side trace in ONE boot to split
  each 58-139ms frame-callback wait into kwin-schedule vs konsole-render
  time; then attack the larger half (round-trip count in Qt-Wayland init
  is the other axis).
- U3 (cheap M4 win, ~70-100ms): prune Qt imageformats plugin scan for
  konsole (kimg_avif 45ms + exr/jxl/heif/raw; 64-dlopen storm = 149ms of
  launch). Opt-in first (QT_PLUGIN_PATH scoping or image-level prune),
  gated A/B via the deterministic dlopen counters (below konsole_wait
  noise floor ~±300ms, so judge by dlopen count/time).
- U4 (pending perf verdict): async-cursor hover re-A/B at n>=2 on the now
  clean environment (correctness already GO incl. R9 probe; hover medians
  were contaminated by the EGL regression). Also build the reusable kwin
  ioctl-timing LD_PRELOAD shim (repo-owned) — three workers have now built
  throwaway versions.
- U5 (kernel, small): queue DRM_MODE_PAGE_FLIP_EVENT completions in the
  ATOMIC path (`fb_kms_atomic.c:441-689` has none; legacy path :388-434
  does) — prerequisite for any future kwin 6.x/atomic modeset work
  (kwin 5.27 disables AMS unconditionally in VMs; no env override).
- U6 (before any neg-dcache default-on): close the dcache eviction/no-flush
  gap (lookup_seq resets to 0 on inode eviction; no dcache flush on
  evict/unmount; positive-branch child_sb latent UAF noted in review).
- U7 (M7, parked with reopen conditions): native present / unlocked-wait —
  reopen only after (a) root-causing the missing sync completion
  (n1ab-unlocked-a6.log) AND (b) cutting the wedge-decision cost. M7=51.
- U8 (M8, deferred): idle-cadence payoff measurement on a GL boot; N5
  poll-notify full-wait gates are default-ON after the evdev fix.
- U9 (N2, blocked): ext4 direct-read default promotion — needs the kernel
  page-fault diagnosis (cr2=0x1aafdd193 into _rodata) explained first.
- U10 (N3 residual): KWin LibinputBackend nullptr payload bug.
- U11 (P1/M2 <1.5us): needs a NEW approach; cpumask/CR0.TS is dead.
- U12: push the pending commits (currently ~7 ahead of origin) — needs
  explicit user approval.

## Landed opt-in gates (all default-OFF unless noted)

- `vfs_neg_dcache=1` — negative-dcache honor; -95% ENOENT, no M4 effect.
  Correct VFS fix (consumer bug + bump-inside-lock race); see U6 before
  default. Kernel 0cf817d + sentinel fix.
- `virtio_gpu_vblank_paced_flip=1` — flip-complete events paced to the
  synthetic vblank edge; engagement proven; no gap/first-frame win; small
  warm cost (~+250ms/launch). Vsync-semantics infrastructure. 1661795.
- `virtio_gpu_async_cursor=1` — cursor image uploads off the compositor
  thread (17-36ms/upload); correctness GO (R9 probe PASS); perf verdict
  pending U4. 1661795.
- `XV6_ROOTFS_LDSOCACHE=1` (make-rootfs) — bakes multiarch ld.so.conf +
  ld.so.cache (1352 entries); -17..23% openat, -36..51% ENOENT, no M4
  effect. Parity-with-Linux cleanliness candidate.
- `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` / `KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`
  — repo-reproducible SONAME-stub A/B fixtures; NOT responsiveness wins.
- Default-ON (validated): ordered pageflip, poll-notify full-wait (both
  gates, post evdev/pipe/inotify/timerfd producer fixes), TSC jiffies,
  kernel_preempt, console_async, ring depth 60, total-reaper redesign.

## Operational cautions (bite-you-again class)

1. Harness hardcodes the kernel at `kde-plasma-desktop-smoke.expect:6`
   (`$build/kernel/build/kernel/xv6.bin`) and IGNORES `KERNEL` env — bisect
   by sha-verified swap/restore of that file.
2. `qtquick-accelerated-path-policy` FAIL streak right after an fs.img
   rebuild = the image-layout EGL-init race signature (plasmashell dri2
   fail x4 + "EGL display 3001", kwin GL fine). Rebuild the image again to
   confirm; do NOT chase kernels or the host. Durable fix = U1.
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
   (`M=XX; cmd; echo ${M}RES=$?`).
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
| M1 | YouTube-freeze survival | 3/3 clean; P0 closed | hold |
| M2 | getpid_ns | 1.65-1.94us | <1.5us (U11) |
| M3 | tlb_amplification 1024pg | 2.7-3.7us | 0 |
| M4 | konsole_wait_ms | warm ~1.2-1.4s, cold ~1.5s (N=3 baseline); bottleneck = client-commit chain + round-trips (U2), imageformats (U3) | Linux-like (~0.3s) |
| M5 | first_visible_ms | ~6.4s | <15000 |
| M6 | mesakmsgl FPS | 118-125 (ordered default); ~60 by design under vblank-paced gate | done |
| M7 | presentedFPS | 51.0 | >=55 (U7) |
| M8 | idle host CPU | unmeasured-valid | <100% (U8) |
| M9 | Chromium visible | PASS guard | hold |

Fork-safety gate for any syscall/scheduler/TLB/mm change: forktest (rc=1
exhaustion signature OK), clonetest rc=0, cowtest rc=0, same boot.
