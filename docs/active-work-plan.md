# Consolidated work plan

Consolidated: 2026-09-07. Latest desktop evidence in the source plans: 2026-07-19.
The consolidation itself added no build or runtime validation. A subsequent
[graphical audit on 2026-09-07](gui-progress-audit-20260907.md) exercised the
existing image; its narrower findings are recorded separately below.

This file is the single active plan for the umbrella repository and its
first-party kernel, user, and ports work. Update the relevant workstream here
when implementation changes its status. The [archive index](archive/plan-consolidation-20260907/README.md)
maps all 15 original plans to unchanged snapshots, including their full
checklists, experiments, commands, and receipts. Generated audits, reviewed TSV
inventories, reference documents, and reusable skills remain separate references.

## Latest graphical audit — 2026-09-07

The [virgl fence investigation](virgl-fence-publication-fix-20260907.md)
fixes a demonstrated publication-order defect: a command could be classified
as complete before it entered the GPU queue. The deterministic baseline
returned A=169 after B=170 had completed; the fixed kernel leaves A unnumbered
until publication, then returns A=203 after B=202. The regression exits zero
on the fix, and the focused kernel build and sparse checks pass.
In the accelerated Chromium mode, mouse-triggered WebGL 1 and 2 loss/restoration
rebuilds resources and passes pixel checks. The [fixed-kernel GUI audit](chromium-gl-recovery-audit-20260907.md)
still pauses during ordinary animation, including after a separate host-focus
intervention and a clean browser restart with a fresh profile. Final captured
GPU timeouts, failures and failed contexts remain zero, with all 3,106 submitted
commands retired. The [earlier lifecycle baseline](chromium-gl-lifecycle-audit-20260907.md)
also paused. This did **not** close **DESK-02** or establish host-hang recovery;
the callback pauses are investigated below, and synchronous-timeout DMA
lifetime remains open.
The [subsequent frame-callback audit](chromium-frame-callback-audit-20260907.md)
keeps animation running after the five-second threshold and observes natural
resumption. Its retained 60-second repeat has four 6.9–7.5-second callback-entry
gaps, each matching a delayed Wayland frame callback and previous-buffer release.
JavaScript timers continue, focus remains active, and sampled GPU/DRM queues
drain without errors. Both observations still fail the animation progress bound.
A subsequent memory-only first-gap capture found both Wayland socket directions
empty and KWin blocked, uninterruptible, in `DRM_IOCTL_VIRTGPU_EXECBUFFER`.
The debugger interrupted that trial for 1.850 seconds, so its resumption has no
natural-recovery credit. An expanded capture finds Chromium's GPU process
holding the resource-operation mutex and waiting for asynchronous progress,
with all 60 async slots free and posted work fully retired; KWin waits for that
same mutex. The [progress-wait registration repair](virgl-progress-wait-fix-20260907.md)
preserves the caller's retirement sequence across predicate checks and waiter
registration. The deterministic baseline fails four race cases; the candidate
passes all ten cases, kernel build and Sparse. Two fresh virgl boots pass the
unchanged 60-second WebGL observation, with zero stalls/errors and maximum
callback-entry gaps of 229 and 212 ms. Both retained traces have no callback
receipt wait above one second. Mouse context restoration, fullscreen, canvas
resize and the GUIHD scheduler workload also pass. Two traced local-video
trials play without an HTML video error but fail the unchanged dropped-frame gate
(15.48% and 14.70%, versus less than 10%). A separate untraced YouTube trial
advances to 3:23 with mouse pause/resume/fullscreen and 233.4 seconds of captured
non-silent PCM; direct browser and owned VM exits are clean. All three candidate
VMs are reaped and exact QEMU inventory is zero. Kernel fix commit: `81cbd0c5`.
This repairs a guest registration race without establishing actual host-hang
recovery or closing media performance and synchronous-timeout lifetime work.
The normal Chromium launcher policy is unchanged.

The later [audio fix and verification](chromium-audio-fix-20260907.md) resolves
the September YouTube audio-renderer failure below. Ordinary AF_UNIX stream
writes now publish bytes and credentials under one sender lock before waking
readers, including partial writes. The original kernel failed the new
credential regression; the rebuilt kernel passed 4,096 rounds, six Chromium
IPC checks, a 9.7 MB stream transfer and two 20-second ordinary Unix Pulse
streams with clean teardown. In the same SDL/virgl KDE boot, opt-in multiprocess
Chromium played YouTube through 5:28 with working mouse pause/resume, mute,
seek and fullscreen. Stats reported 1280x720@30 AV1/Opus, and an isolated
321.2-second recording contains non-silent media PCM, excluding test tones.
Chromium and QEMU exited cleanly; exact QEMU inventory is zero. This closes
the observed September audio failure, with no matched performance, 60 fps,
hardware-decode or host-speaker listening claim. **DESK-01** retains its
separate historical stream-replacement scope; **DESK-03** remains open.

The earlier [YouTube watch-page audit](chromium-youtube-audit-20260907.md)
uses the existing image and the accelerated multiprocess Chromium mode below.
The Big Buck Bunny watch page, description, comments and recommendations load,
but playback reports **"Audio renderer error. Please restart your computer."**
at 0:00; reloading returns the same error. This adds no successful playback,
audio, hardware-decode or performance credit. Host wheel scrolling works, but
host left-click delivery is uncertain. The QMP virtual mouse produces visible
mute/volume changes, opens quality and playback-speed menus, selects 1.25x and
enters player fullscreen. The intended 720p60 selection still leaves a 360p
label, so successful resolution switching is not established.
Player fullscreen exit, Stats for nerds and the comment-sort menu also work.
Stats reports current resolution 0x0, zero buffer health and AVC/Opus selections;
it supplies no decoded-frame, AV1-playback or 720p60 credit. The final QEMU WAV
contains 25.28 seconds of PCM payload, all zero; it proves no audible content.
The same-run GPU page still reports OpenGL enabled. Chromium exits with status
zero, and the owned VM is synchronously reaped with its overlay removed and
an independently verified exact zero-QEMU inventory.
Keep these input routes separate and do not classify the earlier unchanged
clicks as established YouTube widget failures. See the audit for final control
coverage, fresh diagnostics and cleanup.

The focused [Chromium OpenGL audit](chromium-opengl-audit-20260907.md)
distinguishes the normal launcher from its existing opt-in mode. Normal
Chromium reports OpenGL/WebGL disabled and returns null for both WebGL 1 and
WebGL 2 contexts, including a mouse-triggered retry. A separate diagnostic
boot of the same existing kernel/image, with
`WAYLAND_CHROMIUM_MULTIPROCESS=1`, the actual KDE session environment and a
fresh browser profile, reports OpenGL enabled and hardware-accelerated WebGL.
Both WebGL versions drew visible green triangles, changed to blue after a
host-mouse click, and passed center/corner pixel readback with zero GL errors
or context loss. The fresh renderer is
`ANGLE (Mesa, virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)), OpenGL ES 3.1)`.
This proves basic accelerated Chromium/WebGL operation in that opt-in mode;
the normal desktop policy is unchanged. It does not close long-run recovery,
WebGL conformance, WebGPU, hardware-video-decode or media-performance gates.
The diagnostic browser closed with a confirmed exit status of zero. Both VMs
were synchronously reaped, their temporary overlays removed, and exact QEMU
inventory returned zero.

The earlier [mouse exploration audit](gui-mouse-audit-20260907.md) adds
74 mouse actions and 70 host captures across KDE, Dolphin, KWrite and Chromium.
Folder view modes, sorting, scrolling, split panes, properties, window controls,
editor menu options, browser settings/tab layout and calendar views worked.
**Clicking New Tab `+` was followed by the browser window disappearing twice**,
in horizontal and vertical layouts; the first relaunch showed an
abnormal-shutdown prompt. Process exit was not independently confirmed for
the second disappearance. This is a current
**GUI-01** failure with no established root cause. Some hover-only transitions
did not occur; retain the mixed evidence under **DESK-04**. This second VM was
also reaped, its overlay removed, and exact zero QEMU processes confirmed.

One approximately 15-minute SDL/virgl KDE session passed graphical menu/window
operations, Chromium local HTTP render/type/click and ordinary HTTPS, and KWrite
editing plus same-session save/reopen through Dolphin with an explicit application
choice. Fresh boot logs confirm virgl capsets, the render node and active 3D
scanout/presentation. The VM was synchronously reaped and its private overlay
removed, with exact zero QEMU processes afterward.

Konsole opened without a usable shell prompt. Default Chromium used its launcher's
software/single-process flags, and no default text-file application was selected.
Follow-ups are **DESK-08** and **GUI-01**. Host captures were successful, but QMP
guest captures returned `no surface`; one-shot fitting did not establish the
exact host-client-size gate. The displayed renderer log is append-only, so its
entry is not proven fresh. **DESK-04** remains open. This existing-image run adds
no fresh-build, YouTube, audio, performance-parity or reboot-persistence credit.
See the [audit report and screenshots](gui-progress-audit-20260907.md) for evidence.

## Status and precedence

The immediate outcome is a complete, responsive 1280x800 KDE desktop on the
normal x86_64 SDL/virgl path, with real 1280x720 YouTube playback and audio
reasonably close to the matched Linux KDE control. The broader outcome is
Linux ABI compatibility with upstream applications, supported by reproducible
builds, correct VM/VFS behavior, and measured networking performance.

Recorded completion below applies to the stated workload and date. An old
unchecked box is not evidence that a later implemented feature is missing;
a successful earlier receipt does not validate a later changed kernel.
Resolve future conflicts using the current source and fresh, comparable
receipts. For this consolidation:

- July KDE/SDL/virgl work supersedes June Weston/GTK launch policy and the
  older custom `wlcomp` desktop roadmap. Preserve those controls as history;
  do not restore an old compositor or frontend from an archived checklist.
- Stock Ubuntu APT QEMU is the July migration target and has visual proof;
  the repository-patched QEMU is the known-good rollback. The normal launcher
  must be checked before claiming that this migration is its default.
- Host compiler, host glibc, Linux headers, and `/lib64/ld-linux-x86-64.so.2`
  supersede old custom-musl migration tasks for x86_64.
- The checked-in generated syscall audit supersedes conflicting prose counts:
  302 `native-ok`, 71 `struct-risk`, and zero `native-missing`,
  `wrong-dispatch`, `unsupported-ok`, or `unsupported-bad`. The semantic audit
  records 193 `probed-compatible-core` and 180 `compatible-by-inspection`;
  neither classification establishes full Linux conformance.
- June DRM phases 0–6 are recorded complete for their supported scope. Later
  application failures expose narrower semantics; they do not reopen every
  historical implementation step.
- Older `clone3`/`rseq` ENOSYS-only instructions conflict with later implemented
  subsets and runtime observations. Validate current semantics and reject
  unsupported combinations accurately; do not reinstate old fallback policy.
- Hyper-V offscreen/compute proof is separate from display proof. The June GPU
  assessment concludes that native presentation on plain GPU-P has no usable
  host ABI under the no-custom-host-tool constraint. Keep that work out of the
  active implementation queue unless the platform contract changes.

## Execution order

1. **Desktop reliability and current performance:** close reproducible
   Chromium/Pulse stream-replacement failures and recurring host virgl stalls;
   then obtain fresh matched N>=2 windowed/fullscreen receipts on the retained
   kernel and launch policy. Preserve the five-second async watchdog.
2. **Linux ABI and VM:** reduce concrete application failures, especially
   process/IPC/event lifetime and VM lock/fault latency, before extending
   compatibility or changing scheduler policy.
3. **Graphics and browser coverage:** preserve standard DRM behavior, broaden
   ordinary launch/input/HTTPS/lifecycle coverage, and distinguish software
   decode, hardware decode, presentation, and audio evidence.
4. **VFS and networking:** audit applicability of the older subsystem plans,
   then implement and benchmark one justified slice at a time.
5. **Upstream source cleanup and port provenance:** use the existing inventory
   tools and reviewed data, assign each remaining adaptation to its owner,
   and retire it in dependency order without losing runtime coverage.

These are sequencing priorities, not instructions to run every lane in parallel.
Only one worker may own a VM; every other lane is explicitly no-boot.

## Desktop and YouTube

Sources: [July active plan](archive/plan-consolidation-20260907/docs/active-work-plan.md),
[Linux KDE reference](linux-kde-plasma-reference.md).

### Recorded baseline

- [x] SDL geometry, fitted absolute input, visible Qt Wayland sentinel in guest
  scanout and host window, and rejection of the boot logo as desktop proof.
  Stock APT SDL/OpenGL has a fresh-workspace 1280x800 proof from July 18.
- [x] Reproducible KDE-only rootfs, read-only `e2fsck`, Xwayland/XKB staging,
  versioned ATK for GTK, and Chromium's matching private ATK runtime.
- [x] Active-mode dynamic MODE_ID blobs, coherent CRTC/plane state, ordinary
  unfenced NONBLOCK flips, and hiding the nonexistent Bochs hardware cursor.
  Bochs unfenced compatibility remains synchronous; it is not deferred KMS.
- [x] EEVDF timebase/placement/wakeup corrections, TLB shootdown tickets,
  wait-family harmless-wakeup rescan, linear VM teardown, and the VFS
  destruction/unmount/procfs ownership repairs have focused evidence.
- [x] Pulse -> ALSA -> virtio-snd -> QEMU WAV playback and N=2 forced recovery.
  Period assembly fixes descriptor exhaustion on small valid writes;
  `/dev/snd/timer`, truthful rewind limits, and availability notifications are
  retained. Later capture/mmap/mixer coverage is a separate task.
- [x] Async renderer admission is separate from KMS occupancy and drops the
  global GPU operation lock while waiting for capacity. One event-bearing
  KMS flip may be pending. Default submit depth remains 3; depth 2 is a
  diagnostic control. The coalesced posted-age watchdog quarantines affected
  contexts after about five seconds without claiming to cancel host work.
- [x] The retained SDL/virgl profile uses KVM, `-cpu host`, six vCPUs, 8 GiB,
  `virtio_gpu_async_present=1` and `virtio_gpu_present_clock_60hz=1`.
  Asynchronous KMS and the corrected phase-stable 60 Hz clock remain part of
  the APT migration contract, alongside renderer admission and real audio.
- [x] Every actual scanout-resource switch now issues SET_SCANOUT. Repeated
  hover no longer accumulates plus-sign trails. Fenced flush and broad reuse
  waits remain opt-in diagnostics because they stalled or reduced throughput.
- [x] Monotonic evdev timestamps, duplicate-motion removal, safe popup
  dismissal, desktop warm-up and 50 ms tooltip policy reduce warm Kickoff
  opens to 320–365 ms in the final recorded run; cold open was 2143 ms.

Accepted earlier controls remain useful regression evidence:

| Workload / date | xv6 result | Matched Linux result | Meaning |
| --- | --- | --- | --- |
| Fullscreen, July 16, N=2 | 50.551 / 51.347 presented fps | 51.887 / 49.732 fps | 100.3% of Linux mean, real audio |
| Windowed, July 18, N=2 | 59.618 / 59.835 fps, 2.174% / 1.131% VPQ drops | 62.285 fps mean | 95.9% of Linux mean; above 56.06 fps floor |
| Stock-APT Linux behavior, July 18 | Control only | Registry/D-Bus 3/3, warm Konsole median 206 ms, idle CPU 0.271%, untraced glmark mean 98 | Behavioral comparison, not xv6 acceptance |
| Corrected scanout, July 19 | Hover-in/out medians 258.042 / 183.631 ms, clean capture | No matched new media pair | Visual repair proven; current media parity still open |

The July 18 windowed rows are revalidations of immutable receipts using the
presentation-counter-aware validator. Their original `result.txt` records the
older callback-only verdict. The July 19 corrected-scanout media attempts
include context loss, Pulse restart errors and media-readiness failures;
there is no replacement accepted N=2 pair for that final candidate.

### Open work

- [ ] **DESK-01:** Build a focused Chromium-shaped Pulse client-context /
  stream-replacement reducer for null `pa_operation` and
  `AUDIO_RENDERER_ERROR`. The July 19 harness uses a 120-second idle timeout;
  the older five-second policy is superseded, and failures also occurred
  without suspend. Keep the one-second recovery reducer intentionally strict.
  The September 7 [YouTube audit](chromium-youtube-audit-20260907.md) reproduces
  the visible audio-renderer error in the ordinary KDE audio environment with
  opt-in multiprocess Chromium. Localize that fresh failure separately from
  the historical dedicated-Pulse treatment; the error text alone does not
  establish the same underlying cause.
  The later [September audio fix](chromium-audio-fix-20260907.md) closes that
  fresh ordinary-Unix failure with a kernel credential-publication repair,
  a Linux/xv6 regression, two passing Pulse streams and graphical YouTube
  playback with captured media audio. Reuse the existing Chromium-shaped
  reducer for the historical stream-replacement case; do not reopen the
  fixed ordinary-write failure without a new reproduction.
- [ ] **DESK-02:** Investigate recurring valid host `SUBMIT_3D` stalls and
  context recovery. Preserve posted-age accounting, quarantined ownership,
  abort-generation semantics, exact teardown, and failed receipts. Do not
  lengthen the deadline or enable per-flip host waits to disguise stalls.
  The September fence publication-order defect has a passing deterministic
  regression. The remaining ordinary Chromium animation pauses now correlate
  with delayed Wayland callback/release receipt. An expanded wait capture finds
  Chromium GPU resource cleanup holding the operation mutex while waiting for
  already-retired work, blocking KWin EXECBUFFER. The
  [registration fix](virgl-progress-wait-fix-20260907.md) has a failing baseline,
  ten passing host cases, kernel build/Sparse, two fresh passing 60-second GL
  observations, mouse context restoration and GUIHD coverage. Actual host-hang
  recovery and synchronous-timeout DMA lifetime remain separate open work.
- [ ] **DESK-03:** Restore a complete current windowed measurement and refresh
  N>=2 Linux/xv6 windowed and fullscreen controls before publishing a new
  ratio. Separate media/network readiness from cadence and audio failures.
  September's two traced local-video trials on the progress-wait candidate
  fail the unchanged drop gate at 15.48% and 14.70%, despite near-real-time
  media advance and no HTML video error. There is no matched pre-fix media pair;
  do not attribute the drops to this repair or use these runs as parity proof.
  Trace renderer runnable delay, notification latency, present-clock late
  edges and host submission latency only where the new receipt implicates them.
- [ ] **DESK-04:** Retain the direct-scanout/host-window content gate and
  semantic hover/menu/input checks; broaden long-lived desktop observation.
  Cold QML/tooltip delay remains coverage, not grounds for a broad scheduler
  rewrite. PCID stays disabled until correct invalidation is demonstrated.
  The September 7 audit adds host-visible functional evidence; obtain an
  independent guest capture, default-fit geometry proof and an isolated fresh
  renderer-log segment before crediting the full visual contract.
  The later mouse exploration adds broad control coverage but mixed hover-only
  results: Desktop Icons and KWrite Borders needed clicks, and one Kickoff
  Utilities hover left Internet selected. Isolate input/timing/toolkit behavior
  before treating every hover path as validated.
  The YouTube audit also separates uncertain host left-click delivery from
  successful QMP virtual-mouse controls and host wheel scrolling; resolve the
  host input path before assigning unchanged clicks to application widgets.
- [ ] **DESK-05:** Revalidate the residual July 19 D-Bus #GP startup receipt
  separately from media throughput. Monotonic TLB-ticket repairs are proven
  for their reducers; the later failure prevents declaring every corruption
  or startup-instability path closed.
- [ ] **DESK-06:** Reconcile the final Node media-reducer coverage: the plan
  records both a passing host-only contract suite and an older missing-Node
  follow-up. Verify the focused reducer runs in the supported environment;
  JavaScript parse checks alone do not prove its semantic fixtures.
- [ ] **DESK-07 (conditional):** Implement a true deferred Bochs atomic worker
  only if matched profiling implicates its 4.0–5.8 ms synchronous path. Retain
  BO/owner references and correlate in-fence, out-fence and page-flip events.
- [ ] **DESK-08:** Resolve the September 7 ordinary-application gaps: diagnose
  Konsole's blank shell through child/PTY/prompt readiness, and establish the
  text/plain application association so Dolphin opens saved text in KWrite.
  Recheck visible shell command output and normal save/close/reopen behavior.
  Current source leads and same-session successes are in the
  [graphical audit](gui-progress-audit-20260907.md); no kernel cause is established.

### Acceptance contract

Use identical real YouTube URL/content, Chromium/Wayland/virgl, AV1 video,
Opus audio, 1280x720 `hd720`, 50% unmuted volume, KVM, six vCPUs, 8 GiB and
20 semantic display samples for both systems. Fullscreen means player-owned
fullscreen. After the requested mode is established, require 16 advancing
observations of one connected, active 1280x720 hd720 element before the
20-sample wall window. Bound setup by a real 30-second wall deadline;
playback requests are fire-and-observe, never an awaited play promise.
For replacement elements, preserve source identity, quality, mode and
non-regressing media time. The first callback of each replacement generation
is an anchor with zero callback/interval/frame credit. Count only subsequent
within-element intervals; duplicates still fail closed.

Require nonce-bound validation, 50–75 presented fps against host monotonic
wall time, media/wall 0.90–1.10, VPQ drops <=50%, and positive Pulse stream,
hardware-played and QEMU WAV byte deltas. Coalesced callbacks require clean
metadata with >=1.5 presented frames per callback and independent 50–75 fps
proof against both media and wall time; ordinary 30 fps still fails. No rewind,
detached element, regressing counters, nonpositive intervals or shortened
window earns credit. Require xv6 N>=2 mean >=90% of refreshed matched Linux
N>=2 mean, correct desktop/media geometry, visible content and no freeze.

## Linux ABI and application compatibility

Sources: [ABI foundation](archive/plan-consolidation-20260907/docs/linux-abi-compat-plan.md),
[kernel-gap plan](archive/plan-consolidation-20260907/docs/linux-userland-abi-kernel-gap-plan.md),
[dispatch audit](linux-abi-audit.md), [semantic audit](linux-abi-semantic-audit.md).
The generated semantic table retains the per-syscall remaining gaps.

### Recorded baseline

- [x] Native x86_64 dispatch, gated legacy aliases, host-glibc user programs,
  dynamic loader staging and static/dynamic ELF startup probes.
- [x] Core exec/auxv/proc identity, pthread/TLS/fork/wait, file/path/vector I/O,
  eventfd/select, signalfd setup, accepted-fd flags and socket subsets have
  evidence. Linux kernel termios is explicitly 36 bytes, avoiding glibc
  stack corruption from copying the wrong structure.
- [x] Later desktop work adds clone3 signal/thread-pidfd handling, wait and
  exit-group repairs, procfs/ptrace/Crashpad subsets, AF_UNIX SCM semantics,
  TCP EOF, device readiness, `/dev/shm`, and reservation/fence lifetime fixes.
  Preserve these subsets without claiming exhaustive compatibility.

### Open work

- [ ] **ABI-01 — Dispatch:** Keep generated audits and raw-number reducers
  current; require zero wrong-dispatch/native-missing/unsupported-bad. Audit
  generic-number collisions and residual shape-based `munmap`/`exit` rewrites
  before removing them. Give unsupported operations accurate, quiet errors.
- [ ] **ABI-02 — Process/runtime:** Complete clone/clone3 flag, size/tail,
  pidfd/TID/TLS/clear-child-tid contracts; wait4/waitid options and rusage;
  scheduler-derived resource accounting and enforced limits; credentials,
  sessions/groups, arch_prctl and signal-zero errors. Stress thread-group
  exit, robust/shared/file-backed futexes, TLS and rseq across reschedule,
  migration and signals. Extend PI/modern futex features only for a real need.
- [ ] **ABI-03 — VFS UAPI:** Assert Linux copyout layouts and offsets for
  stat/statfs/statx/open_how/dirent/pollfd/iovec. Broaden path/permission/umask,
  atomic flags, symlink/trailing-slash, statx masks/attributes, xattr, SIGIO,
  pipe capacity/atomicity, large-directory/short-buffer and vector-overflow
  coverage. Accurate errors and durable writes matter beyond successful calls.
- [ ] **ABI-04 — Discovery:** Test parseable procfs/sysfs fields and real
  lifetimes: status/stat/statm/maps/smaps/fdinfo/mountinfo/limits, fd targets and
  octal flags, CPU/memory/credential data, PCI/DRM/class/device discovery.
  Derive HWCAP from supported hardware when required; keep no-vDSO explicit.
- [ ] **ABI-05 — Memory semantics:** Complete MAP_PRIVATE/SHARED/ANON,
  FIXED/NOREPLACE, alignment/overflow, permissions/COW, partial unmap,
  split/merge, mremap overlap, msync durability, mincore residency and madvise
  matrices. Process-memory access needs permission/lifetime validation first.
  VM performance improvements must preserve these contracts.
- [ ] **ABI-06 — Signals/events:** Assert sigaction/sigset/siginfo/ucontext/
  frame ABI; prove alternate-stack execution and rt_sigreturn, queued signals,
  timed waits, permissions, mask/restart races, signalfd payloads, pselect/
  ppoll/epoll_pwait mask restoration, epoll ET/ONESHOT/nesting/close/dup,
  timerfd clocks/absolute/cancel/read semantics. POSIX timers are demand driven.
- [ ] **ABI-07 — Sockets/IPC:** Broaden AF_UNIX stream/datagram and TCP/UDP
  nonblocking/error behavior, sockaddr/options, SCM_RIGHTS/CREDENTIALS,
  CMSG_CLOEXEC, TRUNC/CTRUNC/PEEK/DONTWAIT, short buffers, iovecs and mmsg
  partial-success/timeouts. Stream read/readv discards consumed ancillary
  data; recvmsg/recvmmsg preserves it when requested. Revalidate fd passing
  and readiness across Wayland, X11, D-Bus and Mojo after relevant changes.
- [ ] **ABI-08 — Remaining surfaces:** Add SysV IPC layout/error and truthful
  scheduler/resource tests. Advanced IPC, AIO/io_uring, userfaultfd, pkeys,
  NUMA, keyrings, fanotify and new administrative APIs require an actual
  consumer and explicit scope. Keep unsupported combinations fail-closed;
  sandbox/userns/seccomp/SUID-sandbox work is excluded from this desktop queue.

## VM latency and lifetime

Source: [VM plan](archive/plan-consolidation-20260907/docs/linux-vm-abi-compat-plan.md).

- [x] June 16 evidence records writer-priority VM rwsems, bounded procfs
  formatting, stable anonymous-rmap keys and keep-right splits, stack-growth
  prechecks, pinned unlocked full-page file faults with revalidation, sparse
  present-leaf VM copy, and safe order-0 readahead. Tiny mprotect metadata
  dropped from 4304 ms to 0–1 ms; sparse-copy samples dropped 101→27 ms and
  143→55 ms. Gated readahead reduced cold fault sum 2966→327 ms in its sample.
- [x] Readahead advances `ra_pos` and defaults to files >=64 MiB. The rejected
  broad ext4 merge caused loader crashes; the final gated run was serial/
  trace evidence only because screendump returned `Error: no surface`.
- [ ] **VM-01:** Extend the fault reducer to unaligned ELF mappings and
  partial-tail/EOF pages before deciding whether adjacent PTE installation
  after VMA revalidation is justified. Prove byte correctness, BIO completion,
  physical continuity and pinned VMA/file/page lifetime.
- [ ] **VM-02:** Measure remaining reader waits behind writers, file-cache I/O
  and lock hold times under plain Chromium Ctrl+T and long idle. Use bounded
  diagnostics below the old 200 ms threshold; preserve writer fairness.
  Label semantic VM-lock call sites and distinguish futex/scheduler and
  epoll/poll latency after VM contention has cleared.
- [ ] **VM-03:** Stress large anonymous/file reservations, repeated 16 KiB
  mprotect, concurrent clock/rseq/poll, maple rollback, clone/fork COW/rmap,
  teardown and refined rseq event handling. Consider shorter critical sections,
  per-VMA locks, retry/RCU or usercopy separation only after measured need.
- [ ] **VM-04:** Require the focused reducer, ordinary browser responsiveness,
  long idle and AF_UNIX fd passing to pass; add the video/desktop gate for
  visible changes. Avoid application flags that hide kernel failures.

## DRM, GPU and browser coverage

Sources: [DRM plan](archive/plan-consolidation-20260907/docs/linux-drm-abi-compat-plan.md),
[implementation history](archive/plan-consolidation-20260907/docs/linux-drm-abi-impl-steps.md),
[GPU gaps](archive/plan-consolidation-20260907/GPU_REMAINING_GAPS.md),
[GPU roadmap](archive/plan-consolidation-20260907/.github/skills/xv6-debug-gui-runtime/GPU_OPENGL_PLAN.md),
[Alpine controls](archive/plan-consolidation-20260907/docs/alpine-virgl-desktop-handoff-plan.md),
[WebKit checklist](archive/plan-consolidation-20260907/.github/skills/xv6-debug-gui-runtime/WEBKIT_TODO.md).

### Recorded baseline and boundaries

- [x] Standard Mesa/GBM/libdrm/EGL DRM path: per-file GEM, PRIME/dma-buf,
  sync_file/syncobj/eventfd, atomic rollback/out-fences, property blobs,
  cursor, vblank, EXECBUFFER in/out fences, per-resource WAIT and guest blobs.
  Preserve existing private ioctl compatibility while testing standard UAPI.
- [x] June desktop-wide virgl/WebKit smoke/hardware-cursor V4 and detailed
  V6.1 validation are recorded complete. Their 80–96 fps control is a local
  workload, not current real-YouTube or KDE parity evidence.
- [x] Host-visible blob mapping is correctly host gated (`HOST_VISIBLE=0`
  when rejected). Metadata formats do not imply scanout capability; unsupported
  formats, modifier paths and legacy operations remain accurately fail-closed.
- [x] Hyper-V G0–G4 compute/offscreen work is hardware proven in the archived
  scope. G5 native display has no qualifying host ABI; keep native-present/
  OpenGL-submit credit zero on that lane. KVM virgl is a separate positive
  control. GPU-P is not a BAR-backed DDA/Nouveau display device.

### Open work

- [ ] **GPU-01:** Maintain honest capabilities and regress GEM/dma-buf fd,
  reservation/fence callbacks, syncobj waits, atomic TEST_ONLY/rollback/events,
  BO reuse, teardown and cross-process render-node-to-card presentation. Host-
  visible blobs require negotiated transport and real host acceptance first.
- [ ] **GUI-01:** Broaden minimal-flags/plain Chromium launch, profile/cache
  creation, first-run dismissal, focus/input, refresh/capture, Crashpad-enabled
  and heavier-page liveness. Existing local HTTP render/type/click proofs do
  not close every workflow. Reduce current failures by semantic process role.
  September 7 confirms the ordinary desktop launcher still selects
  `--disable-gpu --in-process-gpu --single-process --no-zygote`; its functional
  browser pass supplies no accelerated Chromium or multiprocess acceptance.
  The later [focused OpenGL audit](chromium-opengl-audit-20260907.md) adds
  a separate positive multiprocess-mode proof: fresh ANGLE/Mesa/virgl renderer,
  hardware-accelerated GPU feature report, and WebGL 1/2 shader draw plus pixel
  readback across a visible mouse-triggered green-to-blue change. Preserve this
  bounded opt-in result separately from the still-disabled ordinary launcher;
  repeated process/GPU recovery and broader browser workflows remain open.
  The [mouse audit](gui-mouse-audit-20260907.md) records browser-window
  disappearance after New Tab `+` twice, across horizontal/vertical layouts
  in one session. The first relaunch confirms abnormal shutdown; the second
  disappearance lacks independent process-exit evidence.
  Settings and layout controls work. Capture browser/thread ownership and
  exit/assertion evidence, then reduce the New Tab path; retain this ordinary
  launch failure instead of crediting multiple-tab support from page rendering.
- [ ] **GUI-02:** Retain ordinary HTTPS/NSS/TLS coverage across browsers and
  full visible response/input/close-reopen evidence. July Chromium YouTube
  proves its HTTPS workload; older missing generic-page/WebKit proofs should
  be rechecked, not reported as a universal TLS failure.
- [ ] **GUI-03:** Validate GPU/context recovery, explicit sync, dmabuf passing,
  compositor/browser IPC and readback lifetime under repeated launch/close.
  Compare interactive launch against validated trace defaults without making
  heavy tracing part of normal operation.
- [ ] **GPU-02:** Preserve correct cursor upload-before-update, descriptor
  reclaim, hidden-until-upload and hotspot semantics. Validate dynamic cursor
  shapes, alpha/composition and focus hide/show on the active frontend. The
  older GTK host-pointer mitigation is profile-specific; guest hardware
  cursor artifacts remain coverage, with no software-cursor workaround.
- [ ] **MEDIA-01:** Prove actual hardware decoder selection separately from
  accelerated composition and staged VA libraries. Keep WebKit MSE MIME/codec
  support, GStreamer demux/decode registration and media segment delivery as
  explicit investigations. Its June SPA-without-decoded-video receipt is
  separate from later successful Chromium playback; codec starvation remains
  a hypothesis until reduced.
- [ ] **MEDIA-02:** Validate supported WebKit long browsing, JS/worker behavior,
  helper lifecycle, input and repeated close/reopen with current clean runtime
  provenance. Old source overrides, interpreter-only settings and feature-
  disabling launch policies are historical experiments, not default repairs.
- [ ] **AUDIO-01 (conditional):** Extend ALSA capture, mmap, async notification,
  mixer/channel maps and status only when a real application requires them.
  July playback/timer/reset fixes remain the supported baseline.
- [ ] **GPU-03 (optional):** Reconsider partial scanout/window unredirection,
  damage optimizations or host-visible blobs only on the current standard
  stack after profiling. No app-sized desktop resize. The old custom-wlcomp
  Tier 3 roadmap is archived, not a mandate to rebuild that compositor.

Hyper-V native-present, Nouveau/DDA display, RISC-V, extra desktop-service
stacks and sandbox implementation need separate explicit scope. Old WSLg,
synthvid dirty-VRAM, fence IDs, submit success, or visible app-loop FPS do not
satisfy native display completion. Preserve the existing zero-credit matrices.

## VFS data I/O

Source: [VFS checklist](archive/plan-consolidation-20260907/kernel/kernel/vfs/VFS_DATA_IO_TODO.md).
This older checklist needs a current source audit before treating every
unchecked implementation as absent; July's ext4 direct-read and lifecycle
fixes remain newer evidence.

- [x] The checklist records address-space/extent interfaces, allocation and
  write-lifecycle/commit-size hooks, limits and checked wrappers complete.
  April 28 kernel build, clean ext4 boot/shutdown, iovectest, mmaptest,
  mmapbigfile and stressfs passed within that snapshot.
- [ ] **VFS-01:** Complete or verify generic regular-file read/write/vector/
  mmap helpers and xv6fs/ext4 adapters. VFS owns dispatch, semantics, lifetime
  and locks; generic page cache owns data transfer/BIO construction; filesystems
  own mappings, allocation, metadata, exact size and transactions; block
  drivers execute BIOs.
- [ ] **VFS-02:** Consolidate page-cache/writeback behavior; add justified
  extent caching, contiguous batching and readahead without duplicating
  filesystem transfer loops or weakening VM data-integrity safeguards.
- [ ] **VFS-03:** Prove EOF, sparse holes, short/error writes, offset/size
  limits, lock ordering, mmap coherence, truncate, fsync and persistence under
  concurrent access. `RWF_NOWAIT` must return `-EAGAIN` if allocation, blocking
  I/O, transaction start or lock waiting is required. Test partial completion
  and rollback, not only happy paths.
- [ ] **VFS-04:** Complete bigfile (the archived run was interrupted after
  silence), grind, symlinktest, dd/cp/find and relevant prior reducers. Compare
  iobench and BIO counts to baseline; require clean shutdown/reboot persistence
  and no regression in regular-file or mapped I/O.

## Network performance

Source: [lwIP plan](archive/plan-consolidation-20260907/kernel/LWIP_PERF_PLAN.md).
Its result rows are empty. The recorded ~50 Mbit/s P1/P4 and P8 timeout are an
old x86_64 six-vCPU virtio-net TAP baseline; 80/120/150 Mbit/s are targets.

- [ ] **NET-01:** Re-audit current lwIP options and establish a fresh
  tcpstress P1/P4/P8 baseline before claiming the proposed levers are missing.
- [ ] **NET-02:** Test custom-pbuf, checksum-on-copy and single-TX-pbuf options;
  then TCP_SND_BUF 64→256 MSS with receive window 128 MSS and memory-pressure
  checks. Treat each as an experiment with its own before/after receipt.
- [ ] **NET-03:** If justified, implement RX PBUF_REF with explicit mbuf
  ownership/custom free callback, and separately test a dedicated CPU for
  tcpip_thread (the original proposal used CPU 2).
- [ ] **NET-04:** After each change, pass DHCP/ping and best-of-three P1/P4/P8
  throughput tests, record failures and revert regressions. Do not infer SMP
  scaling from more vCPUs. Core-locking was previously ineffective; TX
  zero-copy/GSO, multi-stack work and a NetBSD rewrite remain out of scope.

## Upstream source cleanup and port migration

Sources: [de-patching plan](archive/plan-consolidation-20260907/docs/linux-userland-upstream-depatch-plan.md),
[root migration](archive/plan-consolidation-20260907/MIGRATION.md),
[ports migration](archive/plan-consolidation-20260907/ports/MIGRATION.md).

- [x] Kernel/user/ports build orchestration and the host-glibc transition are
  recorded complete. Source recipes cover the listed foundational, font,
  GLib/GTK, Wayland, NetSurf and CPython stacks. This does not prove every
  imported source is clean.
- [x] An inventory scanner, reviewed allowlist, upstream refs, user-program
  audit and phase TSVs already exist in `scripts/audit/` and `docs/`.
  Their presence supersedes the old instruction to create them from scratch;
  all 77 checked-in phase1–phase5 rows record `done` / `abi_adaptation=no`.
  These are reviewed records, not a fresh source-cleanliness validation.
- [ ] **DEP-01:** Verify the complete inventory below against current tree
  membership and reviewed TSVs. Record class, exact upstream ref/checksum,
  source deltas, markers, ABI adaptations, patch/wrapper files and next owner.
  Keep the hygiene gate and allowlist narrow and reviewable.
  The consolidation's membership check found nine programs already absent
  from the original scope: alsapcmpoll, clockbench, consolerecord, dcachetest,
  hoverprobe, regpreservetest, syscalltlb, waitreapstorm and wakestorm. Reconcile
  their classification and reviewed audit data before expanding the parser
  scope; the original and consolidated 157-entry inputs fail membership
  identically until this existing drift is resolved.
- [ ] **DEP-02:** For each imported-source delta, assign a kernel ABI,
  libc/sysroot, runtime-data, build/staging or explicitly local-shim owner
  before removing the workaround. Rebuild the touched package and prove its
  relevant runtime behavior. Marker-only or upstream-equivalent data changes
  are the target; source mutation hidden in a wrapper is still a source delta.
- [ ] **DEP-03:** Audit patch directories, apply-patch and post-checkout
  mutation scripts. No imported-source alternate syscalls, UAPI structs,
  errno rewrites, event/fd/socket/proc/graphics shortcuts, sleeps/retries or
  feature removal to hide kernel defects. Temporary exceptions need a linked
  owning bug and an explicit retirement gate.
- [ ] **DEP-04:** Separate local command/session/test/probe/filesystem-tool
  roles and shared helpers. Production sh/init/mount/umount/ps/top and file
  tools use ordinary Linux libc behavior; private diagnostic helpers stay
  test-only. Audit host_compat.h/user.h/fsutil.h accordingly.
- [ ] **DEP-05:** Document local shims' ownership and upstream-compatible
  interfaces, compare behavior, and replace with upstream where prerequisites
  permit. Locale/compose/fonts/icons/certificates/timezone/mime/hardware data
  belongs in staged runtime data, not imported application source.
- [ ] **DEP-06:** Execute package cleanup in original dependency order:
  foundation/data/host tools → image/font/GLib/GTK → graphics/input/Wayland →
  applications → final local-library/program helper cleanup. The reviewed
  phase1–phase5 TSVs retain package-level ownership/status; the scope appendix
  retains every original inventory member, including historical NetSurf probes.
- [ ] **PORT-01:** Provide a reproducible from-source WebKit recipe and runtime
  validation when pursuing that port. Existing policy selects an explicit
  host-glibc runtime via XV6_WEBKIT_REF_SYSROOT, validates staged contents,
  emits a manifest and skips absent runtime when strict-stage is off. Do not
  silently stage stale musl artifacts or revive retired source overrides.
- [ ] **PORT-02:** Audit any remaining package/dependency/post-install work
  against current recipes. Old custom-musl migration and legacy image-target
  removal are history; remove a legacy target only after confirming that it
  remains present, obsolete and unused by supported build paths.

Completion requires every member classified, imported sources meeting the
allowed-delta policy, documented/tested local shims, reviewed hygiene data,
and no behavioral patch escaping the gate. The old unchecked inventory rows
are scope membership, not evidence that every package still needs the same fix.

## Validation and execution rules

Root [AGENTS.md](../AGENTS.md) and applicable repository skills govern all work.
Use the guarded `/home/es/.local/bin/rg`, source-only bounded searches and the
artifact wrapper for eligible named artifacts. Respect the global search lock.
If a command returns a running-process handle, synchronously wait on that exact
process before another search or heavy command; output limits do not bound work.

Before VM authorization, require no active VM worker and an exact `/proc/*/exe`
zero count for all qemu-system-* and qemu-kvm executables, never `pgrep`.
Wait for external VMs to exit naturally; never signal them. Each owned run
requires verified PID/start-time/token/process-group ownership, synchronous
reap, bounded cleanup and the final exact no-QEMU check. Serial silence proves
nothing: wait on the command, check state, recover a fresh prompt. Keep serial
commands short with marker variables and account for first-character drop.

Keep at most three completed generated build/image iterations, deleting only
confirmed-unused old iterations before a fourth. Protect active disks, Docker
data, deployed images, source trees and the newest known-good rollback.

For implementation validation:

- Build the touched layer. Prefer kernel-only builds for kernel-only fixes;
  run Sparse where relevant. Refresh rootfs only when staged payload or image
  provenance requires it. Verify the actual kernel/image being booted.
- Start with the smallest Linux-comparable reducer; extend one syscall,
  flag, fd, event, mapping or lifetime edge at a time. Derive process roles
  from same-run argv/exe/thread/fd/socket/surface state, never copied PIDs.
- ABI closure requires linuxsyscallabitest/raw Linux-number regressions,
  host static/dynamic/no-libc probes and the relevant pthread/fork/file/proc/
  signal/event/socket/DNS/TLS matrices against the current kernel and staged
  loader/runtime. Refresh the image when relevant; supported startup remains
  quiet. Inspection-only coverage is not a substitute for these runtime gates.
- After GPU/DRM/desktop-visible edits, run the supported perf-video gate and
  relevant SDL/GUIHD checks. Within the active desktop effort, every further
  kernel/ABI change requires its focused reducer, build/Sparse, SDL/GUIHD and
  at least N>=2 matched media trials. The older local perf-video gate disables
  audio and cannot replace the real-audio YouTube proof.
- Capture logs and actual screenshots for visual claims. A title, live
  process, HTTP bytes or counters alone cannot prove visible content/input.
  Mark capture failure explicitly; diagnostic serial-only evidence remains
  useful without acquiring visual credit.
- Preserve failed receipts and rejected experiments. Use bounded opt-in
  traces and clean A/B controls. Do not weaken workload or validator criteria.
- For source cleanup, require diff checks, reviewed inventory coverage,
  touched package builds and relevant runtime smoke. This consolidation itself
  requires archive integrity, reference and inventory-parser checks only.

## Evidence and reference map

Build and `/tmp` paths below identify historical receipts, not guaranteed
retained files or prerequisites for a fresh clone. Full run histories and
rejected experiments remain in the archived originals. Use maintained script
paths and fresh source/image hashes when repeating a measurement.

| Area | Reference / receipt |
| --- | --- |
| Linux ABI | [Dispatch audit](linux-abi-audit.md), [semantic audit](linux-abi-semantic-audit.md), scripts/audit/linux_abi_audit.py and linux_abi_semantic_audit.py |
| DRM | [DRM audit](linux-drm-abi-audit.md); archived implementation history; drmabitest / drmiftest / PRIME / host GUI proofs |
| SDL | scripts/gpu/sdl-geometry-probe.sh; build-x86_64/sdl-geometry-probe/live-x11-hidpi-off-20260718T205013Z-2308562 |
| Windowed controls | build-x86_64/youtube-parity/xv6-windowed-20260718T223622Z-2490797-direct-pulse and xv6-windowed-20260718T225053Z-2503729-direct-pulse |
| Fullscreen controls | xv6-fullscreen-20260716T035521Z-610088-direct-pulse and xv6-fullscreen-20260716T035737Z-612822-direct-pulse; matched linux-fullscreen-20260716T033032Z-591724 and linux-fullscreen-20260716T033341Z-593897 |
| Latest media failures | xv6-windowed-20260719T021952Z-2670295, 022229Z-2672790, 022703Z-2677178, 022948Z-2680006, 023315Z-2683063 (all direct-pulse suffixes; July archive has full paths/verdicts) |
| Audio / scheduler / wait | build-x86_64/alsapcmrecover/20260716T004850Z-411756 and 004957Z-413956; scheduler-probe/guihd-20260716T021126Z-509286; waitreapstorm/20260716T022613Z-527413 and 022632Z-528884 |
| VM | chromium-newtab-unlocked-filefault-tailguard-20260616a; chromium-newtab-vmcopy-sparse-20260616a; chromium-newtab-filefault-ra-gated-20260616b; weston-ready-ra-gated-20260616a |
| Older desktop controls | build-x86_64/alpine-trace/alpine-virgl-behavior-summary.txt; host-gui-proof-verify; chromium-normal-desktop-proof; perf-video-gate/run.log and frame captures |
| Current Chromium OpenGL | [Normal versus opt-in GPU/WebGL audit](chromium-opengl-audit-20260907.md), including screenshots, WebGL JSON results, launch provenance and cleanup |
| Current YouTube exploration | [Playback attempt and watch-page controls](chromium-youtube-audit-20260907.md), including audio-renderer failure, separate input routes and same-run evidence |
| Browser investigation | [Historical YouTube report](../YOUTUBE_KERNEL_GAP_REPORT.md), [retired WebKit overrides](../.github/skills/xv6-debug-gui-runtime/WEBKIT_GAP_MAP.md) |
| Hyper-V | [GPU references](../HYPERV_GPU_REFERENCES.md), [previous fail-closed archive](../GPU_REMAINING_GAPS.dxg-failclosed-archive.md) |
| Source hygiene | scripts/audit/userland_depatch_inventory.py; docs/linux-userland-depatch-allowlist.tsv, linux-userland-upstream-refs.tsv, linux-userland-user-program-audit.tsv, linux-userland-depatch-phase*.tsv |
| Build / launch | [README](../README.md), [script guide](../scripts/README.md); scripts/launch/run-qemu.sh, qemu-wait-natural-zero.sh and qemu-exact-inventory.sh |

## Source-cleanup inventory scope

The three headings and bare unchecked entries below preserve the original
machine-readable input to `scripts/audit/userland_depatch_inventory.py`.
They declare audit membership. Record implementation completion in DEP tasks
and reviewed inventories; do not interpret these rows as per-package verdicts.

### Local User Libraries

- [ ] `user/lib`
- [ ] `user/lib/x86_64`

### Local User Programs

- [ ] `init`
- [ ] `sh`
- [ ] `cat`
- [ ] `echo`
- [ ] `sleep`
- [ ] `kill`
- [ ] `reboot`
- [ ] `shutdown`
- [ ] `sync`
- [ ] `free`
- [ ] `ps`
- [ ] `top`
- [ ] `lsblk`
- [ ] `mount`
- [ ] `umount`
- [ ] `mknod`
- [ ] `mkdir`
- [ ] `cp`
- [ ] `mv`
- [ ] `rm`
- [ ] `ln`
- [ ] `find`
- [ ] `grep`
- [ ] `wc`
- [ ] `xargs`
- [ ] `dd`
- [ ] `losetup`
- [ ] `mkfs_xv6fs`
- [ ] `dumpinode`
- [ ] `dumpchan`
- [ ] `dumppcache`
- [ ] `dumprq`
- [ ] `bigfile`
- [ ] `devtest`
- [ ] `dh`
- [ ] `wallclock`
- [ ] `timerdemo`
- [ ] `pngtest`
- [ ] `keyinject`
- [ ] `mouseinject`
- [ ] `mousetest`
- [ ] `waitgdb`
- [ ] `kprofile`
- [ ] `pingpong`
- [ ] `primes`
- [ ] `randtest`
- [ ] `zombie`
- [ ] `forktest`
- [ ] `vforktest`
- [ ] `clonetest`
- [ ] `cloexectest`
- [ ] `cowtest`
- [ ] `mmaptest`
- [ ] `mmapbigfile`
- [ ] `fdtabletest`
- [ ] `iovectest`
- [ ] `kqueuetest`
- [ ] `timerfdstress`
- [ ] `syscalltest`
- [ ] `linuxsyscallabitest`
- [ ] `testsig`
- [ ] `usertests`
- [ ] `grind`
- [ ] `crashtest`
- [ ] `stressfs`
- [ ] `iobench`
- [ ] `symlinktest`
- [ ] `blocksendwake`
- [ ] `dontwaitsend`
- [ ] `dnsstress`
- [ ] `tcpstress`
- [ ] `fbstat`
- [ ] `gldemo`
- [ ] `virgltest`
- [ ] `gpubuftest`
- [ ] `gpucorevalidate`
- [ ] `drmabitest`
- [ ] `drmiftest`
- [ ] `drmprimeprobe`
- [ ] `nouveauabitest`
- [ ] `dxgprobe`
- [ ] `d3d12probe`
- [ ] `ttmtest`
- [ ] `webkitabitest`
- [ ] `webkitnettest`
- [ ] `preempttest`

### Ports, Libraries, Programs, Data, And Headers

- [ ] `cmake`
- [ ] `linux-uapi-headers`
- [ ] `khronos-headers`
- [ ] `hwdata`
- [ ] `wayland-host`
- [ ] `glib-host`
- [ ] `libpng-host`
- [ ] `zlib`
- [ ] `bzip2`
- [ ] `xz`
- [ ] `libffi`
- [ ] `libexpat`
- [ ] `pcre2`
- [ ] `json-c`
- [ ] `sqlite`
- [ ] `ncurses`
- [ ] `readline`
- [ ] `openssl`
- [ ] `curl`
- [ ] `openssh`
- [ ] `libxml2`
- [ ] `libpng`
- [ ] `libjpeg-turbo`
- [ ] `freetype`
- [ ] `fontconfig`
- [ ] `pixman`
- [ ] `fribidi`
- [ ] `harfbuzz`
- [ ] `glib`
- [ ] `atk`
- [ ] `cairo`
- [ ] `gdk-pixbuf`
- [ ] `pango`
- [ ] `gtk3`
- [ ] `libudev`
- [ ] `libseat`
- [ ] `libevdev`
- [ ] `libinput`
- [ ] `libdrm`
- [ ] `drm_info`
- [ ] `kmscube`
- [ ] `libepoxy`
- [ ] `wayland-libs`
- [ ] `wayland-protocols`
- [ ] `xkeyboard-config`
- [ ] `libxkbcommon`
- [ ] `xv6-gbm`
- [ ] `mesa`
- [ ] `weston`
- [ ] `netsurf-buildsystem`
- [ ] `netsurf-libwapcaplet`
- [ ] `netsurf-libparserutils`
- [ ] `netsurf-libcss`
- [ ] `netsurf-libnsbmp`
- [ ] `netsurf-libnsgif`
- [ ] `netsurf-libnslog`
- [ ] `netsurf-libnspsl`
- [ ] `netsurf-libnsutils`
- [ ] `netsurf-libhubbub`
- [ ] `netsurf-libsvgtiny`
- [ ] `netsurf-nsgenbind`
- [ ] `netsurf-libdom`
- [ ] `netsurf`
- [ ] `cpython`
- [ ] `vim`
- [ ] `peanut-gb`
- [ ] `wayland-src`
- [ ] `webkit`
- [ ] `wayland`
