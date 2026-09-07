# Active goal: stable SDL KDE and Linux-comparable YouTube playback

Updated: 2026-07-19 UTC

## Outcome

Make the normal x86_64 SDL/virgl launch show the complete 1280x800 KDE
desktop without a black or oversized window, remain interactive under browser
load, and play real 1280x720 YouTube with audio at a level reasonably close to
the matched Linux KDE VM.

Current status:

- [x] SDL native geometry and scaled absolute input are controlled. A guest
  mode transition can still make stock APT SDL drift from 1280x768 to
  1356x897, so the launcher re-applies the fit only when the client changes.
- [x] Current xv6 KDE renders on the untouched stock-APT SDL/OpenGL modules.
  The nonce-bound 2026-07-18 gate proves a live Qt Wayland surface in both the
  direct QEMU scanout and host SDL window at 1280x800, with the kernel boot logo
  absent from both captures.
- [x] A complete rootfs was rebuilt from the current workspace and passes
  read-only `e2fsck`. The image is KDE/Plasma-only: stale Weston payloads are
  pruned, the validated Xwayland wrapper and real server are both present, and
  the required XKB compiler/data are staged reproducibly.
- [x] The final N=2 xv6 windowed YouTube receipts pass after the rootfs
  rebuild. The loader repair is reproducible: the ATK port builds a real
  versioned shared library for GTK, while Chromium receives Ubuntu's matching
  ATK beside its private `libatk-bridge`; Mesa/DRM/Wayland remain guest-owned.
  The untraced receipt presents 59.618 fps with 2.174% VPQ drops and the traced
  receipt presents 59.835 fps with 1.131% drops. Both advance media at real
  time and prove Pulse stream, hardware-played, and QEMU WAV byte growth.
- [x] The opening-image stall was reduced to three xv6 DRM/KMS compatibility
  gaps: dynamic libdrm MODE_ID blobs were rejected, KWin's ordinary unfenced
  `NONBLOCK|PAGE_FLIP_EVENT` commits were rejected, and xv6 advertised a cursor
  plane on Bochs even though only virtio-gpu owns a cursor queue. All three are
  fixed and the Wayland sentinel now advances frames.
- [x] A fresh same-host Linux control on the untouched APT QEMU renders KDE
  and Chromium/Wayland and sustains 62.834 presented fps for 1280x720 video.
  This supplies the behavioral boundary before further kernel changes.
- [x] Three current stock-APT behavior samples complete native Wayland registry
  and KWin D-Bus roundtrips. Their pooled warm Konsole median is 206 ms, idle
  CPU averages 0.271% across six vCPUs, and the two untraced glmark scores
  average 98. The third sample supplies a current syscall trace.
- [x] The external VM coexistence contract is enforced without signalling the
  external VM. The external RISC-V supervisor may replace its PID during a
  long control; the harness records that churn while cleanup targets only its
  own PID/start-time/token.
- [x] Browser/kernel freezes found during this work have focused reducers and
  fixes; the fresh delayed-group wait/reap reducer passes after the final
  wait-family repair.
- [x] The remaining D-Bus/desktop #GP was traced to a TLB-shootdown
  acknowledgement race. Per-CPU monotonic full/page tickets now prevent one
  request from acknowledging a different or newer invalidation.
- [x] Real Pulse -> ALSA -> virtio-snd -> QEMU WAV playback is proven.
- [x] Pulse recovery after a live virtio-sound reset passes twice with the
  Chromium-shaped overlapping-stream reducer. The descriptor-fragmentation
  `-EAGAIN` that stopped Chromium's audio/media clock is fixed.
- [x] Post-TLB-fix windowed playback completes twice without a freeze:
  `xv6-windowed-20260716T022828Z-532832-direct-pulse` and
  `xv6-windowed-20260716T024843Z-552809-direct-pulse`.
- [x] Actual player-owned fullscreen is deterministic and semantically proven.
  With the final direct-Pulse policy, two strict trials pass at 50.551 and
  51.347 fps. The refreshed matched Linux trials pass at 51.887 and 49.732
  fps, so xv6 reaches 100.3% of the Linux N=2 presented-fps mean.
- [x] SDL+virgl launches default to asynchronous KMS presents and the corrected
  phase-stable 60 Hz present clock. The Bochs compatibility path now accepts
  KWin's unfenced NONBLOCK flips but still completes them synchronously; it is
  documented as compatibility, not misreported as true async behavior.
- [x] SDL+virgl launches also default the virtio-gpu submit admission depth to
  three. That is the measured performance fix: it lets host virgl/D3D12 work
  overlap instead of repeatedly stalling the guest submit path, while an
  explicit kernel flag remains available for diagnostic controls.
- [x] Async `SUBMIT_3D` admission is now independent of async KMS
  flush/scanout occupancy, and its capacity wait drops the global virtio-gpu
  operation lock. KWin still has one event-bearing page flip pending at a time,
  as on Linux, but renderer admission no longer serializes the KMS worker
  behind the previous virgl submission.
- [x] The 2026-07-19 long-lived desktop observation exposed a remaining KMS
  ownership regression: repeated hover/cursor damage left vertical trails and
  responsiveness degraded over time. The page-flip path incorrectly treated
  `SET_SCANOUT` as permanent resource registration: after KWin's three BOs had
  each appeared once, cycling back to an older BO skipped the rebind and QEMU
  kept scanning out a different resource. Every actual resource switch now
  issues `SET_SCANOUT`; the three-resource set is diagnostic only. Per-flip
  host waits, fenced scanout flushes, and broad resource-reuse waits are
  rejected as defaults because they reduce hover to 6--8 seconds or video to
  37.769 fps. On the corrected unfenced/pipelined path, 12/12 hover-ins complete
  at 258.042 ms median and 12/12 hover-outs at 183.631 ms, and the final host
  capture has no accumulated plus-sign trails. Build, Sparse, owned teardown,
  and the error-log scan pass.
- [x] The remaining multi-second Kickoff latency was an input/session issue,
  not normal scanout cost. Evdev now timestamps records from the kernel's real
  monotonic clock instead of advancing a synthetic clock by 1 ms per record,
  eliminating KWin event backlogs that had reached 425 seconds. Absolute tablet
  packets no longer emit duplicate relative motion. The warm-up/probe dismissal
  point also moved from the screen center—which is inside an open Kickoff popup
  and had launched System Settings—to an empty upper-right desktop point. The
  final fresh-image run completed pristine (`pristine=1`), with no accidental
  System Settings process or plus-sign trails: the cold 2143 ms open becomes
  320--365 ms after desktop warm-up, closes take 140--149 ms, and associated
  framebuffer presents average about 2.6--8.8 ms. Supported SDL+virgl launches
  now enable Kickoff/tooltip warm-up and a 50 ms tooltip delay by default; every
  policy remains explicitly opt-out with a kernel command-line value of zero.
- [x] Asynchronous GPU commands have one coalesced watchdog armed for five
  seconds from the oldest host post; timer/workqueue dispatch then reaps and
  rechecks the actual command age before recovery. The separate 60-second
  synchronous budget remains available for cold shader setup, but it can no
  longer turn a command that is already about 15 seconds old into a 74.9-second
  async wait. Timeout recovery quarantines the still device-owned slots and
  fails affected contexts; it does not claim to cancel an operation already
  owned by host virgl.
- [x] The EEVDF timebase repair materially reduces renderer runnable delay and
  closes the strict two-sample windowed VPQ envelope without a priority hack.
- [x] The SDL live probe and GUIHD scheduler probe passed the pre-regression
  kernel. The corrected-SET_SCANOUT kernel also has a fresh full build, 209-file
  Sparse pass, clean SDL capture, repeated-hover pass, and a depth-2 stability
  A/B; the reproducible performance policy retains submit depth 3.
- [x] The host-only YouTube contract suite passes in the cleaned workspace.
  Its manifest digest matches the document-start producer hook, its V3 source
  check follows the live post-mode video rebind, and deleted July 10-11 build
  logs are optional historical replays rather than fresh-clone prerequisites.
  Mandatory embedded CRLF/CRCRLF, framing, mutation, 60-fps coalescing, and
  ordinary-30-fps rejection fixtures still run on every static check.
- [x] The intermittent VFS inode-cache warning is fixed. The investigation
  closed three ownership races: the dirty-sync destroying gap, the stale
  ref-count-one unmount evictor contract, and procfs/tmpfs eviction crossing a
  `vfs_iput()` destruction already in progress.
- [ ] Chromium's separate Pulse client-context restart failure remains a
  reliability follow-up, but it is downstream of restoring basic Wayland
  registry dispatch and visible compositor output.
- [x] The shared post-mode windowed boundary is complete. The xv6 N=2 mean is
  59.727 presented fps, 95.9% of the matched Linux N=2 mean of 62.285 fps and
  above the predeclared 56.06-fps floor. JavaScript
  `requestVideoFrameCallback` delivery is coalesced on xv6's renderer main
  thread (typically 50 ms), but the callback metadata presentation counter,
  media/wall clock, VPQ counters, KMS flips, and audio counters independently
  prove that this is not a 20-fps playback result.

“Reasonably close” does not mean weakening the workload. Accepted samples use
the same real YouTube URL, Chromium/Wayland/virgl path, 1280x720 `hd720` media,
AV1 video, Opus audio, 50% unmuted volume, 6 vCPU, 8 GiB, KVM, and 20 semantic
samples as the Linux control. Software rendering, null audio, local media,
maximized-as-fullscreen, or media-time-as-wall-time results are invalid.

## Reference and thresholds

The matched control is Ubuntu 24.04.4, Linux 6.8.0-134, Plasma 5.27.12/KWin
5.27.11 on Wayland, QEMU 9.0.2, KVM, virtio-vga-gl, SDL/OpenGL, and the NVIDIA
D3D12 virgl renderer. Durable details are in
`docs/linux-kde-plasma-reference.md`.

Current stock-APT behavioral boundary:

| Metric | Current result |
| --- | ---: |
| Native Wayland registry / KWin D-Bus | 3/3 PASS / 3/3 PASS |
| Konsole warm launches | 15/15 PASS |
| Konsole warm min / median / mean / max | 188 / 206 / 213.5 / 288 ms |
| Konsole captured runqueue wait | 0.441-1.226 ms |
| Guest idle CPU busy | 0.271% N=3 mean across 6 vCPU |
| glmark guest CPU busy | 5.505% untraced N=2 mean across 6 vCPU |
| glmark KWin CPU | 57.964% untraced N=2 mean of one core |
| glmark score | 99 / 97; 98.0 untraced mean |
| Current syscall trace | 10,161 calls; 77.48% poll+futex time |
| Screenshot / teardown | 3/3 1280x768; zero residual clients/QEMU |

Healthy Linux KWin also sleeps in `poll` on its main, D-Bus, libinput, and QML
threads and in `futex` on workers. The xv6 discriminator is therefore failure
to wake and finish a pending exchange, not the existence of blocking waits.
The historical glmark score 127 is retained as older evidence; 98 is the
current untraced stock-APT graphics reference. The trace-complete run's score
84 is excluded from that mean because its prior instrumentation increased
the downstream aggregate guest load.

Current shared-boundary fullscreen controls:

| Guest | Receipt | Presented fps | Media/wall | VPQ drops |
| --- | --- | ---: | ---: | ---: |
| xv6 1 | `xv6-fullscreen-20260716T035521Z-610088-direct-pulse` | 50.551 | 0.954956 | 19.928% |
| xv6 2 | `xv6-fullscreen-20260716T035737Z-612822-direct-pulse` | 51.347 | 0.930100 | 21.034% |
| Linux 1 | `linux-fullscreen-20260716T033032Z-591724` | 51.887 | 1.051494 | 34.522% |
| Linux 2 | `linux-fullscreen-20260716T033341Z-593897` | 49.732 | 1.025689 | 37.249% |

Current shared-boundary windowed xv6 controls:

| Receipt | Trace | Presented fps | Media/wall | VPQ drops |
| --- | --- | ---: | ---: | ---: |
| `xv6-windowed-20260718T223622Z-2490797-direct-pulse` | off | 59.618 | 0.999437 | 2.174% |
| `xv6-windowed-20260718T225053Z-2503729-direct-pulse` | epoll diagnostic | 59.835 | 1.000563 | 1.131% |

These rows are post-run revalidations of immutable receipts. Their original
`result.txt` files record the former callback-only cadence verdict; the current
validator accepts coalesced callback delivery only when the clean callback
stream carries at least 1.5 presented frames per callback and the independent
presentation count proves 50-75 fps against both media time and host wall
time. A synthetic ordinary 30-fps stream still fails.

Current acceptance contract:

- the strict nonce-bound validator must pass without accepting a rewind,
  detached element, counter regression, nonpositive cadence, or shortened
  observation;
- presentation must remain 50-75 fps against host monotonic wall time,
  media/wall must remain 0.90-1.10, and browser VPQ drops must not exceed 50%;
  direct 15-18.5-ms callbacks pass, while coalesced callbacks require the
  independent metadata presentation counter to prove the same frame rate;
- reasonable cross-OS fullscreen throughput requires the xv6 N>=2 presented
  fps mean to remain at least 90% of a refreshed Linux N>=2 mean under the
  identical shared boundary; the current result is 100.3%;
- exactly 20 display samples with the correct mode semantics;
- positive Pulse stream, hardware-played, and QEMU WAV byte deltas;
- virgl renderer, correct 1280x800 desktop, 1280x720 media, no freeze.

## Active APT QEMU migration

The target host frontend is the Ubuntu APT `/usr/bin/qemu-system-x86_64`, with
the repository-patched QEMU retained only as a rollback. The APT profile must
keep the already accepted performance contract: KVM, `-cpu host`, six vCPUs,
8 GiB, virgl on WSL D3D12, and the guest's asynchronous/phase-stable 60 Hz
present flags. Do not trade the black-window fix for synchronous GPU waits,
software rendering, fewer resources, or a non-KDE desktop.

Current Linux control:

- `linux-windowed-20260718T045117Z-1249241-apt-system` ran the system APT QEMU
  with `QEMU_MODULE_DIR` unset and passed the real YouTube validator at 62.834
  presented fps, 1.097489 media/wall, and 7.862% VPQ drops. Pulse stream,
  hardware-played, and QEMU WAV byte counts all advanced.
- Direct rendering was active through
  `virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`.
- KWin/Plasma and a native Wayland Chromium client rendered visibly. This
  proves stock APT SDL/OpenGL, WSLg, virgl, and the host adapter can carry the
  required workload together.
- The APT client initially fit 1280x768, drifted to 1356x897 after the Linux
  mode transition, then returned to 1280x768 when the native fit was re-applied.
  Geometry drift is real but independent of the xv6 black frame.
- The reusable prepared control is
  `build-x86_64/linux-kde-reference/linux-kde-behavior-20260714T024146Z.qcow2`;
  `qemu-img check` passes. The two latest successful YouTube receipts and three
  latest completed behavior iterations are retained.

Current xv6 observation:

- The visible "opening image" was xv6's kernel framebuffer logo. KWin's first
  dynamic `TEST_ONLY|ALLOW_MODESET` request failed because xv6 accepted only
  immutable MODE_ID 5; after that repair, real frame commits still failed when
  the nonexistent Bochs hardware cursor aborted the combined atomic update.
- Dynamic blobs are now accepted only when they exactly copy the active mode.
  Until atomic resize state is genuinely implemented, the connector advertises
  only that active mode and rejects nonadvertised blobs, avoiding
  successful-but-ignored modesets.
- Bochs exposes only its primary plane. The cursor plane is exposed only when
  virtio-gpu owns the visible scanout and its optional cursor queue completed
  initialization; every hidden-plane ioctl path rejects plane ID 7.
- GETCRTC, the CRTC `MODE_ID`/`ACTIVE` properties, and the primary plane's
  framebuffer now report one coherent state after legacy, direct-property, and
  atomic updates instead of allowing KWin to observe contradictory ownership.
- Ordinary unfenced KWin `0x201` commits now complete successfully. Focused
  traces measured roughly 4.0-5.8 ms per software present, within a 60 Hz
  frame budget, while fenced NONBLOCK remains fail-closed pending a real
  deferred worker.
- `build-x86_64/sdl-geometry-probe/live-x11-hidpi-off-20260718T205013Z-2308562`
  is the fresh-workspace proof on `/usr/bin/qemu-system-x86_64` with untouched
  system SDL/OpenGL modules. The guest mode and fitted SDL client are exactly
  1280x800; guest and host captures both show the alternating Qt Wayland
  sentinel, frame 1 advances, and neither capture matches the boot-logo
  detector.
- The same fresh image passes the full headless `drmiftest` runtime matrix.
  Its receipt records one advertised 1280x800 mode, hidden Bochs cursor,
  dynamic MODE_ID and active-mode coherence, ordinary unfenced NONBLOCK flip
  compatibility, per-commit events, fence lifetime, and final `drmiftest: ok`.

Remaining follow-ups, in order:

1. Treat the xv6 renderer-main notification latency as a separate scheduler /
   userspace wakeup investigation. It is not a graphics-throughput blocker:
   native blocking epoll, flattening nice weights, and changing the EEVDF
   sleeper floor did not improve it and are not retained. PCID also remains
   disabled because the kernel lacks a correct per-PCID INVPCID invalidation
   path and the prior global-flush experiment regressed KDE.
2. Implement a true deferred Bochs atomic worker only if matched profiling
   shows the current 4.0-5.8 ms synchronous compatibility path is limiting the
   workload. It must retain BO/owner refs and correlate in-fence, out-fence,
   and page-flip events; accepting a flag alone is not an async implementation.
3. Keep the direct-scanout/host-window visual content gate in the normal SDL
   reducer so a non-black boot logo can never satisfy the screenshot check.
4. Keep all cleanup exact to the owned PID/start-time/token. External VM PID
   churn is evidence to record, never permission to signal it.

## Completed SDL repair (patched rollback)

The oversized-window symptom belonged to QEMU 9.0.2's SDL GL frontend, not to
xv6 KMS. The host window could be 1908x987 while SDL stretched the 1280x800
guest surface as though it were the same shape, leaving only two or three
upper-left icons visible. The black GTK reference was a separate host frontend
failure (`GtkGLArea`/DMABUF); SDL/OpenGL is the accepted frontend.

The pinned QEMU patch now:

- aspect-fits the guest texture into the drawable;
- centers it with letterbox/pillarbox margins;
- clears uncovered pixels to black;
- maps absolute input back through the same viewport;
- reports `sdl_aspect_fix=patched` in the launch contract.

Accepted cold receipts:

- `build-x86_64/sdl-geometry/run-live-20260714T063538Z-484858`
- `build-x86_64/sdl-geometry/run-live-20260714T063705Z-486588`

Both retain guest 1280x800, SDL client 1908x987, centered viewport
164,0 1579x987, non-black output, correct pointer/button behavior, KVM, and
NVIDIA-backed virgl.

## Kernel fixes retained

The browser workload exposed real kernel defects. Keep the focused fixes and
their reducers; do not replace them with timeouts or priority hacks.

- EEVDF placement no longer mistakes the currently running entity (removed
  from its rb-tree) for an idle CPU.
- `ttwu_runnable` kicks the entity's real owner CPU rather than a placement
  suggestion that did not receive the task.
- IRQ-off spin and TLB-shootdown waits service only the required local
  TLB-flush IPI bits, breaking the observed six-vCPU deadlock without consuming
  unrelated IPIs.
- Full and page TLB invalidations use separate per-CPU atomic request tickets.
  Receivers publish monotonic acknowledgements with compare/exchange only
  after servicing the exact observed request, so an older handler can no
  longer acknowledge a newer shootdown. This closes the D-Bus #GP reproduced
  under concurrent VM teardown.
- GPU IRQ completion no longer re-enters the shared virtio operation lock.
- Async virtio-gpu admission tracks renderer submissions separately from KMS
  transfer/flush commands. A renderer waiting for depth now releases the
  global operation lock, and one scheduler-timer/workqueue watchdog covers the
  oldest posted async command at a time rather than inheriting the 60-second
  synchronous cold-start allowance. Recovery reaps once more and checks age
  under the serialized reaper before quarantining device-owned slots, so a
  completion race cannot sacrifice younger work. A monotonic abort generation
  also prevents a concurrently sleeping fence drain from reporting quarantine
  as successful completion.
- `wait`, `waitpid`, and `waitid` now rescan child state when their generic
  interruptible queue reports `-EINTR` without a deliverable signal. Harmless
  asynchronous scheduler wakes can no longer escape to userspace as a false
  wait failure; real pending signals still return `EINTR`. PID 1 retains its
  liveness retry as defense in depth.
- VM teardown clears and walks the maple tree once instead of performing a
  quadratic residual scan after each VMA.
- EEVDF and its PELT/balance policy windows use measured-timebase conversions
  instead of raw 1-GHz-assuming literals. The request slice matches Linux
  6.8's 750-us base with logarithmic CPU scaling (2.25 ms on six vCPUs).
- ext4 normal file reads use the validated direct-read route by default.
- AF_UNIX/SCM_RIGHTS/eventfd/epoll and audio SyncSocket boundary fixes retain
  their focused ABI reducers.
- `vfs_iput()` claims destruction before dropping its inode lock for dirty
  writeback. Generic unmount eviction now treats exactly zero references as
  idle instead of consuming a real owner's final reference, and the procfs and
  tmpfs evictors refuse to cross an inode destruction already in progress.

Key post-fix receipts include:

- scheduler GUIHD:
  `build-x86_64/scheduler-probe/guihd-20260716T021126Z-509286`
  (p99 wake-to-run 781 us, max 12.175 ms, no unfinished worker);
- wait/reap storm:
  `build-x86_64/waitreapstorm/20260716T022613Z-527413` and
  `build-x86_64/waitreapstorm/20260716T022632Z-528884` (N=2 after the TLB
  ticket repair). The original pre-fix run failed when a harmless wake became
  `waitpid == -1`;
- SDL live geometry/non-black/input:
  `build-x86_64/sdl-geometry-probe/live-x11-hidpi-off-20260716T022700Z-530434`;
- six-second ALSA sustain: 6.003 seconds elapsed for six seconds nominal,
  with real hardware progress and no pacing defect.

The starvation probe stayed silent in both latest fullscreen samples. There
is no evidence for another broad scheduler rewrite.

Two 2026-07-19 SDL hover reducer runs validate the new GPU admission policy,
and the final run validates the active watchdog, without a kernel fault,
watchdog allocation failure, async timeout, or leaked QEMU process. The final
warm hover-in samples are 200--240 ms; the first cold hover is still 442 ms, so
the remaining ordinary delay is in Plasma/QML animation and tooltip work
rather than a reason to queue multiple event-bearing KMS flips. Hover-out
samples are 134--264 ms. The click pixel probe changes in four of six samples
at 19--296 ms and times out twice; that probe is not a semantic launcher-open
assertion and its framebuffer readback drains GPU work, so it is retained as
diagnostic evidence rather than a pass claim.

The corresponding fresh windowed 720p YouTube trial reaches playback and
tears down cleanly with no GPU timeout, but fails the Linux-parity cadence
gate: 37.331 presented fps, 50.000 ms median callback interval, and 22.81%
near-60-Hz intervals. This separates the fixed 74.9-second hang exposure from
the still-open steady-state video-throughput problem; one failed trial does
not replace the accepted matched N=2 control above.

A second trial, before the active timer was added, reproduced the rare host
failure. The posted-age admission check quarantined the valid `SUBMIT_3D` at
7.747 seconds instead of 74.9 seconds and isolated context 6, but detection
still waited for the next queue user; playback consequently failed at 28.951
fps. That observation directly motivated the coalesced active watchdog. The
post-watchdog hover run proves normal timer rearm/disarm and teardown. A later
post-watchdog media trial then reproduced the backend failure naturally: the
watchdog quarantined the still-owned `SUBMIT_3D` at 5.000712 seconds and
isolated context 2, directly proving the active bound. That client could not
recover video replacement (`video-replacement-invalid-7`), which is expected
context-loss fallout rather than a 74.9-second desktop freeze. The host-owned
command cannot be cancelled safely, so both recovery receipts remain durable
evidence instead of being hidden.

The second post-watchdog media trial completes without a timeout but still
fails cadence at 30.462 presented fps and a 33.333-ms median. Its end snapshot
has 867 KMS presents, zero present-copy calls, 1.032 ms last host-present time,
and 104 renderer admission waits with an 18.666-ms maximum. Thus the remaining
video deficit is not another 74.9-second wait, framebuffer copying, or the
single event-bearing KMS flip contract; renderer/media scheduling and the
frequent present-clock late snaps remain the next throughput investigation.

## YouTube performance treatment

The honest default fullscreen baseline was:

`build-x86_64/youtube-parity/xv6-fullscreen-20260715T045314Z-1807165`

It produced 53.375 fps, media/wall 0.913536, and 9.237% drops. Its KMS path had
no framebuffer-copy fallback, but KWin received completion timing chained to
real present completion, so per-frame work accumulated onto the nominal
16.67 ms interval.

The retained treatment is paired:

1. `virtio_gpu_async_present=1` moves the fenced present out of KWin's page
   flip ioctl. The current candidate records the host-read fence on the
   scanout resource and synchronizes a later renderer reuse instead of
   blocking every KMS completion.
2. `virtio_gpu_present_clock_60hz=1` gives KWin a wall-clock-anchored 60 Hz
   completion sequence.
3. If a worker completes after its target edge, the clock now snaps to the
   latest elapsed edge. The former `elapsed + 1` calculation waited almost a
   second vblank and converted one miss into two.

The corrected late-edge fullscreen receipts are:

| Receipt | fps | media/wall | VPQ drops | Audio |
| --- | ---: | ---: | ---: | --- |
| `xv6-fullscreen-20260715T060152Z-1936318` | 57.693 | 0.968174 | 5.971% | real |
| `xv6-fullscreen-20260715T060532Z-1939746` | 55.243 | 0.953360 | 7.748% | real |

This is a genuine speed/pacing improvement and is reasonably close to Linux,
but neither receipt satisfies the strict two-sample fullscreen drop envelope.
The first misses by only 0.321 percentage points; the second contains a larger
renderer-long-task tail.

Direct scanout was already working in this historical fullscreen treatment.
The runs show zero framebuffer-copy ticks and about 1.4--1.7 ms average virtio
present time. In that 2026-07-15 experiment, depth 3 removed almost every
admission wait without improving presented output, while depth 8/32 caused
worse loss or startup regressions, so the then-current treatment retained
depth 2. The current SDL policy uses depth 3 with renderer admission accounted
independently from KMS commands, as documented above; the two configurations
are not equivalent.

One trial,
`xv6-fullscreen-20260715T055314Z-1928860`, hit a single 95.9-second host virgl
`SUBMIT_3D` stall and failed media start. Adjacent identical runs did not
reproduce it. Retain it as a host/virgl outlier receipt; do not mislabel it as
kernel corruption or hide it. The current five-second posted-age hangcheck
bounds the guest-visible wait and recovers affected contexts if this host
failure recurs.

## Direct-Pulse freeze repair and remaining startup tail

Earlier PipeWire audio-on measurements completed without black output or
oversizing, but missed the Linux frame-rate/media-clock envelope:

| Receipt | fps | media/wall | VPQ drops | Verdict |
| --- | ---: | ---: | ---: | --- |
| `xv6-windowed-20260715T063853Z-2009061` | 51.629 | 0.904193 | 12.242% | reject |
| `xv6-windowed-20260715T064438Z-2014192` | 53.142 | 0.940470 | 11.072% | reject |
| `xv6-windowed-20260715T064822Z-2019208` | 51.369 | 0.911117 | 10.264% | rejected experiment |
| `xv6-windowed-20260715T131304Z-22235` | 43.827 | 0.759408 | 21.093% | diagnostic reject |

The second run's renderer main thread used 8.891 CPU-seconds over 23.428 wall
seconds; it was not continuously CPU-bound. A completed-present fast path
reduced late present-clock snaps from about 42% to 27% in the third run, but
regressed presented fps by 1.77, so that speculative kernel change was removed.
The corrected elapsed-edge clock remains retained.

Low-overhead scheduler accounting now exposes cumulative `RunWaitTicks` and
`RunSlices` in `/proc/<pid>/status`, and the parity harness selects the actual
video renderer through its `dav1d-worker` threads. Two audio-dead intervals
then made the remaining boundary unambiguous:

| Receipt | Approx. fps | VPQ drops | Renderer CPU | Runnable delay |
| --- | ---: | ---: | ---: | ---: |
| `xv6-windowed-20260715T065521Z-2028531` | 59.96 | 2.10% | 3.081 s | 1.031 s |
| `xv6-windowed-20260715T070807Z-2059417` | 59.59 | 3.84% | 3.982 s | 2.798 s |

These are diagnostic-only because hardware audio bytes remained zero, but
they prove the same renderer/GPU workload can meet the Linux video envelope
when the current audio stack is absent. Long Task growth was only 0 and 71 ms.
The strongest current lever is therefore the audio-on integration cost and
its induced renderer/runqueue interference, not another KMS pacing rewrite.

The latest audio-on role snapshots make that attribution concrete. Over the
26.468-second wall interval, the renderer accumulated 9.237 CPU-seconds and
5.571 run-wait seconds; `pipewire-pulse` accumulated 2.087 CPU-seconds and
4.765 run-wait seconds; the PipeWire main thread accumulated 0.171 and 1.190
seconds respectively. Chromium's AudioThread used only 0.134 CPU-seconds and
waited 0.388 seconds. Hardware audio advanced for only about 23.16 seconds.
The late PipeWire/Pulse service boundary, rather than video decode capacity,
is therefore the current measured interference source.

WirePlumber is required in the current PipeWire configuration: two clean
`kde_wireplumber=0` attempts completed video but exposed no Pulse stream or
ALSA hardware progress, so that treatment was reverted. One preceding run
reproduced the historical WirePlumber #GP exactly (`rip=0x7fffff666364`, RBX
contains `lumber-0`), showing that the old kernel-VM TLB fix reduced but did
not close the residual corruption/crash family.

The direct-PulseAudio treatment is now isolated from KDE and D-Bus with a
Chromium-shaped 20-second stream reducer. That reducer found missing Linux
ALSA timer semantics rather than a Pulse configuration problem: xv6 had no
`/dev/snd/timer`, and its PCM rewind implementation moved only a logical
pointer without retracting submitted virtio-sound descriptors. The kernel now
implements the PCM period-timer ABI, truthfully reports zero unsupported
rewind/forward movement, suppresses duplicate PCM write notifications while a
period timer is active, and publishes timer events only when the negotiated
`avail_min` is actually writable. The native six-second ALSA pacing/recovery
phases continue to pass. Quantizing ALSA's reusable hardware pointer to the
actual virtio period-return boundary reduced tiny false-progress writes, but
the stronger overlapping-stream reducer proved that it did not by itself make
byte-level ring availability agree with descriptor-level capacity.

Two untraced owned-VM confirmations now pass the complete direct-Pulse gate:

- `build-x86_64/alsapcmrecover/20260715T141433Z-124260`: 20.008 seconds,
  962,240 frames, 3,858,304 hardware bytes, and 3,967 notify ticks;
- `build-x86_64/alsapcmrecover/20260715T141553Z-128401`: 20.004 seconds,
  964,288 frames, 3,881,088 hardware bytes, and 3,956 notify ticks.
- `build-x86_64/alsapcmrecover/20260715T143655Z-143192`: 20.001 seconds,
  958,656 frames, 3,856,640 hardware bytes, and 3,971 notify ticks, including
  the one-second idle-suspend policy used by the desktop treatment.

All completed every stream operation and teardown, reaped the identity-bound
PulseAudio process group, passed the native recovery/pacing phases, and left
zero x86 QEMU processes. Reducer credit alone is not performance parity.

The first complete, media-ready treatment receipt is
`build-x86_64/youtube-parity/xv6-windowed-20260715T143834Z-145386-direct-pulse`.
It reached 1280x720 `hd720`, completed all 20 semantic samples, and kept video
decode/presentation advancing, but the HTML media clock froze at 1.254 seconds.
Pulse had successfully resumed the real ALSA sink and created Chromium's
44.1-kHz stereo and mono streams; it then aborted at
`alsa-sink.c:try_recover()` because the first ALSA operation after recovery
returned `-EAGAIN`. AudioThread, Pulse, stream bytes, and hardware bytes all
stopped together, so this is the concrete freeze cause rather than a renderer
capacity inference.

The kernel-side mismatch was at the virtio reset boundary. ALSA pre-start
flushes are serialized and blocking, but `virtio_snd_prequeue()` reused the
nonblocking configure path. Retired descriptors from the prior stream could
therefore make reconfiguration export transient `-EAGAIN`, which PulseAudio
explicitly treats as impossible after a successful availability query. The
prequeue now waits through the RELEASE-to-PREPARE visibility window; a genuine
five-second transport stall becomes recoverable `-EIO` instead of Pulse's
fatal `-EAGAIN`. The focused reducer now requires an initially suspended sink,
starts real playback, force-suspends it with descriptors outstanding, resumes
it, and requires the server and hardware stream to remain live.

That stronger browser-shaped reducer exposed the final freeze mechanism. The
ALSA logical ring still had 260 bytes free, so Pulse validly issued a one-frame
(four-byte) write after rewind. The virtio-sound TX queue has 64 descriptors
and each request consumes three; tiny writes therefore exhausted all 21
request slots even though byte-based availability still promised space.
PulseAudio 16.1 then received `-EAGAIN` after a successful availability query
and aborted at `alsa-sink.c:try_recover()`.

The virtio-sound driver now assembles small writes into its existing 2048-byte
period buffer before consuming a descriptor chain. Full periods submit
immediately; partial periods receive a bounded-latency flush from the existing
5-ms OSS audio work item. Pending/free-byte accounting includes staged data,
start/drain/reset/configure paths preserve the same blocking contract, and the
hardware pointer reports actual played bytes rather than a synthetic period
quantum. This aligns byte-level ALSA readiness with descriptor-level transport
capacity instead of hiding the mismatch with retries.

Two fresh forced-recovery receipts pass with no Pulse assertion, no `EAGAIN`,
continuous hardware progress, exact process teardown, and zero remaining x86
QEMU processes:

| Receipt | Reducer elapsed | Frames | Hardware delta |
| --- | ---: | ---: | ---: |
| `20260716T004850Z-411756` | 20.003 s | 962,160 | 3,857,664 B |
| `20260716T004957Z-413956` | 20.005 s | 960,736 | 3,846,592 B |

The unchanged complete windowed treatment then passes twice:

| Receipt | fps | media/wall | VPQ drops | Pulse / hardware / WAV |
| --- | ---: | ---: | ---: | ---: |
| `xv6-windowed-20260716T005104Z-416659-direct-pulse` | 57.116 | 0.991368 | 10.526% | 8,650,752 / 4,631,812 / 4,116,480 B |
| `xv6-windowed-20260716T005306Z-419430-direct-pulse` | 55.746 | 0.992285 | 11.612% | 8,650,752 / 4,350,336 / 3,940,352 B |

Both use real `hd720` 1280x720 playback, all 20 semantic samples, real Pulse
stream bytes, hardware-played bytes, and QEMU WAV output. Their mean 56.431 fps
is 2.8% below the matched Linux windowed mean of 58.08 fps; media time is now
locked to wall time. The freeze and gross throughput defect are closed.

The remaining drop gap is concentrated in startup, not steady playback. Long
Tasks reached 4.399 s and 3.564 s by samples 3--4 and then stayed flat. Renderer
run-wait growth was about 10.90 s and 10.18 s over the full observations;
direct Pulse contributed only about 1.79 s and 1.61 s, materially below the old
PipeWire/Pulse role's 4.765 s. Later samples accumulate drops at a rate much
closer to Linux.

The hot `sched_starve_probe` diagnostic was made opt-in and tested once with
the same workload. The result
(`xv6-windowed-20260716T005701Z-425456-direct-pulse`) was 56.053 fps,
media/wall 0.994489, and 13.814% drops. Disabling the probe is good measurement
hygiene but did not improve the tail, so it is not evidence for a scheduler
policy rewrite. Do not add an xv6-only prewarm delay or weaken the matched
workload.

The matched Linux artifacts rule out Long Tasks alone as the distinguishing
cause: the two accepted Linux controls accumulated 2.229 s and 3.833 s, which
overlaps xv6's range. The sharper difference is callback cadence. Linux had
119--130 rVFC intervals in the 28--40 ms bucket, while the two xv6 passes had
234--237; xv6 still reports similar presented fps because one callback can
advance by two presented frames. Direct scanout remains intact with zero copy
ticks, so the next treatment targets the demonstrated scheduling cadence.

That audit found a real architecture-dependent EEVDF clock-unit defect. The
task slice and 1/10/32/50/100-ms idle-balance, PELT, EWMA, and periodic-balance
windows were raw literals tuned for an assumed 1-GHz clock, even though
`r_time()` uses the measured architecture timebase. On the measured 2.63-GHz
x86 VM the windows ran about 2.63 times too fast; on a 10-MHz RISC-V timer the
old nominal 10-ms slice became one second. The windows now convert through
`MS_TO_RAWTICKS()`, and task request size matches Linux 6.8's 750-us base slice
with logarithmic CPU-count scaling (2.25 ms at six vCPUs). This restores real
time semantics without a priority change.

The unchanged post-fix windowed workload passes twice:

| Receipt | fps | media/wall | VPQ drops | Pulse / hardware / WAV |
| --- | ---: | ---: | ---: | ---: |
| `xv6-windowed-20260716T011455Z-448575-direct-pulse` | 57.071 | 0.972628 | 7.007% | 7,340,032 / 3,979,712 / 3,657,728 B |
| `xv6-windowed-20260716T012136Z-457734-direct-pulse` | 57.385 | 0.981891 | 4.878% | 7,864,320 / 3,970,752 / 3,555,328 B |

Renderer runnable delay fell from 10.18--10.90 seconds in the pre-fix trials
to approximately 3.09 and 2.85 seconds, a 70--72% reduction. Long Tasks fell
to 1.361 and 1.420 seconds. The 28--40-ms rVFC buckets remain 261 and 226, but
the near-60-Hz buckets rise to 514 and 591 and both VPQ results are within the
matched Linux windowed envelope. This is measured support for retaining the
timebase repair as both a correctness and performance fix.

One identical repeat,
`xv6-windowed-20260716T011717Z-451442-direct-pulse`, hit a 94.969-second host
virgl `SUBMIT_3D` stall. Together with the earlier 92.832/95.9-second samples,
this is a distinct host/backend outlier family, not a reason to undo the guest
scheduler repair.

Fullscreen automation then exposed three harness defects without weakening
the workload. First, the durable evidence path exceeded Linux's 108-byte
AF_UNIX limit for the HMP socket; the ephemeral socket now lives at the short
build root. Second, `sendkey f` was focus-dependent and did not change
`document.fullscreenElement`. The harness now activates YouTube's actual
`.ytp-fullscreen-button` through CDP with `userGesture=true`, matching the
Linux reference. Third, current Chromium binds DevTools to guest loopback even
when passed a wildcard address, while QEMU hostfwd reaches the guest NIC. A
trial-private `socat` relay bridges NIC port 9223 to loopback port 9222; only a
PID-unique host-loopback port is exposed.

Two fresh actual-fullscreen receipts now complete all 20 semantic samples:

| Receipt | fps | media/wall | VPQ drops | Pulse / hardware / WAV |
| --- | ---: | ---: | ---: | ---: |
| `xv6-fullscreen-20260716T014757Z-485557-direct-pulse` | 55.618 | 0.946354 | 8.247% | 6,291,456 / 3,736,512 / 3,227,648 B |
| `xv6-fullscreen-20260716T014954Z-488664-direct-pulse` | 59.346 | 0.999069 | 4.783% | 6,815,744 / 3,864,768 / 3,604,480 B |

Every display row proves `document_fullscreen=1`, `player_fullscreen=1`,
active video, and 1280x720. Their 57.482-fps mean is effectively equal to the
57.382-fps Linux receipt mean; the mean drop rate is 6.515% versus Linux's
5.472%. Renderer runnable delay is approximately 2.45 and 2.35 seconds. The
first trial's 1.442-second startup Long Task tail explains its weaker drop and
media-clock result; the repeat has only 0.373 seconds of Long Tasks and beats
both Linux drop receipts. This closes the reasonable-performance objective,
while leaving the stricter every-trial tail gate open and explicit.

## Post-TLB fullscreen replacement and measurement repair

The post-TLB windowed trials proved that the kernel and browser could complete,
but fullscreen exposed a recoverable YouTube/Pulse transition that the old
probe treated as terminal. The evidence was deliberately retained:

- `xv6-fullscreen-20260716T025044Z-555582-direct-pulse` failed immediately as
  `video-detached-3` while the shell, hardware audio, and VM remained live.
  Chromium had replaced its Pulse output stream and media element.
- `xv6-fullscreen-20260716T030227Z-565647-direct-pulse` followed the replacement
  through all 20 rows, but the validator correctly rejected a 0.650 -> 0.007
  second media rewind. No performance credit was granted.
- `xv6-fullscreen-20260716T031212Z-574766-direct-pulse` advanced 18.950 media
  seconds, but the first callback from a mid-window replacement was mapped to
  a synthetic zero interval. The validator correctly rejected
  `cadence-nonpositive`.
- `xv6-fullscreen-20260716T031619Z-579451-direct-pulse` showed that awaiting
  `HTMLMediaElement.play()` during stream recovery could itself block the
  measurement harness until the start deadline.

The retained probe treatment is shared by Linux and xv6:

1. After the requested display mode is established, require one connected,
   active 1280x720 `hd720` element to advance for 16 observations before
   starting the unchanged 20-sample wall window.
2. Bound this setup phase by a real 30-second wall deadline. Playback requests
   are fire-and-observe; a browser `play()` promise can no longer freeze the
   probe.
3. If YouTube replaces an element after the boundary, accept it only with the
   same bounded source identity, quality, display mode, and non-regressing
   media time. The first rVFC callback is a generation anchor: it earns no
   callback, interval, or frame credit. Every counted interval must be a real
   within-element interval, and later duplicates still fail closed.
4. The guest-side controller now reports a nonce-bound semantic failure as soon
   as it appears instead of polling for 75 seconds after a known failure.

The first shared-boundary xv6 fullscreen receipts both pass the unchanged
strict validator, with zero rVFC regressions/duplicates, all 20 player-owned
fullscreen display rows, 1280x720 video, and positive real audio progress:

| Receipt | fps | media/wall | VPQ drops | Pulse / hardware / WAV |
| --- | ---: | ---: | ---: | ---: |
| `xv6-fullscreen-20260716T032338Z-585583-direct-pulse` | 50.162 | 0.952444 | 20.036% | 6,291,456 / 4,063,208 / 3,608,576 B |
| `xv6-fullscreen-20260716T032604Z-587885-direct-pulse` | 49.642 | 0.966052 | 20.807% | 6,815,744 / 4,468,992 / 3,661,824 B |

The same boundary was then measured on the matched Linux/KDE VM. Its preflight
was first repaired to accept the exact inventory helper's current two-line
zero contract; the gate remains strict and does not accept a nonzero QEMU
inventory. The refreshed Linux receipts are 51.887 and 49.732 fps. Their
50.810-fps mean versus xv6's 49.902-fps mean puts xv6 at 98.2% of Linux. Linux
also dropped more VPQ frames in both samples (34.522% and 37.249% versus
20.036% and 20.807%), so the remaining xv6 media/wall gap must not be described
as a decode-throughput collapse.

The YouTube-only Pulse idle policy now uses a five-second timeout. The focused
ALSA/Pulse recovery reducer intentionally retains its one-second timeout so it
continues to exercise suspend/reopen behavior. Two post-policy fullscreen
receipts pass at 50.551 and 51.347 fps, giving a 50.949-fps mean versus Linux's
50.810-fps mean (100.3%). A further post-VFS trial,
`xv6-fullscreen-20260716T040502Z-619551-direct-pulse`, passes at 58.283 fps,
media/wall 0.997822, and 7.385% drops without the old VFS warning.

The VFS warning itself exposed a chain of real ownership errors. First,
`vfs_iput()` did not claim destruction before its dirty-sync lock gap. After
that was fixed, a rich diagnostic resolved the remaining caller to generic
unmount eviction, whose obsolete ref-count-one rule could consume a live
owner's reference. Restricting that path to exactly zero references exposed a
final procfs race: `procfs_evict_pid()` could remove an inode while
`vfs_iput()` temporarily had its lock dropped but owned `destroying`.
Procfs/tmpfs eviction now checks `destroying` before and after locking. The
post-fix windowed receipt
`xv6-windowed-20260716T041038Z-630367-direct-pulse` reaches Chromium child
termination without any VFS warning.

That same receipt fails for a different, narrower reason at semantic sample
16. Pulse's sink never enters idle suspend, but Chromium logs
`MixableOutputStream: Error during independent playback`, three null
`pa_operation` failures, and `PipelineStatus::AUDIO_RENDERER_ERROR` while it
frees and recreates its Pulse streams. The nonce-bound probe correctly rejects
the replacement: it is connected and source-matched but has width/height zero,
ready state zero, media time zero after 25.240 seconds, and no quality proof.
The auxiliary `YT_MEDIA_VIDEO_REBIND_REJECT_V1` marker records those fields;
the validator remains strict and grants no performance credit. This proves the
remaining issue is a Chromium/Pulse client-context restart failure, not the
old one-second ALSA suspend policy and not a validator flake.

## Execution queue

1. Retain the fenced-present-reuse design and its corrected-APT-SDL 12-cycle
   receipt. Do not restore either unsafe immediate buffer reuse or the rejected
   6.4--8.0-second per-flip strict-retirement wait.
2. Treat the SDL oversize/black/input defect, D-Bus TLB race, virtio-snd
   descriptor freeze, EEVDF timebase defect, VFS destruction races, and
   reasonable N=2 fullscreen performance as closed. Retain every failed
   receipt above as regression evidence.
3. Reproduce the remaining Chromium/Pulse client-context restart failure with
   a focused stream-replacement reducer. Inspect client socket/context and
   operation lifetime around output recreation; do not weaken the media
   rebind marker or attribute this failure to idle suspend without evidence.
4. Refresh N>=2 windowed Linux and xv6 controls with the shared post-mode
   boundary before publishing a new windowed ratio.
5. Re-run the focused media-probe reducer in a Node-capable environment. Host
   Chromium currently proves both JavaScript files parse, and the live N=2
   trials prove the replacement path, but the local host has no Node runtime.
6. After any further kernel/ABI change, repeat the relevant focused reducer,
   build/Sparse checks, SDL/GUIHD checks, and at least N=2 matched media trials.
7. Recover the current windowed media cadence from the post-watchdog N=2 set
   (one context-loss recovery and one 30.462-fps reject) before publishing a
   new Linux ratio. Keep the five-second GPU hangcheck while tracing renderer
   runnable delay, present-clock late snaps, and host virgl submission
   latency; do not re-expand the async deadline to hide throughput.

## VM and artifact safety

- Builds, source inspection, and rootfs staging may coexist with the external
  VM.
- An owned x86 launch waits for every exact `qemu-system-*`/`qemu-kvm`
  executable to reach zero naturally, then repeats the all-QEMU zero check
  immediately before spawn.
- `scripts/launch/qemu-wait-natural-zero.sh` only waits and inventories. It
  never sends a signal, changes ports, or reconfigures a VM.
- Cleanup may signal only the exact owned PID/PGID after verifying PID,
  start-tick, executable, and run token. It must never act on an inventory row.
- Every run synchronously reaps its owned QEMU and proves exact x86 QEMU count
  zero. A later external RISC-V restart is expected and remains untouched.
- The YouTube harness now deletes its private 8+ GiB trial image after evidence
  extraction on both success and failure. Accepted base images remain
  immutable.
- The 2026-07-16 cleanup removed 207 abandoned per-run filesystem/QCOW/ISO
  images plus 18 stale QEMU pidfiles and three stale monitor sockets, reclaiming
  488,866,713,600 allocated bytes (about 456 GiB). It preserved
  `build-x86_64/fs.img`, both Linux KDE reference QCOW2 images, source, and
  diagnostic receipts/logs.
- The 2026-07-19 SDL follow-up removed the completed smoke VM's 7.5-GiB logical
  (3.8-GiB allocated) temporary filesystem plus two redundant 3-MiB PPM proof
  frames. It retains the current final capture, two compact hover/sweep receipts,
  the Linux control, and only three xv6 YouTube iterations (known-good,
  watchdog recovery, and latest A/B).

## Durable files

- Linux reference: `docs/linux-kde-plasma-reference.md`
- SDL QEMU patch: `patches/qemu-9.0.2-sdl-aspect-input.patch`
- SDL reducer: `scripts/gpu/sdl-geometry-probe.sh`
- YouTube xv6 runner: `scripts/gpu/chromium-youtube-parity-xv6.expect`
- YouTube validator: `scripts/gpu/chromium-youtube-parity-validate.tcl`
- Safe launcher: `scripts/launch/run-qemu.sh`
- External natural-zero gate: `scripts/launch/qemu-wait-natural-zero.sh`
- Exact QEMU inventory: `scripts/launch/qemu-exact-inventory.sh`

## Latest validation

- current corrected-SET_SCANOUT/unfenced-present candidate: full x86 kernel
  build PASS; kernel Sparse checks 209 files with zero failures/errors (known
  warnings only);
- corrected APT SDL repeated-hover gate: 12/12 hover-ins changed at 231.656 ms
  median (188.604--574.249 ms), 11/12 hover-outs changed at 163.116 ms median,
  and click changes have a 269.348 ms median. The host-final capture is clean
  after the repeated activity, with no vertical plus-sign trail. The run log
  has no watchdog, fence, context, panic, or page-fault error and the exact
  final inventory is zero QEMU;
- first post-reuse-fix windowed YouTube trial
  `xv6-windowed-20260719T015650Z-2638945-direct-pulse`: GPU/KMS and exact
  teardown are clean, 1280x720 presentation reaches 50.588 fps with 158/1112
  (14.21%) VPQ drops, but the validator correctly rejects the receipt because
  rVFC covers only 12.750 seconds while media advances 18.305 seconds.
  Chromium reports the already-open Pulse client restart error after the
  sample summary. This is a failed N=1, not a replacement for the accepted
  N=2 control; repeat once before attributing its lower cadence to the new
  present-reuse synchronization;
- the first attempted repeat
  `xv6-windowed-20260719T020027Z-2643185-direct-pulse` never reached the media
  workload: pid 54 (`dbus-daemon-host`) took a userspace #GP in
  `libdbus-1.so.3` with the invalid ASCII-derived pointer
  `0x3638782f73706163` (`"caps/x86"` in little-endian bytes). All three KWin
  startup attempts then failed because the system bus was gone. The owned VM
  still reaped cleanly and the exact final QEMU inventory was zero. Treat this
  as a separate guest VM/TLB startup instability, not a GPU cadence sample;
- the clean strict-reuse repeat
  `xv6-windowed-20260719T020359Z-2646907-direct-pulse` confirms that policy is
  too conservative: it finishes without a GPU timeout or visual corruption,
  but 720p presentation falls to 37.769 fps with a 50.000 ms median callback.
  Virgl's submit resource array is not a read/write access mask, so waiting on
  every referenced resource serializes ordinary renderer work behind nearly
  every scanout flush. Keep `virtio_gpu_present_reuse_wait=1` only as an A/B
  diagnostic;
- fenced-flush/pipelined-reuse SDL hover gate after disabling that diagnostic:
  desktop visible in 6.389 seconds; 12/12 hover-ins changed at 243.978 ms
  median and 12/12 hover-outs at 190.780 ms median. Seven click samples changed
  at 278.978 ms median (the alternating no-change click state still times out).
  The final host capture is clean after all transitions, the exact final QEMU
  inventory is zero, and there is no vertical plus-sign trail. This is the
  candidate for the next media measurement;
- first fenced-flush/pipelined-reuse windowed YouTube sample
  `xv6-windowed-20260719T021108Z-2658412-direct-pulse`: PASS at 59.526
  presented fps, 0.995421 media/wall, and 9/1147 (0.785%) VPQ drops. The media
  advances for the full 20.000 seconds, Pulse stream/hardware/WAV counters all
  advance, and exact owned teardown passes. This is 94.7% of the 62.834-fps
  Linux APT-QEMU control and restores the required >=90% cross-OS throughput;
- second fenced-flush sample
  `xv6-windowed-20260719T021305Z-2661107-direct-pulse`: FAIL after two
  `RESOURCE_FLUSH` (0x104) fences each stopped retiring and were quarantined by
  the watchdog at 5.001 seconds. The validator correctly rejects the resulting
  11.646 fps sample. This proves fenced scanout flushes cannot be the normal
  fix even though one sample reached Linux-like throughput;
- root presentation bug and unfenced correction: the page-flip path treated
  SET_SCANOUT as a permanent registration. After resources 4, 5, and 15 were
  seen once, a cycle back logged `already_bound=0 registered=1 rebind=0`; QEMU
  therefore remained bound to the wrong BO and the flush targeted a stale
  resource. SET_SCANOUT now runs for every actual resource switch, while the
  three-resource set remains diagnostics only. Fenced flush is opt-in through
  `virtio_gpu_fenced_scanout_flush=1` and strict reuse additionally requires
  `virtio_gpu_present_reuse_wait=1`;
- corrected-SET_SCANOUT/unfenced SDL hover gate: logs show `registered=1
  rebind=1 async_scanout=1` when each previously seen BO cycles back; desktop
  visibility is 6.304 seconds; 12/12 hover-ins change at 258.042 ms median and
  12/12 hover-outs at 183.631 ms median. The host-final capture is clean after
  the full sequence with no plus-sign trail, and exact teardown passes;
- first corrected-SET_SCANOUT media attempt
  `xv6-windowed-20260719T021952Z-2670295-direct-pulse` is not a cadence sample:
  the naturally recurring host virgl `SUBMIT_3D` hang occurred while Chromium
  replaced the video, and the media producer reports
  `video-replacement-invalid-18`. The watchdog quarantined context 2 at
  5.000495 seconds and owned teardown passed, so the desktop did not enter the
  former 74.9-second global freeze. Retry once for throughput evidence;
- second corrected-SET_SCANOUT media attempt
  `xv6-windowed-20260719T022229Z-2672790-direct-pulse` has no GPU timeout. Its
  presentation counter advances 429, 489, 550, and 612 over the first four
  one-second samples (about 60.6 fps with no drops), then Chromium reports
  `pa_operation is nullptr` and `AUDIO_RENDERER_ERROR` while YouTube replaces
  the media element; the validator correctly rejects it as
  `video-replacement-invalid-6`. The direct-Pulse harness had already suspended
  its ALSA sink after a five-second idle gap and resumed it for Chromium. Its
  idle timeout is now 120 seconds, beyond the complete setup and measurement
  window, so a normal quality/element transition cannot suspend ALSA under the
  strict cadence gate;
- the first 120-second-idle-timeout retry
  `xv6-windowed-20260719T022703Z-2677178-direct-pulse` never becomes a cadence
  sample: YouTube remains in player state 3 with `ready_state=1`, a 640x360
  medium stream, and unavailable quality setters through attempt 120. There is
  no GPU watchdog, quarantine, or audio renderer error; Pulse confirms the new
  timeout was loaded and owned teardown leaves zero QEMU. Treat this as a
  transient media/network readiness failure and retry once;
- the next retry
  `xv6-windowed-20260719T022948Z-2680006-direct-pulse` also remains in media
  readiness, but this time supplies a direct remaining-stall receipt: one
  valid KWin `SUBMIT_3D` (ctx 2, fence 1219) stops retiring and is quarantined
  at 5.000036 seconds. The old 74.9-second global freeze is still prevented,
  but losing the compositor context invalidates the media run. Since historical
  depth 3 removed admission waits without increasing presented fps, test the
  smaller Linux-like bounded overlap at submit depth 2 before retaining depth
  3 as the SDL default;
- submit-depth-2 A/B
  `xv6-windowed-20260719T023315Z-2683063-direct-pulse` also encounters the
  current transient YouTube player-state-3/readiness failure, so it is not a
  throughput sample. Unlike the adjacent depth-3 retry, it completes the full
  bounded readiness interval with no async GPU timeout or context quarantine.
  The exact local hover A/B then passes without a GPU timeout, but hover-in is
  279.492 ms median versus 258.042 ms at depth 3 (+8.3%), and hover-out is
  202.200 ms versus 183.631 ms (+10.1%); click response improves from 305.530
  to 279.828 ms. One timeout-free run with no valid media sample does not
  justify trading away the common hover path, so the reproducible SDL launcher
  retains depth 3 and the five-second posted-age watchdog while explicit depth
  2 remains the stability diagnostic;
- rejected strict-retirement experiment: it produced a visually clean GTK
  host capture, but hover changes were 6992, 6405, and 8029 ms. The harness
  resolved `gtk`, not `sdl`, so this is a useful failure/control and not SDL
  acceptance evidence;
- full x86 kernel build after the virtio-sound assembler, EEVDF timebase, and
  TLB-ticket repairs: PASS;
- kernel Sparse after those repairs: 209 files, zero failures/errors (known
  lock-context warnings remain warnings);
- wait/reap N=2, ALSA/Pulse recovery N=2, SDL live geometry/non-black/input,
  and GUIHD scheduler latency: PASS;
- post-TLB windowed direct-Pulse YouTube N=2: PASS without a VM freeze;
- final-policy actual-fullscreen xv6 N=2 and refreshed Linux/KDE N=2: PASS,
  with player-owned fullscreen, 20/20 semantic display samples, real audio,
  exact teardown, and xv6 at 100.3% of the Linux presented-fps mean;
- post-fix windowed VFS lifecycle: PASS through Chromium child exit with no
  inode-cache warning; the receipt separately and correctly rejects a real
  Pulse client-context/audio-renderer failure at sample 16;
- media probe and reducer parse in host Chromium with no syntax error; the
  expected runtime results are `library-missing` for the standalone probe and
  `require is not defined` for the Node reducer in a browser context;
- top-level and kernel `git diff --check`: PASS;
- post-watchdog SDL hover run: clean kernel/timer/teardown, warm hover-in
  200--240 ms, cold first hover 442 ms; the remaining cold QML/tooltip delay
  is open;
- posted-age 720p YouTube N=2 diagnostic: one cadence reject at 37.331 fps and
  one reproduced host `SUBMIT_3D` hang recovered at 7.747 seconds instead of
  74.9 seconds; the second playback result is a 28.951-fps reject. The active
  coalesced watchdog was added after that recovery sample;
- post-watchdog YouTube N=2: recovery receipt
  `xv6-windowed-20260719T011611Z-2590798-direct-pulse`: the naturally recurring
  host stall is detected at 5.000712 seconds, context 2 is isolated, and exact
  owned x86 teardown passes. Video replacement then fails explicitly rather
  than the desktop waiting 74.9 seconds. The second receipt
  `xv6-windowed-20260719T012037Z-2597067-direct-pulse` has no timeout and clean
  teardown, but is a cadence reject at 30.462 fps;
- KDE smoke harness SHA-256 after the intentional SDL-mode override and exact
  PID/start-time/token cleanup repair is
  `ec2b3b0575ab6c269a60a4b0fb2d6a98d2321e26802b279e4a36de01bb463da8`;
- final exact inventory after every owned run: zero x86 QEMU. Any external VM
  remains governed by the natural-zero gate and is never signalled or stopped.
