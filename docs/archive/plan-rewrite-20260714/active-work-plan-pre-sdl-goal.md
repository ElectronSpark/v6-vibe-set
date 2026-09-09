# Active xv6 GUI performance plan

Last updated: 2026-07-14. This is the sole live performance plan. The former
6,588-line execution chronology is preserved byte-for-byte at
`docs/archive/plan-rewrite-20260714/active-work-plan-pre-linux-behavior.md`.
Historical receipts remain evidence, but they are not the current work queue.

## Outcome

Bring the xv6 KDE/Plasma guest toward the same-host Linux/KDE reference while
preserving correctness and idle efficiency.

The two user-visible acceptance tracks are:

1. Responsive desktop: warm Konsole readiness must improve from the archived
   1184-1205 ms range, first to <=500 ms median and ultimately toward the
   Linux 216 ms median. A valid result has one cold and at least five warm
   samples, a visible non-black desktop, the intended physical mode, virgl
   acceleration, and no residual Konsole or zombie processes.
2. Video: real Chromium 720p60 must sustain roughly 55-60 presented fps with
   low drops in separate N>=2 windowed and actual-fullscreen trials. Final
   acceptance requires the real Pulse/virtio-snd path; disabling audio is
   diagnostic only.

Neither a software renderer, maximized-window substitute for fullscreen,
N=1 timing win, diagnostic estimate, nor weakened semantic gate counts.

## Accepted Linux behavioral reference

The durable receipt is `docs/linux-kde-plasma-reference.md`; the accepted run
is `build-x86_64/linux-kde-reference/run-behavior-20260714T030916Z`.

| Contract | Linux result |
| --- | ---: |
| Guest | Ubuntu 24.04.4, Linux 6.8.0-134, Plasma 5.27 Wayland |
| VM / renderer | KVM, 6 vCPU, 8 GiB, virgl on NVIDIA D3D12 |
| Physical display | 1280x768 at 60 Hz |
| Warm Konsole | 210-232 ms; median 216 ms |
| First/warm main-thread runqueue wait | 0.522 / 2.231 ms |
| Idle guest busy | 0.185% across 6 vCPU |
| Graphics-load guest busy | 5.603% across 6 vCPU |
| Graphics-load KWin CPU | 55.818% of one core |
| Memory pressure | no increase; no swap |
| Warm Konsole storage reads | 0 bytes |

The Linux reference establishes behavior, not identical userspace work. It
shows that a healthy guest can devote one compositor thread to presentation
while most vCPUs stay idle, can keep ordinary runnable-to-run delay below a
few milliseconds, and can absorb many file probes and relocations without
making them the dominant launch cost.

## Current measured gaps

### Desktop responsiveness

- Archived xv6 warm Konsole readiness is 1184-1205 ms, approximately 5.5x the
  Linux median.
- Existing stage evidence assigns roughly 96% of each 58-139 ms frame-callback
  wait to KWin damage-to-repaint scheduling and about 4% to the repaint ioctl;
  client rendering itself is negligible in those samples.
- Prior generic timer, VFS, and loader changes did not reliably improve the
  end-to-end Konsole metric.
- Linux main-thread runqueue wait is only 0.5-2.2 ms. Therefore the evidence
  does not justify a broad process-scheduler rewrite.
- Linux records 1,217 failed file probes and 68,206 final relocations while
  reaching a 216 ms warm median. Failed path lookups and relocation count are
  facts to measure on xv6, not leading hypotheses by themselves.

### Video and presentation

- The sole accepted forced-HD720 windowed sample remains N=1 at 44.46 fps.
  It recorded 56.29 presents/s but a 27.53% VPQ drop share, plus 6,882/6,780
  async make-room stalls with a 401.524 ms maximum.
- The six-vCPU, depth-two change is promising diagnostic evidence, not formal
  acceptance: source-proven runs reached 58.24 and 58.99 presented frames per
  media second, reduced SUBMIT_3D stalls by about 95%, and cut maximum wait to
  about 11 ms. Formal windows failed for control/serial reasons, so N remains
  zero for that treatment.
- The residual lead is frame supply, GPU queue/retire latency, and present
  pacing. Codec capacity is not the current first suspect.

## Ranked implementation queue

Only one item is active at a time. Each item has a measurement prerequisite,
a bounded kernel change, and an A/B gate. If the prerequisite is null, close
the lane rather than implementing from intuition.

### P0: make xv6 stage timing decision-grade

Add low-overhead, monotonic, correlation-ID instrumentation for one complete
visible-frame path:

`client commit -> compositor damage -> repaint armed -> compositor runnable ->
compositor runs -> virtio submit -> host completion/retire -> frame callback`

Also record queue depth, present ID, timer deadline/actual fire time, CPU,
wake source, and whether the compositor was runnable, sleeping, or blocked on
a kernel object. Histograms must report N, median, p95, p99, and max; clocks
and IDs must permit adjacency checks without guessing across frames.

Gate: one idle interval, one Konsole launch batch, and one 720p60 diagnostic
must produce internally consistent stage totals with <=2% probe overhead and
no dropped/duplicate correlation IDs. Instrumentation alone is not a fix; it
must rank P1 versus P2 from the same session.

### P1: remove compositor wake/repaint multi-vblank delay

Enter this lane only if P0 again places material latency between damage,
repaint arming, timer expiry, compositor runnable, and compositor execution.

Inspect and change only the demonstrated mechanism: timer rounding, wakeup
delivery, wait-queue handoff, compositor-affinity contention, or a lock that
keeps the runnable compositor from executing. Preserve blocking event loops;
do not add polling, unconditional priority boosts, or global scheduler policy.

Gates:

- runnable-to-run p95 <=2 ms is the Linux reference; <=5 ms is the initial
  xv6 engineering gate;
- no unexplained 58-139 ms/multi-vblank repaint gaps in a valid N>=5 Konsole
  batch;
- warm Konsole median <=500 ms in the first milestone, with an improving p95;
- aggregate idle busy <=1% and no idle wake-rate regression above 10%;
- scheduler, futex/event-wait, fork/exit, and GUI smoke tests remain clean.

If runnable-to-run is already <=5 ms but the callback is late, close generic
scheduler work and follow the blocked stage into P2.

### P2: reduce virtio-gpu queue and host-retire serialization

Enter this lane when P0 attributes frame loss or callback delay to submit
admission, queue make-room, completion, or host retirement.

First separate four causes by present ID: upstream frame absent, submit blocked,
host completion late, and completion delivered but callback late. Measure
queue-depth occupancy, lock-hold time, sleepers/wakeups per completion,
batched completions, and retire latency. Then make the smallest demonstrated
change, favoring shorter critical sections, bounded queue depth, and batched
wakeups. Never report completion before the host has actually completed the
resource operation, and never turn a bounded queue into unbounded buffering.

Gates:

- async make-room p95 <8 ms and max <50 ms, with no 400 ms tail;
- no lost, duplicated, out-of-order, or prematurely completed fence/present;
- source-proven windowed 720p60 reaches >=52 fps at N>=2 first, then >=55 fps
  with VPQ drops <=5%;
- guest remains mostly idle outside the compositor/render threads and shows
  no sustained block-I/O or memory pressure;
- repeat renderer, fence, DRM/virtio routing, and GUI/video regression suites.

### P3: distinguish frame-supply loss from presentation loss

If P2 host-retire latency is within gate but VPQ/drop remains high, correlate
Chromium rVFC, decoded-frame supply, compositor commit, and present IDs. A
flat media clock or missing decoded frame is upstream; a supplied frame that
never commits is userspace/compositor; a committed frame that retires late is
P2. Do not tune the codec, browser process model, or networking until this
classification is retained from one source-proven session.

Gate: every dropped interval receives exactly one cause class, and the class
totals reconcile with VPQ/rVFC/present counters. Implement only the dominant
class, then repeat windowed N>=2.

### P4: memory faults and page-cache effectiveness, conditional

Linux warm Konsole performs zero storage reads and the six-launch observation
has only 34 major faults. On xv6, split file-fault counts into cache hits,
minor page installs, actual block reads, readahead hits, and wait time. Large
raw fault counts do not authorize an MM rewrite.

Enter only if warm launches show material block reads or fault wait. Then test
a bounded read-ahead/fault-clustering or page-cache change. Gate on warm major
faults near zero, no block-I/O after cache warmup, at least 10% launch-latency
improvement at N>=5, and VM/COW/file-integrity regressions passing. Otherwise
close P4.

### P5: loader and VFS, conditional and last

Linux's failed file probes consume only about 2.70% of accumulated traced
syscall time, and its large relocation volume still fits the fast reference.
Reopen VFS/path caching or loader lookup only if a same-session xv6 profile
assigns >=10% of warm-launch wall time to that exact stage and identifies a
specific repeated miss or algorithm. Require a >=10% N>=5 end-to-end win;
microbenchmark-only improvement is insufficient.

## Audio, fullscreen, and final acceptance

After windowed video reaches >=52 fps at N>=2:

1. Repair and validate the real Chromium Pulse/virtio-snd stream. Require a
   non-null stream, advancing counters, audible/output evidence, and N>=2
   performance samples with audio enabled.
2. Reach >=55 fps with <=5% drops in windowed mode.
3. Repeat N>=2 in nonce-proven actual player/document fullscreen at stable
   1280x720 playback. A maximized window never substitutes.
4. Run default scheduler, futex/event-wait, VM/COW, filesystem, DRM/virtio,
   KDE smoke, responsiveness, and video batteries.

The Linux glmark2 score is context only because xv6 does not yet run the same
benchmark. It is not an xv6 pass/fail threshold.

## Experiment discipline

- Change one causal mechanism per A/B. Pin commit, kernel/user/rootfs hashes,
  vCPU/RAM, renderer, display mode, media asset, browser flags, and sample
  validity rules. Do not pool incompatible runs.
- Report median and tail latency, CPU basis, queue/fault counts, and validity
  failures. A diagnostic estimate stays labeled diagnostic.
- Every build, serial command, and owned VM is synchronously reaped. Serial
  silence is not a verdict; reacquire a fresh prompt after short marker
  commands. Never use `pgrep`.
- Repository searches use only `/home/es/.local/bin/rg`, source trees only.
  Artifact searches name each regular file through the guarded artifact
  wrapper. Never recursively scan images or generated trees.

### External-VM coexistence and cleanup boundary

An independently owned VM may remain running. Treat its PID, executable,
start time, full command line, ports, and process group as immutable external
state. Record it before and after an experiment and check that the owned xv6
VM's ports and writable images do not conflict.

Launch each xv6 QEMU in a new, recorded process group with a unique run token.
Cleanup may signal only that exact negative PGID after revalidating both the
leader executable and the unique token in its command line. Never use a name
match, broad process scan, `killall`, or an all-QEMU cleanup. If ownership
cannot be proved, leave the process untouched and stop. Final validation is
`owned_qemu_remaining=0`; external VMs may still be present and must be
reported, never terminated.

Only one repository-owned performance VM may run at a time. Copy-on-write
derivatives are mandatory; accepted Linux and xv6 base images remain
read-only references.

## Closed lanes unless new evidence reopens them

- Broad scheduler-policy or priority changes: Linux runqueue wait is only
  0.5-2.2 ms; xv6 needs compositor-stage evidence first.
- Generic timer-frequency changes: earlier A/B did not improve Konsole; only
  a newly measured timer/repaint stage may justify a narrow change.
- Broad VFS or dynamic-loader optimization: Linux tolerates comparable lookup
  failures and much larger relocation volume; prior xv6 changes did not move
  the user-visible metric reliably.
- Codec, DNS, launcher packaging, and software-renderer work: none explains
  the current source-proven present/drop/retire evidence.
- More serial/evidence plumbing after P0 is decision-grade: transport work is
  an enabler, not a performance milestone.

## Evidence map

- Linux reference and black-window correction:
  `docs/linux-kde-plasma-reference.md`
- Linux guest behavioral probe:
  `scripts/gpu/linux-kde-behavior-probe.sh`
- Linux DRM/GUI ABI compatibility background:
  `docs/linux-drm-abi-compat-plan.md`
- Pre-rewrite execution chronology:
  `docs/archive/plan-rewrite-20260714/active-work-plan-pre-linux-behavior.md`
- Accepted Linux behavior run (ignored build artifact):
  `build-x86_64/linux-kde-reference/run-behavior-20260714T030916Z`

The next action is P0: obtain one valid xv6 same-session stage trace, use it
to choose P1 or P2, and implement only the winning measured mechanism.
