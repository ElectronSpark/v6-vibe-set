# Active goal: stable SDL KDE and Linux-comparable YouTube playback

Updated: 2026-07-16 UTC

## Outcome

Make the normal x86_64 SDL/virgl launch show the complete 1280x800 KDE
desktop without a black or oversized window, remain interactive under browser
load, and play real 1280x720 YouTube with audio at a level reasonably close to
the matched Linux KDE VM.

Current status:

- [x] SDL geometry, non-black output, and scaled absolute input are fixed.
- [x] The external VM coexistence contract is enforced without signalling the
  external VM.
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
  phase-stable 60 Hz present clock. Explicit `key=0` cmdline values remain
  available for A/B diagnosis.
- [x] The EEVDF timebase repair materially reduces renderer runnable delay and
  closes the strict two-sample windowed VPQ envelope without a priority hack.
- [x] The SDL live probe, GUIHD scheduler probe, kernel build, and Sparse pass
  after the final kernel changes.
- [x] The intermittent VFS inode-cache warning is fixed. The investigation
  closed three ownership races: the dirty-sync destroying gap, the stale
  ref-count-one unmount evictor contract, and procfs/tmpfs eviction crossing a
  `vfs_iput()` destruction already in progress.
- [ ] Chromium can still hit a separate Pulse client-context restart failure
  while YouTube replaces its audio stream. The final VM remains live and the
  VFS warning stays absent, but Chromium reports `pa_operation is nullptr`,
  `AUDIO_RENDERER_ERROR`, and replaces the video with an unready time-zero
  element. This is now the primary reliability issue.
- [ ] Re-run the shared post-mode boundary in windowed Linux/xv6 mode before
  publishing a new windowed cross-OS ratio; older windowed receipts remain
  useful historical evidence but are not byte-identical to the new boundary.

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

Current shared-boundary fullscreen controls:

| Guest | Receipt | Presented fps | Media/wall | VPQ drops |
| --- | --- | ---: | ---: | ---: |
| xv6 1 | `xv6-fullscreen-20260716T035521Z-610088-direct-pulse` | 50.551 | 0.954956 | 19.928% |
| xv6 2 | `xv6-fullscreen-20260716T035737Z-612822-direct-pulse` | 51.347 | 0.930100 | 21.034% |
| Linux 1 | `linux-fullscreen-20260716T033032Z-591724` | 51.887 | 1.051494 | 34.522% |
| Linux 2 | `linux-fullscreen-20260716T033341Z-593897` | 49.732 | 1.025689 | 37.249% |

Current acceptance contract:

- the strict nonce-bound validator must pass without accepting a rewind,
  detached element, counter regression, nonpositive cadence, or shortened
  observation;
- reasonable cross-OS fullscreen throughput requires the xv6 N>=2 presented
  fps mean to remain at least 90% of a refreshed Linux N>=2 mean under the
  identical shared boundary; the current result is 100.3%;
- exactly 20 display samples with the correct mode semantics;
- positive Pulse stream, hardware-played, and QEMU WAV byte deltas;
- virgl renderer, correct 1280x800 desktop, 1280x720 media, no freeze.

## Completed SDL repair

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

## YouTube performance treatment

The honest default fullscreen baseline was:

`build-x86_64/youtube-parity/xv6-fullscreen-20260715T045314Z-1807165`

It produced 53.375 fps, media/wall 0.913536, and 9.237% drops. Its KMS path had
no framebuffer-copy fallback, but KWin received completion timing chained to
real present completion, so per-frame work accumulated onto the nominal
16.67 ms interval.

The retained treatment is paired:

1. `virtio_gpu_async_present=1` moves the fenced present out of KWin's page
   flip ioctl while pinning the BO until host access completes.
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

Direct scanout is already working. The runs show zero framebuffer-copy ticks
and about 1.4--1.7 ms average virtio present time. Increasing async submit
depth is rejected: depth 3 removed almost every admission wait without
improving presented output, while depth 8/32 caused worse loss or startup
regressions. Keep depth 2.

One trial,
`xv6-fullscreen-20260715T055314Z-1928860`, hit a single 95.9-second host virgl
`SUBMIT_3D` stall and failed media start. Adjacent identical runs did not
reproduce it. Retain it as a host/virgl outlier receipt; do not mislabel it as
kernel corruption or hide it.

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

1. Treat the SDL oversize/black/input defect, D-Bus TLB race, virtio-snd
   descriptor freeze, EEVDF timebase defect, VFS destruction races, and
   reasonable N=2 fullscreen performance as closed. Retain every failed
   receipt above as regression evidence.
2. Reproduce the remaining Chromium/Pulse client-context restart failure with
   a focused stream-replacement reducer. Inspect client socket/context and
   operation lifetime around output recreation; do not weaken the media
   rebind marker or attribute this failure to idle suspend without evidence.
3. Refresh N>=2 windowed Linux and xv6 controls with the shared post-mode
   boundary before publishing a new windowed ratio.
4. Re-run the focused media-probe reducer in a Node-capable environment. Host
   Chromium currently proves both JavaScript files parse, and the live N=2
   trials prove the replacement path, but the local host has no Node runtime.
5. After any further kernel/ABI change, repeat the relevant focused reducer,
   build/Sparse checks, SDL/GUIHD checks, and at least N=2 matched media trials.

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
- protected KDE smoke file SHA-256 remains
  `706ba413c8687ba82dcbd38286477333362345592f385a3a3446ccfdd26b532a`;
- final exact inventory after every owned run: zero x86 QEMU. Any external VM
  remains governed by the natural-zero gate and is never signalled or stopped.
