# Chromium local-video drop investigation — 2026-09-08 UTC

This investigation follows the two local-video failures in the
[progress-wait repair audit](virgl-progress-wait-fix-20260907.md).
It uses the repaired kernel without further kernel changes.

The new controls reproduce excess dropping on xv6: pooled full-load rates are
10.19% with Wayland tracing OFF and 14.76% ON, versus Linux's 3.09% OFF and
3.03% ON using the same browser executable, exact clip and diagnostic page.
All eight trials are valid. The comparison is qualified by different viewport
heights, native graphics libraries and a Linux wallet flag; each system has
only one completed boot with two trials per setting. Longer average xv6
frame-callback waits provide a pacing/consumption lead, without recurrence of
the previously repaired multi-second stall. The cause remains unresolved.

## Why a new control is needed

The earlier 15-second, windowed trials reported 141/911 dropped frames
(15.48%) and 133/905 (14.70%). Both inherited `WAYLAND_DEBUG=client`,
and live host screenshots overlapped playback. Serial result collection was
also used, but overlap with the measured interval has not been established.
Media advanced near real time without an HTML video error. There is no matched
pre-fix local-video pair, so these observations do not establish a regression
caused by the progress-wait repair.

Startup accounts for some, but not all, of those drops. Relative to the first
retained live screenshot, the remaining counters are 92/755 (12.19%) and
77/695 (11.08%). Those boundaries occurred at different times, approximately
2.5 and 3.5 seconds, and are not equivalent controlled startup exclusions.

Historical Linux windowed YouTube controls reported 9.740% and 7.862%
([reference](linux-kde-plasma-reference.md)). Different content, browser,
transport and collection conditions prevent using those percentages as the
baseline for this clip.

## Measurement contract

The new [fixture](../scripts/gpu/fixtures/chromium-video-drops.html) keeps the
15-second observation and the old acceptance formula: media/wall speed at least
0.9, no HTML media error, and dropped/total frames below 10%. Its legacy result
uses the full-load counters, as the original fixture did. Separate diagnostic
deltas subtract a baseline captured immediately after the play promise resolves.
They cover the whole measured window, startup through the first sample at or
after two seconds, and the remaining interval. The actual startup boundary and
its overshoot are retained.

The clip is the exact protected-image asset, extracted read-only with debugfs:

- H.264 High, yuv420p, 1280×800, 60 fps, 16 seconds.
- 15,740,901 bytes; SHA-256
  `e79cc0e2fbd02b9928fbf5d3c579566c6db7bb07da5ac923ebca7422599289df`.
- Extraction receipt:
  `build-x86_64/gui-progress-audit/chromium-video-drop-inputs-20260908T004059Z/inputs.json`.
- The larger host overlay clip is a different asset and is not used.

All new arms use same-origin HTTP with byte-range support. This is a deliberate
transport difference from the earlier `file://` trials. An improvement relative
to those trials alone cannot identify tracing, captures or transport as its
cause. The new page also gates playback until the browser is mapped and omits
the old loop's `document.title` update every 500 ms; that update can cause
additional browser/desktop work and remains a separate variable to isolate.
The within-harness comparison varies only browser Wayland tracing in
the order OFF, ON, ON, OFF, with a fresh browser profile for every arm.

Before each measured interval the controller verifies the actual browser
ownership and launch policy, focuses its window with the mouse and captures
the page. The page waits for a host start gate before loading media. From the
`started` event through `complete`, the controller makes no serial, SSH, QMP or
capture calls. The page buffers bounded 500-ms samples and media events, then
uploads after playback stops. Only media delivery remains active during the
measurement.

In this Chromium, `totalVideoFrames` maps to the cumulative decoded-frame
count and includes subsequently dropped frames. The report calls total/wall
`total_frames_per_second`. The old `decodedFPS` divided that cumulative count,
including preroll, by a differently anchored wall interval; it is neither
displayed FPS nor an isolated decoder-capacity measurement. JavaScript
video-frame callback count and rVFC
`presentedFrames` are retained separately. Neither is a physical-display frame
counter. Missing or regressing counters, missing callback coverage, early end,
invalid counter windows, backgrounding, resize, input or excessive deadline
overshoot invalidate diagnostics independently of the legacy result. A valid
drop-gate failure remains usable evidence of poor playback.

## Run identity and results

The kernel is
`build-x86_64/gui-progress-audit/virgl-wait-progress-candidate-build-20260907T225130Z/xv6.bin`,
SHA-256 `59253eb271555b6e1e3e035280002d79a7fc82f3e236b288ca981a0e495ed104`.
Each xv6 boot uses a disposable overlay of the protected
`audio-passcred-baseline-20260907T182610Z/fs.img` and the existing accelerated
MP1 launch mode, with SDL, corrected APT modules, virgl, six CPUs and 8 GiB.
No kernel or rootfs rebuild is part of this investigation.

The actual protected-image Chrome executable hashes to
`0b20b130e7edd9dd51873be867761295fe0cfad490c2b9a64f95bd3cfc08fa71`
(290,614,600 bytes), matching the staged executable selected for Linux.
The guest helper hashes it once in 1-MiB chunks before any timed arm, checks
file stability and verifies the subsequent direct child's executable path.
Actual browser arguments and graphics/loader/audio environment values are
retained for each arm. Fixture SHA-256:
`d5889aa784b8285ad7314a349db8817d71bf83c8792a53122f81ea74a7aef5bf`.

The first run, `chromium-video-drop-ab-20260908T005300Z`, is retained as an
incomplete comparison. HTTP page readiness preceded the first native browser
window mapping: its pre-start screenshot still shows the desktop, and the
fixture correctly rejects its unfocused initial state. Its 7.03% legacy result
is not accepted. Two subsequent traced trials have valid diagnostics and
legacy PASS results, 3.96% and 6.71%. The controller then rejected an exit receipt
because kernel diagnostics interrupted the serial line containing
`MEDIA_CHILD_EXIT=0`; the fourth arm was never launched. The VM wrapper exits
zero, the owned VM is reaped, its overlay is removed, the base is unchanged,
and exact QEMU inventory returns zero.

The retry requires graphical inspection of a mapped browser before granting
each start, with a final mouse focus click afterward. Its exit parser accepts
exactly one numeric exit value equal to zero within a completed marker/fresh
prompt response; it does not depend on the kernel leaving that line intact.

### Completed xv6 comparison

Receipt:
`build-x86_64/gui-progress-audit/chromium-video-drop-ab-20260908T005827Z/`.
All four trials have valid diagnostics, visible/focused windowed playback,
1018×592 CSS-pixel viewport, DPR 1, no HTML media error, direct browser exit 0,
and inspected terminal host captures showing the video and matching HUD.
These are four fresh browser profiles within one VM boot, N=2 per tracing
setting, rather than independent boot replicates. Host/guest cache effects
and time trends remain possible.

| Trial | Full-load dropped / total | Drop rate | After-startup dropped / total | Media/wall | Old gate |
| --- | ---: | ---: | ---: | ---: | --- |
| OFF 1 | 127 / 909 | 13.9714% | 107 / 783 = 13.6654% | 0.9964 | FAIL |
| ON 1 | 198 / 908 | 21.8062% | 158 / 785 = 20.1274% | 0.9937 | FAIL |
| ON 2 | 70 / 908 | 7.7093% | 52 / 781 = 6.6581% | 0.9948 | PASS |
| OFF 2 | 58 / 907 | 6.3947% | 39 / 784 = 4.9745% | 0.9961 | PASS |

| Counter window | Tracing OFF, pooled | Tracing ON, pooled |
| --- | ---: | ---: |
| Legacy full-load counters | 185/1816 = 10.1872% | 268/1816 = 14.7577% |
| Baseline-subtracted measured window | 184/1804 = 10.1996% | 266/1806 = 14.7287% |
| Startup, approximately first 2 seconds | 38/237 = 16.0338% | 56/240 = 23.3333% |
| After startup | 146/1567 = 9.3172% | 210/1566 = 13.4100% |

Actual startup boundaries are 2011.8–2030.2 ms. Each trace setting has one
passing and one failing trial. The observed pooled difference is 4.5705
percentage points, but the small sample and large variation within each
setting do not establish a stable tracing cost. The first run's two additional
valid ON trials were only 3.96% and 6.71%, further showing that a single pair
does not characterize this configuration reliably.

Excess dropping is reproduced with tracing disabled, no captures or console
commands during playback, settled focus, and no periodic title updates.
Those activities are therefore not necessary to trigger it. Startup alone
also does not explain the failing trials: their remaining intervals still
drop 13.67% and 20.13%.

The final counters have GPU failures/timeouts zero, posted = retired = 16,437,
pending zero, DRM queued = read = 3,287 and event depth/drops zero. These GPU/DRM
counters are distinct from HTML video drops. The VM wrapper and controller
both exit zero, all owned processes are reaped, the overlay is removed, the
base size/mtime remain unchanged and both worker and conductor exact QEMU
checks return zero.

### Callback evidence and interpretation

The OFF stderr intervals contain zero Wayland protocol lines. The ON intervals
contain 5,462 and 6,357, confirming the intended trace toggle. All 720 and 848
whole-browser surface-frame callback requests match completions; six
`wl_display.sync` callbacks per trial are excluded. There are no pending
frame callbacks, ID collisions or unmatched completions.

Within the measured windows, ON 1 and ON 2 contain 706 and 835 frame request/
reply pairs, close to their 706 and 834 baseline-subtracted non-dropped video
frames. Maximum client-observed request-to-reply waits are 39.795 and
33.161 ms; p99 is 31.444 and 24.168 ms. There is no multi-second callback wait.
The larger whole-browser maxima, 828.098 and 738.093 ms, occur during startup.
Alignment uses the Wayland log formatter's realtime-microsecond modulo epoch,
checked against all 16 internal Chromium UTC anchors per interval; independent
fixture start/end anchors agree within 0.801 ms. Client logs do not establish
the compositor's actual send time.

The retained `analyze-media-frame-cycles.py` and
`media-frame-cycle-analysis.json` reproduce the following measured-window
client timing. Values are milliseconds:

| Interval | ON 1 mean / p95 | ON 2 mean / p95 |
| --- | ---: | ---: |
| Frame request to reply | 10.665 / 23.058 | 6.523 / 17.714 |
| Reply to next same-surface request | 10.697 / 25.953 | 11.511 / 20.760 |
| Request to next request | 21.357 / 37.953 | 18.036 / 28.933 |

The worse-drop ON 1 trial has higher average request-to-reply latency; its
average delay before issuing the next request is slightly lower. These are
descriptive client timings, not a measurement of GPU execution or compositor
CPU time.

Maximum rVFC callback-entry gaps are 286.7, 267.1, 262.4 and 116.9 ms; after
startup they are only 59.7, 57.6, 58.3 and 51.0 ms. This evidence separates the
current dropped-frame problem from the earlier multi-second progress-wait
stall. It does not demonstrate host-hang recovery.

Chromium's pinned source explains why rVFC metadata can exceed the non-dropped
VPQ count. `VideoFrameCompositor` increments its presentation counter when it
selects a current frame; a later render can report that frame dropped if it
was never consumed. The fixture's `composition_submissions` field therefore
does not count physical display completion.
[Pinned compositor source](https://raw.githubusercontent.com/chromium/chromium/151.0.7922.34/third_party/blink/renderer/platform/media/video_frame_compositor.cc).

In the surface-submission path, an outstanding Viz acknowledgement can prevent
another unchanged-size submission even after the current frame was updated.
`PutCurrentFrame()` follows successful submission, before presentation feedback.
An alternative VideoLayer consumer also exists, and this audit does not yet
establish the selected runtime branch. Frame consumption and submission timing
are concrete leads for a bounded `media`/`cc`/`viz` trace; ACK gating is not yet
an established cause.
[Pinned submitter source](https://raw.githubusercontent.com/chromium/chromium/151.0.7922.34/third_party/blink/renderer/platform/graphics/video_frame_submitter.cc).

Initial rVFC metadata predates the VPQ baseline by 111–284.4 ms and final metadata
precedes the endpoint by 1.5–20 ms. Counter differences support investigating
loss after compositor admission, but do not provide exact per-frame attribution
or a strict numerical lower bound. Sparse `processingDuration` samples also do
not characterize decoder throughput.

### Linux control

The first prepared Linux boot,
`linux-local-media-20260908T010504Z-395293`, reached KDE and verified the copied
Chrome executable/version/dependencies, but timed out waiting for fixture
readiness. No playback result is accepted. The failed hook did not preserve
guest stderr before removing its disposable overlay, so the failure does not
identify a Chrome or GL defect. Owned cleanup/reap and the conductor's exact
zero checks passed.

This boot also exposed a mode difference: Linux selected 1280×800 at 75 Hz.
xv6's connector advertises its current mode at 60 Hz explicitly in
`kernel/kernel/dev/fb/fb_drm_kms_objects.c`. Subsequent Linux attempts use its
available 1280×768 at 60 Hz mode and record the actual browser viewport. The
completed control retains a 29-pixel viewport-height difference.

The next Linux boot, `linux-local-media-20260908T011433Z-401743`, preserves the
previously missing evidence. Its early host capture shows a KDE Wallet creation
dialog for Chrome blocking initial browser mapping. Mouse Cancel exposes the
browser, but the hook had already entered its readiness-timeout error path;
no timed playback is accepted. Browser/Bash/SSH exit 0, bounded stderr, owned
VM cleanup, overlay removal and exact zero are retained. This establishes a
session-integration blocker, not a GL failure.

The Linux measurement retry adds `--password-store=basic` to every arm to avoid
that dialog. This is an explicit difference from the xv6 command line. It also
sets `NO_AT_BRIDGE=1` and empty `GTK_MODULES`, following the current C wrapper's
defaults; those two variables were not captured in the xv6 owner whitelist, so
their actual cross-guest equality is unproven. Native Linux GIO/schema paths
and graphics libraries remain native. The executable and clip hashes still
match, but this is not an identical full-userland comparison.

With the wallet flag, `linux-local-media-20260908T011834Z-405414` reaches fixture
readiness. Its owner check then rejects Linux's rewritten process command line:
Chrome exposes one joined display string and relocated environment storage,
unlike xv6's retained argument/environment records. This is another premeasurement
harness rejection, not a playback failure. The subsequent inspector must retain
the original arguments/environment immediately before `execve`, verify the same
PID/start identity and executable afterward, and preserve the raw rewritten
process title separately. GPU-child discovery must also use executable/parent
identity rather than assuming argv remains split into its original fields.

`linux-local-media-20260908T012622Z-409418` then aborts before Chrome launch:
the native `kscreen-doctor -o` prints its mode list and exits 134 with
`malloc_consolidate(): unaligned fastbin chunk detected`. It supplies no media
result. The corrected mode helper preserves every return code/output, permits
one bounded retry of a failed read-only query and requires a fresh successful
verification after selecting 60 Hz. Ancillary final diagnostic failures remain
separate from completed media observations. Every failed boot retains owned
cleanup and exact-zero receipts.

### Completed Linux comparison

Receipt:
`build-x86_64/gui-progress-audit/linux-local-media-20260908T012918Z-411899/`.
All four trials have valid diagnostics, a visible/focused window, no HTML media
error and direct browser exit 0. They use fresh profiles within one VM boot,
N=2 per tracing setting. The controller preserves a mapped preview and final
mouse focus before each start gate, then makes no diagnostic or capture calls
during the timed interval. Terminal host captures show matching video/HUD
results. The desktop is 1280×768 at 60 Hz; the stable browser viewport is
1018×563, DPR 1, versus xv6's 1018×592.

| Trial | Full-load dropped / total | Drop rate | After-startup dropped / total | Media/wall | Old gate |
| --- | ---: | ---: | ---: | ---: | --- |
| OFF 1 | 18 / 907 | 1.9846% | 10 / 780 = 1.2821% | 0.9989 | PASS |
| ON 1 | 33 / 907 | 3.6384% | 22 / 781 = 2.8169% | 0.9990 | PASS |
| ON 2 | 22 / 908 | 2.4229% | 13 / 783 = 1.6603% | 1.0000 | PASS |
| OFF 2 | 38 / 908 | 4.1850% | 31 / 782 = 3.9642% | 0.9993 | PASS |

| Counter window | Tracing OFF, pooled | Tracing ON, pooled |
| --- | ---: | ---: |
| Legacy full-load counters | 56/1815 = 3.0854% | 55/1815 = 3.0303% |
| Baseline-subtracted measured window | 56/1807 = 3.0991% | 55/1806 = 3.0454% |
| Startup, approximately first 2 seconds | 15/245 = 6.1224% | 20/242 = 8.2645% |
| After startup | 41/1562 = 2.6248% | 35/1564 = 2.2379% |

Actual Linux startup boundaries are 2001.7–2009.9 ms. The pooled full-load
xv6 excess is 7.1018 percentage points OFF and 11.7274 points ON. Linux shows
no consistent tracing penalty in these two trials per setting. These are
descriptive differences, not estimates isolated from geometry, native
userspace, cache effects or host load.

Graphical inspection of `chrome://gpu` after the final trial shows OpenGL
enabled and hardware-accelerated canvas, compositing, rasterization and WebGL
(`final-chrome-gpu-host.png`). The same browser's retained player in
`chrome://media-internals` identifies this H.264 High 1280×800 clip as using
`FFmpegVideoDecoder`, with `kIsPlatformVideoDecoder=false`
(`final-media-internals-details-host.png`). The feature page's advertised
accelerated video-decode availability does not establish its use by this clip.

The GPU process maps native Linux `libEGL.so.1.1.0`, `libEGL_mesa` and
`libGLdispatch`. A separate native `glxinfo` process reports Mesa 25.2.8 virgl
over D3D12/RTX 4060; that string is not a browser-process renderer receipt.
The Chrome executable matches xv6, while its copied closure's absolute EGL/GLES
links are adapted to native Linux providers. No claim of graphics-userland
parity follows from executable equality. The existing experimental MP1 browser
policy remains in place, and the normal launcher is unchanged.

All trace toggles and browser exit checks pass. The Linux controller is
synchronously reaped with exit 0; the disposable overlay is removed, and
worker/conductor exact QEMU inventories return zero. Summary:
`linux-control-summary.json`; full observations:
`trial-summary.json` and `media-events.jsonl`.

### Cross-system pacing evidence

The two Linux traces have 893/913 whole-browser surface-frame requests and
883/902 pairs wholly within measurement. Every request matches a completion;
six display-sync callbacks per trial are excluded, with no collisions,
unmatched completions or pending frame callbacks. The parsers accept both
`#ID` and `@ID` protocol notation. All 14/10 Chromium UTC anchors bracket
correctly, and independent fixture epoch estimates agree within 0.3/0.5 ms.
Frozen stderr hashes match their upload receipts.

| System / trial | Request→reply mean / p95, ms | Reply→next request mean / p95, ms | Request cycle mean / p95, ms |
| --- | ---: | ---: | ---: |
| xv6 ON 1 | 10.665 / 23.058 | 10.697 / 25.953 | 21.357 / 37.953 |
| xv6 ON 2 | 6.523 / 17.714 | 11.511 / 20.760 | 18.036 / 28.933 |
| Linux ON 1 | 2.562 / 11.072 | 14.385 / 24.966 | 16.946 / 27.066 |
| Linux ON 2 | 2.526 / 10.707 | 14.072 / 24.754 | 16.600 / 27.053 |

Linux's request-to-reply maxima are 26.530/44.477 ms, and request-cycle maxima
48.391/44.529 ms. Both systems have short pacing delays rather than
multi-second waits in these windows. Linux has shorter average observed
callback latency and longer average delay before requesting another frame.
The xv6 ON 1 cycle mean exceeds the 16.667-ms period of a 60-fps clip, but
browser-surface requests include UI work and cannot be equated with video
frames. These logs do not separate compositor send time, transport latency,
client scheduling or GPU execution.

All post-startup samples on both systems report `readyState=4`, and neither
records a post-startup waiting/stalled/error/ended event. xv6 is not fully
buffered at the startup boundary, so HTTP delivery work remains a possible
load difference even without observed playback starvation. Sparse samples
cannot exclude all brief stalls.

Reproduction scripts `analyze-media-frame-cycles.py` (xv6),
`analyze-linux-media-frame-cycles.py` and `analyze-linux-on2-frame-cycles.py`
(Linux) are retained beside their input receipts. Linux's
`xv6-linux-frame-cycle-comparison.json` records input hashes, all interval
statistics and interpretation limits.

## Follow-up and validation

The evidence narrows the next investigation to frame pacing and consumption,
without justifying a kernel patch. A bounded Chromium `media`/`cc`/`viz` trace
should first establish the actual frame-consumer path, then correlate frame
selection, submission, acknowledgement and drops. Tracing overhead needs its
own control. Matching viewport geometry and repeating across independent boots
are necessary before a stronger cross-system attribution. This local windowed
clip does not refresh real YouTube/fullscreen parity or prove host-hang recovery;
DESK-03 and the separate recovery work remain open in the active plan.

The committed fixture is the exact frozen version exercised by the eight valid
trials. It requires a same-origin controller serving `/clip.mp4` with byte-range
support, accepting at most 64-KiB JSON reports at `POST /media-result` with a
2xx acknowledgement, and responding
to `GET /trial-control?trial=ID` with `{start, abort}` booleans. Open
`/?trial=off-1` (or another unique lowercase alphanumeric/hyphen ID), inspect the
mapped window and focus before releasing the gate. Keep observation calls out
of the `started` through `complete` interval. Gate waiting is bounded to 120 s;
the controller must enforce an external deadline for page readiness, media
start and completion because guest timers cannot detect an entirely stalled
browser. The run controllers use a 35-second media-phase watchdog and preserve
direct child exits. Ready/start/error results never count as completed trials;
acceptance requires `phase=complete`, `diagnostic_valid=true` and `legacy.status=PASS`.
The visible HUD/title shows only the legacy result and can report PASS when
diagnostics are invalid.

The controllers and preparation scripts are retained as receipt artifacts
(`http-server.py`, `linux-ready-hook.py`, `linux-inspector.py`, and xv6's
`controller.py`/`owner-helper.py`), with run-specific identities and paths.
They are not a general launcher. Runtime validation covers all eight completed fixture
trials, both trace states, graphical content/focus and clean browser/VM teardown.
No production source, submodule, kernel image or rootfs is changed by this audit.
