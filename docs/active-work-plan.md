# Active xv6 Work Plan

Last updated: 2026-07-04 (FULL COMPACTION REWRITE after the 07-04 round:
R5 root-caused + fixed, P2 ordered-pageflip default landed, Q2 real-GL
runtime-validated with the first default-path M9 PASS, P3 ext4 slice landed
gated, N2 ext4 default promotion attempted but NOT accepted after a
default-on KWin #GP rerun; later N2 ON-arm kernel #PF classified and
opt-in diagnostics landed; kprofile measurement validity fixed; N3 R9
coordinate and visible cursor probes passed. All verbose evidence chains moved to
the history file and git log; this file holds current status, queue,
rules, and compact lane conclusions only.)

Single-plan rule: this is the only live plan file. Verbose pre-compaction
records (including the full 2026-07-04 pre-rewrite plan) are preserved
append-only in
`docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`
("the history file"). Durable mechanisms/workflows live in
`.github/skills/`. Every proof run is archived under
`build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/` (or the
lane-specific proof dirs) — archive names cited below are relative to that.
Branch note: this plan lives on `codex/host-linux-abi-shell-port-ff`;
sibling copies on `codex/host-linux-abi-shell-port` /
`origin/codex/kde-qt-wayland-bringup` predate the 07-04 round.

## Mission

Make the KDE/Chromium desktop on x86_64 KVM+virgl stable and responsive.
Current posture: correctness lanes (P0 freeze, R5 corruption) are fixed or
closed; the remaining work is performance (M7 video path, M2 syscall cost,
M8 idle CPU), user-visible input (R9), and statistical closure.

## Scoreboard — Measurements and Goals

Move one metric without regressing the others. Verify the booted cmdline in
every run log (`grep 'x86 kernel cmdline'`).

Measurement validity rules:

- The guest drops ~14% of BSP timer ticks under 6-vCPU desktop load
  (kprofile wall 40000ms vs kernel uptime 34201ms, reconfirmed twice).
  Single-run `*_ms` deltas under load are +/-15% unless cross-checked
  against a monotonic wall reference. Candidate fix (open, N7): compensate
  lost BSP ticks via TSC or let any CPU advance jiffies.
- A kprofile run with `kprofile_timeout_hit=1` AND an incomplete measured
  window is truncated — do not read scoreboard metrics from it. (For
  browser workloads timeout_hit=1 alone is EXPECTED — Chromium never exits;
  judge window completeness by the PERF-VIDEO RESULT/`advanced=` line.)
- Real-GL Chromium video runs need
  `KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE_SECONDS=90`: first-boot virgl->D3D12
  shader compile + cold paging eats the default 40s window.
- kprofile `cpu_busy/total_ms` are scheduler-invocation counts scaled by
  HZ, NOT wall time (scheduler_yield runs per reschedule, ~5x tick rate
  under load); only the busy/total RATIO is meaningful. pgroup fields are
  process-group-only (zygote children escape). kprofile's exec takes
  absolute paths only; guest /bin/sh does not glob (use /bin/bash).

Audio: host audio is NOT a readiness prerequisite. `pactl` probes are
default-off (`kde_pactl_probe=1` to re-enable); non-audio gates run with
`QEMU_AUDIO_BACKEND=none`.

| # | Metric | How measured | Current (2026-07-04) | Goal |
|---|--------|--------------|----------------------|------|
| M1 | YouTube-freeze survival | P0 repro recipe, 15 min, 3 runs | 3/3 responsive clean (2026-07-03 battery); P0 closed | 3/3 clean |
| M2 | Guest `getpid_ns` | `syscalltlb 2000` nographic | 1.65-1.94us (P1 2a/2b landed) | < 1.5us (P1 2c/2d = N4) |
| M3 | `tlb_amplification_ns` (1024 pg) | same | noisy 2.7-3.7us band | 0 on default boot |
| M4 | `konsole_wait_ms` | KDE desktop-interaction reducer | 1883-2144 band across the 07-04 batteries (best 1883 post-R5-fix) | < 2000 |
| M5 | `first_visible_ms` | same | 11471-13850 band | < 15000 |
| M6 | `mesakmsgl` direct-KMS FPS | pageflip A/B | 118-125 ordered, now DEFAULT-ON | done (was: ordered default) |
| M7 | `presentedFPS` (60fps video) | chromium-video kprofile, KPROFILE_SECONDS=90 | 44.2 real GL + ordered pageflip (arc 07-04: 36.8 -> 42.9 -> 44.2; dropPct 29.6; decode 61.5 keeps pace). Sole remaining ceiling: software-blit scanout — every flip copies, native_present_credit=0 | >= 55 (N1) |
| M8 | Idle-desktop host CPU | 10s `/proc/$pid/stat` utime+stime delta (NOT lifetime ps pcpu) | borderline 85-130% band; owner evidence = vCPU/KVM idle wake cadence (guest PCs in arch_idle_halt), not a spinner | < 100% (N5) |
| M9 | Chromium window visible | chromium-video launch-only reducer | PASS on the DEFAULT path with REAL hardware GL (2026-07-04): zero missing-GL fatals, zero software fallbacks, GPU errors 0 | PASS (holds; guard in every battery) |

Fork-safety gate for any syscall/scheduler/TLB/mm change: `forktest`
(rc=1 "fork claimed to work N times!" = known exhaustion signature),
`clonetest` rc=0, `cowtest` rc=0, same boot.

## Work Order — Current Queue (recut 2026-07-04 post-round)

The 07-03 Q1-Q7 queue is RETIRED: Q1 landed, Q2 runtime-validated (real
GL), Q5 root-caused+fixed, Q7 landed. New queue:

- N1 = M7 present path (P2 step 3): the last M7 blocker. All flips are
  software_blit copies. Decision slice result 2026-07-04: route (a) is
  fail-closed on this host. Runnable QEMU is
  `/usr/bin/qemu-system-x86_64` Debian 9.0.2. It advertises classic
  `virtio-gpu-pci`, `virtio-gpu-gl-pci`, `virtio-vga`,
  `virtio-vga-gl`, and `vhost-user-gpu-pci`, but no
  rutabaga/gfxstream/cross-domain device. `virtio-gpu-rutabaga-pci,help`
  says "Device not found"; classic `virtio-gpu-gl-pci,help` fails with
  `undefined symbol: qemu_egl_display`. `/dev/udmabuf`, `/dev/kvm`, and
  `/dev/dxg` exist; no `/dev/dri/renderD*` exists. Host has
  `libvulkan_gfxstream.so` and `libvirglrenderer.so.1`, but no usable
  `rutabaga_gfx_ffi` surfaced. Non-GL `virtio-gpu-pci` supports
  `blob`, `hostmem`, and `max_hostmem`, and a dry-run can form a memfd
  command with `blob=true,hostmem=32M,max_hostmem=32M`; the
  `virtio-vga-gl-primary` + blob lane still fail-closes with
  `QEMU rejects virgl + blob ("blobs and virgl are not compatible")`.
  Landed launcher detection only: `QEMU_BIN=/path/to/qemu-system-x86_64`
  selects the probed binary, and
  `QEMU_GPU=virtio-gpu-rutabaga{,-primary}` or
  `QEMU_GPU=virtio-vga-rutabaga-primary` requires blob/hostmem/
  max_hostmem, `/dev/udmabuf`, hardware host DRI, the rutabaga device,
  `gfxstream-vulkan`, `cross-domain`, `wsi`, and memfd RAM; it refuses
  classic virgl or software fallback. No M7 run was attempted because
  the route cannot dry-run to device args. Kernel route is also not ready
  to implement blindly: creatable capset admission currently supports
  VIRGL/VIRGL2 only (DRM is query-only), not upstream gfxstream/
  cross-domain. Next kernel task, after a capable host/QEMU exists:
  define and wire `VIRTIO_GPU_CMD_SET_SCANOUT_BLOB`, carry blob resource
  format/stride/modifier metadata through the fb/KMS present path, add
  gfxstream/cross-domain capset admission only with a real contract, and
  add native/blob present counters. Route (b) remains low payoff unless
  new evidence contradicts `bo_present_copy_ticks=0` and
  `virtio_present_copy_calls=0`.
- N2 = P3 promotion: attempted 2026-07-04, NOT accepted. The default-on
  guarded battery had static/build/nographic PASS, one KDE active-sample
  PASS, explicit-off control PASS after one known visible-timeout flake,
  and M9 launch-only PASS, but an extra default-on active-sample rerun hit
  `kwin_wayland` #GP in `libQt5Core.so.5` (`kde-session-ready-crash`).
  The post-stop cold-cache A/B did not reproduce that R5-class KWin/QtCore
  signature, but stopped on direct-read ON run 5 with a new kernel page
  fault (`cr2=0x1aafdd193 err=0x2 rip=0xffff80000039a34c`, RIP resolving
  into kernel `_rodata`) after four clean ON runs, one clean OFF run, and
  one known OFF visible-timeout flake. Read-only fault mapping classifies
  that as corrupted control flow into `.rodata`: ASCII bytes decoded as a
  bogus write, not NX; the `sig_trampoline` line is a symbolization
  artifact, and the direct-read tie is still correlational. Kernel
  `04b1ee2` landed opt-in fault/direct-read diagnostics plus a narrow
  ON-only transient compound-node fallback guard. Two debug-ON KDE
  active-sample runs with `ext4_read_page_direct=1
  ext4_read_page_direct_debug=1` did not recur the kernel fault or trip an
  invariant (`20260704T191028Z-n2-direct-read-debug-on-run1-artifact-timeout`,
  `20260704T191406Z-n2-direct-read-debug-on-run2-pass`). The gate remains
  default-OFF; no promotion retry until a diagnostic repeat explains the
  fault or a fresh promotion battery has zero R5-class/kernel crashes.
  Optional second slice still exists after promotion: batch remaining
  non-sequential single-page fills (executable page-in; ~71% of fills).
- N3 = R9 cursor out-of-range (user-visible) + the KWin LibinputBackend
  nullptr payload bug found by R5 forensics (same input area). Harness
  and seat plumbing are fixed; coordinate delivery is now classified and
  has a passing contract. Root cause for the `20260704T195436Z` failure
  was the probe's default RUNPATH preferring the host
  `/usr/lib/x86_64-linux-gnu` libinput/libudev stack; udev enumeration was
  empty and libinput failed monitor/seat setup. Root cause for the
  `20260704T200703Z` coordinate failure was QEMU injection, not kernel
  scaling/storage, evdev, or libinput classification: HMP `mouse_move`
  delivered legacy PS/2 relative samples (`flags=0`, repeated/clamped
  127,127) and zero ABS samples. The strict HMP-selected rerun
  `20260704T202206Z` proved `mouse_set 3`/`info mice` selected
  `QEMU Virtio Tablet (absolute)` but HMP still produced only PS/2
  relative samples. Current harness therefore keeps HMP `info mice` as
  routing evidence, adds a QMP socket, and injects raw 0..32767 tablet
  coordinates with `input-send-event` absolute X/Y pairs. Probe PASS is
  now strict: zero evdev ABS or zero libinput absolute samples fails with
  an explicit reason. Dry-run `20260704T202724Z` PASS; real R9
  `20260704T202747Z` PASS with `/dev/mouse flags=1`, `evdev_abs_samples=10`,
  `libinput_abs_samples=5`, and `result=PASS reason=coordinate_samples`.
  Visible cursor-plane probe `scripts/gpu/r9-cursor-visible-probe.expect`
  then passed in guest cursor mode at
  `build-x86_64/r9-cursor-visible-probe-history/20260704T205234Z-r9-cursor-visible-probe`:
  dry-run proves `show-cursor=off` and no
  `virtio_gpu_host_cursor_only=1`; QMP `input-send-event` remains the
  coordinate proof; `/dev/mouse flags=1`, evdev ABS and libinput absolute
  samples are present; cursor-plane traces land at 0,0 / 640,400 /
  1279,799 with no 2x/out-of-range transform; no panic/KWin crash/
  LibinputBackend nullptr marker was seen. Framebuffer/host capture does
  not prove visible cursor pixels because the GTK hardware cursor overlay is
  outside the QEMU framebuffer capture path. Regression guards passed and
  were archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T210241Z-r9-visible-desktop-interaction-pass`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T205909Z-r9-visible-chromium-launch-only-pass`.
  Remaining N3 work is the later/secondary KWin LibinputBackend nullptr
  payload unless a startup/input crash reproduces under the visible probe.
- N4 = P1 steps 2c/2d (cpumask atomics skip, CR0.TS shadow) for M2 <1.5us.
  Implement both together, one battery.
- N5 = M8 idle cadence: vCPU/KVM idle wake churn (guest halted, host vCPU
  threads at 85-130%). Host-side attribution done; next is guest tick/timer
  cadence reduction (relates to N7).
- N6 = R2 PCID stale-TLB lane: RETEST CHEAPLY FIRST — the R5
  free-before-shootdown fix plausibly WAS the "PCID corruption" (noflush
  widens the same stale-TLB window; the 07-02 noflush trial reproduced the
  exact KWin signature R5 explained). One guarded PCID default-flip KDE
  battery before any new reducer work.
- N7 = timer tick loss (~14% under load): fix jiffies advancement
  (TSC-compensate or any-CPU advance). Fixes measurement trust AND late
  timer fires (frame pacing). Gate: nographic + KDE battery + M2/M3 within
  noise (touches the timer hot path).
- Continuous: R5 statistical closure (9/30+ clean attempt-1 KWin launches
  accrued; count every future battery), R3 recurrence watch (rcu_head
  double-free may share the R5 root cause — one `slab_alloc: repairing
  corrupt freelist cache='rcu_head_cache'` line was seen 07-04 pre-R5-fix).
- Parked: CR-3 (%fs selector reload semantics), CR-7 (starve-probe RCU),
  CR-9 (timer 1-jiffy boundary race, needs timerfd reducer); latent
  hugepage-path bugs (list in the R5 lane — they BLOCK re-enabling
  `vma_file_hugepage_collapse_enabled`); Chromium GL conformance depth
  beyond the validated ladder (only if real workloads hit gaps); KDE
  session-stability residuals (see that lane).

## Work Style — Time Budget and Batching (binding)

1. CODE-FIRST: finish the whole code slice (source read end-to-end,
   hypothesis in the lane, fix + reducer + gated probes) BEFORE any VM
   boot. VM runs validate completed slices.
2. BATCH GATES: one battery validates a batch of independently-revertable
   changes: nographic fork/clone/cow + one KDE active-sample pass + one
   Chromium launch-only guard + M8 spot-check. Bisect only on failure.
3. OFFLINE FIRST: parsers/classifiers/forensics run against the existing
   archives (700+ smoke runs, profile dirs) — never a VM boot for a parser
   change. The 07-04 R5 root-cause (5 subagent passes over archives +
   sources, zero exploratory boots) is the reference example.
4. REUSE BOOTED VMS: plan the probe list before booting; collect
   everything in one session. Budget <=2 boots per lane slice + the final
   battery.
5. EXCEPTION: genuinely run-variant work (flake statistics, corruption
   repro, freeze repro) may use as many runs as the evidence requires.
6. Track the implementation-vs-validation split per lane update; if
   validation dominates twice in a row, stop and re-plan.
7. ORCHESTRATE, DO NOT DO: the handoff agent is an ORCHESTRATOR. Decompose
   each slice into jobs and spawn one subagent per job (source
   read-through, implementation, offline forensics, gate runs, log
   triage); run independent jobs concurrently; keep own context for
   synthesis and decisions. Subagents return conclusions/diffs, never flip
   defaults, never push.
8. COMMIT PERIODICALLY: at every verified checkpoint and before ending,
   commit repo + submodules (kernel, ports, ports/mesa/src, user)
   deepest-first with lane-scoped messages, then update parent pointers.
   Never push without explicit user approval.

## Known Failure Modes — Do Not Repeat

Check BEFORE declaring any gate failed or hypothesis confirmed.

1. Wrong session lane: `host_chromium=1` is weston-only; launch Chromium
   in KDE via `/bin/wayland-chromium <url>` with
   `XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root WAYLAND_DISPLAY=wayland-0`.
2. Passive-visible-detector flake: always set
   `KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`.
3. Silently dropped kernel flags: every run log must show the intended
   tokens in `x86 kernel cmdline` (and `vm_asid_init: max ASID` for PCID).
4. Serial console truncates ~55-60 chars, drops trailing `&`, splits
   output mid-token. Stage script files or debugfs-inject; keep commands
   short.
5. Single-sample misdiagnosis: hangs need multiple timed GDB samples plus
   an executed (not merely echoed) guest liveness command.
6. Nographic gates do NOT validate VM/TLB/scheduler correctness (the PCID
   flip passed nographic then corrupted KWin). GUI-scale gates required;
   audits need a failing-then-passing reducer.
7. Harness label vs app truth: judge by post-evidence/status files and
   guest logs, not wrapper labels.
8. Stale runtime: check kernel/fs.img mtimes vs the running QEMU.
9. Trace volume perturbs timing: thresholded/role-scoped flags only in
   timing runs.
10. Debugcon timestamps are per-boot TSC — never cross-boot wall clock.
11. Starvation-probe lines alone are not freeze proof while liveness
    commands execute.
12. Background Chromium intentionally in freeze repros; short commands.
13. >60-char/multi-line serial commands can wedge bash in
    quote-continuation (recover with a lone closing quote).
14. QEMU gdbstub wedges after a killed gdb ("target is running") —
    restart the VM; always detach; sample against kernel.elf (xv6.bin is a
    bzImage).
15. Desktop-smoke PASS with `chromium=-1` says nothing about M9; judge
    Chromium only by the chromium reducer or manual launch + guest logs
    (`debugfs -R 'cat /host-gui-wayland-chromium.log' build-x86_64/fs.img`).
16. Uncommitted behavior-affecting diffs are handover landmines: commit,
    revert, or list them in this plan.
17. KPROFILE-mode bookkeeping: in `KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE=1`
    runs, `status=FAIL reason=launch-evidence-missing` is EXPECTED
    (kprofile replaces the launch-evidence stream) — not a playback
    failure. Read the kprofile log + PERF-VIDEO lines instead.
18. Serial wait-markers must not appear in the echoed command itself (a
    grep for `MARKER` matches the echo and fires early — killed a cowtest
    run twice). Emit markers via a variable: `M=XX; cmd; echo ${M}RES=$?`.
19. Artifact/visible-timeout harness flake class
    (`*-artifact-timeout`, visible-parser/helper timeouts): 17+ prior
    archives; KWin/desktop healthy. Archive + rerun once.
20. konsole-shell-ready-timeout flake: konsole launches but the ready
    marker never appears (45s timeout, zombie) -> M4 unmeasured. Rerun
    once; escalate only if frequency rises.
21. When host WSLg PulseAudio is down AND a pactl probe is explicitly
    enabled, the guest pactl fault cascades into `kde-session-ready-crash`
    (recovery: `wsl --shutdown` from Windows). Default runs skip pactl.

## Guardrails

- NO un-gated default flips (kernel cmdline defaults, launcher env,
  image/session config). Diagnostics are opt-in default-off. A default may
  change only with: same-session A/B + explicit-off control, a plan entry
  naming the lane, and the regression battery. (Executed examples:
  ordered-pageflip 07-04 PASS; PCID 07-02 FAIL-and-reverted.)
- Regression battery after ANY behavior change: KDE active-sample smoke +
  Chromium launch-only no-worse-than-scoreboard + M4/M5/M8 within noise.
  New failure class = revert first.
- Handover hygiene: `git status` at top level AND every submodule
  (kernel, ports, ports/mesa/src, user); every behavior-affecting diff
  committed, reverted, or listed here. Current dirty-by-design: NONE (tree
  clean as of the 07-04 compaction; `ports/xz/src` if dirty is unrelated —
  do not revert).
- Closed lanes — do NOT reopen without new evidence: scheduler
  wake-to-run latency, futex key/timeout drift, poll/kqueue/AF_UNIX/
  eventfd/pipe primitives, guest cursor upload, raw PTY setup, tiny
  Wayland frame delivery, D-Bus/eventfd readiness, renderer admission
  (R8), inotify FIONREAD (R7b).
- One compile/VM lane at a time; `pgrep -af qemu-system` first; never
  launch QEMU with a trailing `&` (use background task machinery).
- Default-off diagnostic knobs available: `rcu_head_trace=1` (R3 owner
  history), `sched_starve_probe=1` (P0 freeze telemetry),
  `rq_identify_linear_scan=1` (R7a control), `ext4_read_page_direct=1`
  (P3, pending promotion), `kde_pactl_probe=1`, `kde_network_status_sni=1`,
  `kde_plasma_systemtray=1`, `kde_pre_kwin_libinput_probe=1`,
  `KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE` (diagnostic-only),
  `WAYLAND_CHROMIUM_BUNDLED_GL=1` (emergency/diagnostic only — never the
  solution), kwin alloc/loader trace knobs.

## Lanes — Compact Status

### P0 — Chromium/YouTube freeze: CLOSED (2026-07-03)

Structural idle-pull fix landed (kernel `ef2dab6` line); M1 battery 3/3
responsive, probe-silent replay 3. Durable repro recipe for future M1
replays: boot the KDE image
(`DISPLAY_MODE=gtk USE_KVM=1 QEMU_GPU=virtio-vga-gl-primary QEMU_INPUT=virtio
QEMU_NET=1 QEMU_APPEND='root=/dev/disk0 video=1280x800 netsurf=0 webkit=0'`),
then from serial:
`XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root WAYLAND_DISPLAY=wayland-0
/bin/wayland-chromium https://www.youtube.com/` backgrounded, 15-min
liveness (`ALIVE_n` commands must EXECUTE). Evidence:
`build-x86_64/yt-mainpage-freeze-repro/`.

### P1 — Syscall overhead (M2/M3): steps 2c/2d OPEN (= N4)

2a (FS_BASE cache) + 2b (trapframe direct) landed; M2 1.65-1.94us.
Remaining identified fixed costs: 2c cpumask atomics skip, 2d CR0.TS
shadow. Implement together, one battery; then step 3 (gated default flip)
per Guardrails. CR-3 (%fs selector reload) is the latent ABI follow-up.

### P2 — Ordered page flip: steps 1-2 DONE, step 3 = N1

Step 1 direct-KMS A/B: 100 -> 118-125 FPS. Step 2 default flip LANDED
2026-07-04 (kernel `69310a3`, default-aware helper in
`virtio_gpu_scanout.c`; opt out `virtio_gpu_ordered_page_flip=0`):
default-on KDE pass (M4 2144), explicit-off control pass (M4 2122), video
36.8 -> 42.9. Archives `20260704T181500Z/183000Z/185000Z-q7-*`.
Step 3 (zero-copy present, M7 >= 55) is N1: every flip is still
`software_blit` with `native_present_credit=0` (fbstat), the sole
remaining M7 ceiling. N1 route (a) now has fail-closed launcher selectors
for rutabaga/gfxstream/blob plus a `QEMU_BIN` override, but this host
cannot run them: no hardware `/dev/dri/renderD*`, no rutabaga QEMU
device, and broken classic `*-gl` device help. Kernel blob resources and
host-visible mmap already exist, but creatable capsets are VIRGL/VIRGL2
only; scanout-blob/native-present and gfxstream/cross-domain admission are
future slices once a capable host route is available.

### P3 — Ext4 read-path serialization: first slice LANDED gated; N2 stopped

`ext4fs_pcache_read_page` held the per-mount esb mutex across device
waits, serializing all readers/faulters (0.86ms/fill, ~29s per video
window). Fix (kernel `2dee9b7`, gate `ext4_read_page_direct=1` default
OFF): resolve mapping under the lock, direct BIO, release before
`bio_await`; bcache-coherent (any cached block falls back/copies under
lock — dirty data lives in the lwext4 bcache via the write path).
A/B same workload: read_page_ms 29016 -> 14469 (-50%), lookup lock wait
-52%, ext4_fault_ms -66%, browser start 10.63s -> 6.17s (-42%); video
unchanged (present-path ceiling); fork/clone/cow + KDE battery green.
Archive `20260704T151500Z-p3-ext4-read-page-direct-chromium-kprofile-ab-pass`.

N2 guarded promotion attempt 2026-07-04: source flip made omitted token
default-on and explicit `ext4_read_page_direct=0` default-off control, then
ran the acceptance battery. Static/build PASS (`git diff --check`,
`git -C kernel diff --check`, kernel build). Nographic fork safety PASS:
`forktest` rc=1 known exhaustion signature, `clonetest` rc=0, `cowtest`
rc=0, boot cmdline had no direct-read token. KDE active-sample default-on
PASS with no direct-read token
(`20260704T181959Z-n2-ext4-direct-default-on-kde-active-sample-pass`;
M4 3518, M5 13420). Explicit-off control with
`QEMU_APPEND_EXTRA=ext4_read_page_direct=0` had one known visible-timeout
flake (`20260704T182300Z-n2-ext4-direct-explicit-off-kde-visible-timeout-rerun-needed`)
then PASS
(`20260704T182457Z-n2-ext4-direct-explicit-off-kde-active-sample-control-pass`;
M4 2365, M5 10015, cmdline proved token present). M9 launch-only default
real-GL guard PASS with software/bundled GL env unset
(`20260704T182703Z-n2-ext4-direct-default-on-chromium-launch-only-m9-pass`;
GPU/init/GL request errors 0, no `--use-gl`/ANGLE fallback args).
However, an extra default-on KDE active-sample rerun produced
`KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash`: `kwin_wayland`
#GP in `/usr/lib/x86_64-linux-gnu/libQt5Core.so.5`, no direct-read token
in cmdline
(`20260704T183012Z-n2-ext4-direct-default-on-kde-kwin-gp-stop`). Because
the N2 risk list includes KWin/ld.so recycled-byte corruption, the battery
is not clear/safe. The source flip was reverted before commit; current
state remains default-OFF with opt-in `ext4_read_page_direct=1`. Next
diagnostic is crash-focused cold-cache A/B (3-5 default-on vs explicit-off
KDE active-sample runs plus KWin fault/core/PTE or frame evidence if the
#GP recurs). Optional second slice = batch the remaining non-sequential
single-page fills (executable page-in pattern, ~71% of fills).

Post-stop cold-cache crash-focused A/B 2026-07-04 used the reverted
default-OFF source with explicit tokens in both arms
(`QEMU_APPEND_EXTRA=ext4_read_page_direct=1` vs `=0`) and
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, `QEMU_AUDIO_BACKEND=none`, with
software/bundled GL fallback env unset. Result before stop: ON PASS x4
(`20260704T183909Z-n2-ab-direct-read-on-run1-pass`,
`20260704T184121Z-n2-ab-direct-read-on-run2-pass`,
`20260704T184327Z-n2-ab-direct-read-on-run3-pass`,
`20260704T184741Z-n2-ab-direct-read-on-run4-pass`), OFF PASS x1
(`20260704T184534Z-n2-ab-direct-read-off-run1-pass`), OFF known
visible-timeout flake x1
(`20260704T185037Z-n2-ab-direct-read-off-run2-visible-timeout`), then ON
run 5 stopped on a new kernel page fault
(`20260704T185323Z-n2-ab-direct-read-on-run5-kernel-page-fault-stop`):
`*** KERNEL PAGE FAULT: cr2=0x1aafdd193 err=0x2
rip=0xffff80000039a34c`, backtrace line
`sig_trampoline.S:29: sig_trampoline+250691`, `Core: 2`, idle thread,
panic at `kernel/arch/x86_64/irq/trap.c:2023`. Symbol resolution maps the
RIP into kernel `_rodata`, not a normal function body. No A/B archive
matched the prior R5 KWin/QtCore signature (`kwin_wayland` #GP at
`libQt5Core.so.5` file offset `0xdbc9f`, bad pointer
`0x2d34365f3638782f`). Classification: not an R5 recurrence, but the
direct-read arm produced a new kernel fault before the recommended 5x5
could complete, so N2 default promotion remains blocked and default-OFF is
the required state.

Read-only fault mapping resolved the stop more precisely: RIP
`0xffff80000039a34c` is image offset `0x39a34c` inside `.rodata`
(`_rodata` range `0xffff80000035e000..0xffff8000003b7000`), adjacent to
ASCII strings near `Operations` / `TEST: synchronize_rcu()`. Decoding
those bytes yields a bogus write, matching `cr2=0x1aafdd193 err=0x2`
(supervisor write to a non-present low/user-looking address), so this is
corrupted control flow into read-only data, not an NX fault or real
`sig_trampoline` execution. `rbp=0xbefc6cc0` was outside the expected
kstack, so the unwind is secondary/bad; idle context is real but not a
root cause. No stack/register evidence currently places the CPU in
ext4/pcache/bio.

Diagnostic commit kernel `04b1ee2` (`kernel: add n2 direct-read fault
diagnostics`) makes a repeat actionable without changing defaults:
unrecoverable x86 kernel #PF now prints current task, CR3/page-table
identity, full registers, PTE walks for CR2/RIP/RSP/RBP in active/kernel/
current spaces, instruction bytes when mapped, stack words when RSP is on
the current kstack, a `.rodata` execution classifier, and an ext4 direct
read ring dump if enabled. `ext4_read_page_direct_debug=1` adds an opt-in
64-entry direct-read ring plus invariant checks around `bio_add_folio()`
and `bio_await()`. The same commit also adds an ON-only guard that falls
back for transient compound-folio node metadata while direct-read is
enabled; this does not affect default boots because
`ext4_read_page_direct` remains default-OFF.

Bounded diagnostic validation 2026-07-04 used:
`QEMU_APPEND_EXTRA='ext4_read_page_direct=1 ext4_read_page_direct_debug=1'`,
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, `QEMU_AUDIO_BACKEND=none`, and
the software/bundled GL fallback env unset. Kernel build PASS
(`cmake --build build-x86_64 --target kernel -j2`). Run 1 archived
`20260704T191028Z-n2-direct-read-debug-on-run1-artifact-timeout`: known
artifact-timeout class, no kernel #PF/panic/invariant dump markers, cmdline
proved both direct-read tokens. Run 2 archived
`20260704T191406Z-n2-direct-read-debug-on-run2-pass`: status code 0 /
`KDE-PLASMA-DESKTOP-SMOKE-DONE`, cmdline proved both direct-read tokens,
and log search found no `KERNEL PAGE FAULT`, `kernel-pf-context`,
`ext4-direct-read`, invariant, or panic markers. No explicit-off control
was run in this diagnostic slice because the ON-arm fault did not recur in
the two bounded ON runs.

### Q2 / R8 — Chromium real GL: DONE (validated 2026-07-04)

History: "window not visible" was never renderer admission (exonerated);
a real Mesa EGL attr-order bug was fixed (`5e3f4bebe`); the default-path
blocker was Chromium 150's passthrough decoder requiring the ANGLE
extension ladder. Direction settled by the user: REAL GL — no
software/bundled fallback.
Implementation (all in `ports/mesa/src`, HEAD `fb2724503`):
`xv6_angle_passthrough.c` + GLES dispatch for
`GL_ANGLE_robust_client_memory`, `GL_CHROMIUM_bind_generates_resource`,
`GL_ANGLE_client_arrays`, `GL_ANGLE_request_extension`;
`GL_CHROMIUM_copy_texture` (blit/shader paths, `mesacopytexture`
reducer); robust uniform length fix; context-gated
`GL_ANGLE_webgl_compatibility` via
`EGL_ANGLE_create_context_webgl_compatibility` (`mesaanglepassthrough`
reducer); `GL_KHR_debug` pre-existing.
Runtime validation 2026-07-04: image staging verified (fresh Mesa in
/lib, /lib/dri first, no LD shadowing); M9 launch-only PASS on the
default path — zero missing-GL fatals, zero `--use-gl=disabled`
fallbacks, GPU errors 0 (archive
`20260704T192000Z-q2-mesa-angle-ladder-launch-only-first-default-path-pass`);
full video 44.2 presentedFPS / 29.6% drops across 75s of real GL
(archive `20260704T195500Z-q2-real-gl-full-video-m7-44fps`).
Ground truth: Chrome 150's validating decoder is DEAD (runtime-refused),
so the ladder was the only real-GL route; bundled ANGLE stays
disabled/symlinked to guest Mesa. The earlier bundled-ANGLE failure class
is understood (guest Mesa exposes zero pbuffer EGL configs; surfaceless
works) — irrelevant while the default path holds.
Session-stability fixes that unblocked the Q2 gates (landed 07-04 by the
parallel round): wl_shm interface-version interposition fix in
`wayland-shm.c` + `kde-wayland-registry` reducer; KDE ABI closure for
libinput/libudev (gesture-event + `udev_*@LIBUDEV_183` symbols, input-seat
enumeration in the shim); pactl removed from readiness path;
plasmashell system tray + artificial network SNI now opt-in
(`kde_plasma_systemtray=1`, `kde_network_status_sni=1`) after the tray
`compactRepresentationItem` KCrash isolation.
OPEN residuals (park unless they block a gate): plasmashell system-tray
crash root cause (tray stays opt-in), one `xkbcomp` #GP class, one
liveness roundtrip race archive, GL conformance depth beyond the ladder.

### R2 — PCID stale-TLB: OPEN, retest first (= N6)

The R5 free-before-shootdown fix plausibly explains the "PCID corruption"
(noflush widens the same stale-TLB window; the 07-02 noflush default trial
reproduced exactly the KWin corruption signature R5 root-caused). Before
any reducer work: one guarded PCID/noflush default-flip KDE battery on the
fixed kernel.

### R3 — rcu_head_cache double-free: bounded-open, watch

5/5 clean trace-enabled provoke runs; `rcu_head_trace=1` diagnostic
staged default-off. May share the R5 root cause — correlate any recurrence
(one pre-R5-fix `slab_alloc: repairing corrupt freelist` line seen
07-04) with both lanes.

### R4 — pactl fault: CLOSED (payload-side, classified)

libpulse stack-probe/logging-recursion crash family; pactl removed from
the readiness path (default-off probe). Reopen only with a deterministic
reducer or kernel-corruption coupling.

### R5 — KWin startup corruption: ROOT-CAUSED + FIXED (2026-07-04)

One corruption family (24 sites, 11 libs, 7-10%/launch, attempt-1
cold-cache only): NULL-expected globals in the zero-fill tail of each
library's last RW file-backed page poisoned with recycled-frame residue.
Root cause: free-before-TLB-shootdown in `__vma_clear_range` mid-batch
overflow and `__vm_madvise_dontneed` (frames returned to the allocator
while stale translations existed; VA reuse let sibling threads scribble on
reallocated frames). Fix (kernel `67d7b5c`): shoot down the cleared span
before every early `page_free_anon_batch`; madvise per-segment flush
replaces the final flush; pgtable spinlock dropped around IPI-ack waits.
Battery green; M4 1883. Full forensics chain in the history file + git.
Statistical closure: 9/30+ clean attempt-1 launches accrued — count every
future battery's KWin launches.
LATENT hugepage bugs recorded, NOT fixed (all dormant behind
`vma_file_hugepage_collapse_enabled()==0`, and BLOCK re-enabling it):
partial-range 2MB over-free in `__vma_clear_range`; mprotect whole-2MB
protection bleed; madvise missing hugepage check; `page_free_anon_batch`
order-0-only; whole-folio COW head-page ref leak.
Secondary: KWin LibinputBackend nullptr deref (payload, -> N3);
crash-dump tooling gap (/core.PID never extracted, no symbolizer; fatal
dump could print the faulting VA's PTE/frame state).
Watch item: one unreproduced nographic boot stall (1/8, post-service
spawn) noted 07-04.

### R7 — Desktop responsiveness composite: a/b DONE, c partial, M8 = N5

R7a O(1) rq-assertion landed (11-15% of cycles removed; control knob
`rq_identify_linear_scan=1`). R7b inotify FIONREAD ABI fix landed and
closed (M8 345-512% -> ~85-105%; `linuxsyscallabitest inotify-fionread`
reducer). R7c timer_tick fastpath first slice landed; remaining
`__sched_timer` contention is bursty sub-jiffy expiry/insertion, not a
stuck owner. M8 debt is now attributed host-side to vCPU/KVM idle wake
cadence with guest PCs in `arch_idle_halt` (= N5, relates to N7 tick
work).

### R9 — Cursor out-of-range: OPEN (= N3)

User-visible: pointer does not track the host mouse. Triage order: (1)
EVIOCGABS absinfo vs the virtio tablet's 0..32767 (source audit says the
raw path normalizes to 0..65535 — verify at runtime); (2) raw ABS values
at screen edges vs host pointer; (3) cursor-plane transform under
`virtio_gpu_host_cursor_only=1`. Probe machinery is now harness-fixed and
source-only: `scripts/gpu/r9-cursor-contract-probe.expect`
(startup-injected `/r9-run.sh`, debugfs polling, QMP absolute injection after
`phase=armed`) — do NOT drive the probe over the interactive serial shell.
Dry-run `20260704T200628Z` passed with `qemu-dry-run.txt` after the R9
runner inherited the KDE ABI library path. Real probe `20260704T200703Z`
reached `armed_pid=57`, sent all five HMP monitor moves, and passed discovery:
udev enumerated event0/event1 (`count=2`) and libinput assigned `seat0`
(`r9_libinput_status rc=0 errno=0 reason=ready fd=6`), but HMP produced only
relative PS/2 samples: libinput reported 18 events with
`absolute_samples=0`, evdev collected no raw ABS samples, and `/dev/mouse`
clamped/repeated `flags=0 x=127 y=127`.

Coordinate slice result 2026-07-04: HMP routing was the failure layer.
Strict rerun `20260704T202206Z` selected `Mouse #3: QEMU Virtio Tablet
(absolute)` with HMP `mouse_set 3`, then still produced only PS/2 relative
samples and failed strictly with `reason=no_evdev_abs_samples`. The fixed
harness now opens `qemu-qmp.sock` and sends QMP `input-send-event` absolute
axis events in the virtio tablet's raw 0..32767 range, while retaining HMP
`info mice`/`mouse_set` logs as routing evidence and killing the spawned QEMU
process group on finish. Dry-run `20260704T202724Z` PASS; real R9
`20260704T202747Z` PASS: `/dev/mouse flags=1`; event1 EV_ABS samples
0/0, 32768/32768, 65535/65535, 16384/49150, 49150/16384; libinput absolute
samples 0/0, 640/400, 1279.980/799.988, 320/599.976, 959.961/200; summary
`evdev_abs_samples=10 mouse_samples=5 libinput_abs_samples=5 result=PASS
reason=coordinate_samples`.

Visible cursor-plane slice result 2026-07-04: new harness
`scripts/gpu/r9-cursor-visible-probe.expect` PASS at
`build-x86_64/r9-cursor-visible-probe-history/20260704T205234Z-r9-cursor-visible-probe`.
The run uses guest cursor mode (`qemu-dry-run.txt` has `show-cursor=off`)
with no `virtio_gpu_host_cursor_only=1`, preserves
`qemu-qmp-command.log` for `input-send-event` absolute X/Y injection and
`qemu-monitor-command.log` for `info mice` evidence only, and keeps the
strict coordinate contract alive (`/dev/mouse flags=1`,
`evdev_abs_samples=10`, `libinput_abs_samples=5`). Cursor traces prove
`virtio_gpu: cursor upload visible` and injected cursor transforms
0/0 -> 0/0, 32768/32768 -> 640/400, 65535/65535 -> 1279/799 with
`cursor_transform_out_of_range=0`. Crash-marker scan stayed clean for
panic/KWin/LibinputBackend nullptr signatures. `r9-visible-evidence.txt`
records `capture_visibility=NOT_PROVEN`: QEMU framebuffer captures do not
prove the GTK hardware cursor overlay, and this headless harness has no
deterministic host-window capture path. Guard runs after the harness-only
change passed and were archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T210241Z-r9-visible-desktop-interaction-pass`
(desktop-interaction-latency active sample, direct launch PASS) and
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T205909Z-r9-visible-chromium-launch-only-pass`
(chromium-video launch-only PASS). The visible cursor gate is closed; keep
the KWin LibinputBackend nullptr payload here as later/secondary unless this
probe reproduces a startup/input crash. Do not reopen image injection, seat
plumbing, or kernel signed-16 storage without new contradictory evidence.

## Verification Gates

- Static: `git diff --check` in every repo level.
- Build: `cmake --build build-x86_64 --target kernel -j$(nproc)`; after
  user/rootfs changes also `--target user` then `--target rootfs-refresh`;
  ports: `cmake --build build-x86_64/ports --target port-<name> -j2`.
- Runtime battery (see Work Style rule 2). KDE stability claims need
  `timeout 900+ scripts/gpu/kde-plasma-desktop-smoke.expect` with
  `KDE_SMOKE_REDUCER=desktop-interaction-latency` and active sample.
- Reducers available: chromium-video (+`KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1`,
  +`KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE=1`), kde-ready, pre-kwin-libinput,
  kde-wayland-registry, kde-wayland-liveness/client-liveness,
  desktop-interaction-latency; nographic: forktest/clonetest/cowtest,
  syscalltlb, wakestorm, linuxsyscallabitest, mesacopytexture,
  mesaanglepassthrough (host-staged).
- Archive every proof run to
  `build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/`
  (exclude `*.img`, delete the scratch image after copying).

## Evidence Archives

- `build-x86_64/kde-plasma-desktop-smoke-history/` — all KDE/Chromium runs
  (key 07-04 entries: `*-q0-kprofile-wait-fix-first-valid-m7-window`,
  `*-p3-ext4-read-page-direct-*`, `*-r5-tlb-fix-*`,
  `*-q7-ordered-pageflip-*`, `*-q2-mesa-angle-ladder-*`,
  `*-q2-real-gl-full-video-m7-44fps`, `*-n2-ext4-direct-*`).
- `build-x86_64/yt-mainpage-freeze-repro/` — P0/M1 replays.
- `build-x86_64/syscall-tlb-proof/`, `build-x86_64/pageflip-ordered-ab-proof/`,
  `build-x86_64/desktop-bottleneck-profile/`,
  `build-x86_64/r5-kwin-startup-classification/`.
- `docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md` —
  full pre-compaction plans (2026-07-02 and 2026-07-04 snapshots) with
  every evidence chain.

## Commit Policy

Commit PERIODICALLY at every verified checkpoint (completed slice, passing
gate, before a risky change, before ending). Lane-scoped messages with a
lane reference; never bundle unrelated changes. Submodules deepest-first
(`ports/mesa/src` -> `ports`; `kernel`; `user`), then the top-level pointer
update in the same checkpoint. `git status` at top level AND every
submodule at handover. NEVER push without explicit user approval.
