# Chromium bottleneck checkpoint capture — 2026-09-09 UTC

The requested GUI investigation launched through
`scripts/launch/launch-gui.sh`, using the maintained bottleneck controller and
owned-QEMU wrapper. Four local-video trials complete on the unchanged frozen
kernel and workload, with drop rates between 0.77% and 1.21%. No behavior fix
was applied. This is a useful low-drop comparison with the preceding 7–35%
observations, not evidence that the unresolved rejection/ACK cause is repaired.

Receipt: `build-x86_64/gui-progress-audit/chromium-ack-capture-20260909T000034Z/`.
The [preceding audit and open checkpoint](chromium-ack-boundary-investigation-20260908.md#investigation-checkpoint--deeper-evidence-collection)
remain the starting evidence and collection requirements.

Current follow-up: [kernel workqueue investigation](chromium-kernel-workqueue-investigation-20260909.md).
The minimal source fix and opt-in regression are implemented, with runtime
validation pending. The user requires the unchanged host-copied browser;
the incomplete Chromium source build was stopped and its cleanup completed.
These later source changes add no runtime credit to this capture.

## Runtime contract and completed observations

The controller uses `AUTO_BUILD=0`, the frozen progress-wait kernel with SHA-256
`59253eb271555b6e1e3e035280002d79a7fc82f3e236b288ca981a0e495ed104`, the protected
audio-passcred base image and a private temporary overlay. SDL/virgl, six vCPUs,
8 GiB, the Chrome executable, local clip, fixture and trace categories retain
the preceding configuration. Actual arguments and artifact/helper identities
are recorded in `provenance.json`, `qemu-cmdline.json` and `source-state.txt`.
Kernel source remains clean at `81cbd0c5d408cf21a146bdc2fc09d0333e574ddc`.

| Arm | Browser tracing | Dropped / total | Drop rate | Playback speed | Serial bytes between report receipts |
| --- | --- | ---: | ---: | ---: | ---: |
| OFF1 | Off | 11 / 912 | 1.206% | 0.99944 | 3,516 |
| ON1 | Enhanced | 7 / 909 | 0.770% | 0.99980 | 7,201 |
| ON2 | Enhanced | 7 / 909 | 0.770% | 0.99997 | 6,948 |
| OFF2 | Off | 10 / 911 | 1.098% | 0.99999 | 6,330 |

All four fixture observations are usable, report no HTML video error and have
a direct browser exit status of zero. Each mapped fixture receives actual
visual review before its start gate. Host captures show video/HUD output;
the GL QMP guest screendump limitation remains `no surface`. The host avatar
is outside browser content in the reviewed first-arm capture. No input,
screenshots or collection commands are issued during playback. Asynchronous
serial output overlaps every arm: **none is a silent control**. These byte
counts use HTTP report-receipt brackets, not exact guest emission/playback
timestamps. The lower rates do not establish a tracing-overhead measurement.

The first completed image is
[`t1-off-1-complete-host.png`](../build-x86_64/gui-progress-audit/chromium-ack-capture-20260909T000034Z/t1-off-1-complete-host.png),
also inspected by the conductor. Its HUD displays 912 total frames, 11 drops,
15.1 seconds of playback and no video error. Callback count/rate is not
physical scanout evidence.

Both traced raw JSON exports exceed the existing 64-MiB admission gate:
ON1 is 69,761,501 bytes and ON2 is 69,634,209 bytes. Both are rejected and
preserved as bounded gzip archives with original/archive hashes. Ordered
derivatives retain all 366,526 / 364,869 events and pass original-hash and full
structural-reconstruction checks. Manifests are
`analysis/on-{1,2}-trace-shards/manifest.json`; the original export rejection
remains in the receipt.

## Repeated frame boundary

| Observation | ON1 | ON2 |
| --- | ---: | ---: |
| Selected frames | 902 | 902 |
| Selected-cohort frames constructed | 896 | 896 |
| Prepared, selected frames never constructed | 6 | 6 |
| Completed attempts for those six frames | 11 | 9 |
| Failed-attempt median duration | 6 µs | 6 µs |
| Minimum preparation lead before selection | 0.737 ms | 27.143 ms |
| Browser-reported drops | 7 | 7 |
| Construction marker to reported presentation, p95 | 31.409 ms | 33.222 ms |
| Alignment endpoint uncertainty | ±226 µs | ±235.5 µs |

The same selection-to-construction gap persists in the low-drop boot. The
selected cohort partitions exactly into constructed and never-constructed
records; it does not supply a per-frame identity join to the batched browser
drop counter. In particular, the numerical difference of one is not proof of
one frame lost before selection. Exact rejection state and ACK membership
remain unavailable in these traces.

The six-frame lost cohorts and reported drop counts are unchanged at all
alignment endpoint extremes. ON1's successful selected/constructed cohort
varies by one frame at an endpoint; do not claim every count is invariant.
The counts above use the nominal independently aligned window.

The maintained Mojo analyzer checks receive/attempt nesting. It does not
reproduce the earlier receipt-specific actual-send → Connector → method flow
join, so this run receives no new message-transport latency claim. The
pending-ACK count and bundle contents remain explicitly unobserved.

Viz runnable waits for OFF1/ON1/ON2/OFF2 are 480.465 / 487.933 / 456.978 /
493.339 ms. The six host vCPUs accumulate 12.552 / 15.074 / 13.475 / 12.939 ms
of runnable wait. These remain wider snapshot aggregates, not event-level
latency or isolated CPU cost. All GPU timeout/failure, asynchronous error/
fallback and DRM loss/overflow deltas are zero; final pending work is zero in
all four arms. The bounded consolidated report is
`analysis/vm-checkpoint-summary.json`, with source analyzer receipts retained.

## New collection findings

The [field-to-producer map](chromium-bottleneck-field-map-20260908.md) now names
existing browser/kernel producers, absent fields and the smallest observational
hooks needed for the checkpoint. This is source evidence, separate from the
runtime observations above.

- Exact submission guards, unique/last-submitted frame IDs, per-sink pending
  ACK count, ACK membership/application and reset history remain unavailable
  in the current categories. A generic bundle receive is not an ACK application.
- Workqueue identity exists internally through `current->wq` and its queue name,
  but the task collector only sees generic worker/manager names. Per-work
  readiness, enqueue, assignment and execution remain uncorrelated.
- The existing timerfd trace filters WebKit-family names and does not cover
  Chromium by simply enabling its flag. Chromium epoll logging supplies sampled
  return/event summaries, not the required readiness-to-dispatch timestamps.
- Console `sleep_ms(1)` requests need registration-result and wake-reason
  counters: timer-registration failure falls back to a yield. No measurement
  establishes how often that occurs. Selected task time remains insufficient
  to isolate polling execution cost; no separate idle control was collected.
- The existing frame analyzer partitions selected PTS into constructed and
  never-constructed cohorts. Its preparation/selection timing and batched
  dropped-counter updates do not have identical membership rules. Numerical
  differences between their totals cannot identify residual dropped frames.

## Offline cohort ledger

The new [frame ledger](../scripts/gpu/chromium-frame-ledger.py) enumerates all
observed preparation, selection, attempt, construction and named presentation
records, retaining producer/PTS candidates, repeated scopes, incomplete records
and measurement-boundary qualifications. It uses the bounded shared loader and
a half-open measurement window; it does not change the earlier frame analyzer
or assign a drop-counter residual to a stage.

Both new trace manifests pass ledger analysis. Results are
`analysis/on-{1,2}-frame-ledger.json`, with a bounded comparison in
`analysis/frame-ledger-review.json`:

- Each whole trace contains six prepared-without-selection candidates. One
  candidate, PTS 50,000 µs, is prepared before the playback baseline and lies
  inside the selected PTS extent. The other five have PTS beyond the final
  selected frame. This is not evidence of six additional dropped frames, nor
  a proven identity for the numerical difference between seven reported drops
  and six selected frames missing construction.
- All 899/898 constructions across the whole retained traces have unique,
  known named presentation endpoints. No missing endpoint is observed for
  those constructions. These whole-trace counts include work outside the
  measurement window and differ from the selected-cohort count of 896 above.
  Endpoint presence supplies neither interpreted feedback success nor physical
  scanout proof.
- Each trace has one same-renderer dropped-counter producer totaling seven,
  with no invalid counts. Same-process membership is still not stream/reset
  or frame identity. The six selected-but-unconstructed PTS in each existing
  analysis are preserved, with their attempts retained individually.
- No unclosed, unlinked or invalid records occur among the ledger's target
  stages, and reported loss/failure indicators remain zero. Other trace scopes
  remain subject to their independent coverage qualifications; this does not
  prove that every possible producer or path emitted an event.

All 18 [focused ledger tests](../scripts/gpu/test-chromium-frame-ledger.py)
pass, covering missing/open attempts, missing/unclosed/duplicate feedback,
ambiguous containment, conflicting PTS, distinct producers, exact window edges,
loss markers and invalid alignment/clock evidence. Both bounded full-trace
ledger subprocesses exit zero and are synchronously reaped. BT-07 has a more
complete inventory, while explicit discard reasons and frame/stream/reset
identity remain missing.

## Cleanup and checkpoint status

Controller session 90478 and the launcher both exit zero. The owned overlay
is removed and protected base size/mtime are unchanged. VM worker and conductor
independently run `scripts/launch/qemu-exact-inventory.sh --require-zero` after
reap; both report exact QEMU count zero. No kernel/browser build, production
policy change or media-workload change was made.

The source inventory advances BT-01; missing-hook implementation and emission
validation remain open. The unchanged capture cannot close exact rejection/ACK
state (BT-02/03), kernel event timing (BT-04/05), or isolated console cost
(BT-06). The validated ledger advances BT-07 without closing drop attribution. This
local windowed capture does not refresh Linux/fullscreen/YouTube parity or
host-hang recovery.
