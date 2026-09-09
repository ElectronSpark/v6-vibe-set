# Chromium kernel workqueue investigation — 2026-09-09 UTC

Status: **kernel workqueue repair validated: original predicate fails, fixed
predicate passes all four cases; completed browser comparison does not
establish a dropped-frame improvement**.
The workqueue manager can leave an idle worker
asleep while queued work waits behind an executing callback. This is a concrete
dispatch defect. Its contribution to Chromium's measured dropped frames has
not been established.

The user directed this investigation to use the **unchanged host-copied
Chromium browser and fix the kernel**. Do not rebuild Chromium, apply the
retired browser instrumentation, or stage a replacement browser. The preceding
[unchanged-kernel GUI capture](chromium-bottleneck-checkpoint-20260909.md) found that
each traced arm reported seven drops and six prepared, selected frames without
construction. Missing submission guards and per-sink ACK state remain missing;
this kernel source finding does not reconstruct them.

## Workqueue invariant and failure

Relevant source is [workqueue.c](../kernel/kernel/proc/workqueue.c),
[thread_queue.c](../kernel/kernel/proc/thread_queue.c), and
[timerfd.c](../kernel/kernel/vfs/timerfd.c). While `wq->lock` is held:

| Symbol | Counter or state | Meaning |
| --- | --- | --- |
| N | `nr_workers` | Workers belonging to this queue. |
| I | `tq_size(&idle_queue)` | Workers registered in the queue's idle wait list. |
| R | `running_works` | Callbacks already dequeued and executing, including callbacks blocked on another wait. |
| P | `pending_works` | Work items still in the queue. |
| D | `N - I - R` | Active workers created or awakened but not yet executing a callback, available to dequeue queued work. |

For normally operating active workers, `N = I + R + D`. The worker decrements
`pending_works` when dequeuing and increments `running_works` before dropping
the queue lock; it decrements `running_works` after reacquiring the lock when
the callback returns. `tq_wakeup()` removes idle-list membership before waking
the worker. The awakened worker cannot dequeue until the manager releases the
same queue lock. These transitions let the manager count already awakened
workers without counting busy callbacks as available dequeuers.

The old manager predicate was `N - I < P`. Consider a two-worker queue with
callback A running or parked on an unrelated wait, worker B idle, and one newly
queued callback C: `N=2, I=1, R=1, P=1`. The old test evaluates `1 < 1`, false.
`queue_work()` publishes C and wakes the manager, but the manager does not wake
B. C must wait for A to finish despite the idle worker. If A requires progress
from C, that dependency cannot complete through this path.

The fix at `workqueue.c:243` changes only the idle-worker predicate to
`N - I - R < P` and adds its lock invariant comment. In the same state the test
is `0 < 1`, so B wakes. Removing B from the idle list then makes the test
`1 < 1`, preventing an unnecessary further wake. Queue publication, callback
accounting, worker limits and `flush_workqueue()` semantics are unchanged.
The separate `min_active` expression is outside this candidate.

Timerfd uses a two-worker queue (`timerfd.c:759`). Its timer callback increments
readable state and wakes direct readers, then defers kqueue/epoll notification
through that queue (`timerfd.c:272–337`); the worker calls
`vfs_file_knote_notify()` at line 242. The defect can therefore delay a timerfd
notification when this queue reaches the described state. Existing captures
do not contain queue-specific enqueue/start/completion identity, so they do
not show whether that state occurred during the lost-frame intervals.
The 20-ms epoll rescan can rediscover readiness; it is not a measurement of
the original notification delay or a reason to shorten polling.

## Opt-in regression and validation

[workqueue_selftest.c](../kernel/kernel/proc/workqueue_selftest.c) creates a
private two-worker queue only when `workqueue_selftest=1` is present in the
kernel command line. The post-init hook otherwise returns before initializing
test state or creating a test thread/queue. Its four rounds comprise three single-item cases and
one four-item burst:

1. Establish two idle workers and an empty queue.
2. Run A and park it on a private thread queue; verify one running callback,
   one idle worker and no queued work.
3. Enqueue C (or the burst) and require every distinct probe to complete while
   A remains parked and before release is requested.
4. Unconditionally release A, then require a drained queue with both workers
   idle before reusing any item.

Waits have a two-second monotonic deadline and a poll cap. Failure releases A
even if it has not started yet; cleanup uses a bounded drain rather than an
unbounded `flush_workqueue()`. Queue, work items and callback state remain
allocated with static callback-state lifetime because there is no queue destroy
API. This avoids freeing pending work on a failed drain. Per-item membership
and completion-before-release checks reject duplicate or late completions.

Source review and `git diff --check` passed. The paired kernel builds and
initial regression observations are recorded below; candidate Chromium
performance is not yet established.

- [x] Build and run the baseline with the identical selftest/integration and
  only the old manager predicate. Require the single-item progress phase to
  fail while retaining its pre-release state and successful bounded drain.
- [x] Build and run the corrected predicate with the same test. Require all
  four cases to pass, including both-idle/empty cleanup, and complete the
  kernel build and required Sparse checks.
- [x] Compare the unchanged host-copied Chromium workload across baseline and
  candidate kernels using `scripts/launch/launch-gui.sh`. Preserve kernel
  identities, rootfs/browser/clip/fixture hashes, actual flags, viewport, focus,
  trace settings and trial order. Retain at least two matched trials per
  condition across fresh boots and all failures; report variation and serial
  overlap. A reducer pass proves the dispatch repair, not a video improvement.
- [ ] Follow the implicated timer/event path with queue identity and monotonic
  readiness, enqueue, worker start, notification and task-dispatch boundaries
  if media attribution remains unresolved. Preserve pre-enqueue and callback
  time separately; generic worker names and aggregate selected time cannot
  provide those intervals.

Each VM requires the repository's sole authorized VM lane, exact owned process
wait/reap, bounded cleanup and final exact zero-QEMU inventory. Preserve the
frozen kernel and base image; do not replace the normal launch policy. Actual
runtime receipts are retained below.

### Paired kernel build and initial VM results

The [paired build](../build-x86_64/gui-progress-audit/workqueue-idle-dispatch-paired-build-20260909T025529Z/final-verification.json)
uses kernel commit `81cbd0c5d408cf21a146bdc2fc09d0333e574ddc` with identical
selftest/integration sources. Only the wakeup predicate differs between the
two saved source patches. Builds used the matching configured tree directly:
`cmake --build build-x86_64/kernel/build --target kernel_all -j2`. This avoids
the umbrella target's internal six-job override. No Chromium, userland, port
or rootfs build ran.

| Variant | Kernel SHA256 | Build |
| --- | --- | --- |
| Original predicate | `b920d519900907e5ecf433a76830ac95d4d8e841364faa94435f9964f6ed33ec` | Exit 0, 31.0 seconds |
| Corrected predicate | `726e50282607d6659500ce74c437a5ec1e16f513778d75813bee03d49ce7e844` | Exit 0, 7.1 seconds |

Both `xv6.bin` files are 41,703,956 bytes; matching ELF/symbols and source/build
receipts accompany each. The candidate source was restored and hash-verified
after the temporary baseline predicate swap. Sparse checked 210 files with
zero failures/errors and exit 0; context warnings remain in its log, including
workqueue paths. This is not a warning-free result or a paired warning analysis.

The [baseline VM result](../build-x86_64/gui-progress-audit/workqueue-selftest-baseline-20260909T025914Z/result.json)
reproduces the exact starvation state in case 1: `before_release=0`,
`workers=2 idle=1 running=1 pending=1`. The progress phase times out, then the
unconditional release drains the queue (`drained=1`). The host collector
accepts this expected failure. Actual QEMU kernel and selftest arguments are
validated before continuing the initially paused VM.

Two unchanged candidate runs
([first](../build-x86_64/gui-progress-audit/workqueue-selftest-candidate-20260909T025934Z/result.json),
[repeat](../build-x86_64/gui-progress-audit/workqueue-selftest-candidate-20260909T030015Z/result.json))
both emit `END result=PASS cases=4`. That branch executes only after all four
rounds pass their before-release and bounded-drain checks. Concurrent init
output interrupts per-case console records, however, so the strict host
collector rejects both captures for incomplete record coverage. They are
positive internal completion evidence with a collection failure, not accepted
complete per-case captures. Kernel-ring readback is being added to obtain
complete records without modifying or rebuilding either test kernel. A
[readback attempt](../build-x86_64/gui-progress-audit/workqueue-selftest-candidate-20260909T030642Z/result.json)
then retrieved the ring in the guest, but asynchronous init/kernel output also
interrupted its base64 serial transfer. Integrity checks rejected that transfer;
its launcher/overlay/base/inventory cleanup passed. A separate HTTP transport
was added for the ring bytes, retaining short synchronous serial commands
for execution and completion checks. Its [first boot attempt](../build-x86_64/gui-progress-audit/workqueue-selftest-candidate-20260909T031257Z/result.json)
reached the upload before DHCP completed and failed with `EHOSTUNREACH`;
that collection failure and clean owned shutdown are retained. The collector
adds bounded network readiness after taking the ring snapshot: only
`ENETUNREACH`/`EHOSTUNREACH` connection probes retry, and the HTTP POST itself
is never retried.

The [completed candidate regression](../build-x86_64/gui-progress-audit/workqueue-selftest-candidate-20260909T031552Z/result.json)
then **passes all four cases with complete records**. Its unchanged kernel
SHA is `726e50282607d6659500ce74c437a5ec1e16f513778d75813bee03d49ce7e844`.
The host verifies the 14,456-byte ring snapshot SHA
`19cd5ec73850eec44d132b8637afedf561562351127c538edbc4ebec8dbe6f8b`,
the helper source hash, and every case independently:

| Case | Probes | Completed before releasing A | Pending at observation | Drained |
| --- | --- | --- | --- | --- |
| 1 | 1 | 1 | 0 | Yes |
| 2 | 1 | 1 | 0 | Yes |
| 3 | 1 | 1 | 0 | Yes |
| 4 | 4 | 4 | 0 | Yes |

The terminal verdict is `PASS cases=4`; the strict host result is accepted.
The HTTP loop and launcher are synchronously reaped, cleanup errors and
rejected uploads are empty, the base is unchanged, the overlay is removed and
exact QEMU inventory is zero. Eleven small collector tests cover bounds,
timestamp-prefixed records, readiness and single-upload behavior. This closes
the workqueue dispatch regression gate. Collection repairs occur after the
test and do not rebuild or alter either kernel.

All three launches use `scripts/launch/launch-gui.sh`, the protected rootfs and
private overlays. Each launcher exits 0 and is synchronously reaped; overlays
are removed, base stamps unchanged, cleanup errors empty and exact QEMU
inventory zero. Normal browser comparison boots omit `workqueue_selftest=1`.

The maintained [selftest controller](../scripts/gpu/workqueue-selftest.py) and
[media controller](../scripts/gpu/chromium-bottleneck/controller.py) accept
explicit sealed `--kernel-variant` receipts. The media controller retains the
normal host-copied Chromium policy; kernel selection cannot override the
browser, rootfs or fixture. It checks successful build/reap evidence, matching
kernel/source identities, streamed artifact hashes and prelaunch/final file
stability. Its 17 focused tests and 30 existing configuration/cleanup tests
pass. No browser diagnostic configuration is used in this comparison.

### Normal host-copied Chromium comparison

The [baseline run](../build-x86_64/gui-progress-audit/chromium-kernel-baseline-capture-20260909T030152Z/vm-owner-summary.json)
completed with the original workqueue predicate. The actual browser hash
matched `0b20b130e7edd9dd51873be867761295fe0cfad490c2b9a64f95bd3cfc08fa71`
(290,614,600 bytes), and the actual guest command line omitted the selftest.

| Baseline trial | Dropped/total frames | Drop rate | Serial bytes between host start/complete receipts |
| --- | --- | --- | --- |
| OFF1 | 18/908 | 1.982% | 8,560 |
| ON1 | 34/908 | 3.744% | 7,383 |
| ON2 | 18/910 | 1.978% | 8,898 |
| OFF2 | 19/910 | 2.088% | 7,481 |

All four measurements are usable, video errors are zero and each browser
exits 0. Pooled baseline drops are 37/1,818 (2.035%) OFF and 52/1,818
(2.860%) ON. No controller serial commands, QMP calls or screenshots occur
during playback, but every interval contains asynchronous serial traffic.
Chunk timestamps measure host receipt time, not exact guest emission or CPU
cost. The GL guest screendump returns no surface; actual host PNG inspection
establishes visible mapped fixtures.

Both traced JSON exports exceed the 64-MiB collection cap (70,040,251 and
70,040,485 bytes). They are rejected as direct JSON and preserved as hashed
gzip files of 9,878,692 and 9,901,197 bytes. Measurement usability is separate
from that export limit. The baseline controller/launcher exit 0, overlay is
removed, protected rootfs and kernel stamps are unchanged and independent
exact QEMU inventory is zero.

The first [candidate capture](../build-x86_64/gui-progress-audit/chromium-kernel-candidate-capture-20260909T030748Z/cleanup.json)
completed OFF1 playback at 11/911 drops (1.207%) and ON1 at 12/910 (1.319%),
both with valid playback and zero video errors. OFF1 exits 0. ON1 then reaches
the existing 35-second direct-child wait/completion deadline, so ON2 and OFF2
never run and the four-arm comparison is incomplete. No complete paired
improvement is established from the two lower observed rates.

The ON1 serial wait window lasts 35.090 seconds. It contains teardown messages
but no returned child exit value, completion marker or new shell prompt.
Positive serial traffic ends after 6.523 seconds. The last QMP status before
close is running; no guest process/trace-flush snapshot was taken at the
deadline. This establishes missing completion evidence, not a browser or
kernel hang. The failed capture exits 1; its owned launcher exits/reaps 0,
overlay is removed, protected stamps unchanged and final exact QEMU count is
zero. A bounded post-playback process watcher and longer exit wait are being
added to collect that missing boundary before a fresh candidate capture.
The [exit-observer validation](../build-x86_64/gui-progress-audit/chromium-exit-watch-checks-20260909T032020Z/validation.json)
passes 17 focused tests plus the 47 existing configuration, cleanup and
kernel-selection tests. The observer starts only after playback and telemetry,
uses the browser's exact PID/start identity, snapshots at 35 seconds if it
remains present, and distinguishes process disappearance from a confirmed
child exit status. Child wait is bounded at 90 seconds; the observer is reaped
before another trial. A deadline retains a fresh QMP/host image and attempts
serial-prompt recovery before further commands. Completed playback is saved
before close so that an exit failure cannot hide those observations.

The repeat uses the same kernel, rootfs, browser, clip, flags and measurement
window. This post-playback observation/exit-budget change is explicit; it does
not make the repeat a byte-for-byte reproduction of the earlier controller.

The [completed candidate repeat](../build-x86_64/gui-progress-audit/chromium-kernel-candidate-capture-20260909T032225Z/vm-owner-summary.json)
retains all four valid arms:

| Candidate trial | Dropped/total frames | Drop rate | Serial bytes between host start/complete receipts |
| --- | --- | --- | --- |
| OFF1 | 18/908 | 1.982% | 6,326 |
| ON1 | 24/908 | 2.643% | 8,639 |
| ON2 | 20/911 | 2.195% | 6,986 |
| OFF2 | 26/904 | 2.876% | 7,655 |

| Complete-boot cohort | Original predicate | Fixed predicate |
| --- | --- | --- |
| Tracing OFF | 37/1,818 (2.035%) | 44/1,812 (2.428%) |
| Tracing ON | 52/1,818 (2.860%) | 44/1,819 (2.419%) |
| All four arms | 89/3,636 (2.448%) | 88/3,631 (2.424%) |

The direction is mixed: untraced drops increase while traced drops decrease.
Overall rates are effectively similar in this small comparison. One complete
boot per kernel, within-boot variation, serial overlap and the explicit
post-playback observer change prevent an optimization or causation claim.
The earlier incomplete candidate's lower two rates remain separate rather
than being selected or pooled into the complete four-arm comparison.

The [bounded independent count analysis](../build-x86_64/gui-progress-audit/chromium-kernel-candidate-capture-20260909T032225Z/analysis/paired-metrics.md)
recomputes all ten completed playback observations from their fixture samples.
The table above uses full-load counters. Exact measurement-window pooled
rates are 87/3,619 (2.404%) baseline and 86/3,612 (2.381%) candidate.
After-startup rates decrease from 51/3,129 (1.630%) to 34/3,126 (1.088%),
while startup rates increase from 36/490 (7.347%) to 52/486 (10.700%). This
redistribution is retained without assigning it to the kernel change. Source,
browser/fixture/clip/rootfs, actual exposed arguments and geometry parity
checks pass; the observer and tracing/serial limitations remain explicit.

All repeated candidate browser exits and watcher reaps return 0, video errors
remain zero, and no 35-second checkpoint triggers. Host monotonic child-wait
intervals are 0.705/6.666/7.196/0.603 seconds in trial order; the earlier exit
timeout does not reproduce. These replace provisional UTC-derived durations,
which differ from monotonic elapsed time. Both oversized traced exports are
preserved as gzip. Mapped host images and the final visible grid/PASS/26-drop
image were inspected; guest GL screendumps still have no surface.

The repeat controller exits/reaps 0, owned launcher and QEMU cleanup pass,
overlay is removed, protected inputs remain unchanged and independent exact
QEMU inventory is zero. The dispatch correctness repair is complete; the
kernel cause of the remaining media drops is still unproven.

### Current video result versus the recorded Linux control

The [completed September 8 Linux control](chromium-video-drop-investigation-20260908.md#completed-linux-comparison)
uses the same browser executable and clip. Its retained fixture also hashes to
`d5889aa784b8285ad7314a349db8817d71bf83c8792a53122f81ea74a7aef5bf`, matching
the current fixture. Compare the untraced arms: Linux's historical ON arms
enable Wayland logging, whereas the current ON arms enable Chromium tracing.
Those ON settings are not equivalent.

| Untraced video counter window | Current fixed xv6 | Recorded Linux |
| --- | --- | --- |
| Full load | 44/1,812 (2.428%) | 56/1,815 (3.085%) |
| Exact measurement window | 44/1,804 (2.439%) | 56/1,807 (3.099%) |
| After roughly two seconds of startup | 14/1,563 (0.896%) | 41/1,562 (2.625%) |

Both controls play near real time with no HTML video error. The current
sample is in Linux's observed video-performance range, with lower descriptive
drop counts. It does not establish that xv6 is faster or generally at parity:
Linux was measured in an earlier boot, its viewport is 1018×563 versus xv6's
1018×592, it uses native Linux graphics libraries and `--password-store=basic`,
and the current xv6 kernel diagnostic/serial activity remains present.
There are only two untraced trials per completed boot.

The earlier direct callback comparison measured Linux means of about 2.5 ms
versus the then-tested xv6's 6.5–10.7 ms. These are historical client-observed
request/reply intervals, not a latency result for today's fixed kernel.
Current matched callback latency, input responsiveness and CPU-cost parity
remain unmeasured. A fresh Linux control with matching geometry and tracing
policy is needed before a stronger system-performance claim.

The watcher must preserve the limits of this kernel's procfs: `stack` emits
synthetic context rows rather than an unwound kernel backtrace; `wchan` is
`p->chan`, which `tq_wait()` and `sleep_ms()` do not populate; and `syscall`
exposes saved trap registers, including a return value in `rax` after syscall
completion. `R` includes runnable off-CPU tasks. The full status string
distinguishes timer wait `Tm` from stopped `T`, while the stat file truncates
both to `T`. These fields cannot alone identify a workqueue wait, active
syscall, current instruction, or on-CPU execution. Preserve their raw values
and use an actual matching-kernel stack capture if the repeated exit stalls.

### Next kernel-only attribution checkpoint

The reducer establishes the dispatch bug independently of media. The remaining
performance question is whether a timer/event notification reaches this
workqueue state during a lost-frame interval. Collect these boundaries before
changing scheduler, lock, RCU or polling policy:

1. Timerfd expiration: context/generation identity, intended deadline, actual
   expiration time and pending expiration count.
2. Workqueue enqueue and worker entry: queue/work/generation identity, enqueue
   and start timestamps, and pending/running/idle counts under the queue lock.
   Preserve manager wake/dispatch separately from callback execution.
3. Event notification: callback entry/exit, epoll/kqueue notification and
   consumer wakeup, tied to the same context and stable task identity.
4. Scheduling: wake/enqueue/first-dispatch times for the affected consumer,
   calibrated to the same boot's clock. Keep host vCPU wait and guest selected
   time separate.

Use opt-in bounded in-memory records with explicit overflow/coverage evidence;
export after playback rather than printing each event to serial. Keep the
host-copied Chromium unchanged and repeat controls across fresh boots. Existing
aggregate task counters, synthetic procfs stack rows and opaque bundled ACK
messages do not supply these per-event boundaries.

## Timerfd follow-up checkpoints

These are separate source findings and hypotheses about possible runtime
effects. No timerfd/timer code was changed for the workqueue comparison, and
none is an established Chromium drop cause.

| Checkpoint | Source evidence and possible failure | Required isolated evidence |
| --- | --- | --- |
| TF-01 — Periodic overruns | `timerfd.c:279–283` counts elapsed periods at callback time, but delayed rearm work at `:226–227` replaces an overdue deadline with `now + interval` without adding the intervening periods to `expirations`. This can undercount periodic expirations and shift their phase. | Deliberately delay the worker for a short periodic timer; compare returned expiration counts and deadlines against elapsed periods. Establish whether Chromium uses repeating timerfds before relating this to its playback. |
| TF-02 — Stale rearm after disarm or replacement | The worker captures `do_rearm` at `timerfd.c:224–229`, unlocks at `:239`, then removes/rearms the timer at `:249–251` without a generation or cancellation recheck. Concurrent `settime` disarms/replaces state at `:607–618` and `:672–676`. A stale worker can overwrite a replacement deadline or rearm after disarm. | Use a controlled worker barrier and concurrent disarm/new deadline; require no event after disarm and no overwritten replacement. Record the exact interleaving before designing a fix. |
| TF-03 — Callback and final-reference lifetime | The worker's last notification-file put at `timerfd.c:243` can synchronously call file release (`file.c:120–122`), marking the context cancelled while retaining it for pending work (`timerfd.c:406–419`). The previously captured rearm can then run before the worker frees the context at `:264`. Separately, `timer_remove()` returns for a detached node (`timer.c:178–186`), while retry-one expiration detaches and unlocks before invoking its callback (`:246–248`); removal is not an in-flight callback barrier. | Exercise final-close during deferred rearm and cancellation during a detached but not-yet-completed callback with controlled barriers and lifetime checks. Audit caller-owned timer storage before changing cancellation semantics. Do not treat timer removal as proof that callback data is no longer in use. |

The timer callback's “timer lock held” comment does not cover its actual
retry-one path: `sched_timer_set_cb()` uses retry limit one
(`sched_timer.c:183–194`), and `timer_tick()` unlocks before that callback.
Any lifetime reducer must follow the implementation rather than this comment.

## Retired Chromium build and completed cleanup

The browser source-build attempt was stopped and synchronously reaped at the
user's direction before producing a complete diagnostic browser. No diagnostic
runtime or image was staged or booted. Task-created downloaded inputs and build
outputs were removed under that explicit cleanup authorization.

The retained [final cleanup receipt](../build-chromium-diagnostics-151.0.7922.34/receipts/user-cleanup-final.json)
reports **38,433,316,864 allocated bytes removed (35.79 GiB)** and
**18,759,680 bytes retained**, with build-process and exact QEMU counts zero.
The host browser, frozen kernel and rootfs were preserved. Compact receipts
and the [historical source/parser work](chromium-submission-instrumentation-20260909.md)
remain available; they do not authorize resuming the browser build or provide
runtime guard/ACK evidence.

This updates the [active work plan](active-work-plan.md) and the kernel side
of [BT-04](chromium-ack-boundary-investigation-20260908.md#investigation-checkpoint--deeper-evidence-collection).
The broader rejection/ACK, loss-accounting, scheduler, lock, RCU and console
questions remain qualified by their existing evidence gaps.
