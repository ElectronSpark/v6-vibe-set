# Active xv6 Work Plan

Last updated: 2026-07-11. Sole live plan; history is in git/the archive, with runtime proofs under `build-x86_64/`.

Before checkpointing or dispatch, verify live HEAD against its same-name origin. This plan records the first authorized boot after fullscreen-plan checkpoint `fdb628e`. Preserve unrelated KDE harness work.

## Objective and operating state

Close YouTube 720p60 to same-host Linux-VM parity in two acceptance modes:
(A) windowed watch page and (B) true player fullscreen. Each must first reach
>=52 presented fps, then about 55-60 with low drops/stable pacing; windowed
cannot close fullscreen or the overall goal. Use real KVM+virgl yt-presentfps
and PERF-VIDEO only; software is invalid. Each mode needs N>=2 comparable
trials, never pooled; N=1 is diagnostic.

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

Newest runtime artifact and stop verdict:

- `build-x86_64/chromium-youtube-m7/20260710T-windowed-watchpage-n2-fdb-t1/`
  is N=0 INVALID/NULL: the sole authorized boot used real KVM+virgl and passed
  the exact treatment arm plus 3/3 staged media assets, but panicked during
  probe 1 before media, GPU-role, yt-presentfps, or PERF-VIDEO evidence. It
  proves no FPS and makes no fullscreen claim; attempt 2 was correctly stopped.
- Independent NO-BOOT forensics PASS is durable at live HEAD/origin `a2a5f6a`.
  Linux permits arbitrary `.poll` to wake synchronously; xv6 invoked it under
  the kqueue lock in rescan, stale-ready, `EV_CLEAR`, and nested paths; the
  syncobj signal callback then notified a knote and reentered that same lock.
- First rework self-PASS (NO-BOOT) retains scan-local high-water ordering,
  post-drain `KN_DELIVERING`/`KN_PENDING` coalescing, unlocked `.poll`
  dispatch, and pin/identity/generation/ABA/deferred-reclaim checks.
- The second rework self-PASS established `-ENOSPC` registration exhaustion,
  unlocked dispatch, ref-pinned cycle admission, and post-drain coalescing;
  the final independent adversarial NO-BOOT review then **REJECTED** its
  insufficient guard/admission/callback/fairness proof.
- Third independent adversarial NO-BOOT re-review **REJECTED** the focused
  self-PASS: a sole cached-alias, parenthesized member dereference bypassed the
  repository guard and was reproduced in `/tmp`. Production graph/lifetime
  tests show no new bounded failure, but their positives cannot compensate for
  a bypassable prevention policy.
- Fourth independent adversarial NO-BOOT review **REJECTED** the guard: raw
  `consume(f->ops->poll)` function-pointer retrieval bypassed it in a separate
  `/tmp` copy. Its root/kernel-only scope and named diagnostic exceptions are
  non-exhaustive; direct/mutation positives, the 1,000 `-O2` and 1,000
  ASan/UBSan cases, and the normal build are therefore insufficient. TSan is
  unavailable and is not a passing result.
- Fifth independent adversarial NO-BOOT review **REJECTED** the lexical
  raw-member guard: macro-expanded file/cdev member access (`REVIEW_FILE_OPS`
  and its cdev analogue), raw `review_raw_payload.inc` included by copied
  `kqueue.c`, and a raw symlinked `.c` payload compile locked callbacks despite
  `lstat` skipping links. Direct/mutation, `-O2`, 1,000-case ASan/UBSan,
  normal build, and Sparse remain insufficient; TSan is unavailable/non-evidence.
- Sixth compiler-expanded guard self-PASS used a 293-unit compile DB and
  preprocessed `#line` provenance. Independent adversarial NO-BOOT review
  **REJECTED** it: an untracked regular kernel `.c` could token-paste a `.poll`
  escape, so its configured-universe/missing-source claim was false.
- Seventh configured-universe self-PASS is **REJECTED** by its final independent
  adversarial NO-BOOT review. A `/tmp` POC made the compile DB's file/command
  provenance disagree; it also lacked a manifest/index/status race proof.
  Existing admission POCs, configured build, `-O2`, 1,000-case ASan/UBSan, and
  Sparse positives are insufficient; TSan remains unavailable/non-evidence.
- Eighth DB-provenance/snapshot NO-BOOT self-PASS bound canonical DB file,
  kernel root, compiler, and exact `-c` source identity. Its final independent
  adversarial review **REJECTED** it: configured-but-untracked DB C was accepted,
  and a post-snapshot header touch passed because provenance omitted actual
  compiler `#line` dependencies. Positives are insufficient; **BOOT REMAINS PROHIBITED**.
- Ninth dependency-snapshot self-PASS is **REJECTED** by final independent
  NO-BOOT review: an external untracked header's user-controlled line control
  made external raw `.poll` look like a dispatcher whitelist. Standard/numeric
  `#`, `%:`, enabled trigraph, and splice/comment forms reproduce it; include
  marker flags are forgeable. Positive dependency/snapshot checks, stress,
  configured build, and Sparse are insufficient.
- The separate raw-function-pointer report was pre-tenth/stale, not a current
  gate regression. Fresh disposable-mirror reproduction at current
  `95f241a`/`ed80857`, using the current reducer and compile DB, rejects exact
  `kqueue_guard_review_consume_poll(f->ops->poll);` with `rc=1`; no boot ran.
- Tenth guard now has an independent adversarial NO-BOOT **PASS**. It
  normalizes the relevant C translation phases before provenance parsing,
  authenticates the physical compiler-marker/include stack, and snapshots
  every external regular header plus post-snapshot TOCTOU state. It rejects
  user line controls, untrusted external-to-in-tree transitions, external raw
  `.poll`, and raw function-pointer retrieval/escape; the bounded
  null-condition repair permits only the direct condition and rejects the
  following raw retrieval.
- The independent boundary suite passed: normal `--compiler-only`; benign
  external/nested tracked-mirror includes; the 12-case null/next-statement
  matrix; and rejection (`rc=1`) of external/nested raw-consume, `#line`,
  numeric flags 1/3/4, `%:line`/`%:numeric`, `??=line`,
  splice/trigraph-splice/comment-separated controls, and external,
  compile-DB, or in-tree-header post-snapshot touches. This closes the prior
  spoofed-`#line` provenance and TOCTOU findings.
- Evidence: kernel build PASS; Sparse 209 files with zero failures/errors
  (known context warnings including `kqueue.c:1273`); full `-O2` reducer 100
  PASS; ASan/UBSan model 100 PASS; diff checks PASS; temporary POCs cleaned;
  exact `/proc/*/exe` QEMU count was zero before and after checks. The full
  ASan compiler-expanded guard run exited on a host limitation before it
  reported, so it receives **no credit**.
- This PASS lifted the kqueue *review* gate only. Verified checkpoints are
  pushed deepest-first at kernel `ed808576` then top-level `95f241af`; they do
  not themselves authorize a boot. The conductor must separately establish a
  fresh live same-name-origin match, no active VM worker, and fresh exact-zero
  QEMU state before authorizing one VM.
- Preserve scan-local high-water, unlocked/pinned dispatch,
  identity/generation/ABA/deferred reclaim, `-ENOSPC`/cycle admission,
  stale-ready rejection, callback coalescing, and `EV_CLEAR`/oneshot fairness.
  The review gate is lifted, but no VM is currently authorized; QEMU is zero,
  and windowed N>=2, fullscreen, and A2 remain open.

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

1. Latest live authorization gate: **REJECTED only for material uncommitted
   `docs/active-work-plan.md` dirt**. The kernel worktree, no-active-VM,
   exact-zero-QEMU, KVM/virgl prerequisites, and prior checkpoint/origin
   checks otherwise passed. Commit/push this plan-only checkpoint, then take a
   fresh same-name-origin/no-VM/exact-zero-QEMU gate before any boot.
2. Only after that fresh gate may the conductor authorize one sole-VM A1 serial
   windowed N>=2 forced-hd720 measurement. Invalids are NULL.
3. Add/review deterministic fullscreen evidence; then sole-VM fullscreen N>=2.
4. Sole-VM A2 Pulse localization and reviewed stream fix, still held behind A1.
5. Validate audio-on windowed and fullscreen parity; close each mode's
   fullscreen-scaling/present-retire residual before the overall goal.
6. Run kickoff+tooltip promotion battery, then the both-mode video-pair battery
   as lanes free.

Closed: kernel audio base, codec-capacity theory, loader/config M4 levers,
single-lib stubs, syscall/VFS-cost-as-M4, unlocked-wait, present-clock alone,
and PCID/noflush absent prerequisites. Reopen only on new focused evidence.
