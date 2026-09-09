# Chromium submission and notification boundary — 2026-09-08 UTC

This continues the [bottleneck investigation](chromium-bottleneck-investigation-20260908.md).
An enhanced trace reproduces the high-drop symptom and identifies 218 prepared,
selected frames that never reach frame construction. It also captures the
previously missing Chromium GPU process and the actual Viz-to-video-thread
notification path. The notifications are bundled; their ACK entries and the
submission rejection reasons remain unobserved. These results do not identify
a scheduler, lock, deferred-interrupt or RCU defect.

Current follow-up: the user stopped the browser source build and required the
unchanged host-copied Chromium runtime. Build cleanup is complete. A separate
[kernel workqueue investigation](chromium-kernel-workqueue-investigation-20260909.md)
finds an idle-worker dispatch defect and implements a minimal fix plus an
opt-in regression. Runtime validation and its relationship to these frame
losses remain pending; the historical observations below are unchanged.

## First diagnostic boot

Receipt: `build-x86_64/gui-progress-audit/chromium-ack-capture-20260908T041046Z/`.
The frozen kernel, protected base image, Chrome executable, 1280×800/60 clip
and fixture match the preceding investigation. Kernel SHA-256 remains
`59253eb271555b6e1e3e035280002d79a7fc82f3e236b288ca981a0e495ed104`.
The owned launcher uses a private overlay, SDL/virgl, six vCPUs and 8 GiB.
There is no kernel, browser or rootfs rebuild and no production-policy change.
The effective command, identities, helper hashes and source state are retained
in `provenance.json`, `qemu-cmdline.json` and `source-state.txt`.

All arms keep `WAYLAND_DEBUG` disabled and the existing
`virtio_gpu_submit_trace=1` kernel diagnostic enabled. ON additionally records
`media,cc,viz,benchmark,mojom,mojom.flow,graphics.pipeline,disabled-by-default-mojom`
in a 32-MiB ring. The accepted JSON export limit remains 64 MiB. Browser tracing
begins at launch, before the graphical readiness review and measurement gate.

| Arm | Browser tracing | Dropped / total | Drop rate | Playback speed | Serial bytes between host start/complete receipts |
| --- | --- | ---: | ---: | ---: | ---: |
| OFF1 | Off | 223 / 900 | 24.78% | 0.9850 | 6,887 |
| ON1 | Enhanced | 303 / 872 | 34.75% | 0.9505 | 7,820 |
| ON2 | Enhanced; export rejected | 25 / 911 | 2.74% | 1.0000 | 13,413 |
| OFF2 | Not attempted | — | — | — | — |

All three completed fixture observations are diagnostically valid, visibly
reviewed, focused and windowed at 1018×592. No HTML video error occurs. ON1
still drops 34.21% after startup. Every browser exits through its direct-child
wait with status zero. A host notification partially overlaps the first boot's
lower-right captures; the visual review records that limitation. GL guest
screendump again reports `no surface`; host captures supply the visible evidence.

No commands, input or screenshots are issued during playback, but asynchronous
kernel aggregates reach serial during **every** arm. None is a silent control.
The byte counts use host report-receipt brackets, not exact guest emission or
playback timestamps. The large within-setting variation, serial activity and
enhanced tracing prevent attributing the difference between OFF1 and ON1 to
tracing alone.

ON1 exports 59,784,909 bytes successfully. ON2 fails the combined
regular/nonempty/64-MiB export gate; that version did not retain the failing
file's metadata or original contents. Its exact failure condition is unknown.
The error and completed playback observation remain, but ON2 receives no trace
coverage credit. The export error ends the original controller before OFF2.
The controller is synchronously reaped; launcher exit is zero, overlay removal
and unchanged base metadata pass, and worker and conductor independently verify
exact zero QEMU processes. See `worker-review-and-cleanup.json` and `cleanup.json`.

## Completed repeat and preserved export failures

Receipt: `build-x86_64/gui-progress-audit/chromium-ack-capture-20260908T041754Z/`.
The repeat preserves the same kernel/image/workload, categories and 32-MiB
ring. Its controller records exact rejected-export metadata, preserves bounded
originals as gzip archives, and continues the remaining arms after an export
failure. This changes post-playback evidence handling, not playback policy.

| Arm | Dropped / total | Drop rate | Playback speed | Serial bytes between host start/complete receipts |
| --- | ---: | ---: | ---: | ---: |
| OFF1 | 69 / 913 | 7.56% | 0.9977 | 7,833 |
| ON1 | 64 / 904 | 7.08% | 0.9959 | 0 |
| ON2 | 118 / 905 | 13.04% | 0.9898 | 0 |
| OFF2 | 65 / 909 | 7.15% | 0.9971 | 6,959 |

All four playback observations are diagnostically valid, visibly reviewed at
the same viewport, focused and free of HTML video errors. The first boot's
host notification occlusion is absent. Every browser and the controller exit
zero; launcher exit, overlay cleanup, unchanged base metadata and independent
worker/conductor exact zero-QEMU checks pass.

The traced pair has no serial output between host report receipts, supplying
a cleaner same-setting comparison. OFF controls still overlap asynchronous
kernel output. The two boots establish repeatable variation, not a fixed
tracing penalty or matched Linux parity.

Raw ON1/ON2 JSON sizes are 69,535,275 and 67,513,443 bytes, both explicitly
rejected by the 64-MiB admission gate. Complete originals are preserved as
`on-1-rejected-chromium-trace.json.gz` and
`on-2-rejected-chromium-trace.json.gz`, with raw SHA-256 respectively
`9941a464f7b5257f75bfdcb28d9152feda567d7fbddbf2ab294dfe3e82c9e775` and
`ed8bddde667496a8afbd297318641d8e2aab1589ef83f14860c3378344abb4dc`.
Lossless whitespace compaction still exceeds the limit at 69,171,463 and
67,161,018 bytes; failed transformation manifests are retained. No shortened
trace receives coverage credit.

The preserved originals are subsequently recovered through explicit manifests
and ordered JSON event parts, each no larger than 32 MiB, plus a metadata file.
Decompression remains bounded at 128 MiB, each analysis input at 64 MiB, and
the parser at 2 GiB of address space and 90 seconds. Raw hashes match the
preserved originals, and reconstructed structural hashes match the decoded
original JSON. All 363,811 / 352,424 events,
their order, values and metadata are retained. This is a verified derivative;
the original export rejection and untouched archive remain in the receipt.
Manifests are `analysis/on-{1,2}-trace-shards/manifest.json`.

| Repeat trace observation | ON1, 7.08% drops | ON2, 13.04% drops |
| --- | ---: | ---: |
| Selected / constructed selected-cohort frames | 887 / 833 | 867 / 781 |
| Selected frames never constructed | 54 | 86 |
| Completed attempts for those lost frames | 81 | 126 |
| Lost-cohort preparation lead, minimum / median ms | 0.139 / 95.197 | 32.146 / 104.235 |
| Dropped-frame trace statistics / fixture delta | 64 / 64 | 118 / 118 |
| Construction marker to reported presentation, p95 ms | 43.216 | 56.722 |
| Video begin-frame start gap, p95 / maximum ms | 24.115 / 50.013 | 24.835 / 67.556 |

The poorer arm again loses more already-prepared frames after selection and
has a longer reported presentation tail. These cohorts leave other drops
unexplained. Begin-frame intervals also have a longer tail, so do not collapse
the whole pipeline into one submission rejection. Presentation feedback is not
physical scanout. The independently aligned windows have ±222 / ±240 µs
endpoint uncertainty; their coverage and sensitivity checks are retained with
the per-trial frame and Mojo analyses.

All 1,867 / 1,788 playback bundle notifications in ON1/ON2 have unique message
flows from the same-run GPU Viz thread to the renderer video thread. Message
construction-to-receive medians are 0.479 / 0.496 ms, p95 values 2.257 / 3.496 ms,
and maxima 19.278 / 22.078 ms. Their arguments still contain no decoded ACK
payload. Thus the failing repeat has more lost prepared frames while most
observed bundle messages reach the video thread within a few milliseconds;
these timings do not establish when the missing ACK entries were produced.

The conditional association with failed attempts also repeats. Linked parent
messages cover 53 of ON1's 54 lost PTS and all 86 of ON2's lost PTS. Their
message-construction-to-receive p95 is 3.984 / 7.359 ms, versus 2.430 / 3.018 ms
for linked successful constructions in the corresponding arm. Extra attempts
and constructions without a linked parent remain separate. This compares
observed notification delays, not acknowledged frames or a proven return guard.

Each whole trace also records one bundle-registration request, one bundled
sink-creation request and one embedder-connection request from the video thread
to the browser, all before playback. No additional matched creation request,
direct-sink creation or `SubmitEmptyFrame` event is observed. These are observed
initialization requests, not proof of successful creation or absence of every
untraced counter reset. Same-run roles, flow coordinates and lifecycle request
coverage are retained in `analysis/on-{1,2}-mojo-flows.json` and
`analysis/mojo-flow-comparison.json`.

The same boot measures TSC at 2,538,311,000 Hz. All four GPU processes have
12 matched and one new-after thread; all 53 kernel threads are retained.
For ON1/ON2, Viz runnable wait is 750.6 / 993.0 ms, averaging 0.148 / 0.203 ms
per queued interval. The six host vCPUs' total runnable waits are 20.2 / 18.8 ms.
The failing arm again has greater guest Viz wait without greater host runqueue
wait. Snapshot duration, selected-time and absent per-event latency limitations
remain. No GPU timeout/failure, async error/fallback or DRM queue-loss delta is
observed in any repeat arm. Full results are in `analysis/run-summary.json`.

## Prepared frames still fail after selection

The first boot's ON1 trace retains the entire playback window on the relevant media, video,
GPU and Viz threads. Reported ring overwrite, wrap, loss and parser-failure
counters are zero. The main ring writes 14,614,528 bytes, less than half its
capacity; reducing its capacity would not reduce this JSON export without
discarding retained events. The auxiliary
256-KiB buffer also reports zero loss.

The independently aligned fixture window has ±235 µs endpoint uncertainty.
All four combinations of endpoint extremes preserve the same selected,
constructed and lost PTS cohorts. The parser also reproduces the earlier
38/64 lost-frame cohorts with unchanged timing and PTS results.

| ON1 playback observation | Count |
| --- | ---: |
| Selected frames | 783 |
| Selected-cohort frames admitted to construction | 565 |
| Selected frames never constructed anywhere in the retained trace | 218 |
| Completed submission attempts for those 218 frames | 315 |
| Those attempts inside a video `OnBeginFrame` and bundle-receive scope | 217 |
| Those attempts outside `OnBeginFrame` | 98 |
| Reported dropped-frame statistics / fixture counter delta | 303 / 303 |

Every lost-cohort frame has already completed preparation before selection:
minimum lead 3.965 ms, median 112.753 ms. Their failed submission scopes have
median duration 10 µs and maximum 6.244 ms. Unconditional nested `AppendQuads`
cross-checks construction admission; clock boundaries and event pairing do not
explain this cohort. The 218-frame cohort does not account for every one of the
303 reported drops, and preparation of selected frames does not exclude loss
before selection.

Evidence and reproducible analysis are in `analysis/on-1-frame-analysis.json`,
the corresponding frame correlations/alignment, and
`analysis/on-1-frame-mojo-validation.json`.

## What the new message boundary reveals

The same trace identifies GPU PID 417, `CrGpuMain` TID 417 and
`VizCompositorThread` TID 528; renderer PID 457 has `Media` TID 535 and
`VideoFrameCompositor` TID 536. These are same-run coordinates, not reusable
process selectors.

The video thread receives 1,544 `FrameSinkBundleClient::FlushNotifications`
messages during playback and zero direct `CompositorFrameSinkClient` ACK calls.
Matching interface tag, stable method hash, process/thread and scope nesting
positively establishes the bundled path. The receive arguments expose method
identity and byte counts, without decoded ACK arrays, sink IDs or frame IDs.
A bundle can carry begin-frame notifications without an ACK.

Explicit flow edges provide unique message-construction → send → receiver
`Connector::DispatchMessage` → bundle-dispatch chains for 1,543 of those 1,544
receipts. One ambiguous equal-coordinate chain is excluded. All matched sources
are the same GPU Viz thread. Construction-to-video-dispatch latency has median
1.045 ms, p95 8.809 ms and maximum 43.400 ms. Send-to-Connector latency has
median 0.934 ms and p95 8.323 ms; Connector-to-method dispatch has median 24 µs
and p95 141 µs. These measure bundled message progress, including transport
and scheduling, **not video ACK latency or isolated socket latency**.

The 217 failed attempts with a linked parent notification cover 217 distinct
lost PTS out of 218. Their message-construction-to-receive latency has median 1.459 ms
and p95 12.665 ms, versus 1.084 ms and 7.197 ms for 473 successful constructions
with a linked parent. The remaining attempts/constructions remain separate.
This conditional association does not identify the ACK contents or rejection
guard; `analysis/on-1-flow-attempt-correlations.json` retains the full join.

The `Graphics.Pipeline` per-sink begin-frame send marker can precede bundle
assembly. It is not substituted for the actual message-construction/send
boundary. Flow matching, excluded cases, original event coordinates and parser
validation are retained in `analysis/on-1-mojo-flows.json` and its analyzer.

## GPU scheduling coverage is repaired

The omitted GPU processes use `argv[0]=/proc/self/exe`; their executable links
identify the frozen Chrome binary. The collector now records executable,
argv, process/task start identity, identity stability, exclusions and read
failures. Each completed arm contains 12 matched GPU threads, one new GPU
thread after playback, and 53 matched kernel threads. Renderer descendants can
still expose inherited zygote argv, so runtime trace metadata supplies their
roles. No matched task is rejected for malformed/regressing counters or
duplicate identity in these arms.

| Arm | GPU main selected / runnable wait, ms | Viz selected / runnable wait, ms | Viz wait per queued interval, ms | Six host vCPUs' combined runnable wait, ms |
| --- | ---: | ---: | ---: | ---: |
| OFF1 | 9,267.5 / 771.9 | 2,115.7 / 1,480.2 | 0.316 | 25.7 |
| ON1 | 8,670.6 / 867.6 | 3,753.0 / 1,575.9 | 0.355 | 37.3 |
| ON2 | 7,559.7 / 742.8 | 2,565.6 / 809.0 | 0.160 | 19.3 |

Guest conversions use this boot's measured TSC frequency, 2,553,855,940 Hz.
Sequential guest snapshots bracket wider per-counter intervals of about
16.84–19.10 seconds. They do not provide per-drop scheduling latency, a wait
distribution or notification/pre-enqueue delay. Selected time can include host
vCPU descheduling. Host counters remain a separate nanosecond measurement.
The larger Viz wait in the failing arm is an association, not a causal scheduler
verdict; sustained host runqueue starvation remains weak.

Workqueue workers are all named `worker_thread` and managers `manager_thread`
in current source. Their snapshots do not identify the `vgpu-reap`, timer or
RCU queue. Collecting them closes an omission without supplying queue-specific
latency. GPU/DRM errors, timeouts and final pending queues remain zero. Requested
ideal pacing per event is approximately 0.432 / 0.156 / 7.337 ms across these
arms; it excludes actual timer lateness and cannot be added to overlapping
submission costs. All endpoints and qualified deltas are in
`analysis/run-summary.json` and `*-scheduler-analysis.json`.

## A newly visible background polling path

The expanded task inventory also exposes a concrete source-level omission in
the console feeder. `consoleintr()` publishes input into `tty_inbuf` without
notifying the feeder. `console_tty_input_thread()` checks that ring and calls
`sleep_ms(1)` whenever it is empty
(`kernel/kernel/console.c:696-721,826-838`). This is periodic polling rather than
an input-arrival wakeup. The source history predates the frozen kernel.

In the repeat boot's two traced arms, `tty_input` accumulates 3,338 / 4,058 ms
of selected time and 7,324 / 7,332 accounted queued intervals. Both playback
intervals have no input commands and zero serial bytes between host report
receipts. The wider snapshots include surrounding commands, and selected time
includes interrupts and possible host descheduling; these are not isolated
poll-loop CPU costs. The 7.08%-drop and 13.04%-drop arms do not establish that
the feeder caused either result.

This narrows a useful follow-up to empty-loop/received-byte counters and
notification-to-feeder dispatch timing. A change to an event-driven feeder
would need an empty-to-nonempty/wait-registration race regression and matched
performance trials. No such kernel change is made here. Source identity and
same-run counters are retained in the repeat receipt's
`analysis/tty-input-polling-findings.json`.

Host/guest clock comparisons require a separate qualification. First-boot
bracketed host snapshots show approximately 1.62–1.68-second decreases in the
host realtime-minus-monotonic offset. The mechanism is not established. Trace
alignment uses guest monotonic time, and host interval analysis uses host
monotonic receipts. The fixture's playback window is shorter than its report
interval; comparing host report arrivals directly with playback duration would
misstate clock drift. No independent drift or calibration-cause verdict is
made. See the first receipt's `analysis/clock-qualification.{md,json}`.

## Remaining discriminating evidence

The fixture's `requestVideoFrameCallback` registration can keep begin-frame
requests enabled independently of normal submission visibility. Continued begin-frame
and frame-selection activity therefore does not prove that the video's
`ShouldSubmit()` guard is open. Visibility setters, sink rebind and context-loss
counter resets lack dedicated trace events. An empty-frame submission is
traced, but hidden-state cleanup is delayed; its absence does not exclude a
short internal visibility change. PTS and dimensions also do not expose the
unique frame identity used by the duplicate guard.

The [pinned video submitter](https://chromium.googlesource.com/chromium/src/+/151.0.7922.34/third_party/blink/renderer/platform/graphics/video_frame_submitter.cc)
and [video compositor](https://chromium.googlesource.com/chromium/src/+/151.0.7922.34/third_party/blink/renderer/platform/graphics/video_frame_compositor.cc)
support these distinctions. The complete source chain, hashes and guarded
direct-path experiment conditions are retained in
`chromium-ack-boundary-source-20260908/ack-review-findings.{md,json}`.

The next exact rejection verdict requires a bounded per-sink record of the
guard reason, unique frame ID, visibility/force flags, size-change state,
outstanding ACK count, ACK entries/receipt and reset epoch. A separate
`UseVideoFrameSinkBundle`-disabled diagnostic can expose individual ACK calls,
but changes batching and still cannot reveal untraced guard/reset state.
On the kernel side, worker-to-queue identity and event-correlated wake/dispatch
latency remain missing; inclusive GPU post time still needs its previously
identified decomposition. This evidence does not justify a policy rewrite.

The maintained collector/controller is in
[`scripts/gpu/chromium-bottleneck/`](../scripts/gpu/chromium-bottleneck/README.md).
Offline frame, Mojo, flow and scheduler analysis operates on explicitly named
receipts, checks size/identity and writes fresh results. Tool validation includes
the retained 38/64 cohorts, new full-window checks, 11 pairing/negative tests,
and scheduler identity/counter/role checks. Final shared-loader validation adds
16 focused tests, complete shard reconstruction checks and a fresh rerun of
both original standalone traces preserving the exact 38/64 cohorts. See
`chromium-offline-analysis-20260908-v1/tooling-validation.json` and
`chromium-task-coverage-validation-20260908/scheduler-baseline-checks.json`, plus
the repeat receipt's `analysis/tooling-validation.json` and the final golden
comparison in `chromium-offline-analysis-20260908-v2/`.

## Investigation checkpoint — deeper evidence collection

Status: **open; field mapping and the later capture/ledger are complete, but
exact rejection/ACK and correlated kernel timing remain missing**.
This retains the evidence requirements for **DESK-03** in the
[active work plan](active-work-plan.md). The starting result is 54/86 prepared,
selected frames lost before construction in the cleaner traced pair, with
opaque bundled ACKs and no demonstrated kernel cause. Preserve the earlier
218-frame cohort and failed/rejected captures as separate observations.
The current next action is the [kernel workqueue reducer and unchanged-browser comparison](chromium-kernel-workqueue-investigation-20260909.md).
Browser source instrumentation remains historical and must not be built or
staged under the user's current direction.

- [ ] **BT-01 — Establish the collection contract.** Inventory which required
  fields below the pinned browser and kernel actually expose before another
  boot. The [field-to-producer map](chromium-bottleneck-field-map-20260908.md)
  now records browser/kernel availability and the smallest missing hooks;
  hook implementation and emission validation remain open. Extra trace
  categories alone cannot recover compile-time-disabled Mojo
  payloads or absent guard events. Record missing hooks and the smallest bounded,
  opt-in kernel observational instrumentation needed. The host-copied browser
  remains unchanged; unexposed browser fields stay open. Any kernel candidate
  gets its own source diff, hashes and receipt; preserve the frozen baseline and ordinary
  launcher policy. Use the maintained controller/analyzers, check their receipt
  and helper dependencies, and retain same-run executable/process/thread start
  identities. Done when a field-to-producer map and capture validation plan
  exist; an uncollectable field stays explicitly open.

- [ ] **BT-02 — Record why each submission attempt returns.** At entry and
  every return/accepted-construction boundary, record monotonic timestamp,
  process/thread identity, sink/bundle identity and validity, reset epoch,
  attempt ID/origin, PTS, unique and last-submitted frame IDs, natural/output
  dimensions, size-change state, visibility and force-begin-frame flags, pending
  ACK count and exact return reason. Trace the setters and lifecycle transitions
  that change those values. Join preparation, selection, attempt and construction
  without treating PTS as a unique frame identity. Done when failed and successful
  attempts in each accepted playback
  window have explicit outcomes, with unmatched or ambiguous records counted.

- [ ] **BT-03 — Follow real ACK state through the bundle.** Record ACK creation,
  bundle membership/flush, actual message send, receiver dispatch and per-sink
  application, including resource-reclaim callbacks, pending-count transitions
  and lifecycle resets. Retain begin-frame-only bundles. Use actual protocol
  identity or an explicitly validated ordering relationship; do not invent a
  frame ID for an ACK that lacks one. Done when the outstanding count at a rejected attempt can be
  reconciled from submission, ACK application and reset events, and any delay
  can be placed before ACK production, in batching, in delivery or in dispatch.
  A bundle-disabled run is a separate diagnostic that changes batching; it
  cannot replace evidence for the normal bundled path.

- [ ] **BT-04 — Split event notification from scheduling.** For the path
  implicated by BT-02/03, retain event/correlation ID, target task identity,
  CPU, workqueue identity and timestamps for readiness, notification, enqueue,
  runnable transition, dispatch and callback completion. For timer-driven work,
  include requested deadline, rounded deadline, expiry, work enqueue and epoll
  notification; include present-clock lateness where relevant. Distinguish
  `vgpu-reap`, timer and RCU queues even when their threads share a name. Done
  when matched successful/failed frame paths have stage durations and tail
  distributions, including pre-enqueue delay. Keep host vCPU waits and guest
  selected time separate; aggregate task totals do not satisfy this item.
  The [workqueue source finding and regression](chromium-kernel-workqueue-investigation-20260909.md)
  now identify an avoidable dispatch delay when an executing callback is
  incorrectly counted as an available worker. Runtime old/new validation and
  queue-specific timing remain pending; this item is not closed.

- [ ] **BT-05 — Attribute GPU posting and locking costs.** When the frame path
  implicates GPU posting, split initial operation-lock acquisition, unlocked
  admission/progress wait, lock reacquisition, reaping/freeing, queue-lock wait,
  descriptor publication and device notification. Carry an operation-local
  owner/correlation ID across unlocked sections and record overlapping spans.
  Allocation/copying before `post_us` remains a separate stage. Done when the
  delayed operation can be attributed without subtracting overlapping buckets
  or relying on the shared current-owner slot. Instrument a particular lock or
  RCU path only when evidence leads there; backlog alone is not a verdict.

- [ ] **BT-06 — Measure the console polling contribution.** Collect empty-ring
  iterations, sleep requests, timer-registration successes/failures and wake
  reasons, bytes published/consumed, input-to-feeder dispatch time and bounded
  execution spans over the actual playback window and an idle control. A sleep
  request is not proof of a completed timer sleep: registration failure in
  `sleep_ms()` falls back to one yield. Separate surrounding serial commands,
  interrupts and host descheduling
  from feeder execution. Done when idle wake frequency and execution cost are
  measured independently of selected-time totals. If an event-driven candidate
  follows, first exercise empty-to-nonempty arrival before, during and after
  wait registration, plus bursts and repeated sleep/wake, proving no lost input
  or missed wakeup. Evaluate its media effect only in a matched comparison.

- [ ] **BT-07 — Account for the remaining frame losses.** Trace preparation,
  selection, construction, submission and reported presentation, and reconcile
  their cohorts with fixture drop counters. The
  [next capture and validated ledger](chromium-bottleneck-checkpoint-20260909.md#offline-cohort-ledger)
  now enumerate candidate absences and feedback endpoints; exact discard
  identity/reason remains open. Preserve frames lost before selection,
  repeated attempts, trace-boundary cases and unmatched presentation feedback as
  distinct outcomes. The current selected-cohort partition and batched drop-counter
  updates use different membership/timing rules; subtracting their totals does
  not identify residual frames or prove that they were lost before selection.
  Done when all reported losses are either tied to a supported
  stage or listed with the exact missing evidence; do not silently assign the
  residual to the already-proven selection-to-construction gap. Reported
  presentation and rVFC activity still receive no physical-scanout credit.

Every collection receipt must pass these gates before it supports a conclusion:

- Preserve browser/kernel/rootfs/fixture/clip identities, actual flags, decoder,
  viewport, focus, playback bounds and source/diagnostic differences. Use at
  least two matched trials per condition across fresh boots, preserve trial
  order and failures, and report distributions as well as aggregate drop rates.
- Establish silent playback controls, including asynchronous teardown output;
  retain serial-byte brackets and qualify their timing. Avoid input, screenshots
  and collection commands during playback. Compare instrumentation off/on
  separately from a candidate behavior change. Record bounded diagnostics in
  memory and export after playback instead of printing per-event records.
- Validate complete event coverage, loss/overwrite counters, task identities,
  clock domains and alignment sensitivity. Preserve rejected exports and use
  bounded, hash-verified shards where required; do not shrink away evidence.
  Exercise new joins with missing, duplicate, reset and ambiguous-event cases.
- Retain per-attempt/per-message/per-stage outputs, exclusions and unresolved
  fields in the receipt, then update this checklist and the active plan. Follow
  root `AGENTS.md`: one authorized VM lane, owned synchronous waits/reap,
  protected base images, bounded cleanup and a final exact zero-QEMU check.

**Exit criterion:** the applicable collection items above have validated
receipts, and every proposed cause has an observed rejection or loss boundary
plus the correlated state/timing needed to distinguish it from alternatives.
Keep any missing field or unobserved condition open. Collecting evidence does not itself
close DESK-03 or prove a fix. A subsequent behavior change needs a focused
regression, affected-layer build/Sparse where relevant, SDL/GUIHD and at least
two matched media trials under the active plan; Linux/fullscreen/YouTube parity
and host-hang recovery retain their separate gates.
