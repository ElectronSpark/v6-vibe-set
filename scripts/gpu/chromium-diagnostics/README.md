# Chromium video submission diagnostics

**Inactive for the current task.** The user requires the host-copied Chromium
runtime and kernel changes. The attempted browser build was stopped and its
task-created build inputs/outputs are being cleaned up. Keep these source notes
as research; do not resume Chromium compilation or deployment for this work.

This opt-in observational patch targets Chromium `151.0.7922.34`. It records
the missing BT-02/03 fields from the
[source map](../../../docs/chromium-bottleneck-field-map-20260908.md).
**Source application and all nine requested real object outputs pass; the
complete browser build and runtime validation were not completed.** No installed browser, frozen runtime image,
ordinary launcher, wire protocol or submission policy is changed by these
repository files.

`upstream-sources.json` pins the 13 touched upstream files, their retained
source locations and URLs, the patch hash and all 15 resulting file hashes.
The two new files implement the shared scalar recorder. The patch touches the
submitter and client-bundle headers/implementations, compositor selection,
decoder preparation, service-bundle and service-sink headers/implementations,
surface ACK readiness, and base's source/category registration. Exact names
are in the manifest. The retained source/API audit and original application
receipts live in
`build-x86_64/gui-progress-audit/chromium-diagnostic-patch-20260909/` and
`chromium-ack-boundary-source-20260908/diagnostic-inputs/`.

## Check and apply

Use a separately identified full checkout at the pinned revision. Follow root
`AGENTS.md`: one heavy/search lane at a time, guarded source searches, exact
process-handle waits, and no VM unless the root has authorized its sole VM
worker. This directory's tools never download dependencies, build or boot.

```sh
python3 scripts/gpu/chromium-diagnostics/validate_patch.py /path/to/chromium/src
python3 scripts/gpu/chromium-diagnostics/validate_patch.py /path/to/chromium/src \
  --apply --receipt /path/to/new-application-receipt.json
```

The tool requires the Git worktree root, verifies every named original source
and the patch hash, refuses source symlinks and existing new diagnostic files,
runs `git apply --check --whitespace=error-all`, and verifies resulting hashes
when applying. It does not assert that unlisted checkout files match the tag;
retain full checkout/DEPS/toolchain/GN provenance, build output and executable
hashes separately. Reapplying over modified sources is refused.

An already instrumented checkout at the previous `bcc2f21b…` patch revision can
use the small `surface-ack-completion.delta.patch` instead. Its manifest pins
all 15 prior output hashes and all revised output hashes; it cannot apply to
an arbitrary modified checkout:

```sh
python3 scripts/gpu/chromium-diagnostics/validate_patch.py /path/to/chromium/src \
  --delta --apply --receipt /path/to/new-surface-completion-receipt.json
```

The real focused build rejected a native reference member under Chromium's
`rawref` plugin. The subsequent `raw-ref-recorder.delta.patch` upgrades exact
`088f1047…` outputs to the current full patch. It uses `raw_ref<TracedValue>`
and ends the temporary field writer's borrow before trace ownership transfer;
no plugin is disabled and emitted fields are unchanged. Its manifest pins all
15 input/output hashes. Source application and the real focused compile pass;
the full browser build remains in progress.

```sh
python3 scripts/gpu/chromium-diagnostics/validate_patch.py /path/to/chromium/src \
  --raw-ref-delta --apply --receipt /path/to/new-raw-ref-receipt.json
```

The application tests use copies of the actual pinned source files, not C++
stubs. They exercise application/output hashes, guard/counter placement, and
refusal of modified sources, existing files and source symlinks:

```sh
python3 scripts/gpu/chromium-diagnostics/test_validate_patch.py \
  --source-tree /path/to/unmodified/chromium/src
```

After a real build, use a separate diagnostic controller configuration that
adds `disabled-by-default-media.bottleneck` to the existing enhanced categories.
The maintained baseline controller verifies exact browser arguments and must
not silently substitute a diagnostic executable or new category. Keep bundling
enabled for the primary experiment. A bundle-disabled arm is a separate path
change, not a substitute for observing normal bundle membership.

## Record semantics

[`schema.json`](schema.json) is the producer/analyzer contract. Each instant
`VideoBottleneck` has an `args.bt` dictionary. Except for numeric schema version
1, integral payload values are decimal strings, preserving full 64-bit IDs in
JSON. Timestamp/PID/TID remain trace envelope fields. The timestamp is sampled
with the pinned override-free `TRACE_TIME_TICKS_NOW()` before allocating or
formatting the payload, using `TraceTimestampTraits<base::TimeTicks>` from the
pinned trace API. Dictionary construction and all formatting are gated on the
disabled category. The diagnostic build still performs small fixed counter and
frame-attribute bookkeeping with recording off; do not claim zero overhead.
Recording off also traverses batch members to advance event sequences. Its
comparison with recording on isolates enabled recording cost, not all costs of
the diagnostic build relative to the frozen binary.

The recorder has constant-size per-instance state and emits only scalar
fields. It does not copy resource arrays or keep unbounded correlation maps.
The externally configured trace ring bounds storage; per-array membership
uses one scalar record per entry. There is no silent extra event cap. Complete
playback coverage and trace loss/overwrite checks are mandatory before any
absence, ordering or count inference. Compare instrumentation off/on and retain
serial-byte brackets and silent playback. Export after playback, preserving
rejected originals and using verified lossless shards where required.

Object identities come from one exported process-local construction counter;
event sequences advance even when tracing is disabled. Include PID and same-run
task lifetime metadata in joins. Preparation, selection and surface-readiness
records use thread-local observation streams; their address fields are
attributes, not source-object lifetime IDs. Actual `VideoFrame::unique_id()`
and the explicit preparation input/output relationship provide frame identity.
The normal completion calls `CompletePrepare` before popping its input;
`ClearOutputs` and the decoder destructor call cancellation completion before
clearing/destroying the queue. Cancellation emits `output_valid=false`.

`reset_epoch` advances only at actual pending-counter resets. A surface-ID
change emits the separate last-frame-ID reset without inventing a pending
reset. `binding_epoch` advances at `StartSubmitting`, identifying a local
binding attempt rather than successful remote initialization. Attempt IDs
continue across epochs, with their original entry epoch retained.

The original submission guard expressions and evaluation order are preserved.
Diagnostics do not call `ShouldSubmit`. Derived output/size fields remain
unevaluated until the original code reaches those computations. Construct and
application-send enter/exit records bracket the actual calls. The application
send can enqueue a bundle; it is not a transport-write boundary. The pending
increment remains after send, resource cleanup and opacity notification.
ACK callbacks record reclaim completion before zero-count handling/decrement.

Service notification recording covers both SinkGroups and all three immediate
paths for sinks without a BeginFrameSource. Each protocol array has its own
zero-based index; zero-ACK batches and missing client entries are retained.
The `deferred` enqueue flag distinguishes a group queue from immediate forwarding;
it does not assert that a measurable delay occurred. Local batch/callback IDs
are not wire IDs. Match existing actual Mojo flows and uniquely enclosing
send/receive records before claiming cross-process ACK membership or timing.
Service pending admission is recorded where that counter increments, which
does not prove final `SubmitResult::ACCEPTED`.

`surface_frame_trace_id` intentionally emits
`frame.metadata.begin_frame_ack.trace_id`: pinned
`CompositorFrameSinkSupport::MaybeSubmitCompositorFrame` at lines 721–734 uses
that exact field for `Graphics.Pipeline.surface_frame_trace_id`. The field is a
signed 64-bit integer; its default `-1` is unset and cannot establish trace or
flow identity. It remains
source-side frame evidence, not an ACK protocol frame token. Surface-ready,
service-ready and service-emission observations must be joined conservatively;
neither direct nor bundled ACK payloads acquire a new frame ID. Surface readiness
and completion now bracket the exact `SendCompositorFrameAck()` call with a
thread-local callback observation ID. Both records use frame/client values
captured before the callback; completion never dereferences `FrameData` or its
client, which the callback may have destroyed. Null-client pairs have
`called=false`. Strict same-thread containment and the service's observed
`surface_client_address` support a local join; missing or ambiguous pairs
remain unresolved.

Client entry-completion observations share the original method's lifetime
assumption: it already continues through subsequent loops, defer-state writes
and `FlushMessages()` after those callbacks. The final observer callback has
no such original follow-up access and may destroy the bundle, so the added exit
record is guarded by a preexisting weak pointer. A missing exit stays explicit;
it is not synthesized by the analyzer.

## Remaining build and collection gates

The focused real compile passed all nine requested objects with their actual
generated Mojo and Perfetto dependencies; its receipt is
`build-chromium-diagnostics-151.0.7922.34/receipts/focused-compile-pass.json`.
Complete the full browser build next. A dry application, parser test, or
standalone stub is not compilation credit. Exercise category-off/on startup, rejected/accepted
submissions, duplicate/empty/size-change cases, resource callbacks, missing
clients, both reset paths and binding replacement. Validate emitted field types
and full coverage against the analyzer's missing/duplicate/reset/ambiguity
tests, then collect matched silent-playback trials with the frozen baseline
kept separate. An observed counter condition does not replace the exact return
reason, and message receipt does not establish ACK application or scanout.

## Analyze finalized records

The standalone analyzer reads an explicitly named trace or verified shard
manifest through the shared bounded loader. All inputs and the fresh output
must be inside this repository. It never boots a VM or changes existing
frame-analyzer results.

```sh
python3 scripts/gpu/chromium-submission-diagnostics.py \
  RUN/analysis/on-1-trace-shards/manifest.json --trace-manifest \
  --alignment RUN/analysis/on-1-trace-alignment.json \
  --output RUN/analysis/on-1-submission-diagnostics.json
python3 scripts/gpu/test-chromium-submission-diagnostics.py
python3 scripts/gpu/test-chromium-mojo-flow.py
```

Replace `RUN` with a finalized receipt. For a bounded ordinary JSON trace,
pass its filename without `--trace-manifest`. Alignment is optional; without
it, results describe the retained whole trace. With it, the analyzer requires
the existing validated clock evidence and marks the half-open playback window
and endpoint uncertainty. Existing outputs are refused. Memory/time limits and
input/output size gates remain active.

Output retains exact diagnostic record coordinates, producer instances,
sequence gaps, required-field failures, pending-count histories, explicit
attempt outcomes and construction/send timing. A missing initial state or
sequence gap prevents counter-history reconstruction until an explicit valid
reset anchors the new state. Attempts keep their original entry epoch through
reentrant resets. Missing, duplicate, reordered and contradictory records remain
issues; absent diagnostic events or required producer roles are explicit.

Local ACK callbacks retain reclaim completion and count application. Source
surface/service brackets require consistent IDs, state, same-thread ordering
and address relationships during the identified lifetimes. Service callbacks
join queue entries only by strict containment plus sink, route and resource
count. Incomplete and overlapping alternatives prevent unique joins. Validated
local FIFO membership supplies each batch member's enqueue coordinate.

The maintained `chromium_mojo_flow.py` reconstructs actual exported
constructor→send→Connector→receive edges before linking diagnostic batches.
It receives the original events so malformed competing coordinates cannot
silently disappear. Batch contents, identities, boundaries and unique ownership
must agree. Equal local batch numbers or caller-provided validation assertions
are never used as transport proof. Surface frame tokens remain source context,
not invented ACK wire IDs; PTS is never an identity fallback. The signed
`surface_frame_trace_id` value `-1` is explicitly unset and cannot establish
frame or flow identity, even when both local callback snapshots preserve it.

The report compares observed rejected and constructed attempts by exact return
reason while preserving unresolved state and coverage. Source/transport joins
do not prove physical presentation or isolate CPU service, and synthetic tests
do not provide Chromium compilation or runtime emission credit.

## Stage a completed diagnostic runtime

After the real build, prepare a separate host runtime directory with an
explicit JSON inventory. `host_browser_path` must name its executable `chrome`;
each guest suffix maps under that same host directory. The input requires
`schema_version: 1`, the pinned `source_commit` and `patch_sha256`,
`host_browser_path`, `browser_sha256`, `browser_bytes`, and `runtime_files`.
Each runtime record has exactly `guest_path`, `sha256`, and `bytes`, with paths
under `/opt/host-gui/wayland-chromium/chrome-linux64/`. The browser must appear
in that list. Optional `build_provenance` preserves GN arguments, toolchain,
build receipts and other source provenance.

```sh
python3 scripts/gpu/chromium-diagnostics/stage_runtime.py \
  --base-image /absolute/frozen/fs.img \
  --base-sha256 "$FROZEN_IMAGE_SHA256" \
  --runtime-manifest /absolute/completed-runtime-input.json \
  --output-image /absolute/new-private-diagnostic.img \
  --output-manifest /absolute/new-staged-runtime.json
python3 scripts/gpu/chromium-diagnostics/test_stage_runtime.py
```

The tool refuses existing outputs, host symlink parents, hardlinked regular
inputs, unsafe guest paths and changed sources. Supply an explicit expected
base digest. If a retained receipt has no full image SHA-256, first match its
baseline path, size and modification-time evidence, then compute and record a
fresh digest as a **current pre-staging pin**. This does not establish a
historical SHA match. Record that distinction and the metadata comparison in
the staging work receipt. The tool checks the supplied pin before cloning. It creates an exclusive private
copy/reflink, checks that copy against the frozen image, then removes and
recreates only the fixed guest runtime subtree. It preserves inspected source
file/directory modes and verifies the exact resulting inventory and every
guest file's bytes and SHA-256 through `debugfs` readback. Guest symlinks,
special files and hardlinked regular files in that subtree are refused.
The base image, normal launcher and kernel are untouched; filesystem state is
recorded without mounting or running fsck.

The staged manifest matches the diagnostic capture contract and includes the
private image SHA-256, exact runtime file records, build provenance and staging
identities, within the capture controller's 1 MiB manifest cap. The final CLI
summary includes both image and manifest SHA-256 values. Incomplete owned
outputs are removed on failure. Operations have
file/count, command-output and synchronous process deadlines; the default
overall deadline is 1200 seconds (`--timeout-seconds`, maximum 3600). Tests use
only temporary 8 MiB ext2 filesystems. Staging does not establish browser
startup, diagnostic emission or workload behavior.
