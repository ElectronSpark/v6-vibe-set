# Active xv6 Work Plan

Last updated: 2026-07-11. Sole live plan; history is in git/the archive, with runtime proofs under `build-x86_64/`.

Before dispatch, verify live HEAD against its same-name origin. The current
gate `e0f7c90` was consumed by an INVALID/NULL A1 t1; preserve unrelated KDE
harness work.

## Objective and operating state

Close YouTube 720p60 to same-host Linux-VM parity in two acceptance modes:
(A) windowed watch page and (B) true player fullscreen. Each must first reach
>=52 presented fps, then about 55-60 with low drops/stable pacing; windowed
cannot close fullscreen or the overall goal. Use real KVM+virgl yt-presentfps
and PERF-VIDEO only; software is invalid. Each mode needs N>=2 comparable
trials, never pooled; N=1 is diagnostic.

Every concrete job is delegated to an opus worker. The misbound post-checkpoint
`@{upstream}` gate (`80783a`) remains INVALID/NULL and grants no authority. The
explicit same-name `21ff39f` gate was consumed by one A1 *windowed* boot; its
framing INVALID/NULL grants no FPS credit or retry authority. Fresh gate
`e0f7c90` PASSED its live checks and authorized exactly one A1 *windowed*
runner, but that authority is consumed by the latest INVALID/NULL t1. A fresh
same-name-origin, no-active-VM, exact-zero-QEMU gate is required after the
focused NO-BOOT capture-completeness repair/review; it must still reject
llvmpipe/software and never authorizes fullscreen or A2.

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
- `chromium-youtube-m7/20260711T082215Z-a1-windowed-n2-t1-mp1-audio1-media1-hd7201`
  is likewise N=0 **INVALID/NULL**: its sole real KVM+virgl boot passed the
  GPU-role/treatment and 3/3 staged-asset gates, but `result=8
  media-probe-capture-invalid` because capture began on the host
  `YT_MEDIA_PROBE_HOST_CAPTURE attempt` line plus bracketed-paste echo before
  a valid `BEGIN`. Later capture facts prove hd720/1280x720/cadence only; they
  grant no FPS credit. Attempt 2 was correctly stopped, QEMU ended at zero,
  and its scratch image is retained. Repair and adversarially review framing
  NO-BOOT before a fresh gate/retry.
- `chromium-youtube-m7/20260711T095541Z-a1-windowed-n2-framingfixed-t1...`
  is N=0 **INVALID/NULL**: the one real KVM+virgl boot passed GPU-role and
  staged-asset gates, and later showed active hd720 1280x720 source, but exited
  code 8 `capture-begin-transition-invalid` before yt-presentfps/PERF-VIDEO.
  Raw host `YT_MEDIA_PROBE_HOST_CAPTURE attempt` text plus
  `ESC[?2004h` root-prompt `CRCRLF` and `ESC[?2004l` before real `BEGIN`
  triggered the failure. It proves no FPS; t2 was stopped, QEMU is zero, and
  scratch is retained. A NO-BOOT forensic repair/review must distinguish that
  arbitrary outside-transaction preamble from the valid route without reopening
  any spoof/control rejection, then a new live gate is required.
- Capture-framing repair is **REJECTED** by independent NO-BOOT review. The
  self-PASS accepts an exact `ESC[?2004l`+CR immediately before real `END` and
  `BEGIN` followed by `CRCRCRLF`; moving a fake transition/`BEGIN`, or injecting
  a transition/noise payload, also still reaches semantic PASS. That is an
  unbounded ignored-content grammar, not a safe normalization. Repair must
  normalize only the one exact transition immediately before real `BEGIN`, use
  exact permitted line endings, and reject every injected/moved/noise token;
  retain the valid host-preamble/shell-echo route and all earlier nonce/fence,
  spoof, wrong-mode, echoed-`BEGIN`, and missing-fence rejections. QEMU was
  zero; no VM is authorized.
- The boundary repair now has a NO-BOOT **self-PASS**, not boot authority. Its
  byte grammar accepts only exact `ESC[?2004l`+CR+`BEGIN`+CRLF, with raw
  payload (no trimming); it rejects CRCRLF at `BEGIN`/`END`/payload,
  transition at `END`/payload/noise, and moved, fake, duplicate, spoofed,
  wrong-nonce/mode, or missing-fence frames. The retained 20260711 debugcon
  capture and semantic RC/fence proof pass; full static checks and the prior
  fence matrix pass; QEMU is zero. Independent adversarial review remains
  required before a new live gate or VM.
- Independent NO-BOOT boundary review now **REJECTS** that self-PASS. A
  standalone CSI+CRLF after `END`, after `RC`, or before/after `FENCE` still
  reaches capture+semantic COMPLETE+frame across bracketed-paste
  enable/disable, noncanonical/truncated, SGR, cursor, and randomized forms;
  only literal disable was barred. This is a control-byte hole. A replacement
  must enforce a strict byte/control policy from `BEGIN` through the complete
  `RC`/`FENCE`: no ignored CSI/control content there, while retaining the valid
  20260711 debugcon preamble/shell-echo route, valid LF/CRLF/CRCRLF
  `RC`/`FENCE` lines, and the earlier positive/rejection matrix. QEMU is zero;
  no gate or VM is authorized.
- The strict-region boundary replacement has a NO-BOOT **self-PASS** only:
  from authenticated `BEGIN` through unique `RC`/`FENCE`, it permits only
  CR/LF controls. All 28 CSI/C0/DEL/C1 forms at each of six placements cause
  capture rejection, semantic non-COMPLETE, and no valid frame; LF, CRLF, and
  CRCRLF positives pass. The retained SHA-bound debugcon capture is sliced at
  the unique fence, leaving its post-frame prompt outside capture. Static/diff
  checks pass and QEMU is zero; independent adversarial review is next and no
  gate or VM is authorized.
- Independent strict-region NO-BOOT review **REJECTS** that self-PASS. All 108
  control mutations reject, but printable `NOISE`, foreign, or long rows after
  `END`, before/after `RC`, or before `FENCE` still triple-pass capture,
  semantic COMPLETE, and frame validation. Repair needs an exact lexical tail
  grammar after `END`: only its permitted separator, then exactly matching
  `RC`/`FENCE` with valid terminators—no printable, extra, or trailing data.
  Retain the unique retained-slice/tail positive and all 108 rejections. QEMU
  is zero; no gate or VM is authorized.
- The lexical-tail repair has a NO-BOOT **self-PASS** only. Its marker-bound
  grammar is exact `END`+CRLF, blank, then unique `RC:0`/`FENCE` tokens with
  valid terminators; the control scan ends at `FENCE`. The full SHA-bound
  debugcon capture, including its prompt, passes. All three consumers reject
  printable, foreign, long, blank, extra, marker-mismatched, or duplicate-
  `FENCE` tails, while valid tail/prompt positives pass. Static/parser/diff
  checks and exact QEMU zero pass; independent adversarial review is required
  before any gate or boot.
- Independent lexical-tail NO-BOOT review now **PASSES**. The complete
  SHA-bound retained debugcon triple-passes capture, semantics, and frame
  validation with the prompt outside the fence; static checks pass. All 62
  adversarial mutations across framing, tail, marker, control, and their
  compositions yield zero invalid triple-passes, while valid LF, CRLF, and
  CRCRLF tail positives pass. This lifts the framing *review* gate only:
  checkpoint/push the repair, then rerun the explicit same-name live A1 gate;
  neither result itself authorizes a VM.
- New PTY transcript forensics **REJECTS** the current hardcoded-CRLF route.
  Both real live serial samples begin with `H` at offset zero, have opaque host
  preamble, then bracketed-paste disable + `CR BEGIN CRCRLF`, and use CRCRLF
  uniformly for payload, `END`, separator, `RC`, and `FENCE`; debugcon is a
  separate CRLF channel. The implementation therefore rejects the real PTY
  witness. No VM/gate is authorized.
- Replacement review contract is mode-bound: authenticate the exact host
  `H N/M command/phase` envelope; accept opaque preamble, one enable before
  and one disable adjacent to `BEGIN`, globally unique tags/tail, and one
  uniform `E in {CRLF, CRCRLF}` across the owned transaction. Validate raw
  bytes first, then canonicalize only owned separators; keep `BEGIN` through
  `FENCE` strict. Require rich positive/mutation POCs (mixed endings,
  moved/duplicate controls, forged envelope/tag/tail, injected payload/tail,
  cross-channel replay, and malformed/truncated bytes). This is integrity
  framing, not cryptographic protection against a root guest.
- Independent adversarial NO-BOOT PTY-mode review **PASSES**. Parser static
  checks and Expect replay proof pass: byte-0 V1 binds attempt/N/M,
  command/phase; exactly one enable/adjacent-disable and globally unique tail
  select one uniform `E in {CRLF, CRCRLF}` before canonicalization. The
  SHA-256-bound CRLF debugcon witness
  `6ab0c6493c33c3df914f93a36b9e8212da681c7ad82862ba8cd1c6845ce2b8da` and
  CRCRLF PTY1 `2a7fda2c0ec0b79b30929b010eaf343cceac08713aec37b355f1c6214f4c0d43`
  reach capture/semantic/frame COMPLETE; CRCRLF PTY2
  `60e470f054d2ffccee64f09bd63eb8f23256d5dbd0328ba12c1e796ee2b530c5`
  remains honestly semantic INCOMPLETE (no credit). Mixed/moved/duplicate
  controls, forged envelope/tag/tail, payload/tail injection, cross-channel,
  malformed, and truncated mutations reject. The matching `FENCE` ends the
  owned slice; the later bracketed-paste root prompt is outside it. This lifts
  the framing *review* gate only: checkpoint/push parser+harness+plan, then
  establish a fresh explicit same-name live A1 gate; no VM is authorized now.
- Latest `chromium-youtube-m7/20260711T-a1-windowed-n2-ptyfixed-t1` is N=0
  **INVALID/NULL**. Its sole real KVM+virgl boot passed GPU-role, arm, and
  staged-asset gates and selected active hd720 1280x720 (`readyState=4`,
  `paused=0`), but the one-shot timed out at 60003 ms with only 10 of 30
  required rows (`result=8 final-row-count-10-expected-30`). It grants no
  fps/PERF-VIDEO claim, no t2 or fullscreen authority. QEMU ended at zero;
  scratch `/tmp/xv6-yt-20260711T104523Z-pid1642410-mp1-audio1-media1-hd7201.fs.img`
  is retained. Next is focused NO-BOOT capture-completeness forensics/review,
  then a fresh live gate.
- Capture-completeness NO-BOOT forensics finds the same valid artifact source
  stops its producer after sample 3 (10 rows): initial and final `RC:0`
  payloads agree. The host wait/deadline, 30-row cap, and authenticated framing
  neither stopped nor truncated it. This rules out those host mechanisms but
  does not distinguish guest timer, renderer-progress, or rVFC-callback
  lifetime; an arbitrary delay is prohibited. Next is a default-OFF,
  nonce-bound diagnostic of timer arm/fire, rVFC liveness, and before/after
  `fbstat`, with capture semantics unchanged. Its NO-BOOT POCs must prove OFF
  parity and reject missing, forged, replayed, wrong-nonce, duplicate, or
  reordered diagnostic facts before independent adversarial review. No VM is
  authorized; QEMU is zero and this is not a checkpoint.
- The default-OFF completeness diagnostic has a NO-BOOT **self-PASS** only.
  It arms only with forced-hd720 media, exits before FPS, and uses a separate
  nonce-bound strict 16-KiB/64-fact channel for timer arm/fire, rVFC, and
  before/after `fbstat`; the semantic 30-row contract is unchanged. Static
  ON/OFF/media-off checks and diagnostic POCs pass. This is **non-credit**:
  no JS runtime executed the reducer's console-throw/block or OFF-parity
  paths. Independently adversarial NO-BOOT review is next; no gate or VM is
  authorized.
- Independent NO-BOOT diagnostic review **REJECTS** the self-PASS: diagnostic
  `finish 0` enters the normal-success extractor, which requires
  `pre`/`steady0`/`windowA`/`windowB`/`mediaProbe` and exits `8` for missing
  frames. It grants no false FPS credit, but is unusable as a diagnostic. Add
  a dedicated diagnostic-only finish/extraction path that validates only the
  intended diagnostic artifacts and explicitly proves no FPS/semantic success;
  retain parser positives/rejections and the JS-runtime non-credit. QEMU is
  zero; this is NO-BOOT and no live gate or VM is authorized.
- The diagnostic-only finish replacement has a NO-BOOT **self-PASS** only.
  A diagnostic-first selector consumes only nonce-bound diagnostic serial/log
  facts plus before/after `fbstat`, never normal frames or the normal extractor,
  and emits `no_fps_claim`/`no_semantic_success`. Terminal `COMPLETE`, frozen
  `INCOMPLETE`, and invalid/reordered/missing fact POCs behave as specified;
  static `diag0` OFF parity passes. JS console-throw/block and runtime OFF
  parity remain non-credit because they have not run in a guest. Independent
  adversarial NO-BOOT review is next; no gate or VM is authorized and QEMU is
  zero.
- Independent NO-BOOT diagnostic-finish review **REJECTS** that self-PASS.
  The selector is safe, but live `diag1` still samples normal idle/render,
  `ytmediaprobe`, and semantic disposition before its collector; initial
  failures bypass diagnostics, `require_guest` finishes early, and the
  extractor can replace raw status. Replace it with a wholly separate `diag1`
  branch before normal gates/helpers/frames: terminal nonce-bound diagnostic
  INVALID artifacts must not call `require_guest`, while non-diagnostic raw
  result codes remain unchanged. POCs must poison normal-path reachability in
  `diag1` and diagnostic reachability in `diag0`; preserve parser positives and
  rejections. JS console-throw/block and runtime OFF parity remain non-credit.
  QEMU is zero; this is NO-BOOT, no live gate or VM is authorized.
- The separate-route replacement has a NO-BOOT **self-PASS** only. `diag1`
  stages only diagnostic launch/capture helpers and branches after KDE-ready
  before normal idle/render/`ytmediaprobe`/semantic/frame paths; its own raw
  command and diagnostic-fbstat collectors terminalize launch, framing, or
  before/after-context failure as nonce-bound `DIAGNOSTIC_INVALID`, preserving
  `raw_status` and never calling normal guest/result extraction. Static
  poisoned-route cases cover COMPLETE, frozen INCOMPLETE, and launch/before/
  after INVALID with normal helpers poisoned in `diag1`, plus a poisoned
  diagnostic runner in explicit-off `diag0`; both static arms pass. Parser
  nonce/replay/cap cases remain retained. `node`, `nodejs`, `qjs`, `js`, and
  `d8` are absent, so JS guest-runtime console-throw/block and OFF-parity stay
  explicitly non-credit. `git diff --check` passes and exact QEMU is zero.
  Independent adversarial NO-BOOT review is required; no gate or VM is
  authorized.
- Independent adversarial NO-BOOT separate-route review **REJECTS** that
  self-PASS. Both `diag1` and explicit-off static suites pass, and the branch
  is runtime-early, but the POC calls the runner with synthetic returned
  results rather than its `finish` extractor: `guest_cmd` timeout/EOF/panic/
  deadline paths call generic `finish` before `terminalize`, so launch/context
  failures have no nonce-bound raw artifact. Even returned launch failure has
  zero fbstat rows, and returned after-fbstat failure has valid `before` plus
  invalid `after`; the extractor accepts only all-valid or all-invalid rows,
  then replaces the diagnostic result with artifact-validation failure. The
  source POC also does not compare the diag branch position with every normal
  path. Repair must give diag1 an owned nonterminal command outcome API,
  terminalize all launch/framing/before/after outcomes with retained raw
  status, accept the legitimate absent/mixed invalid-context shapes without
  replacement, and add end-to-end poisoned `finish` plus source-order POCs
  (including diag0). Parser nonce/replay/cap positives remain retained; JS
  runtime remains non-credit. QEMU=0; NO-BOOT, no gate or VM is authorized.
- The diagnostic terminal-route repair has a NO-BOOT **self-PASS** only.
  Diag1 now owns timeout/EOF/panic/deadline serial outcomes before generic
  `finish`, terminalizes them with nonce-bound raw status, and validates only
  the stage-true absent-launch, invalid-before, or valid-before/invalid-after
  fbstat shapes. Post-stop validation may fail a complete result but appends
  its detail without replacing diagnostic disposition/raw status. Static diag1
  and explicit-off diag0 pass; poisoned normal helpers, diag0 runner, raw
  finish preservation, nonce/replay/cap, and an injected pre-branch normal
  path all reject as intended. JS runtime remains non-credit; `git diff
  --check` passes and QEMU=0. Independent adversarial NO-BOOT review is next;
  no gate or VM is authorized.
- Independent adversarial NO-BOOT terminal-route review **REJECTS** that
  self-PASS. The actual diag1 selector is only reached after the generic
  root-prompt/KDE-ready serial waits: their timeout/EOF/panic paths call generic
  `finish` with no nonce-bound `DIAGNOSTIC_INVALID` raw artifact, and the
  diagnostic extractor merely appends an artifact failure. The purported
  dynamic proof mocks `capture_completeness_diag_command_result`, so it does
  not exercise real timeout/EOF/panic/deadline terminalization; it also omits a
  capture/framing-invalid stage and full-finish cleanup precedence, whose
  lingering-process checks can replace the diagnostic label. Repair must own
  every diag1 serial terminal path from bootstrap through collector, retain
  disposition/raw status through all postvalidation, and add end-to-end
  non-mocked launch/framing/before/after/timeout/EOF/panic/deadline POCs plus
  explicit diag0 parity. Static-only JS remains non-credit. QEMU=0; no gate or
  VM is authorized.
- The bootstrap-to-collector terminal-owner repair is a NO-BOOT **self-FAIL**,
  not a review candidate. Its first two bounded 1.1 s local-shell timeout
  transport POCs both returned `DIAG_EOF` rather than the expected
  `DIAG_TIMEOUT`; timeout ownership is therefore unproved. No QEMU was
  spawned, no boot authority exists, and no source claim or checkpoint follows.
  First make the local timeout transport deterministic, then rerun the complete
  bootstrap-to-collector static matrix before another independent review.
- Follow-on timeout-transport forensics stopped again at the matrix's first
  row: its child retained a live exact `/proc/<pid>/fd/1` PTY, but `spawn` ran
  inside the fixture proc without a global Expect `spawn_id`, so the nested
  command helper consumed a stale/default channel and returned `DIAG_EOF`.
  The deterministic `exec /bin/sleep` fixture is still unproved until that
  scope fix is rerun; this is a NO-BOOT self-FAIL, not review authority.
  The child was synchronously reaped and exact QEMU count was zero.
- The scoped-`spawn_id` matrix then reached its fifth real transport row:
  negative-scope, timeout, EOF, and panic passed, but the framing fixture
  returned `DIAG_EOF` instead of `FRAME_INVALID`. Its immediate fixture fence
  raced the sent serial line, yielding raw `t<marker>:FENCE` rather than an
  authenticated line-start marker. Matrix execution stopped there; gate the
  fixture emission behind consumed input (without weakening framing) before a
  fresh complete matrix. This remains NO-BOOT self-FAIL; no QEMU/boot/credit.
- The gated C5 repair subsequently passed both controls (early emission
  rejected; consume-then-emit reached `FRAME_INVALID`), but the same restarted
  matrix stopped at deadline-post before any serial byte was sent:
  `DIAG_DEADLINE_PRE_SEND final-timeout-parameter-invalid` replaced the
  expected post-send status because its 1.1 s absolute deadline was already
  incompatible with a requested 30 s command budget. Make that fixture admit
  a valid initial command window and exhaust it only during its slow send,
  then restart the complete matrix. This is NO-BOOT self-FAIL; later rows did
  not run and JS remains non-credit.
- The attempted short-budget deadline-post proof likewise stopped before send:
  `media_probe_final_timeout_seconds` rejects its `post_ms` parameter when it
  is zero, while the local transport invokes the command helper's default
  `reserve_ms=0`. Thus even the explicit preplan reports
  `final-timeout-parameter-invalid`; it proves no post-send behavior. The
  fixture must pass a legal nonzero reserve and then show pre-send admission
  plus slow-send exhaustion; assess any live default-reserve caller separately.
  This is NO-BOOT self-FAIL, with no later matrix rows or credit.
- Static live-call enumeration is a NO-BOOT forensic **FAIL**: diagnostic
  launch, diagnostic `fbstat` before/after, and diagnostic collector all pass
  a nonzero deadline while inheriting `reserve_ms=0`, so their final timeout
  planner cannot admit a command. Normal media initial/final callers pass
  explicit nonzero reserves. No live behavior or VM was changed, and no
  fixture matrix was run after this finding. A separately designed and
  reviewed live diagnostic-reserve repair is required before the complete
  matrix can provide any review authority; JS remains non-credit.
- Focused NO-BOOT diagnostic-reserve **DESIGN PASS** only (not implementation
  or review): add diag1-only `capture_completeness_diag_reserve_ms=5000` and
  raise its outer deadline from 150000 to 165000 ms. Launch (35 s), before
  fbstat (35 s), collector (30 s), and after fbstat (35 s) must each pass that
  explicit reserve to the existing diagnostic command API; do not change its
  zero default, which remains useful to isolated transport reducers. The
  shared absolute deadline invariant is `165000 = 25000 settle + 35000 +
  35000 + 30000 + 35000 command caps + 5000 tail`; at every pre/post-send
  planner call, `remaining-reserve >= 1000`, and selected timeout remains in
  `[1, stage-cap]` seconds. Add static source/runtime POCs rejecting zero or
  too-large reserve, a mutation of launch/collector/the shared before+after
  fbstat caller, and diag0 poisoned-runner OFF parity; preserve raw terminal
  ownership, no-FPS/no-semantic credit, normal budgets, and defaults. Then
  independently review NO-BOOT before rerunning the full transport matrix.
- The first post-implementation static checkpoint is a NO-BOOT **self-FAIL**:
  `static_capture_diag_actual_terminal` reads raw-contract index 3 (the phase)
  where the contract is `ok detail attempt phase raw_status marker`; raw status
  is index 4. C1 otherwise emitted the correct nonce-bound `DIAGNOSTIC_INVALID`
  label retaining `DIAG_EOF` with no-FPS/no-semantic markers, but the broken
  assertion proves nothing. Correct that POC index before any rerun; no
  QEMU/boot/credit and JS remains non-credit.
- The rerun after correcting that raw-status slot is again a NO-BOOT
  **self-FAIL** at the aggregate static source gate, not a runtime/PID/QEMU
  result: it wrongly requires direct `terminalize` text despite valid delegated
  collector/owned-terminal routing, and its new reserve patterns reject Tcl
  line-continuation backslashes, falsely reporting launch/before/collector/
  after absent. No proof follows. Repair those assertion grammars and their
  negative mutations before rerun; JS remains non-credit.
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

1. Implement the reviewed-design candidate NO-BOOT: diag1-only 5000 ms
   explicit reserve at launch, collector, and the shared before/after fbstat
   caller, with the 165000 ms outer envelope and the exact static mutation/
   diag0-off matrix. Independently adversarially review it; only then rerun
   the scoped-`spawn_id` complete timeout/EOF/panic/deadline,
   launch/framing/before/after, full-finish, source-order, and explicit-off
   diag0 matrix. Keep nonce/raw INVALID status, stage-bound fbstat shapes,
   parser replay/cap, and JS non-credit. The framing fixture must emit only
   after consuming serial input; its deadline-post fixture must admit before
   send and expire only while sending. First correct the actual-terminal raw
   status assertion (index 4), then repair the static delegation and
   Tcl-continuation assertion grammars plus negative mutations before rerunning
   the static/in-process proof and matrix. No gate or VM is authorized until
   the repair and matrix both pass review.
   Latest NO-BOOT self-FAIL: the repaired full static/in-process reducer reached
   its aggregate gate; delegated-terminal ownership and its bypass mutation,
   plus before/after reserve mutations, passed/rejected as intended, but the
   launch and collector reserve negative mutations were no-ops and were
   accepted. Repair only those two mutation POCs, then rerun from C1; no boot,
   default change, or performance claim is authorized.
   Follow-on NO-BOOT self-FAIL: launch and collector mutations are now scoped
   to the normalized production proc bodies, each proves exactly one applied
   rewrite/re-extraction, and all four reserve negatives reject; delegated
   terminal/bypass remains correct. The restarted C1 matrix nevertheless exits
   2 at the same aggregate static gate. Treat this as an assertion-aggregate
   failure, not a runtime/VM result; isolate its remaining predicate before any
   further matrix run. JS remains non-credit; QEMU is zero and no boot/default
   change is authorized.
   Aggregate-forensic verdict: the diagnostic ledger reports every other named
   aggregate predicate passing; the sole failure is `bootstrap-order`, observed
   `diagnostic-bootstrap-marker-invalid`. The bootstrap source contract and
   its static mutation POC embed their own exact BEGIN/END marker literals, so
   the contract's global uniqueness test counts those assertion literals as
   extra markers. Next repair only that assertion grammar: construct both
   contract and POC marker strings from pieces, then preserve one production
   marker, the injected-normal-path rejection, and all existing reserve/
   terminal checks before any matrix rerun. This is NO-BOOT; JS remains
   non-credit and no gate/default/VM is authorized.
   Bootstrap assertion repair self-PASS through the complete aggregate ledger:
   exactly one baseline BEGIN/END, injected normal-path, duplicate, missing,
   reordered, and source-literal-poison inputs reject; all reserve, terminal,
   diag0, and in-process predicates pass. The redirected static invocation
   then exits 1 only while printing a later PASS banner (`stdout` bad file
   number after Expect-child cleanup), so it is a harness-output failure, not
   a static/matrix PASS. Rerun with the normal inherited output channel before
   claiming the full suite; JS remains non-credit, QEMU is zero, and no boot
   or default change is authorized.
   Normal-inherited-stdout rerun is the same NO-BOOT **self-FAIL**: the
   aggregate ledger is fully PASS (including C1 transport), then the later
   `YT-CAPTURE-COMPLETENESS-DIAGNOSTIC-STATIC-PASS` write fails `stdout: bad
   file number`. This is therefore Expect stdout ownership/restoration after
   local-child cleanup, not a redirection-wrapper artifact. Do not claim full
   static/matrix PASS; repair/review that harness I/O path before another run.
   JS remains non-credit; QEMU is zero and no VM/default is authorized.
   The focused stdout-ownership repair is a NO-BOOT **self-PASS**. The
   missing-`global spawn_id` negative had sent nested `send`/`expect` to
   implicit `exp0`, closing user stdout on EOF. It now stages an owned stale
   child in `::spawn_id`, synchronously reaps stale and real children, then
   restores the prior global. The omitted-restore source mutation rejects, and
   `puts stdout` passes after scope-missing, timeout, EOF, panic, both frame
   fixtures, deadline pre/post, and aggregate cleanup. Full
   static/in-process/C1 exits 0 with aggregate failures empty and both
   diagnostic/final PASS banners. JS remains non-credit; independent NO-BOOT
   review is next, and this grants no VM/default authority (QEMU zero).
   KVM bootstrap source-route first self-FAIL: its initial live-call bypass
   mutation used `require_kvm_proof_bypassed`, retaining the required token as
   a prefix, so the exactly-applied mutation guard correctly rejected it as
   ineffective. The stop instruction arrived after a token-free replacement
   and an already-started rerun had completed exit 0; that post-stop result is
   recorded only for transparency and grants no review/VM authority. Next is a
   fresh scoped review of the KVM route/mutations, not a boot; JS remains
   non-credit and QEMU is zero.
   Fresh KVM-route verification is a NO-BOOT **self-PASS** (separate from the
   post-stop completion): full static/in-process/local C1 exits 0, aggregate
   failures are empty, and the token-free live-call bypass, terminal rebind,
   and status-removal mutations each apply exactly and reject. The live route
   binds the one bootstrap-block `require_kvm_proof` call to guarded
   `bootstrap-kvm`/`DIAG_BOOTSTRAP_KVM` before generic finish and normal paths;
   the synthetic terminal row remains additional unit evidence only. Stdout
   liveness/PID checks and both PASS banners pass. JS remains non-credit;
   independent adversarial NO-BOOT review is next and no VM/default is
   authorized (QEMU zero).
   Fresh independent NO-BOOT re-review **PASSES** (review only), superseding
   the KVM source-route gap: the contract binds exactly one live bootstrap
   `require_kvm_proof` before normal paths, its diag1-guarded
   `bootstrap-kvm` owned terminal and `DIAG_BOOTSTRAP_KVM` before generic
   finish. Token-free call bypass, terminal rebind, and status-removal
   mutations each apply exactly once and reject. The full diag1 static/local
   C1 suite exits 0 with empty aggregate failures; 165000/5000 four-caller,
   raw/no-FPS precedence, root/KDE, marker, diag0, and stdout/PID checks pass.
   No local fixture or QEMU remains; JS is non-credit. This does not authorize
   a default change, VM, gate, or FPS claim; a fresh conductor gate is still
   separately required for any diagnostic boot.
2. Add/review deterministic fullscreen evidence; then sole-VM fullscreen N>=2.
3. Sole-VM A2 Pulse localization and reviewed stream fix, still held behind A1.
4. Validate audio-on windowed and fullscreen parity; close each mode's
   fullscreen-scaling/present-retire residual before the overall goal.
5. Run kickoff+tooltip promotion battery, then the both-mode video-pair battery
   as lanes free.

Closed: kernel audio base, codec-capacity theory, loader/config M4 levers,
single-lib stubs, syscall/VFS-cost-as-M4, unlocked-wait, present-clock alone,
and PCID/noflush absent prerequisites. Reopen only on new focused evidence.
