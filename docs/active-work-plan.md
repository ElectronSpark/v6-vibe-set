# Active xv6 Work Plan

Last updated: 2026-07-12. This is the sole live plan. Superseded V2/V3
self-failure chronology and disposable artifacts remain in git history.

## Objective and acceptance

Close YouTube 720p60 to the comparable Linux VM on this KVM+virgl host in two
separate, mandatory modes:

1. **Windowed watch page:** N>=2 valid real KVM+virgl runs, first >=52
   presented fps, then about 55-60 with low drops and stable pacing.
2. **Actual player/document fullscreen:** a separate nonce-bound proof of real
   player or document fullscreen, settled active 1280x720 playback, then N>=2
   valid runs with the same thresholds.

A maximized window is not fullscreen. Windowed and fullscreen samples never
pool, and either mode missing its own evidence leaves the overall goal open.
Only `yt-presentfps` plus `PERF-VIDEO` on real KVM+virgl GL count; software
fallback, llvmpipe, N=1 timing wins, and diagnostic-only output do not.

## Binding host and VM discipline

- WSL crashed from overlapping recursive raw-image `rg --text` scans (about
  24 GiB and 6.7 GiB RSS), not QEMU. Repository searches use only
  `/home/es/.local/bin/rg`, source trees only: never recursive images/artifacts
  or `-a`, `-u`, `--no-ignore`, or `--binary`. A necessary artifact operation
  names each regular file and uses the bounded artifact wrapper or `debugfs`.
- Every build, VM, and serial command is synchronously reaped. Serial silence
  is not completion: use short marker commands, allow the known first
  bracketed-paste character drop, wait for the owned command, then reacquire a
  prompt. Never use `pgrep`.
- The conductor authorizes at most one VM worker. Before a VM authorization,
  require no active VM worker and an exact `/proc/*/exe` count of zero for
  `qemu-system-*`/`qemu-kvm`; repeat after owned process-group cleanup and
  synchronous reap. For a performance-VM gate, add one conductor-only,
  synchronous passive zero-QEMU interval of at most 60 s (exact all-arch
  checks only at its beginning and end, no monitor loop), followed by one
  immediate all-arch prelaunch check. This is a short stability screen, not a
  lock: an external QEMU can still launch after the last check, so the
  conductor must abort overlap and reap only its own group. Never edit
  QEMU/launcher scripts to enforce this. Guest RAM is not capped; concurrent
  VMs are.
- Every authorization is named-branch specific: compare HEAD with
  `origin/<current-branch>`, never `@{upstream}`. A consumed gate needs a new
  gate. Preserve unrelated dirt and never touch an external QEMU.

## Current authority and diagnostic state

There is **no current VM authority**. The only prior authorized V3 diag boot,
`chromium-youtube-m7/20260711T150252Z-pid1861734-mp1-audio1-media1-hd7201-capturediag1`,
was a real KVM+virgl boot but N=0 INVALID/NULL: virgl/OpenGL-submit and media
assets were valid, yet its terminal was INCOMPLETE `diagnostic-no-marker-rows`
and the outer validator returned code 8 on diagnostic-artifact disagreement.
It grants no timer/rVFC, semantic, or FPS credit. Its QEMU group was reaped.

The contemporaneous bounded artifact forensic located the observed absence
before serial parsing: the capture file was zero bytes and serial had only
BEGIN/END plus RC/FENCE. The helper's raw status 0 proves neither source-log
presence nor a marker match. Treat that run as invalid evidence, not a kernel,
codec, or presentation conclusion.

**Consumed V3 diag1 gate — INVALID/NULL (2026-07-11):** token
`V3-DIAG1-5d8521a-20260711T172315Z-Q0` was consumed by exactly one fresh run,
`20260711T172454Z-pid2002542-mp1-audio1-media1-hd7201-capturediag1`.
Prelaunch branch/origin and exact-QEMU gates passed; image preflight passed all
three current asset hashes. The owned run proved KVM launch and WSL D3D12 virgl
host selection, and the guest reported virgl capsets/render node, but it
returned driver code 8 before diagnostic launch send:
`INVALID command-launch-status-DIAG_DEADLINE_PRE_SEND`. All three terminal
receipts are the retained nonregular/zero empty state; fbstat is absent and
there are no producer, post-video, collect, timer, rVFC, FPS, audio, fullscreen,
or semantic facts. Serial and summary terminal tuples match, raw witness is
zero-byte, and `media_probe_image_assets=PASS` records the three exact hashes.
The owned QEMU group was synchronously reaped (`waited:2002846 exp4 0 0`) and
the final exact QEMU count was zero. Preserve named evidence in
`/tmp/xv6-v3-diag1.dvKyhE`, log `/tmp/xv6-v3-diag1-log.wzCwlG`, and retained
scratch image `/tmp/xv6-yt-20260711T172454Z-pid2002542-mp1-audio1-media1-hd7201-capturediag1.fs.img`.
This is INVALID/NULL and consumes the gate; no retry or VM authority exists.

**V3 launch pre-send forensic — ACCOUNTING DEFECT / NO-BOOT (2026-07-11):**
named evidence fixes host START at 17:24:55Z, QEMU ownership at 17:25:33Z,
and terminal write at 17:26:23Z (STATUS completion/cleanup at 17:26:24Z), but
does not retain V3 `start_ms` or `before_send_ms`. That absence cannot describe
guest behavior. It does not block localizing this result: V3 launch first
admits `35 s + 170 s downstream + 5 s reserve` against the 210 s outer budget,
then deliberately calls the generic command helper with inner `post_ms=0` to
avoid subtracting the global reserve twice. The generic helper rejects every
`post_ms < 1` *before* computing remaining time, so the pre-send branch returns
`DIAG_DEADLINE_PRE_SEND` with the deterministic
`final-timeout-parameter-invalid` detail; marker C1 is minted before that
check and no serial send can occur. The V3 terminal retains status but not that
raw detail, explaining the named artifact's empty raw witness. This is a
fail-closed internal API/accounting mismatch, not a guest timing conclusion.

The reviewed 210 s envelope starts only after KDE-ready dispatch (thus excludes
preflight/QEMU/KDE startup); it includes V3 serial-command time. Endpoints are
35/105/140/170/205 s with the 5 s global reserve counted in V3 admission once.
The exact sums intentionally leave zero admission slack at each stage start
(for example launch admits only when `now == start_ms`); that fragility did not
produce this raw status but must be explicitly reviewed. The passing static
route does not execute real V3 dispatch: under `YT_STATIC_CHECK=1` it selects
the retained V1 runner, directly tests V3 admission edges, and its generic
pre-send fixture itself expects the same zero-tail parameter failure. **Next
dependency:** narrow NO-BOOT V3 inner-tail repair plus static/adversarial review.

**V3 inner-tail repair — targeted static PASS / NO-BOOT (2026-07-11):** the
generic final-timeout API now defaults to rejecting zero as before, and admits
zero only with the explicit `allow_zero_inner_tail=1` opt-in; it rejects
negative, malformed, unspecified-zero, and nonzero-opt-in values before
deadline arithmetic. Only the V3 wrapper supplies `post_ms=0, min=1000,
allow_zero_inner_tail=1`, after V3 admission has retained the single global
5 s reserve. Fresh empty host-only replay
`/tmp/xv6-v3-inner-tail-static.84iOko` (external log
`/tmp/xv6-v3-inner-tail-static-log.nbvZpN`) exited 0 and exercised the real V3
wrapper over a synchronously reaped local PTY: valid pre-send returned raw 0,
an expired stage returned `DIAG_V3_ADMISSION/v3-admission-stage-deadline`, and
unspecified/negative/malformed tail cases returned
`DIAG_DEADLINE_PRE_SEND/final-timeout-parameter-invalid`. It proves the exact
zero-slack `1000 command + 5000 reserve = 6000 outer` boundary passes while
5999 fails, so no second reserve debit occurs. The retained V1 static dispatch
remains diag0-off/no-credit and reports `js_guest_runtime=UNEXECUTED`. Exact
`/proc/*/exe` scans immediately before and after found zero QEMU processes.
This is source/static credit only; it is not an independent review, VM,
diagnostic, semantic, audio, fullscreen, or FPS result.

**Post-replay external VM context (2026-07-11):** after the completed replay,
one non-owned process appeared in the exact `/proc/*/exe` scan:
PID 2014209 `/usr/bin/qemu-system-riscv64`, parent 2014208, with a `-machine
virt -nographic -m 1024M` command line. It was absent in both replay-boundary
scans, was neither launched nor touched by this source-only work, and does not
change the static verdict. It blocks every future VM authorization until its
owner has synchronously reaped it and a fresh exact count is zero.

**V3 inner-tail independent adversarial review — PASS / NO-BOOT (2026-07-11):**
fresh empty host-only replay `/tmp/xv6-v3-inner-tail-independent.DWu6Sq`
(external log `/tmp/xv6-v3-inner-tail-independent-log.K4FPVi`) exited 0;
exact `/proc/*/exe` QEMU counts immediately before and after were both zero.
`YT_STATIC_CHECK` dispatch is deliberately V1, so the verdict uses its
dedicated, synchronously reaped local-PTY V3-wrapper fixture: V3 admission
reached the real generic pre/post-send planner and returned owned `RC:0` plus
`FENCE`; an expired stage returned
`DIAG_V3_ADMISSION/v3-admission-stage-deadline`. Exact loaded-source checks in
`/tmp/xv6-v3-inner-tail-boundaries-log.ys18Pc` show generic zero, negative,
malformed, and nonzero-opt-in all reject `final-timeout-parameter-invalid`;
only `post=0,optin=1` returns `remaining-1000-post-0`. Admission is unchanged:
6000 (= 1000 command + 5000 reserve) passes and 5999 fails, proving neither
timing relaxation nor a second reserve debit. The replay reports
`v3_diag0_off_parity=PASS` and `js_guest_runtime=UNEXECUTED`; this is no-guest,
no-VM, noncredit evidence. The formerly observed external QEMU is absent in
this audit, but every future VM gate still requires a fresh exact zero-QEMU
preflight; any external QEMU blocks authorization and is never touched.

**A1 windowed N>=2 measurement-session gate — FORMED (2026-07-11):** initial
no-boot audit at `d621eac33ebb6c9d66608e04fbf05b2cbfd5cea8` found exact equality
with `origin/codex/host-linux-abi-shell-port-ff`, no staged source state, only
the unrelated KDE-smoke dirt, no active VM worker, exact QEMU count zero,
read/write `/dev/kvm`, WSL `/dev/dxg` + D3D12/GL dependencies, both X11 and
Wayland display sockets, and all three overlay/base image assets at the
accepted byte/hash receipt. Subject to a repeat post-push audit, token
`A1-WINDOWED-N2-d621eac-20260711-Q0` permits exactly two sequential, separately
valid **windowed** real-KVM+virgl trials only: MP=1, audio-disable=1,
media=1, forced-HD720=1, EGL=0, capturediag=0. Each trial needs its own fresh
run directory and exact zero-QEMU check immediately before launch; its worker
must own/synchronously reap its QEMU group, verify exact zero QEMU before the
next trial, and abort rather than overlap. An external nonzero QEMU is a
nonconsuming gate failure requiring fresh authority; any launched trial is
consumed even if INVALID. This grants no fullscreen, audio-on, default, or
semantic credit; only valid `yt-presentfps` + `PERF-VIDEO` samples count.

**A1 windowed session post-push re-audit — BLOCKED / NO-BOOT (2026-07-11):**
at gate checkpoint `5e5694f91b3cd40985852960598062893b82bd50`, equal to the
explicit origin branch, the repeat audit again found no staged source state,
only unrelated KDE-smoke dirt, no active VM worker, usable KVM, present
D3D12/GL/display prerequisites, and all three current base assets at the
accepted byte/hash receipt. It failed the mandatory exact QEMU condition:
one non-owned process, PID 2024690 `/usr/bin/qemu-system-riscv64` with
`-machine virt -nographic -m 1024M`, was live after the push. It was not
touched. The session was never launched or consumed, its token was not issued,
and a fresh authority is required only after its owner reaps it and a new gate
again observes exact QEMU count zero.

**Fresh A1 windowed execution session — CONSUMED / INVALID (2026-07-11):**
the conductor replaced the unissued session with a live execution gate at
`6a7c9ac5110a8636f86dee34a15abd8ee4b299a4`, equal to the explicit origin
branch. Immediately before the sole launch, no VM worker was active, exact
QEMU count was zero, KVM/D3D12/GL/X11/Wayland prerequisites and all three
base-image hashes passed, and only unrelated KDE-smoke dirt existed. Trial 1,
windowed MP=1/audio-disable=1/media=1/forced-HD720=1/EGL=0/capturediag=0,
started in `/tmp/xv6-a1-windowed-t1.7zGOJ9` (external log
`/tmp/xv6-a1-windowed-t1-log.qCIalN`). Its owned QEMU was PID 2029259; during
bootstrap a non-owned RISC-V QEMU PID 2031440 appeared, making the exact count
two. The external process was never touched. The owner was terminated to
abort overlap; its driver recorded code 143 and synchronously reaped only its
own group (`cleanup-finished remaining=none wait=waited:2029259 exp4 0 0`),
then the exact final count was zero. It reached an idle virgl/OpenGL-submit
fbstat only; no active 1280x720 source, yt-presentfps, PERF-VIDEO, drop, VPQ,
retire, audio, fullscreen, or semantic measurement exists. This is one
consumed INVALID attempt and N=0 valid samples, not N=1 performance credit;
trial 2 was correctly not launched. A new fresh gate is required.

**External-QEMU overlap forensic — INSUFFICIENT PROVENANCE / NO-BOOT
(2026-07-11):** the three observed processes (PIDs 2014209, 2024690, and the
overlap 2031440) are proved only to be non-owned RISC-V QEMU relative to the
then-active xv6 owner. The plan retains `-machine virt -nographic -m 1024M`
for the first two, but no retained parent PID, cwd, complete argv, or launch
record attributes any of the three to a user, an agent, or another worktree.
The retained A1 launch command and run log prove the owned process was only
the x86 KVM+virgl launch (PID 2029259). At the exact trial commit `6a7c9ac`,
the repository RISC-V launcher instead hard-coded `-m 256M`; guarded source
search found neither `qemu-system-riscv64` nor the 1024M command shape. That
excludes this committed launcher shape, not an unrecorded manual/other-tree
launch; strongest supportable attribution is therefore **insufficient**, not
user/external attribution.

The historical driver had an x86-only `exact_qemu_lines` predicate, so it did
**not** detect PID 2031440 itself. Its SIGTERM (code 143) was the conductor's
correct overlap abort, and its owned-only cleanup/reap passed; it reached idle
fbstat/probes but no Chromium launch, active HD720 source, yt-presentfps, or
PERF-VIDEO capture. Thus no performance sample exists. The x86-only predicate
is not singleton enforcement; per the user direction, no QEMU/launcher-script
change is authorized or needed for the conductor-owned policy. A fresh exact
all-arch audit is currently zero, and a synchronous passive 20 s zero-QEMU
interval was zero at both ends. This validates only the screen, grants no VM
authority, and cannot prevent a later external launch.

**A1 passive-quiescence windowed trial — INVALID / N=0 (2026-07-11):** a
fresh all-architecture screen was zero, followed by one passive synchronous
60 s wait with no polling, then a second zero count and immediate prelaunch
audit at `3a6c2326dbfdeae96e3ad05e04893bdbcb55bb54`, equal to origin. KVM,
D3D12/GL/X11/Wayland, and all three image assets passed. The one permitted
windowed MP=1/audio-disable=1/media=1/forced-HD720=1/EGL=0/capturediag=0 run
is `/tmp/xv6-a1-windowed-quiescent-t1.pKZQwy` (external log
`/tmp/xv6-a1-windowed-quiescent-t1-log.WcYJXz`). At its single 75 s bootstrap
audit, exact all-arch count was one: owned x86 QEMU PID 2041507 (parent driver
2041348), with zero foreign processes; its command records GTK
`full-screen=off` and `zoom-to-fit=off`. The driver then returned code 8,
`chromium-render-start-missing`; its owned cleanup synchronously reaped PID
2041507 (`waited:2041507 exp4 0 0`) and the final all-arch count was zero.
Current assets passed, as did idle/probe virgl fbstat, but no Chromium render,
active HD720 source, yt-presentfps, PERF-VIDEO duration/FPS/drop/VPQ/retire,
audio, or fullscreen fact exists. This consumed the one-trial session as
INVALID/N=0, not performance credit; no overlap and no second guest occurred.
A fresh gate and a no-boot localization of `chromium-render-start-missing` are
required before another A1 trial.

**`chromium-render-start-missing` forensic — GATE LOCALIZED / ROOT CAUSE
UNRESOLVED / NO-BOOT (2026-07-11):** retained run
`/tmp/xv6-a1-windowed-quiescent-t1.pKZQwy` reached the normal render-start
predicate at `chromium-youtube-presentfps.expect:8271-8281`: after Chromium
launch and a 12 s argv check it allows five 30 s probes only if
`flips > idle+100` **and** `presents > idle`. Idle was 3/4; probes were
13/14, 18/19, 19/20, 19/20, and 20/21, so only the presents condition passed
and the exact terminal label is correct. The normal guest command did run:
the canonical source log has `launch_marker pid=155`, the complete treatment
argv/nonce/extension URL, and `child_exec`; its redirect contract makes the
empty outer stdio and absent optional chrome-debug log non-dispositive.
The log also shows child no-connection terminations and a network-service
restart, but retains no main-browser exit, role census, or Wayland-surface
fact, so it cannot be called the cause.

This is neither missing/wrong/unreadable source-log evidence nor a
marker/parser rejection: `YT_MEDIA_PROBE_V1` collection begins only after the
render gate, and the media artifact is therefore correctly absent. With
`capturediag=0`, the explicit post-KDE branch skips diagnostic dispatch and
its capture-completeness V1/V3 runners entirely; the recent V3 inner-tail
contract is not on this control path. Do not propose a rendering repair yet.
The smallest next no-boot
addition is a bounded render-start receipt retaining the launch/argv command
frames plus the existing short Chromium role census and canonical-log
cursor/digest at each probe; it must distinguish absent/exited roles from
live-but-no-presentation before any new gate. Current exact all-arch QEMU
count is zero; this forensic ran no VM.

**A1 render-start receipt source/static — PASS / NO-BOOT (2026-07-11):** the
normal `media=1`, `capturediag=0` path now takes a nonce/probe-bound,
transport-framed receipt on every attempted render-start probe.  It binds the
verified launch and argv command-frame markers/digests, a capped existing
Chromium fast-role census (command/status/bytes/digest), capped canonical
launcher-log cursor/frame/digest, and that probe's fbstat flip/present values.
The host-only static reducer passed with missing/unreadable/nonregular source,
no/exited role, failed census, live/no-presentation, marker drift, byte/digest
tamper, truncation, cursor drift, and five-probe ordering negatives rejected;
it also proves the retained `flips > idle+100 && presents > idle` predicate
and `capturediag=0` V3 isolation without executing guest JS. Exact all-arch
QEMU count was zero before and after. This is diagnostic-only/no-credit: an
INVALID or INCOMPLETE receipt stops before FPS, semantic, audio, or fullscreen
success. It does not explain the guest failure or create any performance fact.
Next queue item is an independent no-boot adversarial review of this receipt;
only then may a fresh A1 execution gate be formed.

**A1 render-start receipt independent adversarial review — FAIL / NO-BOOT
(2026-07-11):** fresh normal `media=1,capturediag=0` static replay
`/tmp/xv6-render-receipt-independent.ZxoU6d` (external log
`/tmp/xv6-render-receipt-independent-log.o8YXJp`) exited 0 with exact QEMU
counts zero before and after, and independently re-confirmed nonce/probe
framing, frame binding, source-status/role negatives, threshold retention, and
diag0/V3 isolation. It does **not** prove the claimed tail integrity. The real
parser accepted the named adversarial frame in
`/tmp/xv6-render-receipt-tail-adversary-log.gI1Rk7`: `source_status=regular`,
`source_cursor=2048`, `source_bytes=0`, empty-frame digest, and falsely
`source_tail_truncated=0` parsed `pass` and classified `OBSERVED` for 104/5
against idle 3/4. The parser checks only `source_bytes <= source_cursor` and
the flag's syntax; regular-source tail truth is not enforced. In the live
loop, an OBSERVED receipt avoids the no-credit-invalid path, so this is a
receipt-integrity failure even though it cannot forge host fbstat.

No source change is made here. The minimal repair contract is: for regular
source require `source_tail_truncated == (source_cursor > source_bytes)`;
carry a bounded `role_total` and enforce the analogous role-tail invariant;
static adversaries for both false/true tail mismatches must parse INVALID and
cannot reach OBSERVED. Re-run this independent review after that narrow repair;
no A1 VM gate, retry, FPS, semantic, audio, or fullscreen authority exists.

**A1 render-start receipt tail-integrity repair — STATIC PASS / NO-BOOT
(2026-07-11):** the normal receipt now carries bounded `role_total`; the
parser rejects regular source unless `source_tail_truncated` exactly equals
`source_cursor > source_bytes`, and rejects roles unless the analogous
`role_total > role_bytes` relation (and `role_bytes <= role_total`) holds.
Fresh host-only replay `/tmp/xv6-render-tail-integrity-static.EO673i`
(external log `/tmp/xv6-render-tail-integrity-static.EO673i.log`) exited 0.
It rejects both false/true directions for source and role tails as `INVALID`,
keeps valid untruncated and valid truncated receipts non-OBSERVED/OBSERVED as
appropriate, and retains nonce/probe/digest caps, the 5x30 s flip/present
predicate, and diag0/V3 isolation with `js_guest_runtime=UNEXECUTED`. Exact
all-arch QEMU counts before and after were zero. This is source/static-only,
does not explain Chromium rendering, and supplies no FPS, semantic, audio, or
fullscreen credit. The independent adversarial review must now be rerun before
any A1 VM authority or fresh gate.

**A1 render-start receipt tail-integrity independent adversarial review — PASS /
NO-BOOT (2026-07-11):** a fresh normal `media=1,capturediag=0` static replay
`/tmp/xv6-render-tail-independent.wRqbrZ` (external log
`/tmp/xv6-render-tail-independent-log.wKJqhT`) exited 0; exact all-arch QEMU
counts were zero before and after. Its retained real-parser reducer rejects
both false/true source and role tail mismatches, observes valid truncated
controls, retains the nonce/probe framing and binding, bounded
cursor/bytes/digest/cap rules, the five 30 s `flips > idle+100 && presents >
idle` gate, and `capturediag=0`/V3 isolation.

A separate source-extracted parser harness (external log
`/tmp/xv6-render-tail-parser-adversary.ZfC7X2`, QEMU zero before/after) proved
regular untruncated plus independently source-truncated and role-truncated
controls classify `OBSERVED`; all four false/true tail lies return parser
`INVALID` and cannot be classified `OBSERVED`. It also rejected source/role
cap and total-order drift, nonce/probe, launch/argv digest, and command-marker
mutants, retained the 103/4 and 104/4 threshold negatives with 104/5 observed,
and confirmed that a parser-invalid receipt latches the no-credit path rather
than entering the receipt-object sequence. No VM, guest JS, FPS, semantic,
audio, or fullscreen fact was produced. This clears only receipt integrity: a
new serialized A1 execution gate may now be formed; do not reuse the consumed
session.

**Fresh A1 windowed live gate — BLOCKED / NO-BOOT (2026-07-11):** the exact
all-architecture QEMU scan was zero before and after the required one passive,
synchronous 60 s interval (`/tmp/xv6-a1-windowed-live-gate.fxNUC9`), and the
immediate KVM, WSL `/dev/dxg`/D3D12+GL, X11+Wayland display, kernel/base-image,
and all three named media-asset hash checks passed. The mandatory
branch-lineage check failed: live `HEAD` was
`fb6d28249e35dfc4bb55f52823ed12b42f678016`, while explicit
`origin/codex/host-linux-abi-shell-port-ff` was
`29d050f1416475e306b092bd94f7ff0e144b14f6`. No QEMU was spawned, no trial was
consumed, and no render receipt, HD720, PERF-VIDEO, FPS, semantic, audio, or
fullscreen fact exists. This gate is a no-boot rejection, not permission to
repeat it; a fresh authorization is required after the explicit branch lineage
is reconciled.

**Explicit branch-lineage reconciliation — PASS / NO-BOOT (2026-07-11):** the
historical gate mismatch was configuration, not divergence: local
`codex/host-linux-abi-shell-port-ff` tracks
`refs/heads/codex/host-linux-abi-shell-port`, the truncated remote name. Thus
the earlier default push correctly refused the mismatched upstream and its
then-used fallback updated the truncated ref, not the required `-ff` ref.
Fresh advertised-ref, bidirectional-ancestry, and tree checks found both local
and explicit `origin/codex/host-linux-abi-shell-port-ff` at
`feccc0f2b2457996a6da745c8c3014a9df6a7f9e`; the two commits after `29d050f`
are reviewed plan-only checkpoints (`fb6d282`, `feccc0f`), and the sole dirt is
the preserved KDE-smoke file. This checkpoint is pushed only by the exact
`HEAD:refs/heads/codex/host-linux-abi-shell-port-ff` refspec and re-read after
push. It removes the stale lineage block only: the prior gate remains rejected
and still grants no VM, retry, FPS, semantic, audio, or fullscreen authority.

**Fresh A1 windowed execution after `-ff` reconciliation — CONSUMED / INVALID
/ N=0 (2026-07-11):** the explicit-`-ff` preflight at
`e4a25de0e85da7a08a0b71a8372da1fea9f355cf` passed: exact all-arch QEMU zero
before/after the passive 60 s screen, HEAD equal to explicit
`origin/codex/host-linux-abi-shell-port-ff`, no other VM worker, KVM,
DXG/D3D12+GL/X11/Wayland, kernel/base image, and all three named assets. The
one windowed MP=1/audio-disable=1/media=1/forced-HD720=1/EGL=0/capturediag=0
trial is `/tmp/xv6-a1-windowed-ff-t1.6GdOdZ` (driver log
`/tmp/xv6-a1-windowed-ff-t1.6GdOdZ.driver.log`). Its sole natural-bootstrap
all-arch audit saw exactly one owned x86 KVM+virgl QEMU and zero foreign QEMU;
the post-run count was zero after the driver's owned synchronous reap
(`waited:2084735 exp4 0 0`).

The run reached real virgl fbstat and the unchanged render predicate (idle
3/4; probe 823/824, so `chromium-rendering-confirmed` was correctly emitted),
but the new guest receipt helper used unavailable `awk`/`tr` and an unsupported
guest `wc -c`. Its nonce-bound probe emitted malformed role totals/bytes and
the host parser correctly returned `receipt-meta-parse-drift`; driver code 8
then stopped before any media/HD720 semantic evidence or FPS windows. Thus
there is no yt-presentfps, PERF-VIDEO, HD720, audio, fullscreen, semantic, or
performance fact despite the raw flip increase. This consumes the sole fresh
trial as INVALID/N=0. Do not retry or boot again under this authority; repair
the helper's actual guest-tool contract and repeat independent no-boot review
before requesting a new gate.

**Render-receipt guest-tool compatibility forensic — PASS / NO-BOOT
(2026-07-11):** named staged helper
`/tmp/xv6-a1-windowed-ff-t1.6GdOdZ/ytrenderstartreceipt.sh` proves the fault is
local to lines 41/50/61/63 (`wc -c`), 52/65 (`sha256sum | awk`), and 74/75
(`od | tr`). Its named serial receipt records `awk: command not found`,
`tr: command not found`, and `wc:cannotopen-c`; the latter was serialized as
both role totals and bytes, while the source-log `wc` failure falsely demoted a
regular source to `unreadable`. The host parser therefore correctly rejected
the malformed meta, and the run's real flip increase is still no-credit.

Both the current base and retained trial scratch rootfs have `/bin/bash`,
`/bin/wc`, `/bin/dd`, `/bin/xxd`, and `/bin/openssl`, and lack `awk`, `tr`,
`od`, `sha256sum`, and `stat`. The current `wc` source has no option parser:
stdin invocation emits exactly `lines words bytes` (with only trailing
whitespace), so a pure-shell repair can disable globbing, split it, require
exactly three decimal fields, retain field three only when `<=2147483647`, and
reject empty, extra, nondecimal, negative, or over-cap output. `/bin/xxd -p`
can be split and concatenated only after every token is lowercase hex and its
final length is exactly `2*bytes`; this accepts empty frames and rejects bad
hex or length drift. `/bin/openssl dgst -sha256 -r` emits lowercase digest plus
`*path`; require exactly those two fields, a 64-hex digest, and the expected
temporary path. A host-only contract reducer passed empty, binary/multiline,
malformed, path-drift, and cap cases; QEMU was zero before and after.

The minimal source repair is restricted to this helper: use those three
available commands plus shell builtins, preserve the existing 2048/4096 frame
caps and source/role total-tail equations, and check every `dd`, count, hex,
and digest operation. An internal helper/tool/format failure must exit nonzero
so the existing command-frame path becomes `INVALID`; it must not claim an
actual source is `unreadable` or emit a malformed receipt. Add static and
independent no-boot adversaries for empty frames, malformed count/hex/digest
output, and over-cap totals before any fresh A1 gate. No source, rootfs,
QEMU-script, VM, media, FPS, semantic, audio, or fullscreen authority is
created by this forensic.

**Render-receipt helper portability repair — STATIC PASS / NO-BOOT
(2026-07-11):** the staged helper now uses only guest-present `/bin/wc`,
`/bin/dd`, `/bin/xxd -p`, and `/bin/openssl dgst -sha256 -r`, with globbing
disabled and strict count/hex/digest grammars. Utility, `dd`, count, hex, or
digest failure exits nonzero before receipt emission; it cannot falsely relabel
a regular source `unreadable` or serialize malformed metadata. The existing
2048/4096 caps, cursor/role-tail equations, nonce/digest bindings, render
threshold, and diag0/V3 isolation are unchanged. Fresh host-only replay
`/tmp/xv6-render-receipt-portability-static.VScGYs` (external log
`/tmp/xv6-render-receipt-portability-static.VScGYs.log`) exited 0 with exact
QEMU zero: empty/binary/multiline controls pass and malformed/extra/nondecimal
/over-cap counts, malformed hex/length/path digests, and fail-closed `dd`
paths reject. This is source/static-only; no guest, VM, FPS, HD720, semantic,
audio, or fullscreen fact exists. An independent no-boot review remains
required before any future gate.

**Render-receipt helper portability independent adversarial review — PASS /
NO-BOOT (2026-07-11):** extracted actual helper `ea7d30f` was executed
host-only with the matching current sysroot `wc`/`xxd`/`openssl` binaries (and
only its census command replaced by a deterministic live-role producer). The
named reducer `/tmp/xv6-render-portability-independent.bZmOp0` and external log
`/tmp/xv6-render-portability-independent-log.uZ8rm4` prove valid empty,
binary, multiline, source-tail (2050/2048), and role-tail (>4096/4096) frames.
All are regular/live, parse against the real host parser, and classify
`OBSERVED` only at the retained 104/5 threshold; the parser result is retained
in `/tmp/xv6-render-portability-parser-log.mMz1UV`.

Every independently injected empty/extra/nondecimal/negative/over-cap `wc`,
uppercase/nonhex/length-drift `xxd`, uppercase/wrong-path/extra `openssl`, and
command failure for count/`dd`/hex/digest exited 70 without any receipt-meta
line, so a regular source cannot be silently demoted to `unreadable` or enter
the no-credit object sequence. The actual frames also rejected nonce, probe,
launch/argv digest, and command-marker drift. A fresh normal static replay
`/tmp/xv6-render-portability-independent-static.MRlJCU` (external log
`/tmp/xv6-render-portability-independent-static-log.JqaLy9`) exited 0 with
exact QEMU zero before/after and re-confirmed caps/tails, the unchanged 5x30 s
threshold, and `capturediag=0`/V3 isolation. No guest, VM, FPS, semantic,
audio, HD720, or fullscreen result exists. This clears only helper portability:
the consumed trial remains unreusable, but a new serialized A1 gate may now be
formed.

**Unrecorded A1 passive gate forensic — NO-BOOT / NO AUTHORITY (2026-07-11):**
the named worker gate `/tmp/xv6-final-a1-gate.J359ka/qemu.log` retains only
its initial exact scan at 19:45:24Z (`count=0`); it has no post-60-second row
or terminal PASS/FAIL marker. The owned tool wait handle then disappeared
before emitting command completion, so this is an incomplete gate transport/
execution record, not a timeout, inaccessible-`/proc`, or external-QEMU
finding. One fresh exact `/proc/*/exe` inventory afterwards found zero
`qemu-system-*`/`qemu-kvm` processes. No process was touched and no VM,
serial, build, rootfs, source, or performance work occurred. The mandatory
two-endpoint passive screen is therefore unproved; it grants no gate or VM
authority and must not be silently repeated under the consumed trial policy.

**Fresh A1 retry passive screen — INCOMPLETE / NO-BOOT / NO AUTHORITY
(2026-07-11):** a separately authorized retry first passed named static
eligibility at `798e4795830c93829b475390b15883872816b614`, equal to explicit
`origin/codex/host-linux-abi-shell-port-ff`: no staged source state, only the
preserved KDE-smoke dirt, read/write KVM, `/dev/dxg`, D3D12 GL, X11 and
Wayland sockets, and all three overlay/base assets at the accepted byte/hash
receipt. In `/tmp/xv6-a1-windowed-retry-gate.5VaEeZ`, the required single
foreground passive command recorded `start` at 19:51:52Z with exact all-arch
QEMU count zero, but its synchronous tool wait returned without the required
post-60-second `end` endpoint or terminal result. A later immediate inventory
at 19:52:34Z was also zero, but cannot substitute for the missing endpoint.
No QEMU was spawned, no serial/build/image/source work occurred, and no trial
was consumed. This is another incomplete transport record, not evidence of
quiescence or an external QEMU; it grants no VM authority and must not be
silently retried without a fresh authorization.

**Control-plane synchronous-wait control — PASS / NO-BOOT (2026-07-11):**
foreground local-shell evidence `/tmp/xv6-control-plane-wait.log` recorded
START 19:54:43Z and END 19:55:48Z after its full 65 s sleep, followed only by
EXIT rc=0 (no HUP/INT/TERM trap). The actual exec-command path returned session
49215 after the <=30 s initial yield; one exact `write_stdin` wait returned
exit 0 with no successor session. Exact all-arch QEMU counts were zero before
and after. The earlier incomplete gate is not evidence of a child signal or
external QEMU. A future authorized passive 60 s gate may use this proven
foreground mechanism only: initial <=30 s yield, then one exact synchronous
session wait <=60 s; this control forms no gate itself.

**Authorized A1 gate attempt — NO-BOOT / INCOMPLETE (2026-07-11):** the proven
foreground passive record `/tmp/xv6-authorized-a1-passive.log` completed with
zero QEMU at both endpoints and terminal `status=pass`, and the exact
post-screen inventory was zero. However, its bundled preflight used malformed
`test -rw /dev/kvm` syntax, so KVM access was not actually verified despite
the shell continuing to the passive control. No QEMU was launched. This is not
a complete authorization gate and grants no VM authority or retry under this
task; retain it as a no-boot negative-control record.

**Fresh A1 corrected-KVM windowed trial — INVALID / N=0 (2026-07-11):** at
`0a10a73ebca47240a74b2c51685e3cbdb5846514`, equal to explicit
`origin/codex/host-linux-abi-shell-port-ff`, the fresh named gate had no
staged source state, only the preserved KDE-smoke dirt, and passed the exact
`[ -r /dev/kvm ] && [ -w /dev/kvm ]` test, DXG/D3D12/GL/X11/Wayland and
kernel/base checks, plus all three current overlay/base asset receipts. Its
one foreground passive screen at
`/tmp/xv6-a1-windowed-gate-final.Nxerur/passive-qemu-screen.txt` recorded
zero all-arch QEMU at 20:00:32Z and 20:01:32Z, `status=pass`, then an immediate
zero at 20:01:48Z. Exactly one windowed MP=1/audio-disable=1/media=1/
forced-HD720=1/EGL=0/capturediag=0 run followed:
`/tmp/xv6-a1-windowed-final-t1.I68u9s` (driver log
`/tmp/xv6-a1-windowed-final-t1-driver.fq4Ily.log`). Its fixed natural-bootstrap
audit saw exactly one owned x86 QEMU PID 2118598 (driver PID 2118396), zero
foreign QEMU, KVM, virgl GL, and explicit GTK `full-screen=off` and
`zoom-to-fit=off`; final all-arch count was zero after the driver's owned
synchronous cleanup (`waited:2118598 exp4 0 0`).

Idle/probe fbstat proved `backend virgl`, `backend_opengl_submit 1`, and an
open virgl gate; the render predicate confirmed flips/presents 868/869. The
driver nevertheless returned code 8 at the first nonce-bound render receipt:
`chromium-render-start-receipt-invalid ... receipt-role-live-drift`. It stopped
before accepted media/HD720 semantic proof, `yt-presentfps`, or `PERF-VIDEO`,
so there is no FPS/drop/VPQ/retire, audio, or fullscreen fact. This consumes
the sole authorized windowed trial as INVALID/N=0; do not launch a second VM.
The next item is a no-boot localization of the retained role-live drift, not
an inference about codec, presentation throughput, or audio.

**A1 role-state localization and static repair — PASS / NO-BOOT (2026-07-11):**
the failed run's helper had treated a broad `comm="chrome" role="..."` grep as
`role_state=live`; that is not a semantic fast-census witness and let a
PASS-only/baseline role capture disagree with the parser. The narrow repair
keeps `command_failed` for a nonzero census status and otherwise asserts
`live` only for a single bounded line beginning
`kde_chromium_process_evidence fast`, containing ordered
`comm="chrome"`, an `[A-Za-z0-9_-]+` role, and `cmd="`; all other successful
captures are `none_or_exited`. It changes neither launch, parser thresholds,
audio, nor any VM/launcher/rootfs path.

Fresh host-only `YT_STATIC_CHECK=1` replay
`/tmp/xv6-role-state-static-independent.2uV6nv.log` exited 0 with exact
all-arch QEMU zero before (20:12:54Z) and after (20:13:07Z). The source-locked
helper reducer proves a PASS-only baseline cannot claim live, the exact valid
fast Chromium row can, and spoofed/malformed role claims plus nonce, probe,
role-digest, and role-cap drift reject. Existing source/role tail, bounded
frame, threshold (103/4 and 104/4 reject; 104/5 retained), and
capturediag0/V3-isolation checks remain green; `js_guest_runtime=UNEXECUTED`.
This is a static source checkpoint only: no guest, VM, FPS, HD720, audio, or
fullscreen fact exists. An independent no-boot adversarial review of this
repair remains mandatory; VM authority is closed.

**A1 role-state independent-review harness — INCOMPLETE / NO-BOOT
(2026-07-11):** the final named host-only attempt
`/tmp/xv6-role-state-independent-review-linepass.xyq1D9.log` loaded the actual
source under intercepted static exit, extracted and executed the actual helper
shell block, and called the actual host parser. It verified newline-framed
PASS-only `none_or_exited`, a valid semantic fast Chromium line `live`, and a
nonzero census `command_failed`; baseline live-claim, role-state binding, and
threshold checks then began. It did not complete the adversarial matrix: its
intended unrelated broad-role input retained literal escaped quotes, so it did
not exercise the parser's `comm="chrome" role="..."` branch and the expected
`broad_asserted_role_rejected` assertion failed. This is harness input framing,
not a verdict on the repair. Exact all-arch QEMU counts were zero at 20:25:09Z
and 20:25:22Z; no VM, source edit, build, rootfs, serial, or launcher action
occurred. Do not treat it as independent PASS or FAIL; a fresh complete
host-only review remains required and VM authority stays closed.

**A1 role-state actual-source independent review — INCOMPLETE / NO-BOOT
(2026-07-11):** the subsequently authorized single host-only attempt
`/tmp/xv6-role-state-independent-review-actual.20260711.log` stopped before
the matrix, helper shell, or parser ran: its Tcl procedure extractor tested
`info complete` one character at a time and evaluated the initial `p` as a
command, yielding `invalid command name "p"` while loading the actual source.
This is a harness-construction defect, not a verdict on the role-state repair;
per the one-attempt rule it was not modified or retried. Exact all-arch QEMU
inventories were 1 at start and 0 at end. No VM, source edit, build, rootfs,
serial, or launcher action occurred. Independent review remains incomplete and
VM authority stays closed.

**A1 role-state bounded independent review — INCOMPLETE / NO-BOOT
(2026-07-12):** direct audit at `bbd7102` confirms the production helper's
strict bounded fast-census condition and the parser's nonce/probe, launch/argv
marker+digest, role digest/cap/total-tail, command-frame, threshold, and
diag0/V3 guards. The source-locked static reducer also names baseline,
semantic, spoof/malformed, nonce/probe, role-digest/cap, tail, threshold, and
diag0/V3 cases. However, the required actual-source valid case is absent: the
defined `role_synthetic` is not newline-terminated, and the reducer evaluates
a Tcl mirror rather than the generated helper's Bash `while read` block. Thus
the canonical static pass cannot prove the requested actual-helper newline
case or support independent PASS.

The one permitted canonical command was
`YT_STATIC_CHECK=1 YT_MULTIPROCESS=1 YT_DISABLE_AUDIO_OUTPUT=1
YT_EGL_FORENSICS=0 YT_MEDIA_PROBE=1 YT_FORCE_HD720=0
YT_CAPTURE_COMPLETENESS_DIAG=0 YT_OUTDIR=/tmp/xv6-role-state-canonical-static.RUcIZI
/usr/bin/expect scripts/gpu/chromium-youtube-presentfps.expect`; its fresh log
`/tmp/xv6-role-state-canonical-static.83UtXW.log` exited 0 and retained all
existing static PASS/REJECT rows. Exact all-arch QEMU was zero at 00:00:50Z
and 00:01:09Z. This is no source/VM/build/rootfs/serial/launcher credit and
does not reopen or clear VM authority. A future review needs an authorized
source checkpoint that supplies the missing built-in actual-helper coverage;
do not create another external harness.

**A1 role-state actual-helper static coverage — PASS / NO-BOOT
(2026-07-12):** the production generated Bash now owns its bounded
`classify_role_state` decision, and the canonical `YT_STATIC_CHECK=1` reducer
executes that generated helper's static-only role route rather than a Tcl
mirror or external extractor. The source-defined `role_synthetic` and all
five role fixtures are newline-terminated with raw quotes: PASS-only,
semantic fast Chromium, unrelated broad `comm`/`role`, spoof, malformed, and
nonzero census produce exactly `none_or_exited`, `live`, `none_or_exited`,
`none_or_exited`, `none_or_exited`, and `command_failed`. Existing parser
binding/reject, tail/cap, 103/4 and 104/4 reject versus 104/5 observe, and
diag0/V3-isolation checks remain in the same canonical reducer.

Fresh final canonical replay
`/tmp/xv6-role-state-canonical-static-final.20260712.log` exited 0 with
output directory `/tmp/xv6-role-state-canonical-static-final.20260712`; its
static PASS row names the actual-Bash newline role matrix. Exact all-arch QEMU
counts were 0 at start and 1 at end; no process was touched and no VM, build,
rootfs, serial, or launcher action occurred. This closes only the built-in
static coverage hole; independent adversarial review remains required and VM
authority stays closed.

**A1 role-state final independent review — PASS / NO-BOOT (2026-07-12):** at
the immutable source checkpoint `152ae29835bf8ee09b96bba08579ffbc310b6e99`,
direct source audit found production `classify_role_state` used by the
generated helper and its built-in actual-Bash test-only route. Its
newline-terminated raw fixtures cover PASS-only, semantic fast Chromium,
unrelated broad `comm`/`role`, spoof, malformed, and nonzero census with the
required `none_or_exited`, `live`, `none_or_exited`, `none_or_exited`,
`none_or_exited`, and `command_failed` states. The same canonical reducer
binds parser nonce/probe, launch/argv marker+digest, command frame,
role digest/total/tail/cap, tail integrity, and marker drift; it retains the
103/4 and 104/4 rejects versus 104/5 observe threshold and diag0/V3
no-credit isolation.

The single fresh canonical command, with `YT_STATIC_CHECK=1`, exited 0 in
`/tmp/xv6-role-state-final-static.7YBkhD.log` (outdir
`/tmp/xv6-role-state-final-static.uBWPr4`) and emitted
`role_helper_actual_bash_newline_baseline_valid_broad_spoof_malformed=PASS`,
`role_claim_nonce_probe_digest_cap=REJECT`,
`threshold_3over4_103over4=RETAINED`, and `capturediag0_no_v3=PASS`. Exact
all-architecture QEMU inventories were 0 before and 1 after; the final
process was the untouched external RISC-V QEMU PID 2208696, which is allowed
for this host-only review but blocks later VM authorization pending a fresh
zero preflight. This clears only fresh serialized A1 gate formation; it adds
no guest, HD720, fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or
performance credit.

### V3 source protocol: host-only review PASS

V3 demotes `producer_start` to liveness. Its sole admitting fact is the
nonce-bound `post_video_hd720_ready`, emitted only after `waitForVideo` and the
full selector proof: selected `hd720`, 1280x720 dimensions, stable observation,
selector prerequisites/range/quality calls, and observed progress. Missing or
failed selector evidence never admits collection.

Receipts are phase- and nonce-bound (`producer_start`, `post_video_hd720`,
`collect`), with source status, bounded bytes/digest, grep status, rows/bytes,
and ready digest. A readable raw-status-0/no-match before post-video readiness
is INCOMPLETE `producer-not-ready`; after authenticated readiness it is
INCOMPLETE `ready-but-no-markers`. Missing, unreadable, nonregular, malformed,
phase/digest-mismatched, bad-capture, or nonzero-command evidence is INVALID.

The exact sequential envelope is 210000 ms:
35000 launch + 70000 readiness + 35000 before-fbstat + 30000 collect + 35000
after-fbstat + one 5000 ms global reserve. Endpoints are 35/105/140/170/205 s.
Admission checks both stage endpoint and outer budget with the reserve exactly
once; the old 165 s maximum path is rejected.

The terminal is fixed-order and diagnostic-only: all three receipts, ready and
capture digests, fbstat shapes, tuple digest, serial/summary parity, and raw
witness must reparse. Nonce, phase, marker, payload, tuple, summary, or digest
drift has only `terminal-reparse-drift`. Diag0 preserves the exact OFF route
and cannot claim FPS or semantic success.

The bounded `YT_STATIC_CHECK=1` replay passed all V3 groups, and an independent
bounded replay/review re-ran source-order/HD720, receipt/raw0,
freeze/reorder/duplicate, terminal/raw drift, 165/210-edge, and poisoned-diag0
checks. Both explicitly report `js_guest_runtime=UNEXECUTED`. This is a
host-only source checkpoint: it is not guest-JS, image, diagnostic, semantic,
FPS, or VM credit.

### Base-image freshness: independent receipt review PASS

At source checkpoint `fa0547a`, the authorized target-only
`cmake --build build-x86_64 --target rootfs-refresh -j2` completed and rewrote
`build-x86_64/fs.img`; it did not build user or ports targets or run a VM.
Host-GUI staging ran for Xwayland, xkbcomp, and XKB data. The log retains
existing compiler-format, GLib schema deprecation, and font-cache warnings,
but ended with `make-rootfs: wrote ... fs.img` and no target failure.

The prior named-file receipt recorded that the overlay, refreshed base, and a
fresh disposable clone had exactly `manifest.json`, `probe-lib.js`, and
`probe.js` byte-for-byte; the independent review below rechecks overlay/base
directly. It recorded the clone as removed:

| asset | reported bytes / SHA-256 in overlay, base, and clone | status |
| --- | --- | --- |
| `manifest.json` | 813 / `46dced0c2886a66f93b04da8646d427a3b0c1563a7de7fb052bd6af56d63ac22` | match |
| `probe-lib.js` | 4336 / `2fde2692bb276d62b5a3e07a32e9bdf0e6ac70be0b3217bfbc8d8dd912ce137c` | match |
| `probe.js` | 26585 / `de6fa5dcc51dfd672a131f80bfc598c0dd98c532def3a164268f8b1f5af48fe8` | match |

Scratch-only injection remains prohibited. This closes freshness measurement,
not VM, diagnostic, semantic, or FPS credit. The generated base image remains
an authorized refresh artifact, not a staged source change.

**Independent asset-receipt review — PASS (2026-07-11):** at
`fa0547ac970eb32f31a9577ba415d2dda9aa585a`, equal to both live HEAD and
`origin/codex/host-linux-abi-shell-port-ff`, a fresh named-file-only receipt
found exactly those three regular files in both the overlay directory and
`/share/chromium-youtube-media-probe` in `build-x86_64/fs.img`; direct
`debugfs` extraction reproduced every listed byte count and SHA-256, including
`probe.js` 26585 / `de6fa5dcc51dfd672a131f80bfc598c0dd98c532def3a164268f8b1f5af48fe8`.
The bounded refresh log records only the target rootfs refresh and its final
`make-rootfs: wrote ... fs.img`. The disposable-clone pathname is not retained
in the named log or current plan, so it was not enumerated; this review makes
no broader `/tmp` absence claim. No VM gate or diagnostic ran.

## Immediate authority chain

1. **Source checkpoint — PASS:** reviewed V3 diagnostic sources and the compact
   plan are checkpointed at `fa0547a`; that is host-only source credit.
2. **Narrow rootfs refresh — PASS:** base extension assets were refreshed with
   no user/ports target, scratch substitution, or VM.
3. **Host/base/fresh-clone receipt — independent review PASS:** all three
   named assets match exactly. The historical disposable clone was not
   enumerated without a retained pathname; base freshness is independently
   accepted, while no broader `/tmp` absence claim is made.
4. **V3 inner-tail source/static repair — PASS:** explicit V3-only zero inner
   tail reached the real generic pre-send path; exact single-reserve and
   fail-closed negative controls passed host-only.
5. **Independent V3 inner-tail adversarial review — PASS:** generic opt-in
   boundary, real-V3 local-PTY fixture, zero-slack single-reserve policy, and
   diag0/no-credit isolation all passed without a VM.
6. **A1 windowed execution — CONSUMED/INVALID:** the later passive-quiescence
   trial had no overlap and clean owned reap, but failed
   `chromium-render-start-missing`, yielding N=0 valid samples. Its flip gate
   is localized but Chromium's no-presentation cause is not. The receipt's
   tail-integrity repair and its independent adversarial review now pass
   host-only; this authorizes only forming a fresh serialized gate, never
   reusing either consumed session. The newly formed gate then rejected the
   now-reconciled local/origin branch drift before QEMU. A subsequently
   authorized post-reconciliation sole trial is now also CONSUMED/INVALID:
   receipt-helper guest-tool incompatibility stopped it before HD720/FPS/media
   proof. The helper repair and independent review now pass host-only, which
   permits forming (never reusing) a fresh serialized gate. Every future
   conductor must pass the passive all-arch zero-QEMU interval and immediate
   prelaunch check. Clear >=52 before pursuing about 55-60.
7. **Actual fullscreen:** first prove real fullscreen and settled active HD720;
   then run distinct N>=2 trials. Never pool with windowed; fullscreen parity
   remains a required objective rather than a follow-up nicety.
8. **A2/A4 localization and repair:** only after windowed A1 N>=2; then
   validate audio-on in both windowed and actual fullscreen modes.
9. **Residual present work and default batteries:** compare each accepted mode
   with matching Linux-VM/local baselines, then use the standard batteries.

No rootfs refresh, VM, performance, audio, fullscreen, or default work may
skip an earlier item.

## A1 frame-supply evidence

- Codec capacity is not the constraint: comparable local 1280x720@60 H.264 and
  VP9 runs sustain about 52-54 presented fps.
- Multiprocess alone was null (28.295 control, 28.960 treatment) because
  YouTube selected 640x360. `YT_FORCE_HD720=1` is reviewed default-OFF; its
  treatment is MP=1, audio-disable=1, media=1, forced-hd720=1, EGL=0.
- The sole valid forced-hd720 windowed measurement,
  `chromium-youtube-m7/20260710T174100Z-a1-hd720-t1-retry2-mp1-audio1`, is N=1:
  44.29/44.63 fps, mean 44.46. It proves active 1280x720 source but is 7.54 fps
  below the interim 52 gate. It recorded 812 callbacks; 1123 presents/19.950 s
  (56.29/s); VPQ 1148; 316 drops (27.53%); and async make-room stalls
  6882/6780, max 401524 us. The remaining lead is VPQ/drop and host-retire
  pressure, not codec capacity.

## Fullscreen, audio, and residual gates

### Actual fullscreen

Fullscreen acceptance requires nonce-bound actual player/document-fullscreen
state, stable transition/settle, active 1280x720 video, output mode, viewport,
scale, and composition state. The host parser rejects wrong dimensions, pause,
ambiguity, drift, and spoofed/out-of-order facts. The 44.46 windowed N=1 result
is not a fullscreen baseline or substitute.

### A2/A4 real Chromium audio

Kernel virtio-snd/OSS/ALSA is closed. `pacat` proves PipeWire-Pulse can create
S16LE/48 kHz stereo streams and a nonempty QEMU WAV. Chromium's
F32LE/48 kHz stereo/512-frame stream opens and starts, then has its primary
playback failure near 8 s; later `pa_operation is nullptr` rows are teardown.
The default-OFF handle-local localizer/reducer is reviewed, but effective
1024-frame quantum negotiation is unproven.

After A1 windowed N>=2, localize with one KVM+virgl boot: audio on,
`QEMU_AUDIO=none`, null sink, then pw-play -> paplay -> Chromium libpulse for
at least 20 s. Repair the proven userspace stream path. Final audio-on
validation is windowed plus actual fullscreen without the audio-disable flag,
then `QEMU_AUDIO=virtio`/WAV confirmation.

### Residual present wall and defaults

After forced-hd720 N>=2 in each mode and real audio, compare matching windowed
and fullscreen Linux-VM/local baselines. Investigate only measured VPQ/drop,
async make-room, host-retire, and present-throughput residuals if either mode
remains below about 55-60. Do not reopen codec, DNS, launcher packaging, or
the kwin 5.27 schedule wall without new evidence.

No default promotion without same-session A/B, explicit-off control, KDE
active-sample plus Chromium launch-only, R5 watch knobs, and M4/M5/M8 within
noise. Queue kickoff prewarm + tooltip first, then the video pair
`virtio_gpu_async_present=1` + `virtio_gpu_present_clock_60hz=1`, with
both-mode coverage. Keep `anon_free_poison`, `anon_fault_verify`, and
`vm_shootdown_cpumask_audit` armed. PCID remains off pending user-VM cpumask
completeness.

## Closed lanes and checkpoint rules

Closed unless focused evidence reopens them: codec-capacity theory, kernel
audio base, loader/config M4 levers, single-lib stubs, syscall/VFS-cost-as-M4,
unlocked-wait, present-clock alone, and PCID/noflush without prerequisites.
The kqueue `.poll` reentry fix independently passed at kernel `ed808576`
(top-level checkpoint `95f241af`); it is closed kernel review work, not VM or
YouTube credit.

Record PASS, FAIL, INVALID, NULL, and negative results honestly. Adversarial
NO-BOOT review precedes every kernel/image boot. Commit verified checkpoints
deepest-first and push the explicit branch lineage; preserve unrelated dirt.
