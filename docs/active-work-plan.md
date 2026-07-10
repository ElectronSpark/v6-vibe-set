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
opus(or terra) workers; the conductor orchestrates only.

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

- A1 (IN FLIGHT — one valid hd720 run; N=2 still unsatisfied):
  Codec capacity is NOT the constraint: controlled local 1280x720@60 H.264
  and VP9 reach about 52-54 presented fps, so YouTube frame supply/selection
  and the later local present ceiling are separate walls. The EGL revision-2
  advertisement withdrawal closed external-GPU initialization without faking
  hardening semantics; nested Mesa `7626b94e` and ports `1a15db74` are
  committed on `codex/a1-egl-context-closure-20260710`.
  Canonical real-KVM+virgl multiprocess A/B is control 28.295 vs treatment
  28.960 presented fps (+0.665/+2.35%): METRIC-NULL and far below 52.
  The original N=2 media-probe artifacts
  `chromium-youtube-m7/20260710T102211Z-a1-media-probe-t1-mp1-audio1`
  and `.../20260710T102429Z-a1-media-probe-t2-mp1-audio1` are formally
  INVALID/NULL because the guest helper used unsupported grep options, but
  their canonical 24-row logs prove stable 640x360, quality `medium`,
  33.333/33.334ms rVFC cadence, with `hd720` advertised. The corrected
  option-free helper independently passes NO-BOOT review; strict historical
  replay is a valid envelope with `SOURCE_PROVEN=0`, hence no FPS or
  PERF-VIDEO credit. Do not spend a helper-only rerun.
  The bounded default-OFF `YT_FORCE_HD720=1` selector now has independent
  adversarial NO-BOOT PASS. Its exact arm is multiprocess=1, audio-disable=1,
  media-probe=1, forensics=0. In MAIN world it waits for a playing player with
  `hd720` advertised, then calls `setPlaybackQualityRange('hd720','hd720')`
  before `setPlaybackQuality('hd720')`; emitted rows are raw evidence and only
  the host parser may grant quality/cadence/FPS credit. Adversarial missing,
  duplicate, reordered, spoofed, drift, wrong-dimension, 30-fps, and API-failure
  mutations all reject. Review ended with an exact zero QEMU process count;
  this is implementation proof only and makes no performance claim.
  The authorized sole-VM measurement retained
  `build-x86_64/chromium-youtube-m7/20260710T170200Z-a1-hd720-t1-mp1-audio1`
  and its serial retry
  `build-x86_64/chromium-youtube-m7/20260710T170400Z-a1-hd720-t1r1-mp1-audio1`.
  T1 is INVALID/NULL on a transient serial `fbstat` probe timeout. The retry
  used real KVM+virgl and passed the GPU-role, hd720 selector, and stable
  1280x720 gates, but the source/FPS parser rejected
  `sample-rvfc-presented-first-jump`. Raw diagnostics were sample 1
  callbacks=7/presented=188; final callbacks=796, presented_first=88,
  presented_last=1326, delta=1238, media_delta=21.416667, median=16.667, and
  near60=505; VPQ total delta=1151 and dropped delta=284. The parser sees a
  first presented delta of 100 across six callback slots, beyond its 8x cap
  of 48; the observed 1.565s JS long task/coalescing means presented metadata
  could advance while callback delivery coalesced. Verdict remains NULL with
  no performance claim: N=2 is unsatisfied and further boots stopped. Both
  attempts were synchronously reaped with owned cleanup, and the final exact
  QEMU process count was zero. The scoped coalesced-rVFC parser correction now
  has independent adversarial NO-BOOT PASS: it replaces the arbitrary <=8x
  delivered-callback cap with `ceil(media_time_delta*75)` plus fixed edge
  slack, and reconciles VPQ total/drop progress per sample and across the full
  sampled window while retaining the 720p60, playback, provenance, and cadence
  gates. The exact retained fixture SHA-256 is
  `1c57fdea78c3ebafb0d0b5657dbaa6c4438e7ad9df9898f09be9213cc2f5a5f6`
  and it is byte-identical (`cmp=0`) to the retained retry evidence. Replay now
  passes `source_proven`, but this NO-BOOT fixture/review grants no FPS claim;
  equality accepts, +1 rejects, causal/order/dimension/VPQ/summary mutations
  reject, and 30fps remains diagnostic NULL. Review ended with an exact zero
  QEMU process count. The corrected sole-VM retry retained one valid T1 at
  `build-x86_64/chromium-youtube-m7/20260710T174100Z-a1-hd720-t1-retry2-mp1-audio1`:
  real KVM+virgl, source-proven `hd720`, stable 1280x720, and presented windows
  44.29/44.63 fps (mean 44.46). This is +15.56 fps/+53.8% versus 28.9, but
  still misses the first 52-fps target by 7.54 and the 52-53 local baseline by
  7.54-8.54. Source facts are 812 callbacks, presented delta 1123 over
  19.950s (~56.29/s), 16.667ms median, near60 ratio 0.6338; VPQ delta
  1148/drop 316 (27.53%), leaving 832 non-dropped frames (~43.45/s), with
  8 long tasks/664ms. Display present/completion deltas are parity in both
  windows (3603/3603 and 3559/3559); async posted/retired deltas are
  10812/10812 then 10679/10678 with pending snapshots 1/1/2; async make-room
  stall deltas are 6882/6780 with max wait 401524us. Player-crop deltas are
  178514/178514. Serial T2 at
  `build-x86_64/chromium-youtube-m7/20260710T174600Z-a1-hd720-t2-retry2-mp1-audio1`
  is INVALID/NULL on an `fbstat` timeout; its one retry at
  `build-x86_64/chromium-youtube-m7/20260710T174800Z-a1-hd720-t2r1-retry2-mp1-audio1`
  is INVALID/NULL on the media helper timeout/partial 10-of-30-row envelope.
  Therefore N=2 is unsatisfied and the 44.46 N=1 result is diagnostic only,
  not a performance claim. Exact QEMU count was zero between attempts and at
  final cleanup. The bounded residual is now VPQ/drop plus host-retire/present
  backpressure. Independent NO-BOOT invalid-helper forensics FAILS the current
  wait contract. In `...T174600Z...`, `fbstat` produced nearly the full 58 KiB
  payload but lacked its END/nonce after 35s, an intermittent whole-run NULL.
  In `...T174800Z...`, the helper emitted exactly 10 rows and then the producer
  went silent while roles and console stayed alive; helper rc was 0, but 105
  nominal one-second polls collapsed to about 8.04 host seconds. The wait is
  therefore deterministically unsound, and prior valid `polls=0` runs never
  validated the sleep path. Exact QEMU count was zero at the verdict. Further
  boots are prohibited until a NO-BOOT correction uses host-monotonic timing,
  one-shot partial capture, a 60--90s host wait, and final same-nonce capture
  while preserving the overall 140s bound, then passes offline adversarial
  review. The first host-wait correction is independently REJECTED NO-BOOT.
  Its one-shot capture, absence of guest sleep/grep options, and nominal cap
  pass, but the real 140s ceiling is unenforced: the accepted path can begin a
  90s wait after final-capture work and exceed 150s plus overhead. Reordered or
  duplicated short 10-row envelopes are misclassified as incomplete and earn
  another 75s, while conflicting same-nonce RC rows `0,9,9` accept the first
  zero. Exact QEMU count was zero at review; boot remains prohibited. Required
  correction is an absolute host-monotonic phase deadline covering send,
  parse, wait, and final capture with margin and a 60s wait; validate an exact
  ordered unique prefix before any incomplete classification; require exactly
  one same-nonce RC and fence; then repeat adversarial NO-BOOT review. The
  second host-wait NO-BOOT re-review closes the deadline and unique RC/fence
  blockers but still REJECTS: structurally ordered 10/30 prefixes with semantic
  corruptions (`player_state=2`, `invoked=0`, selected `medium`, or sample
  `640x360`) are classified INCOMPLETE and earn the 60s wait because prefix
  checking validates structure but not semantics. Exact QEMU count was zero;
  boot remains prohibited. Required correction is shared/full semantic
  validation of every present prefix row before any wait, exact mutations for
  those four cases, and another independent adversarial NO-BOOT review. The
  third host-wait NO-BOOT re-review closes those semantic-corruption blockers
  but still REJECTS: exact otherwise-valid terminal prefixes at force lengths
  28/29 and normal lengths 22/23 fail
  `prefix-terminal-row-requires-complete`, although each is a legitimate
  pre-done writer-race prefix. Exact QEMU count was zero; boot remains
  prohibited. Required narrow correction is shared semantic summary/rVFC
  validation, exact terminal prefixes classified INCOMPLETE while corrupted
  terminal prefixes still reject, an all-length prefix matrix, and another
  independent adversarial NO-BOOT re-review. Final independent NO-BOOT review
  now PASSES the host-monotonic correction after those three rejection rounds:
  one absolute 130s deadline preserves a 10s margin, the one allowed wait is
  60s and every command uses only its remaining budget, same-nonce RC and FENCE
  are exact and unique, and prefix/full paths share the semantic validators.
  The exhaustive matrix classifies force prefixes 0--29 INCOMPLETE and 30
  COMPLETE, normal prefixes 0--23 INCOMPLETE and 24 COMPLETE; legitimate
  terminal writer-race prefixes 28/29 and 22/23 remain recapturable, while
  corrupted summary or rVFC rows reject. Every arm/static/adversarial test and
  diff check passes, and the final exact QEMU count is zero. Next, checkpoint
  the verified code, then authorize one sole-VM worker for serial N>=2 A1
  measurement; no A2 VM worker may overlap it.
  Verbose A1 chronology is archived append-only under “A1 frame-supply/EGL/
  media-probe history displaced on 2026-07-10” in
  `docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`.
- A2 (IN FLIGHT): U-AUDIO localization. Retained evidence proves generic
  pipewire-pulse playback-stream creation: S16LE/48k stereo reached RUNNING,
  and a later pacat run exited 0 with nonempty QEMU WAV. Chromium's exact
  F32LE/48k stereo/512-frame path also opened, started, and was alive at ~5s,
  then failed at ~8s; `pa_operation is nullptr` rows occurred during Stop/Close
  and are secondary fallout, not the first failing operation. Leading bounded
  hypothesis is the VM `default.clock.min-quantum` and `pulse.min.quantum`
  1024 policy versus Chromium's 512-frame callback, but applied/effective
  attributes remain unproven. The July paplay scaffold is INVALID under current
  synchronous-wait/process-ownership rules and never produced the needed
  comparison. The first source-controlled localizer passed its own static suite
  but was independently REJECTED NO-BOOT; no VM was launched. Loader audit now
  proves the direct `LD_PRELOAD` `pa_*` tracer cannot observe Chrome 150's
  handle-specific Pulse stubs. The smallest supported replacement is a
  default-OFF, handle-aware `dlopen`/`dlsym` interposer: exact guest glibc 2.39
  `dlvsym` plus a `dlsym@GLIBC_2.2.5`/RTLD_LOCAL reducer captured provider
  lookup and wrapper substitution. Its contract requires the exact libpulse
  handle/path/SONAME/build ID; ABI-typed wrappers including begin-write/write/
  state/errno; stable same-PID context/stream IDs; provider return, errno, and
  success/failure output-pointer parity; fork/thread safety; bounded class caps
  that retain late failures; and opportunistic effective attrs (Chrome does not
  resolve `pa_stream_get_buffer_attr`; the dedicated reducer supplies them).
  Required NO-BOOT gates are exact-handle/off/missing-symbol/dlerror controls,
  full binding and provider-identity proof, multi-context/stream IDs,
  thread+fork-held-lock, poison-output/errno parity, >100k-call cap+late-failure,
  and parser acceptance without a Chromium effective-attr row. The corrected
  localizer now has independent adversarial NO-BOOT PASS across the full
  static/reducer/parser suite. Provider A remains 4 after provider B returns
  104, distinct handle-local wrappers are proven, and default-OFF output is
  byte-equal. Successful 2048- and 1024-byte begin-write results produce those
  exact frame counts and writes; 1025-byte and failure cases produce no write.
  The heap-owned late callback safely defers through mainloop stop, releases,
  then unrefs under ASan/UBSan. Binding 21 covers the context callback and
  optional binding 22 covers effective attrs. Causal parser ordering,
  primary-versus-teardown attribution, provider/source identity, thread/fork,
  and hot-call late-failure tests all pass; the unarmed localizer exits 2.
  Review ended with an exact zero QEMU process count. Next, checkpoint the
  verified code, then spend exactly one sole-VM real KVM+virgl localization
  boot (no concurrent A1 VM worker) with audio enabled, `QEMU_AUDIO=none`,
  explicit null sink, and pw-play ->
  paplay -> exact Chromium libpulse -> >=20s Chromium smoke. A4
  `QEMU_AUDIO=virtio` + WAV/audible work remains subsequent. Kernel
  virtio-snd/OSS/ALSA is closed and is not the gap.
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

Host-resource guardrail (2026-07-10): the WSL OOM was caused by overlapping
recursive `rg --text` scans of multi-GiB raw images; `head` bounded output only.
Root `AGENTS.md` and the authoritative debugging skill now require guarded,
globally serialized source searches with one thread, 64 MiB/file, <=2 GiB
address space, no-follow, finite timeout, generated/image exclusions, and a
separate explicit-file artifact path. Dangerous unrestricted/text/binary flags
and `/usr/bin/rg` bypasses are forbidden; returned process handles must be
synchronously reaped before another heavy job. No performance/VM work resumed.
VM orchestration guard: the conductor authorizes at most one VM worker and all
other lanes remain no-boot. Dispatch requires no active VM worker plus an exact
zero `/proc/*/exe` count for `qemu-system-*`/`qemu-kvm` (never `pgrep`); another
VM worker waits for synchronous completion, owned cleanup, and a repeated zero
count. Enforcement is orchestration-only: do not add QEMU/launcher singleton
changes. Only one VM worker may be authorized at a time, while guest
`QEMU_MEMORY`/`-m` may grow when justified.

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
