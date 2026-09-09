# Chromium bottleneck field-to-producer map

Status: **BT-01 source audit; no new runtime proof.** This maps
the [deeper-evidence checkpoint](chromium-ack-boundary-investigation-20260908.md#investigation-checkpoint--deeper-evidence-collection)
to maintained collectors and retained Chromium `151.0.7922.34` source. BT-02
and BT-03 remain open. The kernel table below maps the BT-04/05/06 producers;
their event-level measurements also remain open.

The source tag establishes intended behavior; it does not establish that the
prebuilt executable has no downstream differences. Each collection must retain
the actual browser executable hash, arguments and process/thread start
identities. Any added hooks require a separately identified diagnostic build,
its source diff and build receipt. No runtime or launcher change was made for
this map.

## Source references

Line numbers below refer to the retained copies, not a moving upstream branch.
The [earlier source review](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/ack-review-findings.md),
its [path/hash receipt](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/ack-review-findings.json)
and the [ACK source manifest](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/sources.json)
retain the pinned URLs and SHA-256s. Failed download entries in the manifest
are not source evidence; the corrected `lib/message.cc` entry is retained.

| Reference | Retained source |
| --- | --- |
| S1 | [video_frame_submitter.cc](../build-x86_64/gui-progress-audit/chromium-bottleneck-source-20260908/video_frame_submitter.cc) |
| S2 | [video_frame_compositor.cc](../build-x86_64/gui-progress-audit/chromium-bottleneck-source-20260908/video_frame_compositor.cc), [decoder_stream.cc](../build-x86_64/gui-progress-audit/chromium-bottleneck-source-20260908/decoder_stream.cc) |
| S3 | [client video_frame_sink_bundle.cc](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/third_party__blink__renderer__platform__graphics__video_frame_sink_bundle.cc) |
| S4 | [service frame_sink_bundle_impl.cc](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/components__viz__service__frame_sinks__frame_sink_bundle_impl.cc) |
| S5 | [compositor_frame_sink_support.cc](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/components__viz__service__frame_sinks__compositor_frame_sink_support.cc), [surface.cc](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/components__viz__service__surfaces__surface.cc) |
| S6 | [CompositorFrameSink protocol](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/services__viz__public__mojom__compositing__compositor_frame_sink.mojom), [FrameSinkBundle protocol](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/services__viz__public__mojom__compositing__frame_sink_bundle.mojom) |
| S7 | [message.cc](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/mojo__public__cpp__bindings__lib__message.cc), [send_message_helper.cc](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/ack-review-send_message_helper.cc), [connector.cc](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/mojo__public__cpp__bindings__lib__connector.cc), [interface_endpoint_client.cc](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/mojo__public__cpp__bindings__lib__interface_endpoint_client.cc) |
| S8 | [generated-interface template](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/mojo__public__tools__bindings__generators__cpp_templates__interface_definition.tmpl), [tracing build defaults](../build-x86_64/gui-progress-audit/chromium-ack-boundary-source-20260908/base__trace_event__tracing.gni) |

## BT-02: attempt identity, state and outcome

“Present” below describes a source producer supported by the earlier retained
captures, not guaranteed coverage in a future ring export. New captures still
need the validation gates below.

| Required field | Existing producer and limit | Smallest additional observation |
| --- | --- | --- |
| Monotonic timestamp, PID/TID and task lifetime | Trace timestamps/metadata plus maintained `owner-helper.py` executable, argv and start-tick snapshots. Snapshots occur outside playback; PID/TID alone is not lifetime identity. | Carry a process-local submitter instance ID and epoch; join to same-run task identities and validate clocks/coverage. |
| Preparation, selection, PTS and dimensions | S2 `VideoDecoderStream::PrepareOutput` and `SetCurrentFrame` (`:177–187`); S1 `SubmitFrame` (`:767–774`) exposes a human-readable frame string with PTS/natural/coded dimensions. | Emit the actual `VideoFrame::unique_id()` at the video preparation/output and selection boundaries, carrying an explicit relationship if a wrapper creates a new frame identity. |
| Attempt ID/origin; construction and outcome | S1 attempt scope, an unambiguous enclosing `OnBeginFrame`, and nested `VideoFrameResourceProvider::AppendQuads` establish partial origin and whether construction was entered. Raw event index is only an analyzer coordinate. | Assign a per-instance attempt sequence and explicit origin at both call paths (`OnBeginFrame`, `SubmitSingleFrame`); emit entry, accepted-construction and every return with that ID. |
| Unique and last-submitted frame IDs | Duplicate comparison exists at S1 `:783`; neither value is emitted. The construction async ID is a compositor frame token (`:944–958`), not the video-frame ID or submitter lifetime. | Record current unique ID and optional `last_frame_id_`, including validity, at each attempt and mutation. Preserve PTS as an attribute rather than substituting it for identity. |
| Sink/bundle identity, validity, reset epoch | Provider request scopes can identify `RegisterEmbeddedFrameSinkBundle`, `CreateBundledCompositorFrameSink`/`CreateCompositorFrameSink` and `ConnectToEmbedder`. Their generic payloads do not disclose endpoint/sink IDs or prove successful creation. S1 `StartSubmitting` (`:656–700`) has no dedicated lifecycle event. | Record instance/sink/bundle IDs, bind/disconnect/destruction events and counter-reset epochs at the submitter and bundle endpoints. Distinguish request issuance, local binding and completed initialization. |
| Visibility and force flags | S1 setters (`:293–315`) and `ShouldSubmit` (`:908–912`) expose no dedicated state trace. Other compositor visibility events concern other objects. | Emit old/new surface/page visibility, `force_submit_`, `force_begin_frames_` and rendering state at setters/lifecycle transitions; snapshot their values and the applicable guard result at each attempt. |
| Natural/output dimensions, transform and size-change state | Natural dimensions occur in the attempt string. S1 `:787–814` applies rotation, compares output size, updates it, then uses `frame_size_changed` to bypass the ACK guard. These derived values are absent from the trace. | Record natural size, effective transform, previous/effective output size and size-change result. Mark derived fields as unevaluated at earlier returns rather than inventing values. |
| Pending ACK count and exact return reason | S1 `:775–858` has four rejection branches and increments the counter after accepted submission; no branch reason or count is emitted. | Record counter before/after, branch and predicate state. Reasons must distinguish missing sink, visibility guard, duplicate ID, empty output, pending ACK with unchanged size, and accepted return, while preserving original guard order and short-circuit behavior. |

The combined first branch tests `!compositor_frame_sink_ || !ShouldSubmit()`.
A diagnostic must report which operand was evaluated and caused the return;
it must not reorder the guards. A positive pending count alone does not prove
that the later ACK branch executed. Continued rVFC/`OnBeginFrame` also does not
prove visibility: S2 `StartForceBeginFrames` (`:303–325`) enables begin frames
independently of `ShouldSubmit` through S1 `:229`.

## BT-03: ACK production, transport and application

| Required boundary | Existing producer and limit | Smallest additional observation |
| --- | --- | --- |
| Frame admission and ACK readiness | S5 service `STEP_RECEIVE_COMPOSITOR_FRAME` carries service sink/trace identity (`:721–734`). `Surface::FrameData::SendAckIfNeeded` (`surface.cc:485–489`) and support `DidReceiveCompositorFrameAck` (`:1027–1044`) have no dedicated unconditional ACK record. | Record service ACK-ready/dispatch sequence with sink/epoch and admission relationship where actually known. Distinguish ACK production from reclaim-only behavior. |
| Bundle ACK enqueue, membership and flush | S4 ACK enqueue (`:90–105`) and flush (`:131–174`) maintain separate ACK, begin-frame and reclaimed-resource arrays. Generic receive byte sizes cannot decode these arrays. | Record bundle/sink IDs, ordered entry kinds and enqueue/flush boundaries with a batch observation sequence, including zero-ACK batches. Associate the flush with the actual outgoing message flow. |
| Actual message construction, send and receiver dispatch | S7 `Message::Message` (`:255–259`), `Send mojo message` (`send_message_helper.cc:15–22`), `Connector::DispatchMessage` (`:511–575`) and endpoint request receive (`:942–987`) provide transport flow boundaries. The send event is immediately before `receiver.Accept`, not proof of a kernel transport write completing. | Reuse uniquely matched existing flow edges; join diagnostic batch/endpoint identity to the message. Treat construction-to-send separately from send-to-Connector and Connector-to-method receive. Retain missing/ambiguous edges. |
| Per-sink ACK application and reclaim callbacks | S3 `FlushNotifications` dispatches ACKs, then reclaimed resources, then begin frames (`:199–237`). S1 `DidReceiveCompositorFrameAck` (`:372–384`) reclaims resources before testing/decrementing the counter. Generic bundle receipt gives neither membership nor the per-sink application boundary. | Record per-entry dispatch and callback entry, resource-reclaim completion, decrement/zero-count handling and callback exit, with sink/epoch and count before/after. Bracket separate reclaim callbacks too. |
| Resets and counter reconciliation | S1 `OnContextLost` resets pending count and last frame ID (`:317–336`) without a dedicated trace. `SubmitEmptyFrame` is traced (`:864`) but its reset (`:884`) is not. | Emit lifecycle/reset cause and before/after counter/frame-ID state. Record empty submission as its own operation; increment the reset epoch at every reset and capture an initial state anchor. |

S6 ACKs contain returned resources; the bundled form additionally identifies
the sink. They do **not** carry a compositor frame token. A diagnostic sequence
is an observation ID, not a protocol ACK frame ID. Reconcile admissions and
applications only through observed sink/epoch and validated ordering; retain
resource callbacks, empty submissions, zero-count ACK handling and resets.
Absence of additional create requests or `SubmitEmptyFrame` does not exclude
an untraced reset or transient visibility change.

For generic dispatch identification the current maintained parser uses
interface plus stable method hash and a request receive scope, rejecting reply
events. Relevant hashes are `FlushNotifications=3677904108`, direct
`DidReceiveCompositorFrameAck=2991119680` and direct `OnBeginFrame=2378294256`.
The interface/hash identifies a method, not its endpoint instance. Exported
flow edge IDs are remapped trace coordinates, not the original message nonce.
Per-sink `STEP_SEND_ON_BEGIN_FRAME_MOJO_MESSAGE` can precede bundle assembly;
it cannot replace actual message send. Ordinary compositor ACK and display
swap-ACK events cannot substitute for the video-sink ACK.

## What categories and flags can supply

The [maintained controller](../scripts/gpu/chromium-bottleneck/controller.py)
already enables
`media,cc,viz,benchmark,mojom,mojom.flow,graphics.pipeline,disabled-by-default-mojom`
for ON trials with a 32-MiB ring. Earlier accepted evidence has generic method,
symbol, flow and byte-count fields, without decoded ACK arrays or guard state.
Adding runtime categories cannot restore fields absent from the source or
parameter serialization compiled out behind `MOJO_TRACE_ENABLED` (S8).
Even a build with extended Mojo tracing must verify actual decoded values;
generated parameter tracing is not an application guard/counter/reset hook.

`--disable-features=UseVideoFrameSinkBundle` selects the direct path in S1
`:663–679` without a source change. It exposes direct ACK method dispatch but
changes batching and still omits guard state and reset history. The maintained
controller verifies exact expected browser arguments; such an arm requires an
explicit diagnostic configuration and receipt, plus observed direct-path
events. It cannot complete normal bundled-path BT-02/03 by itself.

The smallest complete browser extension is therefore focused, opt-in records
at the S1 state/attempt/application sites and S3/S4/S5 ACK/batch sites, plus
frame-identity and message-flow joins. Use bounded in-memory recording and
export after playback. Observation IDs need not change the wire protocol;
the ordering/flow join must be validated rather than presumed. Preserve the
frozen baseline and measure instrumentation overhead separately.

## BT-04/05/06: kernel producer availability

This read-only review uses clean kernel commit
`81cbd0c5d408cf21a146bdc2fc09d0333e574ddc`. The
[named-source hash receipt](../build-x86_64/gui-progress-audit/chromium-ack-capture-20260909T000034Z/kernel-field-source-map.json)
binds the files inspected during the next VM capture. The source map does not
prove that a missing timing stage caused a frame drop, and no flags or kernel
code were changed to produce it.

| Required evidence | Available producer and limit | Smallest additional observation |
| --- | --- | --- |
| Worker-to-queue identity | [workqueue.c](../kernel/kernel/proc/workqueue.c) keeps `current->wq` and `wq->name` internally, but creates generically named workers/managers. Current task snapshots do not export that relationship. | Emit queue/worker identity at creation and work assignment, with lifetime or generation identity rather than an unqualified reusable address. |
| Work readiness through callback completion | `queue_work` takes the queue lock, enqueues and wakes the manager; `__worker_routine` invokes `work->func` outside that lock. Existing counters do not join one work item through those stages. | Record readiness, queue-lock wait, enqueue, manager/worker notification, assignment, execution and completion with a work-instance ID. A worker can receive work directly through its wait queue; cover that path as well as dequeue. |
| Timerfd deadline, expiry and epoll notification | [timerfd.c](../kernel/kernel/vfs/timerfd.c) separates expiration from notification by `timerfd_rearm_work`. The existing `webkit_timerfd_trace` explicitly filters `MiniBrowser`, `WebKit` and `webkitgpusmoke`; enabling it alone does not trace Chromium. | Bounded records of owner/file/timer generation, requested and rounded deadlines, expiry, deferred enqueue/execution and `vfs_file_knote_notify`, preserving rearm/cancel/coalescing state. |
| Chromium epoll delivery | [epoll.c](../kernel/kernel/kqueue/epoll.c) has `chrome_epoll_trace` return/event summaries with PID/TGID, epfd, fd and masks. Default logging samples the first 256 calls and then one in 1,024; verbose mode prints more. It does not provide the readiness/enqueue/dispatch chain. | Correlate file/event readiness, notification, waiter wake, syscall entry/return and task dispatch in memory. Existing printf summaries add serial work and cannot serve as complete latency records. |
| Runnable delay and selected execution | [procfs/inode.c](../kernel/kernel/vfs/procfs/inode.c) exposes `CpuTime`, `RunWaitTicks` and `RunSlices`; [rq.c](../kernel/kernel/proc/rq.c) stamps runnable time after enqueue and accumulates elapsed selected time at put/tick. These are aggregate snapshots, without per-event identity or isolated execution cost. | Record notification-to-enqueue and enqueue-to-selection separately, with task lifetime, CPU and clock domain; account for interrupts and host descheduling separately when claiming execution cost. |
| Exclusive GPU posting/lock stages | [virtio_gpu_user.c](../kernel/kernel/virtio_gpu_user.c) wraps the entire post/retry loop in the posting timer, clearing the shared owner before unlocked admission. [virtio_gpu.c](../kernel/kernel/virtio_gpu.c) reads that shared owner when attributing waits. | Carry an operation-local identity and bracket admission, lock reacquisition, reaping, queue locking, publication and device notification. Retain nested intervals instead of subtracting incompatible owner totals. |
| Console idle work and input arrival | [console.c](../kernel/kernel/console.c) stages bytes without notifying the feeder; empty-ring checks request `sleep_ms(1)`. There are no dedicated empty-loop, received-byte, overflow or wake-latency counters on this path. | Record ring-empty iterations, bytes published/consumed/dropped, sleep requests and bounded feeder execution spans, then join input publication to feeder dispatch in a separate input control. |
| Whether a requested console sleep actually slept | [sched_timer.c](../kernel/kernel/timer/sched_timer.c) sets an interruptible state and registers a timer. If registration fails, `sleep_ms` restores running state and yields once. A sleep request alone is not proof of one millisecond asleep. | Record timer registration result, requested deadline, actual expiry/wake cause and dispatch. Measure registration failures before interpreting queued-interval counts as timer wakeups. |

These are missing observations, not new scheduler, timer, workqueue or console
defect verdicts. In particular, no measurement here establishes how often the
timer-registration fallback occurs. The current GUI capture deliberately
retains the preceding diagnostic configuration; new per-event collection
would need separately identified instrumentation and overhead controls.

## Collector dependencies and completion gates

The [controller README](../scripts/gpu/chromium-bottleneck/README.md) documents
the retained framebuffer helper/layout dependencies, frozen kernel/rootfs/clip,
fresh profiles, OFF/ON/ON/OFF order, visual review and silent playback.
[owner-helper.py](../scripts/gpu/chromium-bottleneck/owner-helper.py) records
identity/exclusion failures; inherited zygote argv still needs trace metadata
to establish thread roles. No generic worker-name inference supplies kernel
queue identity.

[chromium-frame-analysis.py](../scripts/gpu/chromium-frame-analysis.py) validates
the current clip's unique PTS and clock alignment; that validation does not
make PTS a general frame ID.
[chromium-mojo-boundary.py](../scripts/gpu/chromium-mojo-boundary.py) deliberately
leaves `pending_ack_count=null` and `bundle_ack_contents_observed=false`.
It can report attempt/construction and generic-receive association, not an
exact rejection verdict. The retained flow/epoch scripts are additional receipt
dependencies, not automatically part of the maintained controller.

Before marking collection complete:

- Bind the producer map to actual diagnostic source/build/executable hashes.
  Verify each required field is emitted, including all returns and resets;
  unavailable fields stay open. Preserve baseline identities and actual flags.
- Require an instance/epoch anchor and explicit attempt outcomes, joining
  preparation, selection and construction by observed identity. Count missing,
  duplicate, boundary and ambiguous records rather than repairing them silently.
- Reconcile every pending-count transition from accepted submissions, ACK
  application and resets. Check zero-ACK bundles, separate reclaim callbacks,
  size-change admissions and reentrant/tied event boundaries. Test joins with
  missing, duplicate and reset cases before interpreting a real rejection.
- Validate clock domains/alignment sensitivity, complete playback coverage and
  loss/overwrite counters. Export success does not prove that the tracing ring
  retained the whole window. Preserve rejected exports; use the existing bounded,
  ordered, hash-verified shard loader for explicitly labeled derivatives.
- Preserve silent playback and serial-byte brackets, compare recording off/on,
  retain failures and trial order, and satisfy the checkpoint's matched-trial
  and fresh-boot requirements. Attribute message delay only to observed stages;
  a long generic bundle-dispatch interval remains an association until ACK
  membership and counter application are known.

This source map supplies the browser and kernel producer inventory for BT-01.
The missing hooks still require implementation and emission validation before
the collection contract is complete. A further categories-only capture does
not complete BT-02/03 or the kernel timing items.
