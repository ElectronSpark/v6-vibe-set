# Active xv6 Work Plan

Last updated: 2026-07-13. This is the sole live plan. Superseded V2/V3
self-failure chronology and disposable artifacts remain in git history.

## Objective and acceptance

Deliver Linux-VM-parity YouTube 720p60 from one owned x86 real-KVM+virgl guest:
stable roughly 55–60 presented fps with low drops, proven GPU acceleration,
and real Pulse/virtio-snd userspace audio enabled—never the
`--disable-audio-output` workaround. Acceptance is separate N>=2 valid
`yt-presentfps` + `PERF-VIDEO` evidence for each condition: a windowed watch
page and a nonce-proven actual player/document fullscreen with settled active
1280x720 playback. A maximized window is not fullscreen; samples never pool,
and software fallback, llvmpipe, N=1 timing wins, and diagnostic-only output
do not count.

Prioritize work in this order. Trustworthy evidence transport is an enabler,
not the destination; use it only to support the next performance decision.
First take A1 MP1 from the current 28.9 fps state to >=52. Then repair the
real Chromium Pulse stream path and validate audio-on. Next attack the
measured 52–53 local host-retire/present-throughput wall to roughly 55–60.
Only after those gates may default batteries run. Every diagnostic must end in
an implementation decision, an honest close/null verdict, or the next ranked
lever—evidence plumbing must not substitute for performance progress.

## Immediate queue

The deterministic staging proof at `61de08d`, the canonical V2 invocation,
and the retained existing-image proof are **consumed**. The V2 invocation
completed kernel, user, and rootfs-refresh, then failed only because it queried
`/bin/_consolerecord` while the intentional rootfs rule installs the staged
`_consolerecord` as `/bin/consolerecord`. The one authorized read-only proof
subsequently passed against that pinned fresh `fs.img`, establishing the
staged-to-image console mapping, executable mode/hash, and all pinned media
asset sizes/modes/hashes without a rebuild, refresh, image write, QEMU, boot,
or serial action.

Next require an independent adversarial pre-boot review of the committed
kernel/user/driver/rootfs proof, then a fresh conductor-owned
60-second one-x86-VM gate. That gate may authorize exactly one windowed
real-KVM+virgl A1 treatment (MP1, audio-disable1, media1, forced-HD7201).
Use its C8 result to make a concrete frame-supply implementation decision;
only after fixes may valid windowed samples be repeated to N>=2. Next repair
and validate A2/A4 with paplay localization and N>=2 audio-on, then address
the measured host-retire/present residual to about 55–60, then take separate
actual-fullscreen N>=2 evidence, and only then run gated responsiveness/video
default batteries. Evidence plumbing is an enabler, never a stopping point:
if it fails, localize, fix or honestly close it, and return to performance.
Completion is only when every windowed and actual-fullscreen acceptance
criterion is met. No current VM, audio/fullscreen/default, or performance
credit is authorized.

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
- The conductor delegates every concrete audit, measurement, implementation,
  battery, and forensic to a scoped worker, and records an honest verdict
  before the next queue item. Evidence work never bypasses the prioritized
  performance path above.
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

**Fresh-conductor handoff:** branch and publishing lineage are
`codex/host-linux-abi-shell-port-ff` and explicit
`HEAD:refs/heads/codex/host-linux-abi-shell-port-ff`; the truncated tracking
upstream is never a substitute. Preserve the sole unrelated
`scripts/gpu/kde-plasma-desktop-smoke.expect` dirt. Every future gate uses the
guarded source-only `rg` policy that avoids the WSL raw-image-scan crash,
never `pgrep` or monitor loops, and synchronously reaps every build/serial/VM
command. A serial verdict needs a fresh prompt after its short marker command.
At most one owned x86 KVM+virgl VM may exist, and it must leave no QEMU after
owned cleanup; an independently observed `qemu-system-riscv64` is
informational only and never touched, while every other QEMU conflict aborts
the gate. These constraints do not cap justified guest RAM.

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

**C7/C8 adversarial final review — FAIL / NO-BOOT (2026-07-12):** committed
`6d2daca5689099229f567ab7f3147d52a397173e` equals explicit origin and the
named static log genuinely reaches both static PASS markers. C7 itself passes
this review: its literal `ESC[?2004l CR` handling is only at raw-record start
before C7 `BEGIN`, once-only, and its actual static controls reject foreign,
mutated, duplicate, and non-BEGIN placements. C7 remains after the C6 parse
and outside the C6 classifier; it is sidecar/no-credit.

C8 does **not** clear adversarial review. Its helper and probe are staged only
on `capturediag=0`, while existing extension `producer_start` is enabled only
by the `xv6ytcapturediag=1` URL arm (`capturediag=1`). The synthetic C8
producer-only state is therefore unreachable in the normal C8 runtime and
cannot distinguish injection from navigation there. The passing static builds
synthetic C8 frames and string-locks the generated Bash; it does not execute
the C8 helper's count/copy/digest/hex path. It also has no C8 actual-wire
bracketed-paste-prefix/CRCRLF, outer RC/FENCE, duplicate/noncontiguous, or
truthful-over-cap helper case, despite the known C7 transport preamble. The
parser/derive row assertions are useful host-only coverage, not proof of that
runtime path.

C8 and C7 cannot grant current credit: the receipt classifier runs before both
sidecars and has neither as input; their failure can only remain diagnostic or
abort fail-closed. The commit changes only this driver and plan—no
extension/rootfs/launcher/default/kernel file—and preserved the sole unstaged
KDE-smoke file. Before a new gate, either provide a reviewed reachable
same-arm liveness source or explicitly keep injection/navigation unresolved,
then exercise the generated C8 helper and its actual serial grammar with the
listed negatives. No source edit, test, VM, build, rootfs, serial, launcher,
or process action occurred in this review.

**C8 normal-arm reachability and executable-helper design — VERDICT /
NO-BOOT (2026-07-12):** source-only inspection of
`scripts/gpu/chromium-youtube-presentfps.expect`, the packaged
`rootfs-overlay/share/chromium-youtube-media-probe/probe.js`, and its manifest
confirms that normal A1 (`media=1`, forced-HD720=1, `capturediag=0`) constructs
only `#xv6ytprobe=<nonce>&xv6ythd720=1`. V2 `producer_start` is gated by the
separate `xv6ytcapturediag=1` flag, and the existing capturediag branch is a
minimal diagnostic launcher that finishes before normal media/frame work.
Thus its V2 producer state is not a truthful normal-A1 reachability claim.

The smallest unperturbed normal-A1 reader is instead the already emitted,
nonce-bound V1 `force_ready` from the forced-HD selector: a captured authentic
marker positively proves extension execution, and its existing player/video/
ready fields distinguish extension-executed/no-player from a ready player;
subsequent V1 observation gives no-progress versus progress. It needs no URL,
extension, or rootfs change and remains a C8 sidecar outside C6 and the FPS
windows. Its absence is **INCOMPLETE, not no-injection**: the selector waits
up to 120x500 ms after the extension's `document_idle` run, for which the host
has no authenticated start deadline, and C8 currently transports only the
first 16 KiB of the canonical log. A complete negative injection-versus-
navigation proof therefore requires a separate explicitly no-credit,
marker-only diagnostic URL flag (not unconditional `producer_start`); that
option changes `probe.js`/URL parsing and requires overlay/rootfs refresh.
Existing `capturediag=1` can prove its own diagnostic path without an asset
change, but is too perturbing and differently launched to answer normal A1.
Any source-row filtering proposed to avoid the 16-KiB loss must preserve the
canonical cursor/status and separately name/filter-digest the payload; it may
never relabel a truncation as complete.

Before any C8 replay, add a host-only generated-helper harness modeled on the
existing media-helper harness: narrowly validated test overrides for only the
source fixture and temp prefix; execute the actual generated Bash with its
eight production arguments; and assert the real `wc`, `dd`, digest, and `xxd`
output for empty, multiline/binary, and truthful-over-cap inputs, including no
temporary residue. Feed those four emitted rows through a C8 serial-wire
harness with outer `RC:0` and `FENCE`; require LF, CRLF, and CRCRLF transport
with the scoped literal `ESC[?2004l CR` prefix before C8 `BEGIN`, then reject
foreign/mutated/duplicate/non-BEGIN prefixes, nonzero/missing/duplicate outer
RC/FENCE, duplicate or noncontiguous protocol rows, and malformed binding,
cursor, cap, digest, or hex. The over-cap case must parse as transport yet end
`INCOMPLETE source-prethreshold-over-cap` with no derive/credit. Retain the
C6 isolation assertion (neither C7 nor C8 is classifier input) and threshold
boundaries 103/4 reject, 104/4 reject, 104/5 retain. This is a design verdict
only: no source edit, static execution, VM, build, rootfs refresh, serial,
launcher, extension, default, fullscreen, audio, or performance action
occurred. The later primary performance validation remains explicitly both
windowed and fullscreen YouTube at real KVM+virgl GL; no C8 diagnostic result
is performance credit.

**C8 normal-A1 honesty/executable-harness attempt — FAIL / NO-BOOT
(2026-07-12):** one fresh canonical host-only replay with MP=1,
audio-disable=1, media=1, forced-HD720=1, EGL=0, capturediag=0, and outdir
`/tmp/xv6-c8-honesty-static.qIOVlL` exited 2 (external log
`/tmp/xv6-c8-honesty-static.qIOVlL.log`) before the C8 actual-wire matrix. Its
new generated-Bash harness invoked the production C8 helper over a scratch
multiline source and received a correctly framed helper status
`COUNT_INVALID`; LF/CRLF/CRCRLF, preamble, RC/FENCE, duplicate/noncontiguous,
binary, and truthful-over-cap assertions were consequently not reached and
remain unverified.

The source-local root cause is the helper's existing `valid_decimal` case
`0|[1-9][0-9]*`: its nonzero branch requires at least two leading digit
characters, so the valid one-digit multiline `wc` line-count is rejected.
This executable result is a real helper portability/grammar defect, not an
extension, player, C6 threshold, HD720, audio, fullscreen, FPS, or VM result.
Per the single-suite stop rule, no correction, replay, rootfs action, or VM
followed; the C8 driver patch remains unstaged and the unrelated KDE-smoke
dirt remains untouched. Exact all-architecture `/proc/*/exe` inventories were
total/informational-RISC-V/conflicting `0/0/0` both before source work and
after the failed replay. The next authority must first make the narrow
nonzero-decimal grammar repair and repeat the one canonical static suite;
until then C8 is no-credit and normal A1 injection/navigation silence remains
ambiguous.

**C8 helper decimal-grammar forensic — ROOT CAUSE LOCALIZED / NO-BOOT
(2026-07-12):** independent source/log review at committed checkpoint
`9c2fc1f7320da3d9864eff42230ff3df14d4e87f` confirms the failed executable
route is a real C8 helper defect. The named log records a complete four-row
helper frame with `status=COUNT_INVALID`; C8's parser correctly preserves that
as `source-prethreshold-status-COUNT_INVALID` for LF, CRLF, and CRCRLF, so no
actual-wire success was reached or claimed. The C8-local Bash function
`valid_decimal() { case "$1" in 0|[1-9][0-9]*) ...; esac; }` is not the
intended `^(0|[1-9][0-9]*)$` grammar: shell `*` is an arbitrary-character
glob, so it rejects valid one-digit `1..9` (the multiline fixture has a
one-digit `wc` line count) while accepting malformed two-or-more-character
suffixes such as `10x`. The helper must instead use an exact Bash test, e.g.
`[[ "$1" =~ ^(0|[1-9][0-9]*)$ ]]`; adding a one-digit glob alternative alone
would retain the nondecimal-suffix acceptance.

All C8 numeric transport paths otherwise agree and remain fail-closed:
both source and retained `wc` triples require canonical nonnegative values;
the byte cursor is capped to 16384, payload is exactly `min(cursor,cap)`, and
the retained byte count must equal it. The host separately admits canonical
cursor through 2147483647, requires payload/cap 16384, truthful truncation,
and exactly twice-as-long lower-hex (at most 32768), while every non-OK status
must carry zero numeric payload fields and `digest=unavailable`. The only
separate asymmetry is helper parameter `probe`, which accepts zero/leading
zeros while the host's expected probe is canonical positive; normal callers
already pass canonical values and binding rejects a mismatch, so it is not the
observed count failure and no widening is justified.

`valid_decimal` is not shared: the C5 fbstat and C7 census generated helpers
each contain their own verbatim faulty function. A C8-only repair is the
smallest correction for this stopped C8 suite and cannot alter C5/C7 behavior;
their latent one-digit-count defect needs separately scoped review rather than
an unreviewed cross-protocol edit. Before one newly authorized canonical C8
static replay, lock and execute the generated C8 Bash validator for `0`, every
`1..9`, and `10+`, rejecting empty, signed, spaced, nondecimal, and leading-
zero forms; retain actual helper empty/multiline/binary/over-cap runs and
assert their accepted emitted cursor/payload fields after both `wc` triples.
Strengthen the present harness
to compare the no-player payload and the over-cap decoded payload to the exact
source prefix, not only parser-verified count/truncation, while retaining
cleanup, LF/CRLF/CRCRLF, literal preamble, RC/FENCE, duplicate, and
noncontiguity negatives. Static harness construction otherwise has no second
source-local blocker: overrides are path-validated, the helper is executed
directly with its eight production arguments, and `exec` completion is
synchronous. No source edit, test/replay, VM, build, rootfs, serial, launcher,
or process action occurred in this forensic; start inventory was
total/informational-RISC-V/conflicting `0/0/0`. This is diagnostic-only and
grants no gate, HD720, FPS, audio, fullscreen, or performance credit.

**C8 decimal-fix canonical attempt — FAIL / NO-BOOT (2026-07-12):** the one
fresh canonical host-only command with MP=1, audio-disable=1, media=1,
forced-HD720=1, EGL=0, capturediag=0, and outdir
`/tmp/xv6-c8-decimal-static.oHuRmE` exited 1 before `YT_STATIC_CHECK` entered
its reducer (external log `/tmp/xv6-c8-decimal-static.oHuRmE.log`). Tcl failed
while loading `source_prethreshold_guest_helper_script` at line 4466 with
`extra characters after close-brace`; therefore the newly added inline
generated-Bash decimal test route, helper execution, no-player exact payload,
over-cap exact-prefix, and C8 wire assertions did not execute. This is a
driver-template parse failure, not evidence about the decimal grammar,
extension, player, C6 threshold, HD720, audio, fullscreen, FPS, or VM.

Per the one-canonical-run stop rule, no source correction, rerun, build,
rootfs action, serial, launcher/default/kernel/KDE action, or VM followed.
The C8 driver patch remains unstaged and the unrelated KDE-smoke dirt is
untouched. Exact all-architecture `/proc/*/exe` inventories immediately
before source work and after this failed command were both
total/informational-RISC-V/conflicting `0/0/0`. The next authority must first
repair the C8 generator's Tcl bracing while retaining the C5/C7 validators
byte-for-byte, then perform one new canonical static suite; no C8 credit or
A1 interpretation is changed.

**C8 Tcl/Bash decimal-template forensic — ROOT CAUSE LOCALIZED / NO-BOOT
(2026-07-12):** independent source/log review after `afc3e4a` pins the load
failure to line 4519, not to the anchored Bash regexp. The generated helper
is a Tcl braced template beginning at line 4488; its new Bash
`if [ "${YT_SOURCE_PRETHRESHOLD_DECIMAL_TEST_ONLY:-0}" = 1 ]; then` was closed
with a raw `}` rather than Bash `fi`. Its `${...}` pair is Tcl-brace-balanced,
but the standalone `}` closes the enclosing Tcl template early; following
Bash text then yields `extra characters after close-brace`. Had it loaded,
the same line would also be invalid Bash. In that braced template, literal
`[[`, `]]`, `$1`, `(`, `)`, `|`, and `[0-9]` need no Tcl escaping; the exact
literal `[[ "$1" =~ ^(0|[1-9][0-9]*)$ ]]` is safe and preserves the required
anchored decimal grammar. A narrowly retained inline branch would need only
`fi`, never `}`, but it is not the safest final design.

Remove the production `YT_SOURCE_PRETHRESHOLD_DECIMAL_TEST_ONLY` environment
hook altogether: the C8 contract permits only the already reviewed source-log
and temp-prefix test overrides, and a runtime mode that emits no four-row C8
frame is unnecessary even though it would fail closed. The safest static-only
route is one braced Tcl producer for the literal Bash function, inserted via
an `@VALID_DECIMAL@` `string map` token into the real helper and also written
to a dedicated host-static checker ending in braced Tcl text
`valid_decimal "$1"`. Run that checker as the direct Tcl list
`/bin/bash <checker> <value>` (no shell and no environment assignment), and
source-lock the exact shared literal in the emitted real helper. Braced Tcl
literals preserve checker `$1` and regex brackets; do not put either in a
double-quoted Tcl string, where `$1` substitutes and `[...]` command-
substitutes. The present `list` plus `exec {*}$command 2>@1` runner otherwise
passes values as one argument without re-interpolation, so its accept/reject
matrix is sound once detached from the production hook.

The replacement checker must accept exactly `0`, each `1..9`, and representative
`10+` values, and reject empty, `+1`, `-1`, leading/trailing/internal spaces,
`1x`/`10x` (and decimal punctuation), and `00`/`01`; retain the actual helper
empty/multiline/binary/over-cap, exact no-player/prefix, cleanup, wire, and
no-credit assertions. `git diff --check` is clean. The C7 generated-helper
range is byte-identical to committed `6d2daca` (SHA-256
`3cced3bfd6a86ed7312305ceff83c96015b893e52414cfcc888626f0bf79391b`), and
its C7 `valid_decimal` remains the old glob; only C8 carries the attempted
regex/test route. No other source or KDE edit, test/replay, VM, build, rootfs,
serial, launcher, or process action occurred. Exact start inventory was
total/informational-RISC-V/conflicting `0/0/0`. This review grants no C8, A1,
HD720, FPS, audio, fullscreen, or performance credit.

**C8 Tcl-safe decimal-literal canonical attempt — FAIL / NO-BOOT
(2026-07-12):** one fresh canonical host-only static command with MP=1,
audio-disable=1, media=1, forced-HD720=1, EGL=0, capturediag=0, and outdir
`/tmp/xv6-c8-tcl-safe-static.WIdXt2` exited 2 (external log
`/tmp/xv6-c8-tcl-safe-static.WIdXt2.log`). The new direct `/bin/bash <checker>
<value>` checker and exact shared emitted literal passed every required decimal
case: accepts were all `1` for `0`, `1..9`, `10`, `42`, and `2147483647`, and
rejects were all `1` for empty, signed, spaced, `1x`, `10x`, `1.0`, `00`, and
`01`. C8 parser/adversary/source-contract/actual-wire/source-lock/wire-
adversary/no-credit predicates were also `1`; the actual-helper aggregate was
`0` without a more specific detail, so empty/multiline/binary/over-cap
end-to-end credit remains unearned. The suite also reported the independent
render-start C5 receipt fbstat-marker-drift predicate as `0`; this run neither
changes the C5/C7 helper implementations nor establishes that either failure
was introduced by C8.

Per the one-suite stop rule, no driver correction or second replay followed;
there was no VM, build, rootfs, serial, launcher, default/kernel/KDE action.
The C8 driver patch remains unstaged, and unrelated KDE-smoke dirt remains
untouched. Exact all-architecture `/proc/*/exe` inventories before source work
and after the failed suite were both total/informational-RISC-V/conflicting
`0/0/0`. C8, A1, audio, HD720, fullscreen, FPS, and performance remain
no-credit. The next authority must forensically localize the failed
actual-helper aggregate and C5 receipt predicate before any narrowly scoped
repair and one newly authorized canonical static replay.

**C8 aggregate/C5-C7 static-reporter forensic — ROOT CAUSE PARTIAL / NO-BOOT
(2026-07-12):** independent inspection of named scratch
`/tmp/xv6-c8-tcl-safe-static.WIdXt2`, its 13,857-byte external log, and the
unstaged driver separates three facts that the single terminal tuple obscured.
First, C8's actual-helper aggregate really is false (`c8=1,1,1,1,0,1,1,1,1`):
source/static predicate, direct decimal checker, exact emitted-literal lock,
progress LF/CRLF/CRCRLF wire matrix, wire adversaries, and no-credit are all
one; only the combined empty/multiline/binary/over-cap helper expression is
zero. Every helper invocation returned normally and passed its four-row label
check, but the source retains only the final 16,385-byte over-cap input and
not each helper stdout, parsed META, or component boolean. The aggregate is a
long inline `expr` with no detail vector, so the retained evidence cannot
honestly distinguish its no-player, binary, empty, over-cap, or earlier
cleanup conjunct. No production helper correction is localized from it.

Before one new static suite, replace that one opaque boolean with bounded
per-case records (`progress`, `no_player`, `binary`, `empty`, `over_cap`) that
retain command success/error code, row count, parser pass/detail, META
status/cursor/payload/cap/truncated/hex count, exact-payload or exact-prefix
boolean, semantic detail where applicable, and cleanup boolean—never a large
payload in the terminal line. Persist each already bounded helper frame under
the static outdir before the next source overwrite (the over-cap frame is at
most 32,768 lower-hex bytes plus its short envelope), and append this detail
vector to `fail_preflight`. This is instrumentation only; preserve the exact
helper and every existing wire negative until a named component fails.

Second, C5's displayed `c5=0 receipt-fbstat-marker-drift {}` is the intended
fail-closed negative result, not a new C5 failure: the terminal condition has
always required `[lindex $static_render_fbstat_drift 0]` to be false. The
same construction and condition are byte-for-byte present at `6d2daca`,
`a262911`, and current source; print a named `c5_marker_drift_rejected=1`
summary rather than the raw parser tuple to avoid another false alarm. The
C5 emitted helper is unchanged at SHA-256
`a0c6d5a831789dd49221b3cf20f6b72ffe53b18c8e8a7becd831b3a6a15e9ef8`.

Third, the terminal has a separate C7 reporter defect: its new
`static_c7_helper_unchanged` compares the *emitted* helper against
`3cced3...`, which is a prior larger source-range hash, not that helper's
bytes. The current, `6d2daca`, and `a262911` emitted C7 helper all hash to
`cce4b2b01f8a69f06d466a0a84cb75a0b258ad97b9c34e85879e7e908f377842`, so
the guard's `0` is an expectation error, not a C7 behavior change. Correct
only that expected emitted hash (and preferably use the same exact hash form
for C5); retain C7's old local decimal validator byte-for-byte. C5/C7 source
slices also match all three checkpoints, so no C8 interaction or accidental
implementation edit is evidenced. No source edit, test/replay, VM, build,
rootfs, serial, launcher, KDE, or process action occurred; exact start
inventory was total/informational-RISC-V/conflicting `0/0/0`. This grants no
C8, C7, C5, A1, HD720, audio, fullscreen, FPS, or performance credit.

**C8 per-case reporter canonical replay — FAIL / NO-BOOT (2026-07-12):** the
one fresh canonical host-only static command with MP=1, audio-disable=1,
media=1, forced-HD720=1, EGL=0, capturediag=0, and outdir
`/tmp/xv6-c8-reporter-static.OggPZN` exited 2 (external log
`/tmp/xv6-c8-reporter-static.OggPZN.log`). Its bounded static reporter
persisted every actual-helper source/frame/wire artifact before the shared
source fixture was overwritten, under
`source-prethreshold-helper-reducer/actual-{progress,no_player,binary,empty,overcap}.{source,frame,wire}`.
The new terminal vector localizes the formerly opaque C8 aggregate exactly:
`progress` passed (run/rows/parser/exact/derive/cleanup
`1/4/pass/1/COMPLETE-source-observation-progressing/1`), `no_player` passed
(`1/4/pass/1/INCOMPLETE-source-extension-executed-no-player/1`), `empty`
passed (`1/4/pass/1/INCOMPLETE-source-force-ready-unobserved-ambiguous/1`),
and `overcap` passed (`1/4/pass`, cursor/payload/cap/truncated/hex
`16385/16384/16384/1/32768`, exact-prefix `1`, cleanup `1`). Only `binary`
failed: its helper run and four-row label check passed and cleanup was `1`,
but the C8 parser returned `source-prethreshold-hex-roundtrip`; therefore its
META/payload/derive fields remain unavailable and its exact-payload bit is
`0`. This is a localized static transport/fixture observation, not a licensed
production-helper or derive change.

The C5 fbstat-marker adversary now reports its intended negative honestly as
`c5_marker_drift_rejected=1`, with both C5 emitted-helper SHA and validator
guards `1`. The prescribed C7 emitted-helper expectation still produced guard
`0` (terminal C7 tuple `1,1,1,1,1,0,1`); no C7 helper implementation was
changed, so the mismatch requires separate static forensic rather than a
claim that C7 behavior regressed. Per the one-suite stop rule, no driver
correction, second replay, VM, build, rootfs, serial, launcher, default,
kernel, or KDE action followed. The driver remains unstaged and unrelated
KDE-smoke dirt remains untouched. Exact `/proc/*/exe` inventories immediately
before and after were total/informational-RISC-V/conflicting `0/0/0`. This
grants no C8/C7/C5/A1, HD720, audio, fullscreen, FPS, or performance credit;
the next authority must first forensically isolate the binary roundtrip and
C7 emitted-hash expectations before any narrow repair and one new canonical
static suite.

**C8 binary roundtrip/C7 emitted-guard forensic — ROOT CAUSE LOCALIZED /
NO-BOOT (2026-07-12):** named binary artifacts prove a valid production-helper
transport, not malformed fixture data: `actual-binary.source` is exactly 14
bytes `6d756c74696c696e650a00ff410a` (`multiline LF NUL FF A LF`), its
SHA-256 `30553a5bfa023e59853ee00f3caf7af2070486170a371e3f7d5b5c1aae514d31`
equals helper META digest, and the persisted frame declares cursor/payload
`14/14`, cap 16384, truncation 0, hex length 28, and the identical lower-hex
payload. Thus decode cannot fail and encode round-trips that bytearray; the
sole failing C8 parser conjunct is `string bytelength $payload`: Tcl exposes
decoded `FF` through its Unicode/UTF-8 string representation, so that metric
is 15 rather than the protocol's 14 raw bytes. This is a host C8 parser
byte-accounting bug exposed by a valid binary fixture, not a Bash helper,
serial-wire, or derive failure.

The narrow repair leaves the helper and frame grammar unchanged. After hex
decode, obtain `decoded_hex = [binary encode hex $payload]` and require its
ASCII length to equal META `hex_bytes` and its text to equal `payload_hex`;
the earlier exact `hex_bytes == 2 * payload_bytes` check then proves raw byte
length without Unicode `string bytelength`. Also replace C8's use of the
text-mode `render_start_receipt_digest` on decoded payload with a C8-local
binary writer/digest (open `wb`, binary translation and encoding, write no
newline, SHA-256, remove). Otherwise the next run would UTF-8-expand `FF` in
the host digest and stop at a false digest drift. Keep broader C5/C7 parser
changes out of this C8 repair; their current ASCII evidence gives no authority
to widen scope. Represent the binary fixture itself as one bytearray decoded
from exact hex, and assert parsed `binary encode hex` equals that same hex,
not textual `eq` over a double-quoted mixed byte/string fixture. Retain the
14-byte source, 28-hex, META digest/host binary-digest equality, parser pass,
and cleanup assertions.

The C7 guard failure is also a reporter expectation error. C7's braced Tcl
template contains Bash backslash-newline continuations; Tcl replaces each
backslash/newline/following indentation sequence with one space before the
procedure returns. The emitted C8 artifact visibly demonstrates this rule as
`||  ! valid_marker`. Therefore raw-source slicing produced the prior
`cce4...` candidate, but the actual returned C7 helper at current, `6d2daca`,
and `a262911` is SHA-256
`1343803c7941188e0175b765cdda449a97d0aa0ccd52df83efbd7a9ecf138ac5`.
Set the guard to that value and report the digest computed directly from
`[role_census_diag_guest_helper_script]`; never construct a guard from raw
source extraction. C7 implementation remains byte/semantic unchanged. No
source edit, test/replay, VM, build, rootfs, serial, launcher, KDE, or process
action occurred; exact start inventory was total/informational-RISC-V/
conflicting `0/0/0`. This grants no C8/C7/A1, HD720, audio, fullscreen, FPS,
or performance credit.

**C8 binary host-accounting correction — STATIC PASS / NO-BOOT
(2026-07-12):** one fresh canonical host-only static command with MP=1,
audio-disable=1, media=1, forced-HD720=1, EGL=0, capturediag=0, and outdir
`/tmp/xv6-c8-binary-static.H0iy5h` exited 0 (external log
`/tmp/xv6-c8-binary-static.H0iy5h.log`) and emitted both
`YT-RENDER-START-RECEIPT-STATIC-PASS` and
`YT-PRESENTFPS-STATIC-CHECK-PASS`. C8 now decodes lower hex, re-encodes it as
`decoded_hex`, requires that ASCII length to equal META `hex_bytes` and exact
text equality to the encoded payload, and computes the host digest through a
C8-local `wb`/binary-translation/binary-encoding/no-newline temporary file.
The valid exact binary fixture `6d756c74696c696e650a00ff410a` passes the
actual generated helper and parser with cursor/payload/cap/truncated/hex
`14/14/16384/0/28`, parsed re-encode equality, and matching helper/META/host
SHA-256 `30553a5bfa023e59853ee00f3caf7af2070486170a371e3f7d5b5c1aae514d31`.
The retained progress, no-player, empty, and truthful-over-cap actual-helper
records, LF/CRLF/CRCRLF/preamble/outer-RC-FENCE wire matrix, all wire
negatives, decimal matrix/source lock, C5 marker-drift negative, and cleanup
remain green. The C7 emitted-helper guard now passes against the direct
`role_census_diag_guest_helper_script` SHA-256
`1343803c7941188e0175b765cdda449a97d0aa0ccd52df83efbd7a9ecf138ac5`; C7's
old decimal glob and implementation remain untouched.

This changes only C8 host byte accounting/digest validation and its static
fixture/assertions plus the C7 static expected digest; it does not alter the
C8 guest helper/frame grammar/derive behavior, C5/C7 parser behavior, or any
VM/rootfs/build/launcher/default/kernel/KDE path. Exact `/proc/*/exe`
inventories immediately before and after were total/informational-RISC-V/
conflicting `0/0/0`; no process was touched. This is source/static-only and
grants no C8/C7/A1, HD720, audio, fullscreen, FPS, or performance credit. The
next work remains an independent no-boot review before any gate.

**C8 binary correction final independent adversarial review — PASS / NO-BOOT
(2026-07-12):** committed `1717441f13ada1e7ba16f67a09d0482a2bb9465c` equals
the explicit `origin/codex/host-linux-abi-shell-port-ff`. The canonical
static record `/tmp/xv6-c8-binary-static.H0iy5h` and its external log both
reach the two static PASS markers. Its retained generated-Bash binary fixture
is exactly 14 bytes `6d756c74696c696e650a00ff410a`; helper META, decoded hex,
and host binary-file digest agree on 14/14/16384/0/28 and SHA-256
`30553a5bfa023e59853ee00f3caf7af2070486170a371e3f7d5b5c1aae514d31`.
The review rechecked direct Bash decimal execution, empty/no-player/progress/
over-cap records, cleanup, LF/CRLF/CRCRLF, exact C8 BEGIN-only literal
`ESC[?2004l CR`, outer RC/FENCE, duplicate/noncontiguous/preamble negatives,
and cap/count/hex/digest binding. In normal `capturediag=0`, missing
`force_ready` remains the honest INCOMPLETE ambiguity; no V2 reachability or
injection claim is introduced. C8 is set only after C6 classification, and
the classifier has neither C7 nor C8 input, so neither sidecar can credit the
retained C6 threshold.

C5's marker-drift `0` parse result is the intended rejection, and its helper
guard remains `a0c6d5a831789dd49221b3cf20f6b72ffe53b18c8e8a7becd831b3a6a15e9ef8`.
C7's direct emitted-helper guard is
`1343803c7941188e0175b765cdda449a97d0aa0ccd52df83efbd7a9ecf138ac5`; its
literal preamble, CRCRLF, RC/FENCE, duplicate, order, cap, and no-credit
controls remain bound, while its implementation and old decimal glob are
unchanged. The reviewed range changes only this driver and plan: no
extension/rootfs/launcher/default/kernel/C5/C7 implementation change, and
the sole unrelated KDE-smoke dirt is preserved. Final exact QEMU inventory was
total/informational-RISC-V/conflicting `0/0/0`; no VM was formed. This PASS
permits only formation of a fresh serialized **windowed** A1 gate; it is not a
VM authorization and supplies no HD720, fullscreen, audio, `yt-presentfps`,
`PERF-VIDEO`, or performance credit. Actual fullscreen remains a separate
later N>=2 requirement.

**Authorized A1 C8-repair windowed trial — CONSUMED / INVALID / N=0
(2026-07-12):** the fresh conductor authority at HEAD/origin
`16af28fe8a04aa8a9f9a51e8bf485b915f579bc2` passed this worker's immediate
explicit-ref check, `/dev/kvm` read/write check, no other VM worker, and exact
prelaunch total/informational-RISC-V/conflicting QEMU inventory `0/0/0`. The
one and only owned x86 KVM+virgl trial is
`/tmp/xv6-a1-windowed-c8-live.6vPv1Z` (external driver log
`/tmp/xv6-a1-windowed-c8-live.6vPv1Z.driver.log`), with scratch image
`/tmp/xv6-yt-20260712T163225Z-pid2696377-mp1-audio1-media1-hd7201.fs.img`.
It used the windowed GTK KVM/host-DRI/virtio-vga-gl-primary launch contract,
4 GiB RAM, MP=1, audio-disable=1, media=1, forced-HD720=1, EGL=0,
capturediag=0, `QEMU_AUDIO=none`, and the standard 60-second A1 windows; it
is not fullscreen and carries no audio-on/default claim.

The driver stopped at the very first **idle C1** transport before Chromium
launch, C6 receipt, C7/C8 sidecars, HD720/media proof, or either FPS window:
`code=8 FAIL invalid-fbstat-transport tag=idle attempt=1
detail=transport-frame-noncontiguous`. The retained raw artifact
`fbstat-idle-attempt1.txt` is 114441 bytes/17 lines and contains an inner
`status=OK`, raw/payload `56422/56422`, cap `131072`, truncation `0`, and
hex-length `112844` frame, but it was correctly not admitted as an fbstat
sample. `metrics.txt` consequently has only the arm header and
`media_probe_image_assets=PASS`; there is no `PERF-VIDEO`, `yt-presentfps`,
drop, VPQ, retire, C6/C7/C8 diagnostic, active-HD720, audio, or fullscreen
fact. This is an INVALID/N=0 consumed trial, not an N=1 timing sample or
performance credit; no retry is authorized under this gate.

The driver owned leader `2696539`; cleanup observed it zombie-only, synchronously
reaped it (`waited:2696539 exp4 0 0`), and recorded `exact_qemu_after=none`.
Fresh post-run and final exact inventories were total/informational-RISC-V/
conflicting `0/0/0`; no external QEMU was touched. No source, rootfs, build,
launcher, default, fullscreen, audio-path, kernel, or KDE change occurred;
unrelated KDE-smoke dirt remains untouched. The next authority must localize
this fresh idle C1 transport noncontiguity no-boot before forming another A1
gate.

**C1 idle transport wire forensic — ROOT CAUSE LOCALIZED / NO-BOOT
(2026-07-12):** the named regular raw artifact is 114441 bytes (SHA-256
`972875a3afcba6da3509b3ad8080e124cbdd26cd034945cbfecddaadd428a42e`).
Splitting only on LF gives C1 candidate records BEGIN/META/HEX at raw records
7/8/9 and END at 14; its one RC:0 and one FENCE follow at 16/17. All four
transport labels and the nonce occur once, there is no NUL, and candidate
terminators are the accepted CRCRLF form. The terminal bracketed-paste prefix,
command echo, and DRM log precede BEGIN; the capture ends after FENCE, so none
is the failure. The outer command frame necessarily passed (otherwise the
detail would be `transport-command-frame-*`), but the candidate indexes prove
the host's `transport-frame-noncontiguous` rejection is correct.

Records 10--12 are not harmless lines between intact records: each begins or
continues the lower-hex stream while carrying a byte-weaved
`virtio_gpu: page-flip present` printk; record 13 resumes hex before END.
The helper's inner `status=OK`, `56422/56422`, cap 131072, digest, and 112844
hex count are therefore untrusted forensic metadata, never a verified fbstat
payload. This is genuine guest-console wire corruption, not host artifact
truncation/wrapping, a second frame/nonce, binary data, CRCRLF normalization,
or a parser/test defect. The producer emits the whole HEX payload in one
`printf`; x86 `consolewrite` advances user output in 64-byte batches while
asynchronous kernel console output drains in 32-byte steps, with UART locking
only per character. The normal first-eight-flips `printf` in
`virtio_gpu_scanout.c` supplies the observed collision. Do not relax C1 raw
adjacency, ignore noncandidate bytes, or splice hex fragments: that would turn
an unauthenticated corrupt wire into performance evidence.

The small direct mitigation is to make that normal page-flip log opt-in, but
it is insufficient because any later kernel console row can still split a
large C1 write. A robust C1 repair needs a bounded console record-atomic
primitive shared with kernel console emission, plus a sequence/count/digest
bound chunk protocol; only then may the host tolerate noncandidate records
*between* complete sealed chunks. A separate QEMU channel is out of scope.
Before source work, add a retained parser diagnostic with candidate raw indexes
and bounded gap fingerprints, and source-static adversaries for this exact
mid-HEX byte weave, clean LF/CRLF/CRCRLF, pre-BEGIN echo/preamble, duplicate/
reordered/missing chunks, and any corrupt chunk; every corrupt form remains
INVALID/no-credit. No source, test, VM, build, rootfs, launcher/default/kernel/
KDE change occurred here. This trial remains pre-Chromium INVALID/N=0; its
owned QEMU was synchronously reaped and this forensic's exact inventories were
total/informational-RISC-V/conflicting `0/0/0`.

**C1 record-atomic/V2 transport design — PASS / NO-BOOT (2026-07-12):** at
HEAD `8d00a08fb9b1d2add3a68ce22dfe3c663a264ace`, direct source audit confirms
three unshared x86 wire producers: `consolewrite()` sends 64-byte batches,
`consoled` calls `consputs()` in 32-byte steps, and `tty_drain` writes UART
bytes directly; `uart_tx_lock` covers one character only. The retained C1
artifact's mid-HEX weave is therefore expected. Exact inventories before and
after this docs-only audit were total/informational-RISC-V/conflicting
`0/0/0`; no VM, process, source, build, rootfs, test, launcher, default, or
KDE action occurred, and the unrelated KDE-smoke dirt remains preserved.

The selected smallest x86 ABI is one `/dev/console` ioctl, not a new syscall:
`CONSOLE_IOC_WRITE_RECORD` accepts version 1, zero flags/reserved, a user
pointer, and a record length. It copies request and bytes before locking,
requires exactly one final LF with no embedded LF/CR/NUL, and limits the
post-LF-to-CRLF physical record to 512 bytes; copy/shape/panic/timed-lock
failures emit nothing. `console.c` will add a sleepable console-wire mutex and
raw emitter shared by the ioctl, normal `consolewrite` batches, `consputs` /
klog drain, and tty output drain. The recorder holds it for one record only
(about 45 ms at 115200); timed acquisition fails closed, so there is no
multi-chunk printk starvation. Normal order is `pr.lock -> console-wire mutex
-> uart_tx_lock`; copyin and `cons_async`/TTY/pipe spinlocks are released
before the mutex. Early boot and panic remain explicit terminal bypasses and
must remain no-credit; RISC-V arch code/behaviour stays untouched.

C1 V2 will emit sealed BEGIN, META, zero or more CHUNK, and END records
through a silent x86 `consolerecord` user helper, with outer marker-bound
`RC:0`/`FENCE` still mandatory. META binds tag, nonce, status, raw/payload
bytes, cursor, cap `131072`, truncation, payload SHA-256, total hex bytes,
total chunks, and `chunk_hex_max=384`. Each CHUNK binds nonce, contiguous
`seq`, contiguous `hex_offset`, exact even `hex_bytes<=384`, and lower-hex
data; BEGIN/META/END repeat the total/digest bindings. The host may ignore
only wholly noncandidate raw gaps between sealed records, retaining their
bounded fingerprints; any foreign transport candidate, byte-contaminated
row, missing/duplicate/reordered/noncontiguous/oversize chunk, count/cursor/
cap/truncation/hex/digest drift, or nonunique/misordered outer RC/FENCE is
INVALID/no-credit. It never splices fragments and retains candidate raw
indexes on rejection. Existing sacrificial `true;` first-character-drop and
fresh-prompt precautions remain in force.

Rejected: a new syscall expands x86/RISC-V ABI plumbing without benefit;
newline aggregation is unbounded/ambiguous for the former 112 KiB HEX line
and misses klog/TTY; reusing `pr.lock`, `cons_async.lock`, or `uart_tx_lock`
misses a wire producer, protects only staging, or spins per character. Before
any source repair, require host V2 static vectors for valid noisy gaps, the
actual mid-HEX weave, LF/CRLF/CRCRLF, prompt/preamble, and all binding/order/
corruption negatives; generated-helper vectors for empty/binary/multiline/
cap/over-cap and no direct transport `printf`; a host-only fake-UART kernel
unit proving one contiguous emit and zero output for malformed/copy-fault/
panic/timed-lock cases plus shared-path source locks; x86 kernel and
`_consolerecord` user builds only; then independent adversarial review. This
is design evidence only and supplies no A1, HD720, fullscreen, audio,
`yt-presentfps`, `PERF-VIDEO`, FPS, or performance credit.

**C1 record-atomic/V2 adversarial design review — REVISE / NO-BOOT
(2026-07-12):** this supersedes the preceding provisional design PASS; it is
not an implementation result. Exact post-review QEMU inventory was
total/informational-RISC-V/conflicting `0/0/0`. No VM, build, test, rootfs,
source, launcher, default, or KDE action occurred; the only unrelated dirt is
still `scripts/gpu/kde-plasma-desktop-smoke.expect`.

The old `chunk_hex_max=384` is physically impossible under its own bounded
record rule. With `tag<=16`, `nonce<=32`, `seq=697`, and
`hex_offset=262072`, a CHUNK content row is 518 bytes; its final LF is 519
input bytes and normal CRLF emission is 520 physical bytes. V2 must instead
cap tag at 16 ASCII token bytes, nonce at 32, and use exact rows carrying both
tag and nonce on *every* row, including CHUNK. Its fixed maximum CHUNK form
is `YT_FBSTAT_TRANSPORT_V2_CHUNK tag=<16> nonce=<32> seq=697
hex_offset=262072 hex_bytes=376 data=<376>\n`: 510 content + LF = 511 input,
then 512 physical CRLF bytes. Thus `chunk_hex_max=376`, `total_hex<=262144`,
and `total_chunks<=698`; the final maximum-cap chunk is 72 hex bytes. The
shared v1 request is exactly four naturally aligned fixed-width fields
`uint32 version`, `uint32 flags`, `uint64 data_ptr`, `uint32 data_len`,
`uint32 reserved` (24 bytes; offsets 0/4/8/16/20; compile-time size/offset
assertions). A single shared UAPI `_IOW` constant with fixed magic/number and
that exact size is required; all other command encodings, versions, flags,
reserved bits, null pointers, and lengths outside 1..511 fail before output.
The copied payload must contain exactly one terminal LF and no other LF, CR,
or NUL. Therefore `data_len+1<=512` is the physical invariant, not an
ambiguous post-hoc estimate.

`sys_vfs_ioctl` deliberately passes unknown ioctl arguments as raw user
pointers, and `vfs_ioctl` dispatches `/dev/console` through
`console_cdev.dev.ops.ioctl`, not its cdev file operation. The x86 handler
must consequently use `either_copyin` for the 24-byte request and then a
fixed 511-byte kernel buffer *before* taking any wire lock; it must never
dereference a post-lock user pointer. Route both console ioctl entry points
through one common handler, preserve ordinary tty commands exactly, and make
the record command root-only (`current_euid()==0`, otherwise `-EPERM`). The
current device ioctl has no `vfs_file`, hence cannot honestly enforce an
open-mode check without wider VFS ABI work: `_consolerecord` opens
`/dev/console` write-only, while a root read-only descriptor is intentionally
not an extra rejection condition. This decision, invalid-pointer/copy-fault
`-EFAULT`, malformed `-EINVAL`, unavailable/panic `-EAGAIN`, and timed
contention `-ETIMEDOUT` must be tested as zero-byte *pre-emission* failures.
`_consolerecord` takes exactly one argv record *without* its final LF, rejects
empty, >510-byte, CR, or LF input, appends the LF in its private 511-byte
buffer, performs the ioctl, and never writes stdout/stderr or falls back to
`write(2)`. The generated Bash helper must construct each row with a silent
assignment/`printf -v` and invoke that binary; direct `YT_FBSTAT_TRANSPORT*`
`printf`/`echo` to the console is forbidden. The existing fbstat capture and
retained input remain fresh regular, nonsymlink files and retain their own
file-type/short-read checks.

The wire lock design also needs correction. On x86 only, introduce a
sleepable `console_wire` mutex with a 50-ms `mutex_lock_timed` only for the
record ioctl. `consolewrite` locks it per existing <=64-byte postprocessed
batch, consoled per existing <=32-byte step, and tty drain per <=64-byte
batch; they hold no `pr`, async-ring, TTY, or pipe spinlock. The only normal
order is `console_wire -> uart_tx_lock` (each existing `uartputc_sync` still
locks one character); do not bulk-hold `uart_tx_lock` for a 44-ms record and
do not claim `pr.lock -> console_wire`. Normal process/kthread `consputs`
must use this same emitter. Early boot, panic, interrupt, no-current-thread,
or spin-held emission explicitly bypasses it. Every such bypass increments an
x86 emergency generation; the ioctl samples it while locked and returns
no-credit failure if it changed. A concurrent emergency/panic can already
have dirtied a row and cannot be rolled back, so it is never described as a
zero-byte failure; the helper must stop and return nonzero, leaving the host
with an incomplete/corrupt frame to reject. No locking/error path may call
`printf`. All new code and changed normal x86 emitters must be `__x86_64__`
gated; RISC-V retains its old ioctl and output behaviour byte-for-byte.

V2 must use exact BEGIN/META/CHUNK/END schemas with `version=2`; BEGIN,
META, and END repeat cap, total-hex, total-chunks, and SHA-256 digest, while
META additionally binds status/raw/payload/cursor/truncation/chunk maximum.
CHUNK binds tag, nonce, contiguous zero-based sequence, exact
`hex_offset=seq*376`, exact nonfinal/final length, and lower-hex data. The
host accepts only one complete ordered envelope and treats **any** raw record
starting `YT_FBSTAT_TRANSPORT_` (unknown version, V1, foreign, malformed, or
extra included) as a candidate that rejects. It may retain bounded count,
byte count, and binary-safe first/last gap digests only for wholly
noncandidate records outside intact candidates; no global CR normalization,
fragment splicing, unbounded diagnostic text, or candidate after END is
allowed. It must cap the scanned raw serial at the existing 2-MiB ceiling,
record candidate indexes, and require the unique outer `RC:0` then `FENCE`.

`131072` decoded bytes now mean 697 full 512-byte records plus one 207-byte
final CHUNK and 710 bytes of maximal BEGIN/META/END rows: 357781 inner CRLF
recorder bytes before the separately accounted marker-dependent outer rows, or
31.06 seconds at 115200 8N1
before capture, hashing, 698 ioctls, scheduling, and up to 50-ms contention
waits. The existing 35-second
`fb_sample` command limit is therefore unsound. Before implementation, pin a
single calculated C1 timeout (including successful-lock and helper reserve,
not an arbitrary retry) and make its algebra a host static assertion; the
driver must fail closed if that budget cannot fit its enclosing prelaunch
deadline.

Implementation is now sliced and blocked on these no-boot tests: (1) replace
the current V1 parser/helper/static frame generator with V2 vectors covering
empty and binary payloads, cap/max-row arithmetic, LF/CRLF/CRCRLF,
known prompt preamble, valid bounded noisy gaps, actual mid-row weave, unknown
or V1 candidates, every binding/order/count/offset/hex/digest/cursor failure,
and bounded gap diagnostics; (2) add a host fake-UART kernel unit plus source
locks proving all three normal x86 producers share the mutex, record
contiguity, no malformed/copy/privilege/timed/panic-pre bytes, and emergency
generation no-credit; (3) add silent host-glibc `_consolerecord` under the
existing automatic `user/programs/*` discovery/stage path (not the unused
user CMake path), with no stdout/stderr or fallback write, then statically
cover its input-line edges and the capture regular/symlink/FIFO/short-read
edges; (4) x86 kernel and helper builds only, followed by a fresh independent
adversarial review. No kernel boot or performance attempt is authorized until
those slices pass. This remains A1 N=0 and supplies no video, fullscreen,
audio, or responsiveness credit.

2026-07-12 C1 slice-1 local verdict — FAIL (no boot, no QEMU): the newly
added host fake-sink runner was invoked after clean exact-QEMU preflight, but
resolved its source as `/home/es/xv6-os/tests/console_record_host_test.c`
instead of the kernel-submodule path and exited before compiling or exercising
the test. Per the revised contract, no x86 kernel/helper build, VM run, or
speculative repair followed; the uncommitted kernel/user implementation is
preserved for review. This is a harness-path failure, not evidence for or
against record atomicity, and provides no lane credit.

**C1 slice-1 independent source forensic — FAIL / NO-BUILD / NO-BOOT
(2026-07-12):** at top-level `b67c1a6`, exact QEMU inventories before this
review were total/informational-RISC-V/conflicting `0/0/0`. No runner, compile,
test, VM, rootfs, launcher, default, kernel, or KDE action occurred here. The
only reviewed unstaged implementation is kernel-submodule
`kernel/console.c`, untracked `kernel/inc/dev/console.h` and
`kernel/tests/{console_record_host_test.c,run_console_record_host_test.sh}`,
and untracked user-submodule `programs/consolerecord/consolerecord.c`; the
KDE-smoke dirt remains unrelated and untouched.

The retained runner failure is exact: from `kernel/tests`, its `../..`
calculation makes `repo_root=/home/es/xv6-os`, so its source operand becomes
the nonexistent `/home/es/xv6-os/tests/console_record_host_test.c` and its
audit wrapper `/home/es/scripts/audit/safe-rg.sh`. The smallest robust repair
is `kernel_root=$(.../..)` (not `../..`), derive `top_root` from that, use
`$kernel_root/tests/console_record_host_test.c` and
`$kernel_root/kernel/inc`, and `cd "$top_root"` before the guarded relative
`kernel/kernel/console.c` searches. Do not run it until the source defects
below are corrected.

The fixed-width UAPI is otherwise correctly shaped: its v1 request is 24
bytes at 0/4/8/16/20, `_IOW` carries that type, unknown ioctls still reach
TTY, the x86 common handler copies request then bounded payload before the
50-ms mutex, checks root, version/flags/reserved/pointer/length, and restricts
the payload to printable ASCII plus one final LF. `511` input then expands to
at most `512` CRLF physical bytes. Normal x86 `consolewrite` (64-byte
postprocessed batches), `consputs`/consoled (32-byte steps), and tty drain
(64-byte steps) do share the sleepable wire path; bypasses are x86-gated and
advance an emergency generation. The RISC-V `#else` output/ioctl paths appear
semantically unchanged, but no compile has proved it. The host-glibc recorder
is one-argv, appends its private LF, opens write-only, and has no source-level
stdout/stderr fallback; automatic `user/programs/*` discovery will stage its
directory name. That name is currently `consolerecord`, not the reviewed
`_consolerecord` contract, and must be made exact (or separately re-reviewed)
before the generated helper is allowed to call it.

There is one immediate correctness blocker: the current post-lock
`!uart_initialized || panic_state()` branch calls
`mutex_unlock(&console_wire_lock)` **twice**. The second unlock would violate
mutex ownership/assert instead of returning the specified pre-emission
`-EAGAIN`; remove exactly the duplicate. Also repair the fake-sink coverage in
the same pre-build patch: it models a contract rather than compiling the
actual console path, so source locks must bind the actual handler/common
dispatch/init/copy-before-lock/emitter paths and recorder silence; add ABI
offset/command checks, 510-printable-plus-LF -> 512-byte CRLF, CR/NUL/multiple
LF, version/flags/reserved/null-pointer, `-EINTR`, post-lock panic/unavailable,
and emergency-during-row cases. Keep the asserted emergency result explicitly
no-credit-after-emission, not a zero-byte claim. Slice 1 has no V2
parser/helper/chunk/time-budget implementation or proof yet and must not be
misrepresented as the end-to-end transport repair. Correct these items, then
run the host fake-sink test once, perform the x86-only build, and obtain a new
independent review before any kernel boot. This verdict remains A1 N=0 with no
HD720, fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or performance credit.

**C1 slice-1 correction battery — FAIL / NO-BUILD / NO-BOOT (2026-07-12):**
exact preflight QEMU total/informational-RISC-V/conflicting inventory was
`0/0/0`; no VM, boot, rootfs refresh, driver/default/KDE action, or recorder
build occurred. The canonical first battery command
`bash kernel/tests/run_console_record_host_test.sh` stopped during host C
compilation before exercising the fake sink or source locks: its new
kernel-root calculation correctly finds `kernel/tests`, but its include order
places `kernel/kernel/inc` ahead of host libc (`<string.h>` resolves to the
freestanding header), while the test's `kernel/inc/dev/console.h` spelling has
no matching include root. Therefore it emitted the `types.h` `typeof` parse
error and then could not locate `dev/console.h`. Per the ordered-battery rule,
there was no correction, rerun, recorder/kernel build, or performance credit;
all unstaged kernel/user/script implementation remains preserved for the next
forensic correction. This is only a host-harness include-path failure and
remains A1 N=0 with no HD720, fullscreen, audio, or responsiveness credit.

**C1 slice-1 host-include forensic — FAIL / NO-BUILD / NO-BOOT
(2026-07-12):** at `f60fc7f`, exact preflight QEMU
total/informational-RISC-V/conflicting inventory was `0/0/0`; no rerun,
compile, test, VM, rootfs, launcher, default, or KDE action occurred in this
review. The failed command is statically sufficient to localize the collision:
its `-I /home/es/xv6-os/kernel/kernel/inc` is searched before system include
directories for `<errno.h>`/`<string.h>`. The latter selects the freestanding
kernel `string.h`, which includes kernel `types.h` and its GNU `typeof` form
under strict host `-std=c11`. This happens before the host branch of the shared
console header can protect the test. The current quoted spelling
`"kernel/inc/dev/console.h"` *does* have a matching root through the command's
`-I /home/es/xv6-os`; the recorded alleged missing-root is not a second defect
of the current source/command.

Do not add a duplicate test-only UAPI or compile `console.c` with host libc
headers. The smallest functional correction is to remove the kernel-inc
`-I`, retaining only the top-root include. The selected robust boundary is
stronger and equally narrow: the host test defines `HOST_LIBC_PROGRAM`,
includes the same shared header as `"dev/console.h"`, and the runner compiles
only that test with `-iquote "$kernel_root/kernel/inc"` (no `-I` kernel-inc or
top-root). `-iquote` serves that quoted shared UAPI header while all `<...>`
headers remain host libc; the header's existing HOST branch supplies
`stdint.h`, `stddef.h`, and `sys/ioctl.h`. Guarded top-root source locks still
run only after the host binary, so this neither weakens the production-path
locks nor turns a host unit into a production-kernel build. A standalone UAPI
move is unnecessary scope unless this dual-mode header later gains a host
dependency.

Latest source inspection finds the earlier double-unlock repaired: the
post-lock unavailable/panic branch sets `-EAGAIN` and the timed acquisition
has one release. The expanded fake model correctly covers ABI offsets/ioctl
encoding, 511-input/512-CRLF output, copy/privilege/field faults, CR/NUL/
multiple-LF, timeout/EINTR, post-lock unavailable, and emergency no-credit
after emission; the revised host-probe staging correctly maps source
`consolerecord` to `/bin/_consolerecord`. Before the one corrected host run,
also add a missing-final-LF negative and source locks for actual root check,
x86 gating, and the single timed-lock release path. The fake remains a model,
not a console.c execution test; retain that distinction and the later x86-only
kernel build/independent-review gates. This remains A1 N=0 with no HD720,
fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or performance credit.

**C1 slice-1 include-boundary battery — PASS / NO-BOOT (2026-07-12):** exact
preflight QEMU total/informational-RISC-V/conflicting inventory was `0/0/0`.
The corrected host runner, using only `-iquote kernel/kernel/inc` for the
quoted shared UAPI while keeping `<...>` host-libc headers, passed its fake
sink and guarded source locks. The fake now includes the missing-final-LF
negative and locks the root euid check, x86 gating, device ioctl registration,
all normal writer gates, and the single post-timed-lock release path. The
automatic host-glibc staging flow placed executable
`/bin/_consolerecord` (and no unprefixed sibling) in a fresh temporary sysroot;
its broader automatic program sweep still emitted unrelated pre-existing
warnings and a nonfatal `kprofile` compile diagnostic, so that result is only
the recorder staging proof, not clean-whole-userland credit. The non-boot
`cmake --build build-x86_64 --target kernel -j2` x86 build and all tracked plus
new-file whitespace diff checks passed. No VM, boot, rootfs refresh, V2
driver/helper work, video/fullscreen/audio measurement, or performance credit
occurred. The required independent adversarial review remains before any
kernel boot; this stays A1 N=0.

**C1 slice-1 committed adversarial review — PASS FOR SLICE-2 SOURCE ONLY /
NO-BUILD / NO-BOOT (2026-07-12):** exact review inventory was
total/informational-RISC-V/conflicting `0/0/0`. Explicit lineages are equal
and published: kernel `b42d1c37f90b2ac48aa416a9eb215a935920f649` equals
`origin/v6-kernel`, user `a95f8c8a6d0dcb9189b6ec6d51a0f376fc721cc1` equals
`origin/v6-port`, and superproject
`2e348407b336f951694392ee7da8330fb099e060` equals the explicit `-ff` origin
branch. Whitespace checks over all three commits pass. Their name-status
confirms no driver, rootfs, launcher/default, extension, KDE, or unrelated
source change: kernel contains only console/UAPI/fake-unit files; user only
the recorder; the parent only submodule pointers, host-probe staging, and
plan. The sole current dirt is the preserved KDE-smoke file.

Direct source audit re-confirms the 24-byte 0/4/8/16/20 fixed ABI and full
command comparison; root check plus request/payload `either_copyin` both
precede the 50-ms sleepable lock. Printable input is limited to 511 with one
terminal LF, and x86 text emission makes exactly at most 512 CRLF physical
bytes; the committed fake covers missing/embedded LF, CR/NUL, ABI/privilege/
copy faults, timeout/EINTR, post-lock unavailable, and emergency no-credit.
The repaired handler has one post-timed-lock release path. Normal x86
consolewrite, consoled/consputs, and tty-drain paths enter the same wire mutex;
the only direct non-console `uartputc_sync` hit is its declaration, and klog
releases its async-ring lock before normal consputs. Early/panic/IRQ/spin-held
paths bypass and advance the generation, so a concurrent emergency returns
no-credit rather than clean-record success. The common cdev/device ioctl
dispatch and mutex initialization are present. The shared host test uses
`-iquote` only for `dev/console.h`, retaining host libc headers and never
compiling production `console.c`; the host-glibc program is one-argv, silent,
write-only, and the reviewed staging maps it to `_consolerecord`. RISC-V
retains the prior `#else` output and TTY ioctl semantics; this is manual
source evidence, not a RISC-V build.

The battery summary itself retains no named log/artifact pathname, so this
review can independently verify its committed runner/test and not replay its
reported execution. Also, a 64-byte consolewrite *input* batch can ONLCR
expand to 128 physical bytes; it remains mutex-covered and below the 50-ms
record wait, but slice-2's transport-time algebra must use that actual normal
segment rather than call it 64 postprocessed bytes. These are provenance/
budget constraints, not a kernel-source blocker. The result authorizes only
the next host-driver V2 parser/generated-helper/time-budget implementation;
it authorizes no additional kernel/user/staging change, build, rootfs refresh,
VM, or boot. Slice-2 must retain the strict V2 binding/gap vectors and a
calculated timeout before another independent review. A1 remains N=0 with no
HD720, fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or performance
credit.

**C1 slice-2 V2 driver static invocation — FAIL / NO-BUILD / NO-BOOT
(2026-07-12):** exact preflight QEMU total/informational-RISC-V/conflicting
inventory was `0/0/0`; no VM, boot, rootfs refresh, kernel/user/default/KDE
change, or performance sample occurred. The one authorized command,
`YT_STATIC_CHECK=1 /usr/bin/expect scripts/gpu/chromium-youtube-presentfps.expect`,
exited `2` immediately with `missing-required-env=YT_MULTIPROCESS`. It did not
reach the new V2 host harness, so this is neither a parser/helper failure nor
a static-suite pass. Per the first-failure rule there was no amended
invocation, rerun, build, or speculative source repair. The uncommitted V2
driver-only edit is preserved for forensic review; A1 remains N=0 with no
HD720, fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or credit.

**C1 slice-2 V2 static forensic — REVISE / NO-RUN (2026-07-12):** exact
preflight QEMU total/informational-RISC-V/conflicting inventory was again
`0/0/0`. The only failed command is the one above; it has no named log or
outdir because validation fails before `YT_OUTDIR`/the default directory is
created. `YT_MULTIPROCESS` is *not* omittable: current validation requires it
and `YT_DISABLE_AUDIO_OUTPUT`; the safe inert host-only configuration is
`YT_STATIC_CHECK=1 YT_MULTIPROCESS=0 YT_DISABLE_AUDIO_OUTPUT=1
YT_EGL_FORENSICS=0 YT_MEDIA_PROBE=0 YT_FORCE_HD720=0
YT_CAPTURE_COMPLETENESS_DIAG=0 YT_WINDOW_SECONDS=60
YT_PLAYER_CROP=960x540+160+130 YT_PLAYER_DELTA_MIN=1000
YT_VIDEO_ID=aqz-KE-bpKQ`, plus a fresh nonexistent `YT_OUTDIR` and
`PATH=/usr/bin:/bin`. Proposed post-fix command only (not run here):
`d=/tmp/yt-c1-v2-static-<fresh>; test ! -e "$d" && env -i PATH=/usr/bin:/bin
YT_STATIC_CHECK=1 YT_MULTIPROCESS=0 YT_DISABLE_AUDIO_OUTPUT=1
YT_EGL_FORENSICS=0 YT_MEDIA_PROBE=0 YT_FORCE_HD720=0
YT_CAPTURE_COMPLETENESS_DIAG=0 YT_WINDOW_SECONDS=60
YT_PLAYER_CROP=960x540+160+130 YT_PLAYER_DELTA_MIN=1000
YT_VIDEO_ID=aqz-KE-bpKQ YT_OUTDIR="$d" /usr/bin/expect
scripts/gpu/chromium-youtube-presentfps.expect`. The static branch exits before
the fs copy/QEMU path; this command has no VM or KDE/GPU action.

The uncommitted V2 edit has no evident Tcl-brace/`format` quoting failure and
its generated script explicitly uses Bash, but it has four source blockers.
First, it accepts an `ordinary-gap` inserted between V2 BEGIN and META; strict
atomic rows require consecutive raw-record numbers for BEGIN/META/all CHUNKs/
END, with bounded gap fingerprints retained only before BEGIN or after END.
The static corpus currently asserts the opposite and must add both allowed
outside-gap and rejected interior-gap vectors. Second, `/bin/wc -c` cannot run
on the current guest `wc` (its source accepts filenames only); restore the
three-field no-option `/bin/wc < "$retained"` validation and check its byte
field. Third, the helper's unqualified `_consolerecord` is discovered only by
the host harness's injected PATH; bind the production generator to
`/bin/_consolerecord` (the reviewed staging target) and make the test stub an
explicit generated-helper test parameter, rather than silently proving PATH
luck. Add EXIT cleanup and assert it after both recorder success and injected
failure. Fourth, the timeout charges only 698 50-ms acquisitions although the
helper emits 698 chunks plus BEGIN, META, and END; charge 701, derive/lock the
outer marker bound instead of magic `64`, and make the static assertion check
those exact terms, the 128-byte ONLCR normal segments, and the final ceiling.

The V2 aggregate currently reports neither a distinct C1-V2 PASS line nor its
case matrix, and its weak substring locks do not bind the no-option `wc`,
absolute recorder route, candidate-contiguity rule, cleanup, or complete
timeout algebra. Correct those source/harness defects first, then obtain an
independent no-boot review before one fresh canonical static run. No edit,
build, rootfs refresh, VM/boot, performance/fullscreen/audio measurement, or
credit occurred in this forensic; A1 remains N=0.

**C1 slice-2 pinned V2 static invocation — FAIL / NO-BUILD / NO-BOOT
(2026-07-12):** exact preflight QEMU total/informational-RISC-V/conflicting
inventory was `0/0/0`. The single fully explicit inert host-only invocation
used the reviewed MP=0/audio=1/optional-arms=0 arguments, a fresh output path,
and `env -i PATH=/usr/bin:/bin`; it exited `2` before any V2 parser/helper
case, fs copy, or QEMU path with `host-tool-missing tool=debugfs`. The static
driver itself requires `debugfs` later in its pre-existing offline reducer,
but this clean PATH does not expose the installed executable. Per the
first-failure rule, there was no PATH amendment, rerun, build, rootfs refresh,
or source repair. The uncommitted V2-only driver edit remains preserved; A1 is
still N=0 with no HD720, fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or
performance credit.

**C1 slice-2 V2 static host-tool/PATH forensic — REVISE / NO-RUN
(2026-07-12):** exact preflight QEMU total/informational-RISC-V/conflicting
inventory was `0/0/0`; no process, test/replay, build, rootfs, VM, boot,
KDE/default, or driver edit occurred. The failure above is fully explained by
ordering and PATH: `/usr/bin/expect` starts the driver; before static dispatch
it resolves `sh` for the repository root, validates the two required and all
selected arm variables, creates the fresh outdir, runs the exact-QEMU
`sh`/`readlink` screen, requires kernel/fsimg, and `auto_execok`s `debugfs`,
`convert`, `compare`, and `nc`. Thus `/usr/bin:/bin` cannot find installed
`debugfs=/usr/sbin/debugfs`. The static branch subsequently really executes
the offline ext2 reducer through `mke2fs=/usr/sbin/mke2fs` and `debugfs`, so
neither of those may be deferred or treated as a QEMU-only tool.

Read-only resolver proof fixes the static host closure at `/usr/bin/sh` and
`/usr/bin/bash` (both Bash), `/usr/bin/env`, `/usr/bin/sha256sum`,
`/usr/bin/{cat,dd,wc,xxd,openssl,rm,mkdir,chmod,readlink,kill}`, and the two
`/usr/sbin` e2fsprogs binaries; every hard-coded `/bin/...` spelling resolves
to its `/usr/bin` counterpart. Static also executes the exact regular
`build-x86_64/sysroot/bin/grep` and local shell PTY fixtures; `od` and `tr`
are only in the pre-static media-nonce branch and are avoided by inert
`YT_MEDIA_PROBE=0`. `convert`, `compare`, and `nc` are currently resolved by
the unconditional pre-static check but are not executed by static; `cp`, QEMU,
monitor, image copy, and runtime `ps`/`awk` paths occur only after static
exit. Guarded `rg`/`safe-rg` is an audit-only conductor dependency, not a
driver/static PATH dependency, and must remain outside the clean invocation.

The next proposed command only (not run here) is
`d=/tmp/yt-c1-v2-static-path-<fresh>; test ! -e "$d" && env -i
PATH=/usr/sbin:/usr/bin:/sbin:/bin YT_STATIC_CHECK=1 YT_MULTIPROCESS=0
YT_DISABLE_AUDIO_OUTPUT=1 YT_EGL_FORENSICS=0 YT_MEDIA_PROBE=0
YT_FORCE_HD720=0 YT_CAPTURE_COMPLETENESS_DIAG=0 YT_WINDOW_SECONDS=60
YT_PLAYER_CROP=960x540+160+130 YT_PLAYER_DELTA_MIN=1000
YT_VIDEO_ID=aqz-KE-bpKQ YT_OUTDIR="$d" /usr/bin/expect
scripts/gpu/chromium-youtube-presentfps.expect`. This pins every required/inert
driver variable and excludes inherited `YT_*`, display, QEMU, and user-PATH
state; MP remains explicitly `0`, never omitted. The conventional four-dir
PATH is deterministic; its observed minimal resolving subset is
`/usr/sbin:/usr/bin`.

Two narrow source items remain before that one rerun. First, safely move only
the kernel/fsimg existence checks and `convert`/`compare`/`nc` availability
checks behind the static exit (or a non-static branch); retain exact-QEMU,
`debugfs`, and static `mke2fs` fail-closed validation, and preserve every
runtime check before its first use. This removes unrelated runtime-artifact
requirements without weakening static or live behavior. Second, the V2 helper
now correctly emits through `recorder=/bin/_consolerecord`, but the older C5
static guard still searches for the obsolete literal `_consolerecord "$1"`;
it will make `static_c5_helper_unchanged` false after the PATH repair. Update
that guard to bind both the default absolute assignment and the variable call,
matching the already stronger V2 source lock. Re-review this narrow ordering/
lock correction, then perform exactly one fresh canonical static suite; no
performance/fullscreen/audio/HD720 credit exists and A1 remains N=0.

**C1 slice-2 V2 post-PATH static attempt — FAIL / NO-BUILD / NO-BOOT
(2026-07-12):** after the prescribed narrow preflight ordering correction
(kernel/fsimg and `convert`/`compare`/`nc` deferred past the static exit, with
exact-QEMU and `debugfs` retained) and the C5 source lock update for
`recorder=/bin/_consolerecord` plus `"$recorder" "$1"`, one fresh canonical
host-only invocation used `env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin`, MP=0,
audio-disable=1, every optional arm inert, the pinned 60-second/crop/delta/
video inputs, and a nonexistent `YT_OUTDIR`. It exited 1 at the existing
capture-completeness static route with `invalid capture-completeness host
envelope arguments` from `capture_completeness_diag_host_envelope`; no
`YT-C1-V2-STATIC-PASS` or overall static PASS was emitted. This is not a VM
launch: exact QEMU inventories immediately before and after were
total/informational-RISC-V/conflicting `0/0/0`. Per the one-suite rule there
was no retry, build, rootfs, boot, kernel/user/default/KDE action, or further
driver edit; the V2 driver patch remains unstaged and A1 stays N=0 with no
HD720, fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or performance credit.

**C1 capture-completeness host-envelope argument forensic — REVISE / NO-RUN
(2026-07-12):** exact forensic QEMU inventory was
total/informational-RISC-V/conflicting `0/0/0`; no process was touched and no
test/replay, build, rootfs, VM, boot, or source/KDE/default action occurred.
The failed outdir is exactly `/tmp/yt-c1-v2-static-path-2774835`; it retains
only the precreated zero-byte `capture-completeness-fbstat.txt` and no named
external log or terminal artifact, because the Tcl error occurs before a
terminal writer. The plan's recorded `invalid capture-completeness host
envelope arguments` is therefore the complete retained failure detail.

The loaded committed parser (`fa0547a`, unchanged in the worktree) defines
`capture_completeness_diag_host_envelope {attempt nonce marker}` and accepts
only three fields: `attempt` in `{initial final}`, a 32-character lowercase
hex nonce, and an alphanumeric marker. The first failing static owned-terminal
route is `static_capture_diag_actual_terminal bootstrap-owner`, which calls
`capture_completeness_diag_owned_terminal`, then
`capture_completeness_diag_terminalize initial ...`. Its exact envelope
arguments are therefore count `3`: `attempt=initial` (valid),
`nonce=off` (invalid: three bytes, not `[0-9a-f]{32}`), and the minted static
diagnostic marker (valid alphanumeric). This is neither an attempt/marker type
error nor a call-count/signature drift.

The cause predates V2: top-level configuration sets `media_probe_nonce=off`
unless `YT_MEDIA_PROBE=1`, while the established canonical static profile uses
MP=1/media=1 and receives a random valid nonce before the static block. The
unstaged V2 diff contains no envelope, terminalize, parser, or nonce call-site
change. Direct capture-completeness static wire/helper fixtures already pass
the fixed 32-hex `static_nonce` and remain well-formed; the same MP0/global-off
fault also awaits the owned-terminal/runtime-fixture paths and the later V3
static terminal/reparse group, which read global `media_probe_nonce`. Live
capturediag cannot take this bad path because its arm contract requires
MP=1/media=1/forced-HD720=1.

The smallest next action is an invocation correction, not an envelope/parser
edit: use one fresh canonical host-only static suite with
`YT_STATIC_CHECK=1 YT_MULTIPROCESS=1 YT_DISABLE_AUDIO_OUTPUT=1
YT_EGL_FORENSICS=0 YT_MEDIA_PROBE=1 YT_FORCE_HD720=0
YT_CAPTURE_COMPLETENESS_DIAG=0`, the already pinned PATH/window/crop/delta/
video values, and a nonexistent `YT_OUTDIR`. It only generates the host nonce
and runs static fixtures; it does not boot or create QEMU. Before that one run,
add a narrow static source assertion that reports the three envelope arguments
and requires a 32-hex `media_probe_nonce` wherever static owned-terminal or V3
fixtures consume the global, while preserving direct fixtures' `static_nonce`.
Do not change the three-argument parser API or relax the nonce check. This is
no performance/fullscreen/audio/HD720 credit; A1 remains N=0.

**C1 V2 canonical-static-profile attempt — FAIL / NO-BUILD / NO-BOOT
(2026-07-12):** after `e773716`, exactly one fresh host-only canonical suite
used `env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin`, MP=1, audio-disable=1,
media=1, forced-HD720=0, EGL=0, capturediag=0, and the pinned window/crop/
delta/video inputs with a nonexistent output directory. The new static-only
global-nonce assertion boundedly recorded each V1/V3 fixture's three relevant
arguments and required a lowercase 32-hex current nonce before its existing
terminal invocation; the old `nonce=off` envelope exception did not recur.
The replay reached the capture-completeness aggregate predicates, then exited
2 at the later V2 actual-helper binary case:
`static-check-fbstat-v2-helper-binary-exit output=child process exited
abnormally`. It emitted neither `YT-C1-V2-STATIC-PASS` nor the overall static
PASS marker, so no V2/parser/helper, performance, HD720, audio, fullscreen,
`yt-presentfps`, or `PERF-VIDEO` credit is claimed. Exact QEMU inventories
immediately before and after were total/informational-RISC-V/conflicting
`0/0/0`; no VM, build, rootfs, boot, kernel/user/default/KDE action, or retry
occurred. Per the single-suite rule the driver patch remains unstaged and the
unrelated KDE-smoke dirt remains untouched; a new forensic authority is needed
before any V2 helper repair or another static run.

**C1 V2 actual-helper binary-exit forensic — REVISE / NO-RUN
(2026-07-12):** exact no-boot inventory was
total/informational-RISC-V/conflicting `0/0/0`; no process, test/replay,
build, rootfs, VM, boot, KDE/default, or source edit occurred. The failed
canonical outdir is exactly `/tmp/yt-c1-v2-canonical-static-2782738`; its
generated helper/stub directory retains only executable `fbs.sh` (3630 B),
`fbstat` (44 B), `_consolerecord` (305 B), and `payload.bin`. The last file is
the binary case itself: `wc -c=4`, SHA-256
`26f9f592b06d4c3c0dd2116492991fff5e4f013a884de18a0c7261ea85de931d`, exact
bytes `00 ff 0a 7f`. There is no `records.txt` or `count`, and the exact
`/dev/shm/yt-fbstat-probe1-fbfstatstaticnonce-*` residual count is zero.

This proves only a pre-recorder nonzero helper exit. The production-generated
script receives ASCII `tag=probe1`/`nonce=fbfstatstaticnonce`; the raw fixture
is written binary to a named file, the host `fbstat` stub only runs
`cat "$YT_FBSTAT_STATIC_PAYLOAD"` into a redirected capture file, and V2 would
hex-encode that file before constructing any recorder argv. Thus neither an
argv NUL limit, shell text encoding, overlong V2 record, nor recorder-stub
failure explains the retained absence of rows. The empty case had already
passed, while the four-byte binary case can only have failed before its BEGIN
record (the binary-dependent capture/count/digest/xxd/hex-length region,
including a possible stub/tool failure); the retained evidence cannot select
one exit branch or distinguish that branch from host-tool emulation.

The exact child exit code/signal and child stdout/stderr are unrecoverable
from this one attempt: `static_fb_v2_helper_run` used `catch {exec ... 2>@1}
output` without the Tcl options dictionary, then returned only success, rows,
and `output`; Tcl retained only `child process exited abnormally`. No terminal
or external child log exists. This is a static-harness reporter loss, not
evidence to alter production helper logic. Related actual-helper audit: media
captures `-errorcode`; C8 captures it in its runner but its actual-case record
stores only `run_error=[lindex $run 2]` (child output) and drops the third
errorcode field; the render-role actual helper likewise returns output without
Tcl child status. C8's own distinct binary fixture is 14 bytes and was not
reached by this failed suite, so it is no confirmation or counterexample.

Smallest next source change, subject to adversarial review, is static-harness
only: make the V2 runner catch into `output options`, retain a bounded
stdout/stderr token plus `-errorcode` (distinguishing `CHILDKSTATUS` exit from
`CHILDKILLED` signal), rows/count presence, and cleanup result in the failed
case diagnostic; thread the already captured C8 errorcode into its case
record, and give the render-role runner the same bounded status. Do not change
the production helper, stub behavior, parser, shell invocation, or defaults.
After that review, authorize at most one fresh canonical static suite; it must
report this per-case diagnostic before any helper repair decision. No V2,
performance, HD720/fullscreen, audio, `yt-presentfps`, or `PERF-VIDEO` credit
exists; A1 remains N=0.

**C1 V2 observer-only actual-helper reporter patch — READY / NO-RUN
(2026-07-12):** the pending, unstaged driver-only observer hunk is restricted
to the host static actual-helper runners; it makes no new change to the
production generated V2 helper/parser/protocol, kernel/user code, rootfs,
defaults, KDE, or QEMU command. V2 now redirects each helper child's combined
stdout/stderr to a per-case capped artifact, catches Tcl's options dictionary,
retains the exact `-errorcode`, and renders `CHILDKSTATUS` exit separately from
`CHILDKILLED` signal. Its failure record compactly includes all five named
actual cases (`empty`, `binary`, `multiline`, `exact_cap`, `overcap`), with
not-run status where an earlier fatal case stops the suite; each executed case
keeps input size/SHA, helper SHA, sanitized argv, bounded child-I/O metadata,
bounded stub-row-file/count/first/last-row metadata, and cleanup/residue
state. The C8 actual-case record now retains its exit/signal/errorcode and
bounded child-I/O artifact, while the render-role runner exposes the same
bounded termination status in its aggregate failure diagnostic. `git diff
--check` is clean, but no Tcl/static suite, build, test, VM, boot, serial
command, or QEMU has been run, so this is observer readiness only—not a
diagnostic verdict or any V2/performance/HD720/fullscreen/audio credit. The
next action still requires adversarial review and fresh authority for exactly
one canonical host-only static suite.

**C1 V2/C8/render observer-patch adversarial review — FAIL / NO-RUN
(2026-07-12):** direct review at `558d57b0f8cdd0465f918472312f4acbd567315c`
found that HEAD and the required explicit
`origin/codex/host-linux-abi-shell-port-ff` are equal at that commit. The
local tracking upstream remains the historical truncated ref; its advertised
tip happens also to equal `558d57b` today. It was read only: no ref was pushed
or otherwise changed. Exact QEMU inventory was total/informational-RISC-V/
conflicting `0/0/0`; no test/replay, build, rootfs, VM, boot, serial, source,
KDE/default, or process action occurred. The V2 driver and unrelated KDE-smoke
files remain the only dirt.

The new status parsing is otherwise sound and static-only: `catch` keeps the
Tcl options dictionary, `CHILDKSTATUS` maps to its exit field,
`CHILDKILLED` to its signal field, and other errorcodes remain explicit;
combined stdout/stderr is redirected to a per-case artifact without a pipeline
that could mask child status. C1 input/output/row artifacts are uniquely named
per case, C8 preserves source/frame/wire/child-output artifacts before the
shared source is overwritten, and render-role output paths are tag-specific.
Terminal diagnostics contain only bounded hex summaries, hashes, sanitized
argv, and bounded row metadata. The observer hunks lie inside the
`YT_STATIC_CHECK` block; direct scope audit finds no observer edit to the
production generated V2 helper, V2 parser/protocol, timeout algebra, C5/C7,
or runtime credit/aggregate paths. The retained pre-observer generated helper
is consistent with the current production template; no production semantic
change is licensed by this report.

The C1 wrapper is nevertheless not observational. `ulimit -f 128` is inherited
by the generated helper, `fbstat` stub, and recorder; on this host that is a
64-KiB file ceiling. It caps the helper's 131072-byte exact-cap capture and
retained payload before its intended transport logic, and also caps the
roughly 357-KiB maximum exact-cap recorder rows (701 records). Thus it would
manufacture a nonzero/SIGXFSZ-style helper failure for valid `exact_cap` and
`overcap` cases, defeating the stated no-production/no-semantics observer
rule. The C1 runner also snapshots and copies `records.txt` but does not
preserve the independently overwritten `$dir/count` file per case; its derived
row count is useful but not the requested count-file artifact. These are
blocking defects, so no static suite may run.

C8's 256-KiB inherited ceiling exceeds its 16-KiB source/32-KiB encoded-frame
contract, and render-role's 64-KiB ceiling exceeds its capped 4-KiB evidence
and small state output; neither changes their reviewed static cases. Their
error/status, bounded-I/O, cleanup, and no-credit reporting are adequate,
though C8 should print its explicit `output_acceptable` bit in the compact
record for clarity. The next smallest source change is static harness only:
remove the C1 inherited `ulimit -f` wrapper (keep post-exit bounded snapshots
and the generated helper's own existing payload/row caps), persist a unique
per-case count-file artifact/snapshot before deletion, and include its bounded
metadata in the C1 compact diagnostic. Retain C8/render code unless that
clarity bit is accepted. Then re-review and authorize at most one fresh
canonical host-only suite. This FAIL grants no V2/parser/helper, A1,
performance, HD720/fullscreen, audio, `yt-presentfps`, or `PERF-VIDEO` credit.

**C1 V2 observer-review remediation — READY / NO-RUN (2026-07-12):** the
pending, unstaged static-harness-only correction changes the C1 actual-helper
wrapper's inherited file limit from 128 to 2048 512-byte blocks (1 MiB). A
source/static arithmetic guard locks that value against the generated helper's
999999-byte raw-capture guard, the 131072-byte valid payload cap plus the
one-byte over-cap fixture, and the calculated 357781-byte inner CRLF
recorder envelope; the wrapper remains the finite capture/output bound, so it
cannot manufacture the prior 64-KiB valid-case failure or allow unbounded
child artifacts. Each C1 case now preserves `$dir/count` before the next
case's cleanup as `actual-<case>.count.txt`, with source-file and copied-
artifact bounded metadata (or explicit absent state) alongside its existing
row artifact/snapshot. C8/render, the production V2 helper/parser/protocol,
timeout algebra, kernel/user/rootfs/default/KDE paths, and runtime aggregates
are untouched. `git diff --check` is clean; no static suite, test, build,
rootfs, VM, boot, serial command, or QEMU ran. This is repair readiness only,
not an observer verdict or any C1/V2/A1/HD720/fullscreen/audio/performance
credit; a renewed adversarial review remains required before one canonical
host-only suite.

**C1 V2 observer-remediation adversarial review — PASS / NO-RUN
(2026-07-12):** at explicit `-ff` HEAD
`8954b05e22f2620373e9ee41e26c1a7454194a74`, exact QEMU inventory was
total/informational-RISC-V/conflicting `0/0/0`; no test/replay, build, rootfs,
VM, boot, serial, source, KDE/default, or process action occurred. The
unstaged observer change remains inside `YT_STATIC_CHECK`: it does not alter
the production V2 helper/parser/protocol, timeout algebra, runtime aggregate
or credit path, C8/render observers, kernel/user/rootfs/defaults, or QEMU.
The current generated V2 source still has the reviewed `raw_bytes <= 999999`
guard, 131072-byte transport cap, and 357781-byte inner CRLF recorder
envelope.

The C1 wrapper's `ulimit -f 2048` is 2048 512-byte blocks = 1048576 bytes,
strictly above the 999999-byte raw capture, 131072-byte valid input,
131073-byte over-cap fixture, and 357781-byte inner CRLF recorder envelope while still
bounding each child-created capture, rows, and combined-output file. Each
named case now snapshots `$dir/count` before the next case's cleanup, copies it
to unique `actual-<case>.count.txt`, and reports bounded source and copied
artifact metadata with explicit absent/regular/byte/first/last state; expected
pre-recorder failure remains an honest absent-count record. The existing
options-dictionary logic still distinguishes `CHILDKSTATUS`, `CHILDKILLED`,
and other errorcodes without a pipeline masking status; diagnostics contain
only bounded hex/hash/path-sanitized metadata. C8's 256-KiB and render-role's
64-KiB static bounds remain within their respective reviewed contracts and
are unchanged.

This PASS authorizes exactly one fresh canonical **host-only** static suite:
clean conventional PATH, `YT_STATIC_CHECK=1`, MP=1, audio-disable=1, media=1,
forced-HD720=0, EGL=0, capturediag=0, pinned window/crop/delta/video inputs,
and a nonexistent `YT_OUTDIR`. It is not a VM/build/rootfs/KDE/default action
and grants no V2/A1/performance/HD720/fullscreen/audio/`yt-presentfps`/`PERF-VIDEO`
credit unless that one suite actually passes. Preserve its per-case artifacts
and report the first honest failure; do not retry.

**C1 V2 observer-remediation canonical static suite — FAIL / NO-BUILD /
NO-BOOT (2026-07-12):** the one review-authorized clean host-only invocation
used `env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin`, static=1, MP=1,
audio-disable=1, EGL=0, media=1, forced-HD720=0, capturediag=0, the pinned
60-second/window-crop/delta/video inputs, and a fresh nonexistent outdir
`/tmp/yt-c1-v2-observer-static-20260712T223030Z-2850189` (external log
`/tmp/yt-c1-v2-observer-static-20260712T223030Z-2850189.log`). It exited 2
after the pre-existing capture-completeness static predicates and before any
C1 actual-helper case: `static-check-fbstat-v2-helper-observer-file-bound
blocks=2048 bytes=1048576 max_capture=999999 max_input=131072
fixture_input=131073 max_rows=357781`.

The failure is a static assertion expectation error, not a helper result: the
new observer guard required `static_fb_timeout physical_bytes == 357812`, but
the same reviewed timeout contract returned its inner recorder-row value
`357781`; the plan must not silently equate or infer that value from separately
accounted envelope terms. The generated static helper/stubs were written, but
there are zero `actual-*` V2 input/output/row/count artifacts and zero matching
`/dev/shm/yt-fbstat-probe1-fbfstatstaticnonce-*` residue, so no child
errorcode, recorder count, or binary-case conclusion exists. This single suite
is consumed: no source correction, retry, build, rootfs, VM, boot, serial,
kernel/user/default/KDE action, or performance/HD720/fullscreen/audio credit
followed. Exact QEMU inventories immediately before and after were
total/informational-RISC-V/conflicting `0/0/0`; no QEMU was launched or
touched. The driver remains unstaged and the unrelated KDE-smoke dirt remains
untouched. Next authority is limited to correcting and adversarially reviewing
the static observer arithmetic before any further suite.

**C1 V2 observer file-bound arithmetic forensic — REVISE / NO-RUN
(2026-07-12):** exact forensic inventory was
total/informational-RISC-V/conflicting `0/0/0`; no test/replay, build, rootfs,
VM, boot, serial, source, KDE/default, or process action occurred. The failed
outdir and external log are exactly
`/tmp/yt-c1-v2-observer-static-20260712T223030Z-2850189` and
`/tmp/yt-c1-v2-observer-static-20260712T223030Z-2850189.log`. They retain the
generated helper/stubs but no `actual-*` C1 artifact or matching `/dev/shm`
residue, confirming the plan's failure was pre-helper.

First-principles maximum-row arithmetic is: cap `131072`, lower hex `262144`,
chunk width `376`, 697 full chunks plus one 72-hex final chunk, hence 698
chunks and 701 total records. With max tag/nonce/digest lengths `16/32/64`,
the exact logical bytes are BEGIN `205`, META `296`, full CHUNK `510`, final
CHUNK `205`, and END `203`. Console CRLF physical bytes are therefore
`697*512 + 207 + 207 + 298 + 205 = 357781`. The static recorder stub writes
LF, not CRLF, so its rows file is `357781 - 701 = 357080` bytes. Both are
below the 1-MiB (2048 512-byte-block) rlimit and below the valid 999999-byte
raw-capture guard; the 131073-byte over-cap fixture is likewise safe.

The rejected `357812` is exactly `357781 + 31`: 31 is the outer RC/FENCE CRLF
cost for a stale eight-byte marker (`15 + 16`), not an inner recorder row
cost. The actual static marker `YTFBSTATSTATICC5` is 16 bytes, so its outer
cost is 47; the timeout contract correctly keeps that marker-dependent 47 and
the separate 128-byte normal-writer allowance outside `physical_bytes`, for a
static timed-wire total of `357956`, `wire_ms=31073`, `lock_ms=35050`, reserve
`5000`, and timeout `72` seconds. Do not fold any outer/normal term into the
child-file bound and do not weaken this timeout algebra.

The only source mismatch is the observer comment/assertion requiring
`physical_bytes == 357812`; prior plan prose repeats that stale number. The
timeout formula itself, chunk/count constants, lock count, and runtime caller
are internally consistent, but its static check should additionally pin
`physical_bytes=357781`, static `outer_physical_bytes=47`, `wire_ms=31073`,
and `seconds=72`. The smallest correction is observer-only: name the current
`physical_bytes` value a conservative recorder-wire bound and compare it to
`357781` and the 1-MiB limit; optionally derive/assert the distinct static
rows-file bound `physical_bytes-record_count=357080`. Retain `999999` as the
valid raw-capture guard, and leave production helper/parser/protocol/timeout
behavior unchanged. A new adversarial review is required before any new
canonical host-only suite; no V2/A1/performance/HD720/fullscreen/audio credit
exists.

**C1 V2 observer arithmetic correction — READY / NO-RUN (2026-07-12):** the
pending, unstaged static-only hunk now names `physical_bytes` as the inner CRLF
recorder bound and pins it to `357781`; it separately derives the static
LF-row-file bound `357080` (`physical_bytes - 701`). The pre-existing timeout
contract remains behaviorally unchanged but its static source lock now
explicitly retains the current 16-byte-marker outer cost `47`, normal writer
`128`, timed-wire total `357956`, `wire_ms=31073`, `lock_ms=35050`, reserve
`5000`, and timeout `72`. Historical explanatory prose above now distinguishes
these inner and separately accounted terms; the failed-suite evidence itself
continues to record the rejected `357812` assertion. No helper/parser/protocol,
runtime timeout, C8/render, kernel/user/rootfs/default/KDE, build, test,
serial, VM, boot, or QEMU action occurred. This is correction readiness only:
the driver remains unstaged and a new adversarial review is required before
any further canonical static suite or credit.

**C1 V2 observer arithmetic correction adversarial review — PASS / NO-RUN
(2026-07-12):** at plan HEAD `d7483a6c7f541cc27707fc3b546f47a6977d373c`,
direct static-only source inspection and an independent literal calculation
confirm cap/hex/chunk/record values `131072/262144/376/698/701`: 697 full
512-byte CRLF rows, one 207-byte final row, and 207/298/205-byte
BEGIN/META/END rows total exactly `357781` inner recorder bytes. The static
stub writes LF, so its rows file is exactly `357080` bytes. Both are below the
2048-block `ulimit -f` bound of `1048576` bytes; the `999999` raw-capture guard,
131072-byte valid input, and 131073-byte over-cap fixture are separately below
that child-file limit. The 16-byte `YTFBSTATSTATICC5` marker contributes its
separate outer `47` bytes, not file bytes; with normal-writer `128`, timed
wire is `357956`, `wire_ms=31073`, `lock_ms=35050`, reserve `5000`, and the
unchanged timeout is `72` seconds. The failed suite's historical `357812`
assertion remains recorded as the stale eight-byte-marker outer-frame mixup.

The correction is confined to `YT_STATIC_CHECK` observer assertions/comments
and leaves the production timeout contract, generated helper, parser, and V2
protocol unchanged. Exact current `/proc/*/exe` inventory was
total/informational-RISC-V/conflicting `1/1/0`; the independent RISC-V QEMU was
not touched. No suite, build, rootfs, VM, serial, KDE, or source-runtime action
ran. This PASS authorizes exactly one fresh canonical host-only static suite
with clean conventional PATH, `YT_STATIC_CHECK=1`, `YT_MULTIPROCESS=1`,
`YT_DISABLE_AUDIO_OUTPUT=1`, `YT_EGL_FORENSICS=0`, `YT_MEDIA_PROBE=1`,
`YT_FORCE_HD720=0`, `YT_CAPTURE_COMPLETENESS_DIAG=0`, pinned
window/crop/delta/video inputs, and a nonexistent `YT_OUTDIR`; it is no VM or
performance authority, and any first result must be retained without retry.

**C1 V2 observer arithmetic canonical static suite — FAIL / NO-BUILD /
NO-BOOT (2026-07-12):** the one review-authorized host-only command used the
exact conventional clean PATH, static=1, MP=1, audio-disable=1, media=1,
forced-HD720=0, EGL=0, capturediag=0, pinned window/crop/delta/video inputs,
and fresh outdir `/tmp/yt-c1-v2-observer-static-20260712T224813Z-2865831`
(external log `/tmp/yt-c1-v2-observer-static-20260712T224813Z-2865831.log`).
It exited 2 after the capture-completeness predicates, without either static
PASS marker. The C1 actual-helper `empty` case passed (three recorder rows,
count artifact `3`); `binary` then failed before any rows or count file with
empty bounded child output and Tcl errorcode `CHILDSTATUS 2866224 68`.
Its compact observer record has `exit=unavailable`, so the retained raw
errorcode—not a claimed decoded exit field—is the exact termination evidence.
The named binary input artifact is 4 bytes with SHA-256
`26f9f592b06d4c3c0dd2116492991fff5e4f013a884de18a0c7261ea85de931d`, while
the terminal observer reports `input=6` for that same digest; that accounting
discrepancy is recorded, not explained. `multiline`, `exact_cap`, and
`overcap` were not run; matching `/dev/shm` residue count is zero.

No retry, driver correction, build, rootfs, boot, kernel/user/default/KDE, or
VM action followed; the observer driver remains unstaged and the unrelated
KDE-smoke dirt remains untouched. Exact pre/post `/proc/*/exe` inventories
were total/informational-RISC-V/conflicting `1/1/0`: the unrelated RISC-V QEMU
PID 2861796 was only observed and never touched. This is no V2/A1,
performance, HD720, audio, fullscreen, `yt-presentfps`, or `PERF-VIDEO`
credit. The next authority is no-boot forensic of the binary helper's raw
exit 68 and the observer's binary byte accounting before any source change or
fresh static suite.

**C1 V2 binary exit-68/observer discrepancy forensic — ROOT CAUSE LOCALIZED /
NO-RUN (2026-07-12):** at `f91be1874f180958f75abc098ede2dc76eafad73`, named
artifacts prove the binary fixture is exactly four regular-file bytes
`00 ff 0a 7f`, SHA-256
`26f9f592b06d4c3c0dd2116492991fff5e4f013a884de18a0c7261ea85de931d`; both
`actual-binary.input.bin` and the helper payload file agree. The generated
helper, stub `fbstat`, and recorder are executable, the binary input/payload
files are readable, binary child output is empty, and cleanup leaves zero
matching `/dev/shm` paths. Empty already proved the same PATH/stub setup and
the recorder route (three rows/count 3). Thus no NUL argv, filename,
permission, file-limit, recorder, SHA-256, or `xxd` failure explains the
binary stop. The latter tools occur only after the first count branch and were
not executed for this case.

The exact branch is helper exit 68: no-option `wc < "$capture"` has three
fields. Both the host static invocation and the guest `wc.c` contract give the
binary stream `1 2 4` (lines/words/bytes; guest adds only an ignored empty name
field). With globbing disabled, `set -- $count_output` therefore makes `$3=4`.
The C1 helper's `valid_decimal` case `0|[1-9][0-9]*` is not an anchored decimal
grammar: its second class requires a second character, while `*` then matches
arbitrary suffixes. It rejects one-digit `4`, causing exit 68 before `dd`,
digest, hex, or any BEGIN record. Empty is `0 0 0`, so its `$3=0` special case
passes. Multiline `3 3 19` passes the first byte check but would fail exit 71
on the retained line count `3`; exact-cap `0 1 131072` and over-cap's retained
`0 1 131072` would likewise fail exit 71 on word count `1`. The helper's exit
map is: 64 parameter, 65 stale scratch, 66 `fbstat`, 67 capture type/read,
68 initial count/cap, 69 `dd`, 70 retained type/read, 71 retained count/size,
72 digest grammar/path, 73 `xxd`, 74 hex-line grammar, 75 hex length, 76 row
size, 77 recorder, 78 chunk shape, and 79 final chunk count.

The observer's `exit=unavailable` is a separate static-only typo: Tcl reports
`CHILDSTATUS <pid> 68`, but `static_fb_v2_child_status` tests
`CHILDKSTATUS`, so its three-element list never assigns index 2 to
`exit_code`. Its `input=6` is also observer-only: the fixture is a Tcl
`binary format H*` object and `string bytelength $payload` UTF-8-expands `FF`,
whereas `static_fb_v2_write_binary` correctly writes the four raw bytes whose
file SHA is reported. The compact record must take input size from the input
artifact's `file size` (and static-lock its exact binary hex/SHA), not the Tcl
string representation. `sha256_file` uses that artifact through host
`sha256sum`; it is not the source of the discrepancy.

The smallest production repair is confined to the C1 V2 generated helper:
replace only its decimal predicate with the already reviewed Bash-anchored
`[[ "$1" =~ ^(0|[1-9][0-9]*)$ ]]` form and require all three initial no-option
`wc` fields before accepting `$3`. The observer-only companion repair is
`CHILDSTATUS` exit decoding plus artifact-size reporting and binary
hex/SHA/size assertions. Do not silently widen C7/C8. The V2 host decoder also
uses `string bytelength` after `binary decode hex`; it was not reached in this
suite, so this forensic does not claim a second failure, but a future binary
review must prove raw-byte equality by re-encoded lower hex (as C8 does) before
using another canonical suite. No source edit, test, build, rootfs, VM, serial,
KDE, or QEMU action occurred; exact inventories were total/informational-
RISC-V/conflicting `1/1/0`, and the RISC-V process was untouched. This grants
no retry or performance credit.

**C1 V2 wc/observer/binary-decoder correction — READY / NO-RUN
(2026-07-12):** at forensic checkpoint
`98a4a56549bde12ac4624ca647eadbc7f2bb3ed1`, the pending unstaged change is
limited to `scripts/gpu/chromium-youtube-presentfps.expect`; no test, build,
rootfs, VM, boot, serial, kernel/user/default/KDE, or QEMU action occurred.
Exact pre-edit `/proc/*/exe` inventory was total/informational-RISC-V/
conflicting `0/0/0`.

The generated C1 V2 helper now obtains its sole decimal predicate from a
Tcl-braced shared Bash literal, `[[ "$1" =~ ^(0|[1-9][0-9]*)$ ]]`; there is no
production test hook. Both no-option `/bin/wc < file` triples validate all
three canonical decimal fields before their byte-count semantics, repairing
the binary fixture's one-digit `4` rejection without widening C7 or C8.
The C1 static observer now recognizes Tcl `CHILDSTATUS <pid> <status>` while
retaining `CHILDKILLED`/other reporting, and gets input size from the written
input artifact snapshot. Its binary fixture is source-locked as exact
`00ff0a7f`, file size 4, and SHA-256
`26f9f592b06d4c3c0dd2116492991fff5e4f013a884de18a0c7261ea85de931d`.

The C1 V2 host decoder's post-`binary decode hex` check had a second latent
Unicode-sensitive `string bytelength` use. It now re-encodes to lower hex and
requires exact hex text/length equality; its existing `wb`, binary-translation,
binary-encoding temporary-file SHA-256 route is explicitly source-locked.
C1-only static source locks and future fixture assertions cover the shared
decimal literal/matrix, all-three-field wc guards, exact binary artifact, and
binary host decode. Protocol rows, timeout/chunk algebra, C5/C7/C8 behavior,
and all runtime/credit paths are untouched. `git diff --check` is clean. This
is readiness only: the driver remains unstaged and requires independent
adversarial no-boot review before any fresh static suite; no V2/A1/HD720,
audio, fullscreen, `yt-presentfps`, `PERF-VIDEO`, or performance credit is
claimed.

**C1 V2 wc/observer/binary-decoder adversarial review — FAIL / NO-RUN
(2026-07-12):** direct source review at
`fc2fca74ab38885793ca9d0c957b3aaf4a4acb27` confirms the C1-only generated
Bash literal is Tcl-braced and emits exactly
`[[ "$1" =~ ^(0|[1-9][0-9]*)$ ]]`; both no-option `wc` triples now check all
three fields, no production test hook exists, and C7/C8 helpers retain their
separate existing implementations. The V2 decoder correctly replaces its
Unicode-sensitive payload `string bytelength` test with lower-hex
re-encode/length/text equality, while retaining its existing `wb`, binary
translation/encoding digest path. The exact four-byte binary fixture, its
full hex/SHA/size checks, bounded input snapshot, 1-MiB observer limit,
protocol rows, 376-byte chunks, timeout algebra, and no-credit/runtime paths
remain correctly scoped; no kernel, user, rootfs, default, or KDE file is
touched.

The observer status repair itself is nevertheless wrong. Tcl emits the exact
three-element exit errorcode `CHILDSTATUS <pid> <status>` (also used correctly
by the existing debugfs helper), but `static_fb_v2_child_status` still tests
the misspelled `CHILDKSTATUS`; it will again leave an ordinary nonzero child
with `exit=unavailable`. Its `CHILDKILLED` signal branch and explicit
other-errorcode retention do not repair that exit-status loss, and there is no
meaningful static assertion of the `CHILDSTATUS` mapping. Thus the observer
cannot honestly report the next first helper failure and must not authorize a
fresh suite. The smallest correction is static-observer-only: replace that
one selector with exact `CHILDSTATUS`, then add source/static checks for a
three-element exit code, `CHILDKILLED`, and an unrelated errorcode before a
new adversarial review. Do not alter the production helper/parser/protocol or
run the consumed canonical suite. No test, build, rootfs, VM, serial, KDE, or
QEMU action occurred; exact inventory was total/informational-RISC-V/
conflicting `1/1/0`, and the RISC-V QEMU was untouched. This FAIL grants no
performance or retry credit.

**C1 V2 observer `CHILDSTATUS` correction — READY / NO-RUN (2026-07-12):**
the pending driver-only correction replaces the static observer's sole
misspelled selector with Tcl's exact `CHILDSTATUS`; guarded source and pending
diff audits now find zero `CHILDKSTATUS` occurrences. It adds no child
execution: pure in-memory static fixtures require `CHILDSTATUS 4242 68` to
report exit 68/no signal, `CHILDKILLED 4243 SIGTERM observer-test` to report
unavailable exit/SIGTERM, and `POSIX EACCES observer-test` to retain the other
errorcode with unavailable exit/no signal. The fixture matrix is source-locked
to both recognized Tcl forms and is part of the C1 static aggregate.

No production helper/parser/protocol, timeout/chunk algebra, credit path,
kernel/user/rootfs/default/KDE path, test, build, VM, boot, serial, or QEMU
action occurred. Exact pre-edit inventory was total/informational-RISC-V/
conflicting `0/0/0`; no process was touched. `git diff --check` is clean; the
driver remains unstaged. This is readiness only and requires independent
adversarial no-boot review before any new static suite, with no V2/A1/HD720,
audio, fullscreen, `yt-presentfps`, `PERF-VIDEO`, or performance credit.

**C1 V2 observer-status correction adversarial re-review — PASS / NO-RUN
(2026-07-12):** at source checkpoint
`80aabb0caa50a7103b24f394428eec13109035ec`, guarded source audit finds zero
`CHILDKSTATUS` occurrences. `static_fb_v2_child_status` now parses exactly
three-element `CHILDSTATUS <pid> <status>` into `exit_code`, parses
`CHILDKILLED` into its signal field, and retains unrelated errorcodes with an
unavailable exit/no signal. Its pure in-memory Tcl fixtures use list-safe
`CHILDSTATUS 4242 68`, `CHILDKILLED 4243 SIGTERM observer-test`, and
`POSIX EACCES observer-test` values; no fixture starts a child. The matrix is
both an immediate static preflight and a required member of the C1 V2 and
terminal static aggregates, all inside the `YT_STATIC_CHECK=1` branch, so it
cannot supply production or performance credit.

The prior C1 V2 production checks remain intact: the shared anchored Bash
decimal predicate validates all three fields of both no-option `wc` triples,
the binary decoder verifies lower-hex re-encoding rather than Tcl byte length,
and the four-byte binary input snapshot remains source-locked to its exact
hex/SHA. No test, build, rootfs, VM, boot, serial, KDE/default, or QEMU action
occurred; exact `/proc/*/exe` inventory was total/informational-RISC-V/
conflicting `0/0/0`. This PASS authorizes exactly one fresh canonical clean-
PATH host-only static suite with `YT_STATIC_CHECK=1`, MP=1,
audio-disable=1, media=1, forced-HD720=0, EGL=0, capturediag=0, pinned
window/crop/delta/video inputs, and a nonexistent `YT_OUTDIR`. Retain its
first result and artifacts without retry. It is no VM, HD720/fullscreen,
audio-on, `yt-presentfps`, `PERF-VIDEO`, or performance authority.

**C1 V2 observer-status canonical static suite — FAIL / NO-BUILD / NO-BOOT
(2026-07-12):** the one review-authorized clean host-only invocation used
`env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin`, static=1, MP=1,
audio-disable=1, media=1, forced-HD720=0, EGL=0, capturediag=0, pinned
window/crop/delta/video inputs, and fresh outdir
`/tmp/yt-c1-v2-observer-static-20260712T231632Z-2888896` (external log
`/tmp/yt-c1-v2-observer-static-20260712T231632Z-2888896.log`). It exited 1
after the capture-completeness predicates and after all named C1 actual-helper
artifacts were written, without either static PASS marker.

The C1 binary artifact is retained at exactly 4 bytes; empty, binary,
multiline, exact-cap, over-cap, and injected recorder-failure cases each have
bounded input/output/row/count artifacts, and matching `/dev/shm` residue is
zero. The first terminal failure is later and exact: static generated
render-role capture rejects its `baseline` output despite a present regular
151-byte artifact, `static-render-role-output-baseline-2888898`, whose output
begins `YT_RENDER_START_ROLE_TEST_STATE none_or_exited total=103 bytes=103
truncated=0 digest=...`; bounded child message is empty. The retained error is
`static generated helper role-capture output invalid tag=baseline`; no
production-helper, C1 binary-decoder, or render-role cause is inferred from
that static observer failure.

No retry, driver correction, build, rootfs, VM, boot, serial, kernel/user/
default/KDE action followed. Exact pre/post `/proc/*/exe` inventories were
total/informational-RISC-V/conflicting `0/0/0`; no QEMU was launched or
touched. The driver remains unstaged and unrelated KDE-smoke dirt is
untouched. This is no V2/A1, performance, HD720, audio, fullscreen,
`yt-presentfps`, or `PERF-VIDEO` credit. Next authority is limited to no-boot
forensic of the static render-role baseline-output contract before any source
change or fresh static suite.

**Static render-role baseline-output forensic — ROOT CAUSE LOCALIZED / NO-RUN
(2026-07-12):** at plan HEAD
`a3cd0a2bfd93180ccbd55e4a988f653c6b5c165b`, exact inventories before this
read-only forensic were total/informational-RISC-V/conflicting `0/0/0`.
The named outdir and external log from the consumed suite are
`/tmp/yt-c1-v2-observer-static-20260712T231632Z-2888896` and
`/tmp/yt-c1-v2-observer-static-20260712T231632Z-2888896.log`. C1 is not the
failure: its actual-helper controls completed before role capture with regular
zero-byte child outputs, input sizes 0/4/19/131072/131073, row/count pairs
empty `3/3`, binary `4/4`, multiline `4/4`, exact-cap `701/701`, over-cap
`701/701`, and the intended recorder-failure `2/2`; the binary input still
has exact SHA-256
`26f9f592b06d4c3c0dd2116492991fff5e4f013a884de18a0c7261ea85de931d`.
Those per-case guards and cleanup checks passed before the later baseline
observer abort. `static_c1_v2_case_matrix` was constructed, but its terminal
aggregate assertion lies after role capture and was not evaluated; this is no
synthetic C1 PASS claim, while the named artifacts expose no C1 defect.

The sole retained role output,
`static-render-role-output-baseline-2888898`, is a regular mode-0644,
151-byte file (SHA-256
`b358a3cac85e39338c6ac5e98247831ea0d2ca89465b218ca55c39b392843712`). Its
byte stream is exactly one ASCII line followed by one `0a`, with no CR:
`YT_RENDER_START_ROLE_TEST_STATE none_or_exited total=103 bytes=103
truncated=0 digest=c61f1cd48e321dc58bbaf3ca26349ce84007c099bf567a1f3e6de5c6d377faf2`.
The source baseline evidence is exactly 103 bytes and independently hashes to
that digest. Thus state, decimal fields, digest, byte/total relation, regular
mode, and line ending are valid; no stale output or generated-helper/role
classification failure is present.

The causal observer change is precise. At committed `a3cd0a2`, direct Tcl
`exec` captured the generated helper output after Tcl's trailing-newline
normalization. The new static observer redirects output to a bounded regular
file and reads it in binary mode to retain its artifact, thereby preserving
the generated helper's contractual `printf ...\\n` final LF. Its unchanged
anchored matcher instead requires the final digest immediately before `$`, so
the valid preserved LF makes the baseline reject. The C1 status patch only
improves static child status/reporting; it does not alter the generated helper
or its command. The output redirection is also confined to the
`YT_STATIC_CHECK=1` branch, so it supplies no production or performance path.

Baseline aborted before later role artifacts were created. Source audit shows
every successful role tag (baseline, semantic, broad, spoof, malformed,
command-failed, and over-cap) uses this same capture procedure and generated
newline-terminated test-only output, so each would encounter the same stale
observer matcher; missing/stdout-only/stale/nonregular/unreadable fixtures
fail before it. The smallest repair is observer-only: require exactly one
final LF in the regex/output contract (and thus continue rejecting CR,
embedded extra lines, and malformed fields) rather than trimming newlines.
Add static source/fixture assertions for the accepted LF form and rejected
no-LF/CR/double-LF forms, then take a fresh independent no-boot review before
any new canonical suite. No test, build, rootfs, VM, boot, serial, KDE/default,
or QEMU action occurred; no retry, V2/A1, HD720/fullscreen, audio,
`yt-presentfps`, `PERF-VIDEO`, or performance credit exists.

**Static render-role final-LF observer correction — READY / NO-RUN
(2026-07-12):** the pending unstaged driver-only change is confined to the
`YT_STATIC_CHECK=1` role-output observer. It replaces the stale direct-exec
no-LF-only match with an explicit parser that first rejects any CR, then
accepts either the canonical one-final-LF artifact form or the explicit legacy
no-LF direct-exec form. It does not trim, normalize, or accept embedded or
double LF; after the exact line-ending contract, the existing anchored role
state/total/bytes/truncated/digest grammar still matches the evidence line.

Pure in-memory static fixtures bind the baseline output's total and bytes to
the exact baseline evidence length and its digest to the evidence-only digest:
canonical one-LF passes with `final_lf=1`, the compatibility no-LF form passes
with `final_lf=0`, while CRLF, bare CR, and double LF reject. The fixture
contract is source-locked to the anchored matcher and absence of any `string
trim` path, and it is required by the existing static render-helper aggregate.
No generated role helper/classification, C1 V2 helper/parser/protocol, timeout,
credit, kernel/user/rootfs/default/KDE path, test, build, VM, boot, serial, or
QEMU action occurred. Exact pre-edit inventory was total/informational-RISC-V/
conflicting `0/0/0`; the driver remains unstaged. This readiness requires an
independent no-boot review before any new static suite and grants no V2/A1,
HD720, audio, fullscreen, `yt-presentfps`, `PERF-VIDEO`, or performance
credit.

**Static render-role final-LF observer independent re-review — FAIL / NO-RUN
(2026-07-12):** at source checkpoint
`33176d5ce89fb3a9b4b68c79b02d04d731f3b3fa`, exact `/proc/*/exe` inventory
was total/informational-RISC-V/conflicting `0/0/0`. The new static-only parser
is otherwise correctly fail-closed: it first rejects any CR, accepts exactly
one terminal LF or the explicit legacy no-LF direct-`exec` form without a
trim/normalization path, and its body regex is anchored through state,
decimal total/bytes, truncation bit, and 64-lower-hex digest. The canonical
and legacy fixtures bind total and bytes to the baseline evidence length and
the digest to that evidence only; CRLF, bare CR, and double LF reject. That
contract is included in the render-helper matrix, which is in turn required
by the terminal static gate.

However, guarded source audit finds no explicit embedded-LF fixture or matrix
predicate. The parser's `last-byte`/pre-final-LF branches would reject an
embedded line by construction, but a code-path inference does not satisfy the
named adversary contract, and the documented READY claim was stronger than
the actual fixture set. The smallest correction is static-observer-only: add
an output containing a nonempty embedded line before the final LF (so it
exercises the pre-final-LF rejection branch), require its parse result false
in the existing aggregate and failure report, then obtain a fresh independent
no-boot review. Do not trim or weaken the grammar.

The generated role helper/classifier remains unchanged and still emits its
single final LF only in the `YT_STATIC_CHECK=1` test route. The prior C1 V2
generated helper/parser, 376-chunk and timeout algebra, and no-credit/
production paths are unchanged by this observer-only hunk. No test, build,
rootfs, VM, boot, serial, KDE/default, or QEMU action occurred. This FAIL
authorizes no canonical MP=1/media=1 host-only suite and grants no V2/A1,
HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or performance credit.

**Static render-role embedded-LF fixture — READY / NO-RUN (2026-07-12):** the
pending unstaged observer-only hunk adds one pure, otherwise plausible output:
the exact anchored role-state line, then a nonempty
`embedded-observer-line`, then its final LF. Its parser result is now an
explicit required rejection in the same aggregate-gated role-output contract
as canonical one-LF pass, legacy no-LF pass, and CRLF/bare-CR/double-LF
rejections; the failure record includes its parsed result. No parser,
generated helper/classification, C1 V2, protocol, timeout, credit, or
production-path behavior changed. No test, build, rootfs, VM, boot, serial,
KDE/default, or QEMU action occurred; exact pre-edit inventory was
total/informational-RISC-V/conflicting `0/0/0`. The driver remains unstaged and
requires independent no-boot review before any suite, with no performance,
HD720, audio, fullscreen, `yt-presentfps`, or `PERF-VIDEO` credit.

**Static render-role embedded-LF fixture independent re-review — PASS / NO-RUN
(2026-07-12):** at source checkpoint
`035c2828097f8d5cdbe98f4d378c387e3fcad124`, exact `/proc/*/exe` inventory
was total/informational-RISC-V/conflicting `0/0/0`. The added pure fixture is
genuinely plausible observer input: it starts with the otherwise exact
anchored role-state line, appends the nonempty `embedded-observer-line`, and
ends in LF. It therefore reaches the parser's pre-final-LF check rather than
being merely an empty/double delimiter; its required false result joins the
same role-output contract as the canonical one-LF pass, legacy no-LF pass, and
CRLF/bare-CR/double-LF rejects. That contract is required by the static
render-helper matrix and terminal static gate.

The parser remains static-only under `YT_STATIC_CHECK=1`, has no trim or
normalization path, rejects CR before the line-ending decision, and preserves
the anchored state/total/bytes/truncation/digest grammar. The generated role
helper/classifier, C1 V2 generated helper/parser, 376-chunk/timeout algebra,
and production/no-credit paths remain unchanged. No test, build, rootfs, VM,
boot, serial, KDE/default, or QEMU action occurred. This PASS authorizes
exactly one fresh canonical clean-PATH host-only static suite with
`YT_STATIC_CHECK=1`, MP=1, audio-disable=1, media=1, forced-HD720=0, EGL=0,
capturediag=0, pinned window/crop/delta/video inputs, and a nonexistent
`YT_OUTDIR`; retain its first result and artifacts without retry. It is no VM,
HD720/fullscreen, audio-on, `yt-presentfps`, `PERF-VIDEO`, or performance
authority.

**Static render-role embedded-LF canonical static suite — FAIL / NO-BUILD /
NO-BOOT (2026-07-12):** the one review-authorized clean host-only invocation
used the pinned clean PATH/static=1/MP=1/audio-disable=1/media=1/
forced-HD720=0/EGL=0/capturediag=0 profile and fresh outdir
`/tmp/yt-c1-v2-observer-static-20260712T233342Z-2908170` (external log
`/tmp/yt-c1-v2-observer-static-20260712T233342Z-2908170.log`); it exited 1
without either static PASS marker. All bounded C1 actual-helper artifacts are
present, and role capture now progressed past the former final-LF matcher:
baseline, semantic, broad, spoof, malformed, command-failed, over-cap, and
the expected zero-byte failure fixtures all retain their named output
artifacts. Matching `/dev/shm` residue count is zero.

The first terminal failure is the later static compact reporter, not the role
helper: `static_render_generated_helper_role_compact` returns an interpolated
string beginning `$name(success=...)`; Tcl interprets that as an array lookup
and raises `can't read "name(success=1,exit=0,...": variable isn't array` at
the first baseline compact record. The baseline output itself is present and
regular at 151 bytes, and no parser/helper/classification conclusion follows
from this reporter exception. No retry, driver correction, build, rootfs, VM,
boot, serial, kernel/user/default/KDE action followed. Exact pre-run inventory
was total/informational-RISC-V/conflicting `0/0/0`; post-run inventory was
`1/1/0` (an unrelated RISC-V QEMU only, untouched). The driver and unrelated
KDE-smoke dirt remain unstaged. This grants no V2/A1, performance, HD720,
audio, fullscreen, `yt-presentfps`, or `PERF-VIDEO` credit; next authority is
limited to no-boot forensic of the static compact-report interpolation before
any source change or suite.

**Static render-role compact-report Tcl interpolation forensic — ROOT CAUSE
LOCALIZED / NO-RUN (2026-07-12):** at plan HEAD
`659fc64cf0dcf2e1a37bbde1e3765ddfb1e8f89f`, exact inventory was
total/informational-RISC-V/conflicting `0/0/0`; no process was touched. The
named consumed-suite outdir/log are
`/tmp/yt-c1-v2-observer-static-20260712T233342Z-2908170` and
`/tmp/yt-c1-v2-observer-static-20260712T233342Z-2908170.log`. All 12 named
role output artifacts exist. Regular nonempty baseline, semantic, broad,
spoof, malformed, command-failed, and over-cap records retain their expected
one-line helper output; missing, stdout-only, stale, nonregular, and unreadable
are the expected zero-byte helper-failure artifacts. Thus role capture reached
every tag before the later reporter exception; this is no helper,
classification, parser, C1, or production conclusion.

The exact sole unsafe scalar-plus-parenthesis occurrence is
`static_render_generated_helper_role_compact`'s line
`return "$name(success=...)"`. In Tcl, `$name(` is parsed as an array element
reference rather than scalar `name` followed by a literal parenthesis, yielding
the retained `can't read "name(success=1,exit=0,...": variable isn't array`
error at the first baseline compact record. Full guarded source audit finds no
`${var}(` occurrence. The other `$var(...)` matches are intentional Tcl
`env`/`expect_out` arrays; among static compact reporters, C1 uses a completed
`[dict get ...](` substitution and C8 uses braced `format`, both safe. The
remaining compact fields use comma/equal delimiters or completed command
substitutions; their values are not reparsed, so no additional command or
variable-substitution hazard requiring correction was found.

The smallest repair is static-observer-only: use `${name}(...)` or, preferably,
one braced `format` template with `name` and each bounded field passed as an
argument. Add a pure synthetic compact-record fixture whose literal result
starts `compact-fixture(success=1,...)`, plus a source lock that requires the
braced formatter and rejects `$name(`. Require that fixture in the existing
render-helper/terminal static aggregate before a fresh independent no-boot
review. No test, build, rootfs, VM, boot, serial, KDE/default, or QEMU action
occurred; no V2/A1, HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or
performance credit exists.

**Static render-role compact `format` correction — READY / NO-RUN
(2026-07-12):** the pending unstaged static-observer-only hunk replaces the
sole unsafe interpolated compact return with one braced Tcl `format` template
and explicit bounded arguments for name, success, exit, signal, errorcode,
message, I/O metadata, and artifact tail. A pure success-record fixture now
requires the exact literal
`compact-fixture(success=1,exit=0,signal=none,errorcode=none,msg=none,io=present 1 regular 1 bytes 42 first_hex aa last_hex bb truncated 0,artifact=compact-fixture-artifact)`.
The scoped source lock requires that braced formatter and rejects `$name(`
only inside `static_render_generated_helper_role_compact`; the fixture contract
is required by the existing render-helper/terminal aggregate. No production
helper/parser/classification, C1 V2, timeout, credit, kernel/user/rootfs/
default/KDE path, test, build, VM, boot, serial, or QEMU action occurred.
Exact pre-edit inventory was total/informational-RISC-V/conflicting `0/0/0`;
the driver remains unstaged. Independent no-boot review is required before any
suite, with no V2/A1, HD720, audio, fullscreen, `yt-presentfps`,
`PERF-VIDEO`, or performance credit.

**Static render-role compact `format` independent re-review — PASS / NO-RUN
(2026-07-12):** at source checkpoint
`d5dd287cf4858092b0eb5532a47da947898497d1`, exact `/proc/*/exe` inventory
was total/informational-RISC-V/conflicting `0/0/0`. The replacement is wholly
inside the `YT_STATIC_CHECK=1` observer: one braced `format` template has eight
ordered `%s` fields—name, success, exit, signal, errorcode, bounded message,
bounded I/O snapshot, and artifact tail—and receives exactly those eight Tcl
arguments. Tcl passes each variable as one argument without word-splitting or
re-evaluating its content; message/I/O originate in the existing bounded
observer reports, while tag/path inputs are the static fixed fixtures.

The pure synthetic success record is meaningful and exact: it exercises the
same compact procedure and requires the full
`compact-fixture(success=1,exit=0,signal=none,errorcode=none,msg=none,io=present 1 regular 1 bytes 42 first_hex aa last_hex bb truncated 0,artifact=compact-fixture-artifact)`
literal. Its scoped source lock inspects only this compact procedure, requires
the braced formatter shape, and rejects the unsafe `$name(` form without
confusing intentional `env`/`expect_out` arrays elsewhere. The resulting
contract is a required member of `static_render_helper_file_matrix`, itself a
terminal static-gate predicate.

No production helper/parser/classifier, C1 V2 generated helper/parser,
timeout/chunk algebra, credit path, kernel/user/rootfs/default/KDE path, test,
build, VM, boot, serial, or QEMU action occurred. This PASS authorizes exactly
one fresh canonical clean-PATH host-only static suite with `YT_STATIC_CHECK=1`,
MP=1, audio-disable=1, media=1, forced-HD720=0, EGL=0, capturediag=0, pinned
window/crop/delta/video inputs, and a nonexistent `YT_OUTDIR`; retain its
first result and artifacts without retry. It is no VM, HD720/fullscreen,
audio-on, `yt-presentfps`, `PERF-VIDEO`, or performance authority.

**C1 V2 observer canonical static suite — PASS / NO-BUILD / NO-BOOT
(2026-07-12):** the one review-authorized host-only invocation used the exact
clean conventional PATH, static=1, MP=1, audio-disable=1, media=1,
forced-HD720=0, EGL=0, capturediag=0, pinned window/crop/delta/video inputs,
and fresh outdir `/tmp/yt-c1-v2-observer-static-20260712T234322Z-2924043`
(external log `/tmp/yt-c1-v2-observer-static-20260712T234322Z-2924043.log`).
It exited 0 and emitted `YT-C1-V2-STATIC-PASS`,
`YT-RENDER-START-RECEIPT-STATIC-PASS`,
`YT-CAPTURE-COMPLETENESS-DIAGNOSTIC-STATIC-PASS`,
`YT-CAPTURE-DIAG-ROUTE-STATIC-PASS`, and
`YT-PRESENTFPS-STATIC-CHECK-PASS`; the latter records
`js_guest_runtime=UNEXECUTED`.

All named C1 actual-helper artifacts are retained: empty/binary/multiline/
exact-cap/over-cap plus recorder-failure; the binary input is 4 bytes, the
valid exact-cap/over-cap row artifacts are bounded, and matching `/dev/shm`
residue count is zero. The actual render-role matrix, C7/C8 protocol and
adversary matrices, capture-completeness no-credit routes, and static compact
format fixture all completed. This is a static validation of observer/helper
contracts only: no build, rootfs, VM, boot, serial, kernel/user/default/KDE,
actual YouTube, `yt-presentfps`, `PERF-VIDEO`, HD720, audio-on, fullscreen,
FPS, drop, VPQ, retire, or default claim exists. Exact pre/post inventories
were total/informational-RISC-V/conflicting `0/0/0`. The full intended
presentfps driver and this plan checkpoint may now be committed, followed by
independent no-boot review before any fresh gate.

**C1 V2 committed driver adversarial review — PASS FOR ROOTFS/BUILD STAGING
ONLY / NO-BOOT (2026-07-12):** committed driver/plan checkpoint
`433f217a7cca4eccb9a1f3df19c726b7e8ec86fe` was independently reviewed
against parent `908b05b2809d2e8ba18eb58c908655ac368f7a45`. The sole production
driver change is the C1 V2 transport: its generated helper defaults to the
absolute recorder `/bin/_consolerecord`, validates every field of both
no-option `wc` triples with the anchored decimal grammar, and emits only
BEGIN/META/CHUNK/END records. The parser preserves raw-record order, rejects
foreign/spoofed candidates and inner byte-weave/gaps, verifies exact
binary-safe lower-hex re-encoding and binary digest, and requires the unique
outer `RC:0` then `FENCE`. The bounded contract is cap/hex/chunk/record
`131072/262144/376/698/701`; its inner recorder bound is `357781` bytes and
its marker-dependent 72-second fail-closed timeout algebra is retained.

Canonical host-only evidence is
`/tmp/yt-c1-v2-observer-static-20260712T234322Z-2924043` with external log
`/tmp/yt-c1-v2-observer-static-20260712T234322Z-2924043.log`: it exited zero
with the C1 V2, render-receipt, capture-diagnostic, and terminal static PASS
markers. The retained actual helper cases cover empty, binary, multiline,
exact-cap, over-cap, and recorder failure; the latter produces no success
claim. The static adversary matrix covers LF/CRLF/CRCRLF, permitted exterior
noise, rejected interior gaps and the historical mid-HEX weave, candidate
spoof/order/binding corruption, outer-frame failure, and binary bytes. C7/C8,
capture diagnostics, C6 credit classification, performance/default behavior,
and runtime paths are unchanged apart from static observers and static
preflight ordering; `js_guest_runtime=UNEXECUTED` confirms no guest credit.

Nested gitlinks remain kernel
`b42d1c37f90b2ac48aa416a9eb215a935920f649` (`origin/v6-kernel`) and user
`a95f8c8a6d0dcb9189b6ec6d51a0f376fc721cc1` (`origin/v6-port`), with their
reviewed console-record ABI and automatic user-program discovery intact.
Their and the superproject origins are the approved ElectronSpark remotes; no
nested worktree dirt was found. The only superproject dirt is the preserved
unrelated KDE-smoke file. Exact review QEMU inventory was zero and no process
was touched.

**Authority consumed:** the following deterministic rootfs/build staging proof
was the sole permission granted by this review. It was not permission to boot,
launch QEMU, issue serial commands, measure YouTube, or alter defaults; its
later user-target failure closes it and requires the new authority stated in
the final forensic below.

**C1 V2 deterministic rootfs staging proof — INCOMPLETE / NO-BOOT
(2026-07-12):** exact preflight inventory was total/informational-RISC-V/
conflicting `0/0/0`; super HEAD/origin was
`85bade5c6fe3862cabefb49679fff299199d4f3c`, kernel HEAD/origin was
`b42d1c37f90b2ac48aa416a9eb215a935920f649`, and user HEAD/origin was
`a95f8c8a6d0dcb9189b6ec6d51a0f376fc721cc1`. Only unrelated KDE-smoke dirt
was present; kernel and user worktrees were clean. The one canonical staging
sequence, recorded under `/tmp/xv6-c1-rootfs-staging-20260712T235443Z-2939233`,
ran `cmake --build build-x86_64 --target kernel -j2`, then `user`, then
`rootfs-refresh`, each intended to stop at first failure.

The kernel command completed and regenerated the x86 boot artifact (after
mtime `1783900494`, SHA-256 unchanged at
`99b23539aa9ab7c540fe0a81ee00ecd436e9d917ec1e4193fe5a9f186194e067`), but
the 41-byte `user-build.log` contains only `[0/2] Re-checking globbed
directories...`; the wrapper emitted no `stage_result`, no rootfs-refresh log,
and no fresh-user result. This is the first failed/incomplete point, not a
claim that the user target passed or failed for a particular source reason.
The filesystem image is demonstrably stale for this proof: before and after it
has inode `73499`, mtime `1783790041`, size `8724152320`, and SHA-256
`4cb082f56a9b7ccdb4d9ec8e7efd40266c4ac5a023cd02d3c1095a1491ecc56b`.
Therefore no fresh image `/bin/_consolerecord`, browser-asset, or image/hash
claim is made. Exact final inventory was `0/0/0`; no QEMU, boot, serial,
rootfs manual write, or source edit occurred. Per the one-attempt rule no
substitute build or refresh followed. This grants no staging, V2/A1,
performance, HD720/audio/fullscreen, `yt-presentfps`, or `PERF-VIDEO` credit;
next authority is limited to forensic localization before another staging
attempt.

**C1 user-stage wrapper/build forensic — ROOT CAUSE LOCALIZED / NO-BUILD /
NO-BOOT (2026-07-12):** the prior 41-byte `[0/2] Re-checking globbed
directories...` observation was not terminal. The named regular
`user-build.log` subsequently completed at 202491 bytes at 19:56:04, and the
same-time regular `receipt.txt` contains `stage_result=user_failed`; the plan
commit was later at 19:57:47. Thus there is no missing logging flush and no
evidence that Ninja considered `user` up-to-date. The generated Ninja rule is
the direct no-pipeline command `cmake -E env HOST_CC=/usr/bin/cc
scripts/build/build-linux-host-probes.sh build-x86_64/sysroot`; its script uses
`set -euo pipefail` and exits on the observed compiler failure. No persisted
outer wrapper source or tool-session status exists in the named outdir, so an
earlier disconnected wait cannot be attributed; the final artifacts do rule
out timeout, signal, or OOM as the causal termination.

The actual failure is
`user/programs/kprofile/kprofile.c:877`: it reads
`struct konsole_prepty_wake_record.file_poll`, but the reviewed kernel UAPI at
`kernel/kernel/inc/kstats.h:484` supplies `file_poll_capable` (introduced by
kernel `ed808576`). `build-linux-host-probes.sh` sorts program directories,
so `consolerecord` ran first and left a fresh executable sysroot
`/bin/_consolerecord` at 19:55:50; that is partial sysroot state, not a passed
`user` target or image proof. `kprofile` then made Ninja report `subcommand
failed`. No build process remains, exact QEMU inventory is `0/0/0`, and the
rootfs-refresh log is absent. `fs.img` still has the pre-attempt inode/mtime/
size `73499/1783790041/8724152320`; no fresh image or browser asset claim is
made.

**Next authority required:** before any retry, independently review a narrow
user source repair that aligns the stale `kprofile` field use with the current
kernel UAPI; do not skip the program or weaken the full `user` target. The
subsequent one-attempt wrapper must persist its own source/argv, use direct
redirection (no status-masking pipeline), and call each command synchronously
inside `if stage ...; then ...; else ...; fi` so `set -e` cannot preempt the
receipt. For kernel, user, and rootfs-refresh it must record start/end,
unmodified raw exit status, regular-log bytes/SHA-256, and stage result before
proceeding; after a successful user stage it must mark the sysroot receipt
separately from image proof. Only after successful `kernel`, `user`, and
`rootfs-refresh` may it use named-file image inspection to prove executable
`/bin/_consolerecord` and record fresh image identity. That future sequence is
not authorized by this forensic; no build, rootfs refresh, VM, or source edit
occurred here.

**C1 kprofile UAPI repair and targeted user build — PASS / NO-ROOTFS /
NO-BOOT (2026-07-13):** kernel commit
`ed80857670146717f1f4f2335b9ecea7b95e254b` deliberately replaced the stale
`file_poll` function-pointer value with the boolean
`file_poll_capable = (file->ops != NULL && file->ops->poll != NULL)` in the
shared `konsole_prepty_wake_record` ABI. User commit
`3e5b90bd30130ad1a3566ddc20c9589f6159d867` (published and verified at
`origin/v6-port`) makes only the matching kprofile repair: it reads the new
field, prints it as a decimal capability, and has a `_Static_assert` that the
member exists with `uint64` width. A guarded exact-word source check found no
remaining stale `file_poll` use in kprofile; `git diff --check` passed.

The single synchronous non-boot command was
`cmake --build build-x86_64 --target user -j2`, through the retained direct
wrapper `/tmp/xv6-kprofile-uapi-build-20260713T000815Z-2961146/run-kprofile-user-build.sh`;
its regular log is 220275 bytes, SHA-256
`cf22330c71e20a66762556173fe7043859c93fefad5517b3545fefe8050e49a8`, and its
receipt records raw exit `0` and `stage_result=pass`. The fresh staged
`sysroot/bin/kprofile` is 210680 bytes with SHA-256
`e9ce92d6b05f0aa3d0be384207181e9b1b755b8b02ebc0fba94b9292d4217c88`.
Exact `/proc/*/exe` QEMU inventories were total/informational-RISC-V/
conflicting `0/0/0` before and after; no process was touched. `fs.img` remains
unchanged (inode/mtime/size `73499/1783790041/8724152320`, SHA-256
`4cb082f56a9b7ccdb4d9ec8e7efd40266c4ac5a023cd02d3c1095a1491ecc56b`), so this
is a user-stage repair only—not a rootfs/image, `_consolerecord`, VM, serial,
YouTube, HD720/fullscreen, audio-on, `yt-presentfps`, `PERF-VIDEO`, or
performance/default result. A fresh full kernel/user/rootfs staging sequence
still requires its own independent authorization and receipt discipline.

**C1 kprofile repair/build independent review — PASS FOR ONE FULL STAGING
PROOF ONLY / NO-BOOT (2026-07-13):** superproject
`885dfe2e9bfec6c1be87d590e7d321ce6c80f242` equals the explicit
`origin/codex/host-linux-abi-shell-port-ff`; its scope is only the user
gitlink and plan. The nested lineages are exact and published: kernel
`b42d1c37f90b2ac48aa416a9eb215a935920f649` at `origin/v6-kernel`, and user
`3e5b90bd30130ad1a3566ddc20c9589f6159d867` at `origin/v6-port`. The user
commit changes only `programs/kprofile/kprofile.c`; all worktrees are clean
apart from the preserved unrelated KDE-smoke file.

The semantic ABI change is correct: kernel `ed808576` renamed the `uint64`
member from a poll-function pointer to boolean `file_poll_capable`, assigned
from `file->ops != NULL && file->ops->poll != NULL`. Kprofile now makes a
compile-time `uint64` width assertion, labels the value
`file_poll_capable=%lu`, and prints the new member through an unsigned-long
cast. A guarded exact-token audit found no stale `file_poll` token in
kprofile. The retained direct wrapper has no backgrounding or status-masking
pipeline; it records the raw foreground `cmake --build build-x86_64 --target
user -j2` exit after completion. Its 220275-byte log ends in the normal
`build-linux-host-probes: wrote probes` terminal, its receipt records exit 0
and `stage_result=pass`, and the staged kprofile SHA-256 matches the receipt.
The kernel and user target evidence is no rootfs evidence: the named fs image
still has its prior inode/mtime/size/SHA-256, so no fresh image claim is made.

Exact review-start QEMU inventory was total/informational-RISC-V/conflicting
`0/0/0`; no build process remains and no process was touched. At final plan
checkpoint an independent `qemu-system-riscv64` was present
(`1/1/0` total/informational/conflicting) and was only inventoried, never
touched. **Authorization: exactly one** deterministic no-boot staging sequence, using the pinned
kernel/user gitlinks and a persisted wrapper that runs `kernel`, then `user`,
then `rootfs-refresh` synchronously with direct log redirection. It must
retain its source and argv, each stage's raw exit/start/end/log hash, pre- and
final exact QEMU inventories, and stop at the first failure. Only after all
three pass may its named-file image check prove executable
`/bin/_consolerecord`; no VM, serial, FPS, audio, fullscreen, or default claim
is authorized. Any failure consumes this proof; a fresh independent review is
required before another attempt.

**C1 deterministic full staging proof — FAILED PRE-STAGE / NO-BUILD /
NO-ROOTFS / NO-BOOT (2026-07-13):** the sole authorized wrapper invocation
was retained at
`/tmp/xv6-c1-full-staging-20260713T001943Z-2981879/run-full-staging.sh`
(SHA-256 `686f74d254c452997b8e93b6f45a278dec330473e0e3f7a7f66061de0d2d1fc1`).
Its immediate preflight had exact published lineages: super
`61de08db3d2d58f2796e1956529573b2a63efc2b` equals explicit `-ff` origin,
kernel `b42d1c37f90b2ac48aa416a9eb215a935920f649` equals `origin/v6-kernel`,
and user `3e5b90bd30130ad1a3566ddc20c9589f6159d867` equals `origin/v6-port`.
The external exact inventory immediately before invocation was
total/informational-RISC-V/conflicting `0/0/0`; only the unrelated KDE-smoke
file was dirty.

The wrapper exited raw `1` before it could complete its own first inventory:
`line 35: label: unbound variable`. `inventory()` declared local `label` but
did not assign its argument before writing its receipt field under `set -u`.
The 323-byte receipt contains only stage path, wrapper path, start time, and
the three commit IDs; the named directory contains only that receipt and the
wrapper—there are no kernel, user, or rootfs-refresh logs. Therefore no build
target, rootfs refresh, debugfs image check, VM, boot, serial command, source
edit, or manual image write occurred. `fs.img` remains at the prior recorded
inode/mtime/size `73499/1783790041/8724152320`; no new image hash or
`/bin/_consolerecord`/browser-asset claim is made. The final exact inventory
was `1/1/0` (an informational RISC-V QEMU only), which was not touched.

This failure consumes the one staging proof even though it was pre-stage. Do
not repair or rerun this wrapper under the consumed authority. Next authority
must first independently review a corrected persisted wrapper's argument and
receipt paths, then explicitly authorize a fresh one-attempt full staging
sequence. This result grants no image, user/kernel build, VM, YouTube,
HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, performance, or
default credit.

**C1 full-staging wrapper independent forensic — FAIL / NO-RUN / NO-BUILD /
NO-ROOTFS / NO-BOOT (2026-07-13):** the exact persisted wrapper
`/tmp/xv6-c1-full-staging-20260713T001943Z-2981879/run-full-staging.sh`
(SHA-256 `686f74d254c452997b8e93b6f45a278dec330473e0e3f7a7f66061de0d2d1fc1`)
and its 323-byte receipt were read in full. The immediate defect is line 21:
`inventory()` declares `local label` under `set -u` but never assigns `$1`,
then line 35 expands it. The safest narrow repair is arity validation followed
by `local label=$1` before any receipt write; every other helper must likewise
validate and bind its required positional arguments before expansion. The
receipt and absent kernel/user/rootfs logs prove no stage command ran.

Whole-wrapper review finds additional blockers. It records local git heads but
does not verify exact explicit remote refs, expected gitlinks, or permitted
worktree dirt before a build; its QEMU scan must canonicalize `/proc/*/exe`,
exempt only `qemu-system-riscv64`, and reject a conflicting process at both
preflight and final inventory. `run_stage` correctly avoids a status-masking
pipeline, but all log/hash/stat failures must be explicit fail-closed receipt
outcomes; no required post-stage artifact may use `|| true`. The receipt needs
one terminal state only—`finalize` currently writes a result before later
overwriting it—and an EXIT/signal-safe failure route. Persist wrapper SHA-256
and argv before any stage, sync that preflight receipt, then sync each terminal
stage/receipt write (including directory durability) so an abrupt shell exit
cannot leave a misleading partial result. The next stage directory must be
fresh, private, and non-symlinked (fail if it already exists); the current
wrapper truncates a caller-selected receipt path without that guard.

Image proof is incomplete: `-f` admits symlinks, failed post-identity can use
stale global identity fields, and the wrapper has no `debugfs` preflight or
named-file extraction. Require regular non-symlink paths, independently
successful pre/post identity capture, and one final pass only after identity
change plus `debugfs` stat/dump/hash/mode proof that image
`/bin/_consolerecord` is executable and matches its staged source. Retain the
named Chromium media-probe assets through the same bounded debugfs
extraction/hash receipt; fail before any build if required host tools or
receipt paths are unavailable. The existing order has no `cmake` command
before line 132, but the corrected wrapper must retain that all-preflight
barrier and stop after the first failing stage.

Before a new staging authority, perform **only** non-executing static
validation of the corrected persisted script: `bash -n` and ShellCheck when
available, plus a source contract audit for assigned function arguments,
quoted paths, direct stage status capture, one durable terminal result,
pre/final inventory, and all required debugfs/image assertions. Do not execute
any stage during that review. Exact review-start inventory was
total/informational-RISC-V/conflicting `0/0/0`; at final plan checkpoint only
an untouched informational `qemu-system-riscv64` was present (`1/1/0`). This
FAIL grants no wrapper retry, build, rootfs refresh, VM, image, or performance
credit.

**C1 corrected full-staging wrapper static contract — PASS / NO-RUN /
NO-BUILD / NO-ROOTFS / NO-BOOT (2026-07-13):** the fresh persisted artifact is
`/tmp/xv6-c1-full-staging-wrapper-static-20260713T003014Z-2991635/run-full-staging.sh`
(16513 bytes, SHA-256
`359388a9a124757bdde19668e60919c2386c442f25a0e4c34da2f6cb6044839c`), with its
no-execution checker in the same private directory (4573 bytes, SHA-256
`5ee9ba511b4cc8cfd701aa873c8ad90afac6d182fa948d85c6b477b7625ca2dc`). Both
scripts passed `bash -n`. ShellCheck is unavailable on this host
(`SHELLCHECK_NOT_INSTALLED`); this is recorded rather than treated as a pass.

The checker exited 0 with `WRAPPER_STATIC_CONTRACT_PASS`: all helper positional
arguments are arity-checked and assigned under `set -u`; top/kernel/user
branches, explicit remotes, pinned nested heads/gitlinks, and only the known
KDE top dirt are preflight-gated. It requires canonical `/proc/*/exe`
classification with only exact `qemu-system-riscv64` informational, direct
stage redirection with immediate raw exit receipt, receipt/file/directory
sync, a single EXIT-trap terminal result, a fresh private non-symlink runtime
directory, and canonical kernel/user/rootfs-refresh commands only after
preflight. The static checks also require non-symlink regular image/artifact
paths, separate pre/post inode/mtime/size/SHA fingerprints, stale-identity
rejection, debugfs stat/dump proof that image `/bin/_consolerecord` is regular
and executable with SHA equal to staged `_consolerecord`, and the three pinned
media assets (`manifest.json`, `probe-lib.js`, `probe.js`) through debugfs hash
receipts. Final conflicting-QEMU enforcement precedes the sole PASS state;
the checker rejects `pgrep` and `|| true`.

The wrapper was deliberately never invoked. Its directory contains exactly the
wrapper and checker, with no receipt, stage log, rootfs image extraction, or
other runtime artifact. Exact final read-only QEMU inventory was
total/informational-RISC-V/conflicting `0/0/0`; no QEMU, build, rootfs refresh,
image write, debugfs command, source/KDE edit, VM, boot, or serial action
occurred. This is source/static wrapper readiness only, not staging/image/
browser asset, `_consolerecord`, YouTube, HD720/fullscreen, audio,
`yt-presentfps`, `PERF-VIDEO`, performance, or default credit. A separate
independent review and fresh explicit staging authorization remain required.

**C1 corrected full-staging wrapper independent no-exec review — FAIL /
NO-RUN / NO-BUILD / NO-ROOTFS / NO-BOOT (2026-07-13):** the retained wrapper
SHA-256 is the recorded
`359388a9a124757bdde19668e60919c2386c442f25a0e4c34da2f6cb6044839c`; the
private directory contains only that regular wrapper and the actual regular
checker `check-wrapper-contract.sh` (SHA-256
`5ee9ba511b4cc8cfd701aa873c8ad90afac6d182fa948d85c6b477b7625ca2dc`). The
requested name `check-wrapper-static.sh` is absent. No receipt, stage log,
fingerprint, extraction, or other runtime artifact exists, so neither script
was invoked by this review and the wrapper itself was never invoked. The prior
checker exit claim is a historical static report, not a retained checker-output
artifact.

Manual line audit confirms that the corrected helper argument binding,
non-symlink guards, raw stage exit capture without `set -e`, canonical stage
order, exact `qemu-system-riscv64` exemption, final conflicting-QEMU check,
fresh pre/post image fingerprints, and debugfs regular/executable dump logic
are present. ShellCheck being unavailable is not itself a blocker: a manual
review can substitute for it. The wrapper is nevertheless not authorization
ready. Its top-level lineage passes literal `dynamic`, meaning any newer
origin tip can replace the reviewed superproject source; require a pinned
authorization-specific super head (or equivalently pinned reviewed source
tree) and verify it against explicit origin before a stage. `KERNEL_ARTIFACT`
is declared but never read, so it supplies no pre/post kernel artifact
identity, regularity, or hash evidence. Require fail-closed kernel artifact
receipts before kernel build and after successful kernel stage, and require
the staged `_consolerecord` itself to be executable as well as regular before
its image-hash comparison.

The actual checker is non-circular in that it reads a separate wrapper file,
but it is too weak to establish the claimed contract: it only greps literals
and first-occurrence order, can be satisfied by comments/unreachable text,
does not invoke or retain `bash -n`, does not detect the unused kernel
variable/dynamic-super defect, and lacks negative mutation controls. Rename or
record it consistently, then make future no-exec validation run `bash -n` and
the checker over controlled wrapper copies that remove each required gate;
each mutation must reject. Keep ShellCheck optional and record its absence.
Also preflight every externally invoked utility (including `date` and `mkdir`)
or emit a durable preflight failure before any build.

No new staging proof is authorized. Exact review-start and final inventories
were total/informational-RISC-V/conflicting `0/0/0`; no process was touched.
Only a corrected persisted wrapper plus the strengthened non-executing static
evidence may receive another independent review; no build, rootfs, VM, image,
or performance credit follows from this FAIL.

**C1 full-staging wrapper V2 semantic static contract — PASS / NO-RUN /
NO-BUILD / NO-ROOTFS / NO-BOOT (2026-07-13):** the fresh artifact set is
`/tmp/xv6-c1-full-staging-wrapper-v2-static-20260713T004146Z-3002245`.
The wrapper is 15656 bytes, SHA-256
`3c43ab3092fa772fae47971f7c55e1372dfbc145ad7bf978a0efc4d63ee4d255`; the
consistently named semantic checker `check-wrapper-static.sh` is 6405 bytes,
SHA-256 `9b54c458894030d093a0f252bf90e345d35814e973ae797ef30234fce985a9a2`.
Named `bash-n.log` is the empty successful parser output (SHA-256
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`),
while `shellcheck.log` explicitly records `SHELLCHECK_NOT_INSTALLED` rather
than a false pass. Named `checker.log` (SHA-256
`a6416c4be0c1dfb2e6c8cb50efbf0bcd95845833fe05ae061e5da845e7306019`) ends
`WRAPPER_V2_STATIC_CONTRACT_PASS`.

The V2 wrapper pins source behavior to super commit
`885dfe2e9bfec6c1be87d590e7d321ce6c80f242`: runtime HEAD must equal the
explicit branch remote, descend from that source checkpoint, and have only
`docs/active-work-plan.md` committed changes since it. It retains exact kernel
`b42d1c37f90b2ac48aa416a9eb215a935920f649` and user
`3e5b90bd30130ad1a3566ddc20c9589f6159d867` branch/remote/gitlink checks,
permits only the known KDE worktree dirt, and pins the reviewed x86 kernel
artifact SHA-256
`99b23539aa9ab7c540fe0a81ee00ecd436e9d917ec1e4193fe5a9f186194e067` before
and after the kernel stage. It requires both artifact paths to be regular,
non-symlinked, records size/mode/SHA, and requires staged `_consolerecord` to
be executable before the image hash comparison.

The checker first parses the wrapper, strips comment-only lines, extracts
function bodies, and asserts the source-lineage, nested lineage/dirt, direct
stage/raw-exit, single trap terminal, exact RISC-V-only QEMU classifier,
pre/post kernel artifact, image identity, debugfs regular/executable dump,
media hashes, and ordered kernel → post-artifact → user → rootfs → image →
final-QEMU → PASS structure. Five controlled non-executing mutation copies
and logs remove the final QEMU gate, permit a symlink, skip the pre-kernel hash,
loosen the source pin, or reorder the kernel stage; each checker child exits
nonzero and the parent records its rejection. These are checker-only temporary
files, never wrapper executions.

No runtime wrapper receipt, stage log, rootfs-image extraction, build, refresh,
debugfs invocation, VM, boot, serial command, source/KDE edit, or QEMU action
occurred. Exact final read-only QEMU inventory was total/informational-RISC-V/
conflicting `0/0/0`; kernel and user worktrees are clean and only the preserved
KDE-smoke file is dirty. This is static wrapper evidence only—not kernel/user/
image/browser asset, `_consolerecord`, YouTube, HD720/fullscreen, audio,
`yt-presentfps`, `PERF-VIDEO`, performance, or default credit. Independent
review and a new explicit staging authority are still mandatory.

**C1 V2 wrapper/negative-control independent review — FAIL / NO-EXEC /
NO-BUILD / NO-ROOTFS / NO-BOOT (2026-07-13):** the wrapper, checker, parser
log, checker log, and all five mutation files/logs in
`/tmp/xv6-c1-full-staging-wrapper-v2-static-20260713T004146Z-3002245` were
read without invocation. Recorded wrapper/checker/ShellCheck/parser/checker
hashes match their plan values; `bash-n.log` is the empty successful parser
output and `shellcheck.log` honestly says unavailable. The wrapper is never
executed: no receipt, stage logs, image fingerprints, debugfs outputs, or
other runtime artifact exist. Its core source contract is sound on review:
base `885dfe2` must be an explicit-origin, docs-only ancestor; nested heads
and gitlinks are exact; the reviewed kernel artifact SHA is required before
and after its kernel stage; staged console executability/hash, fresh image,
debugfs mode/hash/media checks, raw stage exits, single trap terminal, and
final exact QEMU enforcement are all present. Current nested pins and kernel
artifact SHA match their reviewed values, and the image remains unchanged.

The negative proof is nevertheless insufficient for authorization. Final-QEMU,
symlink, and skipped pre-kernel-hash mutations each remove the intended active
guard and are rejected by their corresponding checker predicate. The alleged
source-pin mutation changes `SOURCE_BASE` to `dynamic`; the wrapper has no
dynamic mode, so runtime `git cat-file -e "dynamic^{commit}"` would fail
closed. Its checker rejection is only the missing literal constant—not proof
that an unsafe source-base bypass is caught. The alleged reorder mutation
changes the *label* from `kernel` to `rootfs-refresh` but leaves the command's
actual `--target kernel` in place; it corrupts receipt/log labels and later
collides with the genuine rootfs label, but does not reorder the kernel/rootfs
commands. Its checker failures therefore do not prove rejection of an actual
stage-order bypass. This violates the required mutation-specific adversarial
contract, not merely a reporting detail.

Before another review, replace those two controls with syntactically valid
mutants that respectively bypass the real source-base ancestor/docs-only gate
and move the actual `--target rootfs-refresh` invocation before the kernel
stage, while preserving all unrelated gates. Their logs must fail the
source-lineage and stage-order predicates specifically. Retain the three
sound mutations, recorded `bash -n`, optional ShellCheck absence, and all
no-exec restrictions. Exact review-start/final QEMU inventory was
total/informational-RISC-V/conflicting `0/0/0`; no process was touched. This
FAIL authorizes no wrapper invocation, staging build, rootfs refresh, VM, or
performance work.

**C1 V2 negative-control repair — PASS / NO-EXEC / NO-BUILD / NO-ROOTFS /
NO-BOOT (2026-07-13):** only the semantic checker changed; canonical wrapper
`run-full-staging.sh` remains byte-identical at SHA-256
`3c43ab3092fa772fae47971f7c55e1372dfbc145ad7bf978a0efc4d63ee4d255`.
The updated checker is SHA-256
`1bfb957c3d5306e0ad42c6025025142c8579f882e6999d9e05dd9853a9f9cdad`.
Named `bash-n-negative-repair.log` is the empty parser PASS output; the
canonical-plus-five-mutant checker log is SHA-256
`ff9bf761ad203ccaddef5011d60800ce6a1da8043748e34c4d07cb6f6057fabc` and ends
`WRAPPER_V2_STATIC_CONTRACT_PASS`.

The repaired source-pin mutation is syntactically valid and preserves every
unrelated guard, but inserts
`SOURCE_BASE=$(git -C "$ROOT" rev-parse HEAD)` after explicit branch/remote
verification. At runtime that makes the ancestor and docs-only checks
self-referential and therefore bypasses the reviewed source pin. Its retained
diff is SHA-256
`69c60054542a3f95e39d5f9f0c7e5ef399812b7e9f22344c47f8ec04f06c749d`; its
child checker exits 1 and specifically emits `STATIC_FAIL
source_base_assignment` because V2 now requires the sole immutable
`SOURCE_BASE=` assignment. The repaired reorder mutation physically moves the
intact canonical `run_stage rootfs-refresh ... --target rootfs-refresh` block,
with its matching failure branch, before the unchanged kernel command/block.
Its retained diff is SHA-256
`06375e580ef5caf0431abe459bf23a1b2b5e9002a8f1f803b8ede598859cbbe9`; its child
exits 1 specifically with `STATIC_FAIL user_then_rootfs` and
`kernel_before_rootfs`. Thus neither rejection relies on invalid syntax,
renamed labels, duplicate stage labels, or an unrelated predicate.

The unchanged final-QEMU, symlink, and pre-kernel-hash controls also each exit
1 for `media_before_final_qemu`/`final_qemu_before_pass`, `nonsymlink_guard`,
and `kernel_preflight_gate`, respectively. Every per-mutant `.diff`, `.log`,
and `.exit` is retained in the V2 directory; each `.exit` records
`checker_exit=1`. No wrapper invocation, stage, build, rootfs refresh,
debugfs/image operation, VM, boot, serial command, source/KDE edit, or QEMU
action occurred. Exact final inventory was total/informational-RISC-V/
conflicting `0/0/0`; only the preserved KDE-smoke dirt remains. This is no-exec
checker evidence only, with no image/`_consolerecord`/browser asset/YouTube/
HD720/fullscreen/audio/`yt-presentfps`/`PERF-VIDEO`/performance/default credit.

**C1 V2 mutation-control independent re-review — PASS FOR ONE CANONICAL
STAGING PROOF ONLY / NO-BOOT (2026-07-13):** canonical
`run-full-staging.sh` remains exactly SHA-256
`3c43ab3092fa772fae47971f7c55e1372dfbc145ad7bf978a0efc4d63ee4d255`;
the corrected checker is SHA-256
`1bfb957c3d5306e0ad42c6025025142c8579f882e6999d9e05dd9853a9f9cdad`.
The empty `bash-n-negative-repair.log`, canonical checker PASS log, all five
diffs/logs/exits, and their recorded SHA-256 values are retained. The wrapper
has not run: no receipt, stage log, rootfs extraction, or other runtime output
exists. ShellCheck remains honestly unavailable and is not a blocker because
the checker plus this manual source review cover its required contract.

The repaired source mutation inserts a syntactically valid post-remote
`SOURCE_BASE=$(git -C "$ROOT" rev-parse HEAD)`: it would make the ancestor and
docs-only tests self-referential at runtime, so it is a genuine unsafe source
pin bypass. It is rejected specifically by `source_base_assignment`; its diff
SHA-256 is
`69c60054542a3f95e39d5f9f0c7e5ef399812b7e9f22344c47f8ec04f06c749d` and
its child exit is one. The repaired reorder mutation physically moves the
intact real `--target rootfs-refresh` block before the real kernel block, not
merely its label. It is rejected specifically by `user_then_rootfs` and
`kernel_before_rootfs`; its diff SHA-256 is
`06375e580ef5caf0431abe459bf23a1b2b5e9002a8f1f803b8ede598859cbbe9` and
its child exit is one. Final-QEMU, symlink, and pre-kernel-hash controls remain
genuine and each retain exit one for their corresponding predicate.

The previously reviewed runtime contract remains intact: source base `885dfe2`
is explicit-origin/docs-only, nested kernel/user heads and gitlinks are
published/pinned, and the reviewed kernel artifact remains exactly SHA-256
`99b23539aa9ab7c540fe0a81ee00ecd436e9d917ec1e4193fe5a9f186194e067` before
the authorized run. It uses raw foreground stage exits, one trap terminal,
fresh image identity, regular/executable console and debugfs/media hash proof,
and final exact QEMU enforcement before PASS. Exact review-start/final QEMU
inventory was total/informational-RISC-V/conflicting `0/0/0`; no process was
touched.

**Authorization:** exactly one invocation of this canonical wrapper to run
kernel, then user, then rootfs-refresh, with no boot or QEMU launch. Preserve
its complete receipt and logs, stop at first failure, and do not retry. Only a
fully successful run with its named image checks may establish staging/image
facts; it creates no YouTube, FPS, audio, fullscreen, or default credit. A
fresh independent review is required after the attempt before any later gate.

**C1 canonical full staging invocation — FAILED AFTER ROOTFS / NO-BOOT
(2026-07-13):** the sole allowed exact wrapper
`/tmp/xv6-c1-full-staging-wrapper-v2-static-20260713T004146Z-3002245/run-full-staging.sh`
was verified at SHA-256
`3c43ab3092fa772fae47971f7c55e1372dfbc145ad7bf978a0efc4d63ee4d255` and run
once, synchronously. Its retained runtime directory is
`/tmp/xv6-c1-full-staging-run-20260713T010154Z-3023153`. Before the stages it
records exact `bd054ca` explicit-origin/docs-only super lineage, exact pinned
kernel/user remotes and gitlinks, only allowed KDE dirt, and exact QEMU
preflight total/informational-RISC-V/conflicting `0/0/0`.

All three canonical foreground commands completed raw zero in order: kernel
(`stage-kernel.log`, 1609 bytes, SHA-256
`5016264adf6a3eeed98238e919ef8e7ec053a068021262fbdf5779a54f4c94c4`), user
(`stage-user.log`, 220275 bytes, SHA-256
`670d63507d39dc6f431c6a94a0f59a33623c80fa8ea269980a2ec4f6d845e79c`), then
rootfs-refresh (`stage-rootfs-refresh.log`, 16678 bytes, SHA-256
`69400ab391cf245f306bf11e8d79a3e748ec50e0e937abb3acb0f73161bed3f5`). The
kernel post-artifact retained the reviewed SHA-256 unchanged. Rootfs-refresh
reported `make-rootfs: wrote ... fs.img`; the wrapper's post image identity is
inode/size/mtime/SHA `73499/8724152320/1783904866/
0003dbdee6edd9f1083b7092b83fa9ab36d934eead9c8a72d39f19a7243df287`, distinct
from the preimage identity.

The staged `_consolerecord` is regular executable mode `755`, 20808 bytes,
SHA-256 `a50bc99c50e00ca53a85499c03b58d17bd9148b5d29930d70896b701294b0d34`.
The next retained row is only `debugfs.consolerecord.stat_raw_exit=0`, followed
by terminal `consolerecord_image_proof_failed` exit/requested-exit `1`. There
is no debugfs dump/hash row for that file, no media asset extraction proof, and
no final in-wrapper QEMU inventory. Therefore the refreshed image identity is
real but the required image `/bin/_consolerecord` semantic/mode/hash proof and
browser/media asset proof are **not** established. No retry, manual image
write, debugfs follow-up, VM, boot, serial, or performance action occurred.

After synchronous wrapper exit, the external exact inventory was
`0/0/0`; an informational RISC-V QEMU observed during rootfs-refresh was
untouched and absent at final check. The only remaining worktree dirt is the
preserved KDE-smoke file. This consumed failure provides no valid image
payload, YouTube, HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`,
performance, or default credit; next authority is limited to independent
forensic localization of the wrapper's post-stat image-proof failure.

**C1 console-image-proof forensic — LOCALIZED / NO-BUILD / NO-ROOTFS /
NO-BOOT (2026-07-13):** retained
`debugfs-consolerecord.stat.log` is exactly two LF-terminated lines (no CR):
the `debugfs 1.47.0` banner and `/bin/_consolerecord: File not found by
ext2_lookup`. `debugfs -R stat` nevertheless returned raw zero, so the
wrapper's `stat_raw_exit=0` was not a lookup-success fact; its subsequent
absence of `Type: regular` correctly produced the generic proof failure. This
is not a parser grammar drift. Read-only `debugfs stat` and `ls -l` against
the still-fresh receipt identity (`inode 73499`, `8724152320` bytes,
`mtime 1783904866`) find no `/bin/_consolerecord`, but find
`/bin/consolerecord` as a regular `0755`, link-count-one, 20808-byte file.
The corresponding sysroot file is only `bin/_consolerecord`; the rootfs
source's explicit xv6-style copy rule uses `${base#_}`, deliberately installing
it at `bin/consolerecord`.

The same shared `debugfs_regular_dump` parser is used for media. Read-only
stats find all three intended media paths as regular, link-count-one files
with the expected sizes/modes: `manifest.json` `813/0644`, `probe-lib.js`
`4336/0644`, and `probe.js` `26585/0644`. Thus their existing non-executable
`Type: regular` grammar is compatible with this `debugfs`; this did not
establish their hashes because the forensic deliberately performed no dump.
The minimal repair is to retain the staged source assertion at
`sysroot/bin/_consolerecord` but call the image proof on `/bin/consolerecord`.
Before another review, enhance the shared proof receipt to classify an
`ext2_lookup` diagnostic as lookup failure rather than relying on raw exit,
then retain static fixtures for actual regular/executable output, a raw-zero
lookup miss, non-regular and malformed/no-`Type` output, non-executable
console mode, and each normal media path. No image write, dump, hash,
wrapper execution, build, refresh, VM, boot, serial, source/KDE edit, or
performance action occurred in this forensic. Exact final inventory was
total/informational-RISC-V/conflicting `1/1/0` (PID 3057622
`/usr/bin/qemu-system-riscv64`), which was not touched; it remains
informational only. The preserved KDE-smoke dirt remains. This locates a
wrapper contract defect but grants no completed image proof or any YouTube,
HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, performance, or
default credit.

**C1 existing-image proof script/static fixtures — PASS / NO-RUN / NO-IMAGE
READ (2026-07-13):** the fresh static-only artifact set is
`/tmp/xv6-existing-image-proof-static-20260713T011508Z-3060579`.
`prove-existing-image.sh` is SHA-256
`b38bd4cd65a218e6bbe2424047a75b1755df29947fbdfc07ecf52d704e42b33b`; its
pure checker `check-proof-static.sh` is SHA-256
`3763886ffde7d32c9511af104f9b6f51d388f720c562e619f47bebc2961a2cce`.
The named empty `bash-n.log` records syntax success and `checker.log`
(SHA-256 `d867d7f608dba3eb15f7cdbe994cdebe5e601d19657c7eaac9267ecf8c1c0ab6`)
ends `EXISTING_IMAGE_PROOF_STATIC_PASS`.

The unexecuted script pins the existing fresh image—not a substitute—to
inode/size/mtime/SHA `73499/8724152320/1783904866/
0003dbdee6edd9f1083b7092b83fa9ab36d934eead9c8a72d39f19a7243df287`, and
requires the reviewed super lineage as a docs-only descendant of `08be6fd`
with exact kernel/user remotes and gitlinks. It retains staged-source proof at
non-symlink executable `sysroot/bin/_consolerecord`, but correctly targets the
image path `/bin/consolerecord`. Runtime code is read-only (`debugfs -R`, never
`-w`), requires pre/final no-conflicting-QEMU inventories, dumps only to a
fresh private `/tmp` destination, and compares dumped console bytes to the
staged SHA. It also pins and would verify all three media path sizes, modes,
and hashes.

Seven retained pure stat fixtures cover valid executable regular console,
raw-zero `File not found by ext2_lookup`, nonregular, missing `Type`, malformed
`Type`, non-executable console mode, and valid `0644` media. The parser rejects
the lookup diagnostic first (fixture return 31), then requires regular type,
link count one, exact size, and expected mode; all fixture expectations pass.
Two checker-only mutation controls remove the lookup diagnostic guard or alter
the installed image target. Their retained children each exit 1 with
`lookup_guard` and `image_console`, respectively. No proof script invocation,
debugfs image command, dump, hash of the image, build, rootfs refresh, VM,
boot, serial, source/KDE edit, or QEMU action occurred. Exact final inventory
was total/informational-RISC-V/conflicting `0/0/0`; only preserved KDE-smoke
dirt remains. This is no-run parser/script credit only, not image payload,
media hash, YouTube, HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`,
performance, or default credit.

**Existing-image proof independent artifact review — PASS / NO-RUN /
NO-IMAGE-READ (2026-07-13):** at explicit `-ff` HEAD
`65f37db64659ab571b64930f6540948e10db2d12`, manual read-only review of the
retained proof, checker, fixtures, checker log, and both mutation diffs/logs
recomputed the recorded script/checker SHA-256 values
`b38bd4cd65a218e6bbe2424047a75b1755df29947fbdfc07ecf52d704e42b33b` and
`3763886ffde7d32c9511af104f9b6f51d388f720c562e619f47bebc2961a2cce`.
The retained empty parser log and checker terminal are consistent with static
syntax/fixture success; this review did not invoke either script. The source
pins the exact fresh image identity, maps staged executable
`sysroot/bin/_consolerecord` to image `/bin/consolerecord`, rejects raw-zero
`ext2_lookup` before type parsing, requires regular/link-one/size/mode and
console executability, and rehashes read-only `debugfs -R` dumps against the
staged console and pinned media hashes. It has pre/final exact-QEMU checks,
allows only informational RISC-V, creates a fresh private dump directory, and
contains no `debugfs -w` or `pgrep` path. The seven fixtures exercise the
actual `debugfs` grammar and the two syntactically valid mutations specifically
lose `lookup_guard` and `image_console`; their retained checker children exit
one. This independent PASS authorizes exactly one later invocation of that
unchanged proof script, read-only against the pinned image, with no build,
refresh, image write, QEMU, boot, or serial action. Its result must be
recorded honestly without retry; an evidence failure must be fixed or closed
and the ranked performance path resumed, not used as a stopping point.

At review time, exact QEMU inventory was total/informational-RISC-V/conflicting
`0/0/0`; kernel and user worktrees were clean and only the preserved KDE-smoke
dirt existed. No proof/checker invocation, debugfs/image operation, source,
test, build, rootfs, VM, KDE, or QEMU action occurred. A successful proof is
still only an image-staging fact: it next requires an independent adversarial
pre-boot review of the committed kernel/user/driver/rootfs proof, then a fresh
conductor-owned 60-second gate before one x86 windowed A1 trial. It supplies
no YouTube, HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, FPS,
performance, or default credit.

**C1 existing-image proof — PASS / CONSUMED / READ-ONLY / NO-BOOT
(2026-07-13):** the sole authorized invocation used the unchanged reviewed
script at SHA-256
`b38bd4cd65a218e6bbe2424047a75b1755df29947fbdfc07ecf52d704e42b33b`:
`bash /tmp/xv6-existing-image-proof-static-20260713T011508Z-3060579/prove-existing-image.sh`.
It exited raw zero exactly once. Invocation metadata, empty combined output,
and raw exit are retained in
`/tmp/xv6-existing-image-proof-invocation-20260713T021107Z-3099086`; the proof
receipt and every stat/dump/extract artifact are in
`/tmp/xv6-existing-image-proof-run-20260713T021107Z-3099092`.

The receipt pins super/kernel/user lineage to `6739538` / `b42d1c3` /
`3e5b90b` and proves image identity inode/size/mtime/SHA
`73499/8724152320/1783904866/0003dbdee6edd9f1083b7092b83fa9ab36d934eead9c8a72d39f19a7243df287`.
Staged `build-x86_64/sysroot/bin/_consolerecord` is executable mode `755`,
20808 bytes, SHA-256
`a50bc99c50e00ca53a85499c03b58d17bd9148b5d29930d70896b701294b0d34`;
read-only `debugfs` proved image `/bin/consolerecord` is regular, link-count
one, mode `0755`, 20808 bytes, and its dumped hash matches exactly. Image
media assets are regular, link-count one, mode `0644`, with pinned size/hash:
`manifest.json` 813 / `46dced0c...3ac22`, `probe-lib.js` 4336 /
`2fde2692...ce137c`, and `probe.js` 26585 / `de6fa5dc...f48fe8`; all stat and
dump raw exits are zero and all full hashes match the receipt's expected
values. Preflight/final exact QEMU inventories were both total/informational-
RISC-V/conflicting `0/0/0`, and the image inode/size/mtime remained unchanged
afterward. No checker, rebuild, refresh, image write, VM, boot, serial command,
source, or KDE action occurred; only the preserved KDE-smoke dirt remains.
This proves staging/image payload only and grants no YouTube, HD720,
fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, FPS, performance, or default
credit. Next is the required independent adversarial pre-boot review before
any fresh conductor gate.

**C1 independent adversarial pre-boot review — FAIL / NO-BUILD / NO-ROOTFS /
NO-BOOT (2026-07-13):** current superproject
`106bff03368a75ad99cfb0f3582ff38d8cda121b` exactly equals the advertised
explicit `origin/codex/host-linux-abi-shell-port-ff`; its only change after
proof checkpoint `6739538` is this plan's consumed-proof record. Kernel
`b42d1c37f90b2ac48aa416a9eb215a935920f649` and user
`3e5b90bd30130ad1a3566ddc20c9589f6159d867` exactly match their gitlinks and
advertised approved remotes, both nested worktrees are clean, and only the
preserved KDE-smoke dirt exists. The current reviewed kernel artifact still
hashes `99b23539...e067`; the current image still has the proved
inode/size/mtime/SHA `73499/8724152320/1783904866/0003dbde...287`. The
consumed proof receipt and recomputed retained extracts establish executable
staged `_consolerecord` and image `/bin/consolerecord` hash equality plus all
three pinned media assets.

The runtime C1 chain nevertheless has one exact blocking mismatch. The
byte-identical reviewed driver still generates live `/fbs.sh` through
`fbstat_transport_guest_helper_script` with default recorder
`/bin/_consolerecord`, while `make-rootfs.sh` deliberately maps staged
`bin/_consolerecord` to image `/bin/consolerecord`; the retained raw-zero
debugfs lookup says `/bin/_consolerecord: File not found by ext2_lookup`.
Thus the otherwise fail-closed V2 chunk/RC/FENCE/no-credit transport would
stop at its missing recorder before an admissible C1 sample. This is not an
unsafe false-credit path, but it leaves the exact kernel/image unready for the
requested A1 trial. Exact audit-start and audit-final QEMU inventories were
both total/informational-RISC-V/conflicting `0/0/0`; no process was touched.
This FAIL authorizes no 60-second gate and no VM. Repair only the live/default
recorder path to `/bin/consolerecord`, update its static source locks/cases,
and obtain fresh static plus independent no-boot review before repeating this
pre-boot review. No commit or push is made for this failed checkpoint.

**C1 recorder image-path repair — STATIC PASS / NO-BUILD / NO-ROOTFS-WRITE /
NO-BOOT (2026-07-13):** the live generated V2 helper now defaults only to the
proved image path `/bin/consolerecord`; its test override remains explicit,
and the staged-source proof name `build-x86_64/sysroot/bin/_consolerecord` is
unchanged. All related C1 source locks now require the image path and reject
the old `/bin/_consolerecord` default. No protocol row, chunk/count/digest,
timeout, RC/FENCE, classifier, diagnostic, or no-credit behavior changed;
C7/C8 implementations remain untouched.

The single clean-PATH canonical host-only suite used static=1, MP=1,
audio-disable=1, media=1, forced-HD720=0, EGL=0, capturediag=0, and fresh
outdir `/tmp/xv6-c1-recorder-path-static-20260713T022858Z-3117399`; external
log `/tmp/xv6-c1-recorder-path-static-20260713T022858Z-3117399.log` is 17755
bytes, SHA-256
`09ac096572069b7e8d6913260c229bb1ce35417f69b31d2b705499baaec8466f`,
and raw exit was zero. It emitted `YT-C1-V2-STATIC-PASS` with
`recorder_image_path=/bin/consolerecord recorder_route=1`, plus the render,
capture-diagnostic, route, and overall static PASS markers with
`js_guest_runtime=UNEXECUTED`; C1 contiguity/adversary/binary/timeout and
diag0/no-credit coverage remained green. Preflight QEMU inventory was
total/informational-RISC-V/conflicting `0/0/0`; final was `1/1/0`, consisting
only of untouched PID 3121298 `/usr/bin/qemu-system-riscv64`. No checker/image
proof, build, refresh, image write, x86 QEMU, VM, boot, serial, rootfs, or KDE
action occurred. This PASS permits commit/publish and a fresh independent
no-boot pre-boot review only; it does not form a 60-second gate or authorize a
VM, YouTube, HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, or
performance credit.

**C1 repaired-path independent adversarial pre-boot review — PASS /
NO-BUILD / NO-ROOTFS-WRITE / NO-BOOT (2026-07-13):** reviewed immutable
`a697d88a101009a7a138a19890a78eb6df3085fc`, exactly equal to advertised
`origin/codex/host-linux-abi-shell-port-ff` at the approved ElectronSpark
superproject remote. Kernel/user gitlinks and clean nested heads remain exact
and published at `b42d1c37f90b2ac48aa416a9eb215a935920f649` / `origin/v6-kernel`
and `3e5b90bd30130ad1a3566ddc20c9589f6159d867` / `origin/v6-port`, on the
approved ElectronSpark remotes; there are no conflicts, and the only top-level
dirt is the preserved KDE-smoke file. The current regular x86 kernel artifact
still has mode/size/hash `0644/41339412/99b23539aa9ab7c540fe0a81ee00ecd436e9d917ec1e4193fe5a9f186194e067`,
matching the reviewed post-kernel receipt for that kernel gitlink.

The consumed read-only proof receipt at
`/tmp/xv6-existing-image-proof-run-20260713T021107Z-3099092/receipt.txt` and
independently rehashed retained extracts prove staged executable
`build-x86_64/sysroot/bin/_consolerecord` maps to image
`/bin/consolerecord`: both are regular `0755`, 20808 bytes, and byte/hash equal
at `a50bc99c50e00ca53a85499c03b58d17bd9148b5d29930d70896b701294b0d34`.
The same receipt/extract comparison proves the three regular `0644` media
assets at 813/4336/26585 bytes and hashes `46dced0c...3ac22`,
`2fde2692...ce137c`, and `de6fa5dc...f48fe8`. The current image remains the
proved regular artifact `73499/8724152320/1783904866/0003dbdee6edd9f1083b7092b83fa9ab36d934eead9c8a72d39f19a7243df287`
(inode/size/mtime/SHA-256).

The committed production diff changes only the C1 V2 generator's default to
exactly `/bin/consolerecord`; all later hunks are static source locks/reporting.
Guarded source audit finds `/bin/_consolerecord` only in explicit static
negative/override checks, while `_consolerecord` remains the intentional
sysroot/stub staging name. Rootfs `${base#_}` mapping, the explicit test
override, silent recorder, fixed 24-byte shared UAPI, x86-only root/copy-before-
lock/timed record path, shared normal-producer wire mutex, and emergency
generation no-credit failure remain coherent; RISC-V output/ioctl branches are
untouched. V2 row/chunk/count/digest/RC/FENCE protocol, the calculated 72-second
C1 timeout, C6 classifier thresholds, C7/C8 sidecar isolation, diag0/V3 route,
and all no-credit rules are byte-unchanged by this repair. Canonical host-only
evidence `/tmp/xv6-c1-recorder-path-static-20260713T022858Z-3117399` and log
SHA-256 `09ac096572069b7e8d6913260c229bb1ce35417f69b31d2b705499baaec8466f`
genuinely exit zero with all static PASS markers and
`js_guest_runtime=UNEXECUTED`; the source-locked route requires the new default,
rejects the old default, proves the explicit stub override, recorder failure,
binary/exact-cap/over-cap, contaminated/interior-gap/reordered/duplicate,
binding/digest/outer-frame, C6/C7/C8, diag0, and no-credit negatives.

Exact audit inventories: start `2026-07-13T02:33:08Z` was total/informational-
RISC-V/conflicting `1/1/0`, PID 3125559; final `2026-07-13T02:37:57Z` was
`1/1/0`, PID 3128320. Both were exact `/proc/*/exe` observations of
`/usr/bin/qemu-system-riscv64`; neither process was touched, and no x86 or other
conflicting QEMU existed. No proof/checker/static replay, build, image write,
rootfs refresh, serial command, QEMU launch, or boot occurred; image access was
limited to current identity hashing and retained proof artifacts. **Scope:**
this PASS authorizes only formation of one fresh foreground 60-second
conductor gate for the later single windowed A1 treatment. It is not VM launch
authority by itself and grants no YouTube, HD720/fullscreen, audio,
`yt-presentfps`, `PERF-VIDEO`, FPS, performance, or default credit.

**Fresh foreground A1 conductor gate — PASS / ONE WINDOWED TRIAL ONLY /
NO-BOOT (2026-07-13):** at explicit `-ff` checkpoint
`f64ce41442ae651ce3833080da4dfdf4ff31805a`, exactly equal to approved
`origin/codex/host-linux-abi-shell-port-ff`, the reviewed source checkpoint is
an ancestor and only plan commits follow the reviewed code. Kernel/user/ports
gitlinks and clean nested heads are exact, only the preserved KDE-smoke dirt
exists, and KVM read/write, WSL DXG/D3D12/GL, X11, Wayland, pinned kernel/image,
and the consumed existing-image console/media receipt all revalidated. With no
other VM worker active, exact start inventory was total/informational-RISC-V/
conflicting `1/1/0` (untouched PID 3135982). One foreground `sleep 60` ran from
`02:44:28Z` to `02:45:28Z`, synchronously reaped through its exact handle with
no polling; the immediate end inventory and full prelaunch audit passed at
`0/0/0`. Evidence is `/tmp/xv6-a1-passive-gate-20260713T024159Z`.

Token `A1-WINDOWED-f64ce414-20260713T024615Z-Q0` authorizes exactly one
subsequent **windowed** real-x86 KVM+virgl A1 treatment on this named branch
(including this docs-only gate-record descendant):
`WAYLAND_CHROMIUM_MULTIPROCESS=1`, audio-disable `1`, media `1`, forced-HD720
`1`, capturediag `0`, EGL `0`, not fullscreen, standard measurement window.
The VM worker must immediately recheck branch/prerequisites and exact all-arch
QEMU conflicts, own and synchronously reap its QEMU process group, and retain a
final exact inventory with zero conflicts; independent RISC-V remains
informational and untouched. Any drift, missing endpoint, conflict, or handle
loss fails without launch authority. This gate launched no VM and grants no
performance, audio-on, fullscreen, or default credit.

**A1 windowed one-shot trial — CONSUMED / INVALID / N=0 (2026-07-13):**
immediate prelaunch at exact explicit `-ff` HEAD/origin
`48d173caccad5d368612c0aef247e56935a3fd77` passed approved remotes and nested
pins, KDE-smoke-only dirt, KVM/DXG/D3D12/GL/X11/Wayland, pinned kernel/image
hashes, the consumed image-proof receipt, and exact QEMU inventory `0/0/0`.
The sole canonical windowed treatment used MP1/audio-disable1/media1/
forced-HD7201/capturediag0/EGL0, a 60-second configured window, GTK fullscreen
and zoom-to-fit off, `QEMU_AUDIO=none`, and 8 GiB RAM. Retain run
`/tmp/xv6-a1-windowed-live-20260713T025202Z-3143430`, external log
`/tmp/xv6-a1-windowed-live-20260713T025202Z-3143430.driver.log`, and scratch
image `/tmp/xv6-yt-20260713T025202Z-pid3143434-mp1-audio1-media1-hd7201.fs.img`.

The first fail-closed boundary was idle C1 V2 transport attempt 1:
`invalid-fbstat-transport ... transport-candidate-contaminated record=37`.
The named raw artifact shows asynchronous `virtio_gpu: page-flip present`
console text split across and merged into chunk records 28 and 32. This is a
transport-atomicity failure before Chromium launch/media proof, not a frame-
supply result. Real KVM, 8 GiB, virgl/OpenGL-submit initialization and matching
media-image assets are retained, but active 1280x720, window-state screenshot,
`yt-presentfps`, `PERF-VIDEO`, FPS/drop/VPQ/retire, frame-supply C6/C7/C8,
audio, and fullscreen facts are absent. All screenshot and Chromium/media
artifacts are explicitly missing in the manifest. The owned PGID ended as
`qemu-system-x86`, was synchronously reaped (`waited:3143596 exp4 0 0`), and
the final exact inventory was `0/0/0`; no retry or second VM ran.

**Next decision:** implement no frame-supply lever from this N=0 run. First
make the C1 envelope emission atomic against asynchronous kernel-console
writes (without weakening chunk/digest/RC/FENCE rejection), obtain fresh
static and independent no-boot review, then use the first valid C8 sample to
choose the ranked frame-supply change. This trial grants no performance,
audio-on, fullscreen, or default credit and leaves VM authority closed.

**A1 idle-C1 V2 contamination forensic — SOURCE CONTRACT DEFECT / NO-BUILD /
NO-IMAGE-WRITE / NO-BOOT (2026-07-13):** bounded review of exact plan commit
`6a4b172771e5ba197d25fea9043b9a3764e27923`, retained run
`/tmp/xv6-a1-windowed-live-20260713T025202Z-3143430`, its external log,
STATUS/metrics/helper, and the 149985-byte raw C1 artifact localizes the
failure without a guest conclusion.  Kernel/user gitlinks are the reviewed
`b42d1c37f90b2ac48aa416a9eb215a935920f649` and
`3e5b90bd30130ad1a3566ddc20c9589f6159d867`; the launched kernel path still
hashes `99b23539...e067`, staged recorder hashes `a50bc99c...b0d34`, and the
consumed image proof binds it to `/bin/consolerecord`.  This is not stale
kernel/user/image provenance.

The diagnostic originates in `virtio_gpu_page_flip_resource()` on the DRM/KMS
ioctl caller's process context, where `printf()` enqueues the complete line to
the async console ring.  Its UART bytes are emitted later by the normal
`consoled` kthread, not by the caller, IRQ, panic, or another emergency path.
`consoled` removes up to 512 bytes but calls `consputs()` in 32-byte steps;
each step independently acquires and releases `console_wire`.  Raw records
36--38 prove a complete page-flip line, then a 96-byte (three-step) diagnostic
prefix immediately followed by the complete sealed seq-28 recorder row, then
the 39-byte diagnostic suffix.  Records 41--44 repeat the shape with a
complete line, a 64-byte (two-step) prefix plus complete sealed seq-32 row,
then a 70-byte suffix.  Thus recorder bytes were not woven internally: each
ioctl row remained contiguous, but it was inserted between kthread steps and
merged with an already-open raw line.  A separate complete page-flip row also
exists between chunks, so merely making each printk line step-atomic would
still violate the unchanged envelope-contiguity contract.

`console_record_write_ioctl()` samples `console_wire_emergency_generation`
only after its timed wire-lock acquisition and compares it after that one row.
Only no-sleep emitters (pre-UART/panic/no-current/IRQ/spin-held) bypass the
wire lock and increment the generation; the value itself is never surfaced.
A change returns `-EAGAIN` after possible row dirt, the silent recorder maps
any return other than exact `data_len` to exit 4, and `fbs.sh` maps any
per-record nonzero to immediate exit 77 with no fallback write.  Here the
normal kthread path acquired the mutex, so the generation did not change:
BEGIN, META, all 301 CHUNKs, and END completed, and outer C1 retained
`RC:0`/`FENCE`.  That proves all 304 recorder calls saw exact-length ioctl
success.  The host correctly rejected the first prefix-merged candidate as
`transport-candidate-contaminated record=37`; this is a producer/serialization
source defect, not a parser defect, and no chunk may be recovered or spliced.

**Smallest safe implementation decision:** replace C1's 304 independent
record ioctls with one bounded batch-record ioctl.  The recorder will submit
the already bounded complete V2 BEGIN/META/CHUNK/END buffer once; the kernel
copies and validates the entire buffer before locking, then holds
`console_wire` across every physical row.  Normal process/kthread/TTY writers
cannot enter between rows.  Sample one emergency generation for the batch and
check it after every row and at completion; an emergency bypass may leave a
partial/corrupt envelope but must abort with non-success so the silent helper,
outer RC/FENCE, and unchanged strict host parser remain fail-closed.  Do not
make page-flip logging alone optional as the fix, relax raw adjacency, accept
interior gaps, or splice prefix-contaminated rows.

Before any new boot require: a fake-UART kernel unit for clean/max batch
contiguity under competing consoled/consolewrite/TTY writers, exact
701-record/357080-logical/357781-physical bounds, and zero pre-emission bytes
for ABI/shape/copy/root/panic/unavailable/timed-lock failures plus explicit
post-emission emergency-generation failure; recorder units for silent exact
success, malformed/over-cap input, and ioctl nonzero; canonical host static
wire cases for clean LF/CRLF/CRCRLF and the exact seq-28/seq-32 contamination,
complete-line interior gaps, corrupt/order/count/offset/digest/RC/FENCE
adversaries, all retaining INVALID/no-credit; x86 kernel and user builds; and
an independent no-boot adversarial review.  Because this decision changes
both kernel UAPI/console behavior and `/bin/consolerecord`, a kernel rebuild,
user rebuild, rootfs/image refresh, and renewed staged-to-image executable/hash
proof are mandatory before a fresh boot.  This run remains N=0; infer no
frame-supply performance from it.

**C1 complete-envelope batch transport — IMPLEMENTED / STATIC+BUILD PASS /
NO-ROOTFS-REFRESH / NO-BOOT (2026-07-13):** at explicit `-ff` source
`8ceaf5f8e5f0ea43668ba341ec91ff56cc5c98ba`, the x86 console now exposes a
32-byte fixed-width v1 batch request and accepts one root-only, fully copied
and prevalidated sequence of at most 701 existing LF records / 357080 logical
bytes / 357781 CRLF physical bytes. It allocates off-stack, acquires the
sleepable wire mutex once for the complete envelope, emits every row in order,
checks the emergency generation before/after each row and at completion, and
returns exact logical bytes only on uncontaminated success. All prelock
metadata/copy/grammar/allocation/unavailable/timeout failures emit zero bytes;
an emergency after emission returns honest no-credit. Both console ioctl
entrypoints share the handler; the existing single-record and separately
gated RISC-V paths remain source-locked.

The silent host-glibc recorder retains its one-record mode and adds exactly
`--batch-file PATH`: it opens a bounded regular non-symlink, verifies the same
record/count/byte grammar, and issues one batch ioctl without output or write
fallback. Automatic staging remains `_consolerecord`, while the image runtime
contract remains `/bin/consolerecord`. Generated C1 now creates one private
regular complete BEGIN/META/CHUNK/END envelope, verifies its 701/357080
limits, invokes `/bin/consolerecord --batch-file` exactly once, and removes
capture/payload/envelope files through its EXIT path. The strict host parser,
outer RC/FENCE, contamination/order/count/offset/digest rejection, C6/C7/C8,
diag0, and all no-credit policy are unchanged. Exact maximum inner wire time
remains 31073 ms; one 50-ms mutex acquisition plus the unchanged 5000-ms
helper reserve yields a source-locked 37-second C1 timeout.

Fresh retained evidence is `/tmp/xv6-c1-batch-20260712-FABgTO`. The kernel
fake-UART runner exited zero (`kernel-host-test.log`, empty SHA-256
`e3b0c442...b855`) and covers max arithmetic, one acquisition, queued normal
writers, ABI/copy/root/allocation/timeout/unavailable zero-prebyte failures,
and emergency post-emission no-credit. The recorder host suite exited zero
(`user-host-test.log`, SHA-256 `a80d40c4...8adf`) across legacy, exact max,
empty/binary/CR/NUL/over-cap/nonregular, ioctl-failure, silence, and one-ioctl
cases. Canonical clean-PATH host-only static output is `static-out`, with
`static-suite.log` SHA-256 `a0de7ad3...822d`; it emitted
`YT-C1-V2-STATIC-PASS` and `YT-PRESENTFPS-STATIC-CHECK-PASS` with
`js_guest_runtime=UNEXECUTED`. Pre/post static and final exact QEMU
inventories were all total/informational-RISC-V/conflicting `0/0/0`.
`git diff --check` across all three repositories passed (empty
`git-diff-check.log`). Synchronous x86 `kernel` and full `user` targets both
exited zero; logs are `kernel-build.log` SHA-256 `f3dcba1f...50d4` and
`user-build.log` SHA-256 `0cbd9a5e...d0f7`. The latter staged executable
`build-x86_64/sysroot/bin/_consolerecord` mode/size/SHA-256
`0755/29616/6f610bf2...cfa6`. No rootfs/image refresh, image write, QEMU
launch, VM, boot, serial, KDE/default, YouTube, HD720, audio, fullscreen,
`yt-presentfps`, `PERF-VIDEO`, or performance credit occurred. The unrelated
KDE-smoke worktree change remains preserved.

Independent integrated no-boot adversarial review then passed without a
rerun or edit: it confirmed the 32-byte offsets/ioctl encoding, exact bounds,
copy/validation/free ordering, one-lock normal-writer exclusion, per-row/end
emergency no-credit, both entrypoints, silent one-ioctl recorder, one-call
helper cleanup, 37-second algebra, and unchanged single-record/RISC-V/parser/
C6-C8/diag0 behavior. Its only operational note was to include the new
`user/tests/` files in the user commit. This clears deepest-first publication
only; rootfs/image refresh and every boot remain closed.

Deepest-first publication completed on the approved explicit remotes: kernel
`6d0151648df87b9d30ffd2dbd0aa780f0c111ec1` is on `origin/v6-kernel` and
user `a9f732fc2e0af7a2eda0c3076e990e8249bfc6b3` is on `origin/v6-port`. The
parent records those gitlinks with the driver/plan change and publishes only
to the full `codex/host-linux-abi-shell-port-ff` ref, never the historical
truncated ref.

**C1 batch-transport deterministic rootfs staging — FAIL AFTER REFRESH /
NO-BOOT (2026-07-13):** at exact published top/kernel/user checkpoints
`de620a307688148bbbf90b431ff582a0f1293b30` /
`6d0151648df87b9d30ffd2dbd0aa780f0c111ec1` /
`a9f732fc2e0af7a2eda0c3076e990e8249bfc6b3`, one fresh private persisted
wrapper was syntax/source audited and invoked exactly once. Evidence is
`/tmp/xv6-c1-batch-rootfs-staging-20260713TnOdTTY`; wrapper SHA-256 is
`3df220491b6841592b81847f45b84f77389cc03c4657aecc1823674ea7bed869`.
It verified the full explicit top/kernel/user/ports remotes, branches and
gitlinks, KDE-smoke-only dirt, required tools, exact pre-kernel artifact
`0644/41359892/3bde70e38384553a1068e002528c767302dcb15366d267652fc631e41e62acd7`,
exact pre-image identity
`73499/8724152320/1783904866/0003dbdee6edd9f1083b7092b83fa9ab36d934eead9c8a72d39f19a7243df287`,
and preflight QEMU `0/0/0`.

The exact foreground kernel, user, and rootfs-refresh stages all returned raw
zero in order; their retained log SHA-256 values are respectively
`a33eae2b8ba62937c7d8881560ce76327812796758dc88ff130ca04235af1330`,
`aeabddd185f51c1dff34412c0c03eb06fb2b5c868ea33a4f368a5dfd747ae620`,
and `06e86c9f427e3c5bf279d6ddbea0d7ba41045b39b91fba91aea1dc46dd43f996`.
Post-kernel mode/size/SHA remained `0644/41359892/3bde70e...acd7`. The staged
`_consolerecord` is regular, non-symlink, executable, 29616 bytes, SHA-256
`6f610bf241c1dd085390858e7cd360955ee187f9bdf350e504df814cab89cfa6`,
and the bounded named-artifact search found `--batch-file`; however its mode
became `0700`. The refreshed image changed identity to
inode/size/mtime/SHA-256
`73499/8724152320/1783914216/66e2ac12a01f4381bb92217b07a7df03712b883704ad7604b6e3e8119840fb1d`.

The first proof failure is exact: read-only named `debugfs stat` returned raw
zero for image `/bin/consolerecord` and proved regular/link-one/29616 bytes,
but mode was `0700`, not required `0755`. The wrapper's private `umask 077`
was inherited by the user/rootfs stages, explaining both staged and image
mode drift. It stopped before console dump/hash equality, all three media
stat/dump/hash proofs, and the single-stat `/bin/_consolerecord` absence
check; none of those unexecuted facts is claimed. Terminal verdict is FAIL,
final exact QEMU inventory is `0/0/0`, and no retry, QEMU, boot, serial, or
image write beyond the authorized rootfs-refresh occurred. The refreshed
image facts are **insufficient for independent pre-boot review**. Any future
authority needs a newly reviewed wrapper that keeps its evidence private
without propagating the restrictive umask into canonical build outputs, then
one fresh full staging/image proof; this consumed invocation cannot be reused.

**C1 corrected staging wrapper static contract — PASS / READY FOR INDEPENDENT
NO-EXEC REVIEW ONLY / NO-BUILD / NO-ROOTFS / NO-BOOT (2026-07-13):** fresh
artifact set
`/tmp/xv6-c1-batch-rootfs-wrapper-v2-static-20260713TynANsT` contains the
uninvoked wrapper `run-batch-rootfs-staging-v2.sh` (SHA-256
`864b585c163bb5579aae9c2f7245484a3bd366dc0148b24f2e392da745062e90`),
semantic checker (SHA-256
`6a37be97bf674a918048c057c755ba2a2b71b8414547143414d785b283629a2c`),
and static driver (SHA-256
`234f270733ea19aaf0db797fe7e13d81f66c649fc402ca00a429ca9ceb6ab07d`).
Both named Bash parser logs are empty PASS outputs; ShellCheck is unavailable
and `shellcheck.log` records `SHELLCHECK_NOT_INSTALLED` rather than claiming a
pass. `checker-canonical.log` ends `WRAPPER_V2_STATIC_CONTRACT_PASS` and
`static-evidence-summary.log` ends `STATIC_EVIDENCE_PASS`. The runtime
directory is absent, proving the wrapper was not invoked.

The wrapper pins batch source base `de620a3` and requires live HEAD to equal
the explicit full `-ff` origin while being a nonempty docs-plan-only
descendant. It pins exact kernel/user/ports heads, approved remotes and
gitlinks, KDE-smoke-only dirt, kernel artifact
`0644/41359892/3bde70e3...acd7`, and the failed-refresh preimage
inode/mode/size/mtime/SHA
`73499/0600/8724152320/1783914216/66e2ac12...0fb1d`. Exact QEMU inventory
classifies only `qemu-system-riscv64` as informational and fails every other
QEMU conflict.

The correction keeps the parent/runtime evidence policy at `umask 077`,
requires a fresh non-symlink `0700` runtime directory, precreates every stage
log `0600`, and runs only each canonical kernel, user, and rootfs-refresh
command inside `( umask 022; ... )`. After every foreground direct-redirection
stage it records the raw exit/log hash and requires the parent umask still be
`0077` and the log still `0600`; build umask cannot leak back. Post-user proof
requires staged `_consolerecord` regular, non-symlink, executable, exact
`0755/29616/6f610bf2...cfa6` with the bounded `--batch-file` artifact match.
Only after all stages pass does it require a changed `0644` host image,
read-only named `debugfs` stat/dump proof that image `/bin/consolerecord` is
regular/link-one/exact `0755` and hash-equal to staged, all three pinned media
assets at exact `0644` mode/size/hash, raw-zero lookup-miss semantics for one
named `/bin/_consolerecord` stat, stable post-proof image identity, final
conflicting-QEMU zero, and one terminal result.

Five syntactically valid controlled mutations are retained with checker exit
one: removing the scoped `umask 022` rejects `build_umask_scope` and
`build_umask_unique`; leaking `umask 022` into the parent rejects
`build_umask_scope`; replacing exact staged mode with executable-only rejects
`staged_console_mode_gate`; deleting the manifest proof rejects
`media_manifest_gate`; and bypassing final inventory rejects
`final_qemu_gate`. Start inspection saw one untouched informational RISC-V
QEMU and zero conflicts; final exact inventory was `0/0/0`. No wrapper,
`cmake`, `debugfs`, old proof, build, rootfs write, QEMU, boot, or serial
command ran, and KDE dirt remains untouched. This clears only a fresh
independent no-exec wrapper review and explicit new one-shot staging
authorization; the prior invocation remains consumed and no image/pre-boot or
performance credit is created here.

**C1 corrected staging wrapper independent no-exec review — PASS / ONE
INVOCATION ONLY / NO-BUILD / NO-ROOTFS / NO-BOOT (2026-07-13):** at exact
explicit-`-ff` plan checkpoint `6ee48c0c1f08c6472477df3339f670e4d64684b8`,
the uninvoked artifact set
`/tmp/xv6-c1-batch-rootfs-wrapper-v2-static-20260713TynANsT` passed direct
manual review. Recomputed wrapper/checker/static-driver SHA-256 values are
`864b585c163bb5579aae9c2f7245484a3bd366dc0148b24f2e392da745062e90`,
`6a37be97bf674a918048c057c755ba2a2b71b8414547143414d785b283629a2c`, and
`234f270733ea19aaf0db797fe7e13d81f66c649fc402ca00a429ca9ceb6ab07d`.
The Bash parser logs are empty, the canonical checker and static summary have
their exact PASS terminals, and ShellCheck remains honestly unavailable—not a
PASS. The runtime directory is absent and no wrapper/checker/static driver was
invoked by this review.

The wrapper exactly pins the published top/kernel/user/ports lineages,
approved remotes and gitlinks, docs-only descendant rule, KDE-only dirt,
kernel/preimage identities, and conflict-free exact-QEMU classification. It
keeps the runtime and all evidence private under parent `umask 077`, precreates
stage logs as `0600`, and runs each foreground canonical stage alone as
`( umask 022; command )`, capturing its raw exit before stopping on the first
failure. Kernel, user, and rootfs-refresh order is fixed. Post-stage gates
require the unchanged kernel identity; staged regular non-symlink
`_consolerecord` at exact `0755/29616/6f610bf2...cfa6` with `--batch-file`;
a changed regular host image; image `/bin/consolerecord` regular/link-one/
`0755` and hash-equal; all three pinned regular/link-one `0644` media files at
exact sizes/hashes; raw-zero `/bin/_consolerecord` lookup-miss semantics; stable
post-proof image identity; and a final zero-conflict QEMU gate before its sole
PASS terminal. It contains no `pgrep`, debugfs write, old proof-script, boot,
VM, or serial route.

All five retained mutations are syntactically valid and genuinely bypass only
their intended active gate while preserving unrelated guards; their diffs,
logs, and `checker_exit=1` receipts recompute consistently. **Authorization:**
invoke this exact unchanged wrapper exactly once, foreground and synchronously,
to run kernel then user then rootfs-refresh and the named read-only image
proofs. Retain the complete runtime directory, receipt, every stage/QEMU/
remote/status/debugfs log and extract, and the wrapper's raw exit. Stop at the
first failure, do not retry, and run no VM, boot, or serial command. Only a
complete PASS may establish refreshed staging/image facts; it grants no
YouTube, HD720/fullscreen, audio, `yt-presentfps`, `PERF-VIDEO`, performance,
or default credit.
