# Virgl progress-wait registration fix — 2026-09-07

The GPU progress waiter could discard a completion that arrived after its
caller checked for pending work. Chromium resource cleanup then slept while
holding the graphics operation mutex, blocking KWin submissions even after
all tracked asynchronous work had retired. The repair carries the caller's
retirement sequence into wait registration. Kernel build, Sparse and the
deterministic host regression pass. Two fresh virgl boots pass the unchanged
60-second WebGL observation without stalls. Local video plays, but both
fresh-profile trials fail the existing dropped-frame threshold. A separate
YouTube trial passes graphical playback controls and captured-audio verification.

## Evidence and failure mechanism

The [frame-callback audit](chromium-frame-callback-audit-20260907.md) retains
natural 6.9–7.5-second animation gaps and the later memory-only debugger
captures. In `chromium-wayland-kernel-gdb-20260907T223500Z`, KWin PID 62 was
waiting for the operation mutex in EXECBUFFER. Chromium GPU PID 252 held that
mutex for a resource operation and waited on `async_wait` during GEM_CLOSE.
Guest state showed zero active, submitted or abandoned asynchronous slots,
all 60 slots FREE, and 6,475 posted and retired commands. This is a guest
wait with no tracked pending work, not proof of an outstanding host GPU hang.
The debugger paused guest execution, so that trial is diagnostically interrupted.

The source permits this ordering:

1. A caller checks its pending-work predicate and finds work remaining.
2. A concurrent reaper completes that work and advances `async_retire_seq`.
3. The old helper acquires the waiter mutex and queue lock, then snapshots the
   already-advanced sequence and rearms the shared completion.
4. With no remaining work, there need not be another completion. The helper
   waits through its progress budget before the caller rechecks its predicate.

The async budget is still five seconds in this workload. Its existing nominal
one-millisecond wait slices can take longer in wall time under scheduling;
this patch does not change that accounting or lengthen any deadline.

## Implementation

Kernel commit: `81cbd0c5` — Preserve GPU retirement progress across wait registration.

In [virtio_gpu.c](../kernel/kernel/virtio_gpu.c), all four progress-wait callers
now acquire-load the retirement sequence **before** checking their predicates:
stale synchronous completion parking, asynchronous drain, target-fence drain,
and queue admission. The helper preserves that sequence through mutex
acquisition and checks it under the queue lock before rearming the completion.
Already-observed progress returns to the caller for reaping and a fresh check.

All five retirement-sequence updates use release ordering after the associated
state updates, including fence validation, slot counts, stale synchronous
completion state and abort generation. An acquire snapshot therefore cannot
observe the new sequence while missing the state it publishes. Queue locking
still serializes completion registration and signaling.

The patch preserves posted-age accounting, admission depths, final predicate
checks, quarantine ownership and abort-generation failure handling. It adds
no production fault injection and changes no browser launcher policy.

## Deterministic regression and build

[test-virtio-gpu-wait-progress.py](../scripts/gpu/test-virtio-gpu-wait-progress.py)
compiles the actual production condition, wait-loop and progress-helper bodies
with deterministic host platform stubs. Its API switch changes only the
invocation between old two-argument and new three-argument forms; the assertions
are identical. The regression models the waiter contract, not the real device,
CPU scheduler, reaper or complete caller loops.

| Case | Baseline | Candidate |
| --- | --- | --- |
| Retirement before helper entry: active, abandoned, stale sync | 3 FAIL | 3 PASS |
| Retirement during waiter-mutex acquisition | FAIL | PASS |
| Unchanged pending work: active, abandoned, stale sync | 3 PASS | 3 PASS |
| Retirement after registration, signaled or polled | 2 PASS | 2 PASS |
| Existing unconsumed used entry | PASS | PASS |

The four failing baseline cases unnecessarily rearmed and slept all three test
budget slices. The candidate returns progress with zero sleeps and no rearm;
the modeled caller rechecks its predicate. Unfinished work still waits.

- [Baseline results](../build-x86_64/gui-progress-audit/virtio-gpu-wait-progress-unit-baseline-20260907T230000Z/results.json):
  four behavioral failures; compiler exit 0 and test exit 1.
- [Final candidate results](../build-x86_64/gui-progress-audit/virtio-gpu-wait-progress-unit-candidate-final-20260907/results.json):
  10/10 PASS, compiler and test exit 0; source SHA-256
  `a74a9948897eed8f23a28a3ba1f08460870fce1105fbaea44a3782e4a7b9e88b`.
- [Build source manifest](../build-x86_64/gui-progress-audit/virgl-wait-progress-candidate-build-20260907T225130Z/source.json)
  and [kernel patch](../build-x86_64/gui-progress-audit/virgl-wait-progress-candidate-build-20260907T225130Z/kernel.patch):
  seven GPU source hashes were checked unchanged after the build.
- [Kernel build](../build-x86_64/gui-progress-audit/virgl-wait-progress-candidate-build-20260907T225130Z/build.log)
  and [Sparse](../build-x86_64/gui-progress-audit/virgl-wait-progress-candidate-build-20260907T225130Z/sparse.log):
  both exit 0; Sparse checked two files with zero failures/errors.

Candidate kernel SHA-256:
`59253eb271555b6e1e3e035280002d79a7fc82f3e236b288ca981a0e495ed104`.
Matching ELF SHA-256:
`ea72a11a3795509df404242bfd7014a75504bf6fedd1ef23ce970777c043ac82`.
The protected audio baseline filesystem is unchanged. Before creating this
fourth kernel iteration, the oldest unused instrumented GL baseline binary and
ELF were removed after exact zero-QEMU and open-file checks; its
[retention receipt](../build-x86_64/gui-progress-audit/virgl-fence-baseline-build-20260907T192302Z/kernel-artifact-retention.json)
preserves hashes. Audit evidence and the audio/fence rollback kernels remain.

## Runtime validation

The first fresh candidate boot,
`chromium-wayland-frame-20260907T225304Z`, passed the unchanged 60-second
observation with no input, debugger pause or capture during its timed interval.
Both APIs rendered 3,545 draws with 29 pixel checks each, zero errors and zero
unexpected context losses. Elapsed time was 60,019 ms; the largest callback-entry
gap was 229 ms, callback execution 56 ms and timer gap 274 ms. The document
remained visible and focused, with zero detected stalls or resumptions.

The [frozen trace analysis](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T225304Z/frame-callback-analysis.json)
matches all 3,554 frame callbacks on the same-run surface 28, excluding nine
display-sync callbacks. Maximum client-observed callback wait is 187.635 ms,
with none exceeding one second. All 16 paired Chromium UTC anchors bracket
correctly. The frozen 1,304,682-byte trace is complete and untruncated, SHA-256
`658c9e35c6231736a96f8ec5e3d0149cd57bd1ca539fd5995abf21cf0b83d5c4`.
Callback receipt latency is distinct from a presentation-rate measurement.

After the timed run, mouse actions lost and restored both WebGL contexts.
Both rebuilt generation 2 resources and passed fresh pixel readbacks. Fixture
controls entered/exited fullscreen and resized the canvas buffers to 640×384:
[restored contexts](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T225304Z/15-restore-webgl2-host.png),
[resized buffers](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T225304Z/16-fullscreen-resize-host.png).
The host assistant overlay obscures part of the right canvas; both diagnostic
readouts remain visible. GL QMP screendump reports no surface, so these are host
captures, without independent guest-image or exact fitting credit.
Final counters showed 22,648 posted/retired commands, zero pending work and zero
failures/timeouts; all 3,701 DRM events were read with zero depth or drops.
The direct browser process exited 0 with a serial completion marker and fresh
prompt. The same KDE boot passed
[`/bin/wakestorm guihd 240 5 3200 32`](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T225304Z/21-guihd-wakestorm.txt) under
the bounded process wrapper: five completed 3,200-ms rounds in 16,597 ms,
zero unfinished threads, child exit 0 and no timeout. This is the same scheduler
workload in the actual six-CPU/8-GiB graphical environment, not the standalone
four-GiB/no-GPU probe configuration.
[Cleanup](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T225304Z/cleanup.json)
records launcher exit 0, no remaining QEMU, removed overlay and unchanged base
size/mtime; worker and conductor independently confirmed exact zero.

The independent second boot, `chromium-wayland-frame-20260907T230235Z`,
also passed with the same kernel, filesystem, virgl policy and fixture:

| Measurement | First boot | Second boot |
| --- | ---: | ---: |
| Observation duration | 60,019 ms | 60,025 ms |
| Draws / pixel checks per API | 3,545 / 29 | 3,492 / 29 |
| Maximum callback-entry gap | 229 ms | 212 ms |
| Maximum callback execution | 56 ms | 199 ms |
| Maximum timer gap | 274 ms | 284 ms |
| Stalls / unexpected context losses / GL errors | 0 / 0 / 0 | 0 / 0 / 0 |
| Maximum Wayland callback receipt wait | 187.635 ms | 203.406 ms |

The second [trace analysis](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T230235Z/frame-callback-analysis.json)
matches all 3,501 surface-39 frame callbacks, excludes six display-sync callbacks,
and validates all 17 paired UTC anchors. Neither candidate trace has a frame
callback receipt wait above one second, unmatched callbacks or ID collisions.
The second complete frozen trace has 1,274,457 bytes, SHA-256
`5f4df5aff9aba34c6c5773c0ad49b9370f217224a6222a06ea47f1fc6f2df384`.
Both raw hashes were unchanged after analysis. The retained baseline repeat had
four 6.9–7.5-second callback-entry gaps under the same observation workload.
These bounded results support this registration repair; they are not an
exhaustive absence-of-stalls claim.

## Local media: playback works, dropped-frame gate fails

Two fresh Chromium profiles in the second candidate boot opened
`file:///share/webkit/perf-video.html?ms=15000`, with the same asset, window
geometry and launch policy. `WAYLAND_DEBUG=client` remained enabled in these
two diagnostic media trials, so they are not an untraced performance baseline.
Both showed advancing test-pattern frames, reached
the end of the 16-second asset without a video error, and exited 0 with serial
completion markers and fresh prompts. They are two same-boot trials, not two
independent media boots or matched Linux controls.

| Measurement | Trial 1 | Trial 2 |
| --- | ---: | ---: |
| Wall time / media advance | 15.09 s / 15.00 s | 15.12 s / 14.90 s |
| Media/wall speed | 0.994 | 0.986 |
| Video-frame callbacks/s | 42.15 | 38.37 |
| Total video frames/s (fixture `decodedFPS`) | 60.37 | 59.86 |
| Dropped / total frames | 141 / 911 | 133 / 905 |
| Dropped percentage | 15.48% | 14.70% |
| Unchanged fixture verdict | FAIL | FAIL |

The [actual staged fixture](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T230235Z/staged-perf-video.html)
requires speed at least 0.9, no video error and **less than 10% dropped frames**.
Both failures are retained without relaxing that criterion. Its headline FPS
may fall back to total-frame or nominal-speed estimates; the table preserves the
individual fields and earns no presentation-parity credit. No matched old-kernel
media trial here establishes whether the drops changed with this patch.

Evidence: [trial 1 metrics](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T230235Z/15-media1-result.txt),
[trial 2 metrics](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T230235Z/19-media2-terminal-result.txt),
[live content](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T230235Z/13-media1-live-host.png),
[later content and HUD](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T230235Z/19-media2-terminal-result-host.png).

The protected image's MP4 is 15,740,901 bytes, SHA-256
`e79cc0e2fbd02b9928fbf5d3c579566c6db7bb07da5ac923ebca7422599289df`.
Its HTML is 5,591 bytes, SHA-256
`017b03f2fc81c4be279200324eeac457f994c52ed65d410ff354808da499ee0c`;
a read-only extraction from the protected image independently matches the guest
hash. The differently sized host-overlay video was not substituted. The local
clip is muted and earns no audio credit.

Second-run [cleanup](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T230235Z/cleanup.json)
completed with launcher exit 0, no remaining QEMU, removed overlay and unchanged
base size/mtime. Worker and conductor independently confirmed exact zero.
The controller's bound expired after the second browser's exit receipt and
before the requested final counter command; no final GPU-counter claim is made
for this run.

## YouTube and audio regression

The third fresh candidate boot, `chromium-wayland-frame-20260907T231151Z`,
used the same kernel and protected image. It imported the actual Plasma
environment, retained `GALLIUM_DRIVER=virgl` and the normal Unix Pulse endpoint,
and used the existing multiprocess Chromium opt-in with a fresh profile.
`WAYLAND_DEBUG` was unset for this media-only trial.

Mouse Play started Big Buck Bunny (`aqz-KE-bpKQ`). The
[playing capture](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T231151Z/13-youtube-progress-controls-host.png)
shows **1:16 / 10:34** after 78.2 host-monotonic seconds from the click, with
different scene content, an active pause control and unmuted volume icon.
Mouse pause at 1:28, resume, [fullscreen entry](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T231151Z/17-youtube-fullscreen-host.png)
and exit all changed the visible state. The final player HUD reached **3:23**.
The video title's “60fps 4K” text is not evidence of selected resolution, frame
rate or hardware decoding; no fresh codec/quality measurement is claimed.

The [direct browser wait](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T231151Z/20-direct-browser-exit-sound.txt)
returned exit 0 with a completion marker and fresh prompt. `/dev/sndstat`
played bytes increased from 3,182,720 before mouse Play to 48,266,624 after
browser exit, with pending bytes zero and ALSA state zero at the end. A
YouTube-only WAV interval was bounded by host file-size cursors before play
and after the direct browser exit.

The [post-cleanup audio analysis](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T231151Z/youtube-audio-analysis.json)
contains **233.4054 seconds** of stereo PCM16 at 44.1 kHz, with 19,569,720 of
20,586,360 samples nonzero, peak 32,766 and RMS 1,901.238 (-24.728 dBFS).
The [extraction script](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T231151Z/audio-extract.py)
retains pre-play cursor 3,002,368 bytes and post-exit cursor 44,351,488 bytes
with their UTC/monotonic timestamps. A one-second front guard and PCM-frame
alignment select source range **[3,178,768, 44,351,488)**; no other deliberate
media or test tones ran during this boot. Extracted WAV SHA-256:
`48b2d245e3a7243857372f18f3ef70af961f7fff8fc18fec5889550e7e8913fc`.
This verifies non-silent recorded output, not host-speaker listening quality,
audio/video synchronization or a correspondence between PCM duration and wall time.

[Final GPU counters](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T231151Z/22-after-gpu-counters.txt)
show zero failures/timeouts, 35,375 posted and retired commands, zero pending
work, and all 7,159 DRM events read with zero depth or drops. The 160 admission
waits with a maximum of 10,788 microseconds are not watchdog timeouts.
[Owned cleanup](../build-x86_64/gui-progress-audit/chromium-wayland-frame-20260907T231151Z/cleanup.json)
completed with launcher exit 0, no remaining QEMU, removed overlay and unchanged
protected base size/mtime. The conductor independently confirmed exact zero.
The optional untraced local-media repeat was skipped to preserve time for
browser exit and cleanup; the earlier traced drop failures remain unresolved.

## Remaining validation limits

The legacy `perf-video-gate.expect` cannot run unchanged on this image:
`/bin/weston-session` is absent, and the harness cleanup predates the required
owned-PID and synchronous-reap contract. Local Chromium playback can exercise
the retained asset, but cannot substitute for that WebKit/Weston gate or the
matched Linux/xv6 media-performance requirements. No parity claim is made.

These checks do not close DESK-02, establish recovery from an actual host hang,
or validate the separate synchronous-timeout DMA lifetime.
