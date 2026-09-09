# Chromium submission and ACK instrumentation — 2026-09-09 UTC

Status: **stopped by user direction**. Chromium is copied from the host and
must not be rebuilt for this investigation. The full browser build was stopped
and reaped before completion; no diagnostic browser or image was staged or
booted. Cleanup of task-created downloaded inputs and build outputs is
**complete**. The [final receipt](../build-chromium-diagnostics-151.0.7922.34/receipts/user-cleanup-final.json)
reports 38,433,316,864 allocated bytes removed (35.79 GiB), 18,759,680 bytes
retained, zero build/QEMU processes, and preserved host browser, frozen kernel
and rootfs. The current work is a [kernel workqueue fix and regression](chromium-kernel-workqueue-investigation-20260909.md)
using the unchanged host-copied browser; runtime validation remains pending.

The remainder records the abandoned instrumentation approach and its source
checks. Its unexecuted browser-build/runtime gates are historical, not the
active plan and not authorization to resume building Chromium.

The [latest GUI capture](chromium-bottleneck-checkpoint-20260909.md) reproduced
six prepared, selected frames that never reached construction in each traced
trial, with seven browser-reported drops. Existing categories do not expose
the relevant guards or per-sink ACK state. The
[field-to-producer map](chromium-bottleneck-field-map-20260908.md) defines the
missing evidence and its source boundaries.

## Build identity and isolation

The existing runtime is a prebuilt Chrome for Testing `151.0.7922.34`, staged
by the repository's image workflow; it is not a locally incrementally buildable
Chromium checkout. The same official source tag resolves to
`782af9cb30a53f54487e5d2e44738645a8ec457c`.

An isolated source/build workspace was prepared at
`build-chromium-diagnostics-151.0.7922.34/`, preserving the frozen browser,
kernel and rootfs. Checkout/dependency/toolchain operations retained bounded
logs, exact commands, stage exit status and synchronous process cleanup.
The user later explicitly authorized removal of these downloaded inputs and
generated outputs; only compact receipts remain. The historical host preflight
found 16 logical CPUs, 28 GiB available RAM and 665 GiB free disk; the build
lane used bounded concurrency and local execution.

The full source checkout completed at the exact pinned commit. After the
official server stalled on a bulk lazy-blob request, its owned checkout was
terminated and reaped with a retained receipt. Chromium's GitHub mirror
independently resolved the same commit; a non-filtered shallow fetch completed
with 504,243 objects (1.32 GiB), followed by local checkout. Dependency sync
completed successfully in 427.8 seconds. Diagnostic patch applications
passed, with all 15 final file hashes verified. Official hooks and GN
configuration completed. The first real object build stopped after 4,734
steps because Chromium's clang plugin requires `raw_ref<T>` instead of the new
recorder's native reference member. Its failure log and exact command are
retained; the process was reaped. A verified delta replaces that member and
ends the temporary borrow before trace ownership transfer, without disabling
the plugin. All 15 revised file hashes passed verification. The focused retry
compiled all nine requested object outputs successfully in 338.7 seconds,
with 3,632 local steps and no failed steps or remote execution. This covers
the recorder in both toolchains, decoder preparation, surface readiness,
service support/bundling, and Blink submission/bundling/selection. The full
browser build was later stopped incomplete at the user's direction.
The release configuration preserves H.264/Wayland/GL, no ThinLTO/PGO, symbol
level zero, and explicit
`dcheck_always_on=false`. The last setting corrects the pinned nonofficial
release default discovered in the first compile command. The focused build
used eight local compile slots and one link slot. A measured full-build interval
used 7.89 of 16 CPU cores with over 21 GiB available memory; its eight-slot
process was stopped and reaped cleanly after 4,042 successful steps. Incremental
compilation resumed with twelve compile slots and one link slot. Subsequent
large-object snapshots retained at least 20.68 GiB available memory, with the
largest sampled compiler at 0.94 GiB. That stage reaped cleanly after 15,072
successful steps and no failures. A final incremental resume used sixteen
compile slots, one link slot and host nice level 5; source/GN behavior settings
were unchanged. The final stage was stopped and reaped after 1,878 completed
steps with zero compiler failures and 40,789 remaining. Commands,
stage exits and application receipts, including `focused-compile-pass.json`, are retained under
`build-chromium-diagnostics-151.0.7922.34/receipts/`.

The stopped build followed the [official Linux build workflow](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md),
using pinned source/dependencies and the downloaded Chromium toolchain. Build
arguments and their differences from the prebuilt CfT artifact must be retained.
The diagnostic configuration must preserve H.264/FFmpeg support and the existing
Wayland/GL workload. A source-built binary with tracing disabled is a separate
control from the frozen prebuilt binary; compilation alone supplies no runtime
or performance credit.

## Observational event contract

The proposed category is `disabled-by-default-media.bottleneck`, with fixed
instant event name `VideoBottleneck` and a nested `args.bt` record carrying
`schema_version=1`. Event construction and value formatting must be gated before
allocation when the category is disabled.

Records distinguish process-local object/instance identity, per-instance
sequence, sink/bundle identity, binding epoch and counter-reset epoch. Attempt records carry origin,
attempt ID, actual video-frame identity and PTS, evaluated guard state, pending
ACK count, dimensions/transform and explicit outcome. Additional observations
cover ACK production, batch membership/flush, client dispatch, resource reclaim,
counter updates and lifecycle resets.

Instrumentation preserves guard order and short-circuit evaluation, callback
ordering, resource ownership and pending-count updates. Fields not evaluated
at an early return remain explicitly unevaluated. No wire-protocol IDs are
introduced: local batch sequences are observation IDs, not ACK frame IDs or a
cross-process join. Actual transport joins require existing uniquely matched
Mojo flow evidence. Recording uses bounded tracing buffers and post-playback
export, without per-event serial output.

The [producer patch and source manifest](../scripts/gpu/chromium-diagnostics/README.md)
are now available. Patch SHA-256 is
`9439dc724c94d34c18d2836605724b57c63d39bd99cf2ff95475111954db3075`.
The application tool verifies 13 pinned original files and all 15 resulting
files. Six tests using retained real source copies pass, including application
hashes, preserved guard/counter placement and rejection of modified sources,
existing diagnostic files and symlinks. Verified deltas upgrade the already
applied initial patch and reproduces the revised full-patch output hashes.
The revision pairs surface ACK readiness/completion with a local callback ID,
capturing scalar frame/client fields before the callback and avoiding later
object dereferences. Source application and the affected real translation
units passed; the complete browser build and runtime emission were not completed.

The analyzer must retain missing/duplicate/ambiguous attempts, unmatched batch
entries, stale/reset state and counter contradictions as explicit issues.
PID/PTS arithmetic and opaque notification receipt cannot supply missing ACK
membership or exact frame-discard identity.

The maintained [Mojo flow helper](../scripts/gpu/chromium_mojo_flow.py) now
reconstructs unique exported constructor → send → Connector → receive paths.
Diagnostic batch delivery additionally requires strict same-thread enclosing
send/receive boundaries, matching bundle identity, matching ordered membership
and counts, locally complete batches, and unique endpoint ownership. It never
uses equal process-local batch IDs as a cross-process identity. Zero-ACK batches
can establish transport delivery without establishing any ACK application.
The [31 focused synthetic tests](../scripts/gpu/test-chromium-mojo-flow.py) pass,
covering damaged/duplicate flows, replies, unsupported identity formats,
ambiguous timestamps, incomplete batches and mismatched membership. This is
parser validation, not new runtime evidence.

Independent review additionally found and fixed a uniqueness error: a second
proven send inside the same batch envelope must invalidate the first join even
if the second receiver's diagnostic records are missing or inconsistent. All
plausible endpoint claims now count before downstream validation, with both
cases covered by regression tests.
Completed constructor and dispatch bounds are also validated, and an extra
source send remains ambiguous even when its downstream flow records are absent.

The [submission analyzer](../scripts/gpu/chromium-submission-diagnostics.py)
integrates this maintained transport helper directly. Its
[44 synthetic tests](../scripts/gpu/test-chromium-submission-diagnostics.py)
pass, including guard evaluation/order, anchored counters and resets,
malformed identities, batch sequence gaps and inconsistent ACK callbacks.

A bounded regression against the previous `on-1` trace's verified shard
manifest also completed. Across all threads, the helper inspected 67,041 flow
pairs, found no malformed/invalid/unsupported pairs, reconstructed 1,926 target
ACK-related receive chains, and retained four ambiguous-coordinate exclusions.
The latest receipt, including completed-scope validation, is
`build-x86_64/gui-progress-audit/chromium-ack-capture-20260909T000034Z/analysis/on-1-maintained-mojo-flow-scope-review.json`.
These are transport paths from the existing capture, not 1,926 video-frame ACKs;
that capture still lacks diagnostic batch membership and pending-count fields.

The submission analyzer also completed a bounded regression on that same
manifest and its independently validated alignment. It explicitly reported
`missing_diagnostic_records=true`, all seven required producer roles absent,
and no validated diagnostic batch deliveries. Receipt:
`build-x86_64/gui-progress-audit/chromium-ack-capture-20260909T000034Z/analysis/on-1-submission-diagnostics-coverage-regression.json`.

## Unexecuted gates from the retired browser-build plan

- Complete the diagnostic browser after the verified source application and
  successful real object build. Retain its executable/runtime hashes and exact
  build configuration.
- Preserve the passing guard, counter/reset, missing/duplicate and message
  identity regressions; add a regression for any new runtime-discovered parser
  defect before interpreting its corrected output.
- Identify and hash the actual runtime files and complete a diagnostic emission
  smoke before interpreting any rejection. Preserve a separately identified
  baseline and compare recording disabled/enabled on the same diagnostic build.
- Run the unchanged local-video fixture through the owned GUI workflow with
  the required visual gates, matched trials, artifact identities, full trace
  coverage and cleanup. Keep any serial overlap and capture failure explicit.
- Reconcile rejected attempts with observed guard/ACK state. Only then propose
  the smallest behavior change and its targeted regression. Kernel scheduling,
  locking, timers, RCU and console polling remain unproven causes.

### Runtime checkpoint before the full comparison

The current controller and guest owner helper intentionally pin the frozen
browser SHA and its normal launcher path. The new explicit
`--diagnostic-config` accepts a separately pinned image/runtime manifest and
verifies the browser and all declared runtime assets on the host and guest.
It preserves source, patch, build, image and runtime identity in each arm.
The private-image stager verifies an explicit base hash, modifies only a new
copy, and reads back the exact guest file inventory, hashes and modes.
Twenty-four focused capture tests, six cleanup tests and sixteen tiny-image
staging tests pass. Independent review led to checks for same-size/restored-mtime
runtime rewrites, uploads outside the sealed smoke trial, reused receipt
directories, nonzero launcher exits and teardown failures. Shutdown and child
reaping are bounded; failed teardown still reaches final exact inventory and
receipt attempts. Real tiny child/server regressions cover these cleanup paths;
the full browser build was stopped and image staging was never performed.
These retired runtime gates are not active work. A fresh base-image hash,
if needed, is a current pre-staging pin and must not be represented as a
historical hash match.

The [pinned Linux deployment audit](../scripts/gpu/chromium-diagnostics/runtime-deployment-notes.md)
identifies the final resource packs and conditional libraries. In particular,
generated DevTools inputs are packed into `resources.pak`; their presence in
GN's 5,172-entry runtime-dependency plan does not require deploying each loose
input. Actual finished files still need a complete inclusion/exclusion receipt,
ELF dependency accounting and hashes before staging.

The controller's `--diagnostic-smoke` requires that explicit configuration,
selects only traced `on-1`, and seals its emission-only purpose and trial order.
It retains the unchanged 15-second fixture and visual/ownership/export gates,
with no comparison or performance credit. Its first ON arm has no OFF argv
reference; a procfs-capped observation records that limitation explicitly.
Preserve the
normal launcher environment and H.264/Wayland workload. A short emission smoke
must first establish `VideoBottleneck` payload shape, submitter lifecycle
anchors, both bundle producers, actual preparation/selection frame IDs, and
working playback. The existing trace categories plus the new opt-in category
must be checked for trace loss and export size before the 15-second trials;
missing anchors or loss are reasons to adjust the capture, not infer zero ACKs.

The subsequent OFF/ON/ON/OFF comparison uses the same diagnostic binary for all
four arms. Its OFF control measures the effect of enabling recording within
that build; the earlier prebuilt-browser control remains a separately identified
comparison because build/toolchain/packaging differences can affect performance.

### Evidence gate for the first optimization

For each prepared/selected frame that lacks construction, follow its actual
process-local `VideoFrame` ID and explicit preparation input/output edges to
the submission attempt. Keep repeated selections, canceled preparation and
missing records separate. A complete observed return determines the next
boundary to inspect:

| Observed result | Required follow-up before changing behavior |
| --- | --- |
| `pending_ack` | Reconcile the counter from an initial/reset anchor; follow unique surface/service calls, enqueue/batch membership, actual Mojo delivery and client application. Measure which supported interval spans the rejected attempt. |
| `duplicate_frame` | Verify the same frame ID was already submitted and determine whether a distinct prepared frame was selected. Repeated selection alone is not a new dropped frame. |
| Visibility or missing-sink guard | Inspect the captured visibility, rendering, binding and sink state at that attempt, including any surrounding lifecycle transition. |
| `empty_output` | Inspect the evaluated frame dimensions/transform and computed output; this guard supplies a concrete correctness boundary. |
| No complete attempt | Establish selection-to-submit coverage and lifecycle identity first; do not assign a guard or fabricate an ACK association. |

Long observed intervals include scheduling and nested work as well as the
named operation. Only a supported interval overlapping the lost-frame cohort
warrants the next targeted timing checkpoint. Scheduler, lock, softirq or RCU
changes require their own measured wait/service evidence. The first behavior
change must then be tested against the same workload with recording disabled,
with recording enabled used separately to confirm the intended mechanism.

No new diagnostic binary or VM-emitted guard/ACK evidence is claimed by this
historical record. Browser-build work is stopped and cleanup is complete;
continue runtime investigation through the unchanged-browser
[kernel checkpoint](chromium-kernel-workqueue-investigation-20260909.md).
