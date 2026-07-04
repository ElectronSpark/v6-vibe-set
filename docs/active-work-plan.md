# Active xv6 Work Plan

Last updated: 2026-07-04 (materialized on branch codex/host-linux-abi-shell-port-ff
as a copy of the canonical plan; appended "M7 / Present-Path + Measurement-
Validity Findings (2026-07-04, OFFLINE)" near the P2 lane. NOTE: the canonical
plan also lives on codex/host-linux-abi-shell-port and origin/codex/kde-qt-
wayland-bringup; reconcile before treating this -ff copy as authoritative).
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

PREREQUISITE for all GUI rows (M1, M4-M9): a live WSLg host audio path.
If host PulseAudio is down, the guest `pactl` faults and every KDE smoke
fails at `kde-session-ready-crash` before any metric is captured — restore
audio first (`wsl --shutdown` from Windows). Only M2/M3 and the fork gate
run without a desktop. See "Runtime Audit" below for the last reproduction.

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
| M9 | Chromium window visible | `KDE_SMOKE_REDUCER=chromium-video KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1` | Default Mesa path still FAILs with the known robust-client crash; explicit default-off `WAYLAND_CHROMIUM_BUNDLED_GL=1` alternate root passes launch-only via disabled/software GL fallback | PASS |

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

- Q1 COMMIT SWEEP — PARTIALLY LANDED; now REVIEW-INFORMED. Status
  2026-07-03: kernel (HEAD `07efc74`) and user (HEAD `6e13b1f`) are
  COMMITTED; mesa attr-order is committed (`ports/mesa/src` `5e3f4bebe`)
  but ~9 files remain dirty in `ports/mesa/src`, `ports` shows a modified
  pointer, and top-level harness scripts (`run-qemu.sh`,
  `stage-host-gui-runtime.sh`, `host-egl-gbm-gl-smoke.c`) are still
  uncommitted. REMAINING WORK: (a) finish the mesa/src + ports + top-level
  commits deepest-first; (b) the kernel was committed WITHOUT resolving
  the two starred Code Review blockers — CR-1 (idle-pull precheck can
  still starve the 1+1 case; committed as `ad7032d`) and CR-2 (unprivileged
  non-canonical `arch_prctl` panic; committed inside `57b09dc`) are now
  IN-TREE and need follow-up fix commits, not a pre-commit gate. CR-5/CR-6
  (probe hot-path atomics + IRQ-stack frame, touch M2/M8) likewise landed
  unfixed — fold into a follow-up. No push without user approval.
- Q2 = R8 GL decision + implementation: default Chromium path fails on
  missing `GL_ANGLE_robust_client_memory` (then a 6-extension ladder) in
  Chromium passthrough. Options analyzed in the R8 section: implement the
  passthrough-required semantics in Mesa/virgl, or adopt a documented
  launcher GL policy (bundled-ANGLE/software fallback — works launch-only
  today but M7-poor). Needs a user decision on direction before heavy
  implementation.
- Q3 = R9 cursor out-of-range triage (user-visible; triage chain in the
  R9 note below).
- Q4 = P1 steps 2c/2d (cpumask atomics skip, CR0.TS shadow) — M2 is at
  1.65-1.94us vs the <1.5us goal; these two cuts are the remaining
  identified fixed costs. Implement both together, one gate battery.
- Q5 = R5 flake-rate reduction: the KWin startup crash family is now the
  main gate polluter (cost 2 P2 default-flip runs and 3 rerun-needed
  archives in one night). Root-cause or bound it; it blocks R6 step 2.
- Q6 = R2 (PCID corruption reducer, then guarded default retry).
- Q7 = R6 step 2 retry (P2 ordered-pageflip default flip) after Q5.
- Tracked follow-up after Q1: R7c/M8 vCPU/KVM idle-cadence work now has
  owner evidence; do not treat it as a Q1 commit blocker, but do not claim
  M8 green. Parked: R3 (bounded-open with diagnostic in place), M7
  (blocked on Q2).

Lane bullets (context for the queue above):

- R1/P0: CLOSED for this round — structural H-d fix landed, 3/3
  responsive M1 replays, R5 classified for the gate. CAVEAT (Code Review
  CR-1): the `eevdf_idle_pull` `nr_running>=2` precheck may not cover the
  1-hog+1-waiter case, so the "3/3 clean" may not prove the code path —
  re-examine before treating P0 as committed-done.
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

## Code Review — Uncommitted Kernel Diff (2026-07-03)

A read-only review of the ~27-file kernel working tree (the Q1 commit
scope) surfaced the findings below. They are NOT yet fixed. Triage each
into its lane during Q1 and decide fix-before-commit vs commit-and-track.
The two starred items gate Q1. Line numbers are the working-tree state at
review time — re-grep before editing.

Q1 triage update (2026-07-03, in progress; nographic validation passed):

- CR-1 fixed in the working tree: `eevdf_idle_pull` now treats
  `nr_running >= 1` on a remote CPU as candidate queued work, matching
  `__eevdf_idle_balance`'s locked pull semantics for the 1-hog+1-waiter
  shape. CR-10 was folded into the same slice: idle-pull cooldown is now
  stamped only after a real locked pull attempt, not on entry/trylock
  contention. Nographic Q1 gate passed, and the Q1 KDE/Chromium/M8 battery
  is now collected. P0 can be committed with the M8 caveat below; do not
  present M8 as green.
- CR-2 fixed in the working tree: `ARCH_SET_FS` rejects `addr >= UVMTOP`
  with `-EPERM` before `wrmsr`, and `thread_clone(CLONE_SETTLS)` validates
  arch TLS before publishing the child trapframe. A regression case was
  added to `linuxsyscallabitest` for both `arch_prctl(ARCH_SET_FS,
  0x0000800000000000)` and raw x86 `clone(..., CLONE_SETTLS, bad_tls)`.
  CR-4 was folded in by wrapping the x86 FS_BASE write/cache update in
  `push_off()`/`pop_off()`. The nographic gate's `linuxsyscallabitest
  tls-reject` reducer passed.
- CR-5/CR-6 fixed in the working tree: starvation-probe note hooks now
  early-return through an exported cached `sched_starve_probe` gate, probe
  per-CPU storage is cache-line aligned, and the large rq snapshot buffer is
  static instead of reserved on the timer IRQ stack. The nographic gate
  remeasured M2 with `syscalltlb 2000`; the GUI/perf battery keeps M8 as a
  tracked R7c/M8 follow-up rather than a Q1 blocker. If M2 remains >1.5us
  after the GUI/perf battery, continue Q4.
- CR-8 fixed in the working tree: the idle loop disables interrupts and
  asserts IF=0 before the `NEEDS_RESCHED`/`arch_idle_halt` check, then
  restores the previous interrupt state after the halt decision.
- Tracked, not fixed in this slice: CR-3 (FS selector reload semantics; P1
  latent ABI follow-up), CR-7 (remote rq probe snapshot consistency; only
  reachable when `sched_starve_probe=1`, default-off), and CR-9 (timer
  `current_tick` fast-path boundary race; needs focused timerfd reducer).

Nographic evidence (2026-07-03):
`build-x86_64/desktop-bottleneck-profile/20260703T185541Z-q1-review-fixes-nographic-gate/`.
After rebuilding `user`/`rootfs-refresh` and recopying the refreshed
`fs.img`, the rerun exited with `harness-rc-rerun.txt = 0` and no leaked
QEMU process in `qemu-pgrep-after-rerun.txt`. Boot cmdline was
`root=/dev/disk0 desktop=0 video=1280x800 netsurf=0 webkit=0 glsmoke=0`.
`syscalltlb 2000` passed with `getpid_ns` 2656/3366/2877/3135 and
amplification 151/961/2122/3732 ns for 64/256/512/1024 pages;
`linuxsyscallabitest tls-reject` printed `x86 TLS reject OK`; `wakestorm
hd 256 5 2600` passed (`worker_runs=1280`, `unfinished_workers=0`,
`probe_snapshots_delta=0`, `idle_needs_resched_delta=0`); `forktest`
returned the expected rc 1 table-exhaustion signature; `clonetest` and
`cowtest` returned rc 0.

Q1 batch evidence (2026-07-03):

- KDE active-sample gate passed:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260703T190608Z-q1-review-fixes-kde-active-sample-pass/`
  (`status_code=0`, reducer `desktop-interaction-latency`). Metrics:
  `first_visible_ms=11373`, `first_nonzero_ms=6725`, direct launch PASS
  `elapsed_ms=4202`, `konsole_wait_ms=2827`. This keeps the desktop
  correctness guard green, but M4 was noisy-high versus the current
  scoreboard target and should not be counted as an improvement.
- Chromium launch-only default-path guard stayed in the known R8/M9 class,
  not a new Q1 regression:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260703T190838Z-q1-review-fixes-chromium-launch-only-known-r8-fail/`
  (status `chromium-video-chrome-crash-regression`, post-evidence
  `status=FAIL reason=chrome-crash-regression`). The launcher log hit the
  known `missing GL_ANGLE_robust_client_memory` fatal path; process evidence
  saw browser/GPU/zygote but no renderer; `chromium_fault_count=0`,
  `gpu_init_error_count=0`, `gl_request_error_count=0`, and no kernel fault
  markers. Perf evidence was usable but below video target:
  `presentedFPS=36.0`, `decodedFPS=62.3`, `dropPct=34.83`.
- M8 late-settle spot-check collected successfully but is still RED:
  `build-x86_64/desktop-bottleneck-profile/20260703T190929Z-q1-review-fixes-late-settle-m8-spinners/`
  (`status=PASS reason=q1-review-fixes-m8-spinners-collected`). Host
  `ps pcpu` was `150/143/139` idle and `150/150/149` post-app; the added
  instantaneous 10s host delta was also high (`onecpu_pct=138.4`). Guest
  procstat does NOT show the old R7b spinner pattern: idle deltas over 10s
  were `kwin_wayland=60`, `plasmashell=60`, `Xwayland.real=21`,
  `kded5=60`, `kactivitymanage=47`; post-app deltas were dominated by the
  launched apps plus modest compositor work.
- Follow-up owner evidence for the high-M8 condition:
  `build-x86_64/desktop-bottleneck-profile/20260703T191543Z-q1-review-fixes-idle-gdb-high-m8/`.
  The condition reproduced (`pcpu` around `107..104`). Fifteen GDB rounds
  produced 90 CPU samples: 71 were `start_kernel + 1518`, mapped by
  `addr2line` to `arch/x86_64/inc/x86.h:207` (`arch_idle_halt()`), with
  only small timer/rq lock presence. Interpretation: the current high-M8
  symptom is not an obvious idle-pull busy loop; most sampled CPUs are at
  the intended halted-idle helper.
- Host-thread M8 attribution accepted/tracked:
  `build-x86_64/desktop-bottleneck-profile/20260703T192636Z-q1-review-fixes-host-thread-m8-attribution/`
  (`status=PASS reason=q1-host-thread-m8-attribution-collected`, no leaked
  QEMU in `qemu-pgrep-after.txt`). Boot cmdline matched the Q1 KDE idle
  shape. Lifetime `ps pcpu` remained high (`128/125/123`, then `120/119`),
  and two 10s per-thread deltas were borderline RED: total vCPU-thread CPU
  was 106.3% then 101.2%. All material ticks were on the six `xv6-qemu`
  vCPU threads (13.6-21.6% each), while the main loop, GTK/GLib, D-Bus,
  dconf, worker, and `qemu-sys:gdrv0` helper threads were 0%; sampled wait
  endpoints were mostly `kvm_vcpu_block` or runnable. Combined with the GDB
  histogram above, this is not an idle-pull hot loop, not an R7b userspace
  spinner, and not a virgl/GTK helper thread. Decision: M8 stays a tracked
  R7c/M8 vCPU/KVM idle-cadence follow-up and Q1 may be committed with this
  caveat; commit messages must not claim M8 is green or improved.

Correctness (high):

- **CR-1 (STAR — blocks closing P0).** `sched_eevdf.c` `eevdf_idle_pull`
  (~:1271): the lock-free precheck only attempts a pull when a remote CPU
  has `nr_running >= 2`, but the running task is dequeued from
  `nr_running` (`__dequeue_entity` ~:239 / `__eevdf_set_next_task`). The
  "1 hog + 1 queued waiter" shape — exactly the fast-wake-piles-on-prev-CPU
  case the P0 fix targets — reads as `nr_running == 1` and is never
  pulled, even though `__eevdf_idle_balance` itself would pull it (filter
  ~:1017 accepts `rnr==1` w/ load>0; under-lock recheck ~:1049 accepts
  `>=1`). IMPLICATION: the M1 "3/3 clean" replays may pass for another
  reason or because the repro never creates the 1+1 shape — re-examine
  before committing P0 as done. Fix: precheck on a remote `nr_running>=1`
  (some other CPU) and let `__eevdf_idle_balance` make the final call.
- **CR-2 (STAR — unprivileged kernel panic).** `syscall.c` (~:114,
  ARCH_SET_FS) and `trap.c` (~:189, usertrapret wrmsr of `trapframe->tp`,
  fed unvalidated by `clone(CLONE_SETTLS)`): a user-supplied non-canonical
  FS base is written via a raw `wrmsr` with no canonical check, raising
  `#GP` in ring 0 -> the kernel-exception path hits `panic("exception")`
  (`trap.c` ~:2273). Pre-existing but inside the P1-2b hunks. Fix: reject
  non-canonical (require `addr < TASK_SIZE`) -> `-EPERM`, both paths.
- **CR-3.** `trap.c` (~:987): removing the entry-time `rdmsr` re-sync
  breaks user `mov %fs` selector loads (GDT exposes user selector 0x23,
  `seg.c` ~:93). Effective FS base flip-flops with scheduling/migration;
  `ARCH_GET_FS` reports stale `tp`. Hurts `%fs`-reloading runtimes (Wine).
  The comment "only arch_prctl()/clone TLS changes it" is false.
- **CR-4.** `syscall.c` (~:114-116): ARCH_SET_FS updates the per-CPU
  FS_BASE cache non-atomically (no `push_off`). Safe ONLY because
  kernel-mode interrupts never preempt today; becomes cross-thread TLS
  corruption the moment kernel preemption lands. Fix: `push_off`/`pop_off`
  around the wrmsr + cache stores.

Perf / probe robustness (medium) — touches active M2/M8:

- **CR-5.** Starve/timer probe note-hooks run UNCONDITIONALLY on the
  hottest paths (`rq_lock`/`trylock`/`unlock` `rq.c` ~:179; `timer_root_lock`
  `timer.c` ~:176; per-tick; per-reschedule-IPI) even when
  `sched_starve_probe` is off (default) — SEQ_CST load of shared `ticks`
  plus atomics into packed `uint64[MAX_CPUS]` globals (false sharing). The
  enabled flag is already a cached `static int` the hooks never consult.
  Likely a current M2 (1.65-1.94us vs <1.5us) / M8 contributor. Fix:
  early-return on an exported cached flag; cache-line-aligned per-cpu
  storage. Do this before/with Q4 and re-measure M2.
- **CR-6.** `sched_starve_probe.c` (~:97): `struct ...snapshot rq[MAX_CPUS]`
  (~5.1KB = 80B*64) on the timer-IRQ stack, reserved by the `-O0` prologue
  on every CPU0 tick even when the probe is disabled -> IRQ-context
  stack-overflow risk. Fix: enabled-check first and/or a `static` per-cpu
  buffer.
- **CR-7.** `rq.c` (~:1398/1418): the probe reads remote
  `current_se->thread` and walks the wake_list lock-free from the IRQ. NOT
  a UAF today (thread frees are RCU-deferred, `thread.c` ~:446, and the
  probing CPU's frozen `rcu_timestamp` blocks GP completion while it sits
  in the IRQ), but it's an undocumented invariant, races concurrent list
  mutation (torn pid/name/depth), and becomes a real UAF if that path ever
  becomes preemptible. Fix: take the remote `rq_lock` or explicit
  `rcu_read_lock`.

Correctness (low / latent):

- **CR-8.** `start_kernel.c` (~:229): the idle loop's atomic `sti;hlt` is
  only race-free if the loop runs with IF=0; the old `intr_off` pairing
  was removed and nothing asserts it, so a first-iteration entry with IF=1
  is a self-healing lost-wakeup window. Fix: `intr_off()` + `assert(!intr_get())`
  before the `NEEDS_RESCHED` check.
- **CR-9.** `timer.c` (~:875 vs :816): the lock-free fast path advances
  `current_tick` while idle (old code left it stale), so a timer armed for
  the next jiffy can lose a 1-jiffy boundary race -> spurious `timer_add`
  `-1` (propagates via `timerfd_settime`) or <=1ms-late expiry.
- **CR-10.** `sched_eevdf.c` (~:994): `__eevdf_idle_balance` stamps its
  ~1ms cooldown on ENTRY, before trylock, so a contended attempt that
  pulls nothing still suppresses retries. Fix: stamp only on a real pull
  attempt/success.

Cleanup (non-blocking; note in lanes, do NOT gold-plate temporary R8
scaffolding): 4th open-coded `FIONREAD` 0x541B (`vfs_syscall.c` ~:8287 +
3 siblings) -> shared uabi header; two new hand-rolled hash tables
(`rq.c` ~:112, `rcu.c` ~:190) duplicate kmemleak's; dual ad-hoc lock-hold
instrumentation (`rq.c` + `timer.c`); x86-only IPI probe hooks (no riscv
parity); ~120 lines of duplicated chrome-unix trace printf blocks in
`sys_socket.c`; stray `}}` reformat `sched_eevdf.c` ~:1225.

## Runtime Audit — Scoreboard Reproduction (2026-07-03, in-VM)

Independent in-VM audit of the scoreboard. Two passes were run: an earlier
pass while WSLg host PulseAudio was DOWN (all GUI metrics blocked), and a
later pass after audio recovered (GUI metrics reproduced). The kernel work
was committed between passes — the audited build (`xv6.bin` 16:11) equals
the current committed tree (kernel HEAD `07efc74`; `ad7032d` idle-pull =
CR-1/CR-10, `57b09dc` FS_BASE cache = CR-2/CR-3/CR-4). Harness scripts
(new): `scripts/audit/plan-audit-nographic.expect` + `plan-audit-battery.sh`
— non-interactive (single in-guest script, one sentinel), avoiding the
bracketed-paste `[?2004h` regex fragility that hangs interactive drivers.

Build & boot: current tree compiles clean and boots clean (no panic) —
none of CR-1..CR-10 manifest as a build/boot break or a nographic-path
crash. CR-1 needs the GUI-load 1+1 shape; CR-2 needs a non-canonical
`arch_prctl` — neither exercised by these gates (still open in review).

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

User symptom (2026-07-02): launching Chromium (desktop icon or
`/bin/wayland-chromium`) shows no window. M9 tracks this lane.

Measured facts (fresh, post-P0 kernel):
- The browser process starts, connects to Wayland, registers D-Bus
  services; the GPU process then either loops forever on
  `eglCreateContext ES 3.0/2.0 failed with EGL_BAD_ATTRIBUTE` ->
  `SharedImageStub: unable to create context` (live-VM capture) or exits
  (`GPU process exited unexpectedly: exit_code=512` ~24s in, controlled
  repro). No primary surface buffer is ever committed -> no window.
- Deterministic repro: `KDE_SMOKE_REDUCER=chromium-video
  KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1 timeout 900
  scripts/gpu/kde-plasma-desktop-smoke.expect` -> FAIL
  `chromium-video-chrome-crash-regression`. Archive:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260702T222900Z-chromium-invisible-window-gpu-crash-repro/`.
- This failure class predates the P0 fix (identical archived signatures
  2026-06-30/07-01: renderer role never stabilizes, `currentSrc` empty,
  no MP4 open, Wayland `no-primary-surface-buffer`). NOT a P0 regression;
  NOT the R5 KWin crash.
- Already exonerated by prior reducers (do NOT redo): EGL robust-access
  attribute rejection (Linux+virgl control rejects identically), raw
  AF_UNIX stream/seqpacket SCM_RIGHTS, SO_PASSCRED, half-close SCM,
  EPOLLONESHOT rearm (`webkitabitest chromium-ipc` 4/4 PASS on xv6),
  RELA/loader bytes (`chrome-rela-probe` chains), execve latency.

Direction (continue where the archived lane stopped — history file
"Next Chromium Reducer" sections):
1. The frontier is browser->child Mojo/NetworkService/renderer ADMISSION:
   children launch, log "15 seconds with no connection", exit cleanly,
   and the renderer role never stabilizes (`renderer_pids=0`,
   `exec_renderer=0`). Build the next reducer around the Mojo invitation
   handshake: trace the socketpair fd numbers passed via `--mojo-platform-
   channel-handle` / `MOJO_*` env into the child, then verify on xv6 that
   the inherited fd survives exec with the same number, CLOEXEC state,
   and non-blocking flags as on Linux (suspect classes: fd-number
   collision during zygote-less spawn, O_NONBLOCK/FD_CLOEXEC drift across
   `execve`, or fd inheritance ordering).
2. Instrument with the existing gated traces (`chrome_lifecycle_trace=1`,
   `chrome_exec_syscall_trace=1`, `chrome_media_fd_trace=1`,
   `chrome_epoll_trace=1`) — see
   `build-x86_64/kde-chromium-launch-diagnose-current.expect` for a
   staged harness that already assembles these.
3. Gate: M9 = the launch-only reducer reaches PASS (browser + GPU process
   + at least one stable renderer, no crash-regression classification),
   then a manual `/bin/wayland-chromium about:blank` from the serial
   console shows a window (verify via guest framebuffer capture or
   `scripts/gpu/capture-host-screen.sh`).

Launcher-state warning: `scripts/image/wayland-chromium-launcher.c`
carries an UNCOMMITTED default flip of `WAYLAND_CHROMIUM_AUTO_GL_FLAGS`
(auto GL flags default OFF; stock default was ON via
`!env_disabled_value`). Evidence says it is not the crash cause (archived
runs fail identically with flags unset), but it changes every manual
launch's GL config and was left undocumented — which cost a full
re-diagnosis of the user's symptom. Decision required at R8 start: either
revert to the stock default (preferred until an A/B proves the clean
baseline is better on xv6) or commit it with justification here. Any
change to the launcher requires rebuilding the image (`--target user` +
`rootfs-refresh` / world) — the launcher lives in fs.img, not the kernel.

2026-07-03 R8 launcher-default status: resolved the inherited launcher
state by restoring the stock default (`WAYLAND_CHROMIUM_AUTO_GL_FLAGS`
auto flags ON unless explicitly disabled); `git diff --
scripts/image/wayland-chromium-launcher.c` is now empty. Because the
launcher lives in fs.img, refreshed the image with `cmake --build
build-x86_64 --target user -j$(nproc)` and `cmake --build build-x86_64
--target rootfs-refresh -j$(nproc)`, then ran the required regression
gates. KDE desktop interaction with active sampling passed at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T013616Z-r8-launcher-stock-default-kde-active-sample-pass/`
(`first_visible_ms=14623`, `konsole_wait_ms=1888`, direct launch
`elapsed_ms=3059`, boot cmdline verified). Chromium launch-only exercised
Chromium and stayed in the existing M9 failure class, not a new one:
archive
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T013844Z-r8-launcher-stock-default-chromium-launch-only-crash-regression/`
has sampler `status=DONE result=PASS`, final
`chromium-video-chrome-crash-regression`, and the same
`EGL_BAD_ATTRIBUTE` / `SharedImageStub: unable to create context` loop.
Refreshed-image late-settle M8/spinner proof passed at
`build-x86_64/desktop-bottleneck-profile/20260703T013947Z-r8-launcher-stock-default-late-settle-m8-spinners/`
after one archived harness prompt-miss attempt in its
`attempt1-boot-prompt-miss/` subdirectory; final idle host CPU samples
were `104%`, `100%`, `95.9%` and post-app samples were `100%`, `99.7%`,
`98.7%`. `procstat-summary.txt` shows no persistent R7b-style spinners
(`kactivitymanage=10` idle ticks / `13` post-app ticks over 10s;
`kate=79`, `kwrite=23` post-app ticks). R8 remains open at the
browser->child Mojo/NetworkService/renderer-admission frontier described
above.

2026-07-03 R8 child-admission fdtable status: ran a default-off diagnostic
Chromium-video pass with `chrome_exec_fdtable_trace=1`,
`chrome_unix_ipc_payload_trace=1`, `chrome_epoll_trace=1`, and related
R8 traces enabled; archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T015746Z-r8-child-admission-fdtable-crash-regression/`.
Boot cmdline was verified. Final status stayed in the existing
`chromium-video-chrome-crash-regression` class, but the immediate symptom
shifted away from the earlier EGL loop in this run: `gpu_init_error_count=0`,
no `EGL_BAD_ATTRIBUTE` / `SharedImageStub` match, `network_restart_count=2`,
`no_connection_count=7`, fast census `renderer_pids=0`, `gpu_process_pids=2`,
`utility_pids=3`, and kernel lifecycle `exec_renderer=0`, `exec_gpu=2`,
`exec_utility=3`. The important fdtable result: browser/ThreadPool sent
renderer launch payloads to the zygote with `cmsg=7` and `type=renderer`
(for example run.log around lines 3590-3607 and 3810-3832); the zygote
received all seven SCM fds, then forked renderer-intended children. In the
pid 386 sample, SCM indices 0-2 became fds 13/14/15 with `cloexec=0` and
flags `0x2`, `0x802`, `0x2`, and clone-child fdtable preserved those exact
socket objects; the child sent `CHILD_PING` on fd 13 and its ChildIOT epolled
fd 14, received Mojo payloads, received SCM fds, and sent SCM back. Similar
fd shapes were recorded for later no-connection children (397, 417, 458).
This makes a simple fd-number/CLOEXEC inheritance loss or initial SCM
delivery loss unlikely for the renderer launch path. R8 now narrows to the
post-zygote-fork child bootstrap / Mojo admission handshake after
`CHILD_PING`: identify the exact communication fd roles from the zygote
process-launch payload, compare a Linux control for the same payload/fd
mapping, and trace why the child-side browser connection never reaches a
stable renderer role before the 15s no-connection self-termination.

2026-07-03 R8 zygote-admission parser update: added a parser-only
post-evidence line (`zygote_admission_summary`) to
`scripts/gpu/kde-plasma-desktop-smoke.expect`. It correlates zygote
process-launch payloads, SCM fd installation, clone-child fdtable entries,
`CHILD_PING`, browser credential receipt, browser ack, zygote ack, and
ChildIOT activity. Offline replay against the archived fdtable run above
reported `launch_count=7`, `type_counts=renderer:3,utility:4`,
`renderer_with_child=3`, `no_connection_children=5`,
`renderer_no_connection_children=2`, `utility_no_connection_children=3`,
`child_ping_count=7`, `browser_ping_cred_count=7`, `browser_ack_count=7`,
`zygote_ack_count=7`, `child_iot_count=7`, and both
`full_fd_match_launch_count=7` and `full_file_match_launch_count=7`.
The replayed contexts show the repeated fd role shape: SCM index 0 -> fd 13
(`CHILD_PING`/browser bootstrap), index 1 -> fd 14 (ChildIOT/Mojo traffic in
the failing renderer children), index 2 -> fd 15 (inherited browser-side
socket), followed by V8 snapshot/tmp shared-file fds. This preserves the
current R8 conclusion: the next evidence target is post-ack child bootstrap
and Mojo admission semantics, not raw SCM delivery or fdtable inheritance.

2026-07-03 R8 full-payload token diagnostic: extended the existing
default-off `chrome_unix_ipc_payload_trace=1` path in
`kernel/kernel/lwip_port/sys_socket.c` to scan full captured sendmsg
payloads for Chromium launch argv tokens and to bound values using the
Chromium 32-bit argv-string length word. This is diagnostic-only; no
runtime default changed. `git diff --check`, `git -C kernel diff --check`,
and `cmake --build build-x86_64 --target kernel -j$(nproc)` passed. Proof
run:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T023020Z-r8-lengthword-token-payload-chromium-launch-only-sampler-timeout/`.
Boot cmdline was verified and included the lean R8 trace set
(`chrome_lifecycle_trace=1 chrome_exec_fdtable_trace=1
chrome_unix_ipc_trace=1 chrome_unix_ipc_payload_trace=1
chrome_syscall_trace_child_processes=1 chrome_epoll_trace=1 kasan=0
kmemleak=0 klog=0`). The wrapper again ended as
`chromium-video-sampler-completion-timeout`; post-evidence, not the wrapper
label, is the truth: `launch_evidence_status=PASS`,
`sampler_completion_seen=0`, and final post-evidence
`status=FAIL reason=sampler-status-incomplete`.

The corrected extractor now reports complete launch fields: utility storage
payload line 1600 has `metrics=...524288` and
`field_trial=...262144`; renderer payload lines 2467/2718/2816 have
`renderer_client_id`/`gpu_client_id` pairs 5/5, 7/7, and 6/6,
`metrics=...2097152`, complete `field_trial=...262144`, and complete
`time_ticks=-1783045713636625`. All observed zygote launch payloads still
have `mojo_channel_handle=-`; this Chromium shape uses inherited/SCM fds
and renderer/gpu client ids, not `--mojo-platform-channel-handle`.

R8 narrowed again, not solved. `zygote_admission_summary` reported
`launch_count=3`, `type_counts=renderer:2,utility:1`,
`renderer_with_child=2`, `renderer_no_connection_children=0`,
`utility_no_connection_children=1`, `browser_ping_cred_count=3`,
`browser_ack_count=3`, `zygote_ack_count=2`, `child_iot_count=2`, and
`full_fd_match_launch_count=2/full_file_match_launch_count=2` (the storage
utility context still showed `fd_matches=7,file_matches=7` but duplicated
SCM receipt). Renderer-intended children 448 and 461 sent `CHILD_PING`;
renderer child 448/ChildIOT also received fd-14 Mojo traffic, including
large `chrome.mojom.RendererConfiguration` and font/config payloads. The
process sampler still reported `renderer_seen=0` and the final visible
content sampler never completed. The only "15 seconds with no connection"
exits were PID 371 (NetworkService utility, full argv/fd context, clean
exit 0) and PID 380 (storage utility/zygote child, clean exit 0). GPU init,
GPU config, GPU exit, Chromium fault, and Chromium int3 counts were all
zero; the host log only showed the known Vulkan/Wayland compatibility
warning plus missing cpufreq/proc files after teardown. Next R8 evidence
should follow the Mojo browser-control protocol after `CHILD_PING` and
ChildIOT traffic, not fd numbering, CLOEXEC, raw SCM delivery, or EGL.

2026-07-03 R8 Mojo ChildIOT parser update: added a parser-only
`mojo_child_iot_summary` post-evidence line to
`scripts/gpu/kde-plasma-desktop-smoke.expect`. It scans existing
`chrome-unix-ipc-payload` lines from `run.log` for `Chrome_ChildIOT`
traffic, totals payload counts/bytes/fds per child TGID, records whether
that child sent `CHILD_PING`, and extracts sanitized `*.mojom.*` interface
names from the first 512 printable payload bytes. This introduces no kernel
or image behavior change. Offline replay against
`20260703T023020Z-r8-lengthword-token-payload-chromium-launch-only-sampler-timeout`
reported `payload_lines=94`, `recv_lines=90`, `send_lines=4`,
`child_iot_tgid_count=4`, and five interface families:
`chrome.mojom.RendererConfiguration:1`,
`content.mojom.RendererVariationsConfiguration:24`,
`content.mojom.SyntheticTrialConfiguration:1`,
`metrics.mojom.ChildHistogramFetcherFactory:2`, and
`visitedlink.mojom.VisitedLinkNotificationSink:1`. The most useful context
was renderer-side TGID 431: `child_ping=1`, `recv=54`, `bytes_in=266984`,
`fds=14`, with `chrome.mojom.RendererConfiguration`,
`content.mojom.RendererVariationsConfiguration`, and
`visitedlink.mojom.VisitedLinkNotificationSink` observed before the sampler
timed out. TGID 448 had `child_ping=1` but only the initial small ChildIOT
exchange in the captured window; TGID 461 had `CHILD_PING` in the run log
but no sampled ChildIOT payload before harness timeout. Next R8 comparison
should use this summary to decide which renderer child diverges from Linux
after `RendererConfiguration`, rather than adding more raw payload volume.

2026-07-03 R8 Linux-control tracer prep: existing Linux Chromium control
archives under `build-x86_64/linux-chromium-control/20260628T151255Z-...`
already used `chromium-egl-trace-preload.so`, but their IPC samples only
show short binary heads and do not expose `*.mojom.*` interface names.
Extended the default-off preload tracer
(`scripts/image/chromium-egl-trace-preload.c`, active only when
`CHROMIUM_EGL_TRACE=1`) to scan full traced IPC iovecs and emit
`mojom_count=N mojom="a|b|..."` on `ipc_sendmsg`/`ipc_recvmsg` lines.
Validation: direct host compile with the same staging flags passed, a tiny
AF_UNIX `sendmsg`/`recvmsg` preload smoke test emitted
`chrome.mojom.RendererConfiguration|content.mojom.RendererVariationsConfiguration`,
`git diff --check` and `git -C kernel diff --check` passed, and
`cmake --build build-x86_64 --target host-gui-runtime -j$(nproc)` restaged
the updated preload. No VM run was performed for this tracer-only change.
Next Linux control should run Chromium with `CHROMIUM_EGL_TRACE=1
CHROMIUM_EGL_TRACE_LOG=/tmp/chromium-egl-trace.log
LD_PRELOAD=/mnt/xv6/build-x86_64/host-gui-runtime/chromium-egl-trace-preload.so`
and compare renderer `mojom=` sequences against the xv6
`mojo_child_iot_summary`, focusing on the first divergence after
`chrome.mojom.RendererConfiguration`.

2026-07-03 R8 Linux-control Mojo result + xv6 browser-milestone parser: reran
the existing Linux Chromium control with the restaged preload and an absolute
archive base so trace logs stayed self-contained at
`/home/es/xv6-os/build-x86_64/linux-chromium-control/20260703T024800Z-r8-mojom-linux-control-absolute/passive-preload`.
Use absolute archive bases for future runs or fix the archived script first:
the first relative-path attempt split trace logs under
`build_x86_64/build-x86_64/...` because the script `cd`s into
`$repo/build_x86_64`. The absolute run reached the video result before the
outer timeout killed Chromium (`status.txt` has `exit_status=143`):
`PERF-VIDEO metrics presented=707 presentedFPS=47.06 decoded=906
decodedFPS=60.31 dropped=7` and `event:ended ... presented=755 decoded=960
dropped=7`. It produced 81 trace logs plus `mojom-summary.txt`; that summary
reported 58 `mojom=` event lines, all browser-side, with 33 unique interfaces.
Linux renderer children still performed binary initial IPC and SCM fd receipt
(`pid=3986908`: `recvmsg=7`, `send=6`, `recv_bytes=1408`,
`scm_rights_fds_total=10`; `pid=3986922`: `recvmsg=8`, `send=8`,
`recv_bytes=1632`, `scm_rights_fds_total=10`), but the preload did not see
child-side ASCII `*.mojom.*` names there. The useful positive Linux
milestones were browser-side renderer/page/GPU bindings:
`content.mojom.RenderMessageFilter`, `blink.mojom.LocalFrameHost`,
`blink.mojom.LocalMainFrameHost`, `gpu.mojom.GpuChannel`, and
`font_service.mojom.FontService`. Linux also logged GPU-process init exits
while the video still passed, so do not treat that control as new EGL
evidence.

Added parser-only `chromium_mojo_browser_milestone_summary` to the KDE smoke
post-evidence path; it scans existing `chrome-unix-ipc-payload` lines for the
Linux milestone interfaces and does not change kernel, image, or launcher
behavior. Offline replay against
`20260703T023020Z-r8-lengthword-token-payload-chromium-launch-only-sampler-timeout`
reported `payload_lines=317`, `parsed_payload_lines=312`,
`interface_lines=35`, `unique_interface_count=10`, and milestone counts:
`content.mojom.RenderMessageFilter:0`, `blink.mojom.LocalFrameHost:0`,
`blink.mojom.LocalMainFrameHost:0`, `gpu.mojom.GpuChannel:1`,
`font_service.mojom.FontService:0`, `chrome.mojom.RendererConfiguration:1`,
`content.mojom.RendererVariationsConfiguration:24`,
`visitedlink.mojom.VisitedLinkNotificationSink:1`, and
`metrics.mojom.ChildHistogramFetcherFactory:2`. Interpretation: xv6 reaches
renderer ChildIOT configuration traffic and one browser-side GPU-channel
binding, but not the Linux browser-side renderer/page/font milestones. Next
R8 evidence should use this parser in the launch-only reducer and locate
whether browser-side renderer/page bindings are never sent, are sent but not
delivered, or occur only after the current sampler timeout. Do not add more
raw payload volume before answering that milestone question.

2026-07-03 R8 fresh xv6 parser runs: the parser is now wired into current
post-evidence. A launch-only reducer with the lean R8 trace set archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T025805Z-r8-browser-milestone-parser-launch-only-pass/`
returned wrapper `status_code=0` and post-evidence `status=PASS
reason=launch-only`, but it did not exercise the failing path:
`zygote_admission_summary launch_count=0`, `mojo_child_iot_summary
payload_lines=0`, and `mojo_browser_milestone_summary interface_lines=0`.
Treat that archive only as a parser-wiring/process-liveness proof, not as
Chromium visibility or renderer-admission closure.

The useful full-video reducer with the same verified cmdline is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T030201Z-r8-browser-milestone-full-video-chrome-crash-regression/`.
Wrapper status was `status_code=8` with label
`chromium-video-capture-status-timeout`, but the post-evidence file is more
specific and should be the judge: `capture_status=status=DONE result=PASS
samples=4`, `capture_result_pass=1`, `scanout_sample_current_summary
status=PASS`, and final `status=FAIL reason=chrome-crash-regression`. The
generated frame conversions `chromium-video-00.png`/`03.png` show the KDE
desktop with no Chromium window, so nonblack capture is not a visibility pass.
The failure is not the earlier EGL-init loop in this run:
`gpu_init_error_count=0`, `gpu_config_error_count=0`, `gpu_exit_error_count=0`,
and `gl_request_error_count=0`. Host Chromium stderr instead shows one
NetworkService restart and repeated 15s no-connection child terminations
(`no_connection_count=6`, pids `439,462,542,553,566,574`).

This full run refines, and partly supersedes, the previous "no browser
milestones" hypothesis. `zygote_admission_summary` had `launch_count=5`
(`renderer:3,utility:2`), `renderer_with_child=3`, `child_ping_count=5`,
`browser_ping_cred_count=5`, `browser_ack_count=5`, `zygote_ack_count=5`,
`child_iot_count=5`, and full fd/file matches for all five launches.
`mojo_child_iot_summary` had `payload_lines=144`; the main renderer-intended
child TGID 533 saw `payloads=108`, `recv=107`, `bytes_in=344344`, fd 14,
and `chrome.mojom.RendererConfiguration`,
`content.mojom.RendererVariationsConfiguration`, and
`visitedlink.mojom.VisitedLinkNotificationSink`. The browser-side milestone
summary had `payload_lines=520`, `interface_lines=56`,
`unique_interface_count=23`, and present milestones
`blink.mojom.LocalFrameHost` and `font_service.mojom.FontService` plus the
ChildIOT configuration families; it still missed
`content.mojom.RenderMessageFilter`, `blink.mojom.LocalMainFrameHost`, and
`gpu.mojom.GpuChannel`. Exact interface counts from the archive include
`content.mojom.RendererVariationsConfiguration:29`,
`storage.mojom.StorageService:8`, `network.mojom.NetworkService:2`,
`ukm.mojom.UkmRecorderFactory:2`,
`memory_instrumentation.mojom.CoordinatorConnector:2`,
`content.mojom.FrameHost:1`, `blink.mojom.LocalFrameHost:1`,
`blink.mojom.NonAssociatedLocalFrameHost:1`,
`blink.mojom.DomStorageProvider:1`,
`blink.mojom.RendererAudioOutputStreamFactory:1`, and
`font_service.mojom.FontService:1`.

Next R8 evidence should move later than initial ChildIOT/fd inheritance and
earlier than broad rendering work: explain why renderer-intended children
reach partial browser frame-host Mojo but still never appear as stable
renderer roles (`process_summary renderer_seen=0`, `role_lifecycle_summary
renderer_samples=0`) and why several children self-terminate after 15s with
no connection. Compare the missing Linux milestones
`content.mojom.RenderMessageFilter`, `blink.mojom.LocalMainFrameHost`, and
`gpu.mojom.GpuChannel` against the successful Linux control, and correlate
their absence with the NetworkService restart/no-connection sequence.

2026-07-03 R8 renderer-admission correlation parser: added parser-only
`renderer_admission_milestone_summary` to
`scripts/gpu/kde-plasma-desktop-smoke.expect`, wired into Chromium-video
post-evidence. This changes no kernel, image, launcher, or runtime default.
Offline replay against
`20260703T030201Z-r8-browser-milestone-full-video-chrome-crash-regression`
reported `launch_count=5` (`renderer:3,utility:2`),
`renderer_with_child=3`, `renderer_child_iot_count=3`,
`renderer_child_iot_with_config=1`, `renderer_sampled_any_role=3`, but
`renderer_sampled_as_renderer=0`, `process_renderer_samples=0`, and
`process_renderer_pid_count=0`. The host order was
`no_connection:439|no_connection:462|network_restart|no_connection:542|no_connection:553|no_connection:566|no_connection:574`;
both no-connection renderer children (`542`, `553`) occurred after the
NetworkService restart and each saw only the tiny initial ChildIOT exchange
(`iot_payloads=2`, no `*.mojom.*` interfaces). The primary renderer-intended
child `533` did not hit the 15s no-connection log in the captured window and
received substantial ChildIOT traffic (`iot_payloads=108`,
`bytes_in=344344`, `chrome.mojom.RendererConfiguration`,
`content.mojom.RendererVariationsConfiguration`,
`visitedlink.mojom.VisitedLinkNotificationSink`), but the process sampler
still classified it as `zygote` for samples `56-62`, never `renderer`.

Interpretation: xv6 now reaches more than fd inheritance and initial
configuration; it also reaches browser-side partial frame/font milestones
(`blink.mojom.LocalFrameHost`, `font_service.mojom.FontService`, plus
`content.mojom.FrameHost`, `blink.mojom.NonAssociatedLocalFrameHost`,
`blink.mojom.DomStorageProvider`, and
`blink.mojom.RendererAudioOutputStreamFactory`). It still does not reach the
successful Linux milestones `content.mojom.RenderMessageFilter`,
`blink.mojom.LocalMainFrameHost`, or `gpu.mojom.GpuChannel`. Next R8 work
should focus on the post-`RendererConfiguration` renderer admission/role
transition: compare the Linux order of those three missing milestones, then
trace xv6 child fd 13/14/15 peer close/readiness around renderer child 533
and the post-restart children 542/553. Do not reopen EGL or raw SCM/fdtable
inheritance without new evidence.

2026-07-03 R8 renderer fd-lifecycle parser update: extended the same
parser-only `renderer_admission_milestone_summary` to record renderer-child
fd 13/14/15 activity, last socket state, epoll watches, and clone-to-exit
time from existing run logs. Offline replay against the same full-video
archive produced the next R8 split. Primary renderer-intended child `533`
registered fd 13 and fd 14 with epoll; fd 13 only sent `CHILD_PING` (11
bytes), fd 14 stayed active through the capture (`226` traced IPC events,
`106` recv completions, `344368` traced bytes in, `272` bytes out,
`7` SCM receives), and the last fd-14 state was still healthy:
`sk_shutdown=0x0`, `sk_err=0`, `peer_shutdown=0x0`, `peer_err=0`,
`ready=0`. No child-side fd 15 activity was traced. Child `533` had no
recorded exit before capture end, yet was still sampled only as `zygote`.
The two post-NetworkService-restart renderer children `542` and `553`
also sent `CHILD_PING`; each then received one 52-byte fd-13 response and
only the initial fd-14 ChildIOT bootstrap exchange (`184` bytes in, `168`
bytes out, one SCM receive), had no fd-15 activity, and exited cleanly as
`child+zygote` after about `51.5s` / `51.4s` from clone.

Linux-control order from `mojom-summary.txt` is now explicit for comparison:
on the first page/renderer browser fd, `content.mojom.RenderMessageFilter`
appears before `blink.mojom.LocalFrameHost`, which appears before
`blink.mojom.LocalMainFrameHost` (`fd=115` lines 466, 478, 502 in the
summary); GPU-channel fds show `gpu.mojom.GpuChannel` immediately before
`font_service.mojom.FontService` (`fd=133` lines 715/716, repeated on
`fd=134` lines 828/830). xv6's full-video run instead sees the primary child
consume lots of fd-14 renderer configuration while browser-side fd 123 gets
`font_service.mojom.FontService` and later `blink.mojom.LocalFrameHost`, but
never the Linux-positive `RenderMessageFilter`, `LocalMainFrameHost`, or
`GpuChannel`. The fd-13 payload is now classified enough to guide the next
probe: the post-restart renderer children first send `CHILD_PING`, then their
last fd-13 payload is a 52-byte binary response ending in `UTC` (hex head
`3000000037000000...` for child `542`, `300000003b000000...` for `553`).
The Linux renderer controls do not stop at that shape; renderer child logs show
larger early IPC on their bootstrap fd (for example 312-byte recv with
SCM_RIGHTS, then 64/112/200-byte messages with more SCM fds), while the
browser-side trace later reaches the `RenderMessageFilter -> LocalFrameHost ->
LocalMainFrameHost` sequence.

2026-07-03 R8 fd-13 UTC classification update: extended
`renderer_admission_milestone_summary` again, still parser-only, to match
52-byte child fd-13 `UTC` receives back to browser `sandbox_ipc_thr` sends by
identical payload hex and to surface `device.mojom.TimeZoneMonitor` /
`Etc/UTC` breadcrumbs. Offline replay of the full-video archive now reports
`browser_utc_payload_count=4` on browser fds `56,151,52,168`,
`child_fd13_utc_count=2`, `renderer_fd13_utc_count=2`, and
`control_milestones=device.mojom.TimeZoneMonitor`. The two post-restart
renderer children match exactly: child `542`'s 52-byte fd-13 recv matches
browser fd `151`; child `553`'s matches browser fd `52`. The same browser
UTC payload shape is also sent to non-renderer/storage-style children, while
primary renderer-intended child `533` has no fd-13 UTC receive and instead
continues with the large fd-14 ChildIOT/config stream; that stream later
contains the `Etc/UTC` timezone detail. Current interpretation: the fd-13
UTC response is a timezone/control broadcast, not the missing renderer
admission transition. The live primary renderer path is not dying from
child-side fd shutdown/error or fd 15 traffic loss in the captured window; it
is stuck before the browser receives/records the renderer message-filter and
main-frame/GPU-channel binding sequence. Next R8 step: identify the xv6
browser-side fd/peer that should correspond to Linux fd `115` and explain why
xv6 records `LocalFrameHost`/font service traffic but not the Linux-positive
`RenderMessageFilter -> LocalFrameHost -> LocalMainFrameHost` progression.

2026-07-03 R8 fresh xv6 full-video trace attempt: reran the full
Chromium-video reducer with `KDE_SMOKE_CHROMIUM_EGL_TRACE=1` and the lean R8
kernel trace set (`chrome_lifecycle_trace=1 chrome_exec_fdtable_trace=1
chrome_unix_ipc_trace=1 chrome_unix_ipc_payload_trace=1
chrome_syscall_trace_child_processes=1 chrome_epoll_trace=1 kasan=0
kmemleak=0 klog=0`). Archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T033333Z-r8-xv6-full-iovec-egl-trace-chrome-crash-regression/`.
Wrapper status stayed `status_code=8` /
`chromium-video-capture-status-timeout`, while post-evidence is the more useful
judge: `capture_status=status=DONE result=PASS samples=4`, final
`status=FAIL reason=chrome-crash-regression`, `gpu_init_error_count=0`,
`gpu_config_error_count=0`, `gpu_exit_error_count=0`, and
`gl_request_error_count=0`. The failure still has one NetworkService restart
followed by no-connection child terminations, and process evidence still has no
stable renderer role (`renderer_seen=0`, `renderer_samples=0`,
`renderer_pids=0`). Important caveat: although the current preload source emits
`mojom_count=` / `mojom="..."`, this guest archive's
`chromium-egl-trace.log` only has the older `phase=ipc_*` sample format. Treat
the run as kernel 512-byte payload evidence plus preload IPC/SCM volume, not as
full-iovec Mojo-name proof; refresh/restage the guest preload or add an
equivalent bounded kernel scanner before relying on full-iovec names from xv6.

2026-07-03 R8 browser-fd milestone parser: added parser-only
`browser_fd_milestone_timeline_summary` to current post-evidence. Offline
replay of the old full-video archive collapses the useful browser side to fd
`123`: `font_service.mojom.FontService`, `device.mojom.TimeZoneMonitor`,
`blink.mojom.NonAssociatedLocalFrameHost`,
`blink.mojom.RendererAudioOutputStreamFactory`,
`blink.mojom.DomStorageProvider`, `blink.mojom.LocalFrameHost`, then
`content.mojom.FrameHost`; replay of the launch-only archive stays zero
(`browser_fd_count=0`). Replay of the fresh
`033333Z` archive changes the missing-milestone interpretation: browser fd
`129` reaches `gpu.mojom.GpuChannel` at `231520528279`, then
`device.mojom.TimeZoneMonitor`, `blink.mojom.NonAssociatedLocalFrameHost`,
`blink.mojom.LocalFrameHost`, renderer-audio, and `content.mojom.FrameHost`;
fd `136` reaches another `gpu.mojom.GpuChannel`. Therefore `GpuChannel` is not
a stable xv6-missing milestone. The stable missing browser-side admission edge
is now narrower: `content.mojom.RenderMessageFilter` and
`blink.mojom.LocalMainFrameHost` remain absent, while `font_service` varies by
run. Primary renderer-intended child `514` still stays sampled as `zygote`,
does not exit in the captured window, and keeps a healthy large fd-14
ChildIOT/config stream. Next R8 proof should correlate xv6 browser fd
`129`/`136` against Linux fd `115`/`133`/`134`, especially why Linux reaches
`RenderMessageFilter -> LocalFrameHost -> LocalMainFrameHost` and xv6 only
reaches partial frame/GPU traffic.

2026-07-03 R8 browser-fd/Linux correlation parser: added parser-only
`browser_fd_linux_correlation_summary` to current Chromium-video
post-evidence and replayed it offline against the `033333Z` archive:
`build-x86_64/r8-browser-fd-correlation/20260703T064021Z-parser-replay/browser-fd-linux-correlation-summary.txt`.
The new line correlates the xv6 browser fd shape directly against the Linux
control. xv6 fd `129` is the Linux fd `115` analogue: it reaches
`blink.mojom.LocalFrameHost`, plus partial-frame traffic
`content.mojom.FrameHost`, `blink.mojom.NonAssociatedLocalFrameHost`, and
`blink.mojom.RendererAudioOutputStreamFactory`; its last parsed state is
healthy (`sk_shutdown=0x0`, `sk_err=0`, `peer_shutdown=0x0`, `peer_err=0`,
`ready=0`). The missing fd-115 edge is now explicit:
`content.mojom.RenderMessageFilter` and `blink.mojom.LocalMainFrameHost`.
xv6 fd `136` is the GPU-side analogue for Linux fd `133`/`134`: it reaches
`gpu.mojom.GpuChannel`, but not `font_service.mojom.FontService`, and its
last parsed state shows peer shutdown (`peer_shutdown=0x2`) after the GPU
payload. Conclusion: the stable M9 split is not fd inheritance, raw
AF_UNIX/SCM transport shape, or an early fd-129 socket failure; it is the
browser/renderer admission path after partial frame setup, where Linux emits
`RenderMessageFilter -> LocalFrameHost -> LocalMainFrameHost` and xv6 stops
at partial frame plus GPU-channel traffic. Next R8 work should target that
post-`LocalFrameHost` wait/registration edge and the primary child `514`
fd-14 stream/lifecycle correlation, without adding more raw payload volume
until the missing sender/wait condition is identified.

2026-07-03 R8 primary-child overlap read: existing `033333Z` evidence already
rules out a dead primary renderer socket for the fd-129 split. The zygote
admission line maps renderer launch id `2` to child `514` with all seven SCM
fds and file identities matched; the renderer-admission line shows child
`514` has no host "no connection" kill, no exit in the capture window, and a
healthy fd-14 stream (`iot_payloads=117`, `iot_bytes_in=338112`,
`fd14 state=...sk_shutdown=0x0...peer_shutdown=0x0...ready=0`). The timeline
overlaps the fd-129 milestones: child `514` cloned at `212168964637`, fd-14
traffic starts at `216232703315` and continues through `360739384018`, while
fd `129` reaches `GpuChannel` at `231520528279`,
`NonAssociatedLocalFrameHost` at `257678049557`, `LocalFrameHost` at
`259523332593`, renderer audio at `292072248233`, and `FrameHost` at
`322093218009`. Host stderr kills later short-lived children `540`, `551`,
`564`, and `572`, but not `514`. Treat the `sampled_roles=zygote` label on
`514` as a role-classification limitation until proven otherwise: the process
has active ChildIOT/Compositor traffic and is the only renderer-intended child
with the long config stream. Next proof should identify what browser-side
state or child-side signal normally causes `RenderMessageFilter` and
`LocalMainFrameHost` after this partial-frame overlap, not re-prove SCM
delivery or child liveness.

2026-07-03 R8 refreshed full-iovec preload proof: before rerunning, verified
the rootfs copy of
`/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so` was stale
and lacked the new `mojom_count` format, then refreshed `fs.img` with
`rootfs-refresh`; the guest preload hash then matched the host `.so`
(`f7d657f0971b501f09289697f03e0964a8e26f0a84ea5dbb4986312181225a3d`) and
`strings` showed `mojom_count=%d mojom="%s"`. Archived proof:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T065141Z-r8-xv6-full-iovec-restaged-preload-capture-status-timeout/`.
The wrapper exits `status_code=8`
(`chromium-video-capture-status-timeout`), but the direct capture status file
and post-evidence both report `status=DONE result=PASS samples=4`; frame
capture was slow (`capture_timing_summary max_ms=31080.473`) and mostly static
(`unique_hash_count=2`, no perf-media PASS). Booted cmdline includes the lean
R8 flags
`chrome_lifecycle_trace=1 chrome_exec_fdtable_trace=1 chrome_unix_ipc_trace=1
chrome_unix_ipc_payload_trace=1 chrome_syscall_trace_child_processes=1
chrome_epoll_trace=1 kasan=0 kmemleak=0 klog=0`.

The refreshed `chromium-egl-trace.log` now has full-iovec decoded Mojo names.
On browser fd `129`, it records `content.mojom.RenderMessageFilter` at
`chromium-egl-trace.log:348` and `blink.mojom.LocalMainFrameHost` at
`:454`, plus `blink.mojom.LocalFrameHost` at `:361`; browser fds `136` and
`137` record `gpu.mojom.GpuChannel` and more `RenderMessageFilter` traffic.
This corrects the previous fd-129 interpretation: the kernel
`browser_fd_linux_correlation_summary` still reports
`RenderMessageFilter|LocalMainFrameHost` missing because it only scans the
512-byte kernel payload samples, not because the full browser iovec lacks
those strings. The R8 failure remains real (`renderer_launch_count=3`,
`renderer_no_connection_children=2`, `process_renderer_samples=0`,
`no_connection_count=6`, `network_restart_count=1`), but the next proof should
move past "missing browser-side Mojo names" and instead correlate the
full-iovec fd-129/fd-136/fd-137 messages with the child fd-14 streams,
renderer role sampling, and "15 seconds with no connection" exits.

2026-07-03 R8 full-iovec browser/child correlation: generated the requested
offline correlation summary from the refreshed archive at
`build-x86_64/r8-full-iovec-correlation/20260703T065802Z-restaged-preload-browser-child-correlation/full-iovec-browser-child-correlation-summary.txt`.
The important correction is socket-address reuse: fd `60`/child `431` and fd
`129`/child `509` reuse the same kernel socket object addresses, but their
lifetimes do not overlap. The active fd-129 pair is browser `266:129` <-> the
primary renderer-intended child `509:14`; fd `60` <-> child `431:14` is only
the earlier recycled, non-overlapping pair. On that active fd-129 stream, the
full iovec contains the Linux-positive
`RenderMessageFilter -> LocalFrameHost -> LocalMainFrameHost` family, while
child `509` is launch id `2`, type `renderer`, has no host "no connection"
line, no exit in the capture, mapped ChildIOT fds `13,14`, and a live fd-14
stream (`payloads=136`, `recv=135`, `payload_bytes_in=352936`) carrying
`RendererConfiguration`, `RendererVariationsConfiguration`,
`VisitedLinkNotificationSink`, `ChildHistogramFetcherFactory`, and `WebCache`.
Later browser fds `136` and `137` also carry `GpuChannel`,
`FontService`/`TimeZoneMonitor`, and `RenderMessageFilter`, but pair to
renderer children `521` and `531`, which both hit the 15-second no-connection
path after the NetworkService restart and only exchange the initial fd-14
payloads. Current R8 interpretation: browser-side Mojo naming and SCM/fd
inheritance are no longer the leading suspects. The next proof should
instrument renderer role/admission classification and child-side bootstrap
state after fd129/child509 receives the expected page/renderer bindings,
including why the sampler still reports `renderer_seen=0` /
`renderer_candidate_seen=0` while `gpu_seen=1`.

2026-07-03 R8 semantic renderer-role parser update: added parser-only
`renderer_semantic_role_gap_summary` to the Chromium-video post-evidence path
and fixed `process_summary`'s renderer-candidate PID accounting for future
cmdline-visible renderer samples. No kernel, launcher, image, or runtime
default changed. Offline replay of the refreshed full-iovec archive is saved at
`build-x86_64/r8-semantic-role-gap/20260703T070756Z-full-iovec-semantic-role-gap-parser-replay/renderer-semantic-role-gap-summary.txt`.
The replay reports `renderer_launch_count=3`,
`semantic_renderer_candidate_count=3`, `live_config_renderer_candidate_count=1`,
`sampled_as_renderer_count=0`, `sampled_as_zygote_count=3`, and
`live_config_role_gap_count=1`. It also confirms the refreshed preload sees the
full browser-side fd-115 analogue on fd `129`:
`content.mojom.RenderMessageFilter` at line `348`,
`blink.mojom.LocalFrameHost` at line `361`, and
`blink.mojom.LocalMainFrameHost` at line `454`, while GPU-channel traffic is on
fds `136,137`. The live semantic renderer candidate is child `509`: no
no-connection line, no exit, `CHILD_PING=1`, `iot_payloads=136`,
`iot_bytes_in=352936`, and renderer config interfaces on fd 14, but process
sampling still reports `role_source=type_arg type_arg=zygote` for samples
`51-58`. This made the next proof a Linux comparison of the zygote-forked
renderer transition (cmdline, thread names, `/proc` identity, and first
renderer-side control messages), rather than more browser-side Mojo volume.

2026-07-03 R8 Linux proctitle control + tracer role fix: archived comparison is
at
`build-x86_64/r8-linux-xv6-role-compare/20260703T071409Z-linux-xv6-renderer-proctitle-control/linux-xv6-renderer-role-control-summary.txt`.
The Linux control reaches `PERF-VIDEO RESULT pass fps=47.1 speed=0.998`
before the wrapper's outer `exit_status=143` timeout. In the same preload
evidence, six renderer children are visible as single-string process titles
with `--type=renderer` (pids
`3986908,3986921,3986922,3986968,3986970,3987221`), but the old preload
`role=` field labels all six `unknown`: `linux_process_role_renderer_count=0`,
`linux_unknown_role_renderer_title_count=6`,
`linux_nul_separated_renderer_arg_lines=0`, and
`linux_space_separated_renderer_title_lines=6`. Active renderer IPC pids
`3986908` and `3986922` exchange real messages and SCM rights, so a missing
`role=renderer` line from the old tracer is not a valid Linux/xv6 failure
oracle. Diagnostic-only follow-up: `scripts/image/chromium-egl-trace-preload.c`
now recognizes Chromium's single-string ` --type=` process-title form when
deriving `role=`; direct compile passed with
`cc -shared -fPIC -Wall -Wextra -o /tmp/chromium-egl-trace-preload.so ... -ldl`.
Current R8 interpretation splits the evidence in two: xv6 process sampling
probably exposes the original zygote exec/type identity instead of Linux-like
rewritten process titles, which can explain `sampled_as_zygote` as an
introspection/procfs semantic gap; the user-visible failure remains that child
`509` receives the expected renderer/frame/GPU/font admission milestones yet no
Chromium window appears. Next proof should compare child `509`'s post-admission
renderer-side IPC/control-loop progress against Linux active renderer pids
`3986908`/`3986922`, and only then decide whether procfs/proctitle semantics are
runtime-relevant.

2026-07-03 R8 child-local bootstrap comparison: archived evidence is at
`build-x86_64/r8-post-admission-child-compare/20260703T072409Z-xv6-child509-linux-renderer-bootstrap/child-bootstrap-compare-summary.txt`.
The normalized preload sequences show xv6 child `509` is not silent: despite
`role=zygote type_arg=zygote`, it creates fd `27` and matches both Linux active
renderers (`3986908` fd `29`, `3986922` fd `27`) through the common child-local
bootstrap prefix: `send 80 -> recvmsg 312 + 3 SCM fds -> send 104 -> recvmsg 64
-> send 88 -> recvmsg 64`. Linux then continues into a second
`send 104`/`recv` burst and later `recvmsg 200 + 2 SCM fds`; xv6 records no
more pid-509 preload IPC after the common prefix, while the final process sample
still has fd `27` and received socket fd `32` open and the main thread in
syscall `202`. Cross-checking the previous full-iovec correlation keeps the
other half of the picture intact: browser fd `129` / child fd `14` remains live
with `136` payloads, `352936` inbound bytes, renderer configuration interfaces,
no no-connection line, and no exit. Updated R8 target: explain why the
post-prefix child-local fd `27`/fd `32` control loop stops before Linux's later
SCM handoff while fd `14` continues receiving renderer-admission traffic; treat
procfs/proctitle semantics as a separate runtime hypothesis only after this
child-control-loop gap is understood.

2026-07-03 R8 child-local bootstrap parser update: added parser-only
`renderer_child_local_bootstrap_summary` to
`scripts/gpu/kde-plasma-desktop-smoke.expect` so future Chromium-video
post-evidence records this gap directly from existing `run.log` plus
`chromium-egl-trace.log`; no kernel, launcher, image, or runtime default
changed. Offline replay against the refreshed full-iovec archive is saved at
`build-x86_64/r8-child-local-bootstrap-parser/20260703T-parser-replay-full-iovec-509-gap/renderer-child-local-bootstrap-parser-replay-summary.txt`.
The replay reports `renderer_candidate_count=3`,
`candidate_with_trace_count=3`, `common_prefix_count=1`,
`stopped_after_prefix_count=1`, `second_burst_count=0`,
`late_scm_handoff_count=0`, and `live_config_prefix_stop_count=1`. The live
renderer-configured child remains `509`: `bootstrap_fd=27`,
`trace_role=zygote`, `trace_type_arg=zygote`, no no-connection line, no exit,
`iot_payloads=136`, `iot_bytes_in=352936`, `RendererConfiguration` present,
and sequence
`send80>recv312_scm3>send104>recv64_scm0>send88>recv64_scm0` with first SCM
fds `26,32,39`. The two post-restart renderer children `521` and `531` do not
reach this prefix and only show a single `recv52_scm0` control/UTC-style event.
This makes the next R8 target even narrower: identify what should drive or wait
on fd `27`/received fd `32` after the 312-byte SCM handoff for the live child,
without reopening fd inheritance, raw SCM delivery, browser-side Mojo names, or
fd14 liveness.

2026-07-03 R8 child-local fdwait offline read: archived at
`build-x86_64/r8-child-local-fdwait/20260703T-fd27-fd32-offline-evidence/fd27-fd32-offline-summary.txt`.
Read-only evidence from the refreshed full-iovec archive adds one important
detail after the 312-byte fd27 SCM handoff: the received socket fd `32` is not
lost or immediately idle. Compositor thread `544` registers both fd `27` and fd
`32` on epfd `24`, drains two fd-32 messages (`248` and `400` bytes), and the
last fd-32 state is healthy and empty (`sk_shutdown=0x0`, `sk_err=0`,
`peer_shutdown=0x0`, `peer_err=0`, `ready=0`, `peer_marks=648:648`). fd `27`
then drains the two known 64-byte prefix-tail messages and is also healthy and
empty. Process sample 57 has Compositor asleep in syscall `232` on epfd `24`
while Chrome_ChildIOT is still running; final sample 58 keeps fds `26`, `27`,
and `32` open while fd `14` remains the already-proven live admission stream.
Interpretation: the fd27/fd32 stop is not explained by unread queued bytes,
peer shutdown, fd loss, or raw SCM delivery. Next R8 probe should capture the
producer/wakeup side after run.log line 3809: epoll_wait returns/events for
child `509` epfd `24` and peer-side sends to fd `27`/fd `32` that should let
Linux enter the second burst and later `recvmsg 200 + 2 SCM` handoff.

2026-07-03 R8 child-local fdwait parser update: extended parser-only
`renderer_child_local_bootstrap_summary` so future Chromium-video
post-evidence preserves the fdwait conclusion directly. Offline replay is at
`build-x86_64/r8-child-local-bootstrap-parser/20260703T-fdwait-summary-upgrade/renderer-child-local-fdwait-parser-replay-summary.txt`.
The replay against the refreshed full-iovec archive reports
`process_evidence_present=1`, `prefix_stop_side_channel_recv_count=1`, and
`prefix_stop_epoll_sleep_count=1`. In the live child `509` context it records
`first_scm_fds=26,32,39`, `side_channel_recv_fd_count=1`,
`side_channel_all_ready_zero=1`, and `epoll_sleep_sample_seen=1`; fdwait
details show fd `27` and fd `32` both owned by Compositor thread `544`, both
registered on epfd `24`, fd `27` drained as `r312c3>r64c0>r64c0` to
`peer_marks440:440`, fd `32` drained as `r248c0>r400c0` to
`peer_marks648:648`, and four process samples with syscall `232` on epfd
`24` through sample 57. This does not change kernel/image/runtime behavior;
it makes the next producer/wakeup-side probe machine-checkable.

2026-07-03 R8 child-local peer-fdwait parser update: extended the same
parser-only `renderer_child_local_bootstrap_summary` to retain socket peer
addresses, reject stale fd/socket-address reuse, and summarize the producer
endpoint when it is visible in existing `run.log` evidence. Offline replay is
at
`build-x86_64/r8-child-local-bootstrap-parser/20260703T-peer-fdwait-summary-upgrade/renderer-child-local-peer-fdwait-parser-replay-summary.txt`.
The replay reports `prefix_stop_bootstrap_peer_mapped_count=1`,
`prefix_stop_bootstrap_peer_quiet_count=1`, and
`prefix_stop_side_channel_peer_missing_count=1`. In the live child `509`
context, bootstrap fd `27` maps to browser `266` / Chrome_IOThread `333`
fd `133`, whose peer sequence is `r80c0>s312c3>r104c0>r88c0` with
`after_stop=0`; there is no observed fd-133 peer activity after child fd `27`
drains its final 64-byte prefix-tail message at run.log line 3809. The fd `32`
side channel is different: child fd `32` drains `r248c0>r400c0` and is healthy
and empty, but the retained peer endpoint is not named by the current trace
after filtering same-fd/socket-address reuse. Current R8 target is therefore
narrower: fd `27` producer-side is quiet after the prefix, while fd `32` still
needs producer/wakeup-side ownership evidence or a trace hook that names the
peer-side sender/waiter.

2026-07-03 R8 fd32 peer-owner trace hook: added a default-off kernel
diagnostic under the existing `chrome_unix_ipc_trace=1` gate in
`kernel/kernel/lwip_port/sys_socket.c`. `SCM_RIGHTS` send/receive trace lines
now include the passed/installed AF_UNIX socket endpoint (`sk`, `peer`,
`proc_ino`, peer `proc_ino`, type/state/shutdown/error, queue marks, and
visible-fd ref counts), and successful AF_UNIX `socketpair()` calls now log
both created fds with their endpoint and peer identities. This is intended to
name the fd `32` retained producer endpoint in the next R8 Chromium-video run
without enabling broader fd tracing or changing runtime defaults. The
`renderer_child_local_bootstrap_summary` parser now consumes those future
fields, preserves the source of each endpoint mapping (`socketpair`,
`scm_send`, `scm_recv`), and reports
`prefix_stop_side_channel_peer_mapped_count` separately from the existing
missing count. Validation so far: the old full-iovec archive replay still
reports the previous truth (`prefix_stop_side_channel_peer_missing_count=1`
because the archive predates the hook), while an in-memory synthetic replay
using the known fd `32` socket addresses flips to
`prefix_stop_side_channel_peer_mapped_count=1` and maps the peer to a
Chrome_IOThread socketpair fd. `git diff --check`, `git -C kernel diff
--check`, `git -C user diff --check`, and `cmake --build build-x86_64
--target kernel -j2` passed.

2026-07-03 R8 fd32 peer-owner full-video proof:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T082222Z-r8-fd32-peer-owner-full-video-capture-status-timeout/`.
The run booted the lean R8 trace set with `chrome_unix_ipc_trace=1`,
`chrome_unix_ipc_payload_trace=1`, child syscall/epoll tracing, and
KASAN/kmemleak/klog disabled. Wrapper status stayed in the existing
`chromium-video-capture-status-timeout` class (`status_code=8`), but
`capture_status=status=DONE result=PASS samples=4` and post-evidence
completed. The new socketpair/SCM hook is live and parser-visible:
`renderer_child_local_bootstrap_summary` reports
`prefix_stop_side_channel_peer_mapped_count=1` and
`prefix_stop_side_channel_peer_missing_count=0`.

Live renderer-configured child `513` still stops after the common prefix:
`first_scm_fds=27,28,29`, `stopped_after_prefix=1`,
`second_burst_seen=0`, and `late_scm_handoff_seen=0`. Bootstrap fd `26`
drains `r312c3>r64c0>r64c0`; its browser peer is Chrome_IOThread fd `170`,
with peer sequence `s52c0>r80c0>s312c3>r104c0>r88c0` and no post-stop
activity (`after_stop=0`). The fd `32` analogue is fd `28`, which drains one
side-channel message (`r648c0`, `ready=0`) and is epoll-added on epfd `24`.
Raw lines show the browser Chrome_IOThread created socketpair fd `173`/`174`
(sk `bd62a600`/`bd62ac00`), passed fd `174` to Compositor fd `28` and fd
`173` to the same child's Chrome_ChildIOT fd `30`; fd `30` is only
SCM-installed and epoll-added, with no later traced send/recv. So the fd `32`
peer is now named and is quiet/waiting, not an unseen browser-side producer.
The same post-evidence line reports `prefix_stop_bootstrap_peer_quiet_count=1`:
the bootstrap peer was mapped and then saw no activity after the child stopped
at the common prefix.

2026-07-03 R8 bootstrap-producer offline compare: a read-only compare of the
fresh xv6 archive above with
`build-x86_64/r8-post-admission-child-compare/20260703T072409Z-xv6-child509-linux-renderer-bootstrap/`
and the Linux control browser trace
`build-x86_64/linux-chromium-control/20260703T024800Z-r8-mojom-linux-control-absolute/passive-preload/chromium-egl-trace.3986731.log`
narrows the next target. Linux's active renderer children continue on their
bootstrap fd after the shared prefix: pid `3986908` fd `29` reaches
`send104>recv64>send88>recv64>recv200_scm2>recv640>send72`, and pid `3986922`
fd `27` reaches a second `send104/send88` pair, `recv704`, then
`recv200_scm2`. The matching browser side also sends the later SCM burst on
the bootstrap stream; for pid `3986908`, browser fd `136` sends `200+SCM2`
with socket `[59388225]` and file `StQfqZ`, matching child fd `29` receiving
socket `[59388226]` and the same file. In the latest xv6 run, child `513`'s
bootstrap peer is browser Chrome_IOThread fd `170`, installed from fd `129`;
it receives the child's `104` and `88` replies (`run.log` lines `3646` and
`3665`) and then emits no later fd `170` send/recv before the child waits
after its final prefix receive (`chromium-egl-trace.log` line `1023`). This
makes the quiet fd `28`/fd `30` side-channel pair a symptom to keep, but not
the immediate Linux divergence. Current R8 target shifts to browser-side
producer progress on the mapped bootstrap peer: capture a focused
Chrome_IOThread fd `170` callstack/wait/wakeup trace after the child `88`
reply, and keep the Compositor/Chrome_ChildIOT fd `28`/fd `30` state as
corroborating context rather than the lead suspect.

2026-07-03 R8 bootstrap-peer owner epoll parser update: extended the
parser-only `renderer_child_local_bootstrap_summary` so future post-evidence
reports mapped bootstrap peer owner epoll activity after the child stops at
the common prefix. Non-executing replay against
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T082222Z-r8-fd32-peer-owner-full-video-capture-status-timeout/`
reports `prefix_stop_bootstrap_peer_owner_epoll_after_stop_count=1` and
`prefix_stop_bootstrap_peer_owner_ready_after_stop_count=1`: child `513`'s
fd `26` still has no direct post-stop fd `170` send/recv
(`bootstrap_peer_after_stop_seen=0`), but browser Chrome_IOThread `334`
owns fd `170` on epfd `26` and later receives readiness on fd `129`
(`peer_epoll_event_fds_after_stop=129`, last after-stop event
`event:7840:129:0x1`). This rules out "Chrome_IOThread simply stuck asleep
on the fd-170 epoll set" as the immediate split. Next proof should capture
Chrome_IOThread's browser-side state/callstack after the child `88` reply and
explain why it services fd `129`/later streams without producing Linux's
second bootstrap burst on fd `170`.

2026-07-03 R8 bootstrap-peer owner IPC parser update: extended the same
parser-only `renderer_child_local_bootstrap_summary` to keep per-thread
AF_UNIX send/recv history and compact process-state samples for the mapped
bootstrap peer owner. Non-executing replay against
`20260703T082222Z-r8-fd32-peer-owner-full-video-capture-status-timeout`
reports
`prefix_stop_bootstrap_peer_owner_ipc_after_stop_count=1` and
`prefix_stop_bootstrap_peer_owner_other_fd_ipc_after_stop_count=1`. In the
live child `513` context, browser Chrome_IOThread `334` still has no direct
fd-`170` post-stop send/recv (`after_stop=0`), but after the child's final
prefix receive it records `67` IPC events on other fds, specifically fd `129`
and fd `136`; fd `129` carries repeated reads and fd `136` has
`r248c1>s288c1>s288c1>r4096c0>r136c0`. The process evidence samples for the
same thread show it is not simply parked forever: `24` samples include `13`
running and `11` interruptible-sleep states, with the final sample running.
Current R8 split: the browser IO thread is alive and servicing later
renderer/browser streams, but the fd-`170` bootstrap stream does not receive
Linux's second `send104/send88` and `recv200+SCM2` continuation. The next
probe should identify the Chromium-side producer condition that decides to
continue fd `170`, using a narrow per-thread stack/control-state sampler only
if the existing IPC/state timeline cannot name the missing branch.

2026-07-03 R8 live-child identity parser update: extended
`renderer_child_local_bootstrap_summary` to extract `--renderer-client-id`,
`--gpu-client-id`, and `--top-chrome-webui` from the traced child argv, then
count live renderer-config prefix-stop candidates whose identity is still not
renderer-shaped. Non-executing replay of
`20260703T082222Z-r8-fd32-peer-owner-full-video-capture-status-timeout`
reports
`live_config_prefix_stop_type_arg_not_renderer_count=1`,
`live_config_prefix_stop_renderer_client_missing_count=1`, and
`live_config_prefix_stop_gpu_client_missing_count=1`. The live child `513`
context is now explicit:
`trace_type_arg=zygote`, `trace_renderer_client_id=missing`,
`trace_gpu_client_id=missing`, `trace_top_chrome_webui=0`, while the same
context still has `iot_has_renderer_config=1`, `live_config_candidate=1`,
`stopped_after_prefix=1`, no fd-`170` post-stop activity, and browser
Chrome_IOThread post-stop IPC on fd `129`/`136`. The existing Linux control
comparison remains the contrast: active renderers `3986908`/`3986922` have
`type_arg=renderer` with renderer/gpu client ids `6/6` and `7/7` and then
receive Linux's second child-local bootstrap burst plus the late
`recv200+SCM2` handoff. Current interpretation: the missing fd-`170`
producer continuation is correlated with the xv6 child never presenting the
Linux-style renderer argv/client-id identity, while browser-side renderer
configuration traffic is already flowing. Treat this as a testable branch
condition, not yet as proof that procfs/proctitle semantics are the root
cause: the next R8 runtime proof should determine whether Chromium consults
the child command line/title/client-id state before producing the second
bootstrap burst, or whether the identity mismatch is an introspection symptom
of another child-control-loop gap.

2026-07-03 R8 launch-vs-child identity correlation parser update: extended
parser-only `zygote_admission_summary` and
`renderer_child_local_bootstrap_summary` to preserve launch payload
`renderer_client_id` / `gpu_client_id` values beside the child-local traced
argv identity. Non-executing replay of
`20260703T082222Z-r8-fd32-peer-owner-full-video-capture-status-timeout`
now reports
`live_config_prefix_stop_launch_identity_trace_gap_count=1`,
`live_config_prefix_stop_launch_renderer_id_present_trace_missing_count=1`,
and
`live_config_prefix_stop_launch_gpu_id_present_trace_missing_count=1`.
The live child `513` is launch id `2`, `type=renderer`, with launch
`renderer_client_id=5`, `gpu_client_id=5` (recovered from the browser
send-side payload because the zygote receive sample truncated the renderer
id), while its traced child identity remains `type_arg=zygote`,
`trace_renderer_client_id=missing`, and `trace_gpu_client_id=missing`.
This strengthens the branch-condition read: the browser launch request is
renderer-shaped and includes client ids, but the child-local bootstrap still
stops after the shared prefix without presenting Linux-style renderer
identity or receiving the second bootstrap burst. The next runtime proof
should stay on the fd-`170` producer condition after the child `88` reply,
not on raw launch payload creation.

2026-07-03 R8 proc-identity runtime diagnostic prep: added a default-off
preload subgate, `KDE_SMOKE_CHROMIUM_EGL_TRACE_PROC=1` / direct
`CHROMIUM_EGL_TRACE_PROC=1`, to trace Chromium-owned opens/reads of procfs
identity files (`cmdline`, `comm`, `stat`, `status`) plus
`PR_SET_NAME`/`PR_GET_NAME` calls in the existing
`chromium_egl_trace_summary`. The tracer's own process-identity reads now
use raw syscalls so proc trace lines do not self-report preload bookkeeping.
No kernel, image, launcher default, or runtime behavior changed unless the
new proc subgate is explicitly enabled. Next R8 full-video proof should run
the lean trace set with both `KDE_SMOKE_CHROMIUM_EGL_TRACE=1` and
`KDE_SMOKE_CHROMIUM_EGL_TRACE_PROC=1` to decide whether Chromium itself reads
or names stale zygote identity before the fd-`170` continuation condition.

2026-07-03 R8 proc-identity full-video proof: restaged the updated
Chromium EGL trace preload, refreshed rootfs, and verified the guest
`/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so` hash
matched the host runtime (`4866e1b3d9e74d24c8958231840ba79c9111c6ccf5dcb482b1a456f400aa0d4b`),
then ran the full Chromium-video reducer with the lean R8 trace set plus
`KDE_SMOKE_CHROMIUM_EGL_TRACE=1` and
`KDE_SMOKE_CHROMIUM_EGL_TRACE_PROC=1`. Archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T092703Z-r8-proc-identity-full-video-capture-status-timeout/`.
Boot cmdline was verified. The wrapper label was
`chromium-video-capture-status-timeout`, but post-evidence completed capture
with `capture_status=status=DONE result=PASS samples=4` and ended in the
existing `status=FAIL reason=chrome-crash-regression` class; it did not
reintroduce a GPU/EGL loop (`gpu_init_error_count=0`, `gl_request_error_count=0`).
The new proc/prctl tracer was active:
`proc_open=31`, `proc_read=50`, `proc_cmdline_read=12`,
`proc_status_read=10`, `proc_stat_read=10`, `proc_comm_read=18`,
`prctl=107`, `prctl_set_name=107`, `prctl_get_name=0`,
`process_renderer=0`, and `process_zygote=7`.

The proc-identity result narrows, but does not solve, the branch. Chromium
does consult proc identity (`/proc/508/status` was opened by the browser),
and renderer-intended child `508` later opened `/proc/self/cmdline` and read
only the zygote argv (`--type=zygote` with no renderer/gpu client ids). No
browser-side `/proc/<child>/cmdline` read was observed in this trace, so the
failure is not the simple shape "browser reads child cmdline and sees zygote."
The launch request remains renderer-shaped (`renderer_client_id=5`,
`gpu_client_id=5`, `no_connection=0`, `child_ping=1`, `iot_payloads=120`,
`iot_bytes_in=352232`, renderer config present), but sampled child identity
still reports `trace_role=zygote`, `trace_type_arg=zygote`,
`trace_renderer_client_id=missing`, and `trace_gpu_client_id=missing`.
Full-iovec browser-side milestones are present
(`content.mojom.RenderMessageFilter`, `blink.mojom.LocalFrameHost`,
`blink.mojom.LocalMainFrameHost`, `gpu.mojom.GpuChannel`), so do not reopen
raw Mojo-name/SCM delivery as the next lead.

Current R8 target after this proof: determine why the renderer-intended child
never presents Linux-style renderer identity after the zygote launch request
and prefix bootstrap exchange. The best next diagnostic is in the child-side
command-line/proctitle mutation path (argv rewrite, `setproctitle`-style
writes, `PR_SET_NAME`, or Chromium's child process bootstrap branch after
`CHILD_PING`) and its relationship to the missing second fd-`170` bootstrap
burst. The child-local fdwait result still shows fd `27` stopping after the
shared prefix (`send80>recv312_scm3>send104>recv64_scm0>send88>recv64_scm0`);
its browser peer fd `155` is mapped and quiet for that same fd after the stop
while `Chrome_IOThread` continues other-fd IPC on `127`, `133`, and `134`.

2026-07-03 R8 process-title/runtime proof: first ran the standalone guest
`proc-cmdline-rewrite-proof`, archived at
`build-x86_64/proc-cmdline-rewrite-proof/20260703T093306Z/`, which passed and
proved `/proc/self/cmdline` exposes setproctitle-style argv/env rewrite when
the guest process actually mutates its argument span. Then added an opt-in
`process_title` sampler to the existing Chromium EGL trace preload under
`KDE_SMOKE_CHROMIUM_EGL_TRACE_PROC=1` / `CHROMIUM_EGL_TRACE_PROC=1`. Host
preload smoke passed, `host-gui-runtime` and `rootfs-refresh` rebuilt, and the
guest/host preload hash matched
`06a76a5fae0e07a5249a8474c4f18680066b345634b9954896e1f928cae9cda3`.
Full Chromium-video archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T094059Z-r8-process-title-full-video-capture-status-timeout/`.
The wrapper status remained `chromium-video-capture-status-timeout` even
though post-evidence saw the capture script finish with
`capture_status=status=DONE result=PASS samples=4`; the host output also
reported the existing `chromium-video-chrome-crash-regression` class after
the app-launch probe passed, so keep that wrapper/status mismatch in mind as
harness noise for this diagnostic run.

The new process-title evidence rules out a broad procfs cmdline rewrite bug.
The trace produced `process_title=21` samples with
`proc_has_type_renderer=0` and `title_has_type_renderer=0`; the only
`proc_has_renderer=1` samples were browser-root argv containing renderer-ish
feature strings or the page URL, not `--type=renderer`. The live
renderer-intended prefix-stop child was `528`: its child-local trace stayed
`proc_role=zygote`, `title_role=browser`, `proc_bytes=324`,
`title_bytes=53`, no renderer/gpu client ids, and no in-memory title mutation
beyond `/opt/host-gui/.../chrome`. The same post-evidence still has
`iot_has_renderer_config=1`, `live_config_candidate=1`, `common_prefix=1`,
`stopped_after_prefix=1`, first SCM fds `27,28,29`, side-channel receive
activity, and a mapped browser peer (`Chrome_IOThread` fd `170`), but no
second child-local burst and no late SCM handoff while the browser IO thread
continues other-fd IPC. Current R8 split: the browser launch/config traffic is
renderer-shaped, but the xv6 child never reaches the Chromium branch that
rewrites argv/proctitle to Linux-style renderer identity before the fd-`170`
continuation condition. Next target should instrument the child-control path
after `CHILD_PING`/renderer config and the fd-`26` side-channel/prefix reads,
not `/proc` cmdline export itself.

2026-07-03 R8 callsite/epoll runtime diagnostic and proof: added a
default-off preload subgate,
`KDE_SMOKE_CHROMIUM_EGL_TRACE_CALLSITE=1` / direct
`CHROMIUM_EGL_TRACE_CALLSITE=1`. When enabled with the existing Chromium EGL
trace, it appends caller object/offset fields to IPC lines and records capped
`epoll_create1`/`epoll_ctl`/`epoll_wait`/`epoll_pwait` lines; the smoke
harness now exports the subgate only when requested and records summary
counts, while the launcher only logs the new env key. No runtime default was
changed. Gates: `git diff --check` passed for touched files and both
submodules; `host-gui-runtime`, `user`, and `rootfs-refresh` rebuilt; the
guest preload hash matched the host runtime
(`b22de318f032e345dd0e91b0caa2a044add0ff9098bf834ed330340b485ab7f3`);
and a host preload smoke emitted `callsite=1` IPC plus epoll lines.

Full Chromium-video proof archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T095731Z-r8-callsite-full-video-fail-capture-status-timeout/`.
Boot cmdline was verified with the lean R8 trace set. The wrapper/status noise
remained: `kde-plasma-desktop-smoke.status` reports
`chromium-video-capture-status-timeout`, host-wrapper output reports
`chromium-video-chrome-crash-regression`, and post-evidence is the best judge.
Post-evidence completed capture (`capture_status=status=DONE result=PASS
samples=4`) but all four frames were black/static
(`capture_nonblack_count=0`, `unique_hash_count=1`,
`scanout_sample_current_summary nonblack_count=0`) and the final result was
`status=FAIL reason=chrome-crash-regression`. The only crash-class trigger was
the known NetworkService restart line; `chromium_fault_summary fault_count=0`,
`gpu_init_error_count=0`, `gpu_exit_error_count=0`, and
`gl_request_error_count=0`.

The new diagnostic was active in the guest:
`chromium_egl_trace_summary` reported `ipc_callsite=712`,
`epoll_create1=28`, `epoll_ctl=73`, `epoll_wait=905`,
`epoll_ready=413`, `epoll_drop=1`, and `epoll_callsite=1006`. This run
also changes the current R8 split. `renderer_semantic_role_gap_summary`
reported three semantic renderer candidates, one live renderer-config
candidate (`child=514`) with no no-connection line or exit and a substantial
ChildIOT stream (`iot_payloads=112`, `iot_bytes_in=355328`, interfaces
including `chrome.mojom.RendererConfiguration` and
`content.mojom.RendererVariationsConfiguration`). Browser-side full-iovec
milestones were present for `content.mojom.RenderMessageFilter`,
`blink.mojom.LocalFrameHost`, `gpu.mojom.GpuChannel`, and
`font_service.mojom.FontService`; the kernel payload milestone summary also saw
`blink.mojom.LocalMainFrameHost:1`, while the full-iovec complete-channel
summary still had `blink.mojom.LocalMainFrameHost:0`. There is still no sampled
renderer role (`process_renderer=0`, `sampled_as_renderer_count=0`). The two
later semantic candidates (`523`, `532`) were no-connection/short-initial-
exchange children.

Offline callsite pass over the same archive: browser IPC callsites concentrate
on `Chrome_IOThread` receives at `caller_obj_off=0x3b36a7d`, sends at
`0x2f8cbdb`, and SCM/sendmsg handoff at `0x3eeaefe` across fds `120`, `136`,
`137`, and `144`. The packaged Chromium binary is stripped and references
`chrome.debug` via `.gnu_debuglink`, but no local `chrome.debug` was present, so
these offsets are stable opaque IDs until a matching debug bundle is staged.
The child side is not asleep: live child `514` has `Chrome_ChildIOT` epfd `19`
watching fd `14`, with at least 120 positive `epoll_wait` returns at offsets
`0x32bc748`/`0x32bd21d`; later child `548` similarly gets positive waits on
epfd `13`/fd `5`. Preload child IPC only records closes, so child payload facts
still come from the kernel syscall tracer (`child=514` fd `14`: 112 payloads,
355328 bytes in).

Current R8 target: preserve the callsite subgate for narrow follow-up, and move
from proc/title export toward the renderer-admission-to-visibility gap: why a
live renderer-config child plus browser Mojo/SCM/main-frame milestones still
yields no renderer role sample and a black scanout after the NetworkService
restart. The next proof should correlate browser fd `120`/`144` and child fd
`14`/`5` state around the kernel-payload `LocalMainFrameHost` event, then enable
bounded Wayland surface delivery tracing for that same run to decide whether the
gap is before surface creation/commit or after KWin/scanout delivery.

2026-07-03 R8 callsite + bounded Wayland delivery proof: reran Chromium-video
with `KDE_SMOKE_CHROMIUM_WAYLAND_DEBUG=1` plus the R8 callsite/proc preload
subgates and the same lean kernel lifecycle/IPC/epoll trace set. The harness did
not auto-create a history copy for this run, so the live evidence was archived
manually without the scratch rootfs image at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T100925Z-r8-callsite-wayland-debug-fail-capture-status-timeout/`
(`20M`; no `kde-plasma.fs.img`). Wrapper status again reported
`chromium-video-capture-status-timeout`, host-wrapper reported
`chromium-video-chrome-crash-regression`, and post-evidence is the judge:
`status=FAIL reason=chrome-crash-regression`, with the known NetworkService
restart as the crash-class trigger and no kernel/user fault
(`chromium_fault_summary fault_count=0`, `gpu_init_error_count=0`,
`gpu_exit_error_count=0`, `gl_request_error_count=0`).

The new Wayland result is decisive. `WAYLAND_CHROMIUM_WAYLAND_DEBUG=1` reached
the launcher (`wayland_debug_requested="1"`, `wayland_debug="client"`), protocol
logging was active (`protocol_seen=1`), and Chromium created four `wl_surface`s,
but performed no delivery operations at all:
`created_surface_without_delivery_count=4`, `surface_delivery_op_count=0`,
`attach_count=0`, `damage_count=0`, `frame_count=0`, `commit_count=0`, and
`primary_surface=missing`. The matrix failed as
`reason=no-primary-surface-commit`. The raw protocol agrees: create-surface
lines for `wl_surface#36`, `#20`, `#23`, and `#39`, then an idle timeout, with
no `wl_surface.attach`, `damage`, `frame`, or `commit` and no xdg toplevel role.
This moves the R8 gap before KWin/scanout delivery: Chromium/Ozone never commits
a primary surface for the page, even though the desktop scanout itself is
nonblack (`capture_nonblack_count=4`, `scanout nonblack_count=8`,
`scanout changed_count=1`; capture frames are static with `unique_hash_count=1`).

Renderer admission in the same run still shows the familiar identity gap:
`process_renderer=0`, `renderer_sampled_as_renderer=0`, and
`sampled_as_zygote_count=3`. The live renderer-config child was `512`, with
`iot_payloads=109`, `iot_bytes_in=350936`, and interfaces including
`chrome.mojom.RendererConfiguration` and
`content.mojom.RendererVariationsConfiguration`, but it sampled as zygote rather
than renderer. Browser-side full-iovec milestones were present for
`content.mojom.RenderMessageFilter`, `blink.mojom.LocalFrameHost`,
`gpu.mojom.GpuChannel`, and `font_service.mojom.FontService`, but
`blink.mojom.LocalMainFrameHost` was absent in this Wayland-debug run. The
callsite subgate remained active (`ipc_callsite=635`, `epoll_callsite=958`,
`epoll_ready=396`).

Updated R8 target: stop treating the black/hidden Chromium page as a compositor
or scanout problem. The next useful proof should instrument the Chromium/Ozone
surface path between renderer/browser Mojo admission and the first Wayland
`wl_surface.commit`: why surfaces are created but never assigned an xdg toplevel
role, attached, damaged, framed, or committed. Keep the callsite and Wayland
debug gates default-off; a narrow next probe could trace `wl_proxy_marshal*`
callers or Ozone/Wayland window state transitions for the browser/gpu processes
only, correlated with child fd `14` and browser fd `125`/`139` milestones.

2026-07-03 R8 wl_proxy marshal subgate: added a default-off Chromium preload
subgate, `KDE_SMOKE_CHROMIUM_EGL_TRACE_WAYLAND=1` / direct
`CHROMIUM_EGL_TRACE_WAYLAND=1`, under the existing `CHROMIUM_EGL_TRACE=1`
umbrella. When enabled it wraps `wl_proxy_marshal_flags` and
`wl_proxy_marshal_array_flags`, logs a bounded Wayland request stream with
callsite metadata if `CHROMIUM_EGL_TRACE_CALLSITE=1`, and keeps the normal
launcher path unchanged when disabled. The KDE post-evidence parser now reports
Wayland marshal totals plus `create_surface`, `get_xdg_surface`, `get_toplevel`,
`attach`, `damage`, `damage_buffer`, `frame`, `commit`, and
`ack_configure` counters. Validation before runtime: direct preload compile
PASS, `host-gui-runtime` rebuild/restage PASS, guest/restaged preload hashes
matched (`402fcb7b2b3f2a52b62f1712ec24c383d351e35ab9b57f090c1645b1d70b7156`),
and a host dummy Wayland provider emitted a `wayland_marshal_flags` commit line
with `callsite=1`.

Runtime notes for this subgate: the first VM rerun reached the known R5/KWin
startup #GP before Chromium evidence and was archived as invalid-for-R8 at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T102656Z-r8-wayland-marshal-kde-r5-startup-rerun-needed/`.
The second rerun reached Chromium and is archived without the scratch image or
raw captures at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T103045Z-r8-wayland-marshal-full-video-capture-status-timeout/`
(`7.0M`). It is diagnostic evidence, not a green gate: wrapper status was
`status_code=8 label=KDE-PLASMA-DESKTOP-SMOKE-FAIL
chromium-video-capture-status-timeout`, while post-evidence still classified
the run as `status=FAIL reason=chrome-crash-regression` due to the known
NetworkService restart and reported no kernel/user Chromium fault.

The new tracer confirms the earlier Wayland-debug boundary with a lower-level
client-side request view. `chromium_egl_trace_summary` reported
`wayland_marshal=34`, `wayland_marshal_flags=34`,
`wayland_marshal_array_flags=0`, `wayland_marshal_drop=0`,
`wayland_marshal_callsite=34`, `wayland_create_surface=2`,
`wayland_get_xdg_surface=0`, `wayland_get_toplevel=0`,
`wayland_surface_attach=0`, `wayland_surface_damage=0`,
`wayland_surface_damage_buffer=0`, `wayland_surface_frame=0`,
`wayland_surface_commit=0`, and `wayland_xdg_ack_configure=0`. The only
matched surface-delivery requests were two `wl_compositor.create_surface`
marshals from `libgdk-3.so.0`; there were no attach/damage/frame/commit
marshals. `WAYLAND_DEBUG` still saw four created `wl_surface`s with
`created_surface_without_delivery_count=4`, `surface_delivery_op_count=0`, and
`reason=no-primary-surface-commit`. Current R8 target is now narrower: find the
Chromium/GDK/Ozone state transition after registry bind/surface allocation that
should request an xdg surface/toplevel and first commit, instead of adding more
KWin/scanout evidence.

2026-07-03 R8 GTK/GDK + legacy Wayland marshal follow-up: added two more
default-off preload subgates under the existing Chromium EGL trace umbrella.
`KDE_SMOKE_CHROMIUM_EGL_TRACE_GTK=1` / direct
`CHROMIUM_EGL_TRACE_GTK=1` traces selected GTK/GDK window-state entry points
(`gtk_widget_show*`, `gtk_widget_realize`, `gtk_widget_map`,
`gtk_window_present*`, `gdk_window_show*`, `gdk_window_ensure_native`,
`gdk_wayland_window_get_wl_surface`, `gdk_window_set_title`) plus GTK/GDK
`dlsym` lookups. `KDE_SMOKE_CHROMIUM_EGL_TRACE_WAYLAND=1` now also wraps the
legacy exported marshal entry points (`wl_proxy_marshal*` and array/constructor
variants), not just `wl_proxy_marshal_flags`. Host validation before the VM
pass: direct preload compile PASS, GTK/GDK dlsym/wrapper smoke PASS, legacy
Wayland marshal dummy smoke PASS, `host-gui-runtime` rebuild PASS, `user` and
`rootfs-refresh` restage PASS, and the guest/restaged preload hash matched
`60515a70109b0b3157b44403bc67e4733d42d4c594b852b9b762dbd34c8c9763`.

The launch-only VM proof for those subgates is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T105725Z-r8-gtk-wayland-legacy-marshal-launch-only-chrome-crash-regression/`.
It again reached the known R8 failure class: wrapper `status_code=8`
`chromium-video-chrome-crash-regression`, `launch_evidence_status=PASS`,
sampler `status=DONE result=PASS`, no renderer, browser+GPU+zygotes present,
and a GPU EGL loop (`gpu_init_error_count=15794`,
`Failed to create shared context for virtualization`). `WAYLAND_DEBUG` moved
the surface boundary forward from the previous no-commit run: Chromium created
five surfaces, produced one xdg toplevel on `wl_surface#28`, acked configure,
and issued one `wl_surface#28.commit()`, but still performed no
`attach`/`damage`/`damage_buffer`/`frame` and delivered no primary buffer
(`primary_commit_count=1`, `primary_buffer_count=0`,
`reason=no-primary-surface-buffer`). The preload view stayed limited to the
GDK connection (`wayland_marshal=37`, `wayland_marshal_flags=37`,
`wayland_marshal_legacy=0`, `wayland_get_xdg_surface=0`,
`wayland_get_toplevel=0`, `wayland_surface_commit=0`, `gtk_dlsym=5`,
`gtk_event=0`), proving that Chromium/Ozone's later Wayland surface path
bypasses the exported libwayland marshal symbols we can interpose with
`LD_PRELOAD`.

2026-07-03 R8 wire-level Wayland probe: because Chromium/Ozone is not using
the exported marshal symbols for the main window path, added a default-off
wire decoder under the same `CHROMIUM_EGL_TRACE_WAYLAND=1` gate. The preload
now marks connected AF_UNIX fds whose peer path is `wayland-*`, tracks a sparse
fd/object class table, and decodes client requests observed through
`write`, `writev`, `send`, `sendto`, and `sendmsg`. It recognizes enough of
the protocol to follow `wl_display.get_registry`, `wl_registry.bind`,
`wl_compositor.create_surface/create_region`, `wl_shm.create_pool`,
`wl_shm_pool.create_buffer`, `xdg_wm_base.get_xdg_surface`,
`xdg_surface.get_toplevel`, `xdg_surface.ack_configure`, and
`wl_surface.attach`/`damage`/`damage_buffer`/`frame`/`commit`, while retaining
callsite logging when `CHROMIUM_EGL_TRACE_CALLSITE=1`. Host validation before
the VM pass: direct preload compile PASS, synthetic Wayland socket smoke PASS
for `connect`, `writev`, and `sendmsg` decode through
`get_xdg_surface`/`get_toplevel`/`ack_configure`/`commit`, `host-gui-runtime`
rebuild PASS, `rootfs-refresh` PASS, and the guest/restaged preload hash
matched `61d2476a7c403d02fe8d2d9d7bf2b3585a9874685b21f55560909eb8e647cd42`.

The wire-level launch-only VM proof is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T111248Z-r8-wayland-wire-launch-only-chrome-crash-regression/`
(`15M`, scratch image removed). It is still a failing diagnostic run, not a
gate pass: wrapper `status_code=8`
`chromium-video-chrome-crash-regression`, post-evidence
`status=FAIL reason=chrome-crash-regression`, `launch_evidence_status=PASS`,
sampler `status=DONE result=PASS`, browser+GPU+zygotes present, no renderer,
and the GPU shared-context failure is present again (`gpu_init_error_count=8482`,
`Failed to create shared context for virtualization`). The important new result
is that the wire decoder sees Chromium's bundled/static Wayland path:
`wayland_connect=2`, `wayland_wire=124`, `wayland_wire_sendmsg=124`,
`wayland_wire_unknown=0`, `wayland_wire_drop=0`,
`wayland_wire_create_surface=4`, `wayland_wire_get_xdg_surface=1`,
`wayland_wire_get_toplevel=1`, `wayland_wire_xdg_ack_configure=1`,
`wayland_wire_surface_commit=1`, and still
`wayland_wire_surface_attach=0`, `wayland_wire_surface_damage=0`,
`wayland_wire_surface_damage_buffer=0`, `wayland_wire_surface_frame=0`.
This matches the `WAYLAND_DEBUG` matrix exactly: five created surfaces, one
active xdg-toplevel primary surface (`wl_surface#28`), one primary commit,
zero attach/damage/frame requests, zero primary buffers, and
`reason=no-primary-surface-buffer`.

2026-07-03 R8 buffer-path probe: extended the default-off Chromium EGL preload
under the existing trace gates to log the post-configure drawing path: readable
render-node ioctl request names, `eglMakeCurrent`, swap-with-damage,
EGL image create/destroy, `wl_egl_window_create/resize/destroy`, and GBM BO
create/import/destroy calls. Host checks before the VM proof passed: direct
preload compile, `host-gui-runtime`, `user`, `rootfs-refresh`, and the guest
image's staged preload hash matched
`54f1169a732e4154b3fcfaf557ddeb4dda05d5d982b8c85a15ed73b81a2c7ada`.
The first diagnostic run, archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T120118Z-r8-buffer-path-launch-only-sampler-timeout/`,
validated the new summary counters but was too noisy: the sampler timed out,
Chromium only reached four `create_surface` requests, and the xdg-toplevel
commit boundary shifted earlier than the previous wire proof. It is useful as
a trace-volume negative control: all EGL/GBM/`wl_egl_window` counters were
zero, and only `DRM_IOCTL_VERSION` ioctls were observed.

The cleaner buffer-path rerun is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T120647Z-r8-buffer-path-light-launch-only-sampler-timeout/`
(`43M`, scratch image excluded). It used the launch-only reducer with Wayland
wire/debug, GTK, EGL callsite tracing, and only lifecycle/fdtable kernel
tracing. The run still failed as a diagnostic timeout (`status_code=8`
`chromium-video-sampler-completion-timeout`), but it restored the important
Wayland boundary: `launch_evidence_status=PASS`, five created surfaces, one
active xdg-toplevel primary surface, one `ack_configure`, one primary
`wl_surface.commit`, zero primary attach/damage/damage_buffer/frame requests,
zero primary buffers, and `reason=no-primary-surface-buffer`. The GPU process
opened `/dev/dri/renderD128` and made 97 GPU-side ioctls, but the named ioctl
split is only `DRM_IOCTL_VERSION=32` and `DRM_IOCTL_VIRTGPU_GETPARAM=73`:
`VIRTGPU_CONTEXT_INIT`, resource create/blob, resource info, execbuffer, wait,
and caps are all zero. The new buffer counters are also all zero:
`egl_make_current`, swap, swap-with-damage, EGL image create/destroy,
`wl_egl_window_*`, and GBM BO create/import/destroy never fire. The old
shared-context signature is present and much louder
(`gpu_init_error_count=30241`, GPU pid `329`,
`eglCreateContext ES 3.0 failed with error EGL_BAD_ATTRIBUTE`, followed by
`Failed to create shared context for virtualization`).

Current R8 boundary after the buffer-path proof: Chromium's browser/Ozone path
does reach registry bind, xdg-toplevel creation, configure ack, title/geometry
updates, and one primary-surface commit, but the GPU dies or fails early enough
that no drawable/window/buffer allocation path starts. This is no longer a
KWin/scanout blind spot, a libwayland interposition gap, or a dmabuf/GBM
resource-creation failure after allocation; Chromium never reaches those calls.
The next useful R8 probe should explain why the GL/EGL shared-context failure
does not pass through the current exported-wrapper counters (`dlsym` observes
`eglCreateContext`, but the wrapper count stays zero), or otherwise identify
the libEGL/internal dispatch path that rejects ES 3.0/2.0 with
`EGL_BAD_ATTRIBUTE` before the first `wl_surface.attach`.

2026-07-03 R8 EGL dlsym-wrapper diagnostic: fixed the post-evidence parser to
count the current `_enter` EGL wrapper names (`eglChooseConfig_enter`,
`eglCreateContext_enter`, `eglCreatePbufferSurface_enter`, and
`eglCreateWindowSurface_enter`) and tested the missing `dlsym` route by
returning EGL wrappers for interesting `dlsym()` lookups. The first proof run
was the pre-gate behavior-changing diagnostic, archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T121548Z-r8-egl-dlsym-wrapper-launch-only-pass/`.
It reached launch-only PASS with active Wayland surface delivery:
`primary_attach_count=528`, `primary_damage_count=528`,
`primary_frame_count=528`, `primary_buffer_count=2`, `rvfc_count=607`,
`presented=607`, and `decoded=884`. But the same archive also shows why this
cannot be treated as an R8 fix: the GPU process hit `GLDisplayEGL::Initialize`
failures, retried, then ran a `--use-gl=disabled` fallback. The tracer saw
EGL `dlsym` lookups with `traced=1`, yet still recorded no actual wrapper call
entries for `eglChooseConfig`, `eglCreateContext`, `eglMakeCurrent`, or the
buffer path. Returning wrappers from `dlsym()` therefore perturbs Chromium's
EGL startup enough to force a software/disabled-GL route where the browser can
present; it does not validate the real virgl/EGL path.

To keep that diagnostic from silently changing normal launches, EGL wrapper
substitution through `dlsym()` is now explicitly default-off behind
`KDE_SMOKE_CHROMIUM_EGL_TRACE_DLSYM_WRAP=1` /
`CHROMIUM_EGL_TRACE_DLSYM_WRAP=1`. The smoke post-evidence records
`chromium_egl_trace_dlsym_wrap`, and the launcher logs the env key. Host
compile, `host-gui-runtime`, and `rootfs-refresh` passed; the staged guest
preload hash matched the host runtime at
`de04e52b07e1309285f387f1df630dc301e961681d2f4e4bc87d5dae52c90bb9`.
The first passive-control retry was invalid for R8 because it hit a
`wireplumber` #GP before Chromium evidence; archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T122047Z-r8-passive-dlsym-wrap-off-wireplumber-gp-invalid/`.

The valid passive gate-off control is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T122250Z-r8-passive-dlsym-wrap-off-chrome-crash-regression/`.
It restored the existing R8 failure with the new code's default behavior:
`chromium_egl_trace_dlsym_wrap=0`, launch sampler PASS, final
`status=FAIL reason=chrome-crash-regression`, one primary xdg-toplevel commit
but no primary attach/damage/frame/buffer (`reason=no-primary-surface-buffer`),
`gpu_init_error_count=11068`, and the shared-context fatal
`Failed to create shared context for virtualization`. The raw trace still saw
EGL `dlsym` lookups, including `eglCreateContext`, `eglGetDisplay`,
`eglInitialize`, and `eglMakeCurrent`, but reported `traced=0`; all EGL, GBM,
`wl_egl_window`, virtgpu resource, execbuffer, wait, and caps counters remained
zero. Updated R8 boundary: the default passive path is still the original
EGL/shared-context failure before the first drawable buffer, while the opt-in
dlsym-wrapper path proves the rest of KWin/Wayland/browser presentation can
work if Chromium bypasses GL with a software/disabled-GL fallback. The next
honest fix must either make real Mesa/virtgpu EGL display/context init succeed
(`GLDisplayEGL::Initialize` / shared context) or deliberately choose a
documented launcher policy for software GL fallback; do not count the
behavior-changing dlsym diagnostic as a fix.

2026-07-03 R8 deliberate software-GL policy probe: tested the fallback
explicitly, without EGL dlsym wrapper substitution, by launching the
Chromium-video launch-only reducer with
`KDE_SMOKE_CHROMIUM_VIDEO_EXTRA_FLAGS='--use-gl=disabled'` and the harness
default `WAYLAND_CHROMIUM_AUTO_GL_FLAGS=0`. Archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T122918Z-r8-deliberate-use-gl-disabled-launch-only-pass/`.
This was a clean M9-style launch-only PASS: wrapper `status_code=0`, post
evidence `status=PASS reason=launch-only`, launch sampler PASS
(`samples=63`, `duration_ms=18000`), no NetworkService/no-connection
restart (`no_connection_count=0`, `network_restart_count=0`), no GPU init,
config, exit, GL request, launcher crash, Chromium fault, or int3 errors.
The launcher policy line proves the requested shape:
`chromium_extra_flags="--use-gl=disabled"`, `use_gl="disabled"`, no
`LD_PRELOAD`, no EGL trace, and `chromium_egl_trace_dlsym_wrap=0`. The page
played the local MP4 (`ready=4`,
`currentSrc=file:///share/webkit/perf-1280x800-60fps.mp4`); because this is
still software/disabled GL, performance is not an M7 pass:
`presentedFPS=40.3`, `decodedFPS=60.9`, `dropped=349`, `dropPct=37.77`.

The visibility/delivery proof for the same deliberate policy is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T123130Z-r8-use-gl-disabled-wayland-delivery-launch-only-pass/`.
It added only `KDE_SMOKE_CHROMIUM_WAYLAND_DEBUG=1` to the same explicit
`--use-gl=disabled` launch. It also passed launch-only (`status_code=0`,
sampler PASS) and the Wayland surface matrix is the important result:
`status=PASS reason=surface-delivery-active`, primary surface `39` is an
`xdg_toplevel`, `primary_attach_count=504`, `primary_damage_count=504`,
`primary_frame_count=504`, `primary_commit_count=506`, and
`primary_buffer_count=2`. Raw protocol shows the normal route after configure:
`get_xdg_surface`, `get_toplevel`, `ack_configure`, SHM pool/buffer creation,
then repeated `wl_surface.frame`, `attach`, `damage`, and `commit`. This
reproduces the dlsym diagnostic's positive path without behavior-changing
interposition. Current branch decision: an intentional launcher policy of
`--use-gl=disabled` is a viable M9 visibility workaround with measurable
playback, but it bypasses hardware GL and remains below the video-FPS goal.
Do not make it the default without an explicit lane decision and the full
no-default-flip regression gate; keep the real fix path focused on why
Chromium's normal EGL/virgl shared-context initialization fails before the
first drawable buffer.

2026-07-03 R8 Mesa context parser + bundled-GL alternate-root status: added a
default-off Mesa EGL context trace (`XV6_MESA_EGL_CONTEXT_TRACE=1`, exposed to
the smoke as `KDE_SMOKE_CHROMIUM_MESA_EGL_CONTEXT_TRACE=1`) and used it to
find a real Mesa parser ordering bug. Chromium passed
`EGL_CONTEXT_OPENGL_NO_ERROR_KHR` before `EGL_CONTEXT_MAJOR_VERSION_KHR`, so
Mesa rejected the no-error attribute while the requested version was still the
default 1.x. The parser now validates no-error API/version legality only after
all context attributes are parsed, keeping the GL/GLES >= 2 rule intact without
making acceptance depend on attribute order. The diagnostic failure archive is
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T130738Z-r8-mesa-no-error-attr-order-display-bits-launch-only-crash-regression/`;
the fixed-parser default Mesa proof is
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T131336Z-r8-mesa-no-error-attr-order-fix-unblocks-video-angle-robust-client-memory-regression/`.
That proof removed GPU init/config/exit errors and played the local video, but
it still failed the Chromium crash classifier on
`ContextResult::kFatalFailure: missing GL_ANGLE_robust_client_memory` and
remained below M7 (`presentedFPS=45.87`, `dropPct=32.02`). A
`--use-cmd-decoder=validating` probe did not help; this Chromium build reports
the validating decoder unsupported, so the passthrough path and robust-client
fatal remain
(`20260703T131639Z-r8-cmd-decoder-validating-still-angle-robust-client-memory-regression/`).

To test whether Chromium's bundled ANGLE stack can provide the missing
passthrough extension without changing defaults, the image stage now saves
Chrome's bundled `libEGL.so`/`libGLESv2.so` under `chrome-linux64/xv6-bundled-gl`
while leaving the default Chrome root symlinked to guest Mesa, and also stages
an alternate hardlink root at `chrome-linux64-xv6-bundled-gl`. The launcher
uses that alternate root only when `WAYLAND_CHROMIUM_BUNDLED_GL=1`; the first
LD_LIBRARY_PATH-only attempt proved Chromium still opened `$ORIGIN/libEGL.so`
from the normal root and failed in the old class
(`20260703T134352Z-r8-bundled-angle-runtime-knob-launch-only-chrome-crash-regression/`).
The alternate-root opt-in proof passed the launch-only gate:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T135355Z-r8-bundled-angle-alt-root-launch-only-pass-software-fallback/`.
Its process evidence maps the preserved ANGLE `libEGL.so` and `libGLESv2.so`
from `chrome-linux64-xv6-bundled-gl`, `launcher_crash_seen=0`, sampler PASS,
and post-evidence `status=PASS reason=launch-only`. This is still not an M7 or
hardware-GL fix: early bundled-ANGLE GPU processes fail
`eglCreateContext ES 3.0 failed with error EGL_SUCCESS`, then Chromium falls
back to `--use-gl=disabled`; video remains poor (`presentedFPS=31.79`,
`dropPct=26.07`). A fresh default-control launch-only run against the same
image is archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T135821Z-r8-alt-root-default-mesa-path-known-chrome-crash-regression/`:
it executed the normal `chrome-linux64/chrome`, left
`WAYLAND_CHROMIUM_BUNDLED_GL` unset, sampler PASS, and failed in the same known
robust-client crash class. Current R8 status: default behavior remains the
Mesa path and is not fixed; the default-off alternate root is a useful M9
visibility workaround/probe that confirms the remaining default blocker is
Chromium passthrough's required robust-client-memory extension, not renderer
admission or KWin surface delivery.

2026-07-03 R8 robust-client extension-ladder diagnostic: added the
default-off smoke knob `KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE`, which
passes `MESA_EXTENSION_OVERRIDE` only to the staged Chromium launch and logs
the value in launcher/post-evidence output. The first run exposed and fixed a
post-evidence Tcl global bug, archived as invalid-for-result at
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T141950Z-r8-mesa-extension-override-harness-post-evidence-bug/`.
With `GL_ANGLE_robust_client_memory` forced into Mesa's extension string, the
launch still failed as `chrome-crash-regression`, but the fatal moved forward
to `missing GL_CHROMIUM_bind_generates_resource`; archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T142156Z-r8-mesa-extension-override-chrome-crash-regression/`.
With both `GL_ANGLE_robust_client_memory` and
`GL_CHROMIUM_bind_generates_resource` forced, the fatal moved forward again
to `missing GL_CHROMIUM_copy_texture`; archive:
`build-x86_64/kde-plasma-desktop-smoke-history/20260703T142458Z-r8-mesa-extension-override-two-token-chrome-crash-regression/`.
That two-token run proved the page/video path still advances
(`presentedFPS=41.97`, `decodedFPS=61.55`, `dropPct=34.90`) and that GPU
init/config/exit errors stay zero, but it is not a fix. Chromium passthrough
initialization checks a set of ANGLE/Chromium feature flags in sequence:
`GL_ANGLE_robust_client_memory`, `GL_CHROMIUM_bind_generates_resource`,
`GL_CHROMIUM_copy_texture`, `GL_ANGLE_client_arrays`,
`GL_ANGLE_webgl_compatibility`, `GL_ANGLE_request_extension`, and
`GL_KHR_debug`. Therefore default-advertising unknown extension names in Mesa
would only walk Chromium into later missing semantics/entrypoints. Keep the
override knob diagnostic-only; the real default path needs either actual
Mesa/virgl support for the required passthrough semantics or an explicit,
documented software/bundled-GL launcher policy.

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

Status: classified and closed for current kernel-gate interpretation. Treat
future pactl crashes as optional-helper noise only when the gate explicitly
sets the default-off tolerate knob; otherwise the smoke harness still fails
fast.

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
the page's canvas/HUD onto raster fallback during the measured window. No new
decision — it reinforces that Q2 must be resolved before M7 is trustworthy.

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
- `ports/xz/src` is unrelated dirty state; do not revert.
- Dirty-by-design inventory (2026-07-03, superseding the noflush-only
  note): kernel submodule carries the uncommitted P0 idle-pull/atomic
  halt, R7a O(1) rq registry, R7b inotify FIONREAD ABI fix, R7c timer
  tick fastpath, P1 2a FS_BASE cache + 2b trapframe change, and gated
  diagnostic probes (rq/timer/starve/R3 RCU tagging — all default-off);
  `ports/mesa/src` carries the validated EGL no-error attr-order fix +
  gated context trace; `user` carries the linuxsyscallabitest
  inotify-fionread reducer and wakestorm; top level carries harness/
  launcher/preload diagnostics (default-off) and the bundled-GL
  alternate-root staging. ALL of this is queue item Q1 — commit it
  lane-scoped before new work; keep this list current afterward. NOTE:
  the kernel portion has an open review (see "Code Review — Uncommitted
  Kernel Diff (2026-07-03)") — triage CR-1..CR-10, resolve blockers CR-1
  and CR-2, before/as part of committing.

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

Commit after major verified steps only when the dirty scope is understood.
Nested submodules: commit deepest-first, update parent pointers, never push
without explicit user approval, never bundle unrelated changes.
For the current Q1 sweep: "verified" is not sufficient — the kernel diff
has an open review (Code Review section); triage CR-1..CR-10 and resolve
blockers CR-1/CR-2 before committing the P0 and P1 lanes.
