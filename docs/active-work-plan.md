# Active xv6 Work Plan

Last updated: 2026-07-10. Sole live plan; history is in git and the archived
full-history plan, with runtime proofs under `build-x86_64/`.

Branch `codex/host-linux-abi-shell-port-ff`: HEAD and same-name origin are
`da4a4265d2c4b7916c91dc455078562c0aaf6438`, including the passed wide-integer correction. Preserve unrelated KDE harness work.

## Objective and operating state

Close YouTube 720p60 to same-host Linux-VM parity in two separate acceptance
modes: (A) normal watch-page/windowed and (B) true YouTube/player fullscreen.
Each must first reach >=52 presented fps and finally about 55-60 with low drops
and stable pacing; windowed cannot close fullscreen or the overall goal. Use
real KVM+virgl yt-presentfps and PERF-VIDEO only; software is invalid. Each mode
needs N>=2 valid comparable trials, never pooled; N=1 is diagnostic.

Every concrete job is delegated to an opus worker. No VM worker is authorized;
an exact `/proc/*/exe` scan finds zero QEMU processes.

## Host and VM safety (binding)

- The WSL crashes were host OOM, not QEMU: overlapping recursive `rg --text`
  scans of raw images reached about 24 GiB and 6.7 GiB RSS. `head` bounded
  output, not scan allocation.
- Root `AGENTS.md` requires guarded `/home/es/.local/bin/rg`: serialized
  source-only searches, one thread, 64 MiB/file, 2 GiB address-space bound,
  finite timeout, no symlink following or unrestricted/text/binary recursion;
  artifact files use `safe-rg-artifact.sh`. Never bypass with `/usr/bin/rg`.
- Synchronously wait on every returned process/session handle. Builds, VM runs,
  and console commands use synchronous waits only; no monitors or abandoned
  polling loops.
- Only the conductor authorizes at most one VM worker; all other lanes are
  NO-BOOT. Dispatch requires no active VM worker and exact zero `/proc/*/exe`;
  never use `pgrep` for this.
- Every VM run owns its PID/process group, is synchronously reaped, performs
  bounded cleanup, and ends with the same exact zero-process check. Serial
  silence is not completion: wait on the command, check state, and reacquire a
  fresh prompt. Use short commands/marker variables and account for the known
  first-character drop after bracketed paste.
- Singleton protection is orchestration-only. Do not edit QEMU or launcher
  scripts to enforce it. There is no guest-RAM cap; raise `QEMU_MEMORY`/`-m`
  when justified while still running only one VM.

## A1 — frame supply and YouTube presentation

Status: IN FLIGHT; frame selection is fixed, but N=2 and the 52-fps gate are
not satisfied.

- Codec capacity is not the constraint. Controlled local 1280x720@60 H.264
  and VP9 sustain about 52-54 presented fps.
- The original multiprocess-only A/B was 28.295 control versus 28.960 fps
  treatment: null for the 52-fps target. Media evidence showed YouTube chose
  640x360 `medium` at 33.333/33.334 ms while advertising `hd720`.
- Default-OFF `YT_FORCE_HD720=1` has adversarial NO-BOOT PASS. Its exact arm is
  multiprocess=1, audio-disable=1, media-probe=1, EGL-forensics=0; MAIN-world
  code requires `hd720`, calls range before quality, and leaves classification
  to the host parser.
- Source cadence is now proven under multiprocess+forced hd720. The only valid
  run is `chromium-youtube-m7/20260710T174100Z-a1-hd720-t1-retry2-mp1-audio1`:
  44.29/44.63 fps, mean 44.46. That is +15.56 fps/+53.8% over 28.9, but 7.54
  fps below 52 and about 7.5-8.5 below the local 52-53 baseline. It is N=1,
  so it is diagnostic rather than a performance claim.
- It recorded 812 callbacks, presented 1123/19.950 s (~56.29/s), 16.667 ms
  median, near60 0.6338; VPQ 1148/drop 316 (27.53%), leaving 832 non-dropped
  frames (~43.45/s). Presents/completions were parity; async make-room stalls
  were 6882/6780, max 401524 us. Residual: VPQ/drop plus host-retire pressure.

Verified harness closures, in order:

1. Option-free media helper and forced-hd720 selector, with source/provenance,
   stable 1280x720, rVFC/counter, cadence, and adversarial mutation gates.
2. Coalesced-rVFC accounting reconciles media time and VPQ totals instead of
   rejecting valid browser long-task callback coalescing.
3. Host-monotonic one-shot capture uses an absolute 130 s deadline, one 60 s
   recapture, unique same-nonce RC/fence, and shared semantic prefix checks.
   Full prefix matrices and corruption mutations pass/reject as intended.
4. Commit `ec4a733` accepts only LF, CRLF, or CRCRLF console fences and rejects
   malformed, duplicate, missing, reordered, spoofed, or wrong-nonce frames.
5. Commit `da4a426` closes bounded signed Tcl `wideinteger` validation.
   NO-BOOT review passed epoch-scale exact-130000 ms arithmetic,
   115000/114999 edges, live phase anchoring, min/max, overflow fail-closed
   cases, all arms, and JS/static/diff checks. Placement, budgets, framing,
   and FPS logic are unchanged.

Newest runtime artifact:

- `chromium-youtube-m7/20260710T211500Z-a1-hd720-fencefix-t1-mp1-audio1`
  is INVALID/NULL. Real KVM+virgl, 3/3 staged media assets, GPU role, and
  rendered Chromium passed, then the correctly placed initial media deadline
  gate returned `remaining_ms=-1 required115000`. Capture was never sent, so
  there is no source, FPS, or PERF-VIDEO evidence and no T2 was spent.
- Root cause is the Tcl 32-bit type gate, not fence placement or capture. The
  artifact did not log a phase-start value because the gate precedes that log;
  epoch values used in the reducer are synthetic and must not be attributed to
  the artifact.

Next: after a fresh zero-QEMU/no-active-worker gate, one sole VM worker runs
serial real-KVM+virgl windowed trials for at least two valid samples. One retry
may replace a harness-invalid run; invalids remain NULL. Hold A2 VM work until
the A1 windowed N>=2 gate and exact QEMU zero.

## Fullscreen acceptance mode

Status: OPEN; no deterministic fullscreen performance baseline exists.

- FPS credit needs raw nonce-bound actual player/document fullscreen proof,
  not merely a maximized window: stable transition/settle, active playback,
  selected `hd720`, stable source `videoWidth/videoHeight=1280x720`; only the
  host parser classifies.
- Record output mode, viewport, scale, and composition state so scaling cost is
  not mislabeled as frame supply. Reject drift, ambiguity, wrong dimensions,
  paused playback, or spoofed/out-of-order facts.
- Run N>=2 valid fullscreen trials comparable to windowed and matching
  Linux-VM/local fullscreen baselines. Never pool modes; >=52 interim and final
  about 55-60/low-drop/stable-pacing gates apply here.
- The current 44.29/44.63 (mean 44.46) artifact is watch-page/non-fullscreen
  diagnostic N=1 unless its raw artifact proves otherwise; it is not a
  fullscreen baseline or claim.

## A2/A4 — real Chromium audio

Status: localizer ready; runtime localization and stream fix remain open.

- Retained `pacat` evidence proves the pipewire-pulse server/userspace base can
  create and run S16LE/48 kHz stereo streams and produce a nonempty QEMU WAV.
- Chromium's F32LE/48 kHz stereo/512-frame stream opens and starts, stays alive
  about 5 s, then its first primary playback failure appears near 8 s. Later
  `pa_operation is nullptr` rows belong to Stop/Close teardown and are
  secondary, not the root failure.
- Commit `051dfa1` is the default-OFF handle-local Pulse localizer/reducer.
  NO-BOOT review passes identity, provider slots (A remains 4 after B returns
  104), short begin-write/frame counts, causal primary/teardown attribution,
  stable IDs, concurrency/lifetime, >100k late-failure, mutations, and OFF parity.
- Leading hypothesis remains the guest 1024-frame PipeWire/Pulse minimum
  quantum versus Chromium's 512-frame callback, but effective negotiation is
  unproven and must not be promoted to a verdict.

After A1 windowed N=2 and zero QEMU, authorize one A2 boot: real KVM+virgl,
audio on, `QEMU_AUDIO=none`, null sink, then pw-play -> paplay -> Chromium
libpulse -> >=20 s Chromium smoke. Fix the proven userspace path so YouTube
runs without the audio-disable flag; then validate `QEMU_AUDIO=virtio`/WAV.
Final A2 validation and remeasurement must pass windowed and true fullscreen
with real audio and no disable workaround. Kernel virtio-snd/OSS/ALSA is closed.

## Residual present wall

After forced-hd720 N>=2 per mode and real audio, measure YouTube against
matching windowed and fullscreen Linux-VM/local baselines. Separate
fullscreen-specific scaling/composition, present-retire, VPQ/drop, and pacing
overhead from the windowed frame-supply path. If either mode remains below
55-60, pursue only the recorded VPQ/drop, async make-room, host-retire, and
present-throughput residual. Do not reopen codec selection, DNS, launcher
packaging, or the kwin 5.27 schedule wall without new evidence.

## Secondary default-promotion queue

No default flips without same-session A/B, explicit-off, KDE active-sample,
Chromium launch-only, R5-watch knobs, and M4/M5/M8 within noise.

1. Kickoff prewarm plus tooltip delay/prewarm: proven opt-in results are menu
   2164 -> 493 ms and tooltip median 945 -> 486 ms.
2. Video pair, with windowed and true-fullscreen regression coverage:
   `virtio_gpu_async_present=1` plus
   `virtio_gpu_present_clock_60hz=1`; prior local video result was 52.3 ->
   53.4 fps with lower drops.

Keep `anon_free_poison`, `anon_fault_verify`, and
`vm_shootdown_cpumask_audit` armed in batteries. The kwin 5.27 damage/repaint
schedule floor is framework-level, not a kernel lane. PCID stays OFF pending
user-VM cpumask completeness.

## Binding validation and checkpoint rules

- Adversarial NO-BOOT review precedes every kernel/image boot.
- Real KVM+virgl GL is mandatory; verify renderer/GPU-role evidence and reject
  llvmpipe or any software fallback.
- Use yt-presentfps plus PERF-VIDEO metrics; record PASS, FAIL, INVALID, NULL,
  and negative results honestly. N>=2 is mandatory for timing conclusions.
- No ungated defaults. Keep closed lanes closed and advance to the next queue
  item when a lane closes.
- Respect the standard crash/artifact checks, maximum two boots per slice, and
  exact owned-QEMU cleanup after each run.
- Commit verified checkpoints deepest-first and push the current branch
  lineage; pushing this lineage is pre-approved. Preserve unrelated dirt.

## Immediate queue

1. Checkpoint this plan on the pre-approved branch lineage.
2. Sole-VM A1 serial windowed N>=2 forced-hd720 measurement; invalids are NULL.
3. Add/review deterministic fullscreen evidence; then sole-VM fullscreen N>=2.
4. Sole-VM A2 Pulse localization and reviewed stream fix.
5. Validate audio-on windowed and fullscreen parity; close each mode's
   fullscreen-scaling/present-retire residual before the overall goal.
6. Run kickoff+tooltip promotion battery, then the both-mode video-pair battery
   as lanes free.

Closed: kernel audio base, codec-capacity theory, loader/config M4 levers,
single-lib stubs, syscall/VFS-cost-as-M4, unlocked-wait, present-clock alone,
and PCID/noflush absent prerequisites. Reopen only on new focused evidence.
