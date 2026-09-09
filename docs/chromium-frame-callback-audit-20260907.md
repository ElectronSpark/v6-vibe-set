# Chromium frame-callback audit — 2026-09-07

Two bounded observations show animation pauses followed by spontaneous
resumption. The repeat's retained trace matches all four JavaScript pauses to
delayed client-observed Wayland frame callbacks and previous-buffer releases.
Two first-gap debugger captures narrow the wait: Chromium's GPU process 252
holds the GPU operation mutex in a `RESOURCE` operation and sleeps on async
progress while KWin 62 waits for that mutex inside
`DRM_IOCTL_VIRTGPU_EXECBUFFER`. At the expanded snapshot, all 60 async slots are
free and both browser Wayland connections are empty. Source review and a
deterministic host test support a lost-progress race between the caller's
predicate and wait registration. The separate
[fix report](virgl-progress-wait-fix-20260907.md) records two passing fresh-boot
candidate observations and their remaining media/recovery limits.
Both natural animation observations remain FAIL, and both debugger trials are
separately classified as interrupted. All three completed baseline trace captures confirm
direct Chromium exit 0.

The first bounded observation measured a **7.286-second gap between animation
callback entries, followed by spontaneous resumption**. JavaScript timers and
HTTP reports continued, and the document remained visible and focused throughout
the soak. The 60-second result remains **FAIL** because the progress bound was
violated. No retained protocol trace identifies the cause, and browser/supervisor
exit was not confirmed before successful owned QEMU cleanup.

This extends the [earlier GL recovery audit](chromium-gl-recovery-audit-20260907.md),
whose fixture canceled animation at the first five-second threshold crossing.
It establishes natural callback resumption in this observation; it does not
establish recovery from a kernel or host GPU hang.

## First observation: run and measurement contract

- Receipt: `chromium-wayland-frame-20260907T214639Z`, ending 21:52:39 UTC.
- Kernel SHA-256: `df79132d675029ff03386989574e8b1dbb9697adcd3236dc455b404ca147487b`.
  The deterministic fence-order probe was disabled with
  `virtio_gpu_fence_order_probe=0`.
- Existing SDL/virgl setup: `virtio-vga-gl-primary`, corrected APT modules,
  six CPUs, 8 GiB, virtio input, and a disposable owned overlay; `AUTO_BUILD=0`.
- Chrome for Testing 151.0.7922.34 used the existing multiprocess launcher opt-in.
  Actual browser PID 218's environment contained `WAYLAND_DEBUG=client` and
  `WAYLAND_CHROMIUM_MULTIPROCESS=1`; guest rendering used `GALLIUM_DRIVER=virgl`.
- Fixture: `chromium-webgl-lifecycle-v2`,
  `http://10.0.2.2:45831/?run=trace-first&observe=1`, SHA-256
  `28bed448e72426df7740735f7b96e5bf022ecee5948a8912122e7f672116b49e`.

Evidence: [provenance](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/provenance.json),
[source state](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/source-state.txt),
[actual browser environment](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/09-actual-trace-env.txt),
[saved fixture](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/webgl-fixture.html).

Observation mode retains the pending `requestAnimationFrame` chain after a gap
exceeds five seconds. Its deadline remains **60 seconds total from start**.
A 250 ms timer detects gaps; callback entry and exit use `performance.now()`
independently of the supplied rAF timestamp. The next callback records the full
entry-to-entry gap and a resumption event. Any observed gap keeps the terminal
result FAIL, even after drawing resumes. The deadline cancels further callbacks;
this run makes no claim about behavior beyond it.

Both 320×192 canvases draw one triangle per callback, with periodic center/corner
pixel checks. These checks cover the expected framebuffer colors and sampled GL
errors, not compositor presentation. This observation requested no context loss,
restoration, resize or fullscreen transition. The
[action timeline](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/actions.jsonl)
contains no further input, serial command or host capture between the start click
and terminal receipt.

## First observation: measured progress

The [36 saved JSON reports](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/webgl-results.jsonl)
include initialization, progress, the gap, completion and subsequent close events.
Report 34 is the terminal soak result.

| Measurement | Result |
| --- | ---: |
| Soak duration, monotonic and wall | 60,023 ms |
| Gap detection | 10,762 ms elapsed; callback-entry age 5,076 ms; 270 callbacks |
| Spontaneous resumption | 12,971 ms elapsed; entry-to-entry gap 7,286 ms; callback 271 |
| Detected gaps / resumptions / active at completion | 1 / 1 / false |
| Animation callbacks | 2,334 |
| Draws / pixel checks, per API within soak | 2,334 / 26 |
| GL errors / global errors / unexpected context losses | 0 / 0 / 0 |
| Timer ticks / maximum timer gap | 240 / 405 ms |
| Maximum callback execution duration | 162 ms |
| Maximum rAF timestamp offset from actual entry | 69 ms |
| Terminal result | FAIL: observation completed with animation gaps |

The detection age is a threshold crossing; 7,286 ms is the completed callback gap
measured when execution resumed. The maximum callback duration is much shorter
than that gap. Timer-driven progress reports reached the host during the pause.
Every soak report was visible and focused. Hidden/blur events first appear after
the post-soak close click, about 37 seconds after completion. Context totals of
2,335 draws and 27 checks include initialization; the table uses soak deltas.

## First observation: GPU and DRM samples

| Cumulative counter | Before browser | After observation |
| --- | ---: | ---: |
| GPU async posted = retired | 265 | 14,871 |
| GPU async pending | 0 | 0 |
| GPU failures / timeouts / context failures | 0 / 0 / 0 | 0 / 0 / 0 |
| DRM events queued = read | 51 | 2,402 |
| DRM event depth / drops / overflows | 0 / 0 / 0 | 0 / 0 / 0 |
| Display presents = completions | 52 | 2,403 |

The [before](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/06-before-extended-counters.txt)
and [after](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/13-after-extended-counters.txt)
samples completed with serial markers and fresh prompts. They show completed GPU
work and drained DRM events at those boundaries. They are not continuous samples
of the pause and cannot identify the layer that delayed animation scheduling.

## First trace boundary and pinned source context

The canonical `/host-gui-wayland-chromium.log` cursor increased from
**31,306 to 874,203 bytes**. Both cursor receipts identify browser PID 218 with
start ticks `113079`: [before](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/10-pre-observation-cursor.txt),
[after](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/12-post-observation-cursor.txt).
The guest log was lost before upload. File growth proves neither which protocol
messages occurred nor callback/buffer progress during the gap.

Matching Chromium 151.0.7922.34 source identifies useful distinctions for a
retained trace, without attributing this observation:

- The optional external Wayland BeginFrame source is
  [disabled by default](https://github.com/chromium/chromium/blob/151.0.7922.34/ui/base/ui_base_features.cc#L127)
  and [constructed conditionally](https://github.com/chromium/chromium/blob/151.0.7922.34/ui/ozone/platform/wayland/host/wayland_window.cc#L98).
  Its [fallback timer](https://github.com/chromium/chromium/blob/151.0.7922.34/ui/ozone/platform/wayland/host/begin_frame_source_wayland.cc#L176)
  uses two refresh intervals, about 33 ms at the default 60 Hz. Runtime feature
  state was not captured here.
- Ordinary frame processing can wait for the previous surface's
  [frame callback](https://github.com/chromium/chromium/blob/151.0.7922.34/ui/ozone/platform/wayland/host/wayland_frame_manager.cc#L148).
  Submission acknowledgment also depends on
  [previous-frame buffer release](https://github.com/chromium/chromium/blob/151.0.7922.34/ui/ozone/platform/wayland/host/wayland_frame_manager.cc#L792).
  Retention of the newest displayed buffer alone is expected.
- The frame-callback timer is
  [50 ms](https://github.com/chromium/chromium/blob/151.0.7922.34/ui/ozone/platform/wayland/host/wayland_frame_manager.cc#L42).
  Its [timeout handler](https://github.com/chromium/chromium/blob/151.0.7922.34/ui/ozone/platform/wayland/host/wayland_frame_manager.cc#L922)
  marks a freeze; callback skipping requires
  [active video capture](https://github.com/chromium/chromium/blob/151.0.7922.34/ui/ozone/platform/wayland/host/wayland_frame_manager.cc#L1060).
  The private `FreezeTimeout` helper has no caller or scheduled timer in this
  implementation. Presentation-feedback flushing forwards ready feedback; it
  does not manufacture a frame callback or buffer release. These paths provide
  no established seven-second recovery timer for this run.

A retained trace must match each `wl_surface.frame` callback object to its `done`
event, separately from `wl_display.sync`, and track replaced buffers through
release. Missing frame callbacks, delayed previous-buffer releases, or both
continuing during the JS pause would direct investigation to different stages.
This receipt cannot make that distinction.

## First observation: incomplete browser exit, completed VM cleanup

The close click produced hidden/blur reports and a
[host capture](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/14-browser-close-host.png).
It did **not** prove process exit. The exact serial command beginning
`wait $spid; echo supervisor_exit=$?` did not complete within its 45-second
controller bound. No completion marker, exit result or fresh prompt arrived;
`actions.jsonl` records `serial command completion marker missing`. Supervisor
PID 217 and browser PID 218 therefore have no confirmed guest exit receipt.

The owned QEMU controller subsequently cleaned up and was synchronously reaped.
[Cleanup](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/cleanup.json)
records launcher exit 0, no QEMU remaining, overlay removal and unchanged base
size/mtime. The [run log](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T214639Z/run.log)
records owned-process cleanup and final exact QEMU counts of zero; the conductor
also confirmed exact zero independently. Launcher exit 0 is not a browser exit
status. Protocol attribution and browser/supervisor exit remain unresolved in
this receipt.

## Repeat with retained protocol trace

Receipt `chromium-wayland-frame-20260907T215721Z` used the same kernel, fixture
hash and rendering setup. Chromium PID 219 was a direct Bash child, using fresh
profile `/tmp/wl-frame-trace` and `run=trace-repeat&observe=1`. The helper recorded
ownership and copied/uploaded a frozen log prefix; it did not launch, poll, wait
for or signal Chromium. The
[trace upload](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/14-frozen-trace-upload.txt)
completed before close. Its
[receipt](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/trace-upload-summary.json)
confirms all 543,338 bytes from offset zero, untruncated, SHA-256
`b8b70c6213d3786098200bf34b75765ba5fc087e8d1a8594341ec027dbd1c546`.
This is the complete prefix through the post-soak snapshot, not a shutdown trace.

The [repeat results](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/webgl-results.jsonl)
contain four gaps and four resumptions. Terminal elapsed time was 60,014 ms
monotonic / 60,015 ms wall, with 1,401 callbacks and, per API, 1,401 draws,
18 pixel checks, zero GL errors and zero unexpected losses. Maximum timer
spacing was 399 ms; maximum callback execution was 142 ms. The page remained
visible and focused throughout the soak. The
[action timeline](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/actions.jsonl)
contains no input or capture between the start click and terminal result.
Resumption did not clear the terminal progress failure.

The [reproducible parser](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/analyze-frame-callbacks.py)
matches outgoing `wl_surface.frame` requests to incoming `wl_callback.done`
using each active callback ID. It excludes nine `wl_display.sync` callbacks,
allows sequential ID reuse, and retains pending matches across `delete_id`
until `done`. All 1,409 surface-frame requests belong to surface 28 and complete;
there are no active-ID collisions, unmatched completions or remaining requests.
Median latency is 12.860 ms, p95 25.045 ms and p99 38.457 ms. Exactly four exceed
five seconds. Protocol lines lack connection/PID identifiers; no ambiguous
concurrent callback-ID reuse occurred in this trace.

Clock alignment uses the
[local Wayland formatter](../ports/wayland-src/src/src/connection.c):
`CLOCK_REALTIME` microseconds truncated to 32 bits, printed as milliseconds.
The period is 4,294,967.296 ms, about 71.58 minutes. The parser selects the modulo
epoch nearest the fixture's guest wall time, without fitting an offset. All 17
interleaved Chromium UTC log timestamps fall between their adjacent Wayland log
timestamps. Fixture event wall times agree with soak-start wall plus monotonic
elapsed values within 2 ms. Host HTTP arrival times are not the alignment clock.

| Gap | JS entry gap | Frame request → done | Replaced-buffer release wait | Done → JS resume | Trace request/done lines |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 7,301 ms | 7,277.385 ms | 7,276.709 ms | 43.391 ms | 2094 → 2100 |
| 2 | 7,456 ms | 7,397.582 ms | 7,397.074 ms | 60.046 ms | 3291 → 3297 |
| 3 | 7,127 ms | 7,087.215 ms | 7,086.329 ms | 43.371 ms | 7506 → 7512 |
| 4 | 6,900 ms | 6,868.281 ms | 6,867.273 ms | 46.480 ms | 10271 → 10277 |

All four use callback ID 53 in separate lifetimes. At each pause's end,
`delete_id`, the previous buffer's release and callback completion are logged
together; the release immediately precedes `done`. JavaScript timing is rounded
to milliseconds, and event logging follows entry, so the final timing column
has approximate millisecond accuracy despite the protocol's finer precision.
The callback payload advances only 16–17 ms from the preceding callback, but its
epoch and relationship to actual server send time are not established here.
It is not evidence that the server sent the event before the pause.

Evidence: [complete trace](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/wayland-client-trace.log),
[callback analysis and exact excerpts](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/frame-callback-analysis.json),
[short callback summary](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/frame-callback-analysis.txt),
[buffer parser](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/analyze-buffer-releases.py),
[independent buffer analysis](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/buffer-release-summary.json).
The buffer analysis found no reuse before release or unreleased replaced
buffers. Retaining a currently displayed buffer is not itself an error.

The [post-soak counters](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/15-after-extended-counters.txt)
show GPU posted = retired = 9,032, pending/failures/timeouts zero, DRM queued =
read = 1,463, and depth/drops/overflows zero. As before, these are boundary
samples. The trace demonstrates delayed client-observed Wayland progress
associated with all four rAF pauses; server-side timestamps or focused
transport/read-dispatch evidence would be needed to assign the delay further.

After the close click, the exact direct-child
[`wait $gpid`](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/19-direct-browser-wait.txt)
returned `chromium_exit=0`, a completion marker and a fresh prompt.
[Owned QEMU cleanup](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/cleanup.json)
completed with launcher exit 0, overlay removal and unchanged base size/mtime.
The [run log](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T215721Z/run.log)
and the conductor's independent exact check confirmed zero QEMU processes.
This completes repeat evidence collection and process cleanup, while the
animation-pause cause remains open.

## First-gap debugger capture: compositor blocked in EXECBUFFER

Receipt `chromium-wayland-gap-gdb-20260907T221505Z` retained the same kernel,
fixture and rendering settings. Actual role discovery identified browser 217,
KWin 62 and GPU process 253 before the mouse start. A host receipt for the first
five-second animation gap triggered one memory-only GDB capture with the matching
kernel ELF. No target functions or register writes were used.

The [capture receipt](../build-x86_64/gui-progress-audit/chromium-wayland-gap-gdb-20260907T221505Z/gdb-capture.json)
records a confirmed pause at 22:19:44.620 UTC and confirmed resume at
22:19:46.470 UTC: **1.850 host seconds**. GDB completed and was reaped with exit 0.
This trial is **diagnostically interrupted**; its measured resumption times do
not receive natural-recovery credit. The gap existed before the diagnostic pause.

The [memory snapshot](../build-x86_64/gui-progress-audit/chromium-wayland-gap-gdb-20260907T221505Z/gdb-readonly.log)
completed all three 2,048-entry fd scans and 51 relevant thread records without
helper errors:

- Both browser Wayland connections had **zero pending bytes in both directions**.
  KWin's matching endpoints agreed; neither endpoint had socket read/write
  waiters. There was no captured callback data waiting unread in those rings.
- Browser leader 217 was interruptible in saved Linux syscall 271 (`ppoll`),
  with four descriptors. Its Wayland read knotes were active, unqueued, and
  attached to an empty private poll queue with one waiter.
- KWin leader 62 was **uninterruptible, off CPU and off the run queue**, in
  saved syscall 16 (`ioctl`). Its fd 19 was DRM cdev 226:0 and request
  **`0xc0406442` was `DRM_IOCTL_VIRTGPU_EXECBUFFER`**. KWin's Wayland epoll queue
  was empty and had no waiter. The
  [ioctl definition](../kernel/kernel/inc/uabi/drm.h) and
  [handler](../kernel/kernel/dev/fb/fb_drm_dispatch.c) identify the submission path.

This sample directs the next capture to the waits inside KWin's EXECBUFFER:
input fences, resource attachment, operation-lock ownership, async admission and
completion. A saved ioctl and thread state do not distinguish those waits.
The snapshot does not establish when KWin created or flushed the delayed events.

The [trial summary](../build-x86_64/gui-progress-audit/chromium-wayland-gap-gdb-20260907T221505Z/observation-summary.json)
records terminal FAIL at 60,061 ms, two gaps/resumptions, 2,285 draws and 22 pixel
checks per API, with no GL errors or unexpected losses. The first callback gap
was 8,714 ms and included the debugger pause; the second was 7,293 ms after
resumption. Post-trial GPU posted = retired = 14,586 with pending/timeouts/failures
zero; DRM queued = read = 2,358 with zero depth/drops/overflows. These remain
boundary samples, not GPU queue state during the captured wait.

The frozen [client trace](../build-x86_64/gui-progress-audit/chromium-wayland-gap-gdb-20260907T221505Z/wayland-client-trace.log)
was uploaded **before browser close**, complete from offset zero at 857,823 bytes,
SHA-256 `8be6a9cfb71639d39e24665bab15fffc2c668f5ee97b2cddd647a1f46a7ed9c7`.
Guest and host hashes matched. The direct browser wait returned exit 0 with a
fresh prompt. [Owned cleanup](../build-x86_64/gui-progress-audit/chromium-wayland-gap-gdb-20260907T221505Z/cleanup.json)
reaped controller session 40869 at 22:21:56 UTC, removed the overlay and preserved
the base. Worker and conductor exact process checks both found zero QEMU.

The [retained-trace analysis](../build-x86_64/gui-progress-audit/chromium-wayland-gap-gdb-20260907T221505Z/frame-callback-analysis.json)
matches all 2,294 surface-frame callbacks on this run's surface 39, separately
from eight display-sync callbacks. The two gaps correspond to callback receipt
delays of 8,689.685 and 7,216.010 ms; JavaScript resumption follows by about
40 and 64 ms. The first includes the debugger pause. All 17 Chromium UTC entries
with both adjacent Wayland timestamps validate the modulo-clock alignment; one
trailing entry has no later Wayland timestamp and is excluded from that check.
This run crosses the formatter's 32-bit microsecond wrap, so the
[parser](../build-x86_64/gui-progress-audit/chromium-wayland-gap-gdb-20260907T221505Z/analyze-frame-callbacks.py)
accepts padded timestamp fields and resolves each timestamp to the nearest
fixture-wall epoch. Callback payload advances are 17 and 33 ms from their
predecessors, then 8,666 and 7,250 ms to their successors. Payload timing still
does not establish server-send timing.

## Expanded capture attempt: desktop startup failed

Receipt `chromium-wayland-kernel-gdb-20260907T223139Z` attempted the same workload
with additional memory-only GPU queue/lock and saved-stack helpers. It did not
reach Chromium: the fresh boot serial records GPU/card0 registration followed
by three failed KWin startup attempts, and fresh process checks found no KWin or
Plasma. The host capture showed the background gradient. No fixture, browser or
debugger capture ran, so this receipt provides no animation-gap result.

The retained KWin log tail reports failed DRM-device opens, but it lacked a
prelaunch cursor and is not wholly attributable to this boot. Fresh counters
showed zero async posts and display presents, with zero GPU timeouts/failures.
The unchanged-image startup failure remains separate from the captured
EXECBUFFER wait. [Owned cleanup](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223139Z/cleanup.json)
reaped controller 10392 at 22:33:57 UTC, removed the overlay and preserved the
base; worker and conductor exact inventories were zero before an identical retry.

## Expanded retry: GPU progress waiter blocks the compositor

Receipt `chromium-wayland-kernel-gdb-20260907T223500Z` reached visible KDE with
KWin 62 and Plasma 88. The same kernel/ELF, SDL/virgl configuration, six CPUs,
8 GiB, disposable overlay, five-second async watchdog and disabled fence probe
were retained. Actual argv and environment identified direct-child browser 217
and GPU process 252, using the existing multiprocess opt-in, a fresh profile and
`WAYLAND_DEBUG=client`. Both initial WebGL pixel checks passed before the mouse
start. Evidence: [role argv](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/09-role-argv.txt),
[browser environment](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/11-actual-trace-env.txt),
[capture provenance](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/gdb-provenance.json).

The first gap triggered one memory-only capture. The
[capture receipt](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/gdb-capture.json)
records QMP pause confirmation at 22:41:21.140 UTC and resume at
22:41:23.246 UTC, **2.106 host monotonic seconds** apart. GDB was reaped with
exit 0. No input or intermediate host capture was sent during the observation;
the debugger interruption is the explicit exception to natural execution.

The [saved memory snapshot](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/gdb-readonly.log)
records the following wait chain and state:

- GPU process **252** owns `op_lock` and `async_wait_serialize`. The operation
  label is `RESOURCE`; the separate `op_lock_holder` value 6 is an operation
  category, not a PID. GPU 252 is uninterruptible in `async_wait`, whose
  completion has `done=0` and one waiter.
- KWin **62** is the one waiter on `op_lock`, uninterruptible inside its saved
  `DRM_IOCTL_VIRTGPU_EXECBUFFER` call. `async_reap_serialize` is unowned.
- Guest `async_count`, `async_submit_count` and `async_abandoned` are all zero.
  All **60 slots are FREE**, async posted = retired = **6,475**, retirement
  sequence = **10,881**, and the watchdog is disarmed. GPU failure and timeout
  counters are zero; last completed fence is 4,381.
- Device `used->idx` and available index are both **10,881**. The guest consumer
  `q->used_idx` was **not retained**: the
  [capture helper](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/xv6-gpu-wait-snapshot.py)
  overwrote its `used_idx` output field with the device index. This snapshot
  therefore cannot compare device and consumer used indices directly.
- Both browser Wayland connections again have zero pending bytes in both
  directions, with matching KWin endpoints. Browser leader 217 is in `ppoll`.
  No unread callback bytes were captured in these socket rings.

The GPU helper completed with zero errors, and the Wayland helper recorded 49
threads. The saved-stack helper selected the role leaders but skipped **252, 62
and 217** because their high-half saved RSP values fell outside its assumed
identity-map bounds. It captured no leader stack bytes; this run gives **no
leader-stack or exact inner-call-site credit**. A later helper correction cannot
recover those missing bytes from this receipt.

The [terminal summary](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/observation-summary.json)
and [visible result](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/14-frozen-trace-upload-host.png)
retain **FAIL at 60,023 ms monotonic** (60,024 ms wall). There were two gaps and
two resumptions, 1,984 draws and 22 pixel checks per API, with zero GL/global
errors and unexpected losses. The first **9,042 ms** callback gap includes the
debugger pause. The later **7,160 ms** gap resumed without another intervention,
but remains part of a diagnostically interrupted trial. Neither resumption
clears the terminal failure or establishes recovery from a GPU watchdog fault.

The [before](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/10-before-extended-counters.txt)
and [after](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/15-after-extended-counters.txt)
boundary samples show async posted = retired increasing from 373 to 12,775,
with pending/failures/timeouts/context failures zero. DRM queued = read increased
from 69 to 2,062, with zero depth/drops/overflows. These endpoint measurements
remain distinct from the paused snapshot above.

The [frozen trace upload](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/14-frozen-trace-upload.txt)
completed **before browser close**. Its
[receipt](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/trace-upload-summary.json)
retains all **750,005 bytes** from offset zero, untruncated, SHA-256
`7630a2834e4e11644ebc7a7708ceb1ac366cd8df9f8f50776826ba7ea0f6a16b`.
The host independently matched the guest hash. The
[direct browser wait](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/17-browser-wait.txt)
returned `chromium_exit=0`, its completion marker and a fresh prompt.
[Owned cleanup](../build-x86_64/gui-progress-audit/chromium-wayland-kernel-gdb-20260907T223500Z/cleanup.json)
reaped controller 45188 at 22:42:55 UTC with launcher exit 0, no remaining QEMU,
overlay removal and unchanged base size/mtime. Worker and conductor independently
confirmed exact zero QEMU processes.

## Supported diagnosis and candidate validation boundary

The observed empty-population progress wait is consistent with a source race:
another consumer retires the needed command after a caller checks its predicate
but before the wait helper takes a new retirement snapshot. That newer snapshot
discards the progress, allowing a wait for unrelated future work while the GPU
operation mutex remains held. The candidate passes the caller's earlier snapshot
through serialization and compares it under the queue lock before rearming.
Release publication follows the associated retirement state changes; callers
retain their own predicate rechecks and timeout handling.

A deterministic host test compiles the exact production waiter bodies with
platform stubs. Its [baseline](../build-x86_64/gui-progress-audit/virtio-gpu-wait-progress-unit-baseline-20260907T230000Z/results.json)
fails four retirement-before-registration cases; its
[candidate](../build-x86_64/gui-progress-audit/virtio-gpu-wait-progress-unit-candidate-final-20260907/results.json)
passes all ten cases, including unchanged no-progress timeout controls. This
supports the race diagnosis and the narrow helper correction. It does not test
actual device IRQs, scheduling, all caller interleavings or sustained Chromium
animation. The separate [progress-wait fix report](virgl-progress-wait-fix-20260907.md)
records two fresh candidate boots passing the unchanged 60-second observation,
with no callback receipt wait above one second, plus context-restoration and
desktop checks. Those later results do not change the failing and interrupted
classification of the baseline receipts in this document.
