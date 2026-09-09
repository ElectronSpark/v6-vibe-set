# Chromium GL recovery audit — 2026-09-07

The candidate kernel supports the tested WebGL 1/2 context loss/restoration,
canvas resize and fullscreen transitions, but **sustained animation remains
unresolved**. All three requested 60-second soaks stopped early because animation
callbacks stopped progressing. Timer-driven failure reports reached the host and
became visible; GPU timeout/failure counters remained zero and all posted GPU work
was retired at the sampled boundaries. This is an animation-callback pause, not
established evidence of a virgl watchdog or whole-desktop hang.

## Run and provenance

- Receipt: `chromium-gl-fence-20260907T193916Z`, 19:39:16–19:50:14 UTC.
- Candidate kernel SHA-256: `df79132d675029ff03386989574e8b1dbb9697adcd3236dc455b404ca147487b`.
- The actual command line has `virtio_gpu_fence_order_probe=0`,
  `virtio_gpu_async_stall_ms=5000` and the existing SDL/virgl configuration.
  This GUI run does not exercise the deterministic fence-order hook.
- QEMU 9.0.2, SDL, corrected APT modules, `virtio-vga-gl-primary`, six CPUs,
  8 GiB, virtio input, 1280×800 guest. `AUTO_BUILD=0`; existing private raw image
  with a disposable owned overlay. Dedicated serial socket and one-shot host
  window fitting are diagnostic harness settings.
- Chrome for Testing 151.0.7922.34 used the existing
  `WAYLAND_CHROMIUM_MULTIPROCESS=1` opt-in launcher, with actual Plasma PID 88's
  environment and fresh `/tmp/gl-first` and `/tmp/gl-second` profiles. The normal
  desktop launcher default was not changed. No new GL/ANGLE/browser flags were added.
- Verified environment: `EGL_PLATFORM=wayland`, `GALLIUM_DRIVER=virgl`,
  `MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu`, `LIBGL_ALWAYS_SOFTWARE=0`,
  `WAYLAND_DISPLAY=wayland-0`, and the normal UNIX Pulse server. Importing padded
  `/proc/88/environ` produced empty-entry export warnings; the relevant values
  were subsequently read back explicitly.
- Fixture SHA-256: `4aeeb5b24d53a756dd5b6258f802dd8171ad64531b8a96bfe01c8c0c13846096`.
  It was served at `http://10.0.2.2:41007/`, with separate `run=first`/`run=second`
  identifiers and append-only POST receipts containing host UTC plus guest times.

Evidence: [provenance](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/provenance.json), [actual QEMU argv](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/qemu-cmdline.json),
[guest command line/initial roles](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/02-fresh-roles-cmdline.txt),
[verified environment](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/04-selected-env-log-baseline.txt),
[first browser/GPU argv](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/22-failure-argv-logs.txt.escaped.txt),
[second browser/GPU argv and log](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/35-second-argv-log.txt.escaped.txt).

## Graphical controls and outcomes

QMP mouse coordinates were derived from current captures, using guest origin
(38,59) within a 1356×897 host-client capture. Host wheel input exposed the context
controls. Mouse clicks performed the operations below; serial commands only
launched processes and collected diagnostics. No mouse events occurred between a
soak's start and its terminal result. The recovery and second-profile intervals
also had no intermediate host captures.

| Operation | Observed result |
| --- | --- |
| Initial WebGL 1 and 2 | Both ready; shader triangle and center/corner readback pass. |
| Lose, then restore WebGL 1 | Expected loss event; restoration PASS, generation 2, resources rebuilt, readback pass. |
| Lose, then restore WebGL 2 | Same independent PASS; both contexts generation 2, no unexpected loss or GL error. |
| Enter fullscreen | Fixture fullscreen event and visible full-screen canvases; pixel checks pass. |
| Resize while fullscreen | Both canvases change 320×192 → 640×384; pixels pass. |
| Exit fullscreen | Browser returns to window view; both pixel checks pass. |
| Close first browser | Window disappears; synchronous `wait` returns exit 0. |
| Open a second fresh profile | Both APIs again initialize and pass initial pixels. |
| Close second browser | Window disappears; synchronous `wait` returns exit 0. |

The renderer reported by both APIs was
`ANGLE (Mesa, virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)), OpenGL ES 3.1)`.
This identifies the underlying renderer; API strings were WebGL 1.0/ES 2.0 and
WebGL 2.0/ES 3.0. It does not establish desktop OpenGL conformance.
Restoration PASS covers fresh resources and two framebuffer samples per context.
The five-second restoration deadline covers event arrival; rebuilding and
readback occur afterward, so PASS does not establish total recovery within five
seconds. Pixel readback alone does not prove compositor presentation or recovery
from a kernel/host GPU hang.

Screens: [initial triangles](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/09-fixture-visible-host.png),
[both contexts restored](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/14-both-restored-host.png),
[fullscreen](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/16-fullscreen-enter-host.png),
[fullscreen resize](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/17-fullscreen-resize-host.png),
[fullscreen exit](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/18-fullscreen-exit-host.png),
[fresh profile ready](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/31-fresh-profile-launch-host.png).

## Three separate soak failures

Each context's draw/check counts below are measured within that soak, excluding
initialization, restoration and transition checks. All three terminal results had
`visibility=visible`, zero GL/global errors and zero unexpected context losses.

| Trial | Terminal host UTC | Elapsed monotonic | Last-frame gap | Draws/checks per API | Result |
| --- | --- | --- | --- | --- | --- |
| First profile, after restoration/fullscreen/resize | 19:44:30.378 | 12,011 ms | 5,079 ms | 252 / 3 | FAIL: animation stopped progressing |
| Same profile, after host focus intervention | 19:46:48.456 | 6,255 ms | 5,177 ms | 42 / 0 | Same FAIL |
| Fresh second profile, no preceding loss/resize/fullscreen | 19:48:36.948 | 6,009 ms | 5,158 ms | 32 / 0 | Same FAIL |

The first screen reports 12,012 ms because it displays elapsed wall time.
The fixture detects a last-frame gap above five seconds with a 250 ms timer;
the roughly 5.1-second values are threshold crossings, not measured recurring
GPU timeout durations. On failure it cancels the pending animation callback.
These trials therefore measure time to failure detection, not the natural
duration of the pause or whether callbacks would eventually resume unaided.
The measured gap can include drawing/readback time and does not isolate GPU work.
[First failure](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/20-soak-midpoint-host.png),
[focus-intervention failure](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/27-recovery-soak-failed-host.png) and
[fresh-profile failure](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/33-second-profile-soak-failed-host.png) all visibly show
the terminal result. This differs from the earlier
[baseline audit's stale capture](chromium-gl-lifecycle-audit-20260907.md).
The independent generation-2 restoration successes do not turn these failures
into a passing soak.

Before the second trial, a single host titlebar drag was attempted. Its helper
reported foreground ownership, but client geometry remained (564,123), so an
actual window move/expose is unproven. A subsequent explicit host click also
reported `foreground=True`. Captures 20, 24, 25, 27 and 33 show QEMU's input-grab
title. These are sampled focus/grab observations, not continuous focus telemetry
or proof that focus caused the pauses. Neither intervention nor browser restart
restored a sustained 60-second run.

The [raw 30 POST receipts](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/webgl-results.jsonl),
[structured summary](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/webgl-summary.json) and
[full action timeline](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/actions.jsonl) retain every outcome in order.

## GPU and log boundary

| Counter | Before browser | After first failure | After second-profile failure |
| --- | ---: | ---: | ---: |
| Failures / timeouts / context failed / context failures | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| Async posted = retired | 227 | 2,391 | 3,106 |
| Async pending | 0 | 0 | 0 |
| Submits / last fence | 142 / 145 | 1,546 / 1,549 | 2,009 / 2,012 |
| Submit admission stalls | 0 | 18 | 22 |
| Maximum admission wait | 0 µs | 20,268 µs | 22,436 µs |
| Display presents = completions | 43 | 423 | 549 |

[Counter comparison](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/gpu-counter-comparison.json),
[first failure roles/counters](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/21-after-stall-counters-roles.txt),
[final roles/counters](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/34-final-counters-roles.txt).
KWin remained PID 62 and Plasma PID 88. First browser/GPU roles were 217/251;
second browser/GPU roles were 387/422. GPU progress counters increased, but no
sample establishes why Chromium stopped scheduling animation callbacks.

The canonical Chromium log was absent before the first launch; launch markers
and PID-specific records separate the two fresh runs. It reached 6,601 bytes
at the first failure inspection and 7,512 bytes before first close. The complete
[first pre-close log](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/28-preclose-browser-log.txt) and
[second-launch interval](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/35-second-argv-log.txt.escaped.txt) contain startup
Wayland/Vulkan warnings, unavailable desktop services and background registration
errors, without a reported frame/presentation/sync-token failure explaining the
pauses. The [compositor log](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/36-compositor-log.txt) reports virgl/ES 3.1; it
was already 31,720 bytes at the pre-browser boundary, so its untimestamped startup
messages are not assigned to either pause.

A named-file [log scan](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/log-review.json) found no matching late GPU timeout,
quarantine, panic, console-drop or presentation/IPC failure signature in the
complete retained logs. Original serial/debugcon bytes are preserved with
[escaped derivative hashes and transformation](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/escaped-log-provenance.json).
No kernel watchdog recovery, performance, video decode or audio credit follows
from these functional controls. The callback-pause cause remains open.

## Completion and exclusions

Both browser [wait results](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/30-first-browser-wait.txt)
[returned zero](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/38-second-browser-wait.txt). The owned controller was
synchronously reaped with launcher exit 0; [cleanup](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/cleanup.json) confirms
no remaining QEMU, overlay removal and unchanged base size/mtime. An independent
[exact inventory](../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193916Z/final-exact-inventory.txt) also reported zero.

A preceding startup-only receipt `chromium-gl-fence-20260907T193806Z` closed on
controller stdin EOF because the host invocation omitted its PTY; it ran no
Chromium test and is not a guest failure. This replacement GUI run used retained
PTY session 31264. No additional VM or YouTube run followed these results.
