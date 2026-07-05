# Active xv6 Work Plan

Last updated: 2026-07-04 (FULL COMPACTION REWRITE after the 07-04 round:
R5 root-caused + fixed, P2 ordered-pageflip default landed, Q2 real-GL
runtime-validated with the first default-path M9 PASS, P3 ext4 slice landed
gated, N2 ext4 default promotion attempted but NOT accepted after a
default-on KWin #GP rerun; later N2 ON-arm kernel #PF classified and
opt-in diagnostics landed; kprofile measurement validity fixed; N3 R9
coordinate and visible cursor probes passed; cleanup removed stale ignored
proof images; N4/P1 cpumask+CR0 and CR0-only attempts stopped/reverted with
no landing. All verbose evidence chains moved to the history file and git
log; this file holds current status, queue, rules, and compact lane
conclusions only.)

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
| M2 | Guest `getpid_ns` | `syscalltlb 2000` nographic | accepted baseline 1.65-1.94us (P1 2a/2b landed); N4 stopped/reverted after clean CR0-only rerun failed at 1973/2102/1925/2185ns | < 1.5us (requires a different P1 approach) |
| M3 | `tlb_amplification_ns` (1024 pg) | same | accepted baseline noisy 2.7-3.7us band; clean CR0-only N4 rerun failed at 4504ns | 0 on default boot |
| M4 | `konsole_wait_ms` | KDE desktop-interaction reducer | 1883-2144 band across the 07-04 batteries (best 1883 post-R5-fix) | < 2000 |
| M5 | `first_visible_ms` | same | 11471-13850 band | < 15000 |
| M6 | `mesakmsgl` direct-KMS FPS | pageflip A/B | 118-125 ordered, now DEFAULT-ON | done (was: ordered default) |
| M7 | `presentedFPS` (60fps video) | chromium-video kprofile, KPROFILE_SECONDS=90 | 44.2 real GL + ordered pageflip (arc 07-04: 36.8 -> 42.9 -> 44.2; dropPct 29.6; decode 61.5 keeps pace). Sole remaining ceiling: software-blit scanout — every flip copies, native_present_credit=0 | >= 55 (N1) |
| M8 | Idle-desktop host CPU | 10s `/proc/$pid/stat` utime+stime delta on a GL-pipeline boot (non-GL boots never present — invalid for M8) | UNMEASURED-VALID: the 2026-07-05 44%/57-64% readings were doubly invalid (non-GL boots with zero presentation AND poll flags since fully reverted for interactive hangs). Historical band 85-130%. Re-measure with the documented GL recipe on shipped defaults | < 100% |
| M9 | Chromium window visible | chromium-video launch-only reducer | PASS on the DEFAULT path with REAL hardware GL (2026-07-04): zero missing-GL fatals, zero software fallbacks, GPU errors 0 | PASS (holds; guard in every battery) |

Fork-safety gate for any syscall/scheduler/TLB/mm change: `forktest`
(rc=1 "fork claimed to work N times!" = known exhaustion signature),
`clonetest` rc=0, `cowtest` rc=0, same boot.

## Work Order — Current Queue (recut 2026-07-04 post-round)

The 07-03 Q1-Q7 queue is RETIRED: Q1 landed, Q2 runtime-validated (real
GL), Q5 root-caused+fixed, Q7 landed. New queue:

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
Recommended execution order: (1) N1 IMPLEMENTATION slice (op_lock/
make-room fix + ring depth; attribution DONE, see entry); (2) N5 poll
fast-path promotion battery (92% churn collapse proven; M8 payoff
expected); (3) N7 tick-loss fix; (4) N6 DONE; (5) N2 retry ONLY after its fault
diagnosis gate; (6) N3 residual LibinputBackend nullptr; (7) a NEW P1
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
