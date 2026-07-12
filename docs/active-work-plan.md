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
- The conductor authorizes at most one x86 KVM+virgl performance VM worker.
  Exact all-architecture `/proc/*/exe` inventories remain required, without
  `pgrep`, but prospectively an independent, non-owned
  `qemu-system-riscv64` is informational only and is never touched. Before an
  x86 trial, require no active VM worker and no conflicting non-RISC-V QEMU;
  a performance gate's passive interval (at most 60 s, synchronous, no
  monitor loop) and immediate prelaunch check retain those inventories. During
  the trial there must be exactly one owned x86 performance QEMU and zero
  foreign x86 or other conflicting QEMU. Synchronously reap only the owned
  process group and retain a final inventory proving no owned or conflicting
  QEMU; an independent RISC-V process neither aborts nor satisfies a gate.
  Never edit QEMU/launcher scripts to enforce this. Guest RAM is not capped;
  concurrent x86 performance VMs are.
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

**Sole A1 windowed gate — NO-BOOT / INCOMPLETE (2026-07-12):** the fresh
authorization independently passed branch `77a7d997ac2b0c227a8b5cf5b79e86fc0009f3cc`
equal to explicit `origin/codex/host-linux-abi-shell-port-ff`, empty staging,
the preserved KDE-smoke dirt only, read/write KVM, DXG/D3D12/GL, X11/Wayland,
launcher, and all three overlay/base extension-asset byte/hash receipts in
`/tmp/xv6-a1-windowed-sole-gate.iizzOY/preflight.log`. Its one foreground
passive screen recorded only its zero-QEMU start at 00:16:35Z in
`/tmp/xv6-a1-windowed-sole-gate.iizzOY/passive-qemu-screen.txt`; the
synchronous command handle completed without the required 60-second end row
or `passive_status=PASS`. A final exact all-architecture inventory was zero.
No QEMU was launched or touched, no serial/build/rootfs/source/launcher action
occurred, and no trial was consumed. This incomplete screen is not evidence of
quiescence; do not retry it under this authorization. A fresh authority is
required before any VM launch, and windowed/fullscreen/FPS/audio credit remains
unchanged.

**A1 trial-only authorization — NO-BOOT / PRELAUNCH BLOCKED (2026-07-12):**
the conductor's replacement passive screen had zero at both retained endpoints
and supplied this worker a sole windowed trial authority. The worker's required
immediate independent check found HEAD
`5441ee413abe4b8f8bba6b2fccaaa6d7b676b9a5` equal to explicit
`origin/codex/host-linux-abi-shell-port-ff`, but exact all-architecture QEMU
count was 1. The process was not touched; no driver, serial command, QEMU, VM,
build, rootfs, source, or launcher action occurred. The only permitted trial
was never launched or consumed, and there are no role, HD720, yt-presentfps,
PERF-VIDEO, drop, VPQ, retire, audio, fullscreen, or performance facts. A
final exact all-architecture inventory was 2, also untouched. A fresh
zero-QEMU authorization is required; do not retry under this authority.

**Prospective QEMU coexistence policy (2026-07-12):** this branch now records
the full exact `/proc/*/exe` inventory while treating an independent,
non-owned `qemu-system-riscv64` as informational only: it is never inspected
beyond that inventory, signalled, waited on, or otherwise touched. A future
x86 KVM+virgl gate still requires no conflicting non-RISC-V QEMU before
launch, exactly one owned x86 performance QEMU during the trial, zero foreign
x86 or other conflicting QEMU, and owned synchronous cleanup. The prior
all-architecture-zero gate and abort verdicts above remain historical results
under the prior rule; this change neither reclassifies them nor supplies a
trial, VM, fullscreen, audio, or performance credit.

**A1 RISC-V-exempt sole windowed trial — INVALID / N=0 (2026-07-12):** under
the user-approved informational RISC-V exemption, the immediate worker check
at `24fcb6e8eb50cb7b32ef9569e6042991853739f8` equaled explicit origin and had
zero conflicting QEMU. The only real x86 trial,
`/tmp/xv6-a1-windowed-riscv-exempt-live-20260712T002809Z-2224502` (driver log
`/tmp/xv6-a1-windowed-riscv-exempt-live-20260712T002809Z-2224502.driver.log`),
used MP=1/audio-disable=1/media=1/forced-HD720=1/EGL=0/capturediag=0 and
proved KVM launch, 4 GiB guest memory, GTK `full-screen=off` and
`zoom-to-fit=off`, virgl `backend_opengl_submit=1`, and matching media assets.

It terminated `code=5`, `FAIL eof phase=render-start-receipt-probe1`, before
any repaired role receipt, active-HD720 semantic proof, media result,
`yt-presentfps`, or `PERF-VIDEO` window. Therefore FPS, drops, VPQ, retire,
audio, and fullscreen are all absent/N=0. The direct-spawn x86 PID/PGID/SID
2225003 was verified against this run's scratch-image command and was the only
owned group; after the driver control ended without its normal visible cleanup
receipt, that owned group alone was terminated. The retained `STATUS.txt` then
records `exact_qemu_after=none`, and the final exact inventory had zero
conflicting (and zero informational RISC-V) QEMU. No second VM was launched;
this trial is consumed INVALID and a new authority is required.

**A1 render-start receipt EOF forensic — INCOMPLETE / CONTROL-PROVENANCE GAP /
NO-BOOT (2026-07-12):** bounded reads of the named trial and its driver log
at source checkpoint `deb9669` fix the terminal chronology. Probe-1 fbstat
completed with command marker C5 `RC:0` plus `FENCE`, followed by a live
`root:/#` prompt. The driver then sent the nonce-bound receipt command with
launch marker C3, argv marker C4, and command marker C6; only a partial echo
was retained. There is no C6 `RC`/`FENCE`, receipt BEGIN/META/END, helper
stdout/stderr, or helper exit artifact. The same final buffer says
`qemu: terminating on signal 15 from pid 2225878 (/bin/bash)`, but retains no
PID/PGID provenance for that sender or monotonic ordering against cleanup.

`driver_rc=5` is therefore an EOF, not a helper verdict. The final debugcon
and run-log tails end at the probe fbstat/partial command and retain no panic
or reboot marker; this is not proof that a guest fault was impossible. The
staged helper's ordinary failures would return a framed nonzero status, but it
was not observed running. Cleanup proves no QEMU remained after the driver's
terminal path: at 00:30:02Z owned x86 leader 2225003 was already `Zs`,
`live=none`; its exact child was synchronously reaped at 00:30:03Z
(`waited:2225003 exp4 0 0`) and `exact_qemu_after=none`. It is consequently
incorrect to attribute the SIGTERM or a post-control lingering QEMU from this
record. A fresh exact inventory for this forensic found zero conflicting and
zero informational RISC-V QEMU; no process was touched.

The directly relevant source confirms the evidence defect: `guest_cmd` sends
C6 then its `eof` arm calls `finish 5` immediately; `finish` calls
`stop_qemu`, and `render_start_receipt_probe` never receives a result to write
its serial artifact. The smallest fail-closed repair is to retain a capped,
marker-bound EOF raw-buffer and pre-cleanup owned-group snapshot before that
terminal `finish`, while retaining code 5/no-credit and the existing owned
cleanup. Required no-boot static evidence is an actual EOF-after-partial-C6
fixture proving bounded artifact+marker retention, no receipt parse/FPS or
semantic admission, normal framed helper-nonzero behavior unchanged, and the
existing synchronous-reap/zero-conflicting-QEMU postcondition. Do not repair
or request another VM gate until that narrow change and independent review
pass.

**A1 render-start EOF control-evidence repair — STATIC PASS / NO-BOOT
(2026-07-12):** at source checkpoint `37d80e48d39ea96823d47254eb20088b110a52ed`,
the `guest_cmd` EOF arm now retains bounded `eof-control-<phase>-<marker>.txt`
before `finish 5` can enter `stop_qemu`, plus a separate pre-cleanup snapshot.
The raw record is marker/phase-bound and hex-encoded with `raw_bytes`,
`retained_bytes`, and explicit `truncated=0|1`; it keeps only the final 8192
bytes in production. The snapshot precedes cleanup and records timestamp,
source, owned leader, and owned-PGID rows with PID/PPID/PGID/SID/state/live
classification. This is forensic evidence only: it neither supplies a command
result nor relaxes receipt, threshold, sender-identity, semantic, FPS, or
no-credit rules.

Fresh canonical host-only replay
`/tmp/xv6-eof-control-static-final.awKlCM` exited 0 with
`YT-PRESENTFPS-STATIC-CHECK-PASS`. Its actual-writer matrix retained a 100-byte
partial C6 echo at a 128-byte test cap with phase/marker/snapshot exactness;
retained helper bytes containing `RC:0` but no `FENCE` without admitting a
frame; truthfully marked a 181-byte input as `retained_bytes=128,truncated=1`
and retained its exact tail; and kept a complete `RC:0`/`FENCE` frame valid.
It also proves source order is retain/snapshot before `finish`/`stop_qemu` and
that EOF remains code 5/evidence-only while the partial/helper forms cannot
enter credit. No VM, QEMU, build, rootfs, serial, launcher, media, audio,
fullscreen, semantic, or FPS action occurred. An independent no-boot
adversarial review remains mandatory before any VM gate.

**A1 render-start EOF control-evidence independent review — PASS / NO-BOOT
(2026-07-12):** immutable source
`53bebd90d52c4b9ad93f96e988d217ef0545568b` was equal to the reviewed helper.
Direct audit confirms the actual `guest_cmd` EOF arm retains the raw buffer
before `finish 5`; the writer binds sanitized phase and locally minted marker,
hex-encodes only the final 8192-byte cap with truthful
`raw_bytes`/`retained_bytes`/`truncated`, and takes the owned PID/PPID/PGID/SID
snapshot before `finish` can call `stop_qemu`. Code 5 selects evidence-only;
the normal complete RC/FENCE arm remains earlier and returns its normal command
result, so EOF evidence cannot become a receipt, semantic, FPS, or other
credit.

The single fresh canonical replay
`/tmp/xv6-eof-control-independent-static.CWjbq9.log` (outdir
`/tmp/xv6-eof-control-independent-static.Qlxpht`) exited 0. Its actual-writer
assertions retain partial C6 data and its snapshot, reject helper `RC:0`
without `FENCE`, retain the exact over-cap tail with `truncated=1`, and keep a
complete RC/FENCE frame valid; source-order assertions require retain/snapshot
before `finish`/`stop_qemu` and retain code-5/no-credit behavior. The same
replay retained the actual-Bash role matrix, the 103/4 and 104/4 rejects versus
104/5 threshold, `capturediag0_no_v3=PASS`, and V3 diag0-off parity. Exact
all-architecture QEMU inventories were zero before and after; no process was
touched. This clears only fresh serialized A1 gate formation, never a VM
authorization or guest/HD720/fullscreen/audio/`yt-presentfps`/`PERF-VIDEO`
credit.

**A1 EOF-repair sole windowed trial — CONSUMED / INVALID / N=0 (2026-07-12):**
the conductor's passive screen `/tmp/xv6-conductor-a1-eof-repair-passive-20260712.log`
had total/conflicting QEMU zero at 00:56:50Z and 00:57:50Z. Immediate worker
audit had HEAD=explicit origin `6b34ad9be19f5510458cc2eab452298f05b91785`,
zero total/conflicting QEMU, read/write KVM, and only preserved KDE-smoke dirt.
The one real windowed MP=1/audio-disable=1/media=1/forced-HD720=1/EGL=0/
capturediag=0 run is `/tmp/xv6-a1-eof-repair-windowed.ks2qKJ`; it used KVM +
WSL D3D12 virgl OpenGL-submit, 4 GiB guest RAM, and no fullscreen/audio-on arm.
Its owned leader 2249172 was found `Zs` then synchronously reaped
(`waited:2249172 exp4 0 0`); final exact total/conflicting/informational-RISC-V
was `0/0/0`. No second VM launched or foreign QEMU was touched.

C6 returned complete receipt BEGIN/META/SOURCE/ROLE/END plus `RC:0`/`FENCE`,
so no EOF-control artifact was required. Idle/probe fbstat proved virgl/
OpenGL-submit and advanced 3/4 to 520/521 flips/presents. The receipt is
`INCOMPLETE chromium-roles-none-or-exited`: fast census returned
`role_status=0,total=252,bytes=252`, but only baseline/summary lines—not the
required semantic `kde_chromium_process_evidence fast ... comm="chrome" ...
role=... cmd=...` row. Launch/argv and source-tail bindings cannot substitute
for dynamic role liveness. Driver code 8 correctly stopped before active
HD720/media semantic proof, `yt-presentfps`, `PERF-VIDEO`, FPS/drop/VPQ/retire,
audio, or fullscreen. This is N=0 and consumes authority. Next: narrow
NO-BOOT localization of the production fast-census semantic-row absence, then
static/adversarial review before a fresh gate; do not infer Chromium exit.

**A1 fast-census receipt-stream forensic — ROOT CAUSE LOCALIZED / NO-BOOT
(2026-07-12):** named retained evidence from that sole consumed run proves a
producer/output-routing defect, not a Chromium absence or a role-parser format
mismatch. C6 invoked `/bin/bash /ytrenderstartreceipt.sh` with the authenticated
nonce, probe, launch marker/digest, and argv marker/digest; its complete
BEGIN/META/SOURCE/ROLE/END plus `RC:0`/`FENCE` frame retained the exact 252-byte
role payload. Decoded, it contains only the probe's redirected baseline stderr
line and its `chromium_fast_census_only=1 ... status=PASS` stdout summary. The
receipt consequently and correctly classifies `role_status=0`,
`role_total=role_bytes=252`, untruncated, as `none_or_exited`; this rules out
the 4096-byte frame cap as the explanation. In the same named run,
`kde-chromium-process-evidence.log` contains the required contemporaneous
`fast-*` semantic rows (including `comm="chrome"` with browser, zygote,
utility, and gpu-process roles), so dynamic rows existed and `/proc` discovery
did not fail.

The production fast-census branch redirects stderr to stdout before dispatch,
but `run_chromium_fast_census_only()` opens the fixed evidence pathname
`/kde-chromium-process-evidence.log` with `O_APPEND` and writes the semantic
rows there; its status summary alone is printed on stdout. The receipt helper
captures only that stdout/stderr stream into `role_all` before framing it.
Thus `status=0` and only the two nonsemantic lines are the expected result of
the wrong producer stream; neither another command/mode, process exit, live
parser relaxation, nor a performance conclusion is justified. No HD720,
media, `yt-presentfps`, `PERF-VIDEO`, FPS/drop/VPQ/retire, audio, or fullscreen
credit exists.

The smallest fail-closed repair is confined to the generated receipt helper:
remove the known evidence pathname before invoking the existing fast census,
then require its newly created regular evidence artifact; preserve
stdout/stderr separately for diagnostics and frame that semantic artifact (with
the existing total/4 KiB cap/digest/tail binding), not the diagnostic stream.
Missing, unreadable, nonregular, or stale/unfresh
evidence must make the receipt command fail rather than synthesize a `none`
result; a genuinely fresh, valid empty census remains `none_or_exited`. Keep
the strict live-row grammar and all nonce/probe/launch/argv/cap/tail checks
unchanged. Before any gate, add canonical host static cases for: zero-status
semantic rows; fresh empty evidence; nonzero census; stale/missing/nonregular
evidence; cap/tail/digest binding; and parser rejection of spoofed/malformed
rows, then obtain an independent no-boot adversarial review. No source change
or VM authorization is granted by this localization.

**A1 fast-census evidence-file repair — STATIC PASS / NO-BOOT (2026-07-12):**
at source checkpoint `195952b8b1ddad210fea0f700e3f81fe710d8a0b`, the generated
receipt helper removes `/kde-chromium-process-evidence.log` before fast census
and, on census status 0, requires a newly created readable regular,
non-symlink evidence file. Census stdout/stderr is separate; only the evidence
file feeds the existing role total/4 KiB bytes/digest/tail fields. Nonzero
census remains `command_failed` and noncredit. Missing, stale, nonregular,
unreadable, stdout-only, or malformed semantic evidence cannot claim `live`;
nonce/probe/launch/argv/source bindings, strict role grammar, caps/tails,
thresholds, diag0/V3 isolation, and no-credit behavior are unchanged.

Fresh canonical host-only replay `/tmp/xv6-role-evidence-static.niS3dc` exited
0 and emitted
`role_helper_actual_bash_fresh_evidence_live_stdout_stale_missing_nonregular_unreadable_command_overcap=PASS`
plus `YT-PRESENTFPS-STATIC-CHECK-PASS`. The actual-Bash matrix accepts fresh
semantic evidence as live; keeps fresh empty/broad/spoof/malformed evidence
non-live; rejects stdout-only/no evidence; proves a stale file is purged rather
than admitted; fail-closes missing/nonregular/unreadable successful census;
retains `command_failed`; and proves truthful 4096-byte over-cap tail/digest.
Existing parser digest/tail-drift rejection and normal receipt cases remain
green. Exact QEMU inventories before/after were total/conflicting/
informational-RISC-V `0/0/0`; no VM, build, rootfs, serial, or launcher action
occurred. This is source/static-only; independent no-boot adversarial review
remains mandatory before any VM gate.

**A1 fast-census evidence-file repair — INDEPENDENT REVIEW PASS / NO-BOOT
(2026-07-12):** immutable `d3043c723b7fd7ab7812034125f3efc378cab6fb` equals
the explicit origin branch and changes only this receipt helper, its existing
canonical static route, and the plan; it does not alter QEMU, launcher, rootfs,
or VM code. Direct audit confirms that the helper removes only the known
`/kde-chromium-process-evidence.log` pathname, rejects any surviving
file/symlink before census, routes census stdout/stderr solely to
`$role_stdout`, and on status 0 requires a readable regular non-symlink before
copying *that evidence file* to the capped role frame. Existing strict
count/digest/total/tail handling then binds the frame. Missing, stale,
stdout-only, nonregular, or unreadable evidence returns failure before a
receipt; nonzero census keeps an empty frame and is still
`command_failed`/noncredit. The normal nonce/probe/launch/argv, digest/tail,
strict helper role grammar, parser, threshold, and diag0/V3/no-credit paths
were not relaxed. The only removal outside per-receipt `/tmp` frames is that
canonical evidence pathname itself; static cleanup removes only its fresh
`YT_OUTDIR` test file.

One fresh existing canonical command,
`YT_STATIC_CHECK=1 YT_MULTIPROCESS=1 YT_DISABLE_AUDIO_OUTPUT=1
YT_EGL_FORENSICS=0 YT_MEDIA_PROBE=1 YT_FORCE_HD720=0
YT_CAPTURE_COMPLETENESS_DIAG=0 YT_OUTDIR=/tmp/xv6-role-evidence-independent-static.2269929
/usr/bin/expect scripts/gpu/chromium-youtube-presentfps.expect`, exited 0
(log `/tmp/xv6-role-evidence-independent-static.2269929.log`). It emitted the
actual-Bash fresh-evidence live/stdout/stale/missing/nonregular/unreadable/
command/over-cap PASS matrix; retained parser nonce/probe/digest/cap and
false/true tail-drift rejection, semantic-role controls, 103/4 and 104/4
reject versus 104/5 retention, `capturediag0_no_v3=PASS`, V3 diag0-off parity,
and `js_guest_runtime=UNEXECUTED`. Exact `/proc/*/exe` inventories at start
and end were total/conflicting/informational-RISC-V `0/0/0`; no process was
touched. This PASS permits only formation of a fresh serialized A1 gate. It
does not authorize a VM or create HD720, fullscreen, audio, `yt-presentfps`,
`PERF-VIDEO`, or performance credit.

**A1 evidence-file-repair gate — WINDOWED INVALID, N=0 (2026-07-12):** the
sole authorized owned x86 KVM+virgl/OpenGL-submit trial,
`/tmp/xv6-a1-role-evidence-windowed.JWr2U5`, started at immutable
HEAD/origin `a7e85bcc461d79a47bbdaacf81a094101e9266ba` with MP1, audio-disable1,
media1, forced-HD7201, EGL0, capturediag0, and a 60-second window. The GTK
launch command has neither fullscreen nor zoom-to-fit enabled; this is a
windowed-only sample and creates no fullscreen credit. Idle fbstat admitted
the real `backend=virgl`, `backend_opengl_submit=1` path (3 flips/4 presents),
and Chromium's recorded argv contains the multiprocess/HD720 media arm and
`--disable-audio-output`. At probe1 the driver rejected its sole fbstat sample
as `missing-kms_page_flip_events` (`fbstat-probe1-attempt1.txt`), then stopped
with code 8 before the role receipt/canonical evidence, HD720 semantic proof,
media probe, or any `yt-presentfps`/`PERF-VIDEO` interval. Thus FPS, drops,
VPQ, retire, audio-on behavior, and fullscreen behavior are all absent—not
zero, and not comparable. The owner `2276159` was synchronously waited/reaped
(`waited:2276159 exp4 0 0`); final exact `/proc/*/exe` inventory was
total/conflicting/informational-RISC-V `0/0/0`. No second VM ran. Next is
no-boot forensic/static review of the probe1 fbstat `kms_page_flip_events`
parser/producer mismatch (including the accepted idle provenance) before any
new gate; no role, audio, presentation, default, or fullscreen conclusion is
opened by this invalid sample.

**A1 probe1 fbstat provenance-frame forensic — TRANSPORT INTEGRITY DEFECT
LOCALIZED / NO-BOOT (2026-07-12):** the named retained probe raw artifact
`fbstat-probe1-attempt1.txt` is complete (58,986 bytes, SHA-256
`73bb30ab91f903b96133626360c7749b8ac77e0278d2ebc171b8baa1fd829b2e`): it
has `YT_FBSTAT_BEGIN tag=probe1`, `YT_FBSTAT_END tag=probe1`, and the one
owned C5 `RC:0`/`FENCE`. Its outer command status is therefore not the cause.
There is no separate fbstat payload digest or small capture cap in this
protocol; `guest_cmd` retains the complete serial buffer under `match_max
2000000`, and the retained 59 KiB frame is far below that ceiling. The
same-run idle raw is likewise complete (58,497 bytes, SHA-256
`b8a3a89d28c0c3ff4a1204f4a9ef5446f72ad7e2ecd112aabaad27c979ce309e`) and
its clean line-34 current schema
`d3d12_native_completion_not_kms_matrix ... kms_page_flip_events=3 ...`
was accepted with `virgl_bo_presents=4`. Thus neither a renamed/conditionally
omitted producer field nor a parser overrequirement explains the rejection.

Probe line 34 starts that same provenance matrix but is split/interleaved by
the concurrent kernel `virtio_gpu: page-flip present ...` console diagnostic;
the required `kms_page_flip_events=<decimal>` bytes no longer form one exact
matrix token. Its continuation at line 35 starts `_software_blit=4 ...`.
The parser deliberately accepts the flip only from a line anchored at that
matrix and an exact token, so it correctly returns
`missing-kms_page_flip_events` rather than reconstructing a counter from
corrupted fragments. The producer is not absent: `FB_GPU_GET_STATS` snapshots
the stats under `fb_state.lock`, and the current fbstat producer's relevant
path prints that matrix with `stats.kms_vblank_page_flip_events`; the injected raw
bytes match the bounded `virtio_gpu_scanout.c` page-flip `printf`. This is
serial-output interleaving after the snapshot, not a trustworthy observed
flip value, truncation, status drift, or performance result.

The smallest fail-closed repair is a generated `/fbs.sh` transport change, not
a parser relaxation: write `fbstat` stdout/stderr to a fresh tag-bound regular
guest file, retain its inner exit status, then serialize a bounded encoded
payload with tag, byte count, and digest. Host code must bind that frame to C5,
verify all framing/count/hex/digest/cap invariants, decode it, and run the
unchanged current parser only on the verified payload. Any interleaving,
missing/duplicate/wrong tag, incomplete hex, cap/length/digest drift, nonregular
file, or nonzero inner command must stay INVALID/no-credit; never recover a
substring from raw serial. Required host-only static evidence: clean current
idle/probe schema; injected page-flip console interleaving; incomplete or
duplicate frame; tag/status/cap/length/digest/hex drift; and retained strict
backend/virgl-present/flip threshold plus diag0/V3 isolation. An independent
no-boot adversarial review must follow. No source change or VM authority is
granted by this localization. The named owner cleanup was already synchronous
(`waited:2276159 exp4 0 0`); this forensic's exact start/end inventories were
total/conflicting/informational-RISC-V `0/0/0`, with no process touched.

**A1 fbstat provenance-frame transport implementation — INCOMPLETE / NO-BOOT
(2026-07-12):** source work at plan checkpoint `af0740a` began the prescribed
fresh-file, lower-hex, digest-bound `/fbs.sh` transport with a 131072-byte
decoded cap (at most 262144 hex bytes, below the existing 2 MiB serial match
limit) and host C5/tag/nonce/status/count/cap/truncation/hex/digest checks
before the unchanged parser. The first canonical static invocation was a
harness preflight only: its redirected log was mistakenly inside the required
empty `YT_OUTDIR`, so it returned code 2 before static source execution; exact
QEMU inventories before/after were `0/0/0`. A fresh invocation with an empty
outdir and adjacent log then entered the source static route but stopped before
the new matrix on the introduced counter-name typo
`$fbstat_transport_digest` instead of initialized
`$fbstat_transport_digest_counter` (code 1, log
`/tmp/xv6-fbstat-transport-static-final.fIs0rv.log`); exact QEMU inventories
were again `0/0/0`.

The one authorized typo-only follow-up corrected that name and ran exactly one
fresh canonical replay (`/tmp/xv6-fbstat-transport-static-final2.Q4zFkO.log`),
which reached the verifier but failed before any matrix verdict with Tcl
`invalid command name "A-Z_"`: the new double-quoted metadata regexp let its
`[A-Z_]` class undergo Tcl command substitution. Exact QEMU inventories before
and after remained total/conflicting/informational-RISC-V `0/0/0`; no process
was touched, and no VM, build, rootfs, serial, or launcher action occurred.
Per the bounded follow-up rule, no further source edit or static replay was
made. The transport source is unverified, contributes no gate, role, HD720,
audio, fullscreen, `yt-presentfps`, `PERF-VIDEO`, or performance credit, and
requires a fresh no-boot authorization/review before any repair or VM gate.

**A1 fbstat provenance-frame transport final correction — INCOMPLETE /
NO-BOOT (2026-07-12):** the preceding stop was superseded by one explicit,
exactly scoped correction: the metadata regexp alone was changed to a braced
`format` literal so `[A-Z_]` could not command-substitute, with no semantic
change intended. Its one fresh empty-outdir canonical replay
`/tmp/xv6-fbstat-transport-static-final3.e4Z6CB.log` still failed code 1
before the transport matrix, now at `invalid command name "0-9a-f"`: the next
new double-quoted hex regexp retained the same Tcl character-class quoting
defect. Exact QEMU inventories immediately before and after were again
total/conflicting/informational-RISC-V `0/0/0`; no process was touched and no
VM, build, rootfs, serial, or launcher action occurred. No further source edit
or static replay is authorized from this checkpoint. The working transport
source remains unverified and uncommitted; it supplies no gate or performance
credit and must receive a fresh no-boot review/authorization before repair.

**A1 fbstat provenance-frame whole-diff audit — INCOMPLETE / NO-BOOT
(2026-07-12):** a fresh authorization audited every newly introduced transport
regexp, double-quoted Tcl string, and variable reference at `294cc92` before
testing. The three dynamic transport regexes (begin, end, and hex) are now
all `format` calls over braced literals; metadata was already in that form.
The digest path consistently initializes and increments
`fbstat_transport_digest_counter`; the remaining new bracket substitutions are
intentional Tcl calls with initialized operands, while the generated guest
helper stays braced. This did not change the transport cap, parser, or
adversary design.

The one fresh empty-outdir canonical replay
`/tmp/xv6-fbstat-transport-static-audit.jAgsHX.log` entered the existing
static route but stopped code 2 before the transport adversary matrix at
`static-check-fbstat-transport-helper-missing fragment=[ -f \"$capture\" ]`.
The static required-fragment control searched for backslashes before
`$capture`, whereas the correctly braced generated helper contains plain
quotes; no guest helper execution or transport-matrix verdict resulted. Exact
QEMU inventories before and after were total/conflicting/informational-RISC-V
`0/0/0`; no process was touched and no VM, build, rootfs, serial, or launcher
action occurred. Per this gate, no further source edit or replay is authorized;
the transport source remains unverified/uncommitted and grants no credit.

**A1 fbstat provenance-frame transport — STATIC PASS / NO-BOOT
(2026-07-12):** the bounded source-lock follow-up corrected only the static
helper expectation to its actual regular-file test
`[ ! -f "$capture" ]`; every required generated-helper fragment was then
checked against the helper procedure with the same `string first` semantics:
cap, `fbstat` redirect, regular/non-symlink checks, `wc`, `dd`, `xxd -p`,
`openssl dgst -sha256 -r`, and the transport metadata row all matched. The
guest helper remains a fresh regular `/dev/shm` capture followed by a bounded
retained copy; it emits one tag/nonce frame with command status, raw and
payload counts, cap, truthful truncation, lower-hex length/data, and digest.
The host binds the outer C5 command frame and tag/nonce, requires exactly one
contiguous four-row transport frame, verifies every count/cap/truncation/hex/
digest invariant, then gives only decoded verified bytes to the unchanged
fbstat parser. No serial fragment reconstruction is admitted.

Fresh canonical host-only replay
`/tmp/xv6-fbstat-transport-static-lock.WngyZV.log` exited 0 with
`YT-PRESENTFPS-STATIC-CHECK-PASS`; exact QEMU inventories before and after
were total/conflicting/informational-RISC-V `0/0/0`. Its transport matrix
accepts a clean 58,986-byte current-schema frame into the unchanged parser and
rejects page-flip console interleaving, duplicate/wrong-tag/wrong-nonce frames,
non-OK status, count/cap/length/digest/hex drift, nonregular-file status, and
a truthful over-cap truncation before parser credit. The 131072-byte decoded
cap yields at most 262144 lower-hex characters, safely below `match_max`
2,000,000. The same replay retained the actual-Bash role evidence matrix,
nonce/probe/digest/cap and tail-drift rejects, 103/4 and 104/4 threshold
rejects versus 104/5 retention, and `capturediag0_no_v3=PASS` with
`js_guest_runtime=UNEXECUTED`. This is source/static-only: no guest, HD720,
audio, fullscreen, `yt-presentfps`, `PERF-VIDEO`, or performance credit exists.
An independent no-boot adversarial review remains mandatory before any fresh VM
gate.

**A1 fbstat provenance-frame transport — INDEPENDENT REVIEW PASS / NO-BOOT
(2026-07-12):** immutable `6b0ba0c72dec20f0e40e28361046bcbe3cc2bbb9` equals
the explicit origin branch. Its production diff is confined to the generated
fbstat transport, its existing canonical static coverage, and the plan; it
does not modify QEMU, launcher, rootfs, kernel producer, role, threshold,
diagnostic, or no-credit policy. Direct audit of the exact braced
`fbstat_transport_guest_helper_script` body finds ordinary Bash quotes and
newlines—no literal escaped-quote construction. It uses only the current
guest shell plus `fbstat`, `/bin/wc`, `/bin/dd`, `/bin/xxd -p`,
`/bin/openssl dgst -sha256 -r`, and `rm`; it removes and then requires fresh
readable regular non-symlink `/dev/shm` capture/payload files. A nonzero inner
`fbstat` status is serialized as non-OK and rejected by the host; a clean C5
must bind the exact tag, run nonce, four contiguous rows, 131072-byte cap,
truthful count/truncation, lower-hex length/round-trip, and digest before the
unchanged parser receives decoded bytes. Missing, duplicate, interleaved,
wrong-tag/nonce, malformed/status/count/cap/length/digest/hex, nonregular, or
truncated frames remain invalid. The canonical static route source-locks that
exact generator but does not emit a host `/fbs.sh`, so no replacement extractor
or noncanonical `bash -n` harness was used.

The 131072-byte decoded limit yields at most 262144 lower-hex characters plus
the short four-row/C5 envelope, safely below `match_max 2000000`; it does not
relax the transport if ambient serial traffic makes a complete frame
unavailable. The old parser and its clean 58,986-byte current-schema case are
unchanged; `fb_sample` now calls it only after transport verification. One
fresh existing canonical replay,
`YT_STATIC_CHECK=1 YT_MULTIPROCESS=1 YT_DISABLE_AUDIO_OUTPUT=1
YT_EGL_FORENSICS=0 YT_MEDIA_PROBE=1 YT_FORCE_HD720=0
YT_CAPTURE_COMPLETENESS_DIAG=0
YT_OUTDIR=/tmp/xv6-fbstat-transport-independent-static.2308987
/usr/bin/expect scripts/gpu/chromium-youtube-presentfps.expect`, exited 0
(log `/tmp/xv6-fbstat-transport-independent-static.2308987.log`). Its
source-locked matrix covers the 58,986-byte clean frame and rejects console
interleave, duplicate, tag/nonce/status/count/cap/length/digest/hex/nonregular
and truthful-truncation adversaries; the same replay retains role evidence,
digest/tail drift rejection, 103/4 and 104/4 rejects versus 104/5 retention,
diag0/V3 isolation, and `js_guest_runtime=UNEXECUTED`. Exact `/proc/*/exe`
inventories before and after were total/conflicting/informational-RISC-V
`0/0/0`; no process was touched. This PASS permits only formation of a fresh
serialized A1 gate, never a VM authorization or any HD720, fullscreen, audio,
`yt-presentfps`, `PERF-VIDEO`, or performance credit.

**A1 fbstat-transport sole windowed trial — CONSUMED / INVALID / N=0
(2026-07-12):** with HEAD/origin
`73c139bb158db466c348325e1556b9516eeedba4` equal after the retained passive
screen, the one owned x86 KVM+virgl/OpenGL-submit windowed run
`/tmp/xv6-a1-fbstat-transport-windowed.xWxE8v` used MP1, audio-disable1,
media1, forced-HD7201, EGL0, capturediag0, GTK fullscreen/zoom-to-fit off,
and 4 GiB guest RAM. It naturally booted with owned leader `2314233`, zero
foreign conflict, and real virgl startup, but stopped code 8 at the **idle**
fbstat gate: `transport-frame-noncontiguous`. The raw frame declares
`status=OK`, raw/payload `56422/56422`, cap 131072, truncation 0, 112844 hex
bytes and digest, BEGIN/META/HEX/END, plus outer C1 `RC:0`/`FENCE`.

No fbstat/parser sample was accepted. The named raw bytes show every transport
row terminated `0d 0d 0a`; current host CR normalization turns that UART form
into blank rows, so strict contiguous-row admission correctly rejects rather
than splicing it. There is no role receipt, HD720/media semantic result,
`yt-presentfps`, `PERF-VIDEO`, FPS/drop/VPQ/retire, audio-on, or fullscreen
fact. The owner was synchronously reaped (`waited:2314233 exp4 0 0`) and final
exact QEMU total/conflicting/informational-RISC-V was `0/0/0`; no second VM or
foreign process was touched. Next: narrow NO-BOOT CRCRLF-normalization and
actual-wire static review before any fresh gate; no performance credit exists.

**A1 fbstat CRCRLF transport-wire forensic — ROOT CAUSE LOCALIZED / NO-BOOT
(2026-07-12):** named capture
`/tmp/xv6-a1-fbstat-transport-windowed.xWxE8v/fbstat-idle-attempt1.txt`
is 113815 bytes, SHA-256
`ee62fd7a3b4b09f31bb9b976ef4d55051b35930c191fb114be073f85f1985e90`.
Its BEGIN, META, HEX, and END rows each end `0d 0d 0a`; the generated guest
helper itself emits LF only. The installed Expect/Tcl evaluator confirms the
current sequential map `{\r\n -> \n, \r -> \n}` produces LF `0a`, CRLF
`0a`, but CRCRLF `0a0a`; it is therefore the sole cause of this capture's
four otherwise ordered rows acquiring blank logical rows and failing
`transport-frame-noncontiguous`. Independently extracting the named HEX row
gave exactly 112844 characters and decoded SHA-256
`8f40d235a58afdc5dbbbe67cd77416dc87698dba1a12236924be33a7fc44900a`,
which equals META; this localizes the observed rejection without treating the
frame as accepted evidence. A global strict-CR rewrite is wrong: unrelated
serial control noise includes `ESC[?2004l\r[`, so it would reject a valid
outer command stream.

The minimal future repair is record-local, not a global `string map`: split
raw serial only on LF and retain every raw record index; for a record starting
with a transport label, accept only terminal LF, CRLF, or CRCRLF (strip zero,
one, or two trailing CRs respectively) and reject any remaining/mid-row CR or
three-or-more trailing CRs as `transport-wire-cr`. Keep every intervening raw
record when proving BEGIN/META/HEX/END adjacency, and count all candidate
transport records, so a blank/interleave still breaks contiguity and a
duplicate still exceeds four; unrelated non-transport serial noise remains
byte-preserved. Required actual-wire static matrix: LF, CRLF, and CRCRLF for
all transport and outer C1 rows must decode to the identical payload/digest;
CRLFCRLF between protocol rows must remain an empty record and reject
noncontiguous; a lone/mid-row CR (and a three-CR suffix) must reject
`transport-wire-cr`; injected blank/interleave and duplicate frames must keep
their current noncontiguous/count rejection; tag, nonce, count, cap, length,
hex, digest, and truncation negatives must remain strict. This is forensic
specification only: no source/static replay/VM authorization, no performance,
HD720, audio, fullscreen, `yt-presentfps`, or `PERF-VIDEO` credit. Exact
QEMU inventories before/after this review were
total/conflicting/informational-RISC-V `0/0/0`; no process was touched.

**A1 candidate-local CRCRLF transport normalization — STATIC PASS / NO-BOOT
(2026-07-12):** at plan checkpoint `5e41015`, the host scanner now splits the
raw serial buffer only on LF, retains each raw-record index, and considers
only exact `YT_FBSTAT_TRANSPORT_{BEGIN,META,HEX,END}` prefixes.  It removes a
trailing CR run only from such a candidate before the existing exact parser;
noncandidate bytes (including lone-CR prompt/control noise) are not mutated,
and a residual/mid-row candidate CR is INVALID `transport-wire-cr`.  The
contiguous-four-row and all existing tag/nonce/status/count/cap/length/digest/
hex/truncation rules are unchanged, so blank/interleaved records still reject
on raw-record adjacency and duplicates still reject on count.  This supersedes
the preceding forensic's provisional two-CR suffix wording: the implemented
candidate terminator rule deliberately accepts a complete trailing CR run,
while never normalizing unrelated serial records.

One fresh canonical static replay exited 0 (log
`/tmp/xv6-fbstat-crcrlf-static.iiINjA.log`) with
`YT_STATIC_CHECK=1 YT_MULTIPROCESS=1 YT_DISABLE_AUDIO_OUTPUT=1
YT_EGL_FORENSICS=0 YT_MEDIA_PROBE=1 YT_FORCE_HD720=0
YT_CAPTURE_COMPLETENESS_DIAG=0`.  Its actual-wire matrix admits equivalent
LF, CRLF, and CRCRLF four-row/C1 transport frames; rejects CRLFCRLF blank-row
insertion as `transport-frame-noncontiguous` and a candidate mid-row CR as
`transport-wire-cr`; and proves lone noncandidate CR noise does not mutate a
clean frame.  The retained interleave, duplicate, and all prior strict
transport negatives stayed green.  Exact QEMU inventories immediately before
and after were total/conflicting/informational-RISC-V `0/0/0`; no VM, build,
rootfs, serial, launcher, or process was touched.  This is no performance,
HD720, audio, fullscreen, `yt-presentfps`, or `PERF-VIDEO` credit.  An
independent no-boot adversarial review is mandatory before any VM gate.

**A1 candidate-local CRCRLF transport normalization — INDEPENDENT REVIEW PASS /
NO-BOOT (2026-07-12):** immutable
`1328b356a1cf11270758ba942e0e0c6e208e2624` equals current HEAD and the
explicit origin branch. Its only production change is the fbstat transport
decoder plus its canonical static cases (the companion plan change grants no
runtime behavior): raw serial is split only on LF, every raw-record index is
retained, and only a record beginning with one of the four transport prefixes
has its trailing CR run removed. A remaining candidate CR fails
`transport-wire-cr`; noncandidate bytes, including `ESC[?2004l\r[`, are not
rewritten. The unchanged exact-four count and raw-record adjacency retain
blank/interleave rejection, while duplicates and all tag/nonce/status/count/
cap/length/digest/hex/truncation checks remain fail-closed. The decoded fbstat
parser, receipt/role logic, thresholds, diagnostic route, and no-credit policy
are outside the source diff.

The one fresh canonical replay, outdir
`/tmp/xv6-fbstat-crcrlf-independent-static.zHkDka`, log
`/tmp/xv6-fbstat-crcrlf-independent-static.zHkDka.log`, synchronously exited
0 and reached `YT-PRESENTFPS-STATIC-CHECK-PASS`. Its fail-preflight
actual-wire matrix therefore accepted identical LF/CRLF/CRCRLF payloads,
preserved CRLFCRLF as a noncontiguous blank, rejected mid-row candidate CR,
and tolerated unrelated lone-CR noise; retained interleave/duplicate and all
prior strict negatives stayed invalid. The same replay retained actual-Bash
role evidence, receipt tail/digest rejection, 103/4 and 104/4 rejects versus
104/5 retention, `capturediag0_no_v3=PASS`, V3 diag0-off parity, and
`js_guest_runtime=UNEXECUTED`. Exact QEMU inventories before/after were
total/conflicting/informational-RISC-V `0/0/0`; no process was touched. This
PASS clears only formation of a fresh serialized A1 gate—never a VM launch or
HD720, fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or performance
credit.

**A1 CRCRLF-repaired sole windowed trial — CONSUMED / INVALID / N=0
(2026-07-12):** after the conductor's passive screen, immutable HEAD/origin
`e26745c2f4d53263f95d1545cedef047bfe50330`, and zero conflicting non-RISC-V
QEMU, one owned x86 KVM+virgl/OpenGL-submit run used MP1, audio-disable1,
media1, forced-HD7201, EGL0, capturediag0, 4 GiB RAM, and the normal 60-second
window. GTK fullscreen and zoom-to-fit were explicitly off. It is retained at
`/tmp/xv6-a1-crcrlf-windowed.qI1K17` (wrapper log
`/tmp/xv6-a1-crcrlf-windowed.qI1K17.log`); the launcher recorded KVM and WSL
D3D12 host GL selection, so software fallback is not a result explanation.

The new actual-wire transport path admitted idle and each of five probes
(`transport=verified`, `backend=virgl-opengl`): idle flips/presents `3/4`, then
`8/9`, `86/87`, `87/88`, `88/89`, and `89/90`. Thus the previous CRCRLF
noncontiguous frame defect did not recur. Each nonce-bound render-start receipt
was fresh and regular with role command status 0 and `role_state=live`, but
classified `INCOMPLETE:live-roles-insufficient-flips`; all five attempts made
that same fail-closed result, and the driver exited code 8
`chromium-render-start-missing`. This stops before HD720 source/1280x720
semantic proof, media capture, or the `yt-presentfps`/`PERF-VIDEO` windows.
There are consequently no FPS, drops, VPQ, retire, audio-on, or fullscreen
facts (absent, not zero); only the pre-frame artifact exists and all steady/
window/media screenshots are absent.

The driver synchronously reaped owned leader `2336525`
(`waited:2336525 exp4 0 0`). Exact final `/proc/*/exe` inventory had x86 and
conflicting non-RISC-V counts `0/0`; the exempt informational RISC-V VM was
present (it was neither controlled nor affected). No second x86 VM ran. This
consumes the sole trial authority. Next is no-boot forensics of the fresh
render-start role/flip admission mismatch before any new gate; no audio,
presentation, default, or fullscreen conclusion is opened.

**A1 `live-roles-insufficient-flips` plateau forensic — REAL GATE FAILURE /
CAUSE UNRESOLVED / NO-BOOT (2026-07-12):** immutable run checkpoint
`f00621e1ff32b0edbddec4dd4aa99f211584f9fe` equals explicit origin. All five
fbstat payloads were transport-verified and timestamped at 02:43:09,
02:44:31, 02:45:37, 02:46:42, 02:47:46, and 02:48:50Z with cumulative
flips/presents `3/4, 8/9, 86/87, 87/88, 88/89, 89/90`; each fresh C6/C8/C10/
C12/C14 receipt had exact C3/C4 binding, `RC:0`/`FENCE`, regular source,
status-0 live role evidence, and truthful tails. This excludes the old
CRCRLF transport defect, receipt timing, or a lost sampled burst as the gate
explanation: after the 78-flip early burst, the accepted cumulative counter
advanced only three times over about 193 seconds. It proves only a real lack
of sustained *measured* page-flip supply, not HD720, video playback, FPS, or a
specific app/kernel cause.

Semantic comparison is noncausal but useful. Current fast census retained
browser, GPU, and `network.mojom.NetworkService` through all six phases, but
had no AudioService (both 520- and 868-flip named controls had one); none of
the three runs emitted a `renderer` role label, so that absence does not
differentiate the plateau. Current source uniquely records two children
terminating after 15 s without a connection and a network-service restart
before probe1, then stops advancing after 02:47:23Z; the controls instead show
ongoing GPU/page-paint activity and their large first-probe bursts. This is
correlation only: the bound role frame is its first 4096 bytes of a
65--70 KiB census and `live` requires merely one semantic Chrome row, while no
nonce-bound player/navigation/readiness or process-exit identity exists. Do
not lower the flip threshold or repair audio/network from this evidence.

The next bounded diagnostic is a no-credit pre-threshold, nonce/probe-bound
receipt that retains a fixed-order, capped/digested role/subtype vector
(browser, GPU, renderer, network, audio, utility, zygote) plus minimal
watch-page/video readiness and short progress state; missing, malformed,
reordered, stale, truncated, or contradictory facts must be INVALID or
INCOMPLETE and must never start media/FPS admission. Static cases must retain
the current threshold and prove no renderer/network/audio distinction can
forge presentation. Exact QEMU inventories before/after this forensic were
total/conflicting/informational-RISC-V `0/0/0`; no process, VM, source, build,
rootfs, serial, or launcher was touched.

**A1 pre-threshold plateau diagnostic — INCOMPLETE / NO-BOOT (2026-07-12):**
at checkpoint `349b35c4f7da3cd3fca5d9020024f61dedeb8216`, the bounded
host/receipt-only implementation preserved the verified fbstat command marker
(C5), added an actual-Bash full-census browser/GPU/renderer/network/audio/
utility/zygote summary, and derived navigation/player/progress diagnostic
states from existing nonce-bound launcher-tail media rows. It neither modified
extension/rootfs/launcher defaults nor changed the role predicate, the
`flips > idle+100 && presents > idle` threshold, diag0/V3 isolation, or
credit route.

Its one fresh canonical static replay,
`/tmp/xv6-a1-plateau-diagnostic-static.z3q7zq.log`, exited 2. The C5 drift
adversary rejected, and the actual-Bash subtype (including AudioService
present/absent, reorder/spoof), source navigation/player/progress
missing/stalled/advancing, cursor/nonce/probe binding, and no-credit latch
matrices all reached their expected states. The required full-census digest
drift adversary instead parsed `pass`: a syntactically valid substituted
`role_full_digest` cannot be recomputed by the host because the existing
receipt retains only the first 4096 role bytes. Therefore that new binding is
not proven fail-closed. Per the one-run rule, no source correction or replay
was made; the source remains unstaged/uncommitted and grants no diagnostic,
role, HD720, media, FPS, audio, fullscreen, or VM authority. Exact QEMU
inventories immediately before and after were total/x86/informational-RISC-V/
conflicting `0/0/0/0`; no process was touched. A fresh no-boot authorization
must redesign and review the full-summary integrity boundary before any VM
gate.

**A1 plateau full-census trust-boundary design — VERDICT / NO-BOOT
(2026-07-12):** immutable `cf637579ca57e8e9da7630f3fec4b55b502084d6`
equals explicit origin; the failed driver and unrelated KDE-smoke changes
remain unstaged and untouched. Direct audit confirms the failure: the helper
calculates `role_full_digest` and subtype counts from the full evidence file,
but transmits only `role_bytes` (at most 4096) and the host recomputes only
the prefix `role_digest`; it then checks a full-digest string's syntax and
vector arithmetic, not their source bytes. The named static full-digest-zero
mutant consequently parsed `pass`. This is an integrity defect, not a reason
to relax the role/flip gate or infer any plateau cause.

The 4096-byte cap is **not** sufficient for a full census: named evidence logs
are 69617, 81929, and 81929 bytes, and the five current receipts declare
role-total 65663--69749 with role-bytes 4096 and truthful truncation. Keep
that cap; no larger UART/receipt limit is authorized by these facts. A full
digest or subtype vector may exist only when `role_total == role_bytes <= 4096`
and `role_tail_truncated=0`: host decodes the exact ROLE_HEX, recomputes its
digest, and derives browser/GPU/renderer/utility/zygote/other counts plus
network/audio-as-utility subtypes itself. Prefer no guest `role_full_digest`
or vector fields; if retained, exact host-derived equality is mandatory. On
any overflow, the new diagnostic must say
`INCOMPLETE role-summary-over-cap`/no-credit with full digest and every
present/absent subtype claim unavailable—not zero. The existing bounded
liveness receipt is not promoted by that diagnostic and the threshold remains
unchanged.

Required next static matrix: (1) a complete <=4096 valid control whose digest
and host-derived vector match exact bytes; (2) forged full digest and forged
vector/extra-vector metadata rejection; (3) bytes>total, false/true tail, and
total/bytes drift rejection; (4) a truthful over-cap prefix that is explicitly
diagnostic-INCOMPLETE and rejects any full digest/vector claim; and (5) exact
complete-vector semantic mismatches (primary-role sum versus semantic rows,
network/audio exceeding utility, or subtype absent/present mismatch) rejected.
No replay, source edit, VM, build, rootfs, serial, or launcher action occurred.
Exact QEMU inventories before/after were total/conflicting/informational-RISC-V
`0/0/0`.

**A1 separate full-census diagnostic transport design — SOUND AS C7 ONLY /
NO-BOOT (2026-07-12):** direct no-boot audit at immutable `038e711` finds
the proven fbstat lower-hex transport already has a distinct 131072-byte
decoded cap, four contiguous candidate records, exact tag/nonce/count/tail/
hex/digest checks, candidate-local CR handling, and host-side recomputation.
It is suitable for a **separate no-credit census sidecar**, not an increase to
the 4096-byte admitting role receipt. It is *not* sound to append this frame
to current C6: the unchanged C6 receipt parser rejects its whole serial text
above 30000 bytes, while named full censuses are 65--82 KiB (up to 164 KiB
lower-hex). Keep the existing C6 receipt and role-live/threshold decision
unchanged; a subsequent C7 command is the only viable placement. Its worst
case 262144 hex bytes plus the short envelope is below `match_max 2000000`,
but it must receive its own reviewed output-time budget--C6's 20 s budget is
not evidence for the capped C7 wire delivery.

The C7 helper may use only the already verified guest Bash, fast census,
`/bin/wc`, `/bin/dd`, `/bin/xxd -p`, `/bin/openssl dgst -sha256 -r`, and `rm`.
It must make a fresh regular non-symlink census capture and emit exactly the
unique four-row `YT_ROLE_CENSUS_DIAG_{BEGIN,META,HEX,END}` frame, bound to
run nonce, probe, C3 launch marker+digest, C4 argv marker+digest, C5 fbstat
marker, and the locally minted C7 command frame. META must contain no guest
full digest or vector: only status, raw_bytes, payload_bytes, cap=131072,
truncated, **payload** digest, and hex_bytes. Host first validates C7 and the
four raw-record-adjacent candidate rows with the same candidate-local CR rule,
then checks exact metadata/no duplicates, bindings, count equation
`payload=min(raw,cap)`, truthful truncation, lower-hex round-trip, and digest.
Only `raw==payload<=131072,truncated=0,status=OK` is complete: host derives
the full-evidence digest and every semantic browser/GPU/renderer/utility/
zygote/other and network/audio-as-utility count from those exact bytes. Any
overflow, missing/nonregular/failed census, missing/duplicate/corrupt frame,
or binding/count/hex/digest failure is `diagnostic-INCOMPLETE`, with no full
digest or subtype present/absent claim; it cannot alter C6 role live, flips,
threshold, HD720, media, FPS, or later admission.

Required static matrix before implementation/review: complete valid control;
C7 outer-RC/FENCE and duplicate/tag/nonce/probe/C3/C4/C5 drift; forged
full-digest/vector or extra META claim rejection; raw/payload/cap/tail/hex
length/decode/digest drift; truthful over-cap and missing/nonregular/corrupt
frames classified only INCOMPLETE/no-claim; byte-derived semantic/subtype
controls; and LF/CRLF/CRCRLF, blank/interleave, mid-row-CR, and duplicate
candidate adversaries. No source, VM, build, rootfs, serial, or launcher
action occurred; exact QEMU inventories before/after were `0/0/0`.

**A1 C7 full-census diagnostic static implementation — INCOMPLETE / NO-BOOT
(2026-07-12):** the one fresh canonical `YT_STATIC_CHECK=1` replay (outdir
`/tmp/xv6-a1-c7-static.7M3dQp`, log
`/tmp/xv6-a1-c7-static.7M3dQp.log`) reached the existing static terminal but
failed `static-check-render-start-receipt`: C7 tuple
`complete,wire,rejects,source,no-credit = 0,0,1,1,1`. Thus the complete
65--82 KiB control and LF/CRLF/CRCRLF controls are not proved; all C7
rejection, source-contract, and no-credit controls did hold, as did the
unchanged C6 baseline gates. Likely defect is C7 META binding's local-name
lookup (`expected_nonce`/`expected_probe` versus field-name variables); this
is a localization, not a verified repair. Per the one-run rule no correction,
rerun, VM, rootfs, build, serial, launcher, or default action followed; the
driver patch remains unstaged. Exact pre-run inventory saw only one unrelated
informational RISC-V QEMU (PID 2370035); exact post-run inventory was zero.
This grants no full-census, role/subtype, HD720, media, FPS, audio, fullscreen,
or VM authority.

**A1 C7 META-binding correction — INCOMPLETE / NO-BOOT (2026-07-12):** the
authorized narrow correction replaced the C7 parser's dynamic field-name local
lookup with one explicit expected-value mapping for tag, nonce, probe, both
C3/C4 marker+digest pairs, C5, and C7; the full binding block was audited for
the same class. Its one fresh canonical replay (outdir
`/tmp/xv6-a1-c7-binding-static.6Rk2Vm`, log
`/tmp/xv6-a1-c7-binding-static.6Rk2Vm.log`) still failed
`static-check-render-start-receipt` with the unchanged tuple
`complete,wire,rejects,source,no-credit = 0,0,1,1,1`. The previous
META-local-name diagnosis is therefore disproved/incomplete. C6 and all
existing static groups remained green. Per the one-run rule no further source
edit or replay followed; exact all-architecture QEMU inventories immediately
before and after were both zero. This remains diagnostic-only/no-authority and
grants no role/subtype, HD720, media, FPS, audio, fullscreen, or VM credit.

**A1 C7 clean-control tokenizer forensic — ROOT CAUSE LOCALIZED / NO-BOOT
(2026-07-12):** immutable `301f0839f6dce2de2dcdc9fe6d4df893c3bc2973`
equals the explicit origin; both named 105-line/13658-byte static logs retain
the identical `c7=0,0,1,1,1` terminal tuple. Direct parser/helper/fixture
audit pins the first failure before any binding, count, digest, hex, raw-row
adjacency, or vector check: every generated C7 META row correctly includes
the required field `c7_marker=...`, while its tokenizer accepts field names
only under `^([a-z_]+)=([^ ]+)$`. The digit `7` violates that grammar, so the
clean 70 KiB control returns `census-meta-parse-drift`; the LF, CRLF, and
CRCRLF copies call that same tokenizer after their otherwise accepted command
frame/candidate-CR handling and fail identically. The authorized expected-value
mapping correction did not, and could not, affect this earlier failure.

Thus the two false controls share exactly that one schema/tokenizer defect.
`rejects=1` is not positive transport evidence: each adversary also fails at
the same early META parse, while source-contract and no-credit are source
predicates (`1,1`). C3/C4/C5/C7 value formats, tag/nonce/probe, cap/count/tail,
digest/hex, row adjacency, and semantic rows are untested by a clean parse;
none is implicated or cleared. The sole smallest correction is to admit the
already required lowercase digit-bearing key, e.g. replace only the META key
grammar with `^([a-z][a-z0-9_]*)=([^ ]+)$`; do not change frame fields or the
4096-byte C6 contract. A new authorized static run must then prove the clean
complete and all three wire controls reach host digest/vector success, and
that every negative rejects for its intended invariant rather than a generic
META-token failure, followed by independent review. No test was rerun and no
source, VM, QEMU, build, rootfs, serial, or launcher action occurred; exact
QEMU inventories before/after were `0/0/0`.

**A1 C7 META-tokenizer correction and static matrix — PASS / NO-BOOT
(2026-07-12):** the narrow parser correction now admits only lowercase-leading
META keys under `^[a-z][a-z0-9_]*$`, allowing required `c7_marker` while
rejecting leading-digit, uppercase, and punctuation keys. The one fresh
canonical replay (outdir `/tmp/xv6-a1-c7-token-static.F4q8Lc`, log
`/tmp/xv6-a1-c7-token-static.F4q8Lc.log`) exited 0 and reports the C7 PASS
row. Its 70,000-byte full-census control (within the named 65--82 KiB range)
and clean LF/CRLF/CRCRLF frames reach host decode/rehash/vector success.
Each negative now asserts its own invariant: outer RC/FENCE; candidate
duplicate/interleave/mid-row-CR; tag/nonce/probe/C3/C4/C5/C7 META drift;
extra full-digest/vector claims; invalid key grammar; raw/payload/cap/tail/
length/digest/hex/status; and truthful over-cap. A malformed semantic row
cannot forge audio/network/utility claims because host derivation counts only
the exact evidence grammar. The unchanged C6 receipt, 4096-byte cap, role and
flip thresholds, diag0/V3 isolation, no-credit latch, and all existing static
groups are green. Exact all-architecture QEMU inventories immediately before
and after were both zero. This is host/static diagnostic-only credit: no VM,
HD720, media, FPS, audio, fullscreen, or default authority is granted; an
independent C7 adversarial review remains required before another gate.

**A1 C7 full-census sidecar independent review — PASS / NO-BOOT
(2026-07-12):** immutable `ba775231e34b4973fb89cb41984eb418a4d1967d`
equals explicit origin and preserves the sole unrelated KDE-smoke dirt. Direct
audit confirms C6 remains the capped 4096-byte admitting receipt and its
role/flip classifier has no C7 input. C7 is a distinct 45 s outer command,
bound to nonce/probe, C3/C4 marker+digest, C5, and its locally reserved C7
marker. Its 131072-byte decoded cap yields at most 262144 lower-hex bytes plus
the short envelope, below `match_max 2000000`. Candidate-local CR handling and
raw-record adjacency precede complete untruncated host decode/rehash; only
host-derived exact-evidence bytes supply subtype counts. No guest vector or
full-digest field is accepted. Overflow, non-OK/missing, or malformed/corrupt
transport maps to diagnostic-INCOMPLETE with `no_subtype_claim=1` and cannot
alter C6 liveness, threshold, HD720/media, FPS, or any admission.

The key grammar accepts lowercase-leading digit-after `c7_marker` and rejects
leading-digit, uppercase, and punctuation keys. The actual static matrix
requires each negative's own detail: outer RC/FENCE, duplicate,
interleave/mid-row-CR, META extras/key grammar, tag/nonce/probe/C3/C4/C5/C7
drift, count/cap/tail/length/hex/digest/status/over-cap, and semantic spoof.
Fresh canonical host-only replay, outdir
`/tmp/xv6-a1-c7-independent-static.20260712`, external log
`/tmp/xv6-a1-c7-independent-static.20260712.log`, exited 0 with C7, existing
C6/role/threshold, diag0/V3, and no-credit groups green
(`YT-PRESENTFPS-STATIC-CHECK-PASS`, `js_guest_runtime=UNEXECUTED`). Exact
QEMU inventories before and after were total/informational-RISC-V/conflicting
`0/0/0`. No VM, source, build, rootfs, serial, or launcher action occurred.
This PASS clears only formation of a fresh serialized gate; it is no VM,
HD720, fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or performance credit.

**A1 C7-authorized windowed sole trial — CONSUMED / INVALID / N=0
(2026-07-12):** conductor passive screen
`/tmp/xv6-conductor-a1-c7-passive-20260712.log` passed at 14:07:55/14:08:55;
the immediate one-run prelaunch check found immutable
`6b4b9d285ca2e50224952cccf58e67c060552a67` equal to explicit origin, KVM
read/write, zero total/conflicting QEMU, and only unrelated KDE-smoke dirt.
The one real KVM+virgl/OpenGL-submit **windowed** treatment was
`/tmp/xv6-a1-c7-windowed-20260712T1410Z` (external log
`/tmp/xv6-a1-c7-windowed-20260712T1410Z.log`): MP=1, audio-disable=1,
media=1, forced-HD720=1, EGL=0, capturediag=0, GTK `full-screen=off` and
`zoom-to-fit=off`. The direct expect owner PID 2597026 owned x86 QEMU PID
2597195; final exact all-architecture inventory was zero.

Virgl/OpenGL fbstat transport was valid, with idle 3/4 flips/presents and
five probes 16/17, 20/21, 21/22, 23/24, 24/25. Present advanced, but the
largest flip delta was only 21, never the retained strict `> idle+100` gate.
Every C6 receipt was nonce/frame-bound and `role_state=live`; its full role
file was 65663 then 69617 bytes while the unchanged C6 carried its truthful
4096-byte prefix. The regular source tail had no media rows, so navigation,
player readiness, and progress are all unavailable. Each C7 sidecar artifact
is honestly `INCOMPLETE census-frame-count-3 no_subtype_claim=1`; it supplies
no full digest or subtype presence/absence fact and did not affect C6 or the
threshold. The driver ended code 8,
`chromium-render-start-missing` with all five receipts
`live-roles-insufficient-flips`; owned cleanup/reap passed.

There is no active-HD720 proof, `yt-presentfps`, `PERF-VIDEO`, FPS, drop, VPQ,
retire, audio, fullscreen, or local-baseline fact. This consumed the sole
fresh gate as INVALID/N=0, not N=1 credit; no retry is authorized. Next is a
no-boot forensic of the C7 three-candidate live transport observation and the
pre-threshold no-presentation plateau, before any new gate.

**C7 wire and source-order forensic — PASS / no boot (2026-07-12):** named
artifact replay of every C7 serial capture proves four complete, adjacent
protocol rows, valid RC/FENCE, and 65,663/69,617-byte raw payloads (131,326/
139,234 hex bytes), all below the 2,000,000 matcher cap. In all five, only
`BEGIN` is coalesced with the literal terminal transition `ESC[?2004l CR` at
the start of its raw record; `META`, `HEX`, and `END` begin cleanly. The
anchored parser therefore selects three rows. There is no kernel interleave
inside a row, truncation, CR corruption, match-cap event, or C7-marker fault.
The smallest repair contract is a C7-BEGIN-only, once-only acceptance of that
exact preamble, retaining raw-record adjacency and rejecting every other
prefix/control placement; chunking is not evidence-justified. It needs actual
wire replay plus foreign-prefix, duplicate, and non-BEGIN negative cases
before any gate, and remains sidecar/no-credit.

The source conclusion is corrected: `/ytmediaprobe.sh` is a post-gate grep
reader, but MAIN-world `probe.js` emits `YT_MEDIA_PROBE_V1` autonomously from
document-idle. Forced-HD720 emits `force_ready` by at most 60 s even without a
player, then emits observation/start as applicable. The C7 source log has zero
such rows over receipts spanning about 338 s; successful high-flip controls
contain the same extension's `force_ready` and later rows (one has 60 rows).
Thus `source-media-marker-missing` is not an expected collection-timing null:
it localizes an extension injection/execution or navigation pipeline failure,
without separating those alternatives. Existing pre-gate facts need no rootfs
change: diagnostic-V2 `producer_start` (capturediag arm) proves extension
liveness after document-idle, `force_ready` exposes player/video state, and
`force_observation` exposes progress. Launcher argv/child_exec proves launch
only. A reviewed host-side pre-gate reader can use those existing diagnostic
facts; if unchanged-arm injection-vs-navigation separation is required, new
instrumentation is unavoidable. No performance, HD720, FPS, or threshold fact
is supplied by this forensic.

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
   conductor follows the prospective coexistence rule in Binding host and VM
   discipline, including its retained all-architecture inventories. The
   RISC-V-exempt trial is also CONSUMED/INVALID at receipt EOF; its
   control-evidence repair and independent review now permit forming (never
   reusing) a fresh serialized A1 gate. That gate is now CONSUMED/INVALID:
   C6 framing completed, but the authenticated fast census was
   `none_or_exited`, so no HD720/media/FPS fact exists. No-boot forensic now
   localizes the missing semantic rows to the receipt's diagnostic-stream
   capture (the rows were written to the separate evidence file). The
   fail-closed repair and independent no-boot review now pass; they permit
   forming, never reusing, a fresh serialized A1 gate. Its sole trial is
   consumed INVALID because the complete probe1 fbstat frame had a
   console-interleaved KMS provenance token. The fail-closed transport repair
   and independent review now pass; they permit forming, never reusing, a
   fresh serialized A1 gate. That gate's sole run is consumed INVALID at the
   fbstat transport gate: CRCRLF expanded to blank rows under the old global
   map. The scoped candidate-local repair, its actual-wire matrix, and its
   independent no-boot review now pass; their sole gate was then consumed
   INVALID at a real `live-roles-insufficient-flips` plateau. The bounded
   role/player diagnostic's first static attempt has an untrusted full-census
   digest/vector claim. A distinct C7-only, cap-preserving full-census
   diagnostic design is now reviewed; it leaves the 4096-byte admitting C6
   receipt unchanged. Its narrow `c7_marker` META-tokenizer correction,
   strengthened host-only static matrix, and independent C7 review passed;
   their sole fresh windowed gate is now consumed INVALID/N=0 at the unchanged
   live-role/no-presentation threshold, with C7 itself diagnostic-INCOMPLETE.
   The retained no-boot wire replay now localizes C7's three rows to the exact
   bracketed-paste-disable prefix on BEGIN; its no-credit, literal-preamble
   repair needs review before a gate. The source-marker forensic also proves
   that its absence after 338 s is an extension/navigation pipeline failure,
   not a post-gate collection null; use existing V2/force pre-gate facts or
   add reviewed unchanged-arm instrumentation. No retry is authorized. Do not
   lower the threshold. Clear >=52 before pursuing about 55-60.
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

**C7 literal-preamble/C8 source-reader static — INCOMPLETE / NO-BOOT
(2026-07-12):** one canonical host-only attempt ran from checkpoint
`ab70040534f2055ca73a205466b6355d4e1df2a8` with
`YT_STATIC_CHECK=1`, MP=1, audio-disable=1, media=1, forced-HD720=1,
capturediag=0, and outdir
`/tmp/xv6-c8-static-20260712T000000Z-2615091`. The exact prelaunch and final
all-architecture `/proc/*/exe` inventories were both zero total,
zero informational RISC-V, and zero conflicting QEMU. No VM, serial guest,
build, rootfs asset, launcher, extension, or default action occurred.

The attempt did not reach `YT-PRESENTFPS-STATIC-CHECK-PASS`: while evaluating
the new empty/no-marker C8 source case,
`source_prethreshold_derive` threw `expected integer but got "state INCOMPLETE
detail source-no-injection-or-liveness rows 0 ..."` at `dict incr result rows`.
Therefore the C7 BEGIN-only literal `ESC[?2004l CR` parser change and C8
bounded canonical-source staging are both **unverified**; neither supplies
C7 transport, extension-liveness, player/navigation/progress, C6 threshold,
HD720, FPS, audio, fullscreen, or VM authority. The failed driver patch and
the unrelated KDE-smoke dirt remain unstaged. Do not retry this static or form
a VM gate from it; first repair the host-only C8 derivation failure, then take
one newly authorized canonical static replay and independent review.

**C8 source-derive counter forensic — ROOT CAUSE LOCALIZED / NO-BOOT
(2026-07-12):** the exact new line is `incr result rows`, not `dict incr
result rows`. `result` is initialized as the serialized diagnostic dictionary,
so Tcl attempts to parse that whole dictionary as an integer at the first
marker-bearing row and raises the retained error. The empty C8 payload itself
does not reach that line and returns its intended INCOMPLETE state; the next
canonical `producer_start` fixture is the first row that fails. The smallest
correct repair is exactly `dict incr result rows`, with no protocol, threshold,
or C7 change.

The C8 counter audit finds no second instance of this structural error:
`raw_record_number` is a separately initialized scalar, producer/ready/
observation are boolean flags, and C8's `serial_counter + 1` reservation is
paired with the existing `guest_cmd` increment/ownership check. The static
redo should explicitly retain C8 row counts `0,1,1,1,2` for empty, producer,
no-player, no-progress, and progressing fixtures (and `1` for the first-row
order-invalid control), in addition to the existing state/detail, transport,
binding, status, no-credit, and C7 literal-preamble assertions. No source
edit, replay, VM, build, rootfs, serial, launcher, or process action occurred;
the C7/C8 patch and unrelated KDE dirt remain unstaged.

**C7 literal-preamble/C8 counter correction — STATIC PASS / NO-BOOT
(2026-07-12):** at reviewed checkpoint
`473f32c408573f8a537ae6c28ed14b10a3761a3e`, the only counter repair was
`incr result rows` to `dict incr result rows`. The existing C8 static fixture
gate now explicitly proves row counts `0,1,1,1,2` for empty, producer-only,
ready-no-player, ready-player/no-progress, and progressing source payloads,
plus exactly `1` marker row for the order-invalid observation-first fixture.
No C8 protocol, C6 receipt/classifier, flip/present threshold, rootfs asset,
extension, launcher, default, or C7 behavior changed beyond the already
staged literal BEGIN-only preamble handling.

One fresh canonical host-only command with `YT_STATIC_CHECK=1`, MP=1,
audio-disable=1, media=1, forced-HD720=1, EGL=0, capturediag=0, and outdir
`/tmp/xv6-c8-counter-static-20260712T000000Z-2624562` exited 0 (external log
`/tmp/xv6-c8-counter-static-20260712T000000Z-2624562.log`) and emitted both
`YT-RENDER-START-RECEIPT-STATIC-PASS` and
`YT-PRESENTFPS-STATIC-CHECK-PASS`. It retains the C7 literal
`ESC[?2004l CR` BEGIN-only acceptance and foreign/mutated/duplicate/non-BEGIN
rejections; C8 source-prefix producer/ready/observation states; nonce,
C3/C4/C5, order, status, cursor, digest, and cap negatives; and the
diagnostic-only/no-credit threshold latch. Exact `/proc/*/exe` inventories
immediately before and after were total/informational-RISC-V/conflicting
`0/0/0`. No VM, KVM, QEMU, guest serial, build, rootfs refresh, launcher,
extension, default, HD720, FPS, audio, fullscreen, or performance action
occurred. This is source/static-only credit; independent no-boot review is
still required before any gate.
