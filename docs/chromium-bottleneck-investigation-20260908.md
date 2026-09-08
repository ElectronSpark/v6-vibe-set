# Chromium frame-drop bottleneck investigation — 2026-09-08 UTC

This investigation follows the [local-video comparison](chromium-video-drop-investigation-20260908.md)
and tests the proposed scheduler, lock, deferred-interrupt and RCU explanations.
The preceding measurements establish extra drops on xv6, but their Wayland
request/reply intervals combine compositor work, transport and client dispatch.
They do not identify a kernel subsystem as the cause.

The new traces localize a substantial loss after Chromium selects a prepared
video frame and before its submission passes frame-construction guards. The
slower traced arm has more such losses and longer reported presentation
latency. Outstanding compositor acknowledgements are a plausible gate, but
their state is not recorded. This identifies a bottleneck stage, not a proven
defect in softirq, locking, scheduling or RCU.

## Source-derived hypotheses

| Candidate | What the current implementation actually does | Discriminating evidence |
| --- | --- | --- |
| Deferred interrupt work | The GPU IRQ handler acknowledges the device, wakes completion waiters and queues `vgpu-reap`; full reaping runs in a schedulable worker. Allocated DRM timers also dispatch through a worker. There is already deferred processing, irrespective of whether it is called softirq. | IRQ/poll/post/retire deltas, device waits, then timer/worker/client dispatch latency. |
| GPU resource mutex | GPU submission and resource operations share an operation mutex. Admission normally releases it while waiting for queue space. | Existing aggregate initial-lock wait versus submit, attach, admission and retirement costs. Initial-acquisition timing does not include all reacquisition or hold time. |
| Scheduler and timer notification | Timerfd records expiration first and defers epoll notification to workqueue execution. Enqueue and scheduling follow additional locks and wakeups. | Per-role runnable-wait deltas and host vCPU scheduling counters; later per-event tracing if aggregate evidence warrants it. |
| Spinlocks and process VM locks | Spinlock acquisition and RCU read-side sections disable local interrupts. User VM read/write semaphores can serialize sibling threads during copies, faults and mapping changes. | Address/caller-attributed wait and hold durations; a source-level possibility alone is insufficient. |
| RCU | Ordinary FD lookup uses a short read-side pointer/refcount section without waiting for a grace period. Callback reclamation runs outside the callback-list lock in preemptible threads. | Long read-section or callback CPU/batch evidence; neither is established by a backlog count or the previous frame-rate measurements. |
| Presentation clock | The actual SDL append enables both asynchronous presentation and the independent 60-Hz presentation clock. This already paces completion even though the older `vblank_paced_flip` switch is absent/default-off. | Requested pacing delay, clock snaps, async completion and DRM-event deltas. These counters do not measure actual physical scanout. |

Relevant source locations are `kernel/kernel/virtio_gpu.c` (IRQ/reaper and
submission trace), `virtio_gpu_user.c` (operation-mutex acquisition and owner
cleanup), `dev/fb/fb_drm_core_kms.c` / `fb_drm_kms_properties.c` (presentation
clock and async worker), `vfs/timerfd.c`, `proc/workqueue.c`, `proc/sched.c`,
`proc/rq.c`, `lock/spinlock.c`, `lock/rcu.c`, and `vfs/fdtable.c`.

The presentation clock selects a 16,666,667-ns grid point, snaps late work to an
elapsed point, and schedules the remaining delay rounded up to milliseconds
with a minimum of 1 ms. This is not proof of an extra half-frame delay relative
to Linux: arrival phase, rounding and worker scheduling determine the result.
The 20-ms epoll rescan is a missed-notification fallback, not the normal event
delivery period. Both distinctions prevent attributing a similar-looking frame
interval to the wrong mechanism.

## Diagnostic measurement contract

Use the same protected kernel, rootfs overlay, Chrome executable, clip and frozen
fixture identified in the preceding audit. The only kernel-append addition is
the existing `virtio_gpu_submit_trace=1` diagnostic gate. There is no kernel or
rootfs rebuild. All browser arms disable `WAYLAND_DEBUG`. The OFF/ON/ON/OFF
variable now means Chromium tracing in categories `media,cc,viz,benchmark`,
with a 16-MiB ring and a 64-MiB JSON-export cap. Fresh profiles share one boot.

Before each start gate, inspect the mapped page, focus it, then collect passive
guest task and host QEMU task counters. Repeat after the fixture's terminal
report, before closing the browser. No diagnostic requests, screenshot calls
or input occur during playback. The acquisition timestamps bracket the actual
measurement; task deltas also include that small surrounding interval and are
not exact per-frame attribution. Export the browser trace only after a graphical
graceful close and a synchronous direct-child wait.

Guest `CpuTime` and `RunWaitTicks` use raw `r_time()` ticks, converted with the
same boot's measured TSC frequency. `RunSlices` counts completed queued
intervals. Runnable accounting starts after enqueue, excluding notification
and pre-enqueue lock delay. `CpuTime` is elapsed time while selected and can
include host vCPU descheduling. Host `schedstat` CPU and runnable-wait fields
are nanoseconds and remain separate. Match TID, process and start identity;
report new, vanished or invalid threads. Guest context-switch placeholders and
yield-count-based utilization are not performance evidence.

The read-only FB helper exposes existing pacing/presentation fields omitted by
`fbstat`. This ABI has no size/version handshake. The helper's 6,392-byte layout
and all 27 exported offsets were checked against the matching kernel ELF;
header and binary hashes are retained. A guard page and untouched-tail check
reject larger or shorter copyouts, without claiming to detect arbitrary
same-size ABI rearrangements. Pacing-delay totals measure requested ideal
deferral, excluding rounding and worker lateness. Async BO hold maxima are
cumulative acceptance-to-release maxima, not interval means or DMA completion.

The submit-trace gate records per-call counters in memory, but its aggregate
output can occur at unrelated process teardown. Serial chunk timestamps are
therefore retained to check for output overlapping playback. A diagnostic trace
is an observation with possible overhead, not an uninstrumented parity result.

## Collection failures retained

`chromium-bottleneck-trace-20260908T015049Z` exits before any browser trial.
Aggregate kernel output interleaves into a serial JSON readiness response;
the raw output shows readiness checks but the structured result is invalid.
No media or browser-trace result is accepted. Controller session 6691 is reaped
with exit 1, launcher exit 0, overlay removed, protected base unchanged and
exact QEMU inventory zero.

The corrected controller sends every structured helper result through bounded
HTTP receipts and associates them with the completed serial command. The serial
channel retains short acknowledgements, completion markers and a fresh prompt.
This repair covers session, owner, telemetry and export results together.

`chromium-bottleneck-trace-20260908T015555Z` retains a valid OFF1 control:
31/911 dropped frames (3.402854%), 15.0762 seconds, speed 0.99848,
1018×592 viewport, valid diagnostics, visible video/HUD and direct browser
exit 0. ON1 reaches fixture readiness but is rejected before measurement:
`/proc` exposes only the first `MAXARG=32` arguments, omitting its expected
33rd URL after five diagnostic flags. All five flags and the environment
match. This is an introspection limit, not a missing navigation. No browser
trace is exported. Session 47323 is reaped, launcher exits 0, overlay is removed,
base is unchanged and exact QEMU inventory is zero.

The next controller keeps raw observed argv intact. It accepts only that
specific capped prefix with all diagnostic flags verified, a same-PID/start
pre-exec request carrying the expected URL, and the unique fixture readiness
receipt. Complete exposed argv remains required for OFF. A premeasurement
`query-cpus-fast` also maps host TIDs to guest vCPU indices; generic host thread
names alone did not establish that mapping in the earlier control.

`chromium-bottleneck-trace-20260908T020310Z` reaches a visibly mapped first
browser, but its 60-second visual-review deadline expires during a worker
context handoff. No measured arm or exported trace is accepted. Session 88072
is reaped, launcher exits 0, the overlay is removed, the base is unchanged and
the independent exact QEMU count is zero. The next run uses a fresh worker,
concise controller output and a 90-second visual-review deadline; no missing
review is backdated or treated as approval.

## First valid diagnostic control

These observations describe the 3.40% OFF1 control above; it did not reproduce
the preceding audit's worst drop rates. They do not establish the cause of
those earlier failures.

The kernel uses a 1-kHz LAPIC TSC-deadline timer with TSC compensation and a
measured timebase of 2,689,802,480 Hz. Guest snapshots span approximately
16.608–17.022 seconds across individual task reads; host snapshots span
15.518–15.523 seconds. All 88 matched guest threads have valid counter deltas;
seven new threads remain separate. All 33 host threads match.

| Guest thread | Nice | Selected-time delta, ms | Runnable-wait delta, ms |
| --- | ---: | ---: | ---: |
| KWin main, 62 | 0 | 7065.43 | 825.95 |
| Browser main, 212 | -8 | 1462.77 | 336.84 |
| Chromium child main, 297 | 0 | 1291.20 | 1003.19 |
| ChildIOT, 297/312 | -8 | 1105.42 | 825.06 |
| Compositor, 297/316 | -8 | 915.88 | 580.00 |

These matched critical threads average 0.087–0.213 ms per accounted queued
interval. There is no percentile distribution, so occasional long waits remain
possible. All host QEMU threads together accumulate 25.301 ms runnable wait,
with 7.922 ms for the largest individual thread. This weakens sustained host
runqueue starvation for this passing control. Specialized Chromium children
retain inherited `--type=zygote` in guest argv; process/thread trace metadata
is needed for stronger role attribution.

Seven threads appear after the first snapshot, including `Media` and
`VideoFrameCompo`. Their after-snapshot lifetime selected-time counters reach
0.99–2.25 seconds individually. They are not silently treated as matched
before/after deltas. Reproduction and details: `off-1-scheduler-analysis.json`,
`off-1-scheduler-findings.json` and the retained parser/command receipt.

The FB snapshot centers span 16.82149 seconds. There are 880 paced events and
6,437,020 microseconds of requested deferral: 7.314795 ms per paced event.
Clock events increase by 881 and snaps by 211. Async accepted/completed counts
both increase by 881, with no new errors or synchronous fallback. GPU posted
and retired both increase by 4404, pending remains zero, IRQ observations
increase by 654 and poll fallbacks by 108. DRM queued/read both increase by
881, with empty final queues and no drops/overflow. Requested pacing delay
cannot be added to overlapping submission work or equated with excess versus
Linux. See `off-1-fb-pacing-analysis.json` and its source-bound field definitions.

A large Chrome owner interval reports 1761 submits, 5,532,405 us total submit
time, 282,823 us initial mutex wait and 5,205,032 us in `post_us`; KWin reports
881 submits, 481,586 us total, 3129 us initial wait and 455,645 us posting.
This puts the inclusive posting path ahead of initial operation-mutex
acquisition as a measured cost. However, `virtio_gpu_user.c` times the entire
post/retry loop, including unlocked admission and subsequent mutex
reacquisition. `post_us` is not isolated queue-notify or host GPU execution
time. Component attribution and interval completeness must be checked before
calling a particular operation the bottleneck.

The surrounding aggregate records at raw time ticks 206226933203 and
251473074621 contain 2642 additional submissions, 5,660,679 us posting and
358,045 us make-room time, including 334,232 us waiting. These records are
emitted at owner/process cleanup, not by the FB snapshot ioctl. Their roughly
16.82-second separation does not attest identical endpoints with the FB
snapshots. Global make-room is smaller than posting, but these are overlapping
aggregates, not exclusive cost buckets.

Per-owner make-room and progress-wait attribution is weaker: the submit loop
clears the shared current-owner slot before waiting unlocked, and the recording
helper reads whichever slot is current when it records. The wait can be
global-only or attributed to another concurrent operation. There is no owner-0
fallback in that helper. The final submit/post record instead receives its
owner explicitly. Subtracting an owner's recorded admission wait from its
posting time would therefore produce a misleading decomposition.

New command/payload allocation and copying occur in `async_prepare`, before
the post timer. The inclusive post interval can still contain opportunistic
reaping and frees, slot/queue locking, retries, operation-mutex reacquisition,
IRQ-disabled descriptor publication, MMIO notification and bookkeeping.
Existing counters do not isolate any one of those operations. The retained
`off-1-submit-post-boundaries.{md,json}` report binds this distinction to source
hashes and the unmodified serial records.

## Completed four-arm diagnostic boot

Receipt: `build-x86_64/gui-progress-audit/chromium-bottleneck-trace-20260908T020910Z/`.
Controller session 93434 and launcher both exit 0. Every browser has a direct
child exit of 0; every start page and terminal video/HUD is visually reviewed.
The overlay is removed, the protected base's size/mtime is unchanged, and both
worker and conductor exact-process inventories independently report zero QEMU
processes. The frozen kernel/fixture/clip/browser identities remain unchanged.

| Arm | Chromium tracing | Dropped / total | Drop rate | Playback speed | Kernel serial bytes received during playback |
| --- | --- | ---: | ---: | ---: | ---: |
| OFF1 | Off | 29 / 910 | 3.187% | 0.9974 | 6945 |
| ON1 | On | 46 / 910 | 5.055% | 0.9972 | 0 |
| ON2 | On | 69 / 908 | 7.599% | 0.9958 | 0 |
| OFF2 | Off | 65 / 910 | 7.143% | 0.9961 | 6946 |

All four fixture reports are diagnostically valid and usable, but the OFF arms
are not silent controls: unrelated process teardown prints GPU aggregates
during playback. The controller does not issue commands or captures during
those intervals. Serial byte offsets and host receive timestamps retain the
overlap; they are not exact guest emission timestamps. ON1 and ON2 have no
serial output during playback and provide the cleaner same-setting trace
comparison. The variation across the four sequential arms does not establish tracing
overhead or a fixed excess drop rate over Linux. No arm reproduces the earlier
greater-than-10% failures.

The same-boot timebase is 2,672,355,060 Hz, with a 1-kHz LAPIC TSC-deadline
timer. The scheduler parser retains raw snapshots and hashes in four
`*-scheduler-analysis.json` reports and `scheduler-analysis-commands.json`.
Host QMP CPU-index/thread-ID mapping identifies all six vCPU threads.

| Arm | All matched host runnable wait, ms | Six vCPUs' combined runnable wait, ms | Largest individual vCPU wait, ms |
| --- | ---: | ---: | ---: |
| OFF1 | 35.479 | 18.983 | 4.439 |
| ON1 | 24.680 | 15.849 | 3.551 |
| ON2 | 56.636 | 29.088 | 8.252 |
| OFF2 | 25.575 | 14.405 | 3.753 |

Host snapshots bracket approximately 15.40–15.80 seconds. These small runnable
waits weaken sustained host runqueue starvation in all four arms, including
the slower OFF2. They do not exclude host blocking inside a mutex or GPU call.
Guest snapshots cover wider 16.62–17.52-second per-counter bounds.

The guest snapshot filter has a material coverage gap: it selects an exact
Chrome `argv[0]`, and omits the active GPU process. Trace metadata identifies
GPU PIDs 412 and 583 in ON1/ON2, neither present in the corresponding task
inventory. Thus these data cannot establish GPU-thread runnable latency,
whole-Chromium CPU consumption, kernel-worker activity or RCU-worker activity.
Before a further scheduler attribution experiment, capture executable/start
identity and retain excluded or unreadable process entries explicitly.

The trace metadata does identify the active renderers as PIDs 452 and 621.
Their matched main threads accumulate 1331.76 and 1611.45 ms runnable wait,
averaging 0.246 and 0.280 ms per accounted queued interval. Their compositor
threads accumulate 718.09 and 810.19 ms, averaging 0.165 and 0.186 ms; KWin
averages 0.174 and 0.205 ms. This is consistent with somewhat more guest
contention in ON2, but contains no latency distribution or per-drop causal
alignment. Notification, interrupt-disabled and pre-enqueue lock delays remain
outside runnable-wait accounting.

There are 88/95/96/88 matched guest threads, seven new threads in every arm,
and 23/0/23/23 missing-after threads. No duplicate, rejected or invalid matched
counter delta is accepted. Missing/new identities stay separate; their
counters are not zero-filled. Host matched counts are 34/32/33/33, with one
missing-after host thread in ON1. Details and qualifications are retained in
`four-arm-scheduler-findings.json` and `trace-thread-metadata-map.json`.

## Frame-by-frame localization

Both Chromium JSON traces parse successfully and cover their full playback
windows. Buffer overwrite/wrap and reported loss/failure counters are zero.
The parser retains incomplete asynchronous spans and measurement-boundary
crossings separately; a span starting inside the interval may end outside it.
Backdated events are not interpreted as thread CPU execution.
ON1 is 38,228,460 bytes, SHA-256
`703bc21929092c4c39d2cf2906dc1ba031b4bedf2f0578c6e3e695cbe06ef390`;
ON2 is 37,892,190 bytes, SHA-256
`28cb9078f76898789d7ce47d6eb245d7f3d2e3cb38ec39ed4d3026aaeaaa5c18`.

Trace metadata declares `LINUX_CLOCK_MONOTONIC`, consistent with the pinned
[Chromium POSIX clock implementation](https://chromium.googlesource.com/chromium/src/+/151.0.7922.34/base/time/time_now_posix.cc).
Thirty-one unique frame-PTS/rVFC presentation-time anchors in each arm align
the fixture's performance clock with the enclosing `SetCurrentFrame` spans.
The retained endpoint uncertainty is ±222 microseconds for ON1 and ±244
microseconds for ON2; independent guest wall/monotonic brackets agree.
Moving the start and end independently to all four extremes of those bounds
leaves the selected/admitted/lost frame PTS sets and drop-statistic counts
unchanged. There are 16/25 completed interesting spans crossing the measurement
boundaries, which are retained rather than counted as trace loss.
Matching timestamps does not equate a callback, selection or presentation
feedback with physical scanout.

The active frame consumer is positively observed as `VideoFrameSubmitter` on
the renderer's `VideoFrameCompositor` thread, TID 530/700. For every frame
selected inside the measured interval, a unique `PrepareOutput` completion
precedes selection. The minimum preparation lead is 49.249/44.247 ms, with
medians 96.694/96.995 ms. These already-prepared frames are not waiting for
decoder output at their selection point.

| Trace observation | ON1 | ON2 |
| --- | ---: | ---: |
| Selected frames inside aligned window | 894 | 895 |
| Members of that selected cohort admitted to frame construction | 856 | 831 |
| Selected frames with completed submit attempts but no frame construction anywhere in the retained trace | 38 | 64 |
| Dropped-frame statistics inside window | 44 | 67 |
| Fixture VPQ dropped-frame delta inside window | 44 / 903 | 67 / 901 |
| Frame-construction marker end to reported presentation feedback, median ms | 22.232 | 26.518 |
| Frame-construction marker end to reported presentation feedback, p95 ms | 40.857 | 48.536 |

The 38/64 frame cohort is matched by PTS across the entire retained trace,
not obtained by subtracting unrelated window totals. Each has positive,
completed `SubmitFrame` attempts with no nested
`VideoFrameResourceProvider::AppendQuads`. That unconditional frame-construction
call independently cross-checks the conditional `Pre-submit buffering` marker:
the two sets of admitted attempt indices agree exactly in both traces. Missing
decode-end metadata therefore does not explain these missing constructions.
The affected frames specifically were prepared at least 57.643/52.368 ms
before selection. Their 50/103 unsuccessful construction attempts have median
scope durations of 8/9 microseconds, consistent with an early submission guard.
The feedback distribution uses admissions occurring inside
the time window; ON2 includes one such frame selected before that window,
making 832 timed admissions versus 831 admitted selected-cohort members.
The statistics updates are batched, so the table does not assert a one-to-one
same-instant mapping between each selected frame and a VPQ counter increment.
The VPQ deltas also differ from the earlier full-load headline counts by the
fixture's initial baseline, as designed.

The [pinned submission implementation](https://chromium.googlesource.com/chromium/src/+/151.0.7922.34/third_party/blink/renderer/platform/graphics/video_frame_submitter.cc)
can reject for sink/visibility state, duplicate frame identity, empty size or
an outstanding compositor acknowledgement. It continues selecting frames while
an acknowledgement is outstanding; unsuccessful submission skips the call
marking the current frame consumed. The trace has no dedicated acknowledgement
receipt or rejection-reason event. A visible, focused window does not directly
measure every internal guard. Thus acknowledgement backpressure is consistent
with the observed stage, not established as the particular rejection cause.

Frame-construction markers occur before actual submission IPC and before
acknowledgement. The presentation interval above ends at the supplied
presentation-feedback timestamp; it is neither an ACK-latency measurement nor
physical display latency. Nevertheless, the slower arm both admits fewer
selected frames and has a longer presentation-feedback tail. This supports
investigating compositor submission/feedback timing before decoder throughput
or a general scheduler rewrite. It does not explain every possible algorithm
drop or establish performance parity with Linux.

Reproduction and per-frame evidence are retained in
`chrome-trace-frame-analysis.json`, the per-arm frame-correlation and
clock-alignment files, `trace-analysis-reproduction.txt` and the offline
`xv6-analyze-chrome-trace.py` / `xv6-chrome-frame-analysis.py` scripts.
`trace-analysis-validation.json` records five pairing/boundary checks and
real-data assertions for unique frame matching, clock anchors, unconditional
construction-event equivalence, fixture drop deltas and endpoint sensitivity;
all pass. `trace-analysis-artifacts.json` retains artifact hashes. Pinned
source copies, URLs and hashes are in the sibling
`chromium-bottleneck-source-20260908/` receipt.

## GPU and pacing cross-check

The intact global GPU aggregate records surrounding each measurement give:

| Arm | Submissions | Inclusive post per call, ms | Initial operation-lock wait per call, ms | Requested pacing per event, ms |
| --- | ---: | ---: | ---: | ---: |
| OFF1 | 2645 | 2.323 | 0.113 | 5.244 |
| ON1 | 2600 | 2.223 | 0.094 | 4.331 |
| ON2 | 2528 | 2.399 | 0.111 | 4.877 |
| OFF2 | 2534 | 2.382 | 0.114 | 4.901 |

None of these aggregate averages orders all four arms by dropping. Posting
totals remain 5.779–6.144 seconds. The records have broader cleanup-triggered
windows and overlapping buckets, so this neither identifies an exclusive
posting component nor excludes intermittent long waits. Every Chrome teardown
owner record is damaged by interleaved console text; these records remain
unreconstructed and are excluded from whole-arm owner comparisons.

Async submitted/completed, GPU posted/retired and DRM queued/read counts match
at each endpoint. Timeout/failure, async-error/fallback and queue-drop/overflow
deltas are zero. The cumulative BO-hold maximum is unchanged within each arm.
Requested pacing and clock-snap counts do not measure timer lateness or actual
scanout. Full boundaries, raw rows, all 27 FB fields, input hashes and the
reproducible analyzer are retained in `fb-submit-comparison.{md,json}` and
`analyze-fb-submit-comparison.py`.

## Answer to the subsystem hypotheses

| Hypothesis | Verdict from this investigation |
| --- | --- |
| Missing softirq | Not a demonstrated cause. IRQ acknowledgement, waiter wakeup and deferred GPU reaping already exist. Worker dispatch latency remains measurable future work. |
| Lock implementations | Initial GPU operation-lock wait is much smaller than inclusive post cost. Reacquisition, spin/IRQ-disabled sections, reaper locking and VM locking are not individually timed; no particular lock defect is established. |
| Scheduler | Some guest runnable-wait averages increase in the slower traced arm. Host runqueue starvation is weak, but missing GPU/worker task coverage and absent latency distributions prevent a kernel scheduler verdict. |
| RCU | No observed grace-period stall or callback bottleneck. The current instrumentation does not sample RCU workers, so absence of evidence is not a complete exclusion. |

The next discriminating observation is a bounded in-memory record of
`SubmitFrame` rejection reason, outstanding-ACK count and ACK receipt timing,
correlated with the already identified lost frame PTS. On the kernel side,
carry owner identity locally and split the inclusive post timer into reap,
queue acquisition/publication, MMIO notification, unlocked admission and
operation-lock reacquisition. Keep IRQ-disabled duration as a nested metric,
and export after playback. Complete GPU/kernel-worker task coverage before
attributing a long interval to scheduling or RCU. No production policy or
kernel implementation changes are made on the basis of these aggregate data.
