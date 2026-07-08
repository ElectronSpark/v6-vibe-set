# Active xv6 Work Plan

Last updated: 2026-07-08 (recut around unfinished work; the full pre-rewrite
plan is archived verbatim in
`docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`
under "ARCHIVED SNAPSHOT ... 2026-07-08").

Single-plan rule: this is the only live plan file. Verbose lane histories,
evidence chains, and superseded verdicts live append-only in the history
file. Every proof run is archived under
`build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/` (or
lane-specific proof dirs). Branch: `codex/host-linux-abi-shell-port-ff`.

## Mission

Make the KDE/Chromium desktop on x86_64 KVM+virgl stable and responsive,
approaching a comparable Linux VM on the same host class. GPU acceleration
is mandatory: real KVM+virgl GL only; software/llvmpipe/bundled fallback
invalidates a run. kprofile is the recurring metric after every
responsiveness iteration.

## THE BOTTLENECK (settled 2026-07-07/08 — do not relitigate)

Konsole shell-readiness is ~1.2-1.5s vs ~0.3s native. Fully decomposed:

- Dynamic loader = ~3% serial (~22ms; LD_DEBUG-proven). ALL loader/config
  levers CLOSED: XDG prune, hwcaps, LD path, single-lib stubs (QtMM,
  NewStuff, libimobiledevice), ld.so.cache, negative-dcache honor — each
  mechanically proven, none moved konsole_wait_ms.
- Kernel syscall/VFS cost (openat ~130us, ~40k faults/launch) is mostly
  PARALLEL, off the serial critical path — cutting it (neg-dcache -95%
  ENOENT) did not move konsole_wait_ms.
- ~60% of the serial pre-PTY window is BLOCKING WAIT on frame-gated Wayland
  round-trips (xdg-configure/frame-callback) + QDBus; ~15% Qt plugin dlopen
  (mostly imageformats a terminal never needs); ~23% Qt/KF5 init compute.
- The Wayland waits are NOT a missing kwin clock (vblank-paced flip events
  engage perfectly, gaps unchanged): kwin 5.27 RenderLoop is
  repaint-on-damage; the gaps are DAMAGE-ARRIVAL gaps. LOCALIZED 2026-07-08
  (U2, paired konsole+kwin trace): each 58-139ms frame-callback wait is ~96%
  kwin SCHEDULE latency (damage/commit seen -> repaint start), ~4% kwin repaint
  ioctl, and ~0% konsole render (client commits then re-blocks instantly,
  write_to_poll_us~0). So the remaining serial lever is kwin's damage->repaint
  scheduling latency (NOT the client commit chain, which is exonerated), plus
  round-trip COUNT as a secondary axis.

## UNFINISHED WORK — priority queue

- U1 (KERNEL PART DONE 2026-07-08 — metadata hypothesis retired): the
  drmGetDevice2 device-metadata surface is ALREADY implemented+committed in
  the kernel (`kernel/vfs/sysfs/*`: `/sys/dev/char/226:0|226:128/device/{vendor
  0x1af4,device 0x1050,subsystem_vendor,subsystem_device,revision,class
  0x030000,uevent,subsystem->/sys/bus/pci,drm/}`). Re-verified on the current
  image via `drmgpuprobe`: `drmGetDevices2=1`, `bus=0`(DRM_BUS_PCI),
  `nodes=0x5`(primary+render), render node type=2, vendor 0x1af4/device 0x1050;
  also drm_info shows `Device: PCI 1af4:1050` for both nodes
  (proof-upstream-drm-tools.log 2026-06-12). The task's metadata-less-fallback
  evidence (node=-1, "failed to retrieve device information") is stale
  (virgl-kms-seconds-validate.log 2026-06-06, PREDATES the sysfs build-out);
  current mesa source no longer contains that fallback string. Proof archived:
  `kde-plasma-desktop-smoke-history/20260708T150347Z-u1-drmgetdevice2-validation/`.
  IMPLICATION: the residual image-layout EGL race is NOT a metadata problem
  (sysfs is kernel-generated, deterministic, image-independent — always
  present). REMAINING U1 = the secondary lever only: EGL-init retry / readiness
  gate in the session launcher (userspace, not kernel). (Out-of-scope note:
  drmgpuprobe's standalone gbm_bo_create fails EINVAL on USE_WRITE|LINEAR; the
  mesa EGL/platform_drm gbm path is unaffected and holds real GL.)
  RESIDUAL IMPLEMENTED 2026-07-08 (OFFLINE, compile-clean, boot-validation
  pending): userspace EGL/DRI2 readiness gate before plasmashell exec.
  ROOT CAUSE from the pass/fail pair (kwin logs are decisive): FAIL run
  (`...20260707T192438Z-negdcache-control-rep1`) kwin's FIRST output init
  fails — `kwin_scene_opengl: "Could not create gbm device"` +
  `"Could not initialize egl"` + `kwin_wayland_drm: Failed to find a working
  setup for new outputs!` — then kwin RETRIES (2nd `No backend specified,
  automatically choosing drm`) and succeeds with virgl D3D12 GL; plasmashell,
  racing that same window, hits `failed to create dri2 screen` x4 +
  `Failed to initialize EGL display 3001` and drops to QtQuick software (Qt has
  no retry), only recovering when the whole session-child relaunches (2nd
  `graphics EGL_PLATFORM=` block at line 60 -> real GL). PASS run
  (`...20260707T171835Z-ldsocache-control-rep1`) kwin's first output init
  succeeds directly, no gbm/egl failure, plasmashell gets GL first try. So the
  race is on the shared mesa DRI2/virgl SCREEN-CREATION substrate on the render
  node (both GBM and Qt-Wayland clients fail it together in the FAIL run); it
  warms after the first successful creation (why kwin's retry and the 2nd
  plasmashell both succeed). PREDICATE CHOSEN: "can a fresh process create a
  DRI2/virgl EGL screen on renderD128 right now" — the exact failing op —
  realized by new helper `scripts/image/kde-egl-readiness-probe.c` (open render
  node -> gbm_create_device -> eglGetPlatformDisplay(GBM) -> eglInitialize ->
  eglChooseConfig -> eglCreateContext + surfaceless eglMakeCurrent +
  glGetString; deliberately NO gbm_bo_create per the out-of-scope note). Staged
  into the guest via new `stage_kde_egl_readiness_probe` in make-rootfs.sh
  (pkg-config egl/glesv2/gbm/gl against the guest sysroot, mirroring
  stage_kde_drm_probe; warn+skip if dev files absent). GATE: `kde-plasma-
  session-child` runs the probe in a retry loop (10x, 200ms backoff, 8s/attempt
  timeout) right before exec'ing plasmashell (scoped to plasmashell only, not
  kwin/session), emitting `egl-readiness-gate status=READY|RETRY|TIMEOUT` lines;
  it PROCEEDS ANYWAY after the budget (never wedges — plasmashell keeps its
  software fallback + KCrash restart net). DEFAULT OFF (opt-in): cmdline
  `kde_plasmashell_egl_ready_gate=1` or env `KDE_PLASMASHELL_EGL_READY_GATE=1`.
  Justification for default-off: the probe forks a fresh process that dlopens
  mesa, so the no-race path cost is NOT provably ~0 (violates the "default-ON
  only if ~0" rule + the no-un-gated-flip guardrail); promotion to default-on
  needs the standard same-session A/B with owner sign-off (same pattern as U3).
  COMPILE PROOF: `kde-egl-readiness-probe.c` built with the production pkg-
  config command (`cc -O2 -Wall -Wextra ... $(pkg-config --cflags --libs egl
  glesv2 gbm gl) -Wl,--allow-shlib-undefined`) against build-x86_64/sysroot,
  clean; `kde-plasma-session-child.c` rebuilt with its production command
  (`cc -O2 -Wall -Wextra`), clean; `make-rootfs.sh` `bash -n` clean. VALIDATION
  (next image rebuild, do not force): watch op-caution-2 signature. Acceptance =
  no-regression now (gate default-off => byte-behavior unchanged unless
  enabled) and, when the signature recurs, enable the gate and confirm the
  `egl-readiness-gate status=RETRY...->READY` line appears and plasmashell then
  gets real GL (no software fallback). NOTE: the race is image-layout-dependent
  and cannot be forced on demand, so a green boot does not by itself exercise
  the retry — the gate is a standing guard whose engagement is only observable
  if/when the layout reproduces the race.
- U2 (M4 serial lever) — MEASURED 2026-07-08 (one boot, both traces
  co-enabled; LARGER HALF = kwin-schedule latency, ~96%): co-enabled the
  konsole wayland event trace (KDE_APP_LAUNCH_PROBE_KONSOLE_WAYLAND_EVENT_TRACE
  =1 + LOADER_TRACE=1) AND the kwin ioctl-trace shim (kde_kwin_ioctl_trace=1)
  in ONE direct-launch boot (20260708T162206Z-guiperf-phase3-u2-split; DONE,
  crash 0). SPLIT of the 58-139ms frame-callback-wait class (konsole poll(fd=3)
  blocks; n=6 in-band, median 77ms, samples 66/67/75/79/79/107ms):
    - kwin SCHEDULE latency (commit/damage seen -> repaint start) = 73.8ms = 95.8%
    - kwin REPAINT cost (PAGE_FLIP ioctl duration, median)        =  3.2ms =  4.2%
    - konsole RENDER latency (frame-callback recv -> next commit) = ~0.0ms =  0.0%
  Method: konsole poll(fd=3) elapsed_us = time the client is BLOCKED waiting
  for the compositor's frame-done (kwin side); write_to_poll_us (commit->
  re-poll gap) = 0 median, 1/31 nonzero (max 159us) => the konsole client
  commits and IMMEDIATELY re-blocks, so client render is NOT a serial lever.
  kwin repaint ioctl (PAGE_FLIP) is cheap (~3ms). The wait is dominated by
  kwin's schedule latency; the PAGE_FLIP cadence gap median (~67ms) matches the
  ~77ms wait, i.e. kwin services damage on a ~67ms cadence rather than a
  vsync-tight one. FINDING (updates THE BOTTLENECK): the earlier "client commit
  chain (konsole render after frame-callback) is the remaining serial lever"
  hypothesis is CONTRADICTED — konsole render ~0; the lever is kwin
  DAMAGE-ARRIVAL -> REPAINT-START scheduling latency. NEXT M4 ACTION (attack the
  larger half): attack kwin RenderLoop schedule latency — the damage->repaint-
  start delay. Most likely mechanism: kwin gates the next repaint on the prior
  frame's present-completion (flip-done) event, which is paced slowly on xv6
  (ties to M7 presentedFPS=51 and the U5 atomic flip-complete event path). The
  round-trip-COUNT axis (Qt-Wayland init) remains secondary but is a count, not
  a per-wait cost — the per-wait cost is now firmly kwin-schedule.
  ROOT-CAUSED 2026-07-08 (OFFLINE re-analysis of the U2-split archive; design in
  scratchpad kwin-schedule-latency-design.md): the 67ms cadence / 73.8ms schedule
  latency is COMPOSITING-COST bound, NOT clock/timer-anchor bound. The DRM
  PAGE_FLIP ioctl does the ENTIRE present synchronously inside the ioctl
  (gpu_kms_present_fb -> fb_blit_from_bo_format, fence returned but IGNORED;
  fb_drm_kms_properties.c:734,804,814) and BLOCKS 10-26ms per flip in the
  konsole-launch bursts (trace: fb111 dur=20887us, fb120 22405us, fb132 25980us —
  the "3.2ms median" is diluted by idle single-flips). That inflates kwin
  RenderLoop expectedCompositingTime above the 16.67ms refresh, so scheduleRepaint
  cannot land n=1 vblank and re-rounds to n~=3-4 (50-67ms); frame-callback wait =
  one in-flight present (~20ms) + one next cadence (~47ms) ~= 67ms. (a)/(b)/(c)
  verdict: mechanism is (a) timer-schedules-out but ROOT is cost, not stale
  timestamp — the legacy flip-complete carries a grid-snapped submit-time ts <= now
  (fb_drm_core_kms.c:1274-1331) that schedules only 1 vblank out, so phase is not
  the driver. RECONCILES the vblank-paced null A/B: that gate deferred the
  completion EVENT + fixed its phase but kept the present SYNCHRONOUS, so it left
  expectedCompositingTime (hence gaps) unchanged -> ELIMINATES the entire
  timestamp/clock-anchor hypothesis family. (c) event-read starvation is NOT
  confirmable: the trace has ZERO EVTREAD/FLIP_COMPLETE lines despite flags=0x1 on
  every flip because kwin INHERITED the DRM fd (fd=21) across fork/exec so the
  child-side shim never saw the /dev/dri open (path_is_dri/mark_dri_fd) -> read
  tracking blind. BLIND SPOT CLOSED 2026-07-08 (OFFLINE, shim-only, build+host-
  smoke proven, NO boot): the ioctl-trace shim now seeds DRM fds two ways so
  read()/EVTREAD tracking no longer depends on having seen the open —
  (1) classify-on-first-ioctl (primary, robust): the ioctl wrapper marks any fd
  receiving a DRM-type ('d'=0x64 _IOC type) ioctl as a DRM fd permanently, so an
  inherited fd is classified from its first DRM ioctl (broader than the four
  decoded ops; zero scan, zero startup cost, works without procfs); (2) a
  constructor /proc/self/fd readlink scan (fds 0..255) that seeds fds already
  open at preload time — verified viable because this guest's procfs resolves fd
  symlinks for char devices to "/dev/DEVNAME" and the DRM nodes register as
  "dri/card0"/"dri/renderD128" so readlink yields "/dev/dri/..." and path_is_dri
  matches (kernel/kernel/vfs/procfs/inode.c:875-928,
  kernel/kernel/dev/fb/fb_init_panic.c:31,51). A phase=seed banner
  (proc_seeded_fds + classify_on_first_ioctl=active), per-fd DRMFD_SEEDED lines
  (via=proc|ioctl), and proc_seeded_fds/ioctl_seeded_fds in the summary let the
  next A/B confirm closure from the log alone. Host smoke: a pipe fd unseen by
  the open wrappers produced NO EVTREAD before its first DRM ioctl, was seeded by
  it (DRMFD_SEEDED via=ioctl), then a subsequent read produced EVTREAD
  FLIP_COMPLETE; a non-DRM (FIONREAD) ioctl left its fd untracked. kwin is
  PREBUILT (kde-runtime overlay; no ports/ kwin, make-rootfs builds only the
  screenshot probe) => kwin C++ patches OFF the table; levers are kernel + env
  only. RECOMMENDED FIRST SLICE (kernel, gated virtio_gpu_async_present=1 default
  OFF): submit the present to the virgl ring and RETURN from PAGE_FLIP in ~us, queue
  DRM_EVENT_FLIP_COMPLETE on fence-retire instead of inline -> drops per-cycle
  compositing time 10-26ms -> <2ms so the RenderLoop paces at 1 vblank. VALIDATION
  targets: PAGE_FLIP loaded duration_us 10-26ms -> <2ms; cadence gap ~47-67ms ->
  ~16-20ms; konsole frame-callback wait 66-107ms -> 16-20ms; schedule median
  73.8ms -> ~16-20ms; correctness risk = torn/half present (double-buffer + hold BO
  ref to fence), so screenshot-diff guardrail is load-bearing. Composes with (does
  not replace) vblank-paced pacing: pacing fixed event TIMING, this fixes event
  COST.
  SLICE IMPLEMENTED 2026-07-08 (OFFLINE, compile-proof clean, NO boot/build —
  VM+build lanes owned by the crash root-cause worker): gate
  `virtio_gpu_async_present=1` (DEFAULT OFF, cached-cmdline pattern like
  async_cursor). Legacy DRM_IOCTL_MODE_PAGE_FLIP with PAGE_FLIP_EVENT now (gate
  ON) pins the FB's BO via fb_bo_get_owned, snapshots all present params, hands
  the blit to a new single-threaded "fb-present" workqueue and returns in ~us;
  the worker runs the identical present (shared helpers
  gpu_kms_present_lookup_params + gpu_kms_present_blit, factored byte-for-byte
  out of gpu_kms_present_fb), drops the BO ref only after the fenced blit
  returns (host done reading — R5), samples the REAL completion-time vblank
  seq/ts, and queues DRM_EVENT_FLIP_COMPLETE + clears the single in-flight slot
  in ONE fb_state.lock section (no spurious -EBUSY on an instant next flip; ring
  insertion order = submission order). Overlapping flip while one is in flight
  => -EBUSY (legacy DRM pending-flip semantics; also the tearing/R5 guard — a
  flip targeting the still-scanned-out BO is never accepted; kwin
  double/triple-buffers per fb111/120/132 trace alternation so this is a safety
  net, not a hot path). EAGAIN ring backpressure guard unchanged and still runs
  BEFORE accept. Teardown: worker addresses the owner by id and re-resolves
  under fb_state.lock (paced-flip pattern) — fd close mid-present is a benign
  drop, BO ref cannot leak (worker always puts). Every failure path delivers or
  falls back sync (queue_work failure = undo claim + sync present;
  gate OFF/no-wq/lookup-fail = sync path byte-identical). COMPOSITION with
  virtio_gpu_vblank_paced_flip: both ON => worker completes the present, then
  paces the completion event to the next synthetic edge AFTER real completion
  (new gpu_drm_pace_completed_flip_by_id; kvmalloc/arm failure => immediate
  delivery, never dropped). Atomic-commit path deliberately left synchronous:
  it shares gpu_kms_present_fb but its out-fence cancel/arm lifecycle is tied to
  present success inside the ioctl (shared cost NOT low) and kwin 5.27 never
  uses AMS in VMs (U5); revisit with kwin 6.x. Files (kernel submodule):
  fb_drm_kms_properties.c (helpers + slot + worker + accept),
  fb_kms_atomic.c (:399 accept hook), fb_drm_core_kms.c (deliver/pace-by-id
  helpers), fb_init_panic.c (wq init), fb_common.c (workqueue.h include),
  inc/dev/fb.h (now 5 END-appended stats: present_async_submits_total,
  present_async_complete_total, present_async_fallback_sync_total,
  present_async_bo_hold_max_us, present_async_errors_total).
  ADVERSARIAL-REVIEW FIX-UP 2026-07-08 (OFFLINE, compile-proof clean, NO
  boot/build): applied the GO review's two should-fixes + nit to the
  uncommitted slice. (SF#2) blit failure in the worker no longer delivers a
  success-looking completion: it captures gpu_kms_present_blit's return and on
  nonzero STILL delivers DRM_EVENT_FLIP_COMPLETE (deliberate liveness — kwin
  must never freeze) but does NOT advance current_kms_fb_id, does NOT bump the
  success/lane counters or present_async_complete_total, and bumps a NEW
  END-appended counter present_async_errors_total (5th async stat). (SF#3) the
  accept-time ring check now RESERVES a completion slot: a single global
  fb_state.lock-guarded reservation record holds a per-owner COUNT of ring
  slots owed to pending async completions; gpu_drm_event_queue_locked rejects
  any NON-FLIP_COMPLETE enqueue (VBLANK/QUEUE_SEQUENCE) once (used+reserved)
  would exceed the 16-deep ring, so a burst can never displace the completion
  kwin blocks on, while a FLIP_COMPLETE uses the full ring to consume its
  reserved slot (the only completion class produced for the flipping owner
  while the gate is on). A single count (not a per-owner field) is used because
  the owner struct lives outside this slice's editable files and the
  single-CRTC/single-compositor invariant means only one owner reserves at a
  time (a second owner degrades to unreserved). Reservation is released on
  EVERY terminal path — queue_work-failure fallback, non-paced worker
  completion, paced timer callback (rides pf->owns_async_reservation), and the
  paced immediate-deliver fallbacks — so it never leaks or double-releases
  (unreserve is idempotent past zero) and needs no owner-teardown drain hook
  (it is not stored in the owner; the worker/timer always run). (NIT)
  present_async_complete_total comment clarified: counts completed presents,
  not delivered events. Files touched: fb_drm_core_kms.c (reservation record +
  3 helpers + queue headroom + timer-cb release + pace-by-id reserved param),
  fb_drm_kms_properties.c (slot.reserved, worker blit-result branch, release,
  accept acquire), inc/dev/fb.h. COMPILE PROOF: dev/fb module.c TU rebuilt
  with the production -Wall -Werror command (scratchpad .o, in-tree untouched),
  clean; git -C kernel diff --check clean. RMFB pre-existing-bug check: NONE — RMFB only
  unpins (TTM moves here are metadata-only, ttm_metadata_only_moves; backing
  pages are freed only at dead&&refs==0 in fb_bo_put), and both sync+async
  presents hold an fb_bo_get_owned ref across the blit. COMPILE PROOF
  (translation-unit trick, production commands from
  build-x86_64/kernel/build/compile_commands.json, -Wall -Werror): dev/fb
  module.c TU and virtio_gpu.c TU both clean, outputs to scratchpad only
  (in-tree .o untouched); git -C kernel diff --check clean. VALIDATION SPEC
  (conductor, single boot A=baseline B=virtio_gpu_async_present=1, both traces
  armed kde_kwin_ioctl_trace=1 + KONSOLE_WAYLAND_EVENT_TRACE=1; close the
  EVTREAD blind spot first — mark any fd receiving DRM_IOCTL_MODE_* as DRI in
  the shim): (1) engagement: fbstat present_async_submits_total ==
  complete_total > 0, fallback_sync_total ~0; (2) PAGE_FLIP loaded duration_us
  10-26ms -> <2ms; (3) PAGE_FLIP gap_us cadence ~47-67ms -> ~16-20ms; (4)
  konsole frame-callback elapsed_us 66-107ms -> 16-20ms; kwin schedule median
  73.8ms -> ~16-20ms; M4 konsole_wait warm expected real drop; (5) GUARDRAILS
  (mandatory): screenshot-diff metric unchanged (torn/half-present = the
  tearing risk this design carries), virgl/D3D12 real GL both arms, qtquick 0
  violations, crash grep 0, forktest rc=1/clonetest rc=0/cowtest rc=0 same boot,
  M5/M8 within noise, EVTREAD submit->read deltas present in B; (6) kill
  criterion: duration_us drops but gap_us/frame-callback do not -> residual is
  GL-composite cost or event-read starvation, pivot to EVTREAD data; (7) after
  the solo gate is proven, A/B the composed mode (async_present=1 +
  vblank_paced_flip=1): expect edge-paced events with the cadence win retained.
  A/B VERDICT 2026-07-08 (gated ASYNC-PRESENT, DECISIVE — MECHANISM WIN + KILL
  CRITERION TRIGGERED, responsiveness NULL): CONTROL x2 (kde_kwin_ioctl_trace=1)
  vs TREATMENT x2 (+virtio_gpu_async_present=1), full desktop-interaction reducer
  (guiperf phase-1 recipe), kwin ioctl-trace shim armed both arms, both tokens
  verified in booted cmdline. Kernel rebuilt from committed d052c5d; fs.img
  refreshed via `ninja rootfs-refresh` (correct overlay set baked by cmake:
  host-gui:webkit-media:kde-runtime:gameboy-roms:gpup-umd); debugfs-verified
  kwin_wayland + kwin-ioctl-trace-preload.so + /bin/dcachetest baked. 4 boots,
  crash 0, no lingering qemu, tree clean. Archives:
  `kde-plasma-desktop-smoke-history/20260708T200049Z-asyncpresent-control-rep1`,
  `...T200250Z-...-control-rep2`, `...T200444Z-...-treatment-rep1`,
  `...T200637Z-...-treatment-rep2`.
  EVTREAD BLIND SPOT CONFIRMED CLOSED: shim banner shows classify-on-first-ioctl
  seeding fds 19/20/21 (DRMFD_SEEDED via=ioctl); 117-119 EVTREAD +
  117-119 FLIP_COMPLETE lines captured in EVERY run (vs ZERO in the U2-split
  archive). proc_seeded_fds=0 (constructor scan seeded nothing here), ioctl
  classify is the mechanism that fired.
  A/B TABLE (control r1/r2 -> treatment r1/r2):
    - engagement PROOF (shim): PAGE_FLIP duration_us MEDIAN 925/1017 -> 73/64;
      loaded-burst MAX 24398/22961us (18-24ms class) -> 650/425us; loaded-burst
      top5 18.3-24.4ms -> 156-650us. Async completion path proven: EVTREAD
      since_submit_us MEDIAN 531/585 -> 3056/2396 (completion now delivered on
      fence-retire ~2.4-3ms AFTER the ~64us flip return, not inline). 1:1
      PAGE_FLIP:FLIP_COMPLETE, every flip ret=0 errno=0 => present_async_errors
      effectively 0 (fbstat counters NOT capturable in this reducer, same as U4;
      the shim duration collapse + 1:1 completion is the engagement proof of
      record).
    - PAGE_FLIP loaded duration 10-26ms -> <2ms: MET/EXCEEDED (treatment
      loaded-burst max 425-650us, well under 2ms).
    - cadence gap_us (active 10-120ms) MEDIAN 32574/34345 -> 37021/33135;
      p90 71126/72414 -> 72683/62816. UNCHANGED (target 16-20ms NOT MET).
    - konsole frame-callback wait / kwin schedule (inferred from cadence, which
      is unchanged): NOT MET.
    - konsole_wait_ms WARM 1191/1192 -> 1186/1202 (dead flat); COLD 1774/1507 ->
      1506/1505 (control r1 1774 is the high outlier; within the settled cold
      band + op-caution-4 noise). NO M4 win.
    - hover first_changed_visual_ms MEDIAN 448/460 -> 442/416 (overlapping, noise).
    - M5 first_visible_ms 10454/6573 -> 6292/6493 (control r1 outlier; treatment
      inside the 6.2-6.6s band; within noise, no regression).
    GUARDRAILS ALL GREEN: real GL virgl(D3D12) both arms, qtquick 0 violations,
    crash 0, no image-layout race (the single `failed to create dri2 screen` is
    the host GTK/EGL warning at run.log:6 pre-guest-boot, present in CONTROL too;
    guest plasma-child dri2 fail = 0 both arms, op-caution-2 NOT triggered, no
    rebuild needed). TEARING GUARDRAIL PASS: direct-launch-diff = 1001730
    (byte-identical) in ALL FOUR runs = the settled baseline; input-diff 0 in
    both treatment reps; treatment vs control direct-launch screenshots
    pixel-identical in layout (only the wall-clock differs) — NO new visual
    corruption class (no torn/garbled frames).
  VERDICT: the synchronous-present cost IS on the PAGE_FLIP ioctl and this slice
  removes it cleanly and safely (24ms->0.65ms, no tearing, no errors) — but that
  cost is NOT what gates the RenderLoop cadence. This is EXACTLY kill-criterion
  (6): duration_us drops ~99% while gap_us / frame-callback / konsole_wait / hover
  are all unchanged within noise (honest n=2). CONTRADICTS the plan's OFFLINE
  root-cause hypothesis that the 67ms cadence is expectedCompositingTime-bound
  (synchronous flip inflating it) — with per-flip present now ~64us ioctl +
  ~3ms fenced completion (both << 16.67ms vblank), the cadence did not tighten,
  so expectedCompositingTime is NOT the cadence driver. PIVOT (EVTREAD data
  captured): the residual damage->repaint schedule latency is bound by something
  OTHER than present ioctl cost — candidates are kwin RenderLoop scheduling /
  damage-arrival timing, client GL render/glFinish on the plasmashell context, or
  vblank-event phase/count. Event-read starvation is NOT the cause (117:117
  completions, all read back; median delivery ~2.4-3ms). PROMOTION: NONE — no
  responsiveness win, so no default-on justification (keep gated default-OFF,
  same posture as vblank-paced and async-cursor). The correctness/tearing result
  is clean, so the gate is a safe standing opt-in; the composed-mode A/B (step 7,
  async_present + vblank_paced) is now moot for responsiveness since the solo
  cadence null removes the premise, and is deferred unless the pivot reopens it.
- U3 (DONE 2026-07-08 — opt-in gate landed, A/B PASS all criteria): Qt
  imageformats prune for konsole. Gate
  `KDE_APP_LAUNCH_PROBE_QT_MINIMAL_IMAGEFORMATS=1` (default 0 = byte-identical)
  adds `stage_qt_minimal_imageformats_if_enabled` to the smoke expect: debugfs
  -w removes the 18 exotic kimg_*.so from the PER-RUN fs.img copy (committed
  image untouched), keeps libqsvg/libqjpeg/libqico/libqgif; evidence `ls` in
  qt-minimal-imageformats-stage.log. NOTE: QT_PLUGIN_PATH scoping was analyzed
  and REJECTED — it is additive, cannot prune (design doc §2a). A/B (loader
  trace ON both arms, direct-launch konsole-only kprofile): dlopen count
  64->28 cold AND warm (exactly as designed); imageformats dlopen time cold
  70.8->4.0ms (-66.8), warm 56.4->4.3ms (-52.1); total dlopen time cold
  140->62ms, warm 117->50ms; kimg_* trace lines 36->0; shell-ready+bash-prompt
  fire both arms; konsole_wait 1305/1486 (ctl) vs 1192/1387 (prune) — within
  band, NOT the judge; screenshot diff metric identical (1001730) both arms
  (svg icons intact); virgl/D3D12 GL both; qtquick-policy 0 violations; crash
  grep 0. Control first attempt hit FM19 visible-timeout (crash grep 0),
  rerun-once passed. Archives:
  `kde-plasma-desktop-smoke-history/20260708T151810Z-u3-imageformats-control/`,
  `...T152010Z-u3-imageformats-pruned/`,
  `...T151611Z-u3-imageformats-control-attempt1-fm19-visible-timeout/`.
  Verdict: mechanism proven, deterministic win ~55-67ms of serial dlopen off
  the launch path. Promotion path (later, needs owner sign-off): bake the
  prune into make-rootfs (or ship the qt.conf per-app variant, design §2c);
  keep gate opt-in until the M4 lever stack (U2) decides what is worth
  compounding.
- U4 (PERF VERDICT DONE 2026-07-08 — engagement proven, NO hover win at n=2):
  async-cursor hover re-A/B at n=2 vs n=2 on a clean environment (all gates
  green: qtquick 0 violations, virgl/D3D12 GL, crash 0). Full desktop-
  interaction reducer, kwin ioctl-trace shim armed both arms. ENGAGEMENT PROOF
  (kwin ioctl shim, decisive): CURSOR2 (cursor-image upload) max duration
  collapses 27113/32164us (baseline OFF) -> 834/8557us (async ON); cursor
  sub-1ms 47/49 -> 49/49(rep1). This IS the 17-36ms->~0 upload the gate
  promised, now measured directly off the compositor ioctl path. HOVER RESULT:
  hover_median_visual_ms baseline {475.5, 487.0} vs async {491.5, 458.5} —
  fully overlapping, NO separation. konsole warm {1192,1205} vs {1191,1184};
  first_visible {6335,6541} vs {6170,6632} — all within noise. VERDICT: the
  cursor-upload cost is real and async-cursor eliminates it, but that cost is
  NOT on the hover-visual-latency critical path (the ~450-490ms hover floor is
  frame-pacing/sample-interval bound, not cursor-upload bound). So at n=2 vs
  n=2 in a clean environment async-cursor shows NO hover responsiveness win —
  within noise. Correctness GO stands (unchanged); promotion to default-on is
  NOT justified by a responsiveness win (keep opt-in). fbstat cursor_async
  counters were not capturable in the desktop-interaction reducer (kde-fbstat
  log came back "missing guest_path"); the shim CURSOR2 collapse is the
  engagement proof of record. Archives: guiperf phase1-baseline-rep1/rep2
  (20260708T161135Z/T161429Z), phase2-asynccursor-rep1/rep2
  (20260708T161715Z/T161936Z).
  STAGING APPLIED 2026-07-08 (was READY-TO-APPLY): the three kwin-ioctl-trace
  hooks below are now landed and boot-proven engaged — make-rootfs.sh builds
  kwin-ioctl-trace-preload.so into the abi-libs dir; kde-session.c
  spawn_kwin_child prepends it to the child LD_PRELOAD gated on
  kde_kwin_ioctl_trace=1 (default off, composes with alloc-trace/compat);
  smoke.expect plumbs guest/host kwin_ioctl_trace_log (set vars, globals,
  debugfs-rm, dump_optional_guest_artifact, cleanup rm). Image rebuilt with the
  full overlay set (host-gui:webkit-media:kde-runtime:gameboy-roms:gpup-umd);
  shim + kde-session injection + U1 egl-readiness-probe all verified baked in.
  - Reusable kwin ioctl-timing LD_PRELOAD shim: NOW REPO-OWNED (2026-07-08),
    replacing the three throwaway per-worker versions. Sources:
    `scripts/image/kwin-ioctl-trace-preload.c` +
    `scripts/image/build-kwin-ioctl-trace.sh` (deterministic build modeled on
    build-qtmm-shim.sh: SOURCE_DATE_EPOCH=0, -fno-ident, -Wl,--build-id=none,
    ffile-prefix-map, proof file with sha256/readelf -d/nm -D). Justified
    flag delta vs qtmm: this is a real interposer that calls libc+dlfcn, so it
    links libc/libdl (`-ldl`) instead of -nostdlib/-nostartfiles/-nodefaultlibs
    and has no version-script/-soname (an LD_PRELOAD object needs neither an
    exported-symbol allowlist nor a SONAME). Forbidden-DT_NEEDED check is
    inverted: libc/libdl/ld-linux REQUIRED, any Qt/KF5/drm/pulse/glib NEEDED
    fails the build; the proof also asserts the 5 interposer symbols are
    exported.
  - What it captures: wraps libc ioctl() (dlsym RTLD_NEXT, variadic single-arg
    forward) and CLOCK_MONOTONIC-stamps DRM_IOCTL_MODE_PAGE_FLIP /
    _MODE_ATOMIC / _MODE_CURSOR / _MODE_CURSOR2 — one line/op with op name, fd,
    duration_us, gap-since-last-same-op_us, ret/errno, plus flags+fb_id for
    PAGE_FLIP and flags for ATOMIC (struct fields read per the uabi/drm.h
    *_compat layouts; ioctl numbers + the few structs defined locally with a
    comment referencing kernel/kernel/inc/uabi/drm.h:60,70,81,82). Also tracks
    fds opened on /dev/dri/* (open/open64/openat/openat64 wrap; close wrap
    clears) PLUS inherited/pre-existing DRM fds the open wrappers never saw —
    seeded by classify-on-first-ioctl (any DRM-type 'd'=0x64 ioctl marks its fd)
    and a constructor /proc/self/fd scan (see U2 "BLIND SPOT CLOSED"); banner
    reports proc_seeded_fds + classify_on_first_ioctl=active, per-fd DRMFD_SEEDED
    lines record which seeder fired. Wraps read() ONLY on tracked fds to emit
    EVTREAD lines for DRM_EVENT_FLIP_COMPLETE/VBLANK with a since-last-submit_us
    delta (the submit/completion pairing that made the vblank-pacing A/B
    decisive).
    Env knobs: KWIN_IOCTL_TRACE_LOG (default /kde-kwin-ioctl-trace.log),
    KWIN_IOCTL_TRACE_MIN_US (default 0). Constructor banner + best-effort
    destructor summary (per-call lines are authoritative; kwin is SIGKILLed so
    the summary must not be relied on). Safety: __thread recursion guard,
    static buffers (no hot-path malloc), single per-line write() to the
    O_APPEND fd (crash-safe under SIGKILL, no interleave < PIPE_BUF), errno
    save/restore, fail-open to passthrough on every dlsym/log failure.
  - Compile/proof evidence (host cc, scratchpad out-dir): builds warning-clean
    under -Wall -Wextra; sha256 reproducible across two builds
    (2ee023457472fe04e0254b9dc6bae74dbb7475d0d9bbf1a87b8c68216282ffb6); NEEDED
    = libc.so.6 + ld-linux-x86-64.so.2 only; exports open/open64/openat/
    openat64/ioctl/read/close. Host functional test (no /dev/dri on the build
    host, so the EVTREAD/read path is code-review-verified only): two
    PAGE_FLIP ioctls logged fb_id=42/43 with gap_us=15125 across a 15ms sleep;
    ATOMIC flags=0x201; CURSOR/CURSOR2 logged; a non-DRM FIONREAD ioctl and a
    regular-file read/write passed through byte-exact (not logged); MIN_US=1e6
    gated every op line while keeping banner+summary.
  - Staging hook for the harness (READY-TO-APPLY, NOT YET APPLIED — the smoke
    expect and kde-session.c are owned by other in-flight workers; apply these
    when their changes land):
    1. `scripts/image/make-rootfs.sh` (next to the kwin-alloc-trace stanza at
       ~:971): build the .so into the abi-libs dir `${dir}`:

       ```sh
       if [[ -f "${REPO_ROOT}/scripts/image/kwin-ioctl-trace-preload.c" ]]; then
           "${cc_bin}" -O2 -Wall -Wextra -fPIC -shared \
               -o "${dir}/kwin-ioctl-trace-preload.so" \
               "${REPO_ROOT}/scripts/image/kwin-ioctl-trace-preload.c" -ldl
       fi
       ```

       (build-kwin-ioctl-trace.sh remains the standalone deterministic
       proof/repro path; make-rootfs mirrors its sibling's inline build.)
    2. `scripts/image/kde-session.c` spawn_kwin_child (mirror the alloc-trace
       block at :794-846): add
       `#define KWIN_IOCTL_TRACE_PRELOAD "/opt/xv6-kde-abi-libs/kwin-ioctl-trace-preload.so"`
       and `#define KWIN_IOCTL_TRACE_LOG "/kde-kwin-ioctl-trace.log"`, gate on
       `cmdline_has_flag("kde_kwin_ioctl_trace=1") && access(...,R_OK)==0`,
       prepend KWIN_IOCTL_TRACE_PRELOAD to the child LD_PRELOAD (composes with
       the alloc-trace/compat preloads the same way), and
       `setenv("KWIN_IOCTL_TRACE_LOG", KWIN_IOCTL_TRACE_LOG, 1)` for the child
       only (unset after spawn).
    3. `scripts/gpu/kde-plasma-desktop-smoke.expect` (mirror alloc-trace
       plumbing): `set guest_kwin_ioctl_trace_log "/kde-kwin-ioctl-trace.log"`
       and `set kwin_ioctl_trace_log "$outdir/kde-kwin-ioctl-trace.log"`; add
       `kde_kwin_ioctl_trace=1` to the kernel cmdline for the trace arm; pull
       it out with `dump_optional_guest_artifact $guest_kwin_ioctl_trace_log
       $kwin_ioctl_trace_log` and add both to the pre-run debugfs rm + cleanup
       lists.
  - Re-A/B verdict remains PENDING the VM lane; the shim is the instrument the
    re-run will use to compare submit->flip-complete pacing across the
    async-cursor and vblank-paced-flip arms. Three workers converged on this
    exact shim shape; it is now singular and reproducible.
- U5 (kernel, small) — IMPLEMENTED 2026-07-08, BUILD-CLEAN, VALIDATION
  PENDING: atomic commits now queue DRM_MODE_PAGE_FLIP_EVENT completions,
  mirroring the legacy path. `fb_kms_atomic.c` gpu_drm_mode_atomic: (a) new
  pre-present -EAGAIN ring-backpressure guard (scoped to non-TEST_ONLY +
  event flag + has_new_fb; same stats bumps as legacy :388-397); (b) on a
  successful non-TEST_ONLY commit that flips the sole CRTC (has_new_fb),
  emit exactly one DRM_EVENT_FLIP_COMPLETE with the sampled vblank
  seq/timestamp, crtc=GPU_DRM_CRTC_ID(1), and the ioctl's user_data —
  queued under fb_state.lock, notified after unlock. Respects the
  virtio_gpu_vblank_paced_flip gate (routes through gpu_drm_page_flip_paced,
  same kvmalloc-fail fallback-to-sync). Single boolean gate => no
  per-plane double-emit; TEST_ONLY never emits; flag-absent path is
  behaviorally unchanged. Teardown drain (gpu_fops_release ->
  gpu_drm_event_release_stale_locked) already covers atomic-queued events
  (shared owner->drm_events ring). Build proof: translation unit module.c
  (#includes fb_kms_atomic.c) compiled with the exact production command
  from compile_commands.json (-Wall -Werror) to scratchpad, clean; the
  in-flight build-x86_64 xv6.bin was NOT touched (no full cmake link ran,
  by design, to protect a concurrent KDE A/B boot). `git -C kernel diff
  --check` clean. VALIDATION (conductor, do not run yet): extend the
  legacy event read-back pattern in drmiftest.c:1668-1717 (ordered reads,
  user_data match, crtc_id==1, monotonic sequence, ring-full EAGAIN,
  overflow drain) to the ATOMIC ioctl, submitting via drmabitest.c:575-589
  atomic_commit_props() with flags|=DRM_MODE_PAGE_FLIP_EVENT + user_data
  set; nographic drmiftest/drmabitest run asserts one FLIP_COMPLETE per
  commit with matching user_data. GUEST TESTS IMPLEMENTED 2026-07-08
  (compile-clean, guest-run still PENDING): added to drmiftest.c ONLY
  (pragmatic choice — it already carries BOTH the legacy page-flip event
  ring template AND full atomic plumbing: real dumb-FB present + plane
  prop-id discovery, so a-e fit one fd/one file; drmabitest.c left
  untouched). New `check_kms_atomic_flip_events()` (called from main after
  check_kms_fb) + helpers `atomic_flip_commit()` (drm_mode_atomic_compat
  variant carrying flags + user_data) and bounded `drain_drm_events()`.
  Asserts: (a) one FLIP_COMPLETE per event-flagged real commit, user_data
  match, crtc_id==1, strict-monotonic sequence; (b) TEST_ONLY+flag => 0
  events; (c) flag-absent => 0 events; (d) fill the ring
  (DRM_XV6_EVENT_QUEUE_CAPACITY=16), next event commit returns -EAGAIN
  with kms_atomic_commits unchanged (proves pre-present), then all 16
  drain in order; (e) legacy PAGE_FLIP interleaved with atomic commits
  stays ordered+monotonic on the shared ring. Event layout read from
  uabi/drm.h drm_event_vblank_compat; reads non-blocking (empty ring =>
  read()<0) so no nographic hang; teardown drains ring + RMFB +
  DESTROY_DUMB. COMPILE PROOF: clean under the exact production command
  from build-x86_64/user-native-check/compile_commands.json (-Wall, and
  additionally -Werror) — the only available user compile db. NOTE: that
  (stale, 2026-06-30) db surfaces a PRE-EXISTING cross-header hard error
  (drmiftest.c includes vfs/fcntl.h while uabi/drm.h pulls uabi/fcntl.h;
  both define struct flock) that halts at the includes before any test
  code; not introduced here (no include added). A working guest drmiftest
  binary was built 2026-07-06 against these same headers, so the
  authoritative guest build resolves it; proof obtained by compiling a
  scratchpad copy with the redundant vfs/fcntl.h include dropped (O_RDONLY/
  O_RDWR, the only fcntl symbols drmiftest uses, come from uabi/fcntl.h).
  If the conductor's guest build DOES hit the fcntl conflict, the one-line
  unblock is to drop that redundant include from drmiftest.c. A KDE check
  would confirm no regression under kwin 5.27 (still legacy path) and
  readies kwin 6.x/AMS.
  Prerequisite for any future kwin 6.x/atomic modeset work (kwin 5.27
  disables AMS unconditionally in VMs; no env override).
  GUEST VALIDATION ATTEMPTED 2026-07-08 (first-ever guest run of the current
  drmiftest; kernel side clean, matrix line UNREACHED — validator drift):
  - Kernel builds+boots clean with U5 in five nographic boots (archives
    20260708T19*Z-u5u6-fix-*); the authoritative guest drmiftest build does
    NOT hit the fcntl conflict (predicted above; no include change needed).
  - drmiftest runs required kernel+test fixes to progress, all applied:
    (1) KERNEL ABI FIX (committed-code gap, fixed this pass):
    `drm_core_auth_magic` gained a correct "caller must be master" gate on
    2026-06-22 (1c2113c) but WITHOUT Linux's counterpart implicit grant, so a
    lone client could NEVER self-auth (GET_MAGIC->AUTH_MAGIC fails -EACCES on
    an idle device — on Linux the first opener becomes master at open). New
    `drm_core_master_open()` (drm_core.c, decl drm_core.h, called from
    gpu_open_file_common in fb_drm_dispatch.c): first opener of a PRIMARY
    node (only; legacy/render files are born authenticated) becomes
    master+authenticated iff none exists; released on close via the existing
    drm_core_release_file; a compositor's later SET_MASTER (same-owner
    cookie) is idempotent. KDE-validated same day (kwin still acquires
    master; real-GL session green — see gates below).
    (2) TEST REFRESHES (drmiftest.c, stale vs committed June kernel ABI):
    GETPLANERESOURCES now returns 2 planes (cursor plane added 2026-06-17
    d8903ff; old assert demanded exactly 1) and legacy DRM_IOCTL_MODE_ADDFB
    is an implemented shim (2026-06-06 385c40d; old assert demanded
    fail-closed) — both updated to assert the current contract.
  - RESULT (20260708T194027Z-u5u6-fix-ng-final2, desktop=0 so drmiftest can
    hold mastership): check_common + check_primary now PASS end-to-end incl.
    `kms_in_formats_blob_matrix ... status=PASS`,
    `kms_primary_scanout_format_mod_matrix ... status=PASS`,
    `kms_primary_scanout_actual_format_matrix ... status=PASS`, and
    `primary ok mode=1280x800`; then check_kms_fb FAILS on further stale
    fail-closed asserts. `kms_atomic_flip_event_matrix` is UNREACHED: main()
    gates check_kms_atomic_flip_events behind check_kms_fb.
  - REMAINING (needs a dedicated drmiftest validator-refresh pass by its
    owner — out of scope for the kernel crash-fix lane, and each iteration
    costs an image rebuild + boot): check_kms_fb still asserts pre-June
    fail-closed contracts for (at minimum) CURSOR BO (cursor-from-BO now
    implemented), CREATEPROPBLOB (user property blobs now implemented),
    CRTC_QUEUE_SEQUENCE (now functional AND queues a DRM_EVENT_VBLANK that
    must be drained before the page-flip event-order reads; the test's
    vblank_source_matrix additionally asserts deltas on
    kms_crtc_queue_sequence_rejects/_noevent_rejects — counters the current
    kernel NEVER increments — and hard-codes
    `crtc_queue_sequence_fail_closed=PASS` in the matrix text), and SETGAMMA
    (zero-size LUT now succeeds as a no-op). SETPLANE/GEM_FLINK/GEM_OPEN/
    SYNCOBJ_EVENTFD fail-closed asserts are also implemented-in-kernel now
    and need auditing. Until that refresh, U5's guest matrix stays pending;
    the U5 kernel emit path itself is exercised indirectly by the KDE run
    (legacy path unchanged, no regression).
- U6 (IMPLEMENTED + VALIDATED 2026-07-08; crash root-caused H2, see below):
  closed the dcache
  eviction/no-flush gaps. These fix a LIVE default-build bug: the positive
  dcache is ungated, so the stale-positive / child_sb UAF on unmount+remount
  was reachable with the gate OFF. Two unconditional fixes:
  - FIX A (generation uniqueness): global monotonic counter
    `__vfs_dcache_alloc_seq()` (atomic, first value 1) now seeds every
    `inode->lookup_seq` (inode.c:65) and backs every dir-seq bump
    (dcache.c `__vfs_dcache_bump_dir_seq`, RELEASE store). No two incarnations
    of the same (sb,ino) can ever share a parent_seq, so a stale entry can
    never satisfy the `parent_seq==seq` honor-check across eviction/reload or
    sb-address reuse. Removes gap 1 + parent side of gap 3.
  - FIX B (lifetime): `__vfs_dcache_invalidate_sb(sb)` (dcache.c) unlinks every
    entry with parent_sb==sb||child_sb==sb per-bucket under the bucket spinlock,
    frees AFTER unlock (store's discipline). Called under the sb WLOCK, before
    each `fs_type->ops->free(sb)` at fs.c mount-fail path (no-op there), and
    (placed before the `vfs_superblock_unlock(sb)` that immediately precedes the
    free) in vfs_unmount, __vfs_final_unmount_cleanup, vfs_unmount_lazy
    immediate path. Guarantees no freed sb is referenced by any surviving entry
    -> positive/negative honor derefs (child_sb->valid / parent_sb->valid)
    touch live memory only. Removes gap 2 + child side of gap 3.
  - Files: kernel/vfs/dcache.c, inode.c, fs.c, vfs_private.h (kernel submodule).
    Design deviations logged: design's "place at 1202/1412/1548" is AFTER the
    wlock is dropped; flush moved to before each `vfs_superblock_unlock(sb)` to
    keep it strictly under the wlock per the race proof. Design's free-loop
    `list_entry_del_init` (non-RCU, nonexistent) dropped — `_safe` iterator
    latches next before free.
  - Reducer: user/programs/dcachetest/dcachetest.c (user submodule,
    auto-discovered) — mount tmpfs /mnt -> creat /mnt/f -> stat (positive) ->
    umount -> remount -> stat /mnt/f MUST fail; plus 50x distinct-name loop
    re-statting prev name; plus 200x mount/umount churn UAF sentinel. Prints
    `RESULT=PASS/FAIL test=<name>`, exits 0 iff all pass. (xv6 userspace has no
    errno; asserts on sign of return. UAF surfaces as boot-harness panic.)
  - Review: adversarial review GO (2026-07-08). Two non-gating notes applied
    source-only: store-site comment documenting the child_sb==parent_sb
    current-tree fact (invalidate_sb does not rely on it), and invalidate_sb
    header corrected — lookups run WITHOUT the sb rlock (inode.c:664) and are
    safe via bucket spinlock + store-exclusion + free-after-return only.
  - Build status: kernel build DEFERRED to the conductor's consolidated
    validation batch (per coordinator; VM lane was busy the whole slice).
    Reducer compile-VERIFIED standalone with the exact user-program flags
    (host x86_64-linux-gnu-gcc, clean). `git -C kernel diff --check` clean.
  - VALIDATION CRASH ROOT CAUSE (2026-07-08, verdict H2 — PRE-EXISTING bugs
    exposed, NOT introduced by U6): the first validation boot
    (20260708T154610Z-u5u6-validation-nographic) crashed during
    remount_loop/mount_churn: `vfs_iput: warning: inode 2 iput with
    ref_count=0` x5 then ASSERTION fs.c "Superblock refcount underflow" in
    vfs_superblock_put from worker_thread. U6's diff provably touches no
    inode/sb refcount (only integer seq values + dcache entry unlink/free
    under the bucket spinlock); dcachetest is simply the FIRST test ever to
    churn tmpfs mount/umount/remount, exposing two default-path bugs:
    (1) `__vfs_evict_unused_inodes` (fs.c, sole caller vfs_unmount) evicted
    and FREED backendless (tmpfs) inodes at ref_count==1. For backendless fs
    idle-cached inodes sit at ref 0, so ref 1 is a LIVE reference — here the
    deferred fput of the just-closed file (close -> __vfs_fput_call_rcu ->
    call_rcu -> vfs_iput_wq workqueue -> vfs_fput -> vfs_inode_put_ref).
    Unmount freed inode+sb; the deferred fput then iput a freed/reused inode
    (the ref_count=0 warnings) and vfs_superblock_put a freed sb -> underflow
    assert. FIX: skip backendless inodes with ref_count>=1 in both the
    pre-lock and authoritative post-lock checks (matches
    tmpfs_unmount_begin's existing ref_count>0 policy). Unmount now reports
    -EBUSY instead of corrupting.
    (2) `vfs_mount` violated its documented failure contract (header:
    "on failure releases mountpoint inode lock and superblock lock") on all
    early-failure returns incl. `__vfs_turn_mountpoint` -EBUSY: it returned
    with the caller-held sb wlock + ilock still held; vfs_mount_path only
    unlocks on success and immediately vfs_iput(mountpoint) -> "cannot hold
    superblock write lock" assertion -> all-core IPI crash (reproduced boot
    2 when a mount raced the now-EBUSY unmount). FIX: `fail_unlock` path in
    vfs_mount releasing ilock + sb wlock (wholding-guarded) on every
    runtime-reachable early failure; plus 4 tmpfs_smoketest.c call sites
    fixed that unlocked unconditionally (would double-unlock on failure
    under the documented contract).
    SUPPORTING FIX (drain, so transient EBUSY does not surface to userspace):
    vfs_umount_path split into `__vfs_umount_path_once` + bounded drain of
    the deferred-fput pipeline on -EBUSY: up to 4 rounds of `rcu_barrier()`
    (guarantees the close's RCU callback has QUEUED the fput work) +
    new `flush_workqueue(vfs_get_deferred_iput_wq())` (waits until the
    work RAN; new API in proc/workqueue.c with `running_works` tracking,
    polls pending==0 && running==0 with scheduler_yield, kicks the manager)
    + retry. Genuinely-busy mounts still return -EBUSY. An earlier
    yield-only drain variant was proven insufficient on boot 2 (16 yields
    burned <1ms without the worker running; the wakeup chain
    queue_work->manager->worker needs real scheduling slack).
    Files: kernel/vfs/fs.c, kernel/vfs/vfs_syscall.c, kernel/proc/workqueue.c,
    kernel/inc/proc/workqueue.h, kernel/inc/proc/workqueue_types.h,
    kernel/vfs/tmpfs/tmpfs_smoketest.c.
  - VALIDATION RESULTS (archives under
    build-x86_64/kde-plasma-desktop-smoke-history/):
    20260708T191657Z-u5u6-fix-nographic (KDE session up, default cmdline),
    20260708T192632Z-u5u6-fix-ng-nodesktop, and
    20260708T194027Z-u5u6-fix-ng-final2 (the latter two with desktop=0,
    the last on the fresh fs.img): all three boots show
    `RESULT=PASS test=remount_basic`, `RESULT=PASS test=remount_loop_50x`,
    `RESULT=PASS test=mount_churn_200x`, `dcachetest: ALL PASS`, `DCRES=0`;
    zero `iput with ref_count` warnings, zero underflow/ASSERTION/
    IPI_REASON_CRASH; forktest FKRES=1 ("fork claimed to work N times!"
    exhaustion-OK), clonetest CLRES=0, cowtest CWRES=0. Drain behavior:
    ~251 transient one-round EBUSY retries across ~302 umounts, all
    converged (the `vfs_unmount: remaining inodes=2` lines are the per-first-
    attempt diagnostic). KDE direct-launch regression run
    (20260708T194657Z-guiperf-u5u6-crashfix-regression, guiperf recipe,
    validates the full kernel change set incl. drm_core_master_open under
    the real compositor): status DONE (status_code=0), real GL — kwin
    renderer `virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`, no
    llvmpipe/software fallback, no `failed to create dri2 screen` — qtquick
    violations 0 (threaded GL scenegraph, no software backend), direct-launch
    cold+warm `status=PASS`, konsole_wait_ms 1394 (cold) / 1196 (warm) —
    inside the settled 1.2-1.5s band — crashes 0 (no
    KCrash/SIGSEGV/Segmentation in crash capture or session logs), no
    lingering qemu. After validation, neg-dcache default-on can be
    reconsidered (substrate now in place).
- U7 (M7, parked; reopen-(a) FORENSICS DONE 2026-07-08 OFFLINE — verdict
  SUPERSEDE, do not reopen for unlocked-wait): native present / unlocked-wait.
  Writeup: scratchpad u7a-sync-completion-forensics.md. n1ab-unlocked-a6.log is
  GONE; surviving console copy build-x86_64/n1ab-unlocked-stall-archive.log has
  NO kernel printfs (debugcon-only), so (i)/(ii) is unresolvable from evidence
  and is settled by source audit + a decisive-experiment spec. (i) LOST-SIGNAL
  RACE = LOW/not root cause: audited every sync_done/sync_inflight/sync_stale
  transition in virtio_gpu.c (reap :3642/:3661-3683, sync_wait_done :2643,
  sync_post :2571, IRQ :2283) — sync_done set under q->lock+RELEASE / read
  ACQUIRE, reinit under q->lock before each reap, single-reaper via
  async_reap_serialize, FM24 closed (sync uses PRIVATE per-call `done`; shared
  async_wait single-waiter via async_wait_serialize). No live lost-wakeup
  window. (ii) VIRGL WITHHOLDS id-0 BEHIND FOREIGN ASYNC = LEADING:
  submit_mixed_async (:2798) posts id-0 with foreign async still in the FIFO
  ctrl ring; unlocked-wait's op_lock release (virtio_gpu_user.c:449-479) admits
  cross-context interleaving default mode forbids, so id-0 sits behind a
  slow/blocked foreign fence; head-of-line FIFO retire + the op_lock-holding
  sync waiter (irq_wait_ms=60000, wait_window_ms :2509) => one withheld id-0 =
  60s op_lock hold = every present parks = the simultaneous 67-73s all-thread
  stall in the archive. OVERLAP vs async-present (U2): async-present STRICTLY
  DOMINATES — it does NOT remove the op_lock convoy (present-copy still
  op_lock+drain+sync) but moves it to the fb-present workqueue, so a withheld
  id-0 delays ONE flip event instead of freezing kwin's event loop => the
  global-park failure that killed unlocked-wait cannot recur, and M7 paces on
  vblank. RECOMMENDATION: close U7 as superseded if the U2 async-present A/B
  lands M7>=55; only reopen the residual if async-present is validated and
  M7<55 (via workqueue/pacing, NOT op_lock release). Reopen cond (b) (per-ctx
  sync budget / irq_wait_ms tiering) is the only salvageable piece and is
  ORTHOGONAL — it also bounds a stuck workqueue present, so do (b) as a general
  robustness fix decoupled from unlocked-wait. M7=51.
- U8 (M8, deferred): idle-cadence payoff measurement on a GL boot; N5
  poll-notify full-wait gates are default-ON after the evdev fix.
- U9 (N2, blocked): ext4 direct-read default promotion — needs the kernel
  page-fault diagnosis (cr2=0x1aafdd193 into _rodata) explained first.
- U10 (N3 residual): KWin LibinputBackend nullptr payload bug.
- U11 (P1/M2 <1.5us): needs a NEW approach; cpumask/CR0.TS is dead.
- U12: push the pending commits (currently ~7 ahead of origin) — needs
  explicit user approval.

## LANE STATUS ROLL-UP (2026-07-08 end-of-execution)

U1 DONE (metadata retired + gated EGL readiness probe). U2 DONE-measured:
kwin schedule latency ~96% of frame-callback waits; async-present slice
implemented+reviewed+A/B'd -> MECHANISM WIN (flip ioctl 24ms->0.65ms, no
tearing) but RESPONSIVENESS NULL (cadence/konsole_wait/hover flat, n=2) =
kill criterion; the ~33-67ms cadence is NOT present-cost bound. Residual
bottleneck is INSIDE prebuilt kwin 5.27's RenderLoop scheduling +
damage-arrival behavior (kwin unpatched: prebuilt binary, no ports build).
Kernel-side leverage on M4 is now essentially exhausted: four mechanically
proven slices (neg-dcache, vblank pacing, async cursor, async present) all
metric-null. Remaining M4 ideas are framework-level (kwin 6.x with AMS via
U5's atomic events; or session-level compositor alternatives) — outside
current guardrails. U3 DONE (dlopen 64->28, gated). U4 DONE (engagement
proven, no hover win; shim repo-owned + EVTREAD fixed). U5 DONE (kernel +
tests; guest matrix pending drmiftest validator refresh — enumerated).
U6 DONE (H2: two pre-existing umount/mount bugs found+fixed by the
reducer; dcache substrate correct). U7 CLOSED-SUPERSEDED by async-present
(id-0-withheld convoy now bounded to one deferred event; per-context sync
budget survives as orthogonal robustness item). U8-U11 parked per recorded
conditions. U12 (push, ~9 commits ahead) awaits explicit user approval.

## Landed opt-in gates (all default-OFF unless noted)

- `vfs_neg_dcache=1` — negative-dcache honor; -95% ENOENT, no M4 effect.
  Correct VFS fix (consumer bug + bump-inside-lock race); see U6 before
  default. Kernel 0cf817d + sentinel fix.
- `virtio_gpu_vblank_paced_flip=1` — flip-complete events paced to the
  synthetic vblank edge; engagement proven; no gap/first-frame win; small
  warm cost (~+250ms/launch). Vsync-semantics infrastructure. 1661795.
- `virtio_gpu_async_cursor=1` — cursor image uploads off the compositor
  thread (17-36ms/upload); correctness GO (R9 probe PASS). PERF VERDICT DONE
  (U4 2026-07-08): engagement PROVEN via kwin ioctl shim (CURSOR2 upload max
  27-32ms -> 0.8-8.5ms), but NO hover win at n=2 vs n=2 clean (medians overlap,
  within noise) — upload cost is not on the hover critical path. Keep opt-in;
  no default-on justification. 1661795.
- `XV6_ROOTFS_LDSOCACHE=1` (make-rootfs) — bakes multiarch ld.so.conf +
  ld.so.cache (1352 entries); -17..23% openat, -36..51% ENOENT, no M4
  effect. Parity-with-Linux cleanliness candidate.
- `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` / `KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`
  — repo-reproducible SONAME-stub A/B fixtures; NOT responsiveness wins.
- `KDE_APP_LAUNCH_PROBE_QT_MINIMAL_IMAGEFORMATS=1` (smoke expect) — prunes
  18 exotic kimg_* imageformats plugins from the per-run fs.img copy;
  konsole dlopen 64->28, ~55-67ms serial dlopen cut, svg/jpeg/ico/gif kept,
  icons intact (U3 A/B 2026-07-08).
- Default-ON (validated): ordered pageflip, poll-notify full-wait (both
  gates, post evdev/pipe/inotify/timerfd producer fixes), TSC jiffies,
  kernel_preempt, console_async, ring depth 60, total-reaper redesign.

## Operational cautions (bite-you-again class)

1. Harness hardcodes the kernel at `kde-plasma-desktop-smoke.expect:6`
   (`$build/kernel/build/kernel/xv6.bin`) and IGNORES `KERNEL` env — bisect
   by sha-verified swap/restore of that file.
2. `qtquick-accelerated-path-policy` FAIL streak right after an fs.img
   rebuild = the image-layout EGL-init race signature (plasmashell dri2
   fail x4 + "EGL display 3001", kwin GL fine). Rebuild the image again to
   confirm; do NOT chase kernels or the host. NOT a metadata problem
   (U1 kernel part done; sysfs metadata always present) — durable fix is
   the EGL-init retry/readiness gate in the session launcher (U1 residual).
3. Stale-fbstat ABI hazard: FB_GPU_GET_STATS copies out sizeof(kernel
   struct); guest tools older than the header get overflowed in plain-stats
   mode. `fbstat sample-current` is safe. Rebuild guest tools with the
   image before fbstat-stats lanes. Stats struct is append-only ABI.
4. kprofile validity: timeout_hit=0 (except browser), userpc dropped=0,
   real-GL proof, booted cmdline grep, active sample; single-run *_ms
   deltas are ±15% (N=1 "wins" have burned us twice: QtMM cold, neg-dcache
   cold); guest drops ~14% BSP ticks under load (N7 fixed get_jiffs; don't
   compare raw jiffies across that boundary).
5. Failure Mode 19 (visible/artifact/prompt-sync timeout, black fb, zero
   crash markers): archive + rerun once. ALWAYS grep run.log for
   PANIC/IPI_REASON_CRASH/tq_remove first — labels can mask panics.
6. Injected-input batteries do NOT prove interactive responsiveness
   (FM 24a); wakeup-semantics changes need an interactive check.
7. Serial console: ~55-60 char truncation, no globbing in guest /bin/sh,
   absolute paths for kprofile exec, markers via variable
   (`M=XX; cmd; echo ${M}RES=$?`).
8. One VM lane at a time; `pgrep -af qemu-system` before and after; never
   trailing `&`; delete scratch images; no lingering QEMU.
9. Subagent run-workers must WAIT SYNCHRONOUSLY on their own runs (bounded
   poll loops) — monitors that outlive the turn cause premature yields.
10. The full failure-mode list (24 items) and diagnostic-knob inventory are
    preserved in the history file — consult before declaring any gate
    failed.

## Guardrails (binding)

- NO un-gated default flips. A default changes only with same-session A/B
  plus explicit-off control, a plan entry, and the regression battery (KDE
  active-sample, Chromium launch-only, M4/M5/M8 within noise). New failure
  class => revert first.
- CODE-FIRST / OFFLINE-FIRST / BATCH GATES / REUSE BOOTED VMS (<=2 boots
  per slice); adversarial review before boot for kernel changes; boot
  before believing (reviews have both caught blockers AND missed one only
  a boot found).
- ORCHESTRATE: decompose into subagent jobs; subagents never flip defaults
  or push.
- Commit at verified checkpoints (repo + submodules deepest-first); NEVER
  push without explicit user approval.
- Closed lanes — do not reopen without new evidence: scheduler wake-to-run,
  futex drift, poll/kqueue/AF_UNIX/eventfd/pipe primitives, guest cursor
  upload, raw PTY setup, tiny Wayland frame delivery, D-Bus/eventfd
  readiness, renderer admission, inotify FIONREAD, PCID/noflush (needs
  INVPCID), loader/ELF surgery, single-library stub trims as M4 levers.

## Scoreboard (2026-07-08)

| # | Metric | Current | Goal |
|---|--------|---------|------|
| M1 | YouTube-freeze survival | 3/3 clean; P0 closed | hold |
| M2 | getpid_ns | 1.65-1.94us | <1.5us (U11) |
| M3 | tlb_amplification 1024pg | 2.7-3.7us | 0 |
| M4 | konsole_wait_ms | warm 1184-1205ms, cold 1406-1500ms (2026-07-08 clean guiperf N=4, all gates green); bottleneck LOCALIZED (U2): kwin damage->repaint SCHEDULE latency = ~96% of each 58-139ms frame-callback wait, konsole render ~0%, kwin repaint ioctl ~4%; imageformats prune opt-in (U3 DONE, ~55-67ms dlopen cut) | Linux-like (~0.3s) |
| M5 | first_visible_ms | ~6.2-6.6s (clean guiperf N=4) | <15000 |
| M6 | mesakmsgl FPS | 118-125 (ordered default); ~60 by design under vblank-paced gate | done |
| M7 | presentedFPS | 51.0 | >=55 (U7) |
| M8 | idle host CPU | unmeasured-valid | <100% (U8) |
| M9 | Chromium visible | PASS guard | hold |

Fork-safety gate for any syscall/scheduler/TLB/mm change: forktest (rc=1
exhaustion signature OK), clonetest rc=0, cowtest rc=0, same boot.
