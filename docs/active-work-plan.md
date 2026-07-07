# Active xv6 Work Plan

Last updated: 2026-07-07 (current N8 branch has a completed Qt5Multimedia
opt-in A/B probe, plus a full desktop-interaction kprofile PASS after bounded
guest mouse and clean `LD_BIND_NOW` removal:
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000224Z-n8-guest-input-full-interaction-kprofile`.
It was `direct-launch-only=0`, active sample, bounded guest input
(`input_source=guest`, `monitor_path=disabled`, `host_cursor_sync_ms=0`,
bounded coordinates), hover PASS (`first_changed_ms=923`, diff 300), and
app/kprofile PASS (`konsole_wait_ms=1507`, `kprofile_timeout_hit=0`,
userpc stored=631/drop=0). KVM + virgl stayed real-GL with no software
fallback, and `LD_BIND_NOW` is absent from source, refreshed fs image, and
exact smoke tmp image. Newer N3/R9 guest-mouseinject proof
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T005222Z-n3-guest-mouseinject-kprofile-proof`
validates normal desktop reducers in
`scripts/gpu/kde-plasma-desktop-smoke.expect`: forced guest input,
monitor path disabled, host cursor sync off, guest cursor mode, pre-hover
taskbar/desktop settle, hover/app launch PASS, no lingering QEMU, and clean
diff check. Later user inspection still saw the cursor leave the VM; root
cause split is smoke harness vs normal launcher. The smoke harness now
records guest-only pointer evidence (`input_policy=guest-only`,
`pointer_injection=guest-mouseinject`, `host_cursor_sync=disabled`,
`qemu_monitor_input=0`; HMP/QMP input diagnostic-only), while
`scripts/launch/launch-gui.sh` defaults `QEMU_GTK_CURSOR_MODE=guest` and
`QEMU_GTK_SHOW_CURSOR=off` with overrides preserved. Post-patch validation
PASS
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T024621Z-n3-n8-guest-cursor-postpatch-kprofile`
supersedes invalid kprofile attempt `20260707T024333Z` (`probe_rc=1`):
pre/post QEMU scans empty; command proves `-enable-kvm`, virtio-vga-gl,
GTK `show-cursor=off`, no `virtio_gpu_host_cursor_only=1`; renderer is real
virgl (D3D12 NVIDIA GeForce RTX 4060 Laptop GPU), software fallback envs
unset; guest-only proof has `input_source=guest`, `input_policy=guest-only`,
`pointer_injection=guest-mouseinject`, `host_cursor_sync=disabled`,
`qemu_monitor_input=0`; bounded `/bin/mouseinject 11016 64223 0` on
1280x800/workarea 0,0-1279,799 hit guest pixel 215,783; hover PASS
`first_changed_ms=1048`; app/kprofile PASS (`konsole_wait_ms=2816`,
app-launch `elapsed_ms=4159`, `kprofile_elapsed_ms=3616`,
`kprofile_timeout_hit=0`, userpc 1052/0);
crash scan 0, scoped diff-check passed, scratch image deleted/no `*.img`.
M9 Chromium launch-only guard also passed at
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000512Z-n8-m9-chromium-launch-only-real-gl`:
`browser_seen`, render/drm fds, `gpu_init_error_count=0`, `fault_count=0`,
real GL env/renderer held, and no lingering QEMU. Offline branch scouts say to
avoid glibc/ELF-loader surgery: the loader hotspot is real and repeatable but
too ABI-sensitive; follow-up hwcaps/`LD_LIBRARY_PATH` probes were
measurement-only. XDG path-pruning A/B delta was
limited to `XDG_DATA_DIRS`/`XDG_CONFIG_DIRS` defaults relative to its pre-edit
control, but the broader dirty worktree/scoped files still contain prior N8
helper/harness edits, so do not read raw `git diff` breadth as XDG-only; preedit
archive
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T001509Z-n8-xdg-paths-preedit-direct-launch-kprofile`
and pruned archive
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T002116Z-n8-xdg-paths-pruned-direct-launch-kprofile`.
It reduced lookup churn (ENOENT cold/warm 2816/2396 -> 2658/2228,
`ext4_lookup_enoent` 2124/1848 -> 1973/1681,
`kubuntu-default-settings` 73/70 -> 0/0, `/usr/local/share` 2/2 -> 0/0,
local 97/94 -> 7/1) but did not improve readiness beyond noise:
`konsole_wait_ms` 1389/1298 -> 1399/1317, prompt 1541/1613 -> 1511/1558,
loader share stayed ~43-46%, and top symbols remained dynamic-loader work.
Post-verifier archive-local cheap evidence in the pruned archive:
`post-verify-rootfs-refresh.log` (rootfs-refresh rc=0),
`post-verify-qemu-pgrep.log` (no `qemu-system`; an initial self-match wrapper
attempt is retained as `post-verify-qemu-pgrep.selfmatch-superseded.log`),
`post-verify-xdg-rg.log` (old defaults absent/current defaults present), and
`post-verify-git-diff-check.log` (scoped `git diff --check`).
Loader/env measurement-only matrix used a temporary hook that was applied and
reverted exactly (`/tmp/xv6-worker-h/worker-h-temp-hook-only.diff`), restored
touched-file diff, passed user and rootfs-refresh builds, and left no lingering
QEMU. Valid KVM + virgl real-GL/no-software-fallback archives are
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T011159Z-n8-loader-env-control-direct-launch-kprofile`,
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T011412Z-n8-loader-env-hwcaps-mask0-direct-launch-kprofile`,
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T011905Z-n8-loader-env-ldpath-opt-last-direct-launch-kprofile`,
and
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T012101Z-n8-loader-env-ldpath-last-hwcaps-mask0-direct-launch-kprofile`;
the visible-timeout
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T011622Z-n8-loader-env-ldpath-opt-last-direct-launch-kprofile`
was invalid and rerun. Decision: do not promote hwcaps masking or
`LD_LIBRARY_PATH` reorder as an N8 responsiveness fix; current evidence says
M4 is dynamic-loader relocation/symbol work proper, not XDG, hwcaps, or simple
`/opt` path search. Offline loader-category attribution is now complete with
99.59-100% symbolization: relocation + lookup/hash proper dominate, while
mmap/open/path search is secondary, so the next N8 step stays offline/source
level until there is a concrete change to validate.
Branch decision: accept XDG pruning only as a minor reversible lookup-churn
cleanup, not an M4 responsiveness fix; continue N8 with deeper loader
relocation/lookup attribution or targeted probe design, then boot only to
validate a concrete change. Libimobiledevice no-device shim A/B also closed
no-promote: it removed libssl/libcrypto from Konsole maps but did not improve
readiness. N8 static scout decision: Konsole direct closure is ~134 objects,
~227,504 relocations, ~43,056 undefined dynsyms; top contributors include
Qt5Widgets 23,205, Qt5Quick 20,618, gallium 18,275, crypto 18,081, Qt5Qml
12,089, KIOWidgets 5,370, konsoleprivate 4,833, Qt5Multimedia 3,485, Solid
3,488. N8 NewStuff opt-in SONAME-stub initial + repeat A/B completed with no
default flip: exact 8-symbol KNS/KNSCore ABI surface from libkonsoleprivate
only, real NewStuff maps replaced by shim maps, and KVM+virgl real GL held.
Review says default env-absent paths are safe/no persistent rootfs effect, but
the C `set_kde_env` gate is probe-wide when
`KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`; the harness currently scopes staging
to Konsole-only direct-launch. Repeat weakly supports opt-in candidate only:
warm wait/prompt/direct/kprofile improved
1414/1674/2551/2049 -> 1297/1547/2396/1823, but cold was mixed and
openat/ENOENT worsened. Qt5Multimedia gated probe review (2026-07-07): keep
as scratch opt-in; no-go/revert is not warranted, but adjust before stronger
validation or promotion. Its gate is cleaner than NewStuff: when
`KDE_APP_LAUNCH_PROBE_QTMM_STUB=1`, only the forked Konsole child/subtree gets
the shim `LD_LIBRARY_PATH`; the parent `LD_LIBRARY_PATH` is unchanged, siblings
(terminal/dolphin/kate/kwrite/chromium) do not inherit it, and Chromium-only
or sample paths return before this path. Env-absent defaults have no
persistent rootfs/LD path, staging writes only the temp image, and
`LD_BIND_NOW` removal is unrelated/default no-shim. Proof is enough for a
narrow opt-in candidate only: six `Qt_5` QMedia imports from
`libkonsoleprivate`, no Qt5MultimediaWidgets, six-export SONAME shim, and A/B
archives
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T035129Z-qtmm-control-direct-launch-kprofile`
/
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T035327Z-qtmm-stub-direct-launch-kprofile`
show cold/warm direct deltas -803ms/-303ms with timeout 0. Weaknesses: shim
is still `/tmp`-sourced/built, load-only and not call-safe, call logging was
readiness-window/capped only, and full uncapped maps are missing. Next:
convert QtMM shim to a repo-reproducible opt-in fixture, then repeat/control
with full maps and final post-teardown call log before active-sample,
durable, or default consideration; keep NewStuff as weaker/mixed opt-in.
No glibc/ELF-loader surgery or default flips; always require kprofile metrics
and KVM + virgl real GL/no software fallback.
No commit/push yet. 07-04 compaction baseline: R5
root-caused + fixed, P2 ordered-pageflip default landed, Q2 real-GL
runtime-validated with the first default-path M9 PASS, P3 ext4 slice landed
gated, N2 ext4 default promotion attempted but NOT accepted, N3 R9 coordinate
and visible cursor probes passed, and N4/P1 cpumask+CR0/CR0-only attempts
stopped/reverted with no landing. All verbose evidence chains moved to the
history file and git log; this file holds current status, queue, rules, and
compact lane conclusions only.)

2026-07-07 new-session handoff: user is launching a new session; this is the
handoff state. Audit found no live QEMU/kprofile/KDE smoke processes, so no
running VM cleanup is currently needed. Preserve the dirty workspace; no
commit/push unless explicitly requested. Qt5Multimedia repo-reproducible
opt-in fixture work is partially staged but incomplete: untracked
`scripts/image/build-qtmm-shim.sh`, `scripts/image/xv6-qtmm-shim.c`, and
`scripts/image/xv6-qtmm-shim.map`; `scripts/gpu/kde-plasma-desktop-smoke.expect`
has QtMM staging/final-call-log/full-map support; last pointer
`build_x86_64_last_control_archive.path` points to
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T043906Z-n8-qtmm-repro-control-direct-launch-kprofile`;
later qtmm-repro-control runs are control-only failures/status 8; no matching
stub repeat or `*qtmm-final-call-log.txt` was found. Current authoritative
next action: first audit/finish-or-discard the partial QtMM fixture state,
then produce a repo-owned reproducible opt-in QtMM shim fixture and rerun
control/stub direct-launch kprofile with full uncapped maps and final
call-log. Requirements: KVM+virgl real GL/no software fallback, timeout 0,
userpc drops 0 when collected, app probe PASS, no crashes, no lingering QEMU;
keep all shims opt-in, with no default flips and no glibc/ELF-loader surgery.
Cleanup context: big cleanup already reclaimed ~459.6 GiB, but old generated
leftovers are cleanup candidates only after preserving current evidence
(known examples: old guest-mouseinject history image, old clock-domain smoke
image dir, `build-x86_64/qtmm-shim-check`).

2026-07-07 orchestrator audit (this session, decision recorded): the QtMM
repo-reproducible fixture is ALREADY materially in place, not merely partial.
Repo-owned `scripts/image/xv6-qtmm-shim.c` (6 `Qt_5` QMedia exports, raw
syscalls/`nostdlib`, per-call `/tmp/xv6-qtmm-shim-calls.log` logging tagged
`risk=unsafe_load_only`), `scripts/image/xv6-qtmm-shim.map`
(exactly 6 globals, `local:*`), and reproducible
`scripts/image/build-qtmm-shim.sh` (`SOURCE_DATE_EPOCH=0`,
`--build-id=none`, `--no-undefined`, SONAME + forbidden-NEEDED guards,
sha256/readelf/nm proof emit) all exist. The harness
`scripts/gpu/kde-plasma-desktop-smoke.expect` already wires
`stage_qtmm_stub_shim_if_enabled` (builds via the repo helper, stages to
`/opt/xv6-kde-abi-libs/qt5multimedia-shim/libQt5Multimedia.so.5`), full
uncapped maps (`KDE_INTERACTION_DIRECT_LAUNCH_FULL_MAPS_LOG`), and the
post-teardown final call log (`*.qtmm-final-call-log.txt`,
`kde_direct_launch_qtmm_call_log`). ROOT CAUSE of the 4 interrupted
`20260707T04{2949,3305,3727,3906}Z-n8-qtmm-repro-control` runs: all
`status_code=8` = `sample_desktop-interaction-visible_1-artifact-timeout`
(all-black fb `nonzero=0` at the visible-sample stage) = Failure Mode 19
visible/artifact-timeout flake, NOT a fixture defect — they carried the
flaky `desktop-interaction-latency` visible reducer instead of
direct-launch-only mode. Decision: fixture audited essentially complete;
next is offline build/validate of the fixture, then a control+stub
DIRECT-LAUNCH-ONLY kprofile A/B (skip the flaky visible stage) with full
maps + final call-log, requiring real GL, timeout 0, userpc drop 0, app
PASS, no crash, no lingering QEMU. CONFIRMED PASSING RECIPE (from the
035327Z stub / 035129Z control that predate this fixture):
`KDE_SMOKE_INTERACTION_DIRECT_LAUNCH_ONLY=1`, `direct_launch_repeats=2`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, `input_source=guest`,
`QEMU_GPU=virtio-vga-gl-primary`, `USE_KVM=1`,
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_APP_LAUNCH_PROBE_QTMM_STUB` 0=control / 1=stub.

2026-07-07 A/B run progress (this session): offline fixture build GO
(deterministic sha256 `c0f2b707ce14c2bca305d164842cf8597d47cabe1468703575a5147e6138e77a`,
SONAME `libQt5Multimedia.so.5`, 6 `Qt_5` exports, no DT_NEEDED,
`git diff --check` clean, `.c`<->`.map` 1:1). CONTROL arm
(`QTMM_STUB=0`, direct-launch-only) PASSED: archive
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T0529Z-qtmm-repro-control-direct-launch-kprofile-fixturev2`
(status_code=0 DONE, real GL D3D12/virgl/renderD128 no-software, both
direct-launch probes `result=PASS probe_rc=0` cold/warm 18180/15341ms,
full-maps + `qtmm-final-call-log status=ABSENT` correct for no-shim, zero
crash markers). STUB attempt 1 CRASHED at session-readiness BEFORE
Konsole/shim reached: `pid 68 wireplumber: exception 13 (#GP) rip=0x7fffff666364`,
status_code=3 `kde-session-ready-crash`; shim built OK (repo-helper sha256
matched, staged) and real GL held, but no full-maps/per-probe output.
Classification: shim-INDEPENDENT session-startup crash (wireplumber/PipeWire
does not load the Konsole-scoped shim `LD_LIBRARY_PATH`; control + prior
035327Z stub both passed clean); `rbx=0x302d7265626d756c` = ASCII `"lumber-0"`
= R5-family foreign-bytes-in-register signature shape => candidate
CONTINUOUS-R5-WATCH datapoint, not a QtMM regression. Evidence preserved:
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T0532Z-qtmm-repro-stub-direct-launch-kprofile-fixturev2-SESSION-CRASH`.
Bounded stub rerun PASSED (crash was an independent flake): archive
`build-x86_64/kde-plasma-desktop-smoke-history/20260707T0536Z-qtmm-repro-stub-direct-launch-kprofile-fixturev2`
(status_code=0 DONE, both direct-launch probes `result=PASS probe_rc=0`,
real GL held, no crash markers, no lingering QEMU).
A/B RESULT — DURABLE-CANDIDATE PROOF ACHIEVED (repo-reproducible fixture,
all gates met). konsole_wait_ms cold/warm: control 5920/2885 -> stub
2094/2032 (delta -3826ms ~65% cold, -853ms ~30% warm); bash-prompt
6357/3166 -> 2210/2365; kprofile_elapsed 16720/13102 -> 9553/9907;
kprofile_cpu_busy_ms 105194/86923 -> 65673/67201; userpc stored
3869/3053 -> 1743/1869, dropped 0 in all four; kprofile_timeout_hit=0 all;
sys_openat 5917/5060 -> 4902/4776 (ext4 ENOENT +237 both, minor). MAP
CONTRAST PROVEN: control maps real `/usr/lib/x86_64-linux-gnu/libQt5Multimedia.so.5`
+ `libpulse.so.0`/`libpulsecommon-16.1.so`; stub maps only
`/opt/xv6-kde-abi-libs/qt5multimedia-shim/libQt5Multimedia.so.5` with real
Qt5Multimedia + entire libpulse/pulsecommon closure ABSENT (unique .so
219 -> 208, -11 objects). CALL-SAFETY: stub final-call-log `status=ABSENT`
cold+warm => shim is LOAD-ONLY (6 fake QMedia bodies never invoked; ABI risk
not exercised in this path). Provenance PASS (repo-helper build/stage logs,
sha256 `c0f2b707...` match, SONAME + 6 exports + no NEEDED). Both arms real
GL (virgl/renderD128/D3D12 NVIDIA, no software fallback), app PASS, zero
crash markers, no lingering QEMU. REPEAT-N SWEEP (N=3/temperature) OVERTURNS THE 1x1 COLD WIN. Ran 2 more
interleaved runs per arm (all real GL D3D12/virgl, 0 software hits, DONE,
0 crash markers, no lingering QEMU). Archives:
`20260707T0548...T0552Z-qtmm-repro-sweep-{stub,control}-rep{2,3}-fixturev2`.
konsole_wait_ms cold/warm matrix (control rep1 5920/2885 was a first-boot
cold-cache OUTLIER): control cold [5920,1504,1593] median 1593, warm
[2885,1420,1199] median 1420; stub cold [2094,1897,1495] median 1897, warm
[2032,1187,1553] median 1553. MEDIAN delta stub-control = +304ms cold,
+133ms warm (stub marginally SLOWER); excluding the first-boot control
outlier, control cold mean 1548 vs stub 1696. CONCLUSION: the dramatic
-3826ms 1x1 cold "win" was a cold-cache artifact of the control's first
boot; once host-cache state is controlled, the QtMM load-only stub shows NO
reliable konsole_wait_ms improvement (within run-to-run noise, if anything
marginally slower). DECISION: the repo-reproducible opt-in QtMM fixture is
VALIDATED and SAFE (deterministic build, map replacement proven, load-only
/ ABI-risk-not-exercised, all gates green) and stays as a repo-owned opt-in
via `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1`, but it is NOT a demonstrated M4
responsiveness win and is NOT promoted to active-sample/default. QtMM thus
JOINS the other single-library trims (XDG, hwcaps, libimobiledevice,
NewStuff) that reduce map/lookup churn but do not move readiness beyond
noise — reinforcing that the M4 bottleneck is dynamic-loader
relocation+lookup/hash proper across the whole Qt/KF5 closure, not any one
trimmable library. NewStuff remains the weaker/mixed opt-in; no further
single-library-stub trims are worth pursuing as M4 fixes without new
evidence. The stub-attempt-1 wireplumber #GP (ASCII `"lumber-0"` in rbx) is
filed as a CONTINUOUS-R5-WATCH datapoint at `...-fixturev2-SESSION-CRASH`.
No commit/push, no default flips, dirty worktree preserved.

2026-07-07 ld.so.cache A/B (this session) — VERDICT: correctness/cleanliness
win, NOT an M4 responsiveness lever. Grounding: the image ships NO
`/etc/ld.so.cache` and NO multiarch `/etc/ld.so.conf`, forcing ld.so into
linear dir probing. Added an OPT-IN gated `ldconfig` step to
`scripts/image/make-rootfs.sh` (`XV6_ROOTFS_LDSOCACHE=1`, default OFF,
post-overlay/pre-mkfs; bakes multiarch `ld.so.conf.d` + `ld.so.cache`,
emits entry-count evidence) — DEFAULT NOT FLIPPED, `make-rootfs.sh` change
left dirty/uncommitted. Built a cached treatment image (1352 entries incl
Qt5/KF5) and ran control(no-cache) vs treatment(cached) direct-launch-only
kprofile, N=2/arm, all real GL / PASS / no crash / no lingering QEMU;
default fs.img restored to no-cache after. RESULT: cache IS consulted
(`/etc/ld.so.cache` ENOENT 6->0) and cut syscalls structurally — sys_openat
-17%/-23% (cold/warm), ext4_lookup_enoent -36%/-51% (4551->3790 cold,
3996->2543) — reproducible both reps/both phases. BUT konsole_wait_ms FLAT:
control cold 1488/1492 warm 1189/1298 vs treatment cold 1498/1493 warm
1178/1192 (deltas within noise). Why: openat time saved is only ~50-67ms of
kernel `sys_openat_ms` out of ~1200-1500ms wait; userpc CPU rollup unchanged
(relocation+lookup_hash ~37%, mmap_open/path-search 1-3% and flat). Residual
ENOENT is `LD_LIBRARY_PATH` probing `/opt/xv6-kde-abi-libs` first (146
probes) which the cache cannot short-circuit; trimming LD_LIBRARY_PATH would
recover it but is also syscall-cleanliness, not readiness. Archives:
`20260707T17{1835,1942}Z-ldsocache-control-rep{1,2}` /
`20260707T17{2337,2447}Z-ldsocache-treatment-rep{1,2}`; cache evidence in
`kde-plasma-desktop-smoke-history/ldsocache-experiment-evidence/`. LANE
CONCLUSION: ld.so.cache joins single-library trims + XDG + hwcaps as a
map/syscall-churn cleanup with NO konsole_wait_ms effect — EMPIRICALLY
confirming the M4 bottleneck is loader relocation+symbol-hash-lookup CPU
over the ~219-object Qt/KF5 closure, not I/O or path search. The only
remaining lever that touches that CPU cost is zygote/preload (fork WITHOUT
execve so relocations are inherited COW) — but konsole is not a
kdeinit-loadable module (`libkdeinit5_konsole.so` absent), so it needs
konsole rebuilt as a loadable module or a bespoke preloader (high effort,
uncertain payoff), or relinking the closure (infeasible for prebuilt distro
libs). Cheap guardrail-respecting M4 levers are now EXHAUSTED. Optional
follow-up: ship ld.so.cache (+ LD_LIBRARY_PATH trim) as a default via the
full battery IF a syscall-cleanliness win is wanted on its own merits — but
it is not a responsiveness fix. No commit/push, no default flips, dirty
worktree preserved.

2026-07-07 GAP-TO-LINUX ATTRIBUTION (offline, from existing ldsocache
archives) — MAJOR REFRAME: the M4 launch cost is NOT dominated by userspace
loader compute; it is dominated by KERNEL VFS/ext4-lookup + page-fault cost.
Evidence from the direct-launch kprofile counter dumps (control warm/cold):
`sys_openat_ms=566/643` over `sys_openat_calls=4369/4559` = ~130-141us PER
openat (~50-100x native's ~1-3us), i.e. ~566ms of kernel openat time = ~40%
of the 1189ms warm konsole_wait (`sys_openat_lookup_ms=285/341`,
~65-75us/lookup — the ext4/lwext4 directory-walk + ENOENT-probe path).
Plus `vm_file_faults=40140/40469` page faults per launch, of which only
~3134 hit ext4 (`ext4_fault_calls`, `ext4_fault_ms=28-39` = tiny I/O), so
~37k are MINOR (cached) faults whose per-fault guest trap+handler cost is
uncounted. `sys_poll_*_ms` (poll_blocking 24k-113k ms) is BLOCKING wall
time (idle waiting), NOT CPU — excluded. Warm phase breakdown: konsole
spends 0->754ms just reaching PTY-open (pre-PTY Qt/KF5/loader+fault storm)
out of 1189ms. CRITICAL METHOD NOTE: the prior "~half ld-linux
relocation/lookup" attribution was USER-PC sampling = USERSPACE-ONLY; ticks
landing in kernel mode (openat, fault handler) are not user-PC-sampled, so
that breakdown UNDERCOUNTS kernel VFS/fault time. True picture: a large
share of wall-time is kernel VFS/ext4-lookup + minor-fault handling that
runs ~50-100x costlier per-op than native Linux, amplified by the launch's
~4.4k opens + ~40k faults. THIS is why xv6 (~1.2s warm) is 4-6x slower than
a native Linux VM (~0.3s) at the SAME loader work over the SAME closure:
not the loader algorithm, not loader config, but per-syscall/per-fault
guest cost. LANE PIVOT: the M4 responsiveness lever is now firmly the
KERNEL syscall/VFS/fault path — ext4/lwext4 openat-lookup cost per call
(P3 territory, partially landed) and the minor-page-fault handler cost
(M2/M3 syscall/TLB territory), and/or cutting the storm size (fewer opens:
LD_LIBRARY_PATH trim + cache; fewer faults: prefault/zygote).
MICROBENCH QUANTIFIED (nographic, KVM; getpid_ns 1659-1914 matches M2
1.65-1.94us, tlb_amplification 3075/3094 @1024pg matches M3 band => bench
sane): per-bare-syscall ~1.7us; per-minor-fault ~3-5us central (bounded
[1.7us bare-trap, 8.8us = measured ext4_fault_ms 28/3186]; no direct
per-fault microbench exists in-repo -- mmaptest/mmapbigfile are correctness
only). Storm arithmetic per warm launch (~37,283 minor faults = 40469-3186):
openat 4369x130us = 566ms (measured) + minor-faults 37283x(3-5us) =
112-186ms + ext4 I/O 28ms = KERNEL SUBTOTAL ~706-780ms = 59-66% of the
1189ms warm konsole_wait (>55% even at the 1.7us low bound), and that
EXCLUDES all other launch syscalls (read/close/stat/mmap, each ~1.7us +
TLB-amp). Same storm on native Linux ~30-60ms (dcache openat ~1-5us, minor
fault ~0.5-1us), so xv6's kernel-side EXCESS ~650-720ms is the MAJORITY of
the ~890ms gap (1189-300). VERDICT: kernel syscall+fault cost is FIRST-ORDER
-- the dominant reason xv6 app-launch is 4-6x slower than native. Single
biggest amplifier: openat at ~130us/call (~26-130x native), 566ms alone, of
which sys_openat_lookup_ms=285ms is ext4/lwext4 directory-walk+ENOENT probe
(P3/ext4 lane). Userspace loader compute is at most secondary (~400-480ms
residual, much itself syscall/fault wait). HIGHEST-LEVERAGE M4 TARGETS, in
order: (1) openat/VFS ext4-lookup cost per call (P3 read-path already cut
fault I/O ~50%; the LOOKUP path -- dir-walk + negative-lookup caching -- is
the next slice); (2) minor-page-fault handler cost (M2/M3 trap/TLB/pcache
radix path); (3) shrink the storm (LD_LIBRARY_PATH trim to cut ~4.4k->fewer
opens; prefault/zygote to cut faults). Loader-config levers (ld.so.cache,
single-lib trims) are CLOSED as non-responsive. No commit/push, no default
flips, dirty worktree preserved.

2026-07-07 M4 SLICE IN PROGRESS (kernel, gated, NOT yet validated) —
negative-dentry-cache honor fix. Source scout found: xv6 VFS already has a
full positive+negative dcache (`kernel/kernel/vfs/dcache.c`, committed
8299114) with seq-based invalidation wired into all 8 VFS mutations, BUT the
consumer `vfs_ilookup` (`kernel/kernel/vfs/inode.c:642`) only early-returns
on 0/-ENOMEM, so a negative HIT (`-ENOENT`) falls through and re-walks ext4
under the per-mount esb mutex anyway -- the cache DETECTS but does not AVOID
(explains why warm ext4_lookup_enoent stays ~3685). Fix (UNCOMMITTED kernel
working tree, gate `vfs_neg_dcache=1` DEFAULT OFF): honor the `-ENOENT`
negative hit + `sb->valid` guard on the negative branch. Adversarial review
= NO-GO until 2 gate-ON false-negative blockers fixed: (B1)
`__vfs_dcache_bump_dir_seq` called AFTER `vfs_iunlock` in all 8 mutation
sites -> commit-visible-before-bump window returns stale negatives; fix =
move bump inside the lock (also fixes a pre-existing positive-stale bug).
(B2) procfs/sysfs/devtmpfs expose names without bumping seq -> stale
`/proc/<pid>` negatives; fix = per-sb no-neg-dcache flag, skip store+honor
for synthetic sb. Plus should-fix: xv6fs_lookup caches transient block-read
errors as ENOENT (ext4 clean). Blocker fixes being applied; then RE-REVIEW
before boot, then gated A/B (microbench + warm konsole kprofile: expect
ext4_lookup_enoent down, vfs_lookup_negative_hits up, sys_openat_lookup_ms
~285->~110, konsole_wait_ms ~-170ms) + regression battery (fork/clone/cow
nographic + KDE active-sample). FIRST M4 lever targeting real kernel
critical-path time. No commit/push, no default flips.
VALIDATION RESULT (2026-07-07): re-review returned GO but the boot DISPROVED
it — gate-ON (`vfs_neg_dcache=1`) PANICS at boot:
`kernel/kernel/proc/thread.c:468 init_entry: exec /bin/init failed` (exec
returned -1) ~14s uptime, before KDE. Gate-OFF (`vfs_neg_dcache=0`, SAME
kernel) booted clean (crash=0, baseline counters ext4_lookup_enoent
~3986/3693, konsole_wait 1491/1192) -- so the bump-inside-lock reorder is
safe, but HONORING negatives yields a FALSE NEGATIVE for /bin/init (a
boot-critical existing file reported missing). Failure class the two reviews
missed: boot/mount/early-VFS ordering (a negative cached before the file is
visible, or across a mount, honored later). Archives:
`20260707T192438Z-negdcache-control-rep1` (gate-off, qtquick-accel-policy
FAIL but crash=0 -- likely GL flake, counters valid) and
`20260707T192626Z-negdcache-treatment-rep1` (gate-on, launch-crash, 7 crash
markers, PANIC init_entry). Default remains SAFE (gate default-OFF, no flip).
Root-cause in progress; classify QUICK-FIX (invalidate negatives across
mount / don't cache against not-ready root) vs DEEP model gap => if deep,
PARK negative-honor and keep only the bump-inside-lock fix (which is a
genuine pre-existing positive-cache-stale race fix and passed gate-OFF).
No commit/push, no default flips.
ROOT CAUSE = QUICK-FIX (sentinel overload, NOT stale cache): `..` symlink
resolution. `__vfs_dcache_lookup` returned `-ENOENT` for FOUR cases
(bad-args, ".", "..", genuine negative hit); the gate made `-ENOENT`
load-bearing, so honoring the ".." sentinel broke ".." resolution -> the
`/lib64/ld-linux-x86-64.so.2 -> ../lib/...` symlink failed -> exec failed ->
init panic. FIX (landed in kernel working tree): dcache.c returns `-EAGAIN`
for the 3 sentinel/fall-through cases so `-ENOENT` UNIQUELY means genuine
negative hit; single caller vfs_ilookup treats -EAGAIN as a miss. Nographic
re-verify: gate-ON now boots to userspace (root:/# shell, no PANIC),
gate-OFF clean. KDE A/B RE-RUN (fixed kernel, real virgl/D3D12 GL held,
LIBGL_ALWAYS_SOFTWARE=0): treatment `crash=0` (fix holds under full desktop).
MECHANISM PROVEN (deterministic) BUT NO RELIABLE M4 WIN (N=2, corrected):
ext4_lookup_enoent warm is DETERMINISTICALLY 194 in treatment (both reps
identical) vs control 3685/5236 = -95%; sys_openat_lookup_ms warm 338/330 ->
238/285 = ~-70..-100ms consistently. BUT konsole_wait_ms did NOT reliably
improve: cold control 1496/1488 vs treatment 1299/1600 (mean delta -42ms
with a +-300ms spread; the rep2 -197ms did NOT reproduce -- rep3 was
+112ms), warm control 1195/1190 vs treatment 1190/1186 (flat). The N=1
-197ms "win" was a lucky draw, same 1x1 trap as the QtMM cold-cache artifact.
Archives `20260707T195026Z/195127Z-negdcache-{control,treatment}-rep2` and
`.../rep3`. KEY REFINEMENT: the saved openat/lookup kernel time (~100ms,
real) is LARGELY OFF the serial critical-path-to-shell-ready -- it happens
in parallel across konsole's many threads/processes -- so cutting it does
NOT proportionally cut konsole_wait_ms. This corrects the gap-attribution
takeaway: sys_openat_ms being ~40% of total launch TIME does NOT mean it is
40% of the serial critical path; much of the syscall/fault cost is parallel.
So the neg-dcache honor JOINS ld.so.cache/XDG/single-lib trims: a real,
deterministic structural syscall-churn reduction with NO reliable
konsole_wait_ms effect. The change itself is a correct, valuable VFS fix
(the negative cache was built but non-functional; plus the bump-inside-lock
fixes a pre-existing positive-stale race), worth keeping as gated opt-in, but
NOT an M4 lever and NOT default-promoted (also blocked by the eviction/
no-flush residual: lookup_seq resets to 0 on inode eviction, dcache not
flushed on evict/unmount). NOTE: all 4 runs failed qtquick-accel-policy
(plasmashell QtQuick -> SOFTWARE backend) while the GPU renderer stayed real
virgl/D3D12; does not affect the konsole QWidget metric, identical both arms;
appeared on this kernel but not earlier ldso runs (likely host-WSL-GL drift,
separate issue). BIGGER IMPLICATION FOR M4: since even the kernel-syscall
lever doesn't move konsole_wait_ms, the SERIAL critical path to shell-ready
(relocation of first-needed libs + Qt init before PTY open, ~0-754ms pre-PTY
window) is the real limiter -- reducing PARALLEL syscall/fault cost won't
help; only cutting the SERIAL relocation/init chain (zygote/preload, or
fewer serial dependencies) would. No commit/push, no default flips.

2026-07-07 PIVOTAL MEASUREMENT (LD_DEBUG=statistics on konsole main pid,
real KVM+virgl GL) -- THE LOADER IS NOT THE M4 BOTTLENECK; Qt/KF5 APP INIT
IS. glibc runtime-linker stats for konsole main (219 .so, warm, reproduced
within 3%): total startup in dynamic loader = 57.7M TSC cycles @2688MHz =
~21.5ms; time for relocation 32.2M cyc = ~12ms (40,885 symbol relocs, 34,472
= 84% from lookup cache, 141,292 relative relocs = 182k eager; ~45k
JUMP_SLOT PLT relocs deferred by lazy binding); time to load objects ~8.5ms.
=> ld.so pre-main total ~22ms = ~3% of the ~754ms pre-PTY serial window.
The remaining ~730ms (~97%) is Qt/KF5 APPLICATION INIT after the loader
hands control to main() and before konsole opens the PTY (Qt plugin dlopen,
QML/scenegraph + EGL/Wayland/virgl context bring-up, DBus, theming/icons/
fonts, KConfig/KIO). THIS OVERTURNS THE ENTIRE N8 PREMISE: every lever this
session (XDG, hwcaps, single-lib stubs incl QtMM, ld.so.cache, neg-dcache
lookup honor) targeted the dynamic LOADER, which is only ~3% of the serial
path -- so of course none moved konsole_wait_ms. The earlier "~half ld-linux
relocation/lookup" user-PC reading was AGGREGATE across all threads/procs +
plugin-dlopen-during-Qt-init (PC in ld.so but charged to app time), NOT the
~22ms serial pre-main relocation. Archive of the LD_DEBUG run + stats files
in scratchpad; fs.img restored (konsole wrapper removed), no lingering qemu,
real GL held (virgl D3D12 NVIDIA). NEW M4 DIRECTION: the target is Qt/KF5
init reduction on konsole's SERIAL pre-PTY path. Next measurement: break
down the ~730ms Qt-init window (serial syscalls openat/read/dlopen [sys_openat_ms
was ~566ms/launch -- likely mostly Qt-init config/plugin/font opens, serial],
minor faults, DBus/poll waits, vs compute) to find the biggest serial
component. Candidate levers (all app/framework-level, NOT loader): prune Qt
plugin scanning/QT_PLUGIN_PATH, cut config/theme/icon/font file opens
(caching), reduce DBus round-trips, or -- since each xv6 openat is ~130us
(~50-100x native, ext4-lookup-bound) and Qt init does thousands SERIALLY --
reduce per-openat/per-read kernel cost or the serial op COUNT. This is the
first correct localization of the true M4 bottleneck. No commit/push, no
default flips.

2026-07-07 QT-INIT SERIAL DECOMPOSITION (kernel-accounted pre-PTY poll
attribution + dlopen/wayland trace, real virgl GL, instrumentation
non-perturbing: warm konsole_wait 1420 == shipped median) -- THE M4
BOTTLENECK IS COMPOSITOR/VIRGL ROUND-TRIP WAIT, and it is THE SAME ROOT
CAUSE AS M7. Warm pre-PTY window ~984ms split:
konsole_prepty_poll_total_ms=595 (~60%) = BLOCKING WAIT on round-trips:
Wayland compositor roundtrips 408ms (14 calls, ~29ms each -- native is <1ms;
each is guest->QEMU->host-D3D12->reply virgl latency; konsole cannot create
its window / open the PTY until xdg-surface configure returns) + QDBus
roundtrips 187ms (22 calls, ~8.5ms). dlopen plugin storm 149ms (~15%, 64
dlopens) dominated by IMAGE-CODEC plugins a terminal never needs: kimg_avif
45ms(!), kimg_exr 11ms, kimg_jxl 9ms, kimg_heif/raw ~2-5ms => avif/jxl/exr/
heif/raw ~70-100ms; plus KDEPlasmaPlatformTheme 16ms, qt-wayland-egl 10ms,
breeze 8ms. Residual ~23% = Qt/KF5 compute + initial-closure reloc (~22ms) +
minor faults. The ~566ms/launch sys_openat_ms is WHOLE-LAUNCH multi-process,
runs largely PARALLEL and is mostly OFF this serial path (explains the
neg-dcache null result). VERDICT: the biggest M4 lever is the SAME as M7 --
reduce virgl round-trip latency and/or the count of synchronous Wayland
roundtrips in Qt-Wayland client init (GPU/compositor path, N1/M7 territory,
NOT the loader which is confirmed ~15% and off the WAIT path). SECONDARY
CHEAP WIN: prune the imageformats plugin scan for konsole (avif/jxl/exr/heif/
raw ~70-100ms reclaim, zero function loss for a terminal). M4 lever ladder,
final: (1) virgl round-trip latency / fewer synchronous Wayland roundtrips
(~41%, hard, unifies with M7); (2) DBus roundtrip reduction (~19%); (3)
imageformats-plugin prune (~10%, cheap/safe/measurable); loader levers CLOSED
(~3% initial + ~15% dlopen, and dlopen is mostly the imageformats storm =
lever 3). No commit/push, no default flips.

Single-plan rule: this is the only live plan file. Verbose pre-compaction
records (including the full 2026-07-04 pre-rewrite plan) are preserved
append-only in
`docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md`
("the history file"). Durable mechanisms/workflows live in
`.github/skills/`. Every proof run is archived under
`build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/` (or the
lane-specific proof dirs) — archive names cited below are relative to that.
Branch note: this plan lives on `codex/host-linux-abi-shell-port-ff`;
sibling copies on `codex/host-linux-abi-shell-port` /
`origin/codex/kde-qt-wayland-bringup` predate the 07-04 round.

## Mission

Make the KDE/Chromium desktop on x86_64 KVM+virgl stable and responsive,
with the current primary target being desktop responsiveness that approaches
a comparable Linux VM on the same host/class. Correctness lanes (P0 freeze,
R5 corruption) are fixed or closed; smoke PASS alone is not acceptance. The
remaining work is kprofile-driven app-launch/input responsiveness,
performance (M7 video path, M2 syscall cost, M8 idle CPU), user-visible input
(R9), and statistical closure.

## Scoreboard — Measurements and Goals

Move one metric without regressing the others. Verify the booted cmdline in
every run log (`grep 'x86 kernel cmdline'`).

Measurement validity rules:

- The guest drops ~14% of BSP timer ticks under 6-vCPU desktop load
  (kprofile wall 40000ms vs kernel uptime 34201ms, reconfirmed twice).
  Single-run `*_ms` deltas under load are +/-15% unless cross-checked
  against a monotonic wall reference. Candidate fix (open, N7): compensate
  lost BSP ticks via TSC or let any CPU advance jiffies.
- A kprofile run with `kprofile_timeout_hit=1` AND an incomplete measured
  window is truncated — do not read scoreboard metrics from it. (For
  browser workloads timeout_hit=1 alone is EXPECTED — Chromium never exits;
  judge window completeness by the PERF-VIDEO RESULT/`advanced=` line.)
- Real-GL Chromium video runs need
  `KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE_SECONDS=90`: first-boot virgl->D3D12
  shader compile + cold paging eats the default 40s window.
- kprofile `cpu_busy/total_ms` are scheduler-invocation counts scaled by
  HZ, NOT wall time (scheduler_yield runs per reschedule, ~5x tick rate
  under load); only the busy/total RATIO is meaningful. pgroup fields are
  process-group-only (zygote children escape). kprofile's exec takes
  absolute paths only; guest /bin/sh does not glob (use /bin/bash).
- Plasma responsiveness work MUST invoke kprofile as the metric source after
  every responsiveness iteration/fix/trace, and compare toward Linux-like
  behavior on a comparable same-host/class VM rather than merely beating old
  xv6 smoke thresholds. Acceptance records kprofile elapsed/timeout,
  app-launch latency, Konsole shell readiness, pre-PTY timing, phase-log
  `konsole_wait_ms`, `cpu_busy/total_ms` ratio,
  `pgroup_cpu_runtime_ms`, `sys_poll`/`sys_ppoll`/`sys_openat`,
  `sys_poll_blocking`, `sys_poll_wait_notify`, app-probe PASS, eventfd fd
  attribution and user-PC/module attribution when relevant, interaction
  coverage such as hover/app launch, and mandatory KVM + virgl + real GL proof.
  Software/llvmpipe fallback is invalid. Visual proof supplements the metrics;
  subjective observation alone is not acceptance.

Audio: host audio is NOT a readiness prerequisite. `pactl` probes are
default-off (`kde_pactl_probe=1` to re-enable); non-audio gates run with
`QEMU_AUDIO_BACKEND=none`.

| # | Metric | How measured | Current (2026-07-07) | Goal |
|---|--------|--------------|----------------------|------|
| M1 | YouTube-freeze survival | P0 repro recipe, 15 min, 3 runs | 3/3 responsive clean (2026-07-03 battery); P0 closed | 3/3 clean |
| M2 | Guest `getpid_ns` | `syscalltlb 2000` nographic | accepted baseline 1.65-1.94us (P1 2a/2b landed); N4 stopped/reverted after clean CR0-only rerun failed at 1973/2102/1925/2185ns | < 1.5us (requires a different P1 approach) |
| M3 | `tlb_amplification_ns` (1024 pg) | same | accepted baseline noisy 2.7-3.7us band; clean CR0-only N4 rerun failed at 4504ns | 0 on default boot |
| M4 | `konsole_wait_ms` | KDE desktop-interaction/kprofile | N=3 shipped-default direct-launch kprofile (2026-07-07): konsole_wait_ms warm median 1420 (~1310 excl first-boot cold-cache outlier), cold median 1593 (~1549 excl); all gates green (real GL, timeout 0, userpc drop 0, app PASS, no crash); user-PC attribution ~half ld-linux (lookup_hash 17-20% + relocation 17-18%), ~12% libc, ~16-20% Qt5, loader-share ~63-66% of symbolized loader/libc/xv6, 219-`.so` closure, ~4.3-4.5k openat/~3.7k ENOENT per launch; top symbols `_dl_new_hash`/`resolve_map`/`check_match` | converge toward comparable Linux VM behavior |
| M5 | `first_visible_ms` | same | 6473 in clean N8 LD_BIND_NOW-off proof; direct-launch prompt 1508/1429 | < 15000 |
| M6 | `mesakmsgl` direct-KMS FPS | pageflip A/B | 118-125 ordered, now DEFAULT-ON | done (was: ordered default) |
| M7 | `presentedFPS` (60fps video) | chromium-video kprofile, KPROFILE_SECONDS=90 | 51.0 default after total-reaper redesign; native_present_credit remains 0 and native-present is NOT solved. This is not the current app-launch bottleneck | >= 55 (N1) |
| M8 | Idle-desktop host CPU | 10s `/proc/$pid/stat` utime+stime delta on a GL-pipeline boot (non-GL boots never present — invalid for M8) | UNMEASURED-VALID: the 2026-07-05 44%/57-64% readings were doubly invalid (non-GL boots with zero presentation AND poll flags since fully reverted for interactive hangs). Historical band 85-130%. Re-measure with the documented GL recipe on shipped defaults | < 100% |
| M9 | Chromium window visible | chromium-video launch-only reducer | PASS (2026-07-07 M9 launch-only guard): `browser_seen`, render/drm fds, `gpu_init_error_count=0`, `fault_count=0`, real GL env/renderer, no lingering QEMU | PASS (holds; guard in every battery) |

Fork-safety gate for any syscall/scheduler/TLB/mm change: `forktest`
(rc=1 "fork claimed to work N times!" = known exhaustion signature),
`clonetest` rc=0, `cowtest` rc=0, same boot.

## Current Plasma Responsiveness / kprofile Status (2026-07-07)

- Current accepted N8 workflow proof is the full desktop-interaction kprofile
  PASS at
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000224Z-n8-guest-input-full-interaction-kprofile`.
  It ran with `direct-launch-only=0` as an active sample, using bounded guest
  `/bin/mouseinject` for normal input (`input_source=guest`,
  `monitor_path=disabled`, `host_cursor_sync_ms=0`, bounded coordinates).
  Hover passed (`first_changed_ms=923`, diff 300); app/kprofile passed with
  `konsole_wait_ms=1507`, `kprofile_timeout_hit=0`, and userpc stored=631,
  dropped=0. KVM + virgl real GL held with no software fallback, and
  `LD_BIND_NOW` is absent from source, refreshed fs image, and the exact smoke
  tmp image.
- Current N3/R9 guest-mouseinject proof is archived at
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T005222Z-n3-guest-mouseinject-kprofile-proof`.
  `scripts/gpu/kde-plasma-desktop-smoke.expect` now forces normal desktop
  reducers to guest input (`input_source=guest`), `monitor_path=disabled`,
  `host_cursor_sync=0`, `QEMU_GTK_CURSOR_MODE=guest`, and
  `QEMU_GTK_SHOW_CURSOR=off`; HMP/QEMU monitor mouse movement is
  diagnostic-only and disabled for normal reducers. Pre-hover waits for
  taskbar/desktop settle by default: 3000ms plus 3 stable framebuffer samples.
  PASS proof: `status_code=0`, KVM + virgl real GL/no software fallback,
  `cursor_owner=guest-forced-normal-interaction`, bounds
  `framebuffer=1280x800 workarea=0,0-1279,799`, settle
  `elapsed_ms=4711 stable_count=3`, `/bin/mouseinject 11016 64223 0`,
  `host_cursor_sync_enabled=0`, `host_cursor_sync_ms=0`,
  `status=changed first_changed_ms=986`, app launch PASS with
  `konsole_wait_ms=1508`, `kprofile_timeout_hit=0`, userpc stored=602
  dropped=0, no lingering QEMU, and clean diff check. Follow-up after user
  visual inspection split the issue into smoke harness vs normal launcher:
  normal desktop-interaction/direct-launch smoke paths are guest-only pointer
  injection with `input_policy=guest-only`,
  `pointer_injection=guest-mouseinject`, `host_cursor_sync=disabled`, and
  `qemu_monitor_input=0`; monitor/HMP/QMP input is diagnostic-only.
  `scripts/launch/launch-gui.sh` now defaults `QEMU_GTK_CURSOR_MODE=guest`
  and `QEMU_GTK_SHOW_CURSOR=off` while preserving overrides. Post-patch full
  validation PASS:
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T024621Z-n3-n8-guest-cursor-postpatch-kprofile`.
  Earlier `20260707T024333Z` was invalid kprofile usage (`probe_rc=1`) and
  superseded. Proof: pre/post QEMU scans empty; qemu command had
  `-enable-kvm`, `virtio-vga-gl`, GTK `show-cursor=off`, and no
  `virtio_gpu_host_cursor_only=1`; renderer real virgl (D3D12 NVIDIA
  GeForce RTX 4060 Laptop GPU); software fallback envs unset; guest cursor
  proof `input_source=guest`, `input_policy=guest-only`,
  `pointer_injection=guest-mouseinject`, `host_cursor_sync=disabled`,
  `qemu_monitor_input=0`; bounded `/bin/mouseinject 11016 64223 0`,
  framebuffer 1280x800, workarea 0,0-1279,799, guest pixel 215,783; hover
  PASS `first_changed_ms=1048`; app/kprofile PASS
  (`konsole_wait_ms=2816`, app-launch `elapsed_ms=4159`,
  `kprofile_elapsed_ms=3616`, `kprofile_timeout_hit=0`, userpc 1052/0);
  crash scan 0; scoped diff-check passed for
  `scripts/gpu/kde-plasma-desktop-smoke.expect` and
  `scripts/launch/launch-gui.sh`; scratch image deleted/no `*.img` in
  archive. The live N8 responsiveness bottleneck remains
  Konsole/app-launch readiness/loader work, not mouse bounds.
- Chromium M9 launch-only guard PASS is archived at
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000512Z-n8-m9-chromium-launch-only-real-gl`.
  It saw the browser plus render/drm fds, `gpu_init_error_count=0`,
  `fault_count=0`, real GL env/renderer held, and no lingering QEMU.
- XDG path-pruning A/B experiment delta was limited to `XDG_DATA_DIRS` and
  `XDG_CONFIG_DIRS` defaults relative to its pre-edit control. Caveat: the
  same scoped files in the current dirty worktree also carry prior N8
  helper/harness edits, so the raw worktree is not an XDG-only patch. Preedit:
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T001509Z-n8-xdg-paths-preedit-direct-launch-kprofile`;
  pruned:
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T002116Z-n8-xdg-paths-pruned-direct-launch-kprofile`.
  It reduced cold/warm ENOENT totals 2816/2396 -> 2658/2228 and
  `ext4_lookup_enoent` 2124/1848 -> 1973/1681. Specific churn wins:
  `kubuntu-default-settings` 73/70 -> 0/0, `/usr/local/share` 2/2 -> 0/0,
  local 97/94 -> 7/1. Readiness did not improve beyond noise:
  `konsole_wait_ms` 1389/1298 -> 1399/1317, prompt 1541/1613 -> 1511/1558;
  loader share stayed ~43-46% and top symbols remained dynamic-loader work.
  Post-verifier cheap evidence was archived in the pruned directory:
  `post-verify-rootfs-refresh.log` (rootfs-refresh rc=0, `fs.img` refreshed),
  `post-verify-qemu-pgrep.log` (no lingering `qemu-system`; superseded
  self-match pgrep attempt retained separately), `post-verify-xdg-rg.log`
  (old XDG defaults absent/current defaults present), and
  `post-verify-git-diff-check.log` (scoped `git diff --check`).
- Offline branch scouts concluded to avoid glibc/ELF loader surgery. The
  loader hotspot remains real and repeatable, but too ABI-sensitive for the
  next branch. Follow-up hwcaps/`LD_LIBRARY_PATH` probes were
  measurement-only; IFUNC remains only a possible measurement probe if useful.
- Loader/env measurement-only matrix completed with no permanent hook,
  default, or env change: the temporary hook was applied and reverted exactly
  from `/tmp/xv6-worker-h/worker-h-temp-hook-only.diff`, touched-file diff was
  restored, user and rootfs-refresh builds passed, and no QEMU lingered.
  Valid KVM + virgl real-GL/no-software-fallback archives: control
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T011159Z-n8-loader-env-control-direct-launch-kprofile`;
  hwcaps-mask0
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T011412Z-n8-loader-env-hwcaps-mask0-direct-launch-kprofile`;
  ldpath-opt-last
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T011905Z-n8-loader-env-ldpath-opt-last-direct-launch-kprofile`;
  ldpath-last+hwcaps
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T012101Z-n8-loader-env-ldpath-last-hwcaps-mask0-direct-launch-kprofile`.
  Invalid visible-timeout archive, rerun passed:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T011622Z-n8-loader-env-ldpath-opt-last-direct-launch-kprofile`.
  Control: wait 1297/1196, prompt 1510/1431, CPU ratio .546/.563, pgroup
  839/845, userpc 437/0 and 525/0, loader share .416/.465, ext4 ENOENT
  1980/1679, openat 2467/2355, `/opt` rows 145/145, glibc-hwcaps 66/66.
  hwcaps-mask0: wait 1395/1317, prompt 1608/1545, loader .429/.430, ext4
  ENOENT 2020/1679, `/opt` 145/145, glibc-hwcaps 66/66; no improvement and
  masking did not reduce hwcaps path rows here. ldpath-opt-last: wait
  1489/1187, prompt 1600/1428, loader .415/.427, ext4 ENOENT 1793/1430,
  openat 2288/2106, `/opt` rows 28/0, glibc-hwcaps 84/90; cuts lookup churn
  but cold readiness worsened, no stable M4 win, loader remains top module.
  ldpath-last+hwcaps: wait 1402/1051, prompt 1538/1301, loader .414/.433,
  ext4 ENOENT 1726/1428, openat 2177/1956, `/opt` rows 20/0, glibc-hwcaps
  84/90; best warm number, but small repeat count/no clean cold win, still
  loader top.
- Offline loader-category attribution completed without a VM boot, commit, or
  push. `scripts/gpu/kprofile-userpc-phase-attribution.py` now preserves
  existing rows and adds `category=<...>` on `direct-launch-phase-symbol`
  rows plus `direct-launch-phase-loader-category` rollups. Verifiers:
  `python3 -m py_compile scripts/gpu/kprofile-userpc-phase-attribution.py`,
  `git diff --check -- scripts/gpu/kprofile-userpc-phase-attribution.py`, and
  `git diff --no-index --check` because the script is untracked. Reports:
  `n8-loader-category-attribution-01-cold.txt` and
  `n8-loader-category-attribution-02-warm.txt` in each valid loader/env
  archive above. Compact LTP/category matrix: control cold 388 samples
  (loader/libc/xv6 169/37/2; relocation 65, 16.75%; lookup 63; mmap/open 14),
  control warm 472 (232/56/2; lookup_hash 95, 20.13%; relocation 82;
  mmap/open 13), hwcaps cold 389 (177/46/2; lookup_hash 71, 18.25%;
  relocation 67; mmap/open 5), hwcaps warm 441 (198/64/1; relocation 78,
  17.69%; lookup 72; mmap/open 5), ldpath cold 388 (174/43/0; relocation 66,
  17.01%; lookup 60; mmap/open 16), ldpath warm 380 (177/37/5; relocation 69,
  18.16%; lookup 62; mmap/open 4), combined cold 383 (162/51/1; relocation 77,
  20.10%; lookup 58; mmap/open 2), combined warm 369 (171/42/1;
  relocation/lookup tie 69, 18.70%; mmap/open 6). Backing symbols include
  `_dl_new_hash`, `do_lookup_x`, `check_match`, `resolve_map`,
  `elf_machine_rela_relative`, `elf_dynamic_do_Rela`,
  `_dl_map_object_from_fd`, `strcmp`, and `__memset_avx2_unaligned_erms`.
  Symbolization coverage was 99.59-100%.
- Libimobiledevice no-device shim A/B is complete and no-promote. ABI scout
  was safely small: `libKF5Solid.so.5.115.0` imports exactly 10 `Base`
  symbols from `libimobiledevice-1.0.so.6`
  (`idevice_event_subscribe`, `idevice_event_unsubscribe`,
  `idevice_get_device_list`, `idevice_device_list_free`, `idevice_new`,
  `idevice_free`, `lockdownd_client_new`, `lockdownd_client_free`,
  `lockdownd_get_device_name`, `lockdownd_get_value`). A temporary opt-in
  no-device shim was built/used and the repo hook reverted; saved artifacts:
  `/tmp/xv6-worker-o/worker-o-temp-fsimg-hook.patch`,
  `/tmp/xv6-worker-o/xv6-libimobiledevice-nodevice-shim.c`, `.map`, and
  `libimobiledevice-1.0.so.6`. Verifiers passed: rootfs-refresh,
  `git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect`,
  `python3 -m py_compile scripts/gpu/kprofile-userpc-phase-attribution.py`,
  readelf SONAME/libc-only NEEDED/exactly-10-exports proof, no lingering
  QEMU, KVM + virgl real GL in run logs and qtquick accel policy, no software
  fallback, and clean crash-marker scan. Archives: control
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T020310Z-n8-libimobiledevice-control-direct-launch-kprofile`;
  shim
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T020424Z-n8-libimobiledevice-nodevice-shim-direct-launch-kprofile`.
  Compact metrics:
  | run | wait | prompt | timeout | userpc | CPU | pgroup | openat | ENOENT | loader | categories |
  |---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
  | control PASS | 1983/1208 | 2199/1519 | 0/0 | 579/0, 499/0 | .533/.546 | 1229/857 | 2617/2368 | 1979/1681 | 38.35%/38.90% | reloc/lookup/mmap 78/75/9, 66/64/11 |
  | shim PASS | 2085/1211 | 2302/1521 | 0/0 | 606/0, 482/0 | .524/.572 | 1301/849 | 2584/2306 | 1961/1678 | 42.99%/47.20% | reloc/lookup/mmap 82/93/8, 81/76/7 |
  Control maps had imobiledevice+ssl+crypto; shim maps had the imobiledevice
  shim and ssl/crypto gone. Shim call evidence:
  `kde-session-plasma-child.log` showed `idevice_event_subscribe`,
  `idevice_get_device_list result_count=0`, and
  `idevice_device_list_free`; Konsole logs showed the shim loaded.
- NewStuff opt-in SONAME-stub initial + repeat/control are complete
  (2026-07-07), gated only by `KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`; no
  default flip. Review: env-absent paths are default-safe with no persistent
  rootfs effect, and default desktop/Plasma/Chromium normal paths are
  unaffected. Caveat: C-side `set_kde_env` in
  `scripts/image/kde-app-launch-probe.c` is probe-wide when the env is set,
  while `scripts/gpu/kde-plasma-desktop-smoke.expect` currently scopes staging
  and export to the Konsole-only direct-launch probe. The shim is staged from
  `/tmp/xv6-newstuff-shim/` into the tmp image; current minimal patch is
  `/tmp/xv6-newstuff-shim/newstuff-probe-gate.patch` touching
  `scripts/image/kde-app-launch-probe.c` and
  `scripts/gpu/kde-plasma-desktop-smoke.expect`. If promoted, add
  repo-owned shim source/map/build,
  tighten or clearly document the Konsole-only gate, and do not commit whole
  dirty files blindly.
  ABI audit: exactly 8 direct KNS/KNSCore imports, all from
  `libkonsoleprivate.so.1`; `konsole` and `libkonsoleapp` import none.
  Required shadow SONAMEs are `libKF5NewStuffWidgets.so.5` and
  `libKF5NewStuffCore.so.5`; the shim exports only `KNSWidgets::Button` ctor,
  `setConfigFile`, `dialogFinished`, `staticMetaObject`, and
  `KNSCore::EntryInternal` `name`/`installedFiles`/`uninstalledFiles`/`status`.
  ABI risk remains: fake `staticMetaObject`, no real QObject construction,
  vtable, or destructor coverage, no-op methods, and empty Qt returns.
  Previous and repeat runs did not call-cover risky bodies because shim call
  log was absent or explicitly absent-expected.
  Initial archives:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T030649Z-n8-newstuff-control-direct-launch-kprofile`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T030825Z-n8-newstuff-stub-direct-launch-kprofile`;
  warm direct/kprofile improved 3153/2541 -> 2363/1968 while cold moved only
  slightly positive. Repeat archives: control
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T031947Z-n8-newstuff-repeat-control-direct-launch-kprofile`;
  stub
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T032139Z-n8-newstuff-repeat-stub-direct-launch-kprofile`.
  Repeat wait/prompt/direct/kprofile: cold 1776/1995/2710/2220 ->
  1747/1857/2847/2209 (deltas -29/-138/+137/-11); warm
  1414/1674/2551/2049 -> 1297/1547/2396/1823
  (deltas -117/-127/-155/-226). `kprofile_timeout_hit=0` and userpc dropped=0
  in all four; stored samples control 546/557, stub 528/548. CPU/pgroup was
  roughly flat to slightly better, but openat/ENOENT worsened: control
  2594/1975 and 2343/1703; stub 2819/2220 and 2601/1923. Loader categories
  were mixed: warm relocation/mmap 91/14 -> 70/9, but lookup 83 -> 95; cold
  neutral/mixed.
  Real GL held (KVM, virgl D3D12 NVIDIA RTX 4060, no llvmpipe/softpipe,
  fallback envs unset). Maps show control real `libKF5NewStuff*.so.5` and
  treatment shim only; warm control had Qt5Qml/Qt5Quick maps, warm stub did
  not in the capped snapshot, and full uncapped live maps were not collected;
  `maps-artifact-limitation.txt` was added. Treatment archive has shim
  checksum/readelf/SONAME/export proof plus explicit call-log
  absent-expected. Crash scan clean, no lingering QEMU, no scratch images.
  Decision: repeat weakly supports keeping NewStuff as an opt-in promotion
  candidate, but does NOT justify durable/default promotion. Warm improvement
  repeated but smaller; cold was mixed; VFS/open counts worsened. Before
  further promotion, tighten the C gate to Konsole-only or document it
  clearly.
- Qt5Multimedia gated probe review (2026-07-07): keep as scratch opt-in; no
  default flip, and no-go/revert is not warranted, but adjust before stronger
  validation or promotion. Gate
  `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` is cleaner than NewStuff: the shim
  `LD_LIBRARY_PATH` is scoped to the forked Konsole child/process subtree, the
  parent `LD_LIBRARY_PATH` is unchanged, terminal/dolphin/kate/kwrite/chromium
  siblings do not inherit it, and Chromium-only/sample paths return before
  this path.
  NewStuff remains probe-wide when armed via `set_kde_env`. Env-absent paths
  have no persistent default/rootfs LD path; staging writes only the temp
  image. Startup `LD_BIND_NOW` removal is unrelated/default no-shim.
  A/B archives:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T035129Z-qtmm-control-direct-launch-kprofile`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260707T035327Z-qtmm-stub-direct-launch-kprofile`;
  cold direct/helper/kprofile/wait/prompt moved
  3821/3733/3030/2335/2448 -> 3018/2962/2239/1776/1994; warm moved
  2960/2876/2357/1630/2053 -> 2657/2593/2071/1418/1736; timeout stayed 0 and
  real KVM + virgl GL held. Load-only symbols/version/SONAME are enough for
  observed imports: exactly six `Qt_5` QMedia imports from
  `libkonsoleprivate.so.1`, no direct `libQt5MultimediaWidgets`, six exports,
  SONAME `libQt5Multimedia.so.5`, no DT_NEEDED, checksum
  `93ec68fb8cde4eca15638aa6a8341b06e78f21a3acf6ce063ea5eb8af4229018`.
  Weaknesses: shim is still only `/tmp` source/built; durable opt-in needs
  repo-owned source plus version script, reproducible build/stage, and
  archived source/map/build/checksums/readelf proof. It is not call-safe:
  fake `QMediaPlayer` is not a `QObject`, `QMediaContent` layout/ref state is
  uninitialized, and later media calls/`deleteLater`/signal-slot paths are
  high-risk/no-go. Call logging was limited to the readiness window/capped
  output; next repeat must archive the final call log after teardown. Proof is
  sufficient for a narrow opt-in candidate, not durable/default; missing full
  uncapped maps must prove Qt5Multimedia/libpulse/tail absence in stub and
  real presence in control. Next action: convert QtMM shim to a
  repo-reproducible opt-in fixture, then repeat/control with full maps and
  final call-log before any broader active-sample or durable consideration;
  keep NewStuff as the weaker/mixed opt-in candidate.
- Branch decision: accept XDG pruning as a minor reversible lookup-churn
  cleanup, not an M4 responsiveness fix; do not promote hwcaps masking or
  `LD_LIBRARY_PATH` reorder, libimobiledevice/crypto-chain trimming, or
  NewStuff by default as an N8 responsiveness fix yet. LD path-last may stay a
  future minor lookup-churn
  cleanup candidate only after stronger repeat/control, but current evidence
  says the M4 bottleneck is dynamic-loader relocation + lookup/hash proper,
  Qt/KF5/QML/KIO or Konsole-private contributors, not XDG, hwcaps, simple
  mmap/open/`/opt` path search, or the libimobiledevice crypto chain.
  Static scout decision: Konsole direct closure is ~134 objects, ~227,504
  relocations, and ~43,056 undefined dynsyms; top contributors include
  libQt5Widgets ~23,205, libQt5Quick ~20,618, libgallium ~18,275, libcrypto
  ~18,081, libQt5Qml ~12,089, libKF5KIOWidgets ~5,370,
  libkonsoleprivate ~4,833, libQt5Multimedia ~3,485, and libKF5Solid ~3,488.
  NewStuff remains an opt-in promotion candidate only after initial + repeat
  A/B: real NewStuff maps were replaced by shim maps with the exact 8-symbol
  KNS/KNSCore surface, warm improvement repeated but smaller, cold was mixed,
  and openat/ENOENT worsened. Review also found the env-armed C gate is
  probe-wide and the ABI risk was not call-covered. QtMM is a cleaner gated
  scratch opt-in: `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` scopes the shim
  `LD_LIBRARY_PATH` to the forked Konsole child/process subtree, leaves the
  parent `LD_LIBRARY_PATH` and sibling apps untouched, and has no persistent
  default/rootfs LD path when absent. Its A/B replaced real Qt5Multimedia/Pulse
  maps with the six-symbol shim for Konsole direct-launch, direct time improved
  by -803ms cold / -303ms warm, `kprofile_timeout_hit=0`, and real KVM +
  virgl GL held. Do not default-promote it yet: the shim is `/tmp`-only, map
  snapshots are capped, final post-teardown call logs are missing, and
  load-only coverage is not call-safe.
  KIO direct edge is entangled/no-go; Solid/crypto no-go follows the prior
  libimobiledevice shim result; Gallium/GL no-go because real GL is mandatory.
  Next N8 action is to convert QtMM to a repo-reproducible opt-in fixture,
  then repeat/control with full maps and final call-log before active-sample,
  durable, or default consideration; keep NewStuff as the weaker/mixed opt-in
  candidate.
  No glibc/ELF-loader surgery or default flips; keep
  kprofile, KVM + virgl real GL, and no software fallback as validation gates.
  Do not spend the next branch on generic GPU/hover/syscall work unless a
  guardrail fails. Mouse/cursor follow-up does not change this: next action is
  still kprofile-driven Konsole/app-launch responsiveness with KVM + virgl
  real GL; manual/user inspection can relaunch via
  `scripts/launch/launch-gui.sh` using guest cursor defaults. No commit/push
  yet.
- Background N8 proofs still relevant for context: clean `LD_BIND_NOW` direct
  replay
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260706T231950Z-n8-ld-bind-now-off-clean-startup-direct-launch-kprofile`
  superseded the stale-startup-contaminated
  `20260706T225715Z-n8-ld-bind-now-off-direct-launch-kprofile`; attribution
  fix proof remains
  `build-x86_64/kde-plasma-desktop-smoke-history/20260706T182619Z-n8-exec-opened-path-userpc-maps-validation/`;
  clock-domain proof remains
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-clock-domain-uptime-20260706-1538`.
  Existing commit-hygiene caveat:
  `scripts/image/konsole-wayland-event-trace-preload.c` is currently
  untracked, so later commit hygiene must account for it before relying on
  CMake/rootfs dependencies in a commit.
- Retired/background N8 evidence, including older artifacts, raw tables,
  flaky/failed attempts, reducer details, and raw attribution rows, belongs in
  the history file and archived proof directories, not this live plan.

## Work Order — Current Queue (recut 2026-07-07 addendum)

The 07-03 Q1-Q7 queue is RETIRED: Q1 landed, Q2 runtime-validated (real
GL), Q5 root-caused+fixed, Q7 landed. New queue; current next action is
N8:

- N8 = Linux-like Plasma responsiveness (CURRENT NEXT ACTION,
  kprofile-driven): current branch starts from the full desktop-interaction
  PASS
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000224Z-n8-guest-input-full-interaction-kprofile`
  plus the Chromium M9 real-GL guard PASS
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T000512Z-n8-m9-chromium-launch-only-real-gl`.
  Bounded guest input, hover, app launch, clean `LD_BIND_NOW` absence, userpc
  no-drop storage, KVM + virgl real GL, and no lingering QEMU are proven.
  XDG path pruning is accepted only as minor reversible lookup-churn cleanup:
  its A/B experiment delta is XDG-defaults-only relative to the pre-edit
  control, while the wider dirty helper/harness files still include prior N8
  edits. It reduced ENOENT/local/default-settings misses but did not move
  `konsole_wait_ms` beyond noise, and loader share stayed ~43-46%. The pruned
  archive now includes post-verifier static/rootfs/no-lingering-QEMU evidence.
  Completed measurement-only hwcaps/`LD_LIBRARY_PATH` matrix was
  no-go/no-promotion: hwcaps masking did not help, LD path-last cut lookup
  churn but did not produce a stable M4 win, and loader remained the top
  module. Offline loader-category attribution now says relocation plus
  lookup/hash proper dominate, while mmap/open/path search is secondary.
  Libimobiledevice no-device shim A/B was also no-go/no-promotion: ABI surface
  was exactly 10 imported `Base` symbols, the temporary shim removed
  libssl/libcrypto from Konsole direct-launch maps, but cold readiness
  worsened and warm stayed flat. Do not pursue libimobiledevice/crypto-chain
  trimming as an M4 fix unless new evidence appears. NewStuff opt-in initial +
  repeat A/B are complete, no default flip: the gate is
  `KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB=1`, direct Konsole ABI surface is exactly
  8 KNS/KNSCore imports from `libkonsoleprivate.so.1`, real NewStuff maps were
  replaced by shim maps, and real GL held. Initial warm
  wait/prompt/direct/kprofile moved 1914/2131/3153/2541 ->
  1403/1642/2363/1968; repeat control/stub archives are
  `20260707T031947Z-n8-newstuff-repeat-control-direct-launch-kprofile` and
  `20260707T032139Z-n8-newstuff-repeat-stub-direct-launch-kprofile`, with warm
  1414/1674/2551/2049 -> 1297/1547/2396/1823 but mixed cold
  1776/1995/2710/2220 -> 1747/1857/2847/2209 and worse openat/ENOENT.
  Review found env-absent paths default-safe/no persistent rootfs effect, but
  the env-armed C gate is probe-wide while the harness scopes it to
  Konsole-only direct-launch; ABI risk remains uncalled because the shim call
  log is absent/absent-expected. Treat NewStuff as opt-in candidate only, not a
  durable/default promotion. Qt5Multimedia review conclusion: keep as gated
  scratch opt-in; no-go/revert is not warranted, but adjust before stronger
  validation or promotion. `KDE_APP_LAUNCH_PROBE_QTMM_STUB=1` is cleaner than
  NewStuff because the shim `LD_LIBRARY_PATH` is scoped to the forked Konsole
  child/subtree; parent `LD_LIBRARY_PATH` is unchanged; terminal, dolphin,
  kate, kwrite, and chromium siblings do not inherit it; Chromium-only/sample
  paths return earlier; env-absent default/rootfs paths are clean; and staging
  writes only the temp image.
  Startup `LD_BIND_NOW` removal is unrelated/default no-shim. A/B archives
  `20260707T035129Z-qtmm-control-direct-launch-kprofile` and
  `20260707T035327Z-qtmm-stub-direct-launch-kprofile` show cold/warm direct
  deltas -803ms/-303ms, timeout 0, real GL held, and load-only ABI proof for
  the six observed QMedia imports. Not durable/default yet: shim source/build
  is still `/tmp` only, repo-owned source/version script and reproducible
  build/stage are missing, maps are capped, final post-teardown call log is
  missing, and fake `QMediaPlayer`/`QMediaContent` state makes later media,
  `deleteLater`, or signal-slot paths high-risk/no-go. Next N8 action is to
  convert QtMM into a repo-reproducible opt-in fixture and repeat/control with
  full maps proving Qt5Multimedia/libpulse/tail absence in stub and real
  presence in control plus final call-log before broader active-sample or
  durable consideration; keep NewStuff as the weaker/mixed opt-in. KIO is
  entangled, Solid/crypto no-go by prior shim, and Gallium/GL no-go because
  real GL is mandatory. Do not do glibc or ELF-loader surgery or default flips
  next; boot only to validate a concrete change. Acceptance loop: every
  responsiveness iteration/fix/trace uses kprofile, preserves durable raw logs
  and app/hover proof as relevant, requires app probe PASS,
  `kprofile_timeout_hit=0`, virgl renderer/no software fallback, user-PC
  samples without drops when collected, maps/module attribution when relevant,
  and no regression of `cpu_busy/total_ms`, `pgroup_cpu_runtime_ms`, R5/R3
  watches, or GPU fallback rejection. Keep `poll_stuck_trace=1`
  diagnostic-only unless collecting stuck-poller evidence. No commit/push yet.

- N1 = M7 present path (P2 step 3): the last M7 blocker. All flips are
  software_blit copies. Decision slice result 2026-07-04: route (a) is
  fail-closed on this host. Runnable QEMU is
  `/usr/bin/qemu-system-x86_64` Debian 9.0.2. It advertises classic
  `virtio-gpu-pci`, `virtio-gpu-gl-pci`, `virtio-vga`,
  `virtio-vga-gl`, and `vhost-user-gpu-pci`, but no
  rutabaga/gfxstream/cross-domain device. `virtio-gpu-rutabaga-pci,help`
  says "Device not found"; classic `virtio-gpu-gl-pci,help` fails with
  `undefined symbol: qemu_egl_display`. `/dev/udmabuf`, `/dev/kvm`, and
  `/dev/dxg` exist; no `/dev/dri/renderD*` exists. Host has
  `libvulkan_gfxstream.so` and `libvirglrenderer.so.1`, but no usable
  `rutabaga_gfx_ffi` surfaced. Non-GL `virtio-gpu-pci` supports
  `blob`, `hostmem`, and `max_hostmem`, and a dry-run can form a memfd
  command with `blob=true,hostmem=32M,max_hostmem=32M`; the
  `virtio-vga-gl-primary` + blob lane still fail-closes with
  `QEMU rejects virgl + blob ("blobs and virgl are not compatible")`.
  Landed launcher detection only: `QEMU_BIN=/path/to/qemu-system-x86_64`
  selects the probed binary, and
  `QEMU_GPU=virtio-gpu-rutabaga{,-primary}` or
  `QEMU_GPU=virtio-vga-rutabaga-primary` requires blob/hostmem/
  max_hostmem, `/dev/udmabuf`, hardware host DRI, the rutabaga device,
  `gfxstream-vulkan`, `cross-domain`, `wsi`, and memfd RAM; it refuses
  classic virgl or software fallback. No M7 run was attempted because
  the route cannot dry-run to device args. Kernel route is also not ready
  to implement blindly: creatable capset admission currently supports
  VIRGL/VIRGL2 only (DRM is query-only), not upstream gfxstream/
  cross-domain. Next kernel task, after a capable host/QEMU exists:
  define and wire `VIRTIO_GPU_CMD_SET_SCANOUT_BLOB`, carry blob resource
  format/stride/modifier metadata through the fb/KMS present path, add
  gfxstream/cross-domain capset admission only with a real contract, and
  add native/blob present counters.
  ATTRIBUTION SLICE DONE 2026-07-04/05 — the M7 causal chain is NAMED:
  (1) "software_blit" is a classification label (the not-nouveau-native
  bucket, fb_kms_atomic.c:411); blit_bytes is a pre-branch nominal
  counter; the hot path is genuinely zero-copy (copy_ticks=0) and
  per-frame fence waits are zero. The old software-blit-ceiling theory
  is DEAD.
  (2) ROOT (H1): host GL retire back-pressure — guest GL submits stall
  in virtio_gpu_async_make_room when the depth-32 ctrl ring fills,
  waiting on QEMU/WSL-D3D12 used-ring retirement (268-354 stalls/run,
  1.5-2.2s total; stalls only on submit_3d, never flush).
  (3) CONVERTER (H2): the stalled ctx_submit HOLDS the single shared
  op_lock across its stall (virtio_gpu_user.c:338->413), so KWin's
  page-flip present (op_lock(PAGE_FLIP), virtio_gpu_scanout.c:1516)
  blocks behind it: bo_present_virtio avg 8.6ms/present (vs 1.45ms
  unblocked "last" value) -> flip-complete late -> KWin frame callback
  ~45Hz -> Chromium paced to ~44fps, dropPct ~28. Cadence is mono-modal
  ~22.7ms (throughput limiter), not vsync-beat bimodal.
  Evidence: `20260704T231500Z-kprofile-video-current-default-baseline`
  fbstat/qemu-trace + the 44.2/42.9 archives; full chain in the
  attribution report (history file/git). An instrumented run with all
  four perf flags was TRACE-PERTURBED to 5.3fps (Failure Mode 9;
  archived `20260705T003500Z-n1-instrumented-run-trace-perturbed-*`) —
  its structural reads (make_room_depth_max pinned at 32, retire sums
  >> lock_wait) are consistent; use submit_trace ALONE if re-run.
  N1 IMPLEMENTATION SLICE — PARTIAL LANDING 2026-07-05:
  (a) LANDED: async ring depth 32 -> 60 (60 is the hard descriptor-table
  ceiling: ctrl queue NUM=256 descs, slots use 8 + n*4). Validated by the
  same-binary control video run (clean full window, presentedFPS=44.8)
  and the corrected-defaults KDE gate (DONE, M4 2263 / M5 11218).
  Depth alone moves M7 only marginally (44.8 vs 43.9-44.2 band).
  (b) BLOCKED, default-OFF: the op_lock unlocked-wait retry loop
  (`virtio_gpu_submit_unlocked_wait`, mechanism landed opt-in) hit
  `PANIC thread_queue.c:213 tq_remove: queue is empty` in 2/2 default-on
  GUI runs: releasing op_lock across the make-room stall allows MULTIPLE
  concurrent waiters on the used-ring wait queue, and the
  virtio_gpu_wait_for_used sleep/wake path implicitly assumed at most
  one waiter (it only ever ran under op_lock). Bisect conclusive: the
  opt-out control with depth 60 + poll defaults ran clean. Archives:
  `20260705T010000Z-n1n5-defaults-kde-active-sample` (panic),
  `20260705T012000Z-n1n5-video-unlocked-wait-on` (panic),
  `20260705T014000Z-n1n5-video-unlocked-wait-off-control` (clean 44.8),
  `20260705T021500Z-n1n5-corrected-defaults-kde-active-sample` (DONE).
  N1 UNLOCKED-WAIT: THREE ATTEMPTS, LANE PARKED 2026-07-05 with the full
  rework scope now mapped. Attempt log (all archived):
  (1) default-on: PANIC tq_remove — concurrent completion_init on the
  shared g->async_wait; FIXED by the waiter-serialize mutex (landed).
  (2) opt-in: kernel exception in mutex_lock — MY BUG: the new mutex was
  never mutex_init'ed (this kernel's mutex_t/tq_t REQUIRES init —
  zero-init leaves broken list heads; op_lock inits at
  virtio_gpu_scanout.c:837). FIXED (init landed). The earlier
  "pending_completion torn pointer" theory was WRONG — the sync path is
  verifiably q->lock-disciplined (audited: submit_internal sets/clears
  pending_completion under spin_lock_irqsave(&q->lock)).
  (3) opt-in with both fixes: NO kernel panic, but kwin_wayland #GP at
  libc.so.6 file_off 0x9fff4 — a CANONICAL R5-family site
  (pthread_mutex_lock+4). Mechanism hypothesis: the retry loop performs
  the first-ever async REAP without op_lock held
  (make_room -> reap_completed); a racy reap can signal completion
  early / double-process a used-ring entry, so the HOST DMAs into guest
  frames already recycled -> foreign bytes in another process's fresh
  pages (the R5 corruption shape WITHOUT the R5 kernel bug). Cannot yet
  exclude a first residual R5-class observation, but the timing (first
  working unlocked-wait run after 17+ clean launches) points at the
  change. Archive `20260705T040000Z-n1-mutexinit-unlocked-wait-optin-video`.
  ATTEMPT 4 (2026-07-05): the single-consumer reap discipline was
  IMPLEMENTED and adversarially REVIEWED before any boot — the review
  returned NO-GO for unlocked-wait and convicted three blockers the
  implementation had missed, saving the VM run:
  - B1 (FIXED, landed): virtio_gpu_submit_mixed_async is a THIRD reaper
    (retire + plain async_count-- on ctx_submit's own attach path); now
    routed through the reap mutex with a q->lock'd decrement.
  - B2 (REDESIGN REQUIRED): the reap loop consumes used elements it
    cannot map — including id 0, every SYNC command's descriptor head.
    That discard was only safe because op_lock historically excluded
    reap/sync concurrency. An unlocked reaper steals sync completions ->
    5s timeouts + spurious context failures (exposure amplified by
    submit depth-for-reason default 1). Fix direction: sync commands
    through ring slots, or completion-by-response-content instead of
    used-idx occupancy.
  - B3 (REDESIGN REQUIRED): abort_all from an unlocked make_room can
    free slots mid-fill/mid-post of an op_lock'd poster -> descriptors
    published over freed memory -> host DMA corruption (the exact class
    under investigation). Fix needs a claim/fill handshake or abort
    taking op_lock — which inverts op_lock->reap order from sync-drain
    callers; not a one-liner.
  - RISK: wait_progress detects progress by used-ring OCCUPANCY, not
    MOVEMENT; any concurrent consumer erases the evidence and a healthy
    queue can be aborted after the 5s limit. Fix: snapshot-compare
    used->idx.
  LANDED from attempt 4 (default-safe hardening, battery green:
  nographic PASS, KDE DONE M4 2027/M5 11785, zero panics):
  async_reap_serialize on all three reapers + abort; reserve/reap/abort
  count+claim transitions under q->lock; tear-proof slot release
  (body-wipe first, RELEASE-store pending last); all mutexes
  initialized. These closed latent races that exist even in default
  mode.
  2026-07-05 ATTEMPT 5 — B2/B3 REDESIGN LANDED (kernel 043e88a), M7
  JUMPED TO 51 IN DEFAULT MODE; UNLOCKED-WAIT STILL NO-GO:
  Redesign (subagent-implemented, adversarially reviewed GO with 4
  findings incorporated): TOTAL reaper (sync in-flight record
  sync_inflight/sync_done/sync_stale under q->lock; only id==0 is a
  sync completion; unknown ids warn-consumed; mixed_async's bespoke
  third reaper deleted — sync post/wait shared via
  virtio_gpu_sync_post/sync_wait_done); sync posts PARK while
  sync_stale>0 (no desc[0,3) rewrite while device owes a stale element
  — closes misattribution AND double-execution); slot state machine
  FREE->CLAIMED->POSTED->FREE + ABANDONED quarantine (abort abandons
  only POSTED, never frees device-reachable memory; reaper frees
  ABANDONED on used-element arrival and records fences monotonically
  into last_fence); movement-based progress (used->idx +
  async_retire_seq snapshots; reap+recheck before abort); async
  capacity clamped to negotiated qsize ((qsize-8)/4).
  Battery: probe 5/5 PASS default boot; kde-ready DONE clean.
  A/B (chromium-video): CONTROL (default mode, unlocked-wait OFF)
  presentedFPS=51.0 decodedFPS=61.4 dropPct=9.07 — UP from the
  43.9-44.8 band; the default-mode redesign itself (total reaper, no
  bulk-swallow, shared sync path) plus the N5 poll promotion moved M7
  ~+7fps. TREATMENT (virtio_gpu_submit_unlocked_wait=1) FAILED
  session-liveness-before-chromium: GLOBAL desktop stall at t~147s —
  every polling thread parked simultaneously (poll-stuck dumps show
  68-73s parks all starting together), NO panic/corruption/refused
  lines. Hypothesis: op_lock convoy — a sync waiter (fenced sync
  deferred by virgl behind ongoing async retires) holds op_lock through
  repeated fresh 5s movement windows (review finding #8: no cumulative
  deadline on sync_wait_done), blocking every present. Memory-safe but
  a liveness regression. EXCLUDE unlocked_wait=1 runs from R5 closure.
  2026-07-05 ATTEMPT 6 (kernel a199ed6): cumulative wait-window cap
  LANDED — every movement-renewal loop (sync park, sync wait, both
  drains, make_room) now bounds TOTAL wall time at one
  virtio_gpu_irq_wait_ms window; movement renews the retry, never the
  deadline. In default mode this exactly restores the historical
  single-window failure deadline. Battery green (probe 5/5, kde-ready
  DONE). Treatment rerun: NO permanent stall, no panic/abort/refused —
  but STILL NO-GO: session limps (WaylandEventThr parked 130s, 72s
  park clusters during Chromium launch) and perf-video never reports
  start (FAIL chromium-video-perf-start-missing). With the harness's
  irq_wait_ms=60000, each wedge decision under unlocked-wait costs up
  to a 60s bounded op_lock hold, and something under unlocked-wait
  still makes a sync completion go genuinely missing (root cause NOT
  found — candidates: a lost sync_done signal race the review missed,
  or virgl withholding id-0 behind foreign async streams).
  VERDICT: lane PARKED as diminishing-returns — the default-mode
  redesign already moved M7 44->51 and the remaining gap to 60 is
  host-retire (H1) bound; two post-redesign attempts failed on
  liveness. Reopen conditions: (a) root-cause the missing sync
  completion from archive n1ab-unlocked-a6.log (scratchpad), AND
  (b) cut the interactive wedge-decision cost (per-context sync budget
  or irq_wait_ms tiering) so one missing completion cannot cost 60s of
  op_lock.
- N2 = P3 promotion: attempted 2026-07-04, NOT accepted. The default-on
  guarded battery had static/build/nographic PASS, one KDE active-sample
  PASS, explicit-off control PASS after one known visible-timeout flake,
  and M9 launch-only PASS, but an extra default-on active-sample rerun hit
  `kwin_wayland` #GP in `libQt5Core.so.5` (`kde-session-ready-crash`).
  The post-stop cold-cache A/B did not reproduce that R5-class KWin/QtCore
  signature, but stopped on direct-read ON run 5 with a new kernel page
  fault (`cr2=0x1aafdd193 err=0x2 rip=0xffff80000039a34c`, RIP resolving
  into kernel `_rodata`) after four clean ON runs, one clean OFF run, and
  one known OFF visible-timeout flake. Read-only fault mapping classifies
  that as corrupted control flow into `.rodata`: ASCII bytes decoded as a
  bogus write, not NX; the `sig_trampoline` line is a symbolization
  artifact, and the direct-read tie is still correlational. Kernel
  `04b1ee2` landed opt-in fault/direct-read diagnostics plus a narrow
  ON-only transient compound-node fallback guard. Two debug-ON KDE
  active-sample runs with `ext4_read_page_direct=1
  ext4_read_page_direct_debug=1` did not recur the kernel fault or trip an
  invariant (`20260704T191028Z-n2-direct-read-debug-on-run1-artifact-timeout`,
  `20260704T191406Z-n2-direct-read-debug-on-run2-pass`). The gate remains
  default-OFF; no promotion retry until a diagnostic repeat explains the
  fault or a fresh promotion battery has zero R5-class/kernel crashes.
  Optional second slice still exists after promotion: batch remaining
  non-sequential single-page fills (executable page-in; ~71% of fills).
- N3 = R9 cursor out-of-range (user-visible) + the KWin LibinputBackend
  nullptr payload bug found by R5 forensics (same input area). Harness
  and seat plumbing are fixed; coordinate delivery is now classified and
  has a passing contract. Root cause for the `20260704T195436Z` failure
  was the probe's default RUNPATH preferring the host
  `/usr/lib/x86_64-linux-gnu` libinput/libudev stack; udev enumeration was
  empty and libinput failed monitor/seat setup. Root cause for the
  `20260704T200703Z` coordinate failure was QEMU injection, not kernel
  scaling/storage, evdev, or libinput classification: HMP `mouse_move`
  delivered legacy PS/2 relative samples (`flags=0`, repeated/clamped
  127,127) and zero ABS samples. The strict HMP-selected rerun
  `20260704T202206Z` proved `mouse_set 3`/`info mice` selected
  `QEMU Virtio Tablet (absolute)` but HMP still produced only PS/2
  relative samples. Current harness therefore keeps HMP `info mice` as
  routing evidence, adds a QMP socket, and injects raw 0..32767 tablet
  coordinates with `input-send-event` absolute X/Y pairs. Probe PASS is
  now strict: zero evdev ABS or zero libinput absolute samples fails with
  an explicit reason. Dry-run `20260704T202724Z` PASS; real R9
  `20260704T202747Z` PASS with `/dev/mouse flags=1`, `evdev_abs_samples=10`,
  `libinput_abs_samples=5`, and `result=PASS reason=coordinate_samples`.
  Visible cursor-plane probe `scripts/gpu/r9-cursor-visible-probe.expect`
  then passed in guest cursor mode at
  `build-x86_64/r9-cursor-visible-probe-history/20260704T205234Z-r9-cursor-visible-probe`:
  dry-run proves `show-cursor=off` and no
  `virtio_gpu_host_cursor_only=1`; QMP `input-send-event` remains the
  coordinate proof; `/dev/mouse flags=1`, evdev ABS and libinput absolute
  samples are present; cursor-plane traces land at 0,0 / 640,400 /
  1279,799 with no 2x/out-of-range transform; no panic/KWin crash/
  LibinputBackend nullptr marker was seen. Framebuffer/host capture does
  not prove visible cursor pixels because the GTK hardware cursor overlay is
  outside the QEMU framebuffer capture path. Regression guards passed and
  were archived at
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T210241Z-r9-visible-desktop-interaction-pass`
  and
  `build-x86_64/kde-plasma-desktop-smoke-history/20260704T205909Z-r9-visible-chromium-launch-only-pass`.
  2026-07-07 follow-up proof
  `/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-history/20260707T005222Z-n3-guest-mouseinject-kprofile-proof`
  validated the normal KDE desktop-interaction reducers in
  `scripts/gpu/kde-plasma-desktop-smoke.expect`: forced guest input,
  disabled monitor path/host cursor sync, guest cursor mode, diagnostic-only
  HMP movement, and pre-hover settle (3000ms plus 3 stable framebuffer
  samples). PASS proof included KVM + virgl real GL/no software fallback,
  bounded pointer frame/work area, `/bin/mouseinject 11016 64223 0`, hover
  `status=changed first_changed_ms=986`, app launch PASS with
  `konsole_wait_ms=1508`, `kprofile_timeout_hit=0`, userpc stored=602
  dropped=0, no lingering QEMU, and clean diff check. N8 remains limited by
  Konsole/app-launch readiness and loader work, not mouse bounds.
  Remaining N3 work is the later/secondary KWin LibinputBackend nullptr
  payload unless a startup/input crash reproduces under the visible probe.
- N4 = P1 steps 2c/2d (cpumask atomics skip, CR0.TS shadow) for M2 <1.5us:
  ATTEMPTED + STOPPED/REVERTED 2026-07-04. Full cpumask+CR0 built but
  nographic stopped at `forktest` timeout
  (`build-x86_64/desktop-bottleneck-profile/20260704T213831Z-n4-p1-runtime-verification-nographic/`).
  The cpumask half is the likely culprit (sticky/over-inclusive fanout into
  fork/COW/TLB synchronous shootdowns); CR0.TS shadow is lower suspicion but
  was still insufficient. Cpumask was backed out; CR0-only passed functional
  nographic twice, but failed acceptance metrics, so it was reverted. N4/P1
  is not landed; next action requires a different approach.
- N5 = M8 idle cadence: MAJOR LEAD LANDED 2026-07-04/05. The 4,500/s
  poll-timeout churn is a KERNEL POLICY ARTIFACT: every blocking poll is
  sliced into 10ms rescan iterations (POLL_RESCAN_MS=10,
  vfs_syscall.c:5051) because the notify-backed full-wait fast path is
  default-OFF (`poll_notify_full_wait`, plus separate
  `af_unix_poll_notify_full_wait`; gates at vfs_syscall.c:3957-3985).
  Proof: rescan==timeout+ready exactly; mean timed wait 10.0006ms;
  ~43 slices per blocking poll whose real dwell is ~135ms; each expiry
  pays a DOUBLE full fd-set walk (kqueue rescan + vfs poll scan),
  converting idle-halt ticks into busy-rescan ticks.
  A/B with the existing flags ON (video kprofile, same window):
  timeouts 351,838 -> 29,062 (-92%), rescans -92%, notify mean wait
  9.94ms -> 23.3ms (real deadlines), app blocking behavior unchanged,
  M7 unchanged (44.0), zero crash markers. Archives:
  `20260704T231500Z-*-baseline` vs
  `20260705T001500Z-poll-notify-full-wait-video-ab-92pct-collapse`.
  The flags also have prior 07-01 KDE passes.
  N5 PROMOTION LANDED 2026-07-05: both gates default-ON in kernel code
  (opt-outs `poll_notify_full_wait=0` / `af_unix_poll_notify_full_wait=0`).
  Validated within the N1/N5 battery: nographic fork/clone/cow PASS,
  clean full video window (44.8fps), corrected-defaults KDE
  active-sample DONE (M4 2263 / M5 11218, zero crash markers) — the two
  battery panics were bisected to the (now default-off) N1
  unlocked-wait change, NOT the poll flip (the clean control ran with
  poll defaults ON). M8 idle spot-check is the remaining payoff
  measurement (next battery). Residual 366/s = fd classes still
  requiring rescan + real deadlines; re-attribute only if M8 stays red.
  2026-07-05 M8 PAYOFF + PARTIAL REVERT: both-flags idle measured 44%,
  but a real interactive session then HUNG (Wayland clients
  unresponsive; dbus client auth timeout) — consistent with a missed
  AF_UNIX readiness notify; the injected-input batteries had not caught
  it (timer traffic masks lost socket wakeups). AF_UNIX half reverted
  to default-off (kernel e6e3dab); shipped defaults re-measured 57-64%
  — M8 stays GREEN; KDE gate on the reverted kernel DONE clean.
  2026-07-05 FULL REVERT: the GLOBAL half then also froze a real
  interactive desktop (AF_UNIX already off; user A/B with both flags
  forced off restored the known-slow-but-working baseline, video
  unaffected in both). BOTH poll notify full-wait gates are back to
  default-OFF/opt-in. The 92% churn reduction is real but UNSHIPPABLE
  until the notify delivery hooks (eventfd/pipe/timerfd/kqueue wakeup
  paths) are audited for complete transition coverage and validated
  INTERACTIVELY (Failure Mode 24a — injected-input batteries pass while
  interactive sessions hang). N5 lane REOPENED with that audit as the
  path; M8 also needs a first VALID measurement (GL-pipeline boot —
  the 07-05 readings were taken on non-GL boots that never present).
  Additional methodology lesson: M8/interactive checks must use the
  documented GL recipe (QEMU_GPU=virtio-vga-gl-primary ...); the
  default non-GL virtio-gpu-primary path shows the boot gradient and
  never presents KWin output (own issue — track separately if the
  non-GL path is meant to work).
  2026-07-05 AUDIT + PRODUCER FIXES: consumer side (kqueue wait path)
  audited SAFE — triple level re-poll (register-time ops->event
  kqueue.c:1143, wait-entry rescan :1310, post-wait __vfs_poll_scan
  vfs_syscall.c:5133); timeout=-1 full-wait = tq_wait with no timer
  (kqueue.c:1497), so any lost PRODUCER notify = freeze-forever.
  Producer audit found 3 lost-notify defects; all 3 FIXED:
  (1) timerfd.c timer-IRQ deferral: on queue_work failure the old code
      cleared work_pending AND set armed=false — dropped the notify and
      permanently killed repeating timers. Now: wq==NULL (boot) drops
      cleanly; queue_work-failure leaves notify_pending/armed intact
      (the running worker's re-check loop consumes them) and clears
      only work_pending so the next expiry re-attempts.
  (2) pipe.c blocking write: with the ring full, write() parked in
      __pipe_wait_reader WITHOUT ever firing the EVFILT_READ knote
      (the only notify was at end-of-write, unreachable while
      blocked) — a poll-only reader deadlocked against the blocked
      writer. Now the writable==0 branch fires
      vfs_file_knote_notify(read_file, EVFILT_READ) (writer_lock
      fdup protocol, notify outside the lock) before waiting.
  (3) vfs_syscall.c inotify_emit_locked: only the FIRST matching
      watcher's fd was knote-notified per event; 2nd+ inotify fds
      polling the same inode never woke. Now an inotify_notify_set
      (bounded 8, pointer-deduped, overflow logged) collects ALL
      queued watcher files; callers fire the whole set outside the
      global lock.
  Reducer: /bin/poll-notify-probe (scripts/image/poll-notify-probe.c,
  staged into the image) — 3 tests under poll_notify_full_wait=1 with
  15s watchdogs. Pre-fix kernel: pipe-blocking-write FAIL (reproduced
  the deadlock exactly); timerfd passes pre-fix (its defect is a rare
  queue_work race, kept as regression cover); inotify passes pre-fix
  only because truncate/write/close emit a multi-event cascade and the
  first watcher exits between events (single-event gap still real).
  Post-fix: probe RESULT=PASS 3/3 with BOTH gates forced ON, and
  RESULT=PASS 3/3 on a default (gates-OFF) boot — failing-then-passing
  reducer complete. (Probe note: pipe reader must treat read()==0 as
  EOF-after-writer-exit, not error.)
  2026-07-05 ROUND 1 INTERACTIVE: STILL FROZEN ("no response") — the
  three producer fixes were necessary but not sufficient.
  STUCK-POLLER DIAGNOSTIC (landed, on whenever poll_notify_full_wait=1):
  notify-backed full waits with timeout<0 or >=5s register a park entry
  (pid/comm/fd classes captured in the poller's own context, unix paths
  included); any other blocking poller dumps entries parked >10s
  (re-dump every 30s so frozen-forever is distinguishable from
  wake-and-repark). Live KDE dumps isolated the culprit: KWin's
  libinput-connec thread, poll(-1) on {eventfd, epoll-fd}, parked in
  ONE episode from t=39s for the whole session — input dead, rendering
  alive (KWin/plasmashell main loops never appeared: healthy).
  Reducer tests 4 (poll parked ON an epoll fd, pipe producer) and
  5 (cross-process eventfd) both PASS gates-ON → generic
  epoll-propagation and eventfd links are sound.
  ROOT CAUSE #4 (THE interactive killer), FIXED in dev/evdev.c:
  kqueue attach/notify LIST MISMATCH. knote_read/write_attach prefers
  the per-open FILE knote list whenever f->ops->poll exists; evdev
  installs evdev_file_ops (with .poll) via cdev.ops.open_file, so
  epoll/poll knotes for /dev/input/eventN land on the FILE list. But
  evdev's producer notify() only called cdev_knote_notify(&st->cdev)
  — the CDEV list, which stays empty. Input readiness therefore NEVER
  produced a kqueue wakeup; default mode was saved by epoll's 20ms
  rescan, gate-ON full wait froze input forever. Fix: evdev_client
  keeps its open vfs_file (set in open_file, protected by st->lock);
  notify() snapshots client files under st->lock (vfs_fdup) and fires
  vfs_file_knote_notify(EVFILT_READ) after unlock (kqueue_wait holds
  kq->lock while calling ops->poll which takes st->lock — notifying
  under st->lock would ABBA). AUDIT THE SAME MISMATCH ELSEWHERE: any
  cdev whose open_file installs poll-bearing file ops but whose
  producer only calls cdev_knote_notify (check ps2kbd/ps2mouse generic
  cdev wrapper path, ttys).
  Related audit findings (separate lane, rescan-masked today, NOT the
  gate killer): PTY slave-side readiness is structurally un-notifiable
  (pty_pair has no slave file pointer; tty_input commit points
  tty.c:403/421/378 and pty_slave_hangup only tq_wakeup) — must be
  fixed before pts fds could ever be flagged notify-backed; signalfd is
  a stub (poll always 0); unconnected AF_UNIX DGRAM sendto delivery is
  unimplemented (sendto rejects addresses, sendmsg ignores msg_name).
  2026-07-05 ROUND 2 VALIDATION + PROMOTION (kernel b231117): the
  evdev-fixed gates-ON KDE session ran with working input across
  multiple real interaction bursts (stuck-poller telemetry: the
  libinput thread woke on every burst; no thread re-froze), and the
  user moved work forward on that basis. BOTH GATES ARE DEFAULT-ON
  again (opt-out poll_notify_full_wait=0 / af_unix_poll_notify_full_wait=0).
  Validation chain on the promoted kernel: 5-test poll-notify-probe
  PASS on a default boot (gates active by default, diagnostic armed);
  kde-ready smoke DONE clean. CAVEAT: the desktop-interaction-latency
  visibility reducer FAILED IDENTICALLY with gates ON and OFF from the
  agent's headless shell (no screendump artifacts were ever written) —
  an environment limitation, not a flip regression; treat that reducer
  as runnable only from a display-attached session. The stuck-poller
  diagnostic stays active whenever the gate is on (zero cost
  otherwise) and is the standing tripwire for any remaining
  lost-notify class: `poll-stuck:` on serial names the fd classes.
  NEXT for N5: M8 idle-cadence payoff measurement on a GL boot
  (expected large drop in poll-timeout churn / idle wakeups) — DEFERRED
  per user (2026-07-05): interactive freeze is gone but the desktop
  still "responds slowly" → the standing R7/M4 perf lane is now the
  priority, N1 unlocked-wait redesign in progress.
  PTY SLAVE NOTIFY LANDED (kernel d88108b): pty_pair.slave_files[4]
  registry; master-write → slave EVFILT_READ + echo → master notify;
  master-close hangup → raw_wait wake + slave POLLHUP notify; last
  slave close → master EOF notify. pts fds remain rescan-class (NOT
  notify-backed) until the ioctl-driven readability transitions
  (termios canon/raw flips, TIOCSTI-style injection) are audited; the
  notify already wakes kqueue waiters instantly instead of at the next
  10ms rescan boundary (keystroke latency win).
- N6 = R2 PCID stale-TLB lane: RESOLVED 2026-07-04 — retest DONE, lane
  retired as a corruption lane, default stays OFF for perf reasons.
  (a) Safety: offline audit (GO) verified every noflush-specific hazard is
  covered (trapframe slot has its own invlpg; ASID recycle is
  generation-flushed; the only anon-free paths are the R5-fixed ones) and
  re-verified the 07-02 crash signatures as the R5 recycled-frame family.
  Opt-in retest on the R5-fixed kernel (`x86_pcid=1 x86_cr3_noflush=1`,
  both tokens + `max ASID = 4095` verified per boot): nographic
  fork/clone/cow PASS, and 3/3 KDE active-sample DONE with ZERO
  KWin/corruption markers — the 07-02 trial corrupted within 2 runs, so
  the "PCID corruption" is CONFIRMED to have been the R5
  free-before-shootdown bug. Archives
  `20260704T221000Z/222000Z/223000Z-n6-pcid-noflush-retest-kde{1,2,3}`.
  (b) Perf: same-session nographic A/B — tlb_amplification roughly HALVED
  (256/512/1024pg: 1479/1781/4085 -> 911/770/2364ns) but getpid_ns
  unchanged; and the KDE battery shows a consistent desktop REGRESSION:
  M4 2697-2867 (vs 1883-2144 band) and M5 15570-16742 (3/3 above the
  15000 goal). Mechanism: with PCID active every page/range shootdown
  degrades to a full global flush (invlpg cannot cross PCIDs;
  vm_remote_sfence_page forces CR4.PGE toggles), so desktop
  COW/fault shootdown traffic pays more than the syscall path saves.
  VERDICT: keep PCID/noflush default-OFF. Future re-evaluation condition:
  implement INVPCID-based per-PCID single-page/range flushes (CPUID
  check + fallback), then rerun this exact A/B; only promote if M4/M5
  hold within noise. R5 closure accrual from this battery: +3 (12/30+).
- N7 = timer tick loss: LANDED 2026-07-05 (kernel 4bda525).
  Discovery during implementation: the sched_timer wheel was ALREADY
  TSC-driven (sched_timer_refresh_ms absolute-ms expiry) — sleep_ms/
  timerfd/tq deadlines never lost time; the 14% loss bit ONLY the
  get_jiffs() consumers (uptime, poll/ppoll deadline arithmetic,
  itimer bookkeeping, lwip timers, cache aging), which ran slow and
  diverged from the wheel clock. Fix: get_jiffs() derives ms from the
  calibrated TSC (rounded mult, ~0.2ppm vs the wheel) behind an
  advance-only CAS-max clamp (monotonic across CPUs, one CAS/ms, no
  locks); BSP tick accounts the full elapsed span; kstats v9 adds
  timer_bsp_ticks_total + timer_jiffies_tsc_comp_ms_total. Gate:
  TSC>=1MHz AND (InvTSC bit OR hypervisor bit — QEMU does not
  advertise InvTSC; first battery caught the gate disabling the fix,
  boot line is the proof: '[x86] jiffies: TSC-compensated
  (mult=1624)'). Opt-out timer_tsc_jiffies=0 for A/B. Adversarially
  reviewed (GO; 3 RISKY fixes incorporated). Battery: probe 5/5 PASS,
  kde-ready DONE clean. Note for measurement lanes: guest uptime and
  all jiffies-based rates now run ~16% faster under load than old
  archives — do not compare raw jiffies-derived counters across the
  boundary without normalizing.
- Continuous: R5 statistical closure (9/30+ clean attempt-1 KWin launches
  accrued; count every future battery), R3 recurrence watch (rcu_head
  double-free may share the R5 root cause — one `slab_alloc: repairing
  corrupt freelist cache='rcu_head_cache'` line was seen 07-04 pre-R5-fix).
FS-churn attribution 2026-07-05 (vfs_trace_all=1 video boot, 4,477
opens traced): the ~370 opens/s from the kprofile window is MOSTLY
MEASUREMENT MACHINERY — kde-process-probe /proc scans (684) + the
harness samplers/kde-session scripts (826) dominate; among real
desktop processes kwin_wayland leads (1,399) and its churn is
repeated GL/GLX dlopen SEARCH-PATH PROBING (~180 opens across 26
rounds of libGLX.so.1/libGL.so.1 over 10+ path variants — consistent
with ext4_lookup_enoent being 86% of driver lookups). In a live
session without the harness the background churn is far lower.
VERDICT: not the interactive-slowness culprit; keep as a minor
optimization note (dlopen path-scan caching or a slimmer ld search
path for kwin would cut the ENOENT storms).
NEW LANE P0-PREEMPT (2026-07-05, THE systemic desktop-slowness root
cause — supersedes per-subsystem latency lanes for R7):
MEASURED: wake-to-run trace (kde_wake_to_run_trace=<ms> +
wake_to_run_trace_all=1, gate-cache aliasing bug fixed in
kde_ready_trace.c — each cmdline gate now has its own cache) showed
kernel threads (rcu_cb/N pinned, tty_input) taking 50-500ms routinely
and 1.4s in clusters from wakeup to first run on a live desktop.
AUDIT (verified with file:line): the kernel is FULLY COOPERATIVE in
kernel mode. NEEDS_RESCHED is set by ticks/IPIs/wakeups but honored
ONLY at return-to-user (trap.c:977) and the idle loop
(start_kernel.c:229). Kernel-mode trap epilogue (trap.c:2469-2474)
irets straight back; zero cond_resched sites exist. Any long syscall
or kthread batch holds its CPU until voluntary yield. VERIFIED-OK:
wakeup enqueue + idle kick + IPIs (sched.c:426-596), sti;hlt idle
race-free, priorities, tick preemption of USER mode. AMPLIFIER for
the 1.4s clusters: printf = synchronous UART busy-wait under global
pr.lock with IRQs off (printf.c:135, uart.c:261-273) — log bursts
serialize CPUs machine-wide.
FIXES LANDED (kernel f89b60b, battery green: probe 5/5, kde-ready
DONE, zero assertions): (1) IRQ-exit kernel preemption behind
kernel_preempt=1 default-on; (2) async console (klog ring + drain,
panic-synchronous fallback) behind console_async=1 default-on;
(3) preemption-safe per-CPU asserts: 11 rwsem/mutex/semaphore debug
asserts sampled mycpu()->spin_depth with IF=1 — with migration now
possible at any IF=1 instruction they could read ANOTHER CPU's
counter and false-panic (first battery caught it: rwsem assert in
kded5 rseq user-return); spin_depth_snapshot() samples under
push_off; wakeup-path assertion wrapped; scheduler_yield preamble
pinned. STILL OPEN in this lane: cond_resched checkpoints (belt and
braces), wake-list re-placement (minor), RISKY-3 accounting inflation
(each IRQ-exit preempt re-runs __do_timer_tick + counts a tick —
utilization/EEVDF slice skew, correctness OK), RISKY-4 console
cross-stream ordering (smoke scripts keying on kernel-vs-app serial
ordering may flake; console_async=0 to bisect). A/B flags:
kernel_preempt=0, console_async=0.
2026-07-05 POST-LANDING VERDICT: wake-to-run tails collapsed (1.4s
clusters gone; residual periodic ~200-400ms pairs on rcu_cb/0 +
tty_input, follow-up: FIFO-class placement has no idle-pull;
open item). BUT the USER reports 'improvement is not obvious' for the
seconds-scale hover/tooltip/menu latency.
2026-07-06 KPROFILE REFINEMENT: after QtQuick GL fallback rejection,
active visibility sampling, and prompt-safe `poll-stuck:` gating, the
corrected kprofile artifacts identify the current user-visible bottleneck
as Konsole shell readiness/app-launch wait, especially pre-PTY
poll/ppoll waits. The earlier DRM/frame-clock/ghost-frame idea remains a
parked visual-cadence suspicion, not the active top bottleneck. GPU/fence/
input chains remain guardrails, and no native-present credit is implied.
Recommended execution order: (1) N8 Linux-like Plasma responsiveness
(Konsole/poll readiness, kprofile-driven); (2) N1 M7 present-path work
only if the video/FPS lane is resumed; (3) N5/M8 idle payoff after the
responsiveness bottleneck is reduced; (4) N2 retry ONLY after its fault
diagnosis gate; (5) N3 residual LibinputBackend nullptr; (6) a NEW P1
approach for M2 <1.5us (N4 cpumask/CR0.TS is dead: the cpumask half
stalls forktest, CR0-only missed targets — do not re-apply the saved
patches; find a different cost).

- Parked: CR-3 (%fs selector reload semantics), CR-7 (starve-probe RCU),
  CR-9 (timer 1-jiffy boundary race, needs timerfd reducer); latent
  hugepage-path bugs (list in the R5 lane — they BLOCK re-enabling
  `vma_file_hugepage_collapse_enabled`); Chromium GL conformance depth
  beyond the validated ladder (only if real workloads hit gaps); KDE
  session-stability residuals (see that lane).

## Work Style — Time Budget and Batching (binding)

1. CODE-FIRST: finish the whole code slice (source read end-to-end,
   hypothesis in the lane, fix + reducer + gated probes) BEFORE any VM
   boot. VM runs validate completed slices.
2. BATCH GATES: one battery validates a batch of independently-revertable
   changes: nographic fork/clone/cow + one KDE active-sample pass + one
   Chromium launch-only guard + M8 spot-check. Bisect only on failure.
3. OFFLINE FIRST: parsers/classifiers/forensics run against the existing
   archives (700+ smoke runs, profile dirs) — never a VM boot for a parser
   change. The 07-04 R5 root-cause (5 subagent passes over archives +
   sources, zero exploratory boots) is the reference example.
4. REUSE BOOTED VMS: plan the probe list before booting; collect
   everything in one session. Budget <=2 boots per lane slice + the final
   battery.
5. EXCEPTION: genuinely run-variant work (flake statistics, corruption
   repro, freeze repro) may use as many runs as the evidence requires.
6. Track the implementation-vs-validation split per lane update; if
   validation dominates twice in a row, stop and re-plan.
7. ORCHESTRATE, DO NOT DO: the handoff agent is an ORCHESTRATOR. Decompose
   each slice into jobs and spawn one subagent per job (source
   read-through, implementation, offline forensics, gate runs, log
   triage); run independent jobs concurrently; keep own context for
   synthesis and decisions. Subagents return conclusions/diffs, never flip
   defaults, never push.
8. COMMIT PERIODICALLY: at every verified checkpoint and before ending,
   commit repo + submodules (kernel, ports, ports/mesa/src, user)
   deepest-first with lane-scoped messages, then update parent pointers.
   Never push without explicit user approval.

## Known Failure Modes — Do Not Repeat

Check BEFORE declaring any gate failed or hypothesis confirmed.

1. Wrong session lane: `host_chromium=1` is weston-only; launch Chromium
   in KDE via `/bin/wayland-chromium <url>` with
   `XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root WAYLAND_DISPLAY=wayland-0`.
2. Passive-visible-detector flake: always set
   `KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`.
3. Silently dropped kernel flags: every run log must show the intended
   tokens in `x86 kernel cmdline` (and `vm_asid_init: max ASID` for PCID).
4. Serial console truncates ~55-60 chars, drops trailing `&`, splits
   output mid-token. Stage script files or debugfs-inject; keep commands
   short.
5. Single-sample misdiagnosis: hangs need multiple timed GDB samples plus
   an executed (not merely echoed) guest liveness command.
6. Nographic gates do NOT validate VM/TLB/scheduler correctness (the PCID
   flip passed nographic then corrupted KWin). GUI-scale gates required;
   audits need a failing-then-passing reducer.
7. Harness label vs app truth: judge by post-evidence/status files and
   guest logs, not wrapper labels.
8. Stale runtime: check kernel/fs.img mtimes vs the running QEMU.
9. Trace volume perturbs timing: thresholded/role-scoped flags only in
   timing runs.
10. Debugcon timestamps are per-boot TSC — never cross-boot wall clock.
11. Starvation-probe lines alone are not freeze proof while liveness
    commands execute.
12. Background Chromium intentionally in freeze repros; short commands.
13. >60-char/multi-line serial commands can wedge bash in
    quote-continuation (recover with a lone closing quote).
14. QEMU gdbstub wedges after a killed gdb ("target is running") —
    restart the VM; always detach; sample against kernel.elf (xv6.bin is a
    bzImage).
15. Desktop-smoke PASS with `chromium=-1` says nothing about M9; judge
    Chromium only by the chromium reducer or manual launch + guest logs
    (`debugfs -R 'cat /host-gui-wayland-chromium.log' build-x86_64/fs.img`).
16. Uncommitted behavior-affecting diffs are handover landmines: commit,
    revert, or list them in this plan.
17. KPROFILE-mode bookkeeping: in `KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE=1`
    runs, `status=FAIL reason=launch-evidence-missing` is EXPECTED
    (kprofile replaces the launch-evidence stream) — not a playback
    failure. Read the kprofile log + PERF-VIDEO lines instead.
18. Serial wait-markers must not appear in the echoed command itself (a
    grep for `MARKER` matches the echo and fires early — killed a cowtest
    run twice). Emit markers via a variable: `M=XX; cmd; echo ${M}RES=$?`.
19. Artifact/visible-timeout harness flake class
    (`*-artifact-timeout`, visible-parser/helper timeouts): 17+ prior
    archives; KWin/desktop healthy. Archive + rerun once.
20. konsole-shell-ready-timeout flake: konsole launches but the ready
    marker never appears (45s timeout, zombie) -> M4 unmeasured. Rerun
    once; escalate only if frequency rises.
21. When host WSLg PulseAudio is down AND a pactl probe is explicitly
    enabled, the guest pactl fault cascades into `kde-session-ready-crash`
    (recovery: `wsl --shutdown` from Windows). Default runs skip pactl.
22. HMP `mouse_move` delivers legacy PS/2 RELATIVE samples regardless of
    `mouse_set` selecting the absolute virtio tablet. Inject absolute
    coordinates ONLY via QMP `input-send-event` (R9 lesson; two probe
    runs burned).
23. Guest probes must pin RUNPATH/LD_LIBRARY_PATH to the intended guest
    libs: a probe's default RUNPATH preferred the host
    /usr/lib/x86_64-linux-gnu stack and silently broke udev/libinput
    enumeration.
24a. Injected-input reducers do NOT prove interactive responsiveness:
    the AF_UNIX poll-notify default passed the desktop-interaction
    battery yet hung a real interactive session (timer traffic masks
    lost socket wakeups). Wakeup-semantics changes need an interactive
    (human or QMP raw-input) check before default promotion.
24. Single-waiter wait-queue invariants: paths that historically ran
    under a big lock (e.g. virtio_gpu_wait_for_used under op_lock) may
    implicitly assume at most ONE waiter on their tq; allowing
    concurrent waiters panics `tq_remove: queue is empty`
    (thread_queue.c:213). Audit tq usage before lock-scope reductions.
    Also: a harness `prompt-sync-timeout` label can MASK a kernel panic
    — always grep the archived run.log for PANIC/IPI_REASON_CRASH
    before classifying as harness flake.

## Guardrails

- NO un-gated default flips (kernel cmdline defaults, launcher env,
  image/session config). Diagnostics are opt-in default-off. A default may
  change only with: same-session A/B + explicit-off control, a plan entry
  naming the lane, and the regression battery. (Executed examples:
  ordered-pageflip 07-04 PASS; PCID 07-02 FAIL-and-reverted.)
- Regression battery after ANY behavior change: KDE active-sample smoke +
  Chromium launch-only no-worse-than-scoreboard + M4/M5/M8 within noise.
  New failure class = revert first.
- Handover hygiene: `git status` at top level AND every submodule
  (kernel, ports, ports/mesa/src, user); every behavior-affecting diff
  committed, reverted, or listed here. Current dirty-by-design: NONE (tree
  clean as of the 07-04 compaction; `ports/xz/src` if dirty is unrelated —
  do not revert).
- Closed lanes — do NOT reopen without new evidence: scheduler
  wake-to-run latency, futex key/timeout drift, poll/kqueue/AF_UNIX/
  eventfd/pipe primitives, guest cursor upload, raw PTY setup, tiny
  Wayland frame delivery, D-Bus/eventfd readiness, renderer admission
  (R8), inotify FIONREAD (R7b).
- One compile/VM lane at a time; `pgrep -af qemu-system` first; never
  launch QEMU with a trailing `&` (use background task machinery).
- Default-off diagnostic knobs available: `rcu_head_trace=1` (R3 owner
  history), `sched_starve_probe=1` (P0 freeze telemetry),
  `rq_identify_linear_scan=1` (R7a control), `ext4_read_page_direct=1`
  (P3, pending promotion), `kde_pactl_probe=1`, `kde_network_status_sni=1`,
  `kde_plasma_systemtray=1`, `kde_pre_kwin_libinput_probe=1`,
  `KDE_SMOKE_CHROMIUM_MESA_EXTENSION_OVERRIDE` (diagnostic-only),
  `WAYLAND_CHROMIUM_BUNDLED_GL=1` (emergency/diagnostic only — never the
  solution), kwin alloc/loader trace knobs.

## Lanes — Compact Status

### P0 — Chromium/YouTube freeze: CLOSED (2026-07-03)

Structural idle-pull fix landed (kernel `ef2dab6` line); M1 battery 3/3
responsive, probe-silent replay 3. Durable repro recipe for future M1
replays: boot the KDE image
(`DISPLAY_MODE=gtk USE_KVM=1 QEMU_GPU=virtio-vga-gl-primary QEMU_INPUT=virtio
QEMU_NET=1 QEMU_APPEND='root=/dev/disk0 video=1280x800 netsurf=0 webkit=0'`),
then from serial:
`XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root WAYLAND_DISPLAY=wayland-0
/bin/wayland-chromium https://www.youtube.com/` backgrounded, 15-min
liveness (`ALIVE_n` commands must EXECUTE). Evidence:
`build-x86_64/yt-mainpage-freeze-repro/`.

### P1 — Syscall overhead (M2/M3): N4 stopped/reverted

2a (FS_BASE cache) + 2b (trapframe direct) landed; accepted baseline M2
1.65-1.94us and M3 noisy 2.7-3.7us. N4 attempted 2026-07-04 but did not
land. Full patch = cpumask hot-path skip + CR0.TS shadow; it built, then
nographic stopped at `forktest` timeout in
`build-x86_64/desktop-bottleneck-profile/20260704T213831Z-n4-p1-runtime-verification-nographic/`.
Analysis: cpumask sticky/over-inclusive behavior likely caused fork/COW/TLB
synchronous shootdown fanout/stall; CR0.TS shadow is lower suspicion.

Recovery: cpumask half backed out. CR0-only passed functional nographic in
`build-x86_64/desktop-bottleneck-profile/20260704T215704Z-n4-p1-cr0only-nographic-gate-resync/`
but missed acceptance (`getpid_ns=1739`, M3=4818). Clean rerun
`build-x86_64/desktop-bottleneck-profile/20260704T220417Z-n4-p1-cr0only-clean-nographic-gate/`
again passed functional nographic but failed metrics: M2
1973/2102/1925/2185ns, M3 1024-page 4504ns. CR0-only patch reverted; final
repo state was clean; N4/P1 is not landed. Saved patches:
`/tmp/n4-p1-current-20260704T215021Z.patch`,
`/tmp/n4-p1-cr0only-final-20260704T220032Z.patch`,
`/tmp/n4-p1-cr0only-current-20260704T220245Z.patch`, and artifact copy
`build-x86_64/desktop-bottleneck-profile/20260704T220417Z-n4-p1-cr0only-clean-nographic-gate/saved-cr0only.patch`.
Next action: choose a different P1 approach that accounts for fork/COW/TLB
fanout risk; do not mark N4 passing from these runs. CR-3 (%fs selector
reload) remains the latent ABI follow-up.

### P2 — Ordered page flip: steps 1-2 DONE, step 3 = N1

Step 1 direct-KMS A/B: 100 -> 118-125 FPS. Step 2 default flip LANDED
2026-07-04 (kernel `69310a3`, default-aware helper in
`virtio_gpu_scanout.c`; opt out `virtio_gpu_ordered_page_flip=0`):
default-on KDE pass (M4 2144), explicit-off control pass (M4 2122), video
36.8 -> 42.9. Archives `20260704T181500Z/183000Z/185000Z-q7-*`.
Step 3 (zero-copy present, M7 >= 55) is N1: every flip is still
`software_blit` with `native_present_credit=0` (fbstat), the sole
remaining M7 ceiling. N1 route (a) now has fail-closed launcher selectors
for rutabaga/gfxstream/blob plus a `QEMU_BIN` override, but this host
cannot run them: no hardware `/dev/dri/renderD*`, no rutabaga QEMU
device, and broken classic `*-gl` device help. Kernel blob resources and
host-visible mmap already exist, but creatable capsets are VIRGL/VIRGL2
only; scanout-blob/native-present and gfxstream/cross-domain admission are
future slices once a capable host route is available.

### P3 — Ext4 read-path serialization: first slice LANDED gated; N2 stopped

`ext4fs_pcache_read_page` held the per-mount esb mutex across device
waits, serializing all readers/faulters (0.86ms/fill, ~29s per video
window). Fix (kernel `2dee9b7`, gate `ext4_read_page_direct=1` default
OFF): resolve mapping under the lock, direct BIO, release before
`bio_await`; bcache-coherent (any cached block falls back/copies under
lock — dirty data lives in the lwext4 bcache via the write path).
A/B same workload: read_page_ms 29016 -> 14469 (-50%), lookup lock wait
-52%, ext4_fault_ms -66%, browser start 10.63s -> 6.17s (-42%); video
unchanged (present-path ceiling); fork/clone/cow + KDE battery green.
Archive `20260704T151500Z-p3-ext4-read-page-direct-chromium-kprofile-ab-pass`.

N2 guarded promotion attempt 2026-07-04: source flip made omitted token
default-on and explicit `ext4_read_page_direct=0` default-off control, then
ran the acceptance battery. Static/build PASS (`git diff --check`,
`git -C kernel diff --check`, kernel build). Nographic fork safety PASS:
`forktest` rc=1 known exhaustion signature, `clonetest` rc=0, `cowtest`
rc=0, boot cmdline had no direct-read token. KDE active-sample default-on
PASS with no direct-read token
(`20260704T181959Z-n2-ext4-direct-default-on-kde-active-sample-pass`;
M4 3518, M5 13420). Explicit-off control with
`QEMU_APPEND_EXTRA=ext4_read_page_direct=0` had one known visible-timeout
flake (`20260704T182300Z-n2-ext4-direct-explicit-off-kde-visible-timeout-rerun-needed`)
then PASS
(`20260704T182457Z-n2-ext4-direct-explicit-off-kde-active-sample-control-pass`;
M4 2365, M5 10015, cmdline proved token present). M9 launch-only default
real-GL guard PASS with software/bundled GL env unset
(`20260704T182703Z-n2-ext4-direct-default-on-chromium-launch-only-m9-pass`;
GPU/init/GL request errors 0, no `--use-gl`/ANGLE fallback args).
However, an extra default-on KDE active-sample rerun produced
`KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash`: `kwin_wayland`
#GP in `/usr/lib/x86_64-linux-gnu/libQt5Core.so.5`, no direct-read token
in cmdline
(`20260704T183012Z-n2-ext4-direct-default-on-kde-kwin-gp-stop`). Because
the N2 risk list includes KWin/ld.so recycled-byte corruption, the battery
is not clear/safe. The source flip was reverted before commit; current
state remains default-OFF with opt-in `ext4_read_page_direct=1`. Next
diagnostic is crash-focused cold-cache A/B (3-5 default-on vs explicit-off
KDE active-sample runs plus KWin fault/core/PTE or frame evidence if the
#GP recurs). Optional second slice = batch the remaining non-sequential
single-page fills (executable page-in pattern, ~71% of fills).

Post-stop cold-cache crash-focused A/B 2026-07-04 used the reverted
default-OFF source with explicit tokens in both arms
(`QEMU_APPEND_EXTRA=ext4_read_page_direct=1` vs `=0`) and
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, `QEMU_AUDIO_BACKEND=none`, with
software/bundled GL fallback env unset. Result before stop: ON PASS x4
(`20260704T183909Z-n2-ab-direct-read-on-run1-pass`,
`20260704T184121Z-n2-ab-direct-read-on-run2-pass`,
`20260704T184327Z-n2-ab-direct-read-on-run3-pass`,
`20260704T184741Z-n2-ab-direct-read-on-run4-pass`), OFF PASS x1
(`20260704T184534Z-n2-ab-direct-read-off-run1-pass`), OFF known
visible-timeout flake x1
(`20260704T185037Z-n2-ab-direct-read-off-run2-visible-timeout`), then ON
run 5 stopped on a new kernel page fault
(`20260704T185323Z-n2-ab-direct-read-on-run5-kernel-page-fault-stop`):
`*** KERNEL PAGE FAULT: cr2=0x1aafdd193 err=0x2
rip=0xffff80000039a34c`, backtrace line
`sig_trampoline.S:29: sig_trampoline+250691`, `Core: 2`, idle thread,
panic at `kernel/arch/x86_64/irq/trap.c:2023`. Symbol resolution maps the
RIP into kernel `_rodata`, not a normal function body. No A/B archive
matched the prior R5 KWin/QtCore signature (`kwin_wayland` #GP at
`libQt5Core.so.5` file offset `0xdbc9f`, bad pointer
`0x2d34365f3638782f`). Classification: not an R5 recurrence, but the
direct-read arm produced a new kernel fault before the recommended 5x5
could complete, so N2 default promotion remains blocked and default-OFF is
the required state.

Read-only fault mapping resolved the stop more precisely: RIP
`0xffff80000039a34c` is image offset `0x39a34c` inside `.rodata`
(`_rodata` range `0xffff80000035e000..0xffff8000003b7000`), adjacent to
ASCII strings near `Operations` / `TEST: synchronize_rcu()`. Decoding
those bytes yields a bogus write, matching `cr2=0x1aafdd193 err=0x2`
(supervisor write to a non-present low/user-looking address), so this is
corrupted control flow into read-only data, not an NX fault or real
`sig_trampoline` execution. `rbp=0xbefc6cc0` was outside the expected
kstack, so the unwind is secondary/bad; idle context is real but not a
root cause. No stack/register evidence currently places the CPU in
ext4/pcache/bio.

Diagnostic commit kernel `04b1ee2` (`kernel: add n2 direct-read fault
diagnostics`) makes a repeat actionable without changing defaults:
unrecoverable x86 kernel #PF now prints current task, CR3/page-table
identity, full registers, PTE walks for CR2/RIP/RSP/RBP in active/kernel/
current spaces, instruction bytes when mapped, stack words when RSP is on
the current kstack, a `.rodata` execution classifier, and an ext4 direct
read ring dump if enabled. `ext4_read_page_direct_debug=1` adds an opt-in
64-entry direct-read ring plus invariant checks around `bio_add_folio()`
and `bio_await()`. The same commit also adds an ON-only guard that falls
back for transient compound-folio node metadata while direct-read is
enabled; this does not affect default boots because
`ext4_read_page_direct` remains default-OFF.

Bounded diagnostic validation 2026-07-04 used:
`QEMU_APPEND_EXTRA='ext4_read_page_direct=1 ext4_read_page_direct_debug=1'`,
`KDE_SMOKE_REDUCER=desktop-interaction-latency`,
`KDE_SMOKE_INTERACTION_ACTIVE_SAMPLE=1`, `QEMU_AUDIO_BACKEND=none`, and
the software/bundled GL fallback env unset. Kernel build PASS
(`cmake --build build-x86_64 --target kernel -j2`). Run 1 archived
`20260704T191028Z-n2-direct-read-debug-on-run1-artifact-timeout`: known
artifact-timeout class, no kernel #PF/panic/invariant dump markers, cmdline
proved both direct-read tokens. Run 2 archived
`20260704T191406Z-n2-direct-read-debug-on-run2-pass`: status code 0 /
`KDE-PLASMA-DESKTOP-SMOKE-DONE`, cmdline proved both direct-read tokens,
and log search found no `KERNEL PAGE FAULT`, `kernel-pf-context`,
`ext4-direct-read`, invariant, or panic markers. No explicit-off control
was run in this diagnostic slice because the ON-arm fault did not recur in
the two bounded ON runs.

### Q2 / R8 — Chromium real GL: DONE (validated 2026-07-04)

History: "window not visible" was never renderer admission (exonerated);
a real Mesa EGL attr-order bug was fixed (`5e3f4bebe`); the default-path
blocker was Chromium 150's passthrough decoder requiring the ANGLE
extension ladder. Direction settled by the user: do not skip GPU
acceleration. Current accepted GPU path is classic KVM/virgl real GL; no
software/bundled fallback.
Implementation (all in `ports/mesa/src`, HEAD `fb2724503`):
`xv6_angle_passthrough.c` + GLES dispatch for
`GL_ANGLE_robust_client_memory`, `GL_CHROMIUM_bind_generates_resource`,
`GL_ANGLE_client_arrays`, `GL_ANGLE_request_extension`;
`GL_CHROMIUM_copy_texture` (blit/shader paths, `mesacopytexture`
reducer); robust uniform length fix; context-gated
`GL_ANGLE_webgl_compatibility` via
`EGL_ANGLE_create_context_webgl_compatibility` (`mesaanglepassthrough`
reducer); `GL_KHR_debug` pre-existing.
Runtime validation 2026-07-04: image staging verified (fresh Mesa in
/lib, /lib/dri first, no LD shadowing); M9 launch-only PASS on the
default path — zero missing-GL fatals, zero `--use-gl=disabled`
fallbacks, GPU errors 0 (archive
`20260704T192000Z-q2-mesa-angle-ladder-launch-only-first-default-path-pass`);
full video 44.2 presentedFPS / 29.6% drops across 75s of real GL
(archive `20260704T195500Z-q2-real-gl-full-video-m7-44fps`).
Ground truth: Chrome 150's validating decoder is DEAD (runtime-refused),
so the ladder was the only real-GL route; bundled ANGLE stays
disabled/symlinked to guest Mesa. The earlier bundled-ANGLE failure class
is understood (guest Mesa exposes zero pbuffer EGL configs; surfaceless
works) — irrelevant while the default path holds.
Session-stability fixes that unblocked the Q2 gates (landed 07-04 by the
parallel round): wl_shm interface-version interposition fix in
`wayland-shm.c` + `kde-wayland-registry` reducer; KDE ABI closure for
libinput/libudev (gesture-event + `udev_*@LIBUDEV_183` symbols, input-seat
enumeration in the shim); pactl removed from readiness path;
plasmashell system tray + artificial network SNI now opt-in
(`kde_plasma_systemtray=1`, `kde_network_status_sni=1`) after the tray
`compactRepresentationItem` KCrash isolation.
OPEN residuals (park unless they block a gate): plasmashell system-tray
crash root cause (tray stays opt-in), one `xkbcomp` #GP class, one
liveness roundtrip race archive, GL conformance depth beyond the ladder.

### R2 — PCID stale-TLB: CLOSED as a corruption lane (2026-07-04)

The "PCID corruption" WAS the R5 free-before-shootdown bug: signatures
re-verified same-family, all noflush-specific hazards audited covered, and
the opt-in retest on the fixed kernel ran 3/3 clean KDE batteries where the
07-02 trial corrupted within 2 (see N6 in the Work Order for the full
record + archives). PCID/noflush stays default-OFF on perf grounds: M3
amplification halves but desktop M4/M5 regress ~30% because page-level
shootdowns degrade to global flushes under PCID. Reopen only as a PERF
lane behind INVPCID-based per-PCID flush support.

### R3 — rcu_head_cache double-free: bounded-open, watch

5/5 clean trace-enabled provoke runs; `rcu_head_trace=1` diagnostic
staged default-off. May share the R5 root cause — correlate any recurrence
(one pre-R5-fix `slab_alloc: repairing corrupt freelist` line seen
07-04) with both lanes.

### R4 — pactl fault: CLOSED (payload-side, classified)

libpulse stack-probe/logging-recursion crash family; pactl removed from
the readiness path (default-off probe). Reopen only with a deterministic
reducer or kernel-corruption coupling.

### R5 — KWin startup corruption: ROOT-CAUSED + FIXED (2026-07-04)

One corruption family (24 sites, 11 libs, 7-10%/launch, attempt-1
cold-cache only): NULL-expected globals in the zero-fill tail of each
library's last RW file-backed page poisoned with recycled-frame residue.
Root cause: free-before-TLB-shootdown in `__vma_clear_range` mid-batch
overflow and `__vm_madvise_dontneed` (frames returned to the allocator
while stale translations existed; VA reuse let sibling threads scribble on
reallocated frames). Fix (kernel `67d7b5c`): shoot down the cleared span
before every early `page_free_anon_batch`; madvise per-segment flush
replaces the final flush; pgtable spinlock dropped around IPI-ack waits.
Battery green; M4 1883. Full forensics chain in the history file + git.
Statistical closure: 9/30+ clean attempt-1 launches accrued — count every
future battery's KWin launches.
LATENT hugepage bugs recorded, NOT fixed (all dormant behind
`vma_file_hugepage_collapse_enabled()==0`, and BLOCK re-enabling it):
partial-range 2MB over-free in `__vma_clear_range`; mprotect whole-2MB
protection bleed; madvise missing hugepage check; `page_free_anon_batch`
order-0-only; whole-folio COW head-page ref leak.
Secondary: KWin LibinputBackend nullptr deref (payload, -> N3);
crash-dump tooling gap (/core.PID never extracted, no symbolizer; fatal
dump could print the faulting VA's PTE/frame state).
Watch item: one unreproduced nographic boot stall (1/8, post-service
spawn) noted 07-04.

### R7 — Desktop responsiveness composite: a/b DONE, c partial, M8 = N5

R7a O(1) rq-assertion landed (11-15% of cycles removed; control knob
`rq_identify_linear_scan=1`). R7b inotify FIONREAD ABI fix landed and
closed (M8 345-512% -> ~85-105%; `linuxsyscallabitest inotify-fionread`
reducer). R7c timer_tick fastpath first slice landed; remaining
`__sched_timer` contention is bursty sub-jiffy expiry/insertion, not a
stuck owner. M8 debt is now attributed host-side to vCPU/KVM idle wake
cadence with guest PCs in `arch_idle_halt` (= N5, relates to N7 tick
work).

### R9 — Cursor out-of-range: OPEN (= N3)

User-visible: pointer does not track the host mouse. Triage order: (1)
EVIOCGABS absinfo vs the virtio tablet's 0..32767 (source audit says the
raw path normalizes to 0..65535 — verify at runtime); (2) raw ABS values
at screen edges vs host pointer; (3) cursor-plane transform under
`virtio_gpu_host_cursor_only=1`. Probe machinery is now harness-fixed and
source-only: `scripts/gpu/r9-cursor-contract-probe.expect`
(startup-injected `/r9-run.sh`, debugfs polling, QMP absolute injection after
`phase=armed`) — do NOT drive the probe over the interactive serial shell.
Dry-run `20260704T200628Z` passed with `qemu-dry-run.txt` after the R9
runner inherited the KDE ABI library path. Real probe `20260704T200703Z`
reached `armed_pid=57`, sent all five HMP monitor moves, and passed discovery:
udev enumerated event0/event1 (`count=2`) and libinput assigned `seat0`
(`r9_libinput_status rc=0 errno=0 reason=ready fd=6`), but HMP produced only
relative PS/2 samples: libinput reported 18 events with
`absolute_samples=0`, evdev collected no raw ABS samples, and `/dev/mouse`
clamped/repeated `flags=0 x=127 y=127`.

Coordinate slice result 2026-07-04: HMP routing was the failure layer.
Strict rerun `20260704T202206Z` selected `Mouse #3: QEMU Virtio Tablet
(absolute)` with HMP `mouse_set 3`, then still produced only PS/2 relative
samples and failed strictly with `reason=no_evdev_abs_samples`. The fixed
harness now opens `qemu-qmp.sock` and sends QMP `input-send-event` absolute
axis events in the virtio tablet's raw 0..32767 range, while retaining HMP
`info mice`/`mouse_set` logs as routing evidence and killing the spawned QEMU
process group on finish. Dry-run `20260704T202724Z` PASS; real R9
`20260704T202747Z` PASS: `/dev/mouse flags=1`; event1 EV_ABS samples
0/0, 32768/32768, 65535/65535, 16384/49150, 49150/16384; libinput absolute
samples 0/0, 640/400, 1279.980/799.988, 320/599.976, 959.961/200; summary
`evdev_abs_samples=10 mouse_samples=5 libinput_abs_samples=5 result=PASS
reason=coordinate_samples`.

Visible cursor-plane slice result 2026-07-04: new harness
`scripts/gpu/r9-cursor-visible-probe.expect` PASS at
`build-x86_64/r9-cursor-visible-probe-history/20260704T205234Z-r9-cursor-visible-probe`.
The run uses guest cursor mode (`qemu-dry-run.txt` has `show-cursor=off`)
with no `virtio_gpu_host_cursor_only=1`, preserves
`qemu-qmp-command.log` for `input-send-event` absolute X/Y injection and
`qemu-monitor-command.log` for `info mice` evidence only, and keeps the
strict coordinate contract alive (`/dev/mouse flags=1`,
`evdev_abs_samples=10`, `libinput_abs_samples=5`). Cursor traces prove
`virtio_gpu: cursor upload visible` and injected cursor transforms
0/0 -> 0/0, 32768/32768 -> 640/400, 65535/65535 -> 1279/799 with
`cursor_transform_out_of_range=0`. Crash-marker scan stayed clean for
panic/KWin/LibinputBackend nullptr signatures. `r9-visible-evidence.txt`
records `capture_visibility=NOT_PROVEN`: QEMU framebuffer captures do not
prove the GTK hardware cursor overlay, and this headless harness has no
deterministic host-window capture path. Guard runs after the harness-only
change passed and were archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T210241Z-r9-visible-desktop-interaction-pass`
(desktop-interaction-latency active sample, direct launch PASS) and
`build-x86_64/kde-plasma-desktop-smoke-history/20260704T205909Z-r9-visible-chromium-launch-only-pass`
(chromium-video launch-only PASS). The visible cursor gate is closed; keep
the KWin LibinputBackend nullptr payload here as later/secondary unless this
probe reproduces a startup/input crash. Do not reopen image injection, seat
plumbing, or kernel signed-16 storage without new contradictory evidence.

Follow-up 2026-07-06: user visual inspection still saw the cursor leave the
VM window, so the prior guest-input/visible probes were not sufficient for
normal desktop-interaction acceptance. The smoke harness now forces bounded
guest `/bin/mouseinject` against the settled framebuffer/workarea, disables
normal monitor/host cursor sync, and keeps monitor diagnostics opt-in only.
Focused KVM+virgl real-GL/no-software-fallback validation PASS:
`/home/es/xv6-os/build-x86_64/kde-plasma-desktop-smoke-guest-input-20260706.tar.gz`
with `input_source=guest`, `cursor_owner=guest-forced-normal-interaction`,
`monitor_path=disabled`, `host_cursor_sync_enabled=0`, `pointer-bounds PASS`,
bounded guest pixel coordinates, `/bin/mouseinject` command proof, and
`host_cursor_sync_ms=0`. No commit/push yet.

## Verification Gates

- Static: `git diff --check` in every repo level.
- Build: `cmake --build build-x86_64 --target kernel -j$(nproc)`; after
  user/rootfs changes also `--target user` then `--target rootfs-refresh`;
  ports: `cmake --build build-x86_64/ports --target port-<name> -j2`.
- Runtime battery (see Work Style rule 2). KDE stability claims need
  `timeout 900+ scripts/gpu/kde-plasma-desktop-smoke.expect` with
  `KDE_SMOKE_REDUCER=desktop-interaction-latency` and active sample.
- Reducers available: chromium-video (+`KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1`,
  +`KDE_SMOKE_CHROMIUM_VIDEO_KPROFILE=1`), kde-ready, pre-kwin-libinput,
  kde-wayland-registry, kde-wayland-liveness/client-liveness,
  desktop-interaction-latency; nographic: forktest/clonetest/cowtest,
  syscalltlb, wakestorm, linuxsyscallabitest, mesacopytexture,
  mesaanglepassthrough (host-staged).
- Archive every proof run to
  `build-x86_64/kde-plasma-desktop-smoke-history/<UTC>-<label>/`
  (exclude `*.img`, delete the scratch image after copying).

## Evidence Archives

- `build-x86_64/kde-plasma-desktop-smoke-history/` — all KDE/Chromium runs
  (key 07-04 entries: `*-q0-kprofile-wait-fix-first-valid-m7-window`,
  `*-p3-ext4-read-page-direct-*`, `*-r5-tlb-fix-*`,
  `*-q7-ordered-pageflip-*`, `*-q2-mesa-angle-ladder-*`,
  `*-q2-real-gl-full-video-m7-44fps`, `*-n2-ext4-direct-*`).
- `build-x86_64/yt-mainpage-freeze-repro/` — P0/M1 replays.
- `build-x86_64/syscall-tlb-proof/`, `build-x86_64/pageflip-ordered-ab-proof/`,
  `build-x86_64/desktop-bottleneck-profile/`,
  `build-x86_64/r5-kwin-startup-classification/`.
- Cleanup note 2026-07-04: deleted stale ignored `build_x86_64/` (~68G) and
  18 archived scratch `*.fs.img` files from old proof-history directories
  (~149M). Preserved `build-x86_64/fs.img`,
  `build-x86_64/kde-plasma-desktop-smoke/kde-plasma.fs.img`, current Q2
  real-GL evidence, N2 diagnostics, N3 R9 cursor/visible evidence, and the
  robust P1 nographic archive. Top/kernel/user/ports/mesa were clean after
  cleanup.
- Cleanup note 2026-07-07: removed 56 files and 25 dirs, reclaiming
  493,419,025,760 bytes (~459.6 GiB). Preserved `build-x86_64/fs.img`,
  `build-x86_64/kde-plasma-desktop-smoke/kde-plasma.fs.img`,
  `rootfs-generated-overlays`, `host-gui-runtime`, `kde-noble-plasma`, current
  20260707 proof dirs/logs/screenshots, and named evidence dirs. Old history
  scratch images and extra mouseinject `.fs.img` files are gone; no tracked
  source `D` entries.
- `docs/archive/plan-rewrite-20260702/active-work-plan-full-history.md` —
  full pre-compaction plans (2026-07-02 and 2026-07-04 snapshots) with
  every evidence chain.

## Commit Policy

Commit PERIODICALLY at every verified checkpoint (completed slice, passing
gate, before a risky change, before ending). Lane-scoped messages with a
lane reference; never bundle unrelated changes. Submodules deepest-first
(`ports/mesa/src` -> `ports`; `kernel`; `user`), then the top-level pointer
update in the same checkpoint. `git status` at top level AND every
submodule at handover. NEVER push without explicit user approval.
