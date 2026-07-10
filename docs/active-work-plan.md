# Active xv6 Work Plan

Last updated: 2026-07-10 (recut; prior plan archived verbatim in
docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md under
"ARCHIVED SNAPSHOT ... 2026-07-10"). Single-plan rule: this is the only
live plan file; verbose lane histories live append-only in the history
file; proof runs archive under
build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/ (or
chromium-youtube-m7/). Branch codex/host-linux-abi-shell-port-ff; kernel
v6-kernel; user v6-port — all pushed through 03cdf0a lineage.

## Mission (current phase)

Turn the validated opt-in wins into the DEFAULT user experience and close
the remaining media gaps: promote the proven responsiveness gates via the
standard battery, make YouTube/video genuinely good (audio stream fix +
the contention lever), and keep every correctness guardrail. Conduct via
opus workers; the conductor orchestrates only.

## PROVEN OPT-IN GATES awaiting default-promotion battery

Each validated A/B'd, default OFF; promotion = same-session A/B +
explicit-off control + KDE active-sample + Chromium launch-only +
M4/M5/M8 in noise (+ interactive check for input-semantics changes):
- kde_kickoff_prewarm=1 — first menu open 2164->493ms (-77%).
- kde_plasma_tooltip_delay=50 + kde_tooltip_prewarm=1 — tooltip
  945->486ms median, p90 -46%, jitter tamed (first-of-session cold cost).
- WAYLAND_CHROMIUM_DISABLE_AUDIO_OUTPUT=1 — YouTube plays (0.02->28.9fps);
  superseded if U-AUDIO stream fix lands.
- kde_audio_null_sink=1 — valid default sink for apps (NOT sufficient for
  chromium; see U-AUDIO).
- virtio_gpu_async_present=1 (+ virtio_gpu_present_clock_60hz=1) — video
  52.3->53.4fps, drops -22%; small but real; promote only as a pair after
  battery.
- XV6_ROOTFS_LDSOCACHE=1; KDE_APP_LAUNCH_PROBE_QT_MINIMAL_IMAGEFORMATS=1 —
  cleanliness/dlopen (-56%) levers, image-level.
- vfs_neg_dcache=1 — correct + substrate fixed (U6); -95% ENOENT; no M4
  effect; promote for correctness/cleanliness only.

## ACTIVE LANES

- A1 (IN FLIGHT, worker): VP9/YouTube contention discriminator — JOB1 done:
  codec is NOT the constraint (controlled VP9-vs-H264 table in archive);
  pending: WAYLAND_CHROMIUM_MULTIPROCESS=1 boot (single-process contention
  test; launcher forces --single-process --disable-gpu so decode fights
  SW-GL compositing) + 480p knob if ambiguous. Target: YouTube 28.9->~52.
- A2 (IN FLIGHT, same worker): U-AUDIO localization — paplay vs
  pipewire-pulse probe splits server-bug vs chromium-stream-negotiation.
  Kernel is NOT the gap (virtio_snd.c + OSS/ALSA ABI exist and register);
  userspace pipewire/wireplumber/pipewire-pulse all ship; failure is in
  the pulse playback-stream path (MixableOutputStream -> pa_operation
  nullptr) even with a valid default null sink.
- A3: promotion batteries for the gate list above (start with kickoff
  prewarm + tooltip pair — the direct user-feel wins).
- A4 (durable audio): after A2 verdict, fix the real stream path (server
  bug or negotiation), then QEMU_AUDIO=virtio + spa-alsa hw:0 flakiness
  for audible output (op-caution: host WSLg pulse state, wsl restart).
- A5 (the wall, long-term): kwin 5.27 damage->repaint schedule floor
  (~250-480ms popups; repaint-on-damage) bounds hover/tooltip/menu/frame
  waits. Kernel-side exhausted (4 proven-mechanism slices all metric-null:
  neg-dcache, vblank pacing, async cursor, async present). Path: kwin 6.x
  + AMS (U5 atomic flip-events landed + guest-validated as prerequisite)
  or compositor alternatives. Do not relitigate with kernel levers.
- A6 (parked, conditions recorded in history): N2 direct-read promotion
  (ON-arm 5/5 clean; retry after another OFF-heavy armed battery);
  R5-watch: family (c)/kvm stale-TLB FIXED+validated (kernel 3254ce6);
  the 07-04/07-09 kwin #GP writer matched family (c) signature — watch
  with the armed knobs (anon_free_poison/anon_fault_verify/
  vm_shootdown_cpumask_audit) in future batteries; PCID stays OFF until
  user-vm cpumask completeness (hazard recorded).

## Scoreboard (2026-07-10)

| # | Metric | Current | Goal |
|---|---|---|---|
| M4 konsole_wait | warm ~1.19s (kwin wall bounds it) | ~0.3s native-like |
| M7 video local | 53.4 (gates on) | >=55 |
| M7 YouTube | 28.9 (audio-flag) | ~52 (A1) then >=55 |
| M8 idle CPU | 13.7% | <100% MET |
| M10 hover/click | ~205 / ~250ms | kwin wall ~floor |
| M11 menu open | 493ms (prewarm) | hold; battery -> default |
| M12 tooltip | 486ms median (gates) | hold; battery -> default |
| M1 YouTube liveness | pass; now PLAYS (28.9) | smooth 52+ |

Instruments: hoverprobe (hover/click/menu/tooltip modes, ~4-14ms res);
kwin-ioctl-trace-preload (classify-on-first-ioctl); yt-presentfps driver;
armed R5 forensics knobs; kprofile suite. Fork-safety gate: forktest rc=1
exhaustion-OK, clonetest/cowtest rc=0.

## Rules (binding, unchanged)

- NO un-gated default flips; promotion only via the standard battery.
- CODE-FIRST/OFFLINE-FIRST; adversarial review before kernel boots; boot
  before believing; N=1 timing wins are noise until repeated (burned 3x).
- Workers: synchronous waits ONLY (no monitors — they strand the worker);
  pgrep self-match trap; serial console cautions (short cmds, marker-vars,
  first-char drop after bracketed-paste prompt); harness hardcodes kernel
  path (expect:6) — bisect by sha-verified swap; fbstat stats-mode needs
  current-lineage guest tools (append-only ABI); FM19 visible/artifact
  timeout = archive + rerun once, ALWAYS crash-grep first (FM24 masking);
  qtquick-accel FAIL streak after image rebuild = EGL-init race signature
  (rebuild again; U1 gated readiness probe exists); no lingering qemu;
  <=2 boots/slice + batteries; commit at verified checkpoints
  (deepest-first), push only with user approval (standing for this
  branch lineage since U12).
- Closed lanes (do not reopen without new evidence): all loader/config M4
  levers, single-lib stubs, kernel syscall/VFS-cost-as-M4, unlocked-wait
  (superseded), present-clock magnitude, PCID/noflush (needs INVPCID +
  cpumask completeness).
