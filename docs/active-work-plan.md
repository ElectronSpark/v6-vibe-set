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

- U-AUDIO (guest audio backend — STACK MAPPED + PRAGMATIC NULL-SINK FIX
  IMPLEMENTED-AND-REFUTED 2026-07-10; honest requirement spec recorded, gate
  default OFF, NOT committed, fs.img left pristine). USER-VISIBLE SYMPTOM: the
  YouTube stall (M1/M7) is a chromium audio-renderer init failure
  (media/audio/pulse/pulse_util.cc "pa_operation is nullptr" flood +
  PipelineStatus::AUDIO_RENDERER_ERROR); currently worked around by the
  committed `--disable-audio-output` gate (M7), which bypasses PulseAudio
  entirely. STACK MAP (end-to-end, NOT the gap where earlier notes feared):
  (1) KERNEL DRIVER EXISTS — `kernel/kernel/virtio_snd.c` (1173 lines, real
  VIRTIO_SND PCM: SET_PARAMS/PREPARE/START/STOP/tx-completion) binds QEMU's
  `virtio-sound-pci` and `kernel/kernel/dev/ossaudio.c` (3561 lines) publishes
  BOTH an OSS frontend (/dev/dsp, major 14) AND an ALSA char ABI (/dev/snd/
  pcmC0D0p + controlC0, major 116, SNDRV_PCM_IOCTL_HW_REFINE/HW_PARAMS/
  SW_PARAMS/PREPARE/START/SYNC_PTR all decoded). Boot banner confirms:
  "audio: registered OSS /dev/dsp and ALSA /dev/snd/pcmC0D0p". So the feared
  "no kernel sound driver / write virtio-snd = big lane" is ALREADY DONE — the
  kernel is NOT the gap. (2) USERSPACE ships in full: pipewire, wireplumber,
  pipewire-pulse, real pulseaudio daemon, ALSA tools, spa-0.2/{alsa,support}
  plugins (libspa-alsa.so + libspa-support.so=null-audio-sink). `scripts/image/
  kde-plasma-session-child.c` run_audio_services() starts pipewire ->
  wireplumber -> pipewire-pulse and gates on the pipewire-0 + pulse/native
  sockets (runtime: "phase=socket status=PASS pipewire_core=1 pulse_socket=1").
  `scripts/image/stage-kde-runtime.sh:678` bakes an ALSA sink
  "alsa_output.xv6_virtio" (factory api.alsa.pcm.sink, api.alsa.path="hw:0",
  flags=[nofail]) into pipewire.conf; `rootfs-overlay/usr/share/alsa/alsa.conf`
  routes ALSA default PCM -> file plugin -> /dev/dsp. (3) QEMU: `scripts/launch/
  run-qemu.sh` builds `-audiodev <backend> -device virtio-sound-pci,...` when
  QEMU_AUDIO=virtio (default) and QEMU_AUDIO_BACKEND!=none; all non-audio gates
  (incl. the M7 youtube harness) run QEMU_AUDIO=none => NO virtio-sound device
  present, so the kernel /dev/snd has no host sink behind it.
  THE PRECISE GAP (measured, 2 boots): the pipewire ALSA sink registers only
  FLAKILY and, when it does, is SUSPENDED and errors on stream start (no host
  backend under QEMU_AUDIO=none). Baseline runtime evidence:
  plasmashell logs "org.kde.plasma.pulseaudio: No object for name
  alsa_output.xv6_virtio.monitor" (zero sinks). BUT the real gap is DEEPER than
  sink availability: a server-side support.null-audio-sink, even made the
  pactl-confirmed **Default Sink**, does NOT fix chromium — its PulseAudio
  OUTPUT STREAM still fails ("services/audio/output_device_mixer_impl.cc
  MixableOutputStream: Error during independent playback" -> pa_operation
  nullptr -> AUDIO_RENDERER_ERROR, presented 0.02fps, video never decodes).
  So the failure is in the pipewire-pulse <-> chromium-libpulse PLAYBACK-STREAM
  path itself, independent of which sink is default. (A libpulse `paplay` probe
  to localize server-side-vs-chromium-specific was attempted but the diagnostic
  boots got too slow to land the serial command within budget; paplay to the
  null sink appeared to HANG in an early attempt, weakly corroborating a
  server-side stream stall — NOT conclusive.) IMPLEMENTED (gated, default OFF,
  byte-identical when unset, compile-clean cc -O2 -Wall -Wextra, NOT committed,
  source left in kde-plasma-session-child.c; fs.img restored to pristine): gate
  `kde_audio_null_sink=1` / env KDE_AUDIO_NULL_SINK — before pipewire starts,
  writes /dev/shm/kde-config/pipewire/pipewire.conf.d/50-xv6-null-sink.conf
  declaring a support.null-audio-sink node "xv6_null_output" (media.class
  Audio/Sink), and after pulse is up runs `pactl set-default-sink
  xv6_null_output`; logs to kde-audio-status.log (phase=null-sink WROTE/DEFAULT).
  VERDICT: the pragmatic userspace null-sink hypothesis is REFUTED for chromium
  — it does create a valid default sink (fixes the plasma "No object" widget
  warning + gives non-chromium apps a sink) but does NOT make chromium play
  audio. The M7 `--disable-audio-output` gate remains the only working
  YouTube-playback lever. HONEST REQUIREMENT SPEC to get real chromium audio
  (next lane, in rough increasing cost): (a) localize the pulse stream failure
  with a `paplay`/`pw-play` probe on a fast/quiet host (my probe.sh + robust
  expect are in build-x86_64/chromium-youtube-m7/20260710T005044Z-audio-pulse-
  stream-probe/ ready to re-run) — if paplay also fails/hangs it is a
  pipewire-pulse server bug (try the shipped REAL pulseaudio daemon +
  module-null-sink instead of pipewire-pulse for the chromium socket); if
  paplay works it is chromium format/latency negotiation (tune the null sink
  audio.format/rate/period or chromium's requested params). (b) For audible
  host output (not just silent): run with QEMU_AUDIO=virtio (real
  virtio-sound-pci) so /dev/snd has a backend, then debug why spa-alsa hw:0 open
  is flaky (kernel ALSA HW_REFINE/HW_PARAMS constraints spa-alsa can accept +
  the WSLg-pulse host caveat, op-caution 21). Archives:
  build-x86_64/chromium-youtube-m7/20260710T002803Z-yt-nullsink-audio-on/ (null
  sink alone, chromium 0.02fps), 20260710T004004Z-yt-nullsink-default-audio-on/
  (null sink as Default Sink, chromium STILL 0.02fps — decisive), and the probe
  scaffold dir above. See M1/M7.
- U-TOOLTIP (taskbar-icon tooltip slow + unstable — DECOMPOSED + BOTH LEVERS
  IMPLEMENTED AND BOOT-VALIDATED 2026-07-09, gates default OFF; residual =
  default-on promotion): USER PAIN ("hovering a taskbar icon -> tooltip above
  it takes noticeably long, latency unstable run-to-run") reproduced with a
  new hoverprobe TOOLTIP protocol (see Instruments; tip ROI 135,710,140,50
  above the Kate taskbar icon, hover-no-click; full-frame PPM proves a genuine
  "Kate / Advanced Text Editor" tooltip). ROOT (config FIRST, binary-verified):
  plasma-framework 5.115 ToolTipArea reads plasmarc [PlasmaToolTips] Delay,
  compiled default 700 ms (libcorebindingsplugin.so disasm readEntry("Delay",
  0x2bc); enabled=(Delay>0) so Delay<=0 DISABLES tooltips — never use 0); the
  image ships NO plasmarc, so 700 ms show-delay = 74% of the 945 ms baseline
  median. DECOMPOSITION (n=12/event/boot, 7 boots, all gates green, 0 FM19):
  baseline tooltip_open med 948.5/942.9 p90 973/1006 max 1093/1124 sd 50/59 =
  700 config delay + ~250 ms render (tooltip QML update + kwin damage->repaint
  schedule, U2 wall ~3-4x67ms cadence); tooltip_close ~260 ms all arms; A->B
  switch (dialog visible) 233 -> ~110 ms when Delay=50. VARIANCE ATTRIBUTED:
  repeats tight everywhere (sd 44-80); the instability is the FIRST tooltip of
  the session = ToolTipDialog QML cold instantiation, HIDDEN at Delay=700
  (~+150ms) but EXPOSED + jittery at Delay=50 (first 994/2331 ms vs ~470
  repeats) — plus occasional close-side outliers (kwin schedule jitter, known
  wall, quantified not chased). FIX (two gated levers, default OFF,
  byte-identical without tokens; sources edited but NOT committed):
  (1) kde-session.c cmdline `kde_plasma_tooltip_delay=<1..10000>` writes
  /dev/shm/kde-config/plasmarc Delay override (engagement line "plasma tooltip
  delay override Delay=50 ms" in run.log); (2) kde-plasma-session-child.c
  cmdline `kde_tooltip_prewarm=1` (env KDE_TOOLTIP_PREWARM=1) extends the
  kickoff-prewarm double-forked worker (now started if EITHER gate is on;
  kickoff-only portion skipped when its own gate is off) to hover the taskbar
  icon once after the taskbar-ready gate (knobs KDE_TOOLTIP_PREWARM_ICON_ABS_X/
  _Y/_DWELL_MS default 11000,64200/2500) then move away — ~3.4 s worker time at
  login, tooltip auto-hides, pristine; (3) harness A/B hook
  KDE_SMOKE_PLASMA_TOOLTIP_DELAY_MS stages /etc/xdg/plasmarc into the PER-RUN
  fs.img copy (KConfig system cascade). A/B VERDICT (Delay=50 + prewarm, two
  reps): med 945 -> 486/482 (-49%), p90 ~990 -> 541/530 (-46%), worst-case
  2331 -> 607/747, first-of-session ~1.1s -> 486/747 ms; prewarm-only at
  Delay=700 = NULL (first 1105 — the 700ms delay already masks the cold cost;
  prewarm pays off only once the delay is cut); Delay=50 alone leaves the
  first-tooltip jitter (994/2331). Residual ~480 ms floor = U2 kwin
  damage->repaint schedule latency (settled, do not relitigate). Staged into
  committed fs.img via debugfs (kde-session, kde-plasma-session-child,
  hoverprobe — all gated/inert by default; backups in the tooltip worker's
  scratchpad); gate-OFF equivalence proven by boot6 baseline-identical stats
  on the new binaries. PROMOTION to default-on (residual): needs the standard
  regression battery + owner sign-off (same posture as kickoff prewarm); the
  natural default candidate is Delay~200-300 + prewarm if 50 ms feels too
  eager for real users. Archives: 20260709T205408Z-tooltip-ab-summary (+ 7 run
  archives listed inside). See M12.
- U-KICKOFF (start-menu open/close — DECOMPOSED + LEVER PROVEN 2026-07-09;
  PRODUCT-SIDE PREWARM IMPLEMENTED + BOOT-VALIDATED 2026-07-09, gate default
  OFF; only residual is the default-on promotion):
  USER PAIN ("opening/closing the KDE start menu takes SECONDS") reproduced and
  split with a new hoverprobe menu-mode (menu-body ROI + open/close protocol;
  see Instruments). ROOT: the pain is the COLD FIRST Kickoff activation of a
  session = 2254/1892ms (n=2, matches the reported 1394-2517ms); every WARM
  repeat-open in the same session is ~400-500ms and close is ~180-420ms. So the
  ~2s is a ONE-TIME QML-compile + app/recents model-population cost, NOT a
  per-open cost and NOT compositor cadence (co-enabled kwin ioctl-trace shows
  the compositor mostly idle during the cold open; PAGE_FLIP dur ~5ms). The
  residual warm ~400ms floor is the already-settled U2 kwin damage->repaint
  schedule latency (do not relitigate). ATTACK CHOSEN = session-start PREWARM
  (open Kickoff once during login, wait for the model build, close) — cheapest
  lever that works whether the cold cost is compile or model-population. A/B
  (hoverprobe _PREWARM gate, do_hover=0 so iter1 = user's first click): first
  open 2254/1892 -> 498/540ms, -75% (~1550ms), all gates green (real virgl/
  D3D12 GL, qtquick PASS, crash 0, no lingering qemu, full-frame PPM confirms
  genuine Kickoff open). See M11 + archive 20260709T190655Z-menu-prewarm-ab-
  summary (+ ...T190352Z-...-treatment-rep1). PRODUCT INTEGRATION DONE
  2026-07-09 (realization (a), input-injection in the product session): the
  prewarm now lives in `scripts/image/kde-plasma-session-child.c` (not the test
  tool). After services start, `kickoff_prewarm_start()` DOUBLE-FORKS a worker
  (setsid, reparented to init — never blocks/steals the session-child's
  waitpid(plasmashell), no zombie); the worker (i) opens /dev/mouse + /dev/fb0,
  (ii) readiness-gates on the bottom-left launcher ROI going non-black + hash-
  stable (borrowed taskbar-settle heuristic, FB_GPU_SCANOUT_READ), (iii) abs16-
  clicks the Kickoff launcher (px23,783) with OPEN-VERIFY-AND-RETRY — polls the
  menu-body ROI (100,330,200,200) for a change and re-clicks (up to 8x) because
  Kickoff is NOT interactive the instant the corner paints (a single early
  click at ~8s is swallowed; the retry is REQUIRED — the first no-retry build
  logged opened=0 and the user's first open stayed cold at 2583ms), (iv) dwells
  4000ms for the QML/model build, (v) dismisses with an empty-desktop click and
  self-verifies pristine (menu-ROI hash back to the closed baseline) + dumps
  /kde-plasma-kickoff-prewarm-after.ppm as proof. Gate default OFF: cmdline
  `kde_kickoff_prewarm=1` or env KDE_KICKOFF_PREWARM=1 (tunables
  KDE_KICKOFF_PREWARM_{DWELL,SETTLE,GATE,OPEN_TIMEOUT}_MS + _ICON_ABS_X/Y).
  BOOT VALIDATION (3 boots, real KVM + virgl/D3D12 GL, qtquick PASS, crash 0,
  no lingering qemu): gate ON — worker logs gate READY@7.6s, open OPEN
  attempt=1@11.1s, DONE opened=1 pristine=1@16.1s; USER's first Kickoff open
  (hoverprobe menu-mode _DO_HOVER=0 _PREWARM=0, so iter1 = first real click)
  = 493ms vs gate-OFF 2164ms cold same config = -77% (~1670ms), matching the
  test-tool A/B (498/540ms). Gate OFF is byte-behaviour-clean (no
  kickoff-prewarm lines, no flag in cmdline). Pristine screenshot verified
  (wallpaper+icons+panel, no menu). PROMOTION TO DEFAULT-ON (residual, needs
  owner sign-off): the injected click is not a provably ~0 cost (~2s of prewarm
  work at login, though nearly free within the ~6.2-6.6s M5 first-visible
  window), so per the guardrails it stays opt-in until the standard same-
  session A/B + regression battery (KDE active-sample, Chromium launch-only,
  M4/M5/M8 within noise) run green with sign-off. kwin/plasmashell are PREBUILT
  (C++ patches off the table); a plasmashell scripting/DBus toggle (realization
  (b)) was not needed. NOTE (not yet measured, cheaper still if
  it works): persistent QML disk cache (qmlcachegen at image build / prepopulate
  ~/.cache/*qmlcache) would cut only the compile half and needs plasmashell env
  (QML_DISK_CACHE) proven enabled — deferred; prewarm subsumes it and needs no
  image rebuild.
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
  - VALIDATOR REFRESH DONE 2026-07-08 (drmiftest owner lane; drmiftest.c only,
    user submodule, uncommitted). U5's `kms_atomic_flip_event_matrix` is now
    REACHED and PASSES in a nographic guest boot (archive
    20260708T220649Z-u5-drmiftest-refresh):
      drmiftest: kms_atomic_flip_event_matrix event_per_commit=PASS
      test_only_noevent=PASS flag_absent_noevent=PASS
      ring_full_eagain_pre_present=PASS overflow_drain=PASS
      legacy_atomic_interleave=PASS crtc_id=1 capacity=16 status=PASS
    Per-assert refresh (old expectation -> kernel truth, file:line):
    - CURSOR BO: was "unexpectedly enabled" fail-closed -> cursor-from-BO is
      implemented (fb_drm_kms_properties.c:1364 -> gpu_kms_upload_cursor_from_bo
      :444). Now creates a dedicated 64x64 dumb BO, asserts upload succeeds +
      kms_cursor_uploads bumps, and that an over-max dim (>FB_GPU_CURSOR_MAX_DIM
      =64, fb.h:64) is still rejected.
    - CREATEPROPBLOB: was fail-closed -> implemented
      (fb_drm_kms_properties.c:148 gpu_drm_mode_createblob). Asserts create
      returns a nonzero id + DESTROYPROPBLOB accepts it; unknown-blob destroy
      still rejected (-ENOENT, :227).
    - CRTC_QUEUE_SEQUENCE: was "unexpectedly enabled" -> functional
      (fb_drm_core_kms.c:1764); queues one DRM_EVENT_VBLANK with the resolved
      target sequence / crtc_id=1 / user_data. Now asserts success, DRAINS the
      queued VBLANK (else it corrupts the following page-flip event-order
      reads), and keeps fail-closed on bad crtc (-EINVAL :1776) and bad flags
      (bumps kms_crtc_queue_sequence_bad_flags :1779). Dropped the two
      vblank_source_matrix delta asserts on kms_crtc_queue_sequence_rejects /
      _noevent_rejects — the kernel declares (fb.h:1081,1083) but NEVER
      increments those counters; matrix text updated
      (crtc_queue_sequence_functional/event_drained instead of fail_closed).
    - SETGAMMA: was "unexpectedly enabled" -> zero-size LUT succeeds as a no-op
      (fb_drm_core_kms.c:1830-1835); asserts success + bad crtc still rejected.
    - GEM_FLINK/GEM_OPEN (check_dumb, render fd): were fail-closed -> global
      names implemented (fb_drm_dispatch.c:486/498, fb_bo_shmem_dmabuf.c:1967/
      2005). Asserts FLINK returns a nonzero name, OPEN resolves it to a handle
      of matching size, and name 0 rejected.
    - SETPLANE: AUDITED, no drift — the primary plane still returns -EOPNOTSUPP
      (fb_drm_kms_properties.c:630); the existing asserts hold as-is.
    - SYNCOBJ_EVENTFD: AUDITED — implemented (fb_syncobj_prime_virtgpu.c:1170)
      but correctly rejects a non-eventfd fd (the syncobj fd passed) with
      -EINVAL; only the misleading message was corrected (behavior unchanged).
    Newly-exposed (test never reached these before; refreshed to the real
    committed contract, all now PASS):
    - WAIT_VBLANK: absolute WAIT_VBLANK(N) returns reply.sequence == the counter
      at the N-th edge (>= N), i.e. exactly N when crossing from below — it does
      NOT overshoot to N+1. Old assert required >= 42 for a request of 41;
      corrected to >= 41 (the real absolute-wait contract).
    - vblank_source_matrix: the old assert compared kms_vblank_sequence against
      crtc_sequence_sample. Those are NOT one monotonic scale: CRTC_GET_SEQUENCE
      returns the synthetic-time estimate (inflated during the idle WAIT_VBLANK
      block), while the display-present path OVERWRITES kms_vblank_sequence with
      the present count (fb_scanout.c:315), so the display-correlated sequence
      can read LOWER than an earlier idle synthetic sample (observed 22 vs 41).
      Refreshed to the coherent display-correlated contract: synthetic==0,
      display_correlated==1, source flags coherent, kms_vblank_sequence ==
      display_last_complete, and display_last_complete advanced. FINDING
      (pre-existing, non-U5): kms_vblank_sequence is non-monotonic across the
      synthetic-idle -> display-present transition (overwrite, not max) — ties
      to the recorded M4/M7/R9 "kwin has no vsync/present clock" root cause.
    - check_kms_sync_file_in_fence_matrix: the kernel waits in-ioctl on a
      pending sync_file in-fence via an unbounded dma_fence_wait(-1)
      (fb_drm_kms_atomic_props.c:534) and rejects NONBLOCK atomic, so there is
      NO non-blocking path; the old single-threaded submit-then-signal pattern
      DEADLOCKED. Refreshed to drive the pending in-fence commit from a forked
      child while the parent waits for the pending wait to register then
      SYNCOBJ_SIGNALs (dma_fence_signal in place on the shared fence,
      fb_syncobj_prime_virtgpu.c:860 area), mirroring check_syncobj_*_wakeup.
      Now PASS (pending_waits_delta=1, pending_wakeups_delta=1, refs balanced).
  - RESIDUAL (does NOT gate U5; blocks only the final `drmiftest: ok`):
    check_fence_callback_lifecycle_matrix reports fence_objects_live_delta=-1
    with ALL functional callback assertions passing (added=2/fired=1/removed=1/
    late=1/errors=0, fence_fd_live_delta=0). A fence object live at the matrix
    baseline is reaped within its window; not conclusively attributed —
    candidates are a pre-existing lazy fence-object free first exposed now, or a
    child-teardown side effect of the new fork() in the refreshed sync_file
    matrix. Needs one follow-up boot to attribute (deferred: this pass already
    used its boot budget). All drift up to and including U5's matrix is
    validated PASS.
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
  U7-PREMISE MEASURED 2026-07-09 (VM lane, 2 boots, kernel 3254ce6/repo
  6a3f67c): chromium-video kprofile A/B (KPROFILE_SECONDS=90, USE_KVM,
  virtio-vga-gl-primary, audio none) — CONTROL "PERF-VIDEO RESULT pass
  fps=51.2 speed=1.000 presentedFPS=51.2 decodedFPS=62.1 dropPct=9.63
  advanced=15.23" vs TREATMENT (virtio_gpu_async_present=1, token verified
  x3 in booted cmdline) "PERF-VIDEO RESULT pass fps=51.0 speed=0.998
  presentedFPS=51.0 decodedFPS=61.1 dropPct=7.54 advanced=15.37". Both
  windows COMPLETE (advanced ~= runMs 15s); both arms real virgl/D3D12 GL,
  crash 0, gpu_init/config/exit_error_count=0, fault_count=0; kms_page_flips
  897 vs 917, page_flip_events 1:1, all software_blit. dropPct improved
  (9.63->7.54) but presentedFPS is FLAT at ~51. VERDICT: async-present is
  landed+validated AND M7<55 => the recorded reopen condition for the U7
  RESIDUAL (workqueue/pacing, NOT op_lock release) is now formally met;
  presentedFPS ~51 is NOT present-ioctl-cost bound (consistent with the U2
  kill-criterion cadence null). CAVEAT: in-image fbstat does not print
  present_async_* (fields exist in fb.h:1770-1779 only; fbstat.c has no
  printf for them — only cursor_async), so per-run counter engagement is
  not capturable in-guest; engagement proof of record remains the
  2026-07-08 U2 shim A/B (PAGE_FLIP 925-1017us -> 64-73us median, 1:1
  FLIP_COMPLETE) + this run's cmdline token + absence of the
  "async-present workqueue unavailable" fallback print. Archives:
  `kde-plasma-desktop-smoke-history/20260709T151220Z-m7-asyncpresent-control/`,
  `...T152900Z-m7-asyncpresent-treatment/`.
  U7-RESIDUAL ATTRIBUTED + FIX IMPLEMENTED 2026-07-09 (OFFLINE, kernel edits
  UNCOMMITTED, TU compile-proof clean, NO boot — gate DEFAULT OFF; A/B deferred
  to conductor). ATTRIBUTION (mined the fresh U7 A/B archives + qemu traces +
  fbstat): the 51fps ceiling is NOT (a) present-ioctl-cost bound (async-present
  already collapsed the flip ioctl 24ms->0.65ms with presentedFPS flat 51.2->
  51.0), NOT (b) software_blit-bandwidth bound (fbstat control blit_bytes
  3678208000 over 898 full_blits = EXACTLY 1280x800x4 = 4,096,000 B/blit, ~200
  MB/s at 51fps — orders below memcpy BW; copy_ticks=0 per the settled
  attribution), and NOT (c) make_room/retire back-pressure during video (qemu
  trace steady state is a clean 1:1:1 `ctx_submit ctx0x2 size 4336` /
  `res_flush 1280x800 res {0x5,0xe,0x4}` (triple-buffered) / `fence_resp`, and
  fence_ctrl==fence_resp==965 => no ring-fill backlog; the H1 make_room stalls
  were the konsole-LAUNCH finding, ring depth already 60). ROOT CAUSE (confirmed
  from source): kwin has NO free-running vsync clock — the flip-complete event
  carries the DISPLAY-CORRELATED vblank (gpu_kms_sample_vblank_locked overwrites
  kms_vblank_sequence with display_last_complete, fb_drm_core_kms.c:1412; fbstat
  control kms_vblank_display_correlated=1, seq=898=display_last_complete). kwin
  5.27's RenderLoop schedules next-repaint at lastPresentationTimestamp +
  16.67ms; with lastPresentationTimestamp = REAL completion time (present
  round-trip ~3ms per U2 EVTREAD + repaint + wakeup), that per-frame overhead is
  ADDED to every interval, sliding the loop to ~19.6ms = 51fps (matches the
  recorded "mono-modal ~22.7ms throughput limiter, not vsync-beat bimodal",
  ~19.6ms post total-reaper). async-present and vblank_paced_flip both left this
  intact (vblank_paced still chains off real completion — "next edge PAST real
  completion") => both measured M7-null. This is the recorded M4/M7/U5 "kwin has
  no vsync/present clock" root cause, now confirmed as the M7 ceiling.
  FIX (highest-leverage, matches task candidate (c)-opt1): new gate
  `virtio_gpu_present_clock_60hz=1` (DEFAULT OFF; composes with + REQUIRES
  virtio_gpu_async_present=1; no-op when async off => sync flip path
  byte-identical). In the async-present worker, AFTER the blit completes and the
  R5-critical BO ref is dropped (blit -> fb_bo_put -> [arm completion]), the
  flip-complete event's seq/ts is taken from a free-running, wall-clock-anchored
  60Hz grid (new gpu_kms_present_clock_next_locked: monotonic +1/present, edges
  16.67ms apart, self-healing snap-forward past missed edges = a dropped frame,
  correct vsync) INSTEAD of the real-completion clock, and delivered at that grid
  edge via the existing tested gpu_drm_pace_completed_flip_by_id timer/
  reservation/never-drop path (helper returns base=(grid_seq-1, grid_ts-period)
  so pace's internal +1 lands exactly on the grid edge). kwin then paces off a
  stable 60Hz grid instead of a sliding real-completion chain => grid-locks at
  16.67ms = 60fps. SAFETY: changes only the event's CLOCK, not its ordering vs
  the blit (armed after blit+put), so NO new tearing/UAF over async-present;
  gpu_kms_sample_vblank_locked is still called for its side effects (keeps
  kms_vblank_sequence coherent for fbstat/WAIT_VBLANK/CRTC_GET_SEQUENCE), clock60
  overrides only the delivered seq/ts. SUPERSEDES vblank_paced_flip's completion
  timing when both set. WHY higher-leverage than alternatives: retire-batching
  wouldn't help (no video ring backlog), direct-scanout skip-copy is
  architecturally fail-closed on this host (all kms_present_reject_*=898,
  native_present_credit=0, no DDA/nouveau/D3D12-bind) AND the copy isn't the
  limiter (copy_ticks=0). Files (kernel submodule): fb_drm_core_kms.c (gate
  reader + free-clock helper), fb_drm_kms_properties.c (worker: clock60 route +
  one-shot "present-clock 60Hz engaged" banner, printed OUTSIDE the lock),
  inc/dev/fb.h (2 END-appended stats: present_clock60_events_total,
  present_clock60_snap_total). COMPILE PROOF: dev/fb module.c TU rebuilt with the
  production -Wall -Werror command from build-x86_64/kernel/build/
  compile_commands.json to scratchpad (in-tree .o untouched), clean; `git -C
  kernel diff --check` clean. ADVERSARIAL SELF-REVIEW (no blockers): (R1) base_ts
  = anchor+(target_seq-1)*period >= anchor >= 1 > 0, no underflow, predict()
  never early-returns 0; (R2) target_seq>=1 always => base_seq>=0, delivered seq
  monotonic across snaps (wall clock monotonic); (R3) worst-case pace re-reads
  now >= target_ts => delay clamps to 1ms (rare, benign); (R4) grid statics +
  stats touched only under fb_state.lock; (R5) monotonic_ns()-under-lock has
  precedent (gpu_drm_page_flip_paced:1575); (R6) printf moved out of the lock
  (no printf-under-fb_state.lock precedent existed); single-threaded fb-present
  wq => `announced` static race-free; (R7) reservation lifecycle UNCHANGED (rides
  the timer, released by timer_cb; exactly one release, same as vblank_paced);
  (R8) no early delivery vs blit => tearing guardrail holds; (R9) M6 mesakmsgl
  under clock60+async would read ~60 (same documented vsync tradeoff as
  vblank_paced; A/B uses chromium-video, not M6). A/B SPEC (conductor, M7 recipe,
  DO NOT BOOT until VM lane frees): CONTROL = async-present only
  (`virtio_gpu_async_present=1`), TREATMENT = +`virtio_gpu_present_clock_60hz=1`;
  chromium-video kprofile KPROFILE_SECONDS=90, USE_KVM, virtio-vga-gl-primary,
  audio none; verify both tokens in booted cmdline (op-caution 1). ENGAGEMENT:
  grep run.log for `present-clock 60Hz engaged` (one-shot banner) in TREATMENT,
  absent in CONTROL. PRIMARY: presentedFPS 51 -> target >=55 (goal), ideally
  ~60; decodedFPS unchanged ~61; dropPct expected to fall further. DECISIVE
  KILL/CONFIRM (fbstat, if capturable, else infer): if presentedFPS rises toward
  60 with present_clock60_snap_total << events_total => CLOCK hypothesis CONFIRMED
  (limiter was the missing free vsync clock). If presentedFPS stays ~51 AND
  snap_total ~= events_total => residual is HOST PRESENT THROUGHPUT (worker
  blit+round-trip routinely > 16.67ms), NOT the clock — pivot to host-retire, do
  NOT relitigate the clock. GUARDRAILS (mandatory, same as async-present A/B):
  real virgl/D3D12 GL both arms, qtquick 0 violations, crash grep 0, no
  image-layout race, screenshot direct-launch-diff = settled 1001730 (tearing
  guard — clock change must not corrupt frames), M4/M5/M8 within noise,
  fork/clone/cow signatures same boot. PROMOTION: only on a proven presentedFPS
  win with owner sign-off + full regression battery (keep default-OFF until then,
  same posture as all prior M7 gates).
  A/B VERDICT — MEASURED 2026-07-09 (conductor, pushed kernel fcb0ebe + fbstat
  present_clock60 printf added so counters ARE capturable; chromium-video kprofile
  90s, USE_KVM, virtio-vga-gl-primary, audio none; CONTROL=async-present,
  TREATMENT=+present_clock_60hz; both windows complete advanced~15, real virgl/
  D3D12 GL both arms, crash 0, fault/init/exit error counts 0, FM17 launch-
  evidence-missing bookkeeping expected). ENGAGEMENT PROVEN: TREATMENT run.log
  banner "present-clock 60Hz engaged" (absent in CONTROL), both tokens x3 in
  cmdline. RESULT: presentedFPS 52.3 (CONTROL) -> 53.4 (TREATMENT), +1.1fps
  (+2.1%); dropPct 6.50 -> 6.22; decodedFPS ~61-63 unchanged. DECISIVE COUNTERS
  (fbstat): present_clock60 events_total=913 (== kms_page_flip_events, i.e. ALL
  session flips routed through the clock60 path) snap_total=58 = 6.35% << events.
  READ: the clock ENGAGES and PACES a clean 60Hz grid, and the async worker KEEPS
  UP with it (snap only 6.4% — it is NOT the limiter). Yet presentedFPS moved only
  +1.1 and did NOT rise toward 60; goal >=55 NOT met. This is the enumerated third
  case (worker keeps up AND FPS stays sub-60): the clock hypothesis is confirmed as
  a MECHANISM (free vsync grid works, minor real win) but REFUTED in MAGNITUDE — the
  U7-RESIDUAL prediction that a clean completion clock would grid-lock kwin to ~60fps
  does not hold. Since a clean 60Hz completion clock that the worker feeds on time
  still yields only 53.4fps present, the dominant M7 ceiling is DOWNSTREAM host/kwin
  present throughput (kwin RenderLoop does not convert clean completion timestamps
  into 60fps repaint scheduling), NOT the completion-clock timestamp. PIVOT
  (honest): host-retire / present-throughput; do NOT relitigate the completion
  clock. Gate `virtio_gpu_present_clock_60hz=1` stays DEFAULT-OFF (real but
  sub-threshold +2.1% win; no promotion). Also ran hoverprobe under the treatment
  tokens (M10 check): hover_in 203.5 / hover_out 157.8 / click 284.0 ms median,
  WITHIN NOISE of the ~205/193/250-312 baseline — no interaction win from the clock
  (kwin damage->repaint schedule latency dominates, per U2). Archives:
  20260709T164918Z-m7-clock60-control, 20260709T170251Z-m7-clock60-treatment,
  20260709T170502Z-m10-hoverprobe-clock60-treatment. U7-RESIDUAL: clock-60 lever
  EXHAUSTED (measured, sub-threshold); residual ceiling reassigned to host present
  throughput.
- U8 (M8) — MEASURED 2026-07-08 (FIRST VALID reading; GOAL MET, large
  margin): idle-desktop host-CPU on a real-GL boot with the N5 poll-notify
  full-wait gates default-ON (evdev fix). Recipe: `launch-gui.sh USE_KVM=1
  QEMU_GPU=virtio-vga-gl-primary` (detached, serial captured). Validity gates
  all green: real virgl via WSL D3D12 (NVIDIA) — virgl capsets ready, virgl
  3D scanout, renderD128 = "virgl render node OpenGL", 0 llvmpipe/swrast;
  presentation ACTIVE (8 `virtio_gpu: page-flip present` lines — boot/settle
  burst then a t=80s burst; kwin 5.27 is repaint-on-damage so a fully static
  idle screen then stops flipping, which is the genuine idle condition);
  session-ready `KWin and plasmashell are running`; 0 crash markers; booted
  cmdline has 0 N5 opt-out tokens (poll_notify_full_wait / af_unix_...=0),
  confirming both gates ON. METHOD: `/proc/<qemu-pid>/stat` utime+stime delta
  over three consecutive 10s windows, CPU%=delta_ticks/(10*CLK_TCK)*100,
  CLK_TCK=100. READINGS: 13.7% / 13.6% / 13.9%, MEAN 13.7%. Cross-check: a
  4th window's process-stat delta (13.1%) equals the sum of all 31
  per-thread deltas (13.2%) — /proc/PID/stat correctly aggregates the 6 vCPU
  threads, so 13.7% is directly comparable to the historical thread-summed
  85-130% band. ps lifetime pcpu 24.6% (amortizes the boot+settle spike;
  instantaneous idle ~13-14%). VERDICT vs GOAL <100%: PASS with wide margin;
  vs historical 85-130% band: FAR BELOW — this is the N5 payoff (idle no
  longer busy-spins poll rescans; vCPUs halt at idle). 1 boot, clean
  shutdown, no lingering qemu. Archive:
  `kde-plasma-desktop-smoke-history/20260708T201747Z-u8-m8-idle-cpu-gl/`
  (run.log, gl-presentation-proof.txt, m8-readings.txt, debugcon.log,
  qemu-proc-stat-final.txt). NOTE: the 07-05 44%/57-64% readings are
  superseded — they were non-GL boots that never presented; this is the
  first reading on a GL boot with confirmed active presentation.
- U9 (N2) — FORENSICS DONE 2026-07-08 (OFFLINE; scratchpad
  u9-rodata-fault-forensics.md): the `cr2=0x1aafdd193 err=0x2
  rip=0xffff80000039a34c` fault is CORRUPTED CONTROL FLOW INTO `.rodata`
  (err=0x2 = supervisor WRITE; RIP genuinely in `_rodata` 0x35e000..0x3b7000;
  the executed rodata bytes decode to a stray write at cr2 — cr2 and the
  `sig_trampoline` symbol are second-order symptoms of executing data, not the
  origin). Frame `0xbefc6cc0` is INSIDE CPU-2's idle kstack (0xbefc0000+32KB),
  so the corruption sits on the idle thread's own control-flow (saved
  return-addr / a sched or IRQ pointer) — a stray write from elsewhere.
  ROOT CAUSE (named, plausible): `rcu_head_cache` free-list / callback
  corruption — the R3 lane's unroot-caused double-free/UAF under fd-teardown
  load. The slab free list is intrusive (next-ptr at obj offset 0 = the
  rcu_head's own `->next`; mm/slab.c:601,634), so a double-free/UAF-write of a
  slab rcu_head corrupts it; a corrupted head reaching `func(data)`
  (lock/rcu.c ~866) or `call_rcu` scribbling through a bad free-list ptr
  (rcu.c:806-810) = the wild transfer seen. CORROBORATION: R3's archived
  double-free is `cpu=2` on `rcu_cb/2` (SAME core), clustered on socket/fd
  teardown, and `repairing corrupt freelist cache='rcu_head_cache'` was seen
  07-04 (slab.c:624). The heavy fd/inode churn funnels through the deferred
  fput path (close->call_rcu->workqueue->vfs_fput; vfs_syscall.c:911-946,
  file.c:134). AUDIT: `ext4_read_page_direct` itself is CLEAN
  (ext4fs_file.c:686/89/186 — only local bio_alloc/add_folio/await/release, NO
  call_rcu/rcu_head; DMA target pinned page_ref>=2 + io_in_progress; transient
  compound nodes explicitly fall back, :754) — it is a CORRELATIONAL ACCELERANT
  (−50% latency raises the pre-existing bug's recurrence rate), not the
  corrupter (the R3 double-frees also hit the direct-read-OFF arm). U6/R5
  cross-ref: NEITHER retroactively explains it — the crash kernel (18:53,
  pre-04b1ee2) ALREADY had the R5 stale-TLB fix (67d7b5c), and U6's fix fires
  only on unmount (absent from a KDE run; the runtime LRU evictor
  vfs_evict_lru_inodes is ref==0-guarded). Both are SIBLINGS in the same
  call_rcu/stale-frame lifetime family; they narrow the surface but leave the
  R3 door (best match) open. RETRY: N2 CAN be retried as a DIAGNOSTIC-ARMED
  battery (not a blind flip) — the corrupter is whole-system and direct-read-
  independent, so it should not block direct-read indefinitely. BATTERY (spec
  in scratchpad §6): static/build + nographic safety gate now ALSO asserting
  `dcachetest RESULT=PASS`, then the cold-cache A/B with EVERY arm armed
  `ext4_read_page_direct_debug=1 rcu_head_trace=1` (ON) / `=0 rcu_head_trace=1`
  (OFF), complete the interrupted 5x5 and keep accruing to zero faults, all
  §5 guardrails green (real-GL, qtquick 0, crash-grep 0 incl.
  KERNEL PAGE FAULT/__slab_obj_put/repairing corrupt freelist/rcu_head_trace:).
  If it recurs, 04b1ee2's full reg/PTE/instr/.rodata-classifier dump +
  rcu_head_trace owner-history decides rcu-corruption (→ becomes an R3 root-
  cause, N2 stays behind it) vs an ext4-direct-read-ring correlation (→ kill
  promotion) vs neither (→ R5-residual/sched scribble). Standing rec: add
  `rcu_head_trace=1` (default-off, ~0 cost) to EVERY future KDE battery to
  finally root-cause R3.
  BATTERY EXECUTED 2026-07-09 (diagnostic-armed 5x5 complete; NO default
  flip — decision stays with conductor): kernel d052c5d rebuilt no-op
  (binary current), tree clean. NOGRAPHIC SAFETY GATE PASS
  (`20260709T014010Z-n2-diag-safety-gate`, cmdline ext4_read_page_direct=1
  rcu_head_trace=1): dcachetest x3 rc=0 (ALL-PASS==rc0), forktest rc=1
  exhaustion, clonetest rc=0, cowtest rc=0; crash grep clean (the
  `vfs_unmount: remaining inodes=2` console flood during dcachetest mount
  churn is the expected U6 deferred-fput EBUSY-retry diagnostic, NOT a
  fault). COLD-CACHE A/B (desktop-interaction reducer, active-sample,
  audio=none, software-GL unset, KVM+virtio-vga-gl-primary; ON arm
  `ext4_read_page_direct=1 ext4_read_page_direct_debug=1 rcu_head_trace=1`,
  OFF arm `=0 rcu_head_trace=1`; tokens verified in every booted cmdline;
  debug ring arms silently, dumps only on fault, so token==armed):
  ON 5/5 PASS (`20260709T014901Z/T015302Z/T020047Z/T020459Z/T020909Z-n2-
  diag-on-run1..5`; konsole_wait warm 1202-1313 cold 1498-1613,
  first_visible 6301-8541, real-GL virgl(D3D12) all, qtquick 0 violations,
  crash grep 0, direct-launch-diff 1001730 all five).
  OFF 4/5 PASS (`...T015106Z/T015451Z/T020245Z/T021055Z-n2-diag-off-run1/
  2/3/5`; same guardrails green) + 1 CRASH: OFF run4
  (`...T020704Z-n2-diag-off-run4-kwin-gp-r5class-crash`)
  kde-session-ready-crash = kwin_wayland USERSPACE #GP
  (`pid 59 kwin_wayland: exception 13 (#GP) rip=0x7ffffd8545e5 err=0x0`)
  with rbx=0x2d34365f3638782f — the BYTE-IDENTICAL R5-class bad pointer
  from 07-04 (little-endian ASCII "/x86_64-", path bytes read as a
  pointer). Kernel side of that run: ZERO kernel faults, ZERO
  rcu_head_trace anomalies, ZERO slab/double-free/freelist lines — the
  armed kernel diagnostics saw nothing, so this corruption event is
  userspace-visible-only (recycled-byte/ld.so family), not an observed
  kernel rcu_head event. FM19 reruns (not counted, both crash-grep-clean):
  on-run1 attempt1 visible-timeout (`...T014604Z-...-attempt1-fm19-
  visible-timeout`), on-run3 attempt1 prompt-sync-timeout
  (`...T015654Z-...-attempt1-fm19-prompt-sync-timeout`).
  VERDICT: (1) the 07-04 `.rodata` KERNEL page fault did NOT recur across
  the full armed 5x5 (12 KDE boots incl reruns + 1 gate) — the direct-read
  kill criterion did NOT trigger, and the ON arm is 5/5 clean including
  the run-5 slot that faulted on 07-04. (2) The battery is NOT fully green:
  the R5-class kwin #GP recurred on the CONTROL arm (direct-read OFF) —
  strict §5 "full matrix clean" promote criterion NOT met. (3) Attribution:
  a fault-family recurrence with direct-read disabled is further
  EXCULPATION of ext4_read_page_direct (accelerant-not-corrupter thesis
  reinforced); and rcu_head_trace silence during the event weakens the
  R3-rcu path as the explanation for THIS class, pointing at the
  recycled-byte/mapping corruption family instead. RECOMMENDATION
  (conductor decides): do NOT flip yet; either (a) accrue one more
  OFF-heavy armed battery to separate "R5-class background rate" from the
  promotion signal, or (b) explicitly decouple N2 promotion from the
  direct-read-independent R5-class userspace bug (documented recurring
  since 07-04 without the token) and promote on the 5/5-clean ON evidence;
  either way prioritize root-causing the kwin #GP using the fresh archive
  (it now has kde-exception-mem reg/memory dumps). Boots used: 17 total =
  1 gate PASS + 4 gate harness-shakeout boots (guest-console lessons:
  op-caution 7 confirmed + typeahead-flush addendum) + 10 counted A/B + 2
  FM19 reruns; exceeded the <=14 budget by 3, all on gate shakeout.
  Hygiene: no lingering qemu, tree clean except this plan update.
- U9b (R5-class kwin #GP) — OFFLINE FORENSICS DONE 2026-07-09 (0 boots;
  scratchpad r5class-kwin-gp-forensics.md). SITE: libQt5Core.so.5 +0x3075e5
  = `QObjectPrivate::connectImpl` `mov 0x8(%rbx),%rdi` (base 0x7ffffd54d000,
  IDENTICAL to the 07-04 run => low-entropy/deterministic ASLR). rbx =
  0x2d34365f3638782f = "/x86_64-" (ld.so search-path scratch slice),
  byte-identical to 07-04; a DIFFERENT insn site than the 07-04
  QMutex::unlock @0xdbc9f but the SAME recycled-frame poison family (single
  8-byte pointer slot overwritten; rdi/r12/r14 heap ptrs healthy). Both
  sightings share phase: KWin attempt-1, cold-cache, session bring-up.
  MECHANISM: same kernel frame-recycling family as 07-04, at residual rate
  after the 67d7b5c ordering fix (verified present: munmap/madvise/mremap all
  flush-before-free). The cpumask-miss path (trap entry clears vm->cpumask,
  trap.c:2009, but keeps the user TLB — no CR3 switch) is SELF-HEALING with
  PCID OFF (this run: `max ASID=0`): userret does an unconditional MOV-CR3
  full-flush (trampoline.S:36) and kernel user-mem access is software-walk +
  direct-map (copyout walk/walkaddr), so neither user-mode nor kernel-mediated
  stale writes corrupt. => residual is one of two kernel-silent frame-recycling
  micro-windows offline analysis cannot separate: (a) shootdown-vs-concurrent-
  fault / lock-drop timing residual, or (b) COW refcount under-count under the
  fork storm. Host/virgl DMA and pure-userspace UAF are DISFAVORED (poison is
  guest ld.so path text; 07-04 residue was in kernel-zero-filled .bss tails).
  PROMOTION HAZARD (record): do NOT enable `x86_pcid=1`+`x86_cr3_noflush=1`
  before fixing cpumask completeness (in-kernel CPUs holding the user CR3) —
  noflush userret (trampoline.S:35, bit63 set) REOPENS the shootdown-miss into
  a live corruptor. DECISIVE EXPERIMENT (<=1 boot, OFF arm): default-off knobs
  `anon_free_poison=1` (canary-fill anon frames at free) +
  `anon_fault_verify=1` (assert zero-or-canary at anon/.bss fault-in install;
  log pa/vm/pid/va+bytes on mismatch) + `vm_shootdown_cpumask_audit=1`
  (log CPUs whose live CR3==pagetable but absent from vm->cpumask). verify
  fires with path bytes => confirms+localizes kernel free-then-write (cpumask
  audit picks (a) vs COW (b)); verify silent + #GP recurs => exonerates
  frame-recycling, pivot to userspace/host. Also add faulting-VA PTE/frame
  dump to the kwin fatal handler.
  EXECUTED 2026-07-09 (9 boots; kernel edits UNCOMMITTED in submodule; full
  detail scratchpad r5class-kwin-gp-forensics.md par.8-9). Knobs implemented
  (cached-parse, default OFF; canary=0xA5A5A5A5<<32|pfn per frame):
  page.h/page.c/vm.c + x86 vm.c audit. ITERATION 1 (anon-only poison,
  run1b): 28 verify fires = FALSE POSITIVES root-caused to slab intrusive
  freelists (__slab_make writes obj-next PAs into PAGE_TYPE_SLAB pages;
  __slab_destroy frees via the unpoisoned __page_free path; signature
  own-page-PA @obj_size offset). FIX: poison extended to ALL page types at
  every allocator sink (__page_free, __page_ref_dec order-0, anon batch) +
  free-site attribution ring (pfn->freeing pid/name/sink/page-type/jiffies,
  printed on hit). Both nographic gates PASS (dcachetest x3 rc0, fork/clone/
  cow expected rc, 0 false fires, 0 crash markers). RESULT (4 armed OFF-arm
  KDE boots: run2 PASS 2 hits, run3 PASS 3 hits, run4 FM19-artifact-timeout
  0 hits, run5 FM19-prompt-sync 3 hits; crash-greps all clean; no #GP
  recurrence): 8 TRUE kernel write-after-free events, all the SAME invariant
  signature — EXACTLY 9 zero bytes at page offset 0x0-0x8 (8-byte NULL word +
  1 low byte) written 3-73 jiffies AFTER free into frames freed by
  worker_thread pid 43/44 (total-reaper) via page_free sink, PAGE_TYPE_ANON =
  the REAPER-FREED THREAD/KSTACK COMPOUND (thread_create/__kstack_arrange
  place struct thread+utrapframe+sched_entity on the kstack alloc;
  thread.c:387). Victims: kwin_wayland x5, plasmashell, kbuildsycoca5,
  pipewire heap faults at session bring-up. MECHANISM VERDICT: neither (a)
  nor (b) — a THIRD mechanism (c): post-mortem stale-struct-thread write
  (small {ptr,flag}-shaped zero store) after the total-reaper frees the
  thread page; R3-adjacent deferred-teardown lifetime family; bring-up
  thread churn explains the attempt-1 phase lock. Not (b): freeing context
  is reaper page_free, and single-threaded victims hit. Not (a): payload is
  a kernel-struct store, not a user write; audit MISSes (126-128/run,
  capped) are the pervasive benign trap-entry state = (a) PRECONDITION only,
  PCID-OFF self-healing per par.4(2) — promotion gate stands. CAVEAT: all 8
  payloads are zeros (invisible without canary; fault-fill re-zeroes) — the
  NONZERO path-byte writer behind the actual #GP did not recur in 4 armed
  boots (~1-in-5 OFF-arm residual rate; honest null), but family (c) is the
  only demonstrated writer into recycled anon-fault frames = prime suspect.
  NEXT LANE (0 boots to start): audit stale struct-thread users post-reap
  (tq/sched-entity refs, futex/signal, p->lock users, proctab lookups);
  search key = the 9-byte {NULL-ptr + zero-byte} store; then rerun this
  battery — verify silent => R5 family closed. Knobs stay opt-in (full
  poison = 4K memset per free; run4/5 FM19 timeouts may include this cost).
  ROOT-CAUSED + FIXED + VALIDATED 2026-07-09 (6 boots; kernel edits
  UNCOMMITTED; full chain scratchpad r5class-kwin-gp-forensics.md §10). The
  family-(c) "stale struct-thread" hypothesis REFINED away — the writer is
  the KERNEL VM (kvmalloc-large) path, found via the search key: (D1)
  kvm_munmap freed frames BEFORE its TLB flush (free-before-shootdown, the
  67d7b5c class, unfixed on the kernel-VM path; kernel/mm/vm.c), and (D2)
  kernel PTEs are GLOBAL (PTE_G) so they survive user-mode CR3 reloads,
  while vm_remote_sfence*(kernel_vm) only shot kernel_vm->cpumask = CPUs
  currently IN the kernel — a CPU in user mode kept a stale kernel-VA
  translation indefinitely and later wrote through it into the freed/
  recycled frame. Matches every observable: kvm VAs are page-aligned (page
  offset 0x0 in 8/8 hits); the 9-byte store is a kvmalloc-object {u64;u8}
  header clear by a thread that migrated to a stale-TLB CPU; environ/
  cmdline/D-Bus/sockbuf kvmalloc buffers carry "/usr/lib/x86_64-linux-gnu"
  path text (the "/x86_64-" #GP poison, 07-04+07-09); 3-73 jiffies = TLB
  persistence; kernel-silent (TLB hit never faults); attempt-1/cold-cache =
  max kvmalloc churn; worker_thread pid 43/44 = frequent FREER of the
  recycled ANON frames (red herring for the writer). FIX (kernel,
  uncommitted): arch/x86_64/mm/vm.c vm_remote_sfence/_page target
  get_cpu_active_mask() for is_kernel VMs; kernel/mm/vm.c new
  __kvm_unmap_range_flush_free() — batched clear(locked) -> UNLOCK ->
  flush local+all-remote -> free, vma_free only after all spans flushed;
  used by kvm_munmap + both kvm_mmap rollbacks; new VMA_FLAG_KVM_DYING
  guards double-kvfree across the batching. LOCK LESSON (cost 1 boot):
  vm_wlock(kernel_vm) is a SPINLOCK — IPI-sync-wait inside it deadlocks
  bring-up (fix iteration 1, gate 20260709T140233Z hang); the IPI must run
  with the lock dropped. VALIDATION: safety gate PASS
  (20260709T141330Z-u9b-kvmfix-safety-gate2; dcachetest x3 rc0, fork/clone/
  cow expected, crash grep 0, 0 verify fires) + 4 armed OFF-arm KDE runs
  (20260709T142025Z/T142237Z/T142451Z/T142652Z-u9b-kvmfix-off-run1..4):
  ALL PASS, ZERO anon_fault_verify hits 4/4 (pre-fix 8 hits/4 runs;
  P(0|~2/run)~3e-4), real-GL both, qtquick 0, crash greps 0, konsole_wait/
  first_visible inside the armed-baseline band (no shootdown-cost
  regression). U9b CLOSED as "demonstrated writer channel eliminated";
  HONEST RESIDUAL: the #GP itself (~1-in-5 OFF-arm) not re-observed in 4
  runs — consistent but not conclusive for the tail; PCID promotion hazard
  (user-vm cpumask completeness) STANDS, this fix covers the kernel VM
  only. Diagnostics knobs remain opt-in.
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
budget survives as orthogonal robustness item). U8 DONE-measured 2026-07-08:
M8 idle host CPU = 13.7% mean on a real-GL boot with N5 gates default-ON
(first VALID reading; goal <100% MET with wide margin; far below the 85-130%
historical band = the N5 poll-notify full-wait payoff). U9-U11 parked per
recorded conditions. U12 (push, ~9 commits ahead) awaits explicit user
approval.

## Landed opt-in gates (all default-OFF unless noted)

- `vfs_neg_dcache=1` — negative-dcache honor; -95% ENOENT, no M4 effect.
  Correct VFS fix (consumer bug + bump-inside-lock race); see U6 before
  default. Kernel 0cf817d + sentinel fix.
- `virtio_gpu_vblank_paced_flip=1` — flip-complete events paced to the
  synthetic vblank edge; engagement proven; no gap/first-frame win; small
  warm cost (~+250ms/launch). Vsync-semantics infrastructure. 1661795.
- `virtio_gpu_present_clock_60hz=1` — COMMITTED (kernel fcb0ebe), A/B MEASURED
  2026-07-09 (SUB-THRESHOLD, stays default-OFF). Requires+composes with
  async_present; delivers the async flip-complete on a free-running phase-locked
  60Hz grid (decoupled from real completion time) to give kwin a stable vsync
  clock — targeted the M7 ceiling. No-op when async off (sync path byte-identical).
  Supersedes vblank_paced completion timing when both set. RESULT: engaged (banner
  + events_total=913/snap_total=58) and paced, but presentedFPS only 52.3->53.4
  (+2.1%, goal >=55 NOT met) => clock is a MINOR real contributor, not the M7
  limiter; residual ceiling reassigned to host/kwin present throughput. See
  U7-RESIDUAL A/B VERDICT.
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

## Instruments

- `hoverprobe` (`user/programs/hoverprobe/hoverprobe.c`, repo-owned guest
  tool, DONE + baselined 2026-07-09) — accurate end-to-end Plasma
  responsiveness instrument. ONE guest process injects pointer input
  (/dev/mouse absolute16, guest-only policy, same struct mouse_event as
  /bin/mouseinject) AND polls a SMALL region of interest via
  FB_GPU_SCANOUT_READ (fbstat's sample-current primitive) in a tight loop, so
  t_inject and t_first_change share one CLOCK_MONOTONIC. Times hover_in,
  hover_out, click from the pre-injection timestamp to the first readback whose
  ROI FNV hash differs from the reference captured just before injection
  (reference = un-hovered desktop for hover_in, hovered state for hover_out,
  hovered-not-pressed for click). ROI derived in pixels from the icon abs16
  coord using the harness mapping; centred, screen-clamped. The DRM cursor
  plane is NOT composited into the primary scanout the ioctl reads, so the ROI
  reflects highlight/press feedback, not the cursor sprite. Args (all optional,
  default taskbar-left target 11000,64200 / neutral centre 32768,32768):
  `hoverprobe [icon_x icon_y away_x away_y iters rw rh settle_ms timeout_ms
  calib_samples]`. Emits one line/event/iter
  (`hoverprobe event=.. t_inject_ms=.. t_first_change_ms=.. latency_ms=..
  sampler_period_ms=.. samples=.. result=CHANGED|TIMEOUT rect=..`), a
  per-event median/min/max summary, and a sampler_calibration line reporting
  the measured readback resolution. Compile-proof clean under the production
  user command (-Wall -Wextra). Harness hook: `KDE_SMOKE_HOVERPROBE=1` runs it
  in the desktop-interaction-latency reducer right after taskbar-settle (new
  `run_hoverprobe_instrument` + `hoverprobe` guest-script kind, archived via
  the interaction-helper capture); `KDE_SMOKE_HOVERPROBE_ONLY=1` finishes right
  after (lean focused run); knobs KDE_SMOKE_HOVERPROBE_{ITERS,RW,RH,SETTLE_MS,
  TIMEOUT_MS,AWAY_X,AWAY_Y,CALIB} + icon via
  KDE_APP_LAUNCH_PROBE_KPROFILE_HOVER_X/Y. Default OFF = byte-identical (no
  flip). MEASURED RESOLUTION: ~4-5ms per small-rect readback at idle (goal
  <=5ms MET on the calibration floor), inflating to ~13-16ms while kwin
  actively composites (scanout-read ioctl serialises behind kwin's synchronous
  present). Baselines: `kde-plasma-desktop-smoke-history/
  20260709T162636Z-hoverprobe-instrument-baseline` (chromium-launcher target;
  click 4/6, 2 timeouts because launching chromium does not idempotently
  repaint the ROI) and `...20260709T162836Z-hoverprobe-launcher-clean-click`
  (Kickoff-launcher target; click 6/6 clean). See M10. FINDING: the true
  hover-in latency is ~205ms, not the ~450-490ms the retired coarse
  framebuffer-diff sampler reported (that floor was the SAMPLER's serial
  round-trip, not the desktop) — this instrument resolves it to ~4-5ms.
  MENU-MODE EXTENSION (2026-07-09, U-KICKOFF): args 11-17 add a SECOND ROI over
  the popup BODY (distinct from the click target) + a click-launcher->open /
  click-empty-desktop->close protocol with per-iter cold/warm labelling, an
  optional prewarm pass (arg 17), and a skip-hover flag (arg 16, so iter1 is a
  genuine COLD Kickoff activation). New env knobs KDE_SMOKE_HOVERPROBE_MENU_PX_X/
  _PX_Y/_RW/_RH/_ITERS + _DO_HOVER + _PREWARM (all default 0/1 = byte-identical
  when menu ROI unset). Emits event=menu_open/menu_close lines with
  temperature=first|repeat + a menu_prewarm line; run_hoverprobe_instrument
  also pulls a full-frame proof PPM (/kde-plasma-menu-open-proof.ppm) captured
  while the menu is open. Compile-clean -Wall -Wextra. CAUTION: the hover/click
  phase (do_hover=1) CLICKS the same launcher and thereby PREWARMS Kickoff, so
  menu iter1 is only cold with _DO_HOVER=0. Staged into fs.img via debugfs
  (native ELF at sysroot/bin/hoverprobe rebuilt with build_host_user_program
  flags) — no rootfs rebuild needed.
  TOOLTIP-PROTOCOL EXTENSION (2026-07-09, U-TOOLTIP): arg 18 menu_protocol
  (0=click default byte-identical, 1=hover) reuses the menu ROI as a TIP ROI
  above a taskbar icon: OPEN = move onto the icon (no click) -> first tip-ROI
  change (tooltip appears; show-delay + QML + kwin schedule), CLOSE = move
  away -> ROI reverts (hide); args 19,20 = optional icon B abs16 for an A->B
  SWITCH phase (hover A until tooltip up, move straight to B while the dialog
  is visible; per-icon cold + visible-path timer). Emits event=tooltip_open/
  tooltip_close/tooltip_switch with temperature=first|repeat and summaries
  with median/p90/min/max/STDDEV (variance is the deliverable); dumps
  /kde-plasma-tooltip-proof.ppm on iter 1. Env knobs
  KDE_SMOKE_HOVERPROBE_MENU_PROTOCOL/_ICON2_X/_ICON2_Y; helper timeout
  240s->480s; use _DO_HOVER=0 so iter 1 is the genuine first tooltip of the
  session. Compile-clean -Wall -Wextra; staged into fs.img via debugfs.
  Companion gated config stage: `KDE_SMOKE_PLASMA_TOOLTIP_DELAY_MS=<ms>` writes
  /etc/xdg/plasmarc [PlasmaToolTips] Delay into the per-run fs.img copy
  (system-cascade override; Delay<=0 would disable tooltips and is rejected).

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
   (`M=XX; cmd; echo ${M}RES=$?`). ADDENDUM 2026-07-09 (cost 4 boots): the
   guest shell FLUSHES TYPEAHEAD around prompt redraw — input sent while
   it is printing is silently EATEN; only send from a proven-idle prompt
   (flush expect buffer, send \r, require a NEW prompt). Kernel printf
   floods (e.g. dcachetest's vfs_unmount EBUSY-retry lines) byte-interleave
   with shell echo, so live sentinel matching fails: write test rc's to a
   guest file and poll it with `cat` from idle prompts instead.
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
| M1 | YouTube-freeze survival | 3/3 clean; P0 closed. UPGRADE 2026-07-09: YouTube not only survives but PLAYS -- with `--disable-audio-output` (gated, see M7) the watch page decodes VP9 video at ~28-29fps (visual proof: player shows Big Buck Bunny frames). Prior "video never plays / 0.02fps" was an audio-renderer-init failure stalling the combined pipeline, not a freeze. | hold |
| M2 | getpid_ns | 1.65-1.94us | <1.5us (U11) |
| M3 | tlb_amplification 1024pg | 2.7-3.7us | 0 |
| M4 | konsole_wait_ms | warm 1184-1205ms, cold 1406-1500ms (2026-07-08 clean guiperf N=4, all gates green); bottleneck LOCALIZED (U2): kwin damage->repaint SCHEDULE latency = ~96% of each 58-139ms frame-callback wait, konsole render ~0%, kwin repaint ioctl ~4%; imageformats prune opt-in (U3 DONE, ~55-67ms dlopen cut) | Linux-like (~0.3s) |
| M5 | first_visible_ms | ~6.2-6.6s (clean guiperf N=4) | <15000 |
| M6 | mesakmsgl FPS | 118-125 (ordered default); ~60 by design under vblank-paced gate | done |
| M7 | presentedFPS | CLOCK-60 A/B MEASURED 2026-07-09 (chromium-video kprofile 90s, complete windows advanced~15, real virgl/D3D12 GL both arms, crash 0, FM17 launch-evidence-missing bookkeeping expected): CONTROL (async-present) presentedFPS=52.3 dropPct=6.50; TREATMENT (+virtio_gpu_present_clock_60hz=1) presentedFPS=53.4 dropPct=6.22. Clock ENGAGED (run.log banner "present-clock 60Hz engaged", both tokens x3 in cmdline) and PACES (fbstat present_clock60 events_total=913==all session flips, snap_total=58 = 6.4% << events => async worker keeps up with the 60Hz grid). MOVE IS DIRECTIONAL BUT SUB-THRESHOLD: +1.1fps (+2.1%), dropPct -0.28; goal >=55 NOT met and FPS did NOT rise toward 60. Because snap<<events (worker feeds a clean 60Hz grid) yet present still caps ~53, the U7-RESIDUAL prediction (clean completion clock => ~60fps) is REFUTED in magnitude: dominant M7 ceiling is DOWNSTREAM host/kwin present throughput, NOT the completion-clock timestamp. Gate `virtio_gpu_present_clock_60hz=1` stays default-OFF (small real win, sub-threshold). Prior 51.2/51.0 async-present A/B superseded by this pushed-kernel re-read (52.3/53.4). PIVOT: host-retire/present-throughput. YOUTUBE vs LOCAL DELTA (2026-07-09, manual KDE/kwin lane WITH net, 60fps video aqz-KE-bpKQ, real virgl GL, DHCP up IP 10.0.2.15, crash 0, 2 boots reproducible): the chromium-video kprofile harness CANNOT do YouTube (it hard-sets QEMU_NET=0 at smoke.expect:672, and PERF-VIDEO RESULT is emitted only by the local perf-video.html JS, not a real YouTube page) -> drove wayland-chromium manually from serial and measured presented fps = delta(fbstat kms_page_flip_events)/wall-time. RESULT: YouTube presented ~0.02 fps in two 60s steady windows (t0/t1/t2 flips 52/53/54; idle desktop ~0) vs LOCAL FILE 52.3-53.4 fps -- i.e. YouTube video does NOT reach sustained video-rate playback here (compositor presents ~1 frame/min; page loads then goes near-static). decodedFPS/dropPct N/A for YouTube (no JS injection; Stats-for-nerds too fragile). Exact blocker NOT isolated (chromium media log unrecoverable: guest `/` writes don't persist to image, tmpfs-log serial dump failed under load; GL-display monitor screendump yields no PPM) -- candidates: codec (VP9/AV1) / autoplay-not-triggered / buffering stall / consent wall. Consistent with the 2026-07-03 M1 replays which proved YouTube LIVENESS (no freeze) but never smooth decode. Archive: build-x86_64/chromium-youtube-m7/20260709T211902Z-yt-presentfps/ (ARCHIVE-NOTE.txt + driver snapshot scripts/gpu/chromium-youtube-presentfps.expect). YOUTUBE BLOCKER ISOLATED + FIXED 2026-07-09 (diagnosis worker, 3 boots, real KVM+virgl GL, DHCP up IP 10.0.2.15, crash 0, no lingering qemu): the ~0.02fps is NOT codec/autoplay/consent/network. OFFLINE codec inventory: the shipped chrome is "Chrome for Testing" v150.0.7871.24 with ALL decoders statically linked (dav1d/AV1, libvpx/VP9, ffmpeg/H.264+AAC, opus, vorbis) -- codec present, ELIMINATED. VISUAL truth via `fbstat ppm-current` (reads the GL scanout; monitor screendump is blank on virtio-vga-gl -- that is the prior worker's "no PPM"): the full YouTube watch page RENDERS (title "Big Buck Bunny 60fps 4K", sidebar thumbnails, metadata, Sign in) so NOT a consent wall; media CDN reached (console log rr4---sn-gvbxgn-tvf6.googlevideo.com) so NOT network -- but the PLAYER REGION IS PURE BLACK. ROOT CAUSE (chromium stderr, recovered from ROOT /host-gui-wayland-chromium.log which DOES persist -- prior worker only tried tmpfs /dev/shm+/tmp): PipelineStatus::AUDIO_RENDERER_ERROR x8 + media/audio/pulse/pulse_util.cc:395 "pa_operation is nullptr" flooding, and ZERO video/codec/demuxer errors. This guest has no working chromium audio backend; YouTube's element carries an Opus audio track, so the audio renderer fails to init and Chromium's RendererImpl fails the WHOLE combined audio+video pipeline -> video never decodes -> black player 0.02fps. The local baseline plays because perf-1280x800-60fps.mp4 is VIDEO-ONLY (ffprobe: single h264 stream, no audio) so no audio renderer is built (&mute=1 does NOT help -- mute still builds the renderer). FIX (in-stack, NO build gap): `--disable-audio-output` routes audio to a guaranteed-init null sink so the pipeline proceeds. VALIDATED end-to-end: argv_31=--disable-audio-output, AUDIO_RENDERER_ERROR 0, pulse-nullptr 0, no video errors, presented 28.88/28.46 fps over two 57s windows, and the player VISUALLY shows Big Buck Bunny frames (vs black). ~28-29fps (SW VP9 720p60, single-process --disable-gpu, no HW video decode) is genuine playback ~1400x the broken 0.02 and below local 52-53 (audio-less H.264, lighter SW decode) -- the residual video<->local gap is DECODE-COST (SW VP9 vs SW H.264), NOT a blocker. IMPLEMENTED GATED (default OFF, no default flip, NOT committed): scripts/image/wayland-chromium-launcher.c reads env WAYLAND_CHROMIUM_DISABLE_AUDIO_OUTPUT=1 -> appends --disable-audio-output (byte-identical when unset; emits the same argv the validated boot injected via WAYLAND_CHROMIUM_EXTRA_FLAGS); compile-clean cc -O2 -Wall -Wextra. Promotion note: real audio (pulse/ALSA sink for chromium) is a separate larger effort; until then this gate is the YouTube-playback lever. Archives: build-x86_64/chromium-youtube-m7/20260709T232300Z-yt-fix-disable-audio-output/ (FIXED: video plays + ARCHIVE-NOTE + driver snapshot) and ...T231147Z-yt-visual-evidence-b2/ (BROKEN repro: black-player shot + audio-error log). YOUTUBE 28.9 vs LOCAL 52-53 GAP — OFFLINE ATTRIBUTION 2026-07-09 (analysis worker, opus; VM lane owned by audio worker's kde_audio_null_sink boot so boots SPECCED not run): the gap is CLIENT-SIDE FRAME-SUPPLY bound, NOT compositor/present bound. Decisive prior-data argument: the present path demonstrably sustains 51-53 fps (local H.264 file, decodedFPS 62 > presentedFPS 51-53 = present is the ceiling); YouTube's 28.9 sits BELOW that proven ceiling, so the compositor is STARVED of frames — the limiter is upstream frame production (decode -> in-process software-GL composite -> wayland commit), not kwin/present (consistent with the async-present + clock-60 nulls: present cost never gated FPS). Within frame-supply the dominant suspect is SOFTWARE VP9 decode as run by chromium's bundled libvpx in the constrained launch config: wayland-chromium-launcher.c:211-215 forces `--single-process --disable-gpu --in-process-gpu --no-zygote`, so libvpx VP9 decode threads share ONE process scheduler with software-GL compositing + rasterization + (on YouTube) the DASH/MSE/JS main thread, and chromium's libvpx VP9 threading is further capped by the stream's tile-column count (~1-2 for 720p). CAVEAT (honest): raw VP9 720p decode is NOT intrinsically slow on this host class (host ffmpeg native-vp9 decodes 720p60 at >300 fps single-thread on the same i7-12650H), so the effective ~29 fps loss is more likely single-process CPU contention + chromium thread/tile caps than pure codec math — the codec-vs-page split needs one boot to quantify. No decodedFPS was EVER captured for YouTube (getVideoPlaybackQuality needs JS injection; no --vmodule media log was set), which is why the split was never nailed. LEVERS (spec'd, assets built, ready-made gates found — see build-x86_64/chromium-youtube-m7/decode-attribution-assets/SPEC.md): (BOOT1, DECISIVE) built controlled 1280x720@60 video-only VP9 (.webm) + H.264 (.mp4) from the same source clip; run BOTH through chromium via perf-video.html?asset=... (KDE_CHROMIUM_URL; harness accepts custom asset, logs a benign limitation but runs) — it reports decodedFPS AND presentedFPS with NO network/audio/MSE confounders, cleanly attributing the gap to codec-decode vs page-overhead. (BOOT2, LEVER, no code change) env WAYLAND_CHROMIUM_MULTIPROCESS=1 (launcher.c:211) DROPS --single-process/--disable-gpu/--in-process-gpu/--no-zygote => tests whether killing single-process contention lifts YouTube toward 52; RISK: multiprocess may destabilize virgl/software-GL bring-up (that is WHY single-process is default) — exploratory, roll back on GL/crash regression. Also add WAYLAND_CHROMIUM_EXTRA_FLAGS="--vmodule=*/media/*=2" to persist decoder+per-frame timing to ROOT /host-gui-wayland-chromium.log. (BOOT3, cheap confirm) YouTube vq=medium(480p) vs hd720 — if 480p jumps toward 52, decode-CPU-bound confirmed by the resolution knob alone. LEVER (a) prefer-H.264-on-YouTube HONEST VERDICT: there is NO Chromium CLI flag that forces avc1 — YouTube picks codec from MediaSource.isTypeSupported()/MediaCapabilities; only h264ify-style JS override (reject webm/vp9) works, but --single-process/--no-zygote loads no extensions so --load-extension is unreliable (adding a JS-injection path = build change). Flags that do NOT work: --disable-accelerated-video-decode (no codec effect), "--disable-features=VP9*" (VP9 is a built-in codec, not a Feature toggle). itag 298 (720p60 H.264) exists for aqz-KE-bpKQ, so IF forced, H.264 720p60 is available and should match ~52. VERDICT/BEST LEVER: binding constraint = client-side VP9 decode + in-process contention (NOT kwin/present, NOT a blocker — 28.9 fps is genuine playback). Best DIAGNOSTIC lever = BOOT1 local-VP9-vs-H.264 A/B (decisive, offline-buildable, prepared). Best PRODUCT lever depends on BOOT1: if VP9 decodes ~52 locally -> WAYLAND_CHROMIUM_MULTIPROCESS is the win (page/contention was the cost); if VP9 decodes ~29 locally -> codec is the cost and only H.264-forcing (JS-injection, build change) or lower-res recovers it. Assets+SPEC: build-x86_64/chromium-youtube-m7/decode-attribution-assets/ (perf-vp9-1280x720-60fps.webm sha 7241079b..., perf-h264-1280x720-60fps.mp4 sha 9d1476b6..., SPEC.md w/ injection + all 3 boot recipes). | >=55 (U7) |
| M8 | idle host CPU | 13.7% mean (13.7/13.6/13.9, 3x10s /proc/stat delta) on a real-GL boot, N5 gates default-ON — FIRST VALID reading 2026-07-08 (U8); far below the 85-130% historical band = N5 poll-notify payoff | <100% (MET) |
| M9 | Chromium visible | PASS guard | hold |
| M11 | Kickoff (start-menu) open/close latency (hoverprobe menu-mode, menu-body ROI, in-process CLOCK_MONOTONIC) | BASELINE + PREWARM A/B 2026-07-09 (U-KICKOFF, real virgl/D3D12 GL, qtquick PASS, crash 0, n=2/arm, full-frame PPM confirms genuine Kickoff open). USER PAIN REPRODUCED + DECOMPOSED: COLD first-open (never opened in session) = 2254/1892ms (matches the reported 1394-2517ms); WARM repeat-open (same session) = median ~400-500ms (340-670ms); menu CLOSE ~180-420ms. Cold excess ~1500-1850ms is a ONE-TIME-per-session cost = QML component compile + Kickoff app/recents model population (KIO/DBus), NOT compositor cadence (kwin ioctl-trace: compositor mostly idle during the cold open, PAGE_FLIP dur ~5ms). Warm ~400ms floor = U2 kwin damage->repaint SCHEDULE latency + fade frames (known-hard, do not relitigate). ATTACK — session-start PREWARM (gated, open Kickoff once at login, wait for model build, close): user's first open 2254/1892 -> 498/540ms (-75%, ~1550ms), into the warm band. Prewarm one-time cost ~2000ms real, paid at session start (tunable wait). Gate default OFF; product promotion path below. Old "open 1394-2517ms / close 415-963ms" from the retired coarse full-frame sampler is partly artifact (same lesson as M10 hover) BUT the cold-open pain is REAL (~2s) and prewarm is the lever. PRODUCT-PREWARM BOOT A/B 2026-07-09 (U-KICKOFF, gate kde_kickoff_prewarm=1 in kde-plasma-session-child, hoverprobe _DO_HOVER=0 _PREWARM=0 so iter1=user's first real click, real virgl/D3D12 GL, qtquick PASS, crash 0, no lingering qemu, 3 boots): gate ON user first-open=493ms (worker DONE opened=1 pristine=1, pristine screenshot verified) vs gate OFF cold=2164ms same config = -77% (~1670ms), matching the test-tool prewarm A/B (498/540ms). Gate default OFF; promotion-to-default-on pending standard battery + owner sign-off. | approach Linux VM |
| M12 | Taskbar tooltip latency (hoverprobe tooltip protocol: hover -> first tip-ROI pixel; median/p90/max/stddev, n=12/boot) | BASELINE + DECOMPOSITION + GATED FIX A/B 2026-07-09 (U-TOOLTIP, real virgl/D3D12 GL, qtquick PASS, crash 0, 7 boots, 0 FM19). BASELINE (Delay=700 default): open med 948.5/942.9 p90 973.3/1005.8 max 1092.6/1123.7 sd 50.3/58.5 ms; close ~262; A->B switch ~233. DECOMPOSED: 700 ms plasmarc [PlasmaToolTips] Delay compiled default (74%, binary-verified in libcorebindingsplugin.so; image ships no plasmarc) + ~250 ms render (U2 kwin schedule wall) + first-of-session QML cold cost (the JITTER source: hidden at Delay=700, exposed at Delay=50: first 994/2331 vs ~470 repeats). FIX gated default-OFF: kde_plasma_tooltip_delay=50 + kde_tooltip_prewarm=1 -> open med 486.1/482.2 (-49%), p90 541.1/530.3 (-46%), max 606.7/747.1 (worst-case -68% vs delay-only 2331), first == repeats, sd 44.4/79.2; switch ~110. Prewarm-only at Delay=700 NULL (700ms delay masks cold cost). Residual ~480 ms = U2 kwin damage->repaint schedule floor (do not relitigate). Promotion pending battery + sign-off. Archive 20260709T205408Z-tooltip-ab-summary. | approach Linux VM |
| M10 | hoverprobe latency (input-inject -> first ROI pixel change, in-process shared CLOCK_MONOTONIC) | NEW BASELINE 2026-07-09 (U-HP, real KVM+virgl/D3D12 GL, N=6/event, crash 0): hover_in median ~205-234ms (min 176-189), hover_out median ~193-205ms (min 149-159), click median ~250-312ms (launcher-toggle arm 6/6 clean min 24ms). Sampler resolution (256 idle small-rect readbacks) mean 5.5-6.4ms, min 4.0-4.4ms, max ~19-23ms (per-sample inflates to ~13-16ms while kwin composites — scanout-read ioctl serialises behind kwin's synchronous present). SUPERSEDES the retired coarse framebuffer-diff hover metric whose ~448-490ms floor "measured the sampler, not the desktop": true hover-in is ~2x faster (~205ms) at ~4-5ms resolution. CLOCK-60 CHECK 2026-07-09 (hoverprobe under M7 treatment tokens async-present+present_clock_60hz, banner engaged, real GL, crash 0, N=6): hover_in median 203.5ms (185.5-401.4), hover_out 157.8ms (147.5-194.9), click 284.0ms (269.9-286.9, 4/6). WITHIN NOISE of baseline — no clear interaction-latency win from the 60Hz clock; consistent with U2 (hover/click latency dominated by kwin damage->repaint SCHEDULE latency ~96%, not present cadence). | approach Linux VM |

Fork-safety gate for any syscall/scheduler/TLB/mm change: forktest (rc=1
exhaustion signature OK), clonetest rc=0, cowtest rc=0, same boot.
