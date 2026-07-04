# Active xv6 Work Plan

Last updated: 2026-07-04 (FULL COMPACTION REWRITE after the 07-04 round:
R5 root-caused + fixed, P2 ordered-pageflip default landed, Q2 real-GL
runtime-validated with the first default-path M9 PASS, P3 ext4 slice landed
gated, kprofile measurement validity fixed. All verbose evidence chains
moved to the history file and git log; this file holds current status,
queue, rules, and compact lane conclusions only.)

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

Make the KDE/Chromium desktop on x86_64 KVM+virgl stable and responsive.
Current posture: correctness lanes (P0 freeze, R5 corruption) are fixed or
closed; the remaining work is performance (M7 video path, M2 syscall cost,
M8 idle CPU), user-visible input (R9), and statistical closure.

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

Audio: host audio is NOT a readiness prerequisite. `pactl` probes are
default-off (`kde_pactl_probe=1` to re-enable); non-audio gates run with
`QEMU_AUDIO_BACKEND=none`.

| # | Metric | How measured | Current (2026-07-04) | Goal |
|---|--------|--------------|----------------------|------|
| M1 | YouTube-freeze survival | P0 repro recipe, 15 min, 3 runs | 3/3 responsive clean (2026-07-03 battery); P0 closed | 3/3 clean |
| M2 | Guest `getpid_ns` | `syscalltlb 2000` nographic | 1.65-1.94us (P1 2a/2b landed) | < 1.5us (P1 2c/2d = N4) |
| M3 | `tlb_amplification_ns` (1024 pg) | same | noisy 2.7-3.7us band | 0 on default boot |
| M4 | `konsole_wait_ms` | KDE desktop-interaction reducer | 1883-2144 band across the 07-04 batteries (best 1883 post-R5-fix) | < 2000 |
| M5 | `first_visible_ms` | same | 11471-13850 band | < 15000 |
| M6 | `mesakmsgl` direct-KMS FPS | pageflip A/B | 118-125 ordered, now DEFAULT-ON | done (was: ordered default) |
| M7 | `presentedFPS` (60fps video) | chromium-video kprofile, KPROFILE_SECONDS=90 | 44.2 real GL + ordered pageflip (arc 07-04: 36.8 -> 42.9 -> 44.2; dropPct 29.6; decode 61.5 keeps pace). Sole remaining ceiling: software-blit scanout — every flip copies, native_present_credit=0 | >= 55 (N1) |
| M8 | Idle-desktop host CPU | 10s `/proc/$pid/stat` utime+stime delta (NOT lifetime ps pcpu) | borderline 85-130% band; owner evidence = vCPU/KVM idle wake cadence (guest PCs in arch_idle_halt), not a spinner | < 100% (N5) |
| M9 | Chromium window visible | chromium-video launch-only reducer | PASS on the DEFAULT path with REAL hardware GL (2026-07-04): zero missing-GL fatals, zero software fallbacks, GPU errors 0 | PASS (holds; guard in every battery) |

Fork-safety gate for any syscall/scheduler/TLB/mm change: `forktest`
(rc=1 "fork claimed to work N times!" = known exhaustion signature),
`clonetest` rc=0, `cowtest` rc=0, same boot.

## Work Order — Current Queue (recut 2026-07-04 post-round)

The 07-03 Q1-Q7 queue is RETIRED: Q1 landed, Q2 runtime-validated (real
GL), Q5 root-caused+fixed, Q7 landed. New queue:

- N1 = M7 present path (P2 step 3): the last M7 blocker. All flips are
  software_blit copies. Two sub-routes to evaluate FIRST as a decision
  slice: (a) rutabaga/gfxstream-capable QEMU enabling virgl+blob
  (dmabuf/zero-copy scanout; current QEMU rejects "virgl+blob"), an
  external-capability change; (b) kernel-side reduction of copies in the
  virtio-gpu present path short of full zero-copy. Measure the blit cost
  share first (offline: fbstat + kprofile archives) before building.
- N2 = P3 promotion: default-flip `ext4_read_page_direct=1` (browser
  start 10.6s -> 6.2s, read_page_ms -50%, battery green 07-04; needs the
  Guardrails default-flip protocol: same-session A/B + control + battery).
  Then optional second slice: batch the remaining non-sequential
  single-page fills (executable page-in; ~71% of fills).
- N3 = R9 cursor out-of-range (user-visible) + the KWin LibinputBackend
  nullptr payload bug found by R5 forensics (same input area). Harness
  ready: `scripts/gpu/r9-cursor-contract-probe.expect` (source-only,
  startup-injected probe + debugfs retrieval; do NOT drive it over the
  interactive serial shell).
- N4 = P1 steps 2c/2d (cpumask atomics skip, CR0.TS shadow) for M2 <1.5us.
  Implement both together, one battery.
- N5 = M8 idle cadence: vCPU/KVM idle wake churn (guest halted, host vCPU
  threads at 85-130%). Host-side attribution done; next is guest tick/timer
  cadence reduction (relates to N7).
- N6 = R2 PCID stale-TLB lane: RETEST CHEAPLY FIRST — the R5
  free-before-shootdown fix plausibly WAS the "PCID corruption" (noflush
  widens the same stale-TLB window; the 07-02 noflush trial reproduced the
  exact KWin signature R5 explained). One guarded PCID default-flip KDE
  battery before any new reducer work.
- N7 = timer tick loss (~14% under load): fix jiffies advancement
  (TSC-compensate or any-CPU advance). Fixes measurement trust AND late
  timer fires (frame pacing). Gate: nographic + KDE battery + M2/M3 within
  noise (touches the timer hot path).
- Continuous: R5 statistical closure (9/30+ clean attempt-1 KWin launches
  accrued; count every future battery), R3 recurrence watch (rcu_head
  double-free may share the R5 root cause — one `slab_alloc: repairing
  corrupt freelist cache='rcu_head_cache'` line was seen 07-04 pre-R5-fix).
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

### P1 — Syscall overhead (M2/M3): steps 2c/2d OPEN (= N4)

2a (FS_BASE cache) + 2b (trapframe direct) landed; M2 1.65-1.94us.
Remaining identified fixed costs: 2c cpumask atomics skip, 2d CR0.TS
shadow. Implement together, one battery; then step 3 (gated default flip)
per Guardrails. CR-3 (%fs selector reload) is the latent ABI follow-up.

### P2 — Ordered page flip: steps 1-2 DONE, step 3 = N1

Step 1 direct-KMS A/B: 100 -> 118-125 FPS. Step 2 default flip LANDED
2026-07-04 (kernel `69310a3`, default-aware helper in
`virtio_gpu_scanout.c`; opt out `virtio_gpu_ordered_page_flip=0`):
default-on KDE pass (M4 2144), explicit-off control pass (M4 2122), video
36.8 -> 42.9. Archives `20260704T181500Z/183000Z/185000Z-q7-*`.
Step 3 (zero-copy present, M7 >= 55) is N1: every flip is still
`software_blit` with `native_present_credit=0` (fbstat), the sole
remaining M7 ceiling.

### P3 — Ext4 read-path serialization: first slice LANDED gated (= N2)

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
N2 = promote the default per Guardrails; optional second slice = batch
the remaining non-sequential single-page fills (executable page-in
pattern, ~71% of fills).

### Q2 / R8 — Chromium real GL: DONE (validated 2026-07-04)

History: "window not visible" was never renderer admission (exonerated);
a real Mesa EGL attr-order bug was fixed (`5e3f4bebe`); the default-path
blocker was Chromium 150's passthrough decoder requiring the ANGLE
extension ladder. Direction settled by the user: REAL GL — no
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

### R2 — PCID stale-TLB: OPEN, retest first (= N6)

The R5 free-before-shootdown fix plausibly explains the "PCID corruption"
(noflush widens the same stale-TLB window; the 07-02 noflush default trial
reproduced exactly the KWin corruption signature R5 root-caused). Before
any reducer work: one guarded PCID/noflush default-flip KDE battery on the
fixed kernel.

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
`virtio_gpu_host_cursor_only=1`. Probe machinery is READY and source-only:
`scripts/gpu/r9-cursor-contract-probe.expect` (startup-injected
`/r9-run.sh`, debugfs polling, monitor `mouse_move` after `phase=armed`) —
do NOT drive the probe over the interactive serial shell (two runs burned).
Include the LibinputBackend nullptr payload fix here.

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
  `*-q2-real-gl-full-video-m7-44fps`).
- `build-x86_64/yt-mainpage-freeze-repro/` — P0/M1 replays.
- `build-x86_64/syscall-tlb-proof/`, `build-x86_64/pageflip-ordered-ab-proof/`,
  `build-x86_64/desktop-bottleneck-profile/`,
  `build-x86_64/r5-kwin-startup-classification/`.
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
