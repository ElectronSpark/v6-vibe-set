# Active xv6 Work Plan

Last updated: 2026-07-01.

This is the top-level active plan for the current xv6 work. It merges the
live direction from the kernel sanitizer/logging, Linux GUI ABI, KDE/Chromium
performance, userland de-patching, and kernel cleanup lanes. Detailed evidence
stays in the linked plan or evidence files; do not delete historical files just
because their current action items are summarized here.

## Active Goals

1. Mature kernel-space diagnostics:
   - Keep the Linux-style kernel log and `syslog(2)`/`/proc/kmsg` path usable
     for guest-side debugging.
   - Keep KASAN and kmemleak build-time and runtime disable controls available.
   - Keep normal kernels free of sanitizer overhead unless explicitly enabled.

2. KDE/Chromium GUI ABI and performance:
   - KDE Plasma is the primary desktop target.
   - Chromium is the stress/regression probe.
   - Skip Weston unless explicitly reopened.
   - Fix FPS drops through Linux ABI, kernel, libc/sysroot, rootfs data, or
     xv6-owned probes/harness before considering imported source changes.
   - Previous Plasma responsiveness proof:
     `build-x86_64/kde-plasma-desktop-smoke-history/20260630T191127Z-desktop-interaction-launch-timing-zero-delay/`.
     Minimal tray staging reduced tray-open latency from `3754ms` to about
     `1.9s`, and the timing-aware launch probe reduced measured direct-launch
     stress from `7241ms` to `4339ms` by removing probe sleep overhead.
     Remaining bottlenecks are Konsole readiness (`konsole_wait_ms=3408`) and
     hover repaint/input timing around `1s` with some no-change samples.
   - 2026-06-30 Plasma responsiveness proof after redirecting long-running
     KDE child stdout/stderr to guest logs:
     `build-x86_64/kde-plasma-desktop-smoke-history/20260630T2219xx-desktop-interaction-quiet-kde-child-logs-pass/`.
     The reducer passed with desktop visible at `19072ms`
     (`first_nonzero_ms=12079`), hover changes at Chromium `1635ms`,
     Dolphin `1386ms`, KWrite `1410ms`, and Konsole `2994ms`, tray open
     `1582ms`, tray close `1162ms`, and direct launch `5899ms`
     (`konsole_wait_ms=4468`). `kde-prompt-sync.log` had no timeouts; the
     worst prompt sync was `410ms` after the prior failing proof showed a
     `20130ms` prompt-sync timeout during QML/system-tray log spam.
     Redirected logs are preserved as `kde-session-kwin.log`,
     `kde-session-plasma-child.log`, and `kde-network-status-sni.log`.
     Remaining measured launch bottlenecks are `sys_openat_ms=1333`,
     `sys_clock_gettime_ms=1235`, `vm_copyout_ms=1259`,
     `vm_copyin_ms=966`, and `vm_vma_validate_ms=780`; keep copyin fast
     disabled by default because the pactl #GP proof remains unresolved.
   - 2026-06-30 vDSO clock proof:
     `build-x86_64/clockbench-proof/20260630T233239Z-vdso-libcclock/`.
     glibc now registers `AT_SYSINFO_EHDR` and resolves
     `__vdso_clock_gettime@@LINUX_2.6`; 100k libc monotonic calls completed
     at `20ns` per call with only `2` kernel `clock_gettime` calls and
     `54` `vm_copyout` calls, while the raw syscall negative control still
     issued `100002` kernel clock calls, took `5629ns` per call, and drove
     `100613` `vm_copyout` calls. This removes the measured Plasma
     `sys_clock_gettime_ms` launch bottleneck class for imported glibc users.
   - Post-vDSO KDE interaction attempts did not yet yield clean hover/tray
     metrics. Preserved artifacts:
     `build-x86_64/kde-plasma-desktop-smoke-history/20260630T233547Z-desktop-interaction-vdso-wireplumber-gp/`,
     `build-x86_64/kde-plasma-desktop-smoke-history/20260630T233846Z-desktop-interaction-vdso-visible-passive-sample-timeout/`,
     `build-x86_64/kde-plasma-desktop-smoke-history/20260630T233955Z-desktop-interaction-active-sample-kwin-qtcore-gp/`,
     and
     `build-x86_64/kde-plasma-desktop-smoke-history/20260630T234144Z-desktop-interaction-active-sample-kwin-libkwin-gp/`.
     The passive-sample run rendered a visible host desktop, but the reducer
     had no `FB: virgl resource-scanout sample` lines, so active sampling is
     the right harness mode for this lane. The two active-sample retries hit
     KWin startup #GPs before metrics: one at
     `libQt5Core.so.5` file offset `0xdbc9f`
     (`QMutex::unlock()`, bad `this=0x74696c6962697373`) and one at
     `libkwin.so.5` file offset `0x1fe3d4`
     (`EffectsHandlerImpl::checkInputWindowStacking()`, bad
     `this=0x7269757165725f65`). Both bad pointers decode as text fragments,
     so continue from the KWin/Qt object-pointer corruption reducer before
     trusting new desktop interaction latency numbers.
  - 2026-06-30 copyout-fast containment proof:
     default-on `vm_copyout_present_fast` was the first metrics-backed
     corruption bottleneck after vDSO. Two normal active-sample KDE
     interaction retries above crashed in KWin with text-like bad pointers.
     The same reducer with `QEMU_APPEND_EXTRA='vm_copyout_present_fast=0'`
     completed and is archived at
     `build-x86_64/kde-plasma-desktop-smoke-history/20260630T235048Z-desktop-interaction-copyout-fast-default-off-proof/`.
     Kernel now keeps `vm_copyout_present_fast` opt-in only. The normal-default
     verification, with no `vm_copyout_present_fast` cmdline knob, passed at
     `build-x86_64/kde-plasma-desktop-smoke-history/20260630T235300Z-desktop-interaction-copyout-fast-default-off-normal-pass/`.
     It reached visible desktop at `17309ms`, hover changed for Chromium at
     `1365ms` and Dolphin at `2668ms`, KWrite and Konsole were no-change in
     this sample window, tray open was `1761ms`, tray close was `940ms`, and
     direct launch was `5289ms`. The proc-cmdline rewrite reducer still passed
     at `build-x86_64/proc-cmdline-rewrite-proof/20260630T235318Z/run.log`.
     Counter proof: default boot had `vm_copyout_calls=1302` and
     `vm_copyout_fast_hits=0`
     (`build-x86_64/copyout-fast-proof/20260630T235357Z-default-off/`), while
     the explicit opt-in boot had `vm_copyout_calls=1316` and
     `vm_copyout_fast_hits=117`
     (`build-x86_64/copyout-fast-proof/20260630T235558Z-opt-in/`). Treat
     `vm_copyout_present_fast=1` as experimental only until its
     lifetime/concurrency rules are proven.
   - 2026-07-01 Plasma responsiveness metrics after the `vm_copyinstr()`
     chunked-copy NUL race fix are archived at
     `build-x86_64/kde-plasma-desktop-smoke-history/20260701T002233Z-desktop-interaction-copyinstr-nulfix-kprofile/`.
     The reducer passed with desktop visible at `21338ms`
     (`first_nonzero_ms=12020`), hover changes at Chromium `1373ms`,
     Dolphin `3198ms`, KWrite `999ms`, and Konsole `1113ms`, tray open
     `1520ms`, tray close `1077ms`, and direct launch `5872ms`
     (`konsole_wait_ms=4727`). The fixed kernel built cleanly and
     `git -C kernel diff --check -- kernel/mm/vm.c` passed. The metrics do
     not justify treating `vm_copyinstr()` as the main responsiveness fix:
     direct-launch hot buckets remain `vm_vma_validate_ms=1268`,
     `vm_copyin_ms=1266`, `sys_openat_ms=1260`, `vm_copyout_ms=1116`, and
     `ext4_pcache_read_page_ms=593`.
   - 2026-07-01 PCID opt-in A/B evidence is archived at
     `build-x86_64/kde-plasma-desktop-smoke-history/20260701T002438Z-desktop-interaction-pcid-optin-kprofile/`.
     Booting with `QEMU_APPEND_EXTRA='x86_pcid=1'` enabled PCID
     (`vm_asid_init: max ASID = 4095`) and improved the direct-launch CPU
     counters (`elapsed_ms=3906`, `sys_openat_ms=1078`,
     `vm_copyin_ms=1096`, `vm_copyout_ms=998`,
     `vm_vma_validate_ms=1107`, `ext4_pcache_read_page_ms=420`,
     `konsole_wait_ms=3771`), but it regressed/noised visible interactions:
     desktop visible was `21913ms`, Chromium hover was `no-change`, tray open
     was `1898ms`, and tray close was `1192ms`. Keep PCID opt-in only; the
     x86 PCID code still documents prior KWin/Qt corruption under SMP GUI
     workloads. The next implementation target should come from reducer proof
     around the shared copyin/VMA/openat path, not a PCID default flip.
   - 2026-07-01 proc-cmdline rewrite proof after the first `vm_copyinstr()`
     patch emitted guest-side
     `proc-cmdline-rewrite-probe result=PASS argv_span=116 title_span=530`
     but the expect wrapper timed out after reboot with
     `PROC-CMDLINE-REWRITE-PROOF-FAIL reason=eof-missing-proof before=1 after=0 pass=1`.
     Treat this as partial guard evidence only; rerun or harden the harness
     before using it as final proof for future path-copy changes.
   - 2026-07-01 Plasma poll-wait A/B evidence now confirms the current
     Konsole readiness bottleneck is still wait/wakeup dominated. The safer
     capability-gated run with `poll_notify_full_wait=1` is archived at
     `build-x86_64/kde-plasma-desktop-smoke-history/20260701T070514Z-desktop-interaction-poll-notify-flagged-pass/`
     and measured direct Konsole launch `6180ms`, `konsole_wait_ms=4980`,
     `sys_poll_blocking_ms=13636`, `sys_ppoll_ms=4979`, and
     `sys_futex_wait_ms=6225`. The matched full-wait-off run at
     `build-x86_64/kde-plasma-desktop-smoke-history/20260701T075456Z-desktop-interaction-poll-full-wait-off-ab/`
     passed with GPU, network, and PipeWire/Pulse guards intact but regressed
     direct launch to `7796ms`, `konsole_wait_ms=6394`,
     `sys_poll_blocking_ms=17041`, `sys_ppoll_ms=6303`, and
     `sys_futex_wait_ms=7061`, while `sys_openat_ms` stayed comparable.
     Keep `poll_notify_full_wait` default-off until Chromium-video and broader
     notify-backed fd regression proof justify enabling it.
   - 2026-07-01 gated kqueue timer-dispatch trace:
     `build-x86_64/kde-plasma-desktop-smoke-history/20260701T082054Z-desktop-interaction-kqueue-timer-trace-visible-timeout/`
     showed a visible host desktop and intact GPU/network/audio guards, but
     the passive visible detector had no `FB: virgl resource-scanout sample`
     lines and timed out. The active-sample rerun passed at
     `build-x86_64/kde-plasma-desktop-smoke-history/20260701T082442Z-desktop-interaction-kqueue-timer-trace-active-sample-pass/`
     with desktop visible `15784ms`, hover changes Chromium/Dolphin/KWrite/
     Konsole `1561/966/2999/1157ms`, panel hover `910-1339ms`, start menu
     open/close `2076/1254ms`, tray open/close `1570/956ms`, and direct
     Konsole launch `7362ms` (`konsole_wait_ms=5925`). The new default-off
     `kde_kqueue_spin_trace` fields recorded `15` timed kqueue wakes, `4`
     timer-fired wakes, `4` empty timeout wakes, and only `max_dispatch_ms=5`
     / `max_overrun_ms=5`. That rules out raw scheduler timer dispatch as the
     dominant Konsole readiness delay in this run; continue the next
     responsiveness branch in the Konsole/Qt/Wayland/DBus event/admission
     path while keeping `poll_notify_full_wait` default-off.

3. Plan consolidation:
   - This file is the entry point.
   - Active plans should be compact and point to durable evidence.
   - Evidence archives remain append-only unless intentionally moved to a
     dated historical file.

4. Kernel deduplication:
   - Deduplicate only after evidence identifies a shared kernel pattern.
   - Prefer small helpers in the owning subsystem over broad mechanical
     rewrites.
   - Each dedup change needs a build gate and, for GPU/desktop code, a runtime
     GUI or reducer gate.

5. Userland upstream cleanliness:
   - Imported KDE/Qt/KWin/Plasma/Xwayland/Mesa/Chromium sources stay
     upstream-clean.
   - Userland source deltas should move down into kernel ABI, libc/sysroot,
     rootfs data, build wrappers, or xv6-owned probes/shims.

## Current Evidence Snapshot

- KASAN and kmemleak:
  - `XV6_KASAN` is build-time opt-in.
  - Full sanitizer removal is `-DXV6_KASAN=OFF` at configure/build time.
  - `XV6_KMEMLEAK` controls whether the fixed-table allocation tracker is
    compiled at all. It defaults on so existing `/proc/kmemleak` diagnostics
    stay available, and `-DXV6_KMEMLEAK=OFF` compiles the tracker and allocator
    hooks to stubs.
  - Runtime quiet controls accept exact false spellings
    `0`, `off`, `false`, and `no`. A KASAN build with `kasan=0` keeps compiler
    callbacks linked but skips shadow initialization and reporting. KLOG
    similarly defaults on in `XV6_KLOG=ON` builds and `klog=0/off/false/no`
    disables only the backing ring, not the procfs/syslog surface.
  - `kmemleak` defaults on in KASAN builds and off otherwise unless
    `kmemleak=1/on/true/yes` is passed. Unknown values leave each diagnostic
    at its default instead of silently flipping the gate.
  - Runtime allocator accounting hooks cover the steady-state buddy/page and
    slab allocators, including page alloc/free, batched anonymous page frees,
    and slab alloc/free/noshrink. The hooks return before taking locks when
    disabled, and compile to KASAN no-ops in `XV6_KASAN=OFF` builds and
    kmemleak no-ops in `XV6_KMEMLEAK=OFF` builds. The early boot allocator
    remains outside leak accounting because it has no normal free lifecycle.
  - Current disable proof:
    `build-x86_64/kasan-diagnosis-proof/20260630T081638Z-current-disabled-guest-pass/run.log`.
    This KASAN build booted with `kasan=0 kmemleak=0`, printed
    `kasan: disabled by cmdline`, reached a shell with `desktop=0`, and
    `/proc/kmemleak` reported
    `enabled=0 current=0 bytes=0 ... allocs=0 frees=0`. The guest wrote
    `KASAN_DISABLE_DIAG_PASS`; the artifact status file is `0`.
  - Current default-on proof:
    `build-x86_64/kasan-diagnosis-proof/20260630T082045Z-current-default-after-selftest/run.log`.
    This KASAN build booted to a shell with `kmemleak=1`,
    `kasan: enabled page-shadow callback checks`, and `/proc/kmemleak`
    reporting active page/slab accounting. The guest wrote
    `KASAN_DEFAULT_AFTER_PASS`; the artifact status file is `0`. No `PANIC`,
    fatal fault, selftest access report, or unexpected KASAN access report
    appeared in either current proof.
  - KASAN coverage today is page-shadow plus slab-bitmap checking. It is useful
    for page/slab use-after-free, freed-page access, and slab object-boundary
    mistakes, but it is not yet a byte-granular redzone implementation and does
    not classify leaks by reachability.
  - Positive KASAN detector proof:
    `build-x86_64/kasan-diagnosis-proof/20260630T081920Z-kasan-selftest/run.log`.
    Booting with `kasan_selftest=1 kmemleak=1` intentionally checked a freed
    page and a freed slab object through `kasan_check_range()`. The guest log
    contains `poisoned-page-access`, `slab-free-object`, and
    `kasan: selftest page_uaf=PASS slab_uaf=PASS status=PASS`, then reached a
    shell and wrote `KASAN_SELFTEST_PASS`.
  - 2026-06-30 KASAN selftest refresh:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T143739Z-kasan-selftest/qemu-kasan-selftest.log`
    and
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T144059Z-kasan-copy-boundary-selftest/qemu-kasan-selftest.log`
    both contain
    `kasan: selftest page_uaf=PASS callback=PASS slab_inbounds=PASS slab_bounds=PASS slab_uaf=PASS status=PASS`.
    The refreshed selftest now proves the manual ASAN callback path and slab
    object-boundary checks in addition to page/slab use-after-free detection.
    The second run was after adding KASAN checks to `vm_copyout()`,
    `vm_copyin()`, `either_copyout()`, and `either_copyin()` kernel-buffer
    boundaries. Its `status.txt` records `status=PASS`; QEMU was terminated
    immediately after the PASS marker to keep the VM lane clean.
  - 2026-06-30 string-helper KASAN proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T144916Z-kasan-string-attribution-selftest/qemu-kasan-selftest.log`
    contains
    `kasan: selftest page_uaf=PASS callback=PASS slab_inbounds=PASS slab_bounds=PASS slab_uaf=PASS string=PASS status=PASS`.
    This run validates explicit report-only checks in `memset()`, `memmove()`,
    `memcpy()`, and `memcmp()` so x86 inline `rep stosq/movsq` bulk transfers
    no longer bypass KASAN. `memcpy()` now shares the checked implementation
    directly so reports attribute to the real caller rather than the wrapper.
    Normal, KASAN, and `build-x86_64-diagnostics-off` kernels all rebuilt
    successfully after the string-helper patch.
  - 2026-06-30 large `kvmalloc()` KASAN proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T145757Z-kasan-vmalloc-selftest/qemu-kasan-vmalloc-selftest.log`
    contains
    `kasan: vmalloc_selftest inbounds=PASS overflow=PASS uaf=PASS status=PASS`.
    The allocator now records exact requested size for kernel-VM allocations,
    reports page-tail overflow as `kvmalloc-object-overflow`, and reports
    post-`kvfree()` access as `kvmalloc-free-access`. Normal, KASAN, and
    `build-x86_64-diagnostics-off` kernels rebuilt successfully after this
    patch.
  - 2026-06-30 vmalloc runtime-disable proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T145911Z-kasan-vmalloc-selftest-disabled/qemu-kasan-disabled.log`
    booted with `kasan=0 kasan_selftest=1 kmemleak=0`, printed
    `kasan: disabled by cmdline`, and emitted no KASAN selftest,
    `kvmalloc-*`, or `vmalloc_selftest` lines. Build-off coverage is through
    the same `XV6_KASAN=OFF` stubs used by the diagnostics-off build.
  - 2026-06-30 requested-size slab KASAN proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T151059Z-kasan-slab-requested-selftest/qemu-kasan-slab-requested-selftest.log`
    contains
    `kasan: selftest page_uaf=PASS callback=PASS slab_inbounds=PASS slab_bounds=PASS slab_requested_bounds=PASS slab_uaf=PASS string=PASS status=PASS`
    and a `slab-object-overflow` report for a `kmm_alloc(33)` tail access.
    The slab allocator now keeps KASAN-only per-object requested-size metadata
    for `kmm_alloc()` callers while direct typed `slab_alloc()` users keep
    full-cache-object bounds. Normal, KASAN, and
    `build-x86_64-diagnostics-off` kernels rebuilt successfully after this
    patch.
  - 2026-06-30 requested-size slab runtime-disable proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T151159Z-kasan-slab-requested-disabled/qemu-kasan-slab-requested-disabled.log`
    booted with `kasan=0 kasan_selftest=1 kmemleak=0`, printed
    `kasan: disabled by cmdline`, and emitted no KASAN selftest,
    `slab_requested_bounds`, or `kvmalloc-*` lines.
  - Kernel submodule checkpoint:
    `12ce331 kernel: add diagnostic sanitizers and log controls` contains the
    optional KLOG/KMEMLEAK controls, KASAN copy/string/vmalloc/requested-slab
    checks, `/proc/kmsg`, `/proc/kmemleak`, `syslog(2)`, and the KLOG/KMEMLEAK
    exact-bool parser dedup slice. Mixed `vm.c` and `procfs/inode.c` files were
    partially staged so unrelated Chromium/mprotect/live-cmdline diagnostics
    remain unstaged.
  - Selftest disable proof:
    `build-x86_64/kasan-diagnosis-proof/20260630T081948Z-kasan-selftest-disabled/run.log`.
    Booting with `kasan=0 kmemleak=0 kasan_selftest=1` still printed
    `kasan: disabled by cmdline`, emitted no KASAN selftest access reports, and
    `/proc/kmemleak` reported `enabled=0 current=0 bytes=0`.
  - The current `kmemleak` implementation is a fixed-table active-allocation
    tracker. It does not yet do Linux-style reachability scanning, so boot-time
    `leak ...` lines are live allocations until a later scanner classifies
    unreachable objects.
  - 2026-06-30 build verification after disable-control audit:
    `cmake --build build-x86_64 --target kernel -j2` passed with
    `XV6_KASAN=OFF`, and `cmake --build build-x86_64-kasan --target kernel -j2`
    passed with `XV6_KASAN=ON`.
  - 2026-06-30 exact parser audit:
    `kasan=on`, `kmemleak=off`, and `klog=off` now use exact
    case-insensitive boolean parsing, so values such as `kasan=on` are no
    longer misread as false. Fresh builds passed for `build-x86_64`,
    `build-x86_64-kasan`, `build-x86_64-kmemleak-off`, and
    `build-x86_64-klog-off`. `nm -S` confirms normal kernels have KLOG and
    KMEMLEAK but no ASAN callbacks, KASAN kernels have `__asan*`/`kasan_*`,
    `XV6_KMEMLEAK=OFF` removes `kmemleak_*`, and `XV6_KLOG=OFF` leaves only
    small public `klog_*` stubs. A nographic boot/parser artifact at
    `build-x86_64/kasan-diagnosis-proof/20260630T-disable-controls/run.log`
    shows the guest received `kasan=on kmemleak=off klog=off` and enabled
    KASAN without printing the KMEMLEAK enabled banner; that run timed out
    before an interactive shell, so keep the older shell-based runtime proofs
    as the stronger `/proc/kmemleak` evidence.
  - 2026-06-30 hard-off proof:
    `cmake -S . -B build-x86_64-kmemleak-off -DXV6_KMEMLEAK=OFF -DXV6_KASAN=OFF`
    followed by `cmake --build build-x86_64-kmemleak-off --target kernel -j2`
    passed. `nm build-x86_64-kmemleak-off/kernel/kernel.elf` shows no
    `kmemleak_*` symbols, while `build-x86_64/kernel/kernel.elf` does.
  - 2026-06-30 combined diagnostics hard-off proof:
    `cmake -S . -B build-x86_64-diag-off -DXV6_KASAN=OFF -DXV6_KMEMLEAK=OFF -DXV6_KLOG=OFF`
    followed by `cmake --build build-x86_64-diag-off --target kernel -j2`
    passed. `kasan.c` and `kmemleak.c` are absent from the built object set;
    `nm -S build-x86_64-diag-off/kernel/build/kernel/kernel_with_symbols_elf`
    shows only tiny public `klog_*` stubs and no `klog` ring object.
  - 2026-06-30 fresh disable audit proof:
    `cmake -S . -B build-x86_64-diagnostics-off -DXV6_KASAN=OFF -DXV6_KMEMLEAK=OFF -DXV6_KLOG=OFF`
    followed by `cmake --build build-x86_64-diagnostics-off --target kernel -j2`
    passed. The generated caches record all three options as `OFF`. `nm` shows
    no `__asan*`, `kasan_*`, or `kmemleak_*` bodies, and only the public
    diagnostic stubs `klog_ring_enabled` and `slab_kasan_check_range` remain.
    After the KASAN copy-boundary patch, the same
    `cmake --build build-x86_64-diagnostics-off --target kernel -j2` hard-off
    build passed again, proving the new call sites still compile through the
    stubs.
  - 2026-06-30 disable recheck after plan consolidation:
    `cmake --build build-x86_64-diagnostics-off --target kernel -j2` passed
    with cache values `XV6_KASAN=OFF`, `XV6_KMEMLEAK=OFF`, and `XV6_KLOG=OFF`.
    Fresh `nm -S build-x86_64-diagnostics-off/kernel/kernel.elf` output shows
    no `__asan*`, `kasan_*`, or `kmemleak_*` symbols; only the tiny public
    `klog_*` stubs remain.
  - 2026-06-30 disable recheck after Chromium-map harness work:
    `cmake -S . -B build-x86_64-diagnostics-off -DXV6_KASAN=OFF -DXV6_KMEMLEAK=OFF -DXV6_KLOG=OFF`
    followed by `cmake --build build-x86_64-diagnostics-off --target kernel -j2`
    passed. The generated cache still records all three diagnostics as `OFF`.
    `nm -S build-x86_64-diagnostics-off/kernel/kernel.elf` shows no
    `__asan*`, `kasan_*`, or `kmemleak_*` bodies; only tiny public KLOG and
    slab-KASAN call-site stubs remain.
  - Disable model: `XV6_KASAN` removes sanitizer instrumentation at build time;
    `XV6_KMEMLEAK=OFF` removes the fixed-table leak tracker object and public
    symbols at build time; `kasan=0` and `kmemleak=0` are runtime quiet gates
    for diagnostic builds.
  - Open KASAN gaps: requested-size redzones now cover `kmm_alloc()` slab
    callers and large `kvmalloc()` allocations, but typed `slab_alloc()` caches
    intentionally retain full object-size bounds. KASAN is not byte-granular
    for arbitrary intra-object accesses, and kmemleak does not yet classify
    allocations by reachability.

- Logging:
  - Linux-style kernel log plumbing includes `syslog(2)`, `/proc/kmsg`, and
    staged `xv6-dmesg`.
  - `syslog(2)` now follows the existing root-only `capable()` model; `/proc/kmsg`
    remains mode `0400`.
  - Disable model:
    `-DXV6_KLOG=OFF` compiles out the backing log ring and leaves only tiny
    console-preserving stubs; `klog=0` is the runtime quiet gate for diagnostic
    builds. The runtime gate accepts exact false spellings `klog=0`,
    `klog=off`, `klog=false`, and `klog=no`. `kloginit()` now consumes the
    already-discovered x86 boot command line before the ring is marked ready;
    RISC-V does the same early `/chosen/bootargs` scan before full FDT
    initialization. The ring path still lazily rechecks for platforms that
    publish the command line later. Console output is preserved when the ring
    is disabled; `-DXV6_KLOG=OFF` remains the hard-off path that removes the
    backing ring object from the build.
  - 2026-06-30 disable proof:
    `cmake -S . -B build-x86_64-klog-off -DXV6_KLOG=OFF -DXV6_KASAN=OFF -DXV6_KMEMLEAK=ON`
    followed by `cmake --build build-x86_64-klog-off --target kernel -j2`
    passed. `nm -S build-x86_64/kernel/build/kernel/kernel` shows the default
    build has a `0x4040`-byte local `klog` backing object; the
    `build-x86_64-klog-off` kernel has only small public stub functions and no
    `klog` backing object.
  - 2026-06-30 runtime `klog=0` proof after the early gate fix:
    `build-x86_64/klog-proof/20260630T083123Z-runtime-klog0-bin-dmesg-window/run.log`.
    A nographic guest booted with `klog=0`, preserved serial console boot
    output, reached the shell, and `/bin/dmesg` emitted no `/proc/kmsg` replay
    before returning to the prompt. The status file records
    `status=PASS reason=empty-kmsg-bin-dmesg-window`.
  - 2026-06-30 runtime `klog=off` proof after accepting false spellings:
    `build-x86_64/klog-proof/20260630T085150Z-runtime-klogoff-bin-dmesg-window/run.log`.
    The guest command line contained `desktop=0 ... klog=off`, reached a shell,
    and `/bin/dmesg` emitted no kernel log replay before the next prompt. The
    status file records
    `status=PASS reason=no-kmsg-replay-bin-dmesg-window`; one async login1 shim
    line appeared in the terminal window and is recorded separately as
    `async_login1=1`.
  - 2026-06-30 build verification after the runtime gate fix:
    `cmake --build build-x86_64 --target kernel -j2` and
    `cmake --build build-x86_64-kasan --target kernel -j2` both passed.
  - The combined diagnostics hard-off proof above also verifies KLOG can be
    compiled out alongside KASAN and kmemleak.
  - Proof artifact:
    `build-x86_64/klog-proof/20260630T023740Z/run.log`.

- Chromium GBM video:
  - Focused proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T042438Z-chromium-video-gbm-fps-27-2/`.
  - Result: page/video reached playback but failed at `presentedFPS=27.2`,
    `decodedFPS=60.6`, `dropPct=58.23`.
  - Latest startup-safe Wayland-debug run:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T043735Z-chromium-video-wayland-debug-kwin-died-perf-start-missing/`.
  - That run failed before video perf start. GBM preprobe and sampler passed, but
    the Chromium GPU child reported `gl=none,angle=none` despite launcher
    `egl-angle/opengles` flags, no renderer role stabilized, no Chromium primary
    Wayland surface committed, KWin/Wayland died, and scanout readback found no
    present resource.
  - Linux comparison showed that forced Chromium `egl-angle/opengles` is not a
    clean baseline on this host either: it produced no stable GPU/renderer
    process roles in the Linux KWin/Wayland control. Treat those flags as an
    explicit reducer, not as evidence of a kernel-only xv6 failure.
  - Clean Linux KWin/Wayland Chromium baseline:
    `build-x86_64/linux-chromium-vm-control/20260630T050817Z-ubuntu-kwin-wayland-http-perf-no-gl-no-xwayland-summary-fixed/`.
    It passed with `LINUX_CHROMIUM_USE_GL=none`,
    `LINUX_CHROMIUM_USE_ANGLE=none`, and
    `LINUX_CHROMIUM_KWIN_XWAYLAND=0`: Chromium stabilized at one GPU process,
    six renderer processes, `measureFPS=30.2`, `rvfcFPS=41.947`,
    `playbackRate=0.998`, `ctx_submit=2734`, and `fence_resp=2847`.
    Screenshot capture was missing, so the visual artifact gap remains in the
    Linux harness even though playback metrics passed.
  - The current xv6 lead is therefore child-process/GPU admission and sustained
    render-node submission under Chromium's normal Linux launch policy, followed
    by compositor/surface teardown; do not treat it as a pure scanout pacing
    failure yet.
  - xv6 normal-launch reducer after adding the auto-GL disable knob:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T052215Z-no-forced-gl-capture-status-timeout/`.
    Harness label was `chromium-video-capture-status-timeout`, but the
    post-evidence file shows capture completed and the real app result was
    `status=FAIL reason=perf-start-missing`.
  - That run proved `KDE_SMOKE_CHROMIUM_AUTO_GL_FLAGS=0` reached the guest:
    launcher `final_argc=29`, `argv_use_gl_count=0`, `argv_use_angle_count=0`,
    and `WAYLAND_CHROMIUM_AUTO_GL_FLAGS="0"`. There were no Chromium GL-request
    errors, no GPU-process exit/config errors, no INT3, and no page fault.
  - The new failure shape is renderer/media/surface delivery: one browser, one
    GPU process, one utility process, and eight zygotes were seen, but no stable
    renderer role; Chrome produced seven execbuffers, only one
    `PERF-VIDEO before-src` line, no playback/result lines, and the primary
    Wayland toplevel committed once with no buffer attached.
  - Latest full normal-launch lifecycle/IPC/scanout run:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T063826Z-normal-gbm-perf-start-missing-sampler-timeout/`.
    The top-level harness label was
    `chromium-video-sampler-completion-timeout`, but post-evidence showed the
    sampler completion marker was seen and the real app result was
    `status=FAIL reason=perf-start-missing`.
  - That run kept implicit Chromium GL/ANGLE flags disabled
    (`argv_use_gl_count=0`, `argv_use_angle_count=0`), passed both the RELA
    preprobe and GBM preprobe, had no Chromium INT3, page fault, GL-request
    error, GPU-process init/config/exit error, or loader assertion, and captured
    eight nonblack frames.
  - The durable failure shape is still renderer/media/surface delivery:
    Chromium reached browser, GPU, utility, zygote, and zygote child/thread
    evidence, but no `--type=renderer` argv or stable renderer role; the page emitted only the
    `PERF-VIDEO before-src` console event; the primary Wayland `xdg_toplevel`
    surface committed once with no buffer attach/damage/frame callback; and
    Chrome submitted only six execbuffers. Scanout had two KMS framebuffer/BO
    resources but static KMS hashes and no present samples, so capture is
    downstream evidence until renderer/media delivery is fixed.
  - The harness renderer-candidate summary was tightened after this run so
    zygote children no longer count as renderer candidates unless their argv or
    role explicitly says renderer.
  - The run also printed `vfs_iput` duplicate-remove warnings during Chrome
    child cleanup. Treat that as a reducer lead only if it recurs with matching
    child lifecycle evidence; do not assume it caused the missing media start.
  - Skip-canplay follow-up:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T064705Z-skip-canplay-no-perf-lines-sampler-timeout/`.
    The top-level label was still
    `chromium-video-sampler-completion-timeout`, while post-evidence passed in
    capture-skipped mode because this run used a custom URL and zero samples.
    The `skipCanPlay=1` URL advanced the page beyond the previous
    `before-src` stop: Chromium logged `canplay-skip`, `fetch-begin`,
    `after-src`, `after-load-call`, `PERF-VIDEO start`, `before-play`,
    `after-play-call`, microtasks, `fetch-error`, and a timer tick.
  - That proves the media capability query path (`canPlayType` / MSE query) is
    one xv6 blocker in the normal page. It is not the whole failure: the video
    element still stayed at `ready=0`, `net=3`, `currentSrc=(empty)`,
    `presented=0`, `decoded=0`, and `dropped=0`; there were no playing, tick,
    rvfc, or result events. The diagnostic `fetch()` failed because Chromium
    blocks `file://` fetch from `origin null` by CORS, so use it only as a
    script-progress marker, not as proof the `<video src=...>` file load failed
    for the same reason.
  - With the tightened role detector, the same run showed
    `renderer_seen=0`, `renderer_candidate_seen=0`, `renderer_pids=0`, one GPU
    process, one utility process, and seven/eight zygote-role processes. Wayland
    still had an xdg_toplevel commit with no primary buffer attach/damage/frame
    callback (`reason=no-primary-surface-buffer`), and Chrome submitted only six
    execbuffers. The next reducer should compare Linux versus xv6 for video
    file admission and browser-to-zygote renderer launch, not EGL context
    robustness.
  - Added an xv6-owned, off-by-default media FD trace:
    `chrome_media_fd_trace=1`. It logs only Chrome-owned open/openat, fstat,
    lseek, read, and pread64 activity for `.mp4` and `perf-video.html` paths
    with `chrome-media-fd-trace:` prefixes. Use it before broad
    `chrome_fd_trace=1` because the broad trace also enables noisy mmap/fd
    logging.
  - First media-FD trace run:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T065754Z-media-fd-trace-html-only-gpu-crash/`.
    Top-level label was `chromium-video-capture-status-timeout`, while
    post-evidence wrote `status=FAIL reason=chrome-crash-regression`. The new
    trace proved Chromium opened `/share/webkit/perf-video.html`, fstat'd it,
    and read it with two `pread64` calls. No `chrome-media-fd-trace` line for
    `/share/webkit/perf-1280x800-60fps.mp4` appeared.
  - This narrows the local-file branch: xv6 can serve the HTML file through
    Chrome's kernel file path, but the video element never reaches a kernel
    MP4 open before the browser/GPU/renderer path fails. The same run still had
    `currentSrc=(empty)`, no playing/tick/rvfc/result events, no renderer argv
    or candidate, Wayland `reason=no-primary-surface-buffer`, and only eleven
    Chrome execbuffers. It also exposed a GPU-process retry/crash path with
    repeated `eglCreateContext ... EGL_BAD_ATTRIBUTE` from pid 525 despite no
    forced `--use-gl`/`--use-angle`; compare against the Linux GPU-preferences
    baseline before changing EGL behavior.
  - Chromium-only EGL preload trace run:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T070912Z-chromium-egl-trace-no-egl-entrypoints-html-only/`.
    Top-level label was `chromium-video-sampler-completion-timeout`, but
    post-evidence completed in capture-skipped mode with
    `status=PASS reason=capture-skipped`. This run proves the EGL preload is
    Chromium-scoped and no longer kills KWin startup: the trace saw browser,
    one GPU process, zygotes, render-node opens, DRM ioctls, socketpair/SCM
    traffic, and inherited `LD_PRELOAD`/`WAYLAND_CHROMIUM_EGL_TRACE=1` in the
    GPU process.
  - The same EGL-trace run saw zero EGL entrypoints:
    `egl_get_display=0`, `egl_initialize=0`, `dlsym=0`, `dlopen=0`,
    `choose=0`, `create_context=0`. It also had no GPU init/config/exit
    errors and no GL-request errors. Therefore the immediate normal-path
    blocker is not an xv6 EGL attribute rejection at a visible call site.
  - Media/process evidence in that run stayed consistent with the HTML-only
    branch: `chrome_media_fd_trace` again proved only `perf-video.html`
    open/fstat/two `pread64` calls; there was still no MP4 open,
    `currentSrc=(empty)`, `ready=0`, no playing/tick/rvfc/result, no renderer
    role (`renderer_pids=0`), one GPU process, no INT3/fault, Wayland
    `reason=no-primary-surface-buffer`, and seven Chrome execbuffers.
    Next reducer target is browser-to-zygote/renderer admission and Mojo/IPC
    handoff for the video load, not EGL/GBM robustness.
  - procfs argv-rewrite ABI fix:
    Chromium renderers can be forked from the zygote and then rewrite argv
    in-place. Linux `/proc/<pid>/cmdline` reflects that live user argv memory,
    while xv6 had been returning the exec-time snapshot. Kernel procfs now
    falls back to the snapshot but prefers live argv/environ ranges when they
    can be copied from the task vm.
  - Independent reducer proof:
    `build-x86_64/proc-cmdline-rewrite-proof/20260630T072619Z/`.
    The console-only harness booted with `desktop=0`, ran
    `/bin/proc-cmdline-rewrite-probe`, and passed with
    `status=PASS reason=live-cmdline`. The run log shows the after-read from
    `/proc/self/cmdline` containing
    `proc-cmdline-live-probe|--type=renderer|marker=live|...`.
    The rootfs CMake dependency list now includes the probe source so future
    edits force image refresh.
  - Exec race follow-up: procfs live cmdline copying now suppresses live
    argv/environ reads while `execve` is in progress, so `/proc/<pid>/cmdline`
    cannot mix the new vm with the old argv snapshot. Regression proof:
    `build-x86_64/proc-cmdline-rewrite-proof/20260630T074722Z/run.log`
    passed with `PROC-CMDLINE-REWRITE-PROOF-PASS reason=live-cmdline`.
  - Extended setproctitle follow-up:
    `build-x86_64/proc-cmdline-rewrite-proof/20260630T104156Z/run.log`
    passed after changing the reducer to match Linux's longer Chromium shape:
    the rewritten title keeps the original argv boundary non-NUL and places
    `--type=renderer` in the original environment/title area. Linux host proof
    of the same reducer shape passed first with
    `proc-cmdline-live-long-probe ... --type=renderer marker=env-span`.
    Kernel procfs now extends live `/proc/<pid>/cmdline` into the saved
    environment range only for that overwritten-title signature.
  - Next Chromium reducer must re-check role evidence after the procfs fix.
    If renderer roles now appear, treat the earlier `renderer_pids=0` evidence
    as partly a procfs visibility bug and continue at media/MP4 open and
    Wayland buffer delivery. If renderer roles still do not appear, the process
    admission failure is real.
  - Post-procfs Chromium reducer:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T072700Z-procfs-live-cmdline-chromium-no-renderer-no-gpu/`.
    This run used normal Chromium GL policy (`AUTO_GL_FLAGS=0`), GBM and RELA
    preprobes, `chrome_media_fd_trace=1`, and `samples=0` to focus on process
    evidence. The wrapper label was `chromium-video-capture-skipped`, but
    post-evidence wrote `status=PASS reason=capture-skipped`.
  - Result: the procfs fix did not reveal hidden renderers. The process summary
    still had `renderer_seen=0`, `renderer_candidate_seen=0`, `gpu_seen=0`,
    `renderer_pids=0`, `gpu_process_pids=0`, browser + two zygotes + two
    crashpad handlers, and no cmdline truncation. `drm_execbuffer_time_summary`
    had `chrome_count=0`. `chrome_media_fd_trace` produced no file-open lines,
    so this run did not even reach the previous HTML-open point. Chromium's
    stderr only had Wayland binding warnings; there were no GPU init/config/exit
    errors, GL-request errors, INT3 traps, or page faults.
  - Browser wait evidence: the browser stayed alive with render-node fd
    evidence, but sampled threads were mostly in futex/poll. One browser thread
    polled fd 9 (`file:[0]`) and fd 8 (`socket:[175]`), while the zygotes
    waited in `ppoll` on IPC sockets. The next low-noise reducer should trace
    browser-to-zygote/GPU IPC and early child-role admission; EGL, scanout, and
    media decode remain downstream for this run.
  - Latest normal no-forced-GL renderer/IPC/media trace:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T083501Z-normal-no-forced-gl-renderer-ipc-media-trace-capture-skipped/`.
    The top-level harness label was `chromium-video-capture-skipped`, but the
    run was preserved manually without the bulky fs image because the harness
    did not create a history copy. The launcher again proved
    `WAYLAND_CHROMIUM_AUTO_GL_FLAGS=0`, `final_argc=29`, and
    `argv_use_gl_count=0` / `argv_use_angle_count=0`.
  - This run had no kernel fault, no Chromium INT3, no GL-request/GPU-process
    error, sampler completion passed, and capture was intentionally zero-sample
    (`status=DONE result=PASS samples=0`). The failure stayed before FPS:
    `process_renderer=0`, `process_gpu=0`, `process_zygote=2`,
    `renderer_seen=0`, `gpu_seen=0`, no Chrome execbuffers, no EGL entrypoints
    (`egl_initialize=0`, `dlopen=0`, `create_context=0`), no media console
    lines, and `last_currentSrc=missing`.
  - `chrome_media_fd_trace=1` emitted no media/HTML path lines in this run, so
    Chromium did not reach the previous HTML-open point. `chrome_unix_ipc_trace`
    captured only two browser `sendmsg()` calls to zygote-side sockets with no
    SCM rights or credentials. `chrome_poll_summary=1` produced no poll summary
    lines; the next evidence run should use existing `chrome_epoll_trace=1`,
    `chrome_epoll_trace_verbose=1`, `chrome_socket_trace=1`, and focused
    syscall-enter tracing to classify the browser-to-zygote admission stall
    before adding behavior-changing kernel patches.
  - Follow-up epoll/SCM trace:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T084116Z-chrome-epoll-scm-sampler-timeout/`.
    This run again used no forced GL/ANGLE flags and preserved the artifact
    manually without the bulky fs image. The top-level label was
    `chromium-video-sampler-completion-timeout`, but the guest sampler payload
    was configured with `KDE_SMOKE_CHROMIUM_VIDEO_SAMPLES=0` and wrote
    `status=SKIP reason=disabled`; treat the wrapper's lingering
    `status=running` as a harness-status mismatch, not app proof.
  - The trace did make real process-graph progress compared with the previous
    no-renderer/no-GPU run: lifecycle evidence showed one GPU process and one
    utility process exec, browser/zygote/child epoll activity, and repeated
    SCM_RIGHTS handoff for Chromium shared files including
    `v8_context_snapshot.bin`. There were no kernel faults, Chromium INT3
    traps, GPU init/config/exit errors, GL-request errors, or Chrome
    execbuffers.
  - The remaining gap is still before video FPS: no stable `--type=renderer`
    role appeared, no media console lines were emitted, no HTML or MP4
    `chrome_media_fd_trace` open was captured, `last_currentSrc=missing`, and
    scanout capture had zero requested samples. The next reducer should keep
    epoll/SCM tracing but use a nonzero sampler window and fix the zero-sample
    status path so disabled samplers cannot report a completion timeout.
  - The Chromium-video reducer now has an optional RELA prelaunch probe:
    `KDE_SMOKE_CHROMIUM_VIDEO_RELA_PREPROBE=1`. It is off by default. The
    generated guest helper uses `/bin/bash`, completion markers, synced status
    files, and explicit probe result lines rather than shell `$?` evidence.
  - Current launch-only proof in `build-x86_64/kde-plasma-desktop-smoke/`
    passed with `KDE_SMOKE_CHROMIUM_VIDEO_RELA_PREPROBE=1`:
    top-level status was `KDE-PLASMA-DESKTOP-SMOKE-DONE`, the preprobe status
    was `status=DONE result=PASS source=chrome-rela-probe-chain`, and the log
    ended with `CHROME_RELA_PROBE_CHAIN_RESULT checked=35 skipped=5 missing=2
    failed=0` plus `CHROME_RELA_PROBE_CHAIN_PASS`. Post-evidence recorded
    `launcher_crash_seen=0`, `launcher_loader_seen=0`, and
    `status=PASS reason=launch-only`.
  - If a full Chromium-video run now reports an ld.so
    `elf_machine_rela_relative` assertion after a passing RELA preprobe, treat
    static Chrome/dependency bytes and prelaunch file views as clean in that
    boot. The next target is concurrent child exec, file-backed mmap/fault, or
    dynamic-linker mapping behavior under Chromium multiprocess pressure.
  - 2026-07-01 RELA and admission update:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260701T073450Z-chromium-low-noise-census-launcher-loader-regression/`
    is a launcher/loader artifact, not video proof: Chromium exited `status=127`
    after `elf_machine_rela_relative`, with no children. The standalone proof
    `build-x86_64/chrome-rela-probe-proof/20260701T073850Z/run.log` then passed
    eight full `chrome-rela-probe --chrome-chain` iterations
    (`checked=35 skipped=5 missing=2 failed=0` each time), and its generated
    8 GiB fs image was removed while preserving logs. The follow-up desktop
    run
    `build-x86_64/kde-plasma-desktop-smoke-history/20260701T074344Z-chromium-rela-clean-renderer-admission-chrome-crash/`
    passed the in-boot RELA preprobe and had `launcher_loader_seen=0`. It
    reached browser, zygote, GPU-process, NetworkService, and a renderer launch
    packet to the zygote, but still no stable renderer role
    (`exec_renderer=0`, `renderer_pids=0`). The page advanced through
    `PERF-VIDEO start` with `skipCanPlay=1`, but `currentSrc` stayed empty and
    `chrome_media_fd_trace` opened only `perf-video.html`, never the MP4.
    Current target: browser-to-child/NetworkService handoff before MP4 open,
    not DRM/FPS presentation.
  - 2026-06-30 enhanced GBM context-attribute proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T022620-enhanced-gbm-context-attrs/`.
    This launch-only xv6 run passed the RELA preprobe and GBM preprobe while
    recording Chrome-shaped EGL context attempts. Plain ES2/ES3 GBM-window and
    surfaceless contexts passed, no-error and priority-high ES3 contexts
    passed, pbuffer profiles failed only because no pbuffer configs exist, and
    robust-access profiles failed with `EGL_BAD_ATTRIBUTE` or `EGL_BAD_MATCH`.
    The top-level harness still failed `launch-evidence-missing` because the
    launch-only census window did not require full renderer/GPU admission.
  - Matching Linux VM GBM-shape proof:
    `build-x86_64/linux-virgl-gbm-chromium-shape/20260630T063015Z-robust-access-context-attrs/`.
    Linux KVM+virgl produced the same robust-access/no-reset rejection
    (`EGL_BAD_ATTRIBUTE`) and the same robust lose-context/chrome-combo
    rejection (`EGL_BAD_MATCH`) while passing the same plain/no-error/priority
    context classes. Therefore the robust-access rejection is not by itself an
    xv6-specific kernel ABI mismatch and should not be "fixed" by lying about
    EGL robustness support. Continue from Chromium child role admission,
    renderer creation, Wayland buffer delivery, and sustained virtgpu submit
    evidence.
  - Linux comparison harnesses default to the clean baseline now and can still
    force GL/ANGLE by setting `LINUX_CHROMIUM_USE_GL` and
    `LINUX_CHROMIUM_USE_ANGLE`; both scripts emit GPU-process
    `--gpu-preferences` summaries:
    `scripts/gpu/linux-chromium-performance-proof.sh` for KWin/Wayland and
    `scripts/gpu/linux-chromium-visible-youtube-proof.sh` for visible Xorg
    YouTube.
  - Chromium phase-trace evidence after the exec race fix:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T075530Z-exec-phase-chromium-sampler-timeout-preflight-off/`.
    KDE preflight was disabled to remove QtQML preflight noise. Browser exec
    completed in under a second and the GPU-process exec completed in about
    1.3s, so the current failure is not a persistent `execve` hang.
  - That run reached browser, GPU-process, zygote, and utility roles but still
    no renderer role or MP4 open. The local page advanced through
    `PERF-VIDEO start` and `before-play`; `currentSrc` stayed empty and the
    media counters stayed zero. The GPU process repeatedly logged
    `eglCreateContext ES 3.0 failed with EGL_BAD_ATTRIBUTE`, Chrome produced
    no execbuffers, and post-evidence reported
    `status=FAIL reason=chrome-crash-regression`.
  - Harness fix: the Chromium-video sampler guest script now writes
    `status=DONE result=PASS` as soon as the explicit
    `kde_app_launch_probe chromium_sample_only=1 ... status=PASS` line appears,
    before waiting for optional census helpers. This keeps the top-level smoke
    from masking real Chromium failures behind stale
    `phase=probe-returned` status files.
  - Harness disable fix: `KDE_SMOKE_CHROMIUM_VIDEO_SAMPLES=0` now hard-skips
    the generated Chromium-video process sampler as well as frame capture. The
    guest script writes an explicit
    `kde_app_launch_probe chromium_sample_only=0 ... status=SKIP
    reason=zero-samples` result line plus
    `status=DONE result=SKIP reason=zero-samples samples=0 source=harness`,
    so disabled samplers no longer depend on fake shell `$?` status or a later
    timeout fallback.
  - Latest Chromium crash/process proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T085857Z-chrome-crash-process-sampler-proof/`.
    It failed with `status=FAIL reason=chrome-crash-regression`, but not a
    kernel crash: Chromium's network service restarted, multiple children
    logged "15 seconds with no connection", and child exits were clean status-0
    exits. The trace proved `/share/webkit/perf-video.html` opened/fstat'd and
    was read through `pread64`, but no MP4 open or stable renderer role
    appeared. `MSG_CMSG_CLOEXEC` traces matched requested flags, so the next
    reducer target was the combined AF_UNIX stream `SCM_RIGHTS` plus
    `EPOLLONESHOT` rearm shape rather than EGL or media decode.
  - Chromium-shaped AF_UNIX/epoll reducer:
    `user/programs/webkitabitest/webkitabitest.c` now has
    `webkitabitest chromium-ipc`, covering stream `socketpair()`, two
    `SCM_RIGHTS` handoffs, `recvmsg(MSG_DONTWAIT|MSG_CMSG_CLOEXEC)`,
    `FD_CLOEXEC`/`fstat`/read checks on received memfds, one-shot epoll
    disable after delivery, `EPOLL_CTL_MOD` rearm, SCM stream barrier
    semantics, and final nonblocking `EAGAIN`.
  - Linux control proof:
    `build-x86_64/webkit-chromium-ipc-proof/linux-host-control/run.log`
    passed with `PASS: Chromium-shaped stream SCM_RIGHTS epoll oneshot` and
    `webkitabitest: 1 passed, 0 skipped, 0 failed`.
  - xv6 proof:
    `build-x86_64/webkit-chromium-ipc-proof/20260630T091216Z/run.log`
    passed the same `webkitabitest chromium-ipc` reducer in a nographic guest
    using a disposable refreshed rootfs. The status file records
    `status=PASS reason=chromium-ipc`. This closes the exact AF_UNIX
    `SCM_RIGHTS`/`EPOLLONESHOT` mismatch hypothesis for the latest Chromium
    trace; continue at higher-level Chromium child/Mojo connection admission
    and role stabilization before behavior-changing socket or epoll patches.
  - 2026-06-30 seqpacket bootstrap reducer update:
    Linux host probing showed Chromium-like `SOCK_SEQPACKET` bootstrap behavior
    for a 52-byte `SCM_RIGHTS` packet: first `EPOLLIN`, received fd with
    `MSG_CMSG_CLOEXEC`, drained `EAGAIN`, `shutdown(SHUT_WR)` waking as
    `EPOLLIN` plus EOF, and peer close waking as `EPOLLIN|EPOLLHUP` plus EOF.
    `webkitabitest chromium-ipc` now includes
    `PASS: Chromium-shaped seqpacket SCM_RIGHTS bootstrap EOF` beside the
    stream one-shot test. xv6 proof:
    `build-x86_64/webkit-chromium-ipc-proof/20260630T100535Z/run.log`,
    status `status=PASS reason=chromium-ipc saw_stream_pass=1
    saw_seqpacket_pass=1 saw_summary=1`. This closes the raw
    `SOCK_SEQPACKET`/SCM/epoll EOF mismatch hypothesis too.
  - 2026-06-30 seqpacket PASSCRED reducer update:
    Linux host probing showed the Chromium-like credential ping behavior:
    `SO_PASSCRED` on the receiver, an 11-byte `SOCK_SEQPACKET` payload, first
    `EPOLLIN`, a received `SCM_CREDENTIALS` cmsg with the sender pid/uid/gid,
    drained `EAGAIN`, and peer close waking as `EPOLLIN|EPOLLHUP`. The focused
    xv6 proof
    `build-x86_64/webkit-chromium-ipc-proof/20260630T101833Z/run.log`
    passed all three Chromium IPC reducers:
    `PASS: Chromium-shaped stream SCM_RIGHTS epoll oneshot`,
    `PASS: Chromium-shaped seqpacket SCM_RIGHTS bootstrap EOF`,
    `PASS: Chromium-shaped seqpacket PASSCRED bootstrap`, and
    `webkitabitest: 3 passed, 0 skipped, 0 failed`. The status file is
    `build-x86_64/webkit-chromium-ipc-proof/20260630T101833Z/status.txt` with
    `saw_passcred_pass=1`. This closes raw `SO_PASSCRED` /
    `SCM_CREDENTIALS` as the current Chromium no-connection cause.
  - 2026-06-30 seqpacket half-close SCM reducer update:
    Linux host probing showed that a Chromium-like child may still
    `sendmsg()` an `SCM_RIGHTS` packet after local `shutdown(SHUT_RD)` while
    its peer has `shutdown(SHUT_WR)`: Linux returns success, wakes the receiver
    as `EPOLLIN` without `EPOLLHUP`, and delivers the byte plus fd with
    `MSG_CMSG_CLOEXEC`. `webkitabitest chromium-ipc` now includes
    `PASS: Chromium-shaped seqpacket half-close SCM_RIGHTS`. xv6 proof
    `build-x86_64/webkit-chromium-ipc-proof/20260630T120551Z/run.log`
    passed all four focused Chromium IPC reducers with
    `webkitabitest: 4 passed, 0 skipped, 0 failed`. The status file records
    `saw_halfclose_pass=1`. Treat raw fd passing, seqpacket EOF,
    `SO_PASSCRED`, and this half-close SCM shape as closed for the current
    Chromium no-connection failure.
  - Harness evidence update:
    `scripts/gpu/kde-plasma-desktop-smoke.expect` now writes
    `no_connection_child_summary=` in Chromium-video post-evidence. This is a
    host-side parser only; it changes no guest behavior and is active only when
    the Chromium-video reducer produces the logs it consumes. Historical proof
    against
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T085857Z-chrome-crash-process-sampler-proof/`
    found `no_connection_count=5`, `network_restart_count=2`, two
    `network+child+utility` NetworkService attempts, and active IPC/epoll
    before clean exits. That strengthens the current hypothesis: the failing
    boundary is Chromium child/Mojo admission or protocol state, not raw socket
    readiness, fd passing, or one-shot epoll delivery.
  - Fresh zero-sample admission proof after the fast-census gate fix:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T093145Z-chromium-no-connection-summary-gate-fixed/`.
    The run failed as `chromium-video-perf-start-missing`, not as a harness
    sampler/census timeout. Post-evidence records `fast_census_required=0`,
    sampler `result=SKIP reason=zero-samples`, capture `result=PASS samples=0`,
    no renderer/GPU role, no media console lines, no Chrome execbuffers, no
    INT3/fault, and no no-connection child in the shortened wait window.
    Continue with nonzero process evidence for child/Mojo admission before
    changing kernel socket, epoll, EGL, or DRM behavior.
  - 2026-06-30 zero-sample harness/census proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T124134Z-zero-sample-fast-census-harness-proof/`.
    This run verifies the updated disable behavior: `SAMPLES=0` skipped
    framebuffer capture, but the explicitly requested fast census still ran and
    wrote `kde_app_launch_probe chromium_fast_census_only=1 samples=241 ...
    status=PASS`. The wrapper ended as `chromium-video-capture-skipped` under
    strict mode, while post-evidence passed with `reason=capture-skipped`.
    Launcher policy stayed normal/no-forced-GL (`argv_use_gl_count=0`,
    `argv_use_angle_count=0`), no INT3/fault appeared, and the fast census
    recorded browser and zygote processes but no renderer, GPU process, media
    events, or Chrome execbuffers. The current lead remains Chromium
    child/Mojo admission after zygote startup, not raw capture, sampler, GL flag
    policy, or AF_UNIX primitive behavior.
  - Tailtrace child IPC proof:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T095134Z-chromium-tailtrace-child-ipc-capture-timeout/`
    timed out in capture due heavy tracing but preserved useful post-evidence:
    no renderer role, browser/GPU/zygote/utility roles alive, no Chrome faults
    or INT3 traps, no GPU/EGL init errors, no Chrome execbuffers, and six
    no-connection children with active AF_UNIX `SOCK_SEQPACKET` IPC/SCM before
    clean status-0 exits. The exit-tail diagnostic is opt-in behind
    `chrome_syscall_tail_trace=1`; normal boots do not dump tails.
  - New gated payload evidence hook:
    `chrome_unix_ipc_payload_trace=1` is an off-by-default kernel diagnostic
    that prints at most the first 64 payload bytes for Chromium AF_UNIX
    `sendmsg` and `recvmsg` paths as hex. It remains restricted to
    Chromium-like processes and does not change socket behavior. Use it with
    `chrome_unix_ipc_trace=1` to compare Mojo/bootstrap protocol bytes between
    xv6 and Linux now that raw fd passing, seqpacket EOF, one-shot epoll, and
    PASSCRED reducers pass.
  - Latest Linux VM control after the seqpacket proof:
    `build-x86_64/linux-chromium-vm-control/20260630T100838Z-ubuntu-kwin-virtual-http-perf-proof/`
    passed under KWin virtual Wayland with virgl/D3D12. Linux created stable
    Chromium renderer roles by the third process sample and later held one GPU
    process plus six renderers. Video decode progressed
    (`VIDEO_RESULT status=PASS decoded=960 dropped=0 currentTime=16`) even
    though forced `--use-gl=egl` produced Linux-side GL selection errors and a
    later `--use-gl=disabled` GPU process. Therefore the xv6 no-renderer /
    no-connection gap is not explained by Chromium GL fallback alone; keep the
    next reducer at Chromium child/Mojo admission or protocol-payload evidence.
  - Latest Chromium payload trace:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T102732Z-chromium-payload-trace-capture-timeout/`
    failed the wrapper as `chromium-video-capture-status-timeout`, but preserved
    useful logs. GBM preprobe passed, no faults/INT3/GPU init errors appeared,
    and `chrome_unix_ipc_payload_trace=1` recorded 1044 Chromium AF_UNIX
    payload lines. The key result is that a zygote process received renderer
    launch packets containing `--type=renderer` plus seven SCM fds, then forked
    child processes that remained sampled as zygote/no-connection style tasks.
    This moves the next question above raw AF_UNIX bytes/fd passing and into
    zygote child argv-role visibility, post-fork admission, and Mojo state.
  - Latest procfs correction after that payload trace:
    extended `/proc/<pid>/cmdline` proof
    `build-x86_64/proc-cmdline-rewrite-proof/20260630T104156Z/run.log`
    now covers Chromium's long rewritten-title form. Re-run the Chromium
    payload/process reducer before treating those zygote-looking children as
    definitely non-renderers.
  - AF_UNIX `SOCK_SEQPACKET` fd-3 bootstrap fix:
    Linux host probing and the xv6 guest reducer showed a real mismatch in
    `scripts/image/kde-unix-socket-probe.c`: a Chromium-like
    `socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC)`, `dup2(child, 3)`,
    `sendmsg()` of the 8-byte bootstrap payload, `ppoll(fd=3)`, then `read(3)`
    passed on Linux and failed on xv6 before the fix. xv6 reported successful
    send/read lengths but corrupted seqpacket bytes because `sys_sendmsg()`
    queued the payload only in packet metadata while the normal `read()` path
    consumed the socket ring. `kernel/lwip_port/sys_socket.c` now writes
    seqpacket payload bytes into the ring and separately queues packet
    boundaries. Host Linux, xv6 pre-fix, and xv6 post-fix reducer logs proved
    the before/after behavior; post-fix guest output ended with
    `socketpair-sendmsg-ppoll-fd3-seqpacket result=PASS` and
    `kde_unix_socket_probe result=PASS`.
  - Chromium after the seqpacket fix:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T132636Z-seqpacket-fd3-fixed-still-no-renderer/`.
    The run used normal no-forced-GL Chromium policy and process/syscall IPC
    tracing. Chromium got farther than the old fd-3 stall: the browser created
    a sandbox IPC thread and two zygotes, sent the 8-byte seqpacket bootstrap
    payloads successfully, and both zygotes exec'd and initialized until they
    waited in `ppoll(fd=3)`. There were no Chromium faults, INT3 traps, loader
    assertions, GPU init errors, or GL-request errors. The remaining gap is
    browser-side renderer/GPU launch admission: fast census still reported
    `browser_pids=4`, `zygote_pids=2`, `renderer_pids=0`, and
    `gpu_process_pids=0`. Next reducer should use lower-perturbation, longer
    post-zygote browser/thread-state evidence rather than full syscall firehose
    tracing.
  - Lower-perturbation post-fix Chromium evidence:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T133145Z-seqpacket-fix-low-perturbation-browser-read-zygote-ppoll/`.
    This run disabled syscall-entry tracing and kept lifecycle, AF_UNIX IPC,
    and a longer thread-dump window. It preserved 1,488 fast-census lines; the
    sampler status was still `running` when the wrapper timed out, so use the
    post-evidence role counts rather than treating the sampler status as a
    completed PASS. The stable result remained no renderer/GPU:
    `browser_pids=2`, `zygote_pids=2`, `renderer_pids=0`,
    `gpu_process_pids=0`, no faults, no INT3, no GL/GPU errors. Thread dumps
    showed the browser sleeping in a 4-byte read on fd 12
    (`rdi=0xc ... rdx=0x4`) while both zygotes sat in `ppoll(fd=3)` and the
    sandbox IPC thread polled its two fds. The next likely reducer is a
    Chromium-shaped child-to-browser bootstrap/ack test around the zygote
    control socket, not another EGL/GLX or raw seqpacket send test.
  - Chromium-shaped fd3 child-to-browser ack reducer:
    `scripts/image/kde-unix-socket-probe.c` now covers the reduced zygote
    bootstrap/ack shape: browser endpoint sends the 8-byte bootstrap with
    `sendmsg()`, child endpoint is moved to fd 3, the child `ppoll()`/`read()`
    consumes the bootstrap, and the parent waits for a 4-byte ack using plain
    `read()`. Linux host proof passed all stream, seqpacket, seqpacket
    `sendmsg`, and seqpacket `SO_PASSCRED` variants. xv6 proof is archived at
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T134008Z-unix-fd3-child-ack-reducer/kde-unix-socket-probe.log`
    and ends with all `socketpair-fd3-child-ack-* result=PASS` lines plus
    `kde_unix_socket_probe result=PASS`. This closes the reduced fd3 bootstrap
    ack primitive; continue only when full Chromium evidence shows a stricter
    shape.
  - New off-by-default Unix socket read/write evidence hook:
    `chrome_unix_rw_trace=1` logs Chromium-like process reads and writes on
    AF_UNIX socket file descriptors. It is evidence-only and is disabled unless
    passed on the kernel command line.
  - First full Chromium run with the new read/write hook:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T134802Z-chromium-unix-rw-no-zygote-ack/`.
    The browser sent both zygote 8-byte bootstraps
    (`0400000003000000`) and then entered a 4-byte plain `read(fd=12)`.
    No matching child socket write or browser read-exit appeared before the
    shortened observation failed, so the next run added lifecycle/fd evidence.
  - Lifecycle/fd evidence after that run:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T135450Z-chromium-lifecycle-child-loads/`.
    This noisy run used broad `chrome_fd_trace=1` and therefore should not be
    treated as performance evidence. It did prove both zygotes were cloned,
    received socket endpoints via `dup2(..., 3)`, kept fd 3 across `execve`,
    and executed with `--type=zygote`. The browser sent the fd11/fd12
    seqpacket bootstraps and entered `read(fd=12, 4)`. During the observation
    window the children were still loading Chrome resources and libraries; no
    child fd3 read/write ack was observed. The next run should avoid broad
    fd/mmap tracing and use lifecycle plus `chrome_unix_rw_trace=1` over a
    longer post-zygote window.
  - Low-perturbation fd3 post-zygote evidence:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T140304Z-low-noise-zygote-fd3-no-ack/`.
    This run booted with `kasan=0 kmemleak=0 klog=0`, lifecycle tracing,
    AF_UNIX IPC payload tracing, and `chrome_unix_rw_trace=1`, but did not
    enable broad `chrome_fd_trace`. Chromium again launched with no forced
    GL/ANGLE flags. The browser sent both 8-byte seqpacket bootstraps
    (`0400000003000000`), then entered `chrome-unix-rw: op=read-enter ...
    fd=12 requested=4`. Both zygote children exec'd as `--type=zygote`, and no
    child fd3 `read`, child socket `write`, or browser `read-exit` appeared
    before the observation ended. Treat the reduced fd3 primitive as closed,
    and focus next on the real zygote's post-exec startup path before it reaches
    the fd3 read.
  - Low-noise syscall/thread evidence:
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T141931Z-chromium-zygote-loader-slow-before-fd3/`.
    This run booted with `kasan=0 kmemleak=0 klog=0`, launched Chromium with
    bounded child syscall tracing, and failed as `chromium-video-perf-start-missing`.
    It showed the browser entering the 4-byte fd 12 read after both 8-byte
    zygote bootstraps, while both zygote children continued making userspace
    loader/library-search progress rather than blocking in AF_UNIX. Slow trace
    recorded one long `execve`/`openat` interval, and later thread dumps sampled
    both zygotes running in userspace. Do not use this as FPS proof; use it as
    evidence that the full Chromium path is spending too long in real zygote
    startup/admission before video can start.
  - Real Chrome zygote fd3 reducer:
    `scripts/image/chrome-zygote-fd3-probe.c` now execs the imported Chrome
    binary with the observed `--type=zygote` argv, dup2s the child socket to fd
    3, sends the Chromium 8-byte bootstrap `0400000003000000`, and waits for
    the 4-byte ack. Linux host control passed against
    `build-x86_64/host-gui-runtime/wayland-chromium/chrome-linux64/chrome`
    with ack times of about 311 ms for the no-zygote-sandbox variant and 20 ms
    for the plain variant. The xv6 reducer artifact
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T142830Z-chrome-zygote-fd3-reducer-xv6-nogpu/`
    also passed: no-sandbox ack in 6444 ms, plain ack in 1617 ms. Caveat: the
    ad hoc nographic command malformed `QEMU_APPEND`, so KDE still started in
    the background; the reducer result is valid, but rerun with a fixed append
    for a desktop-free artifact if timing precision matters. This closes real
    Chrome fd3 bootstrap/ack semantics; the remaining target is why full
    Chromium under KDE/multiprocess pressure is much slower and still misses
    video start.
  - Harness status fix after that run:
    zero-sample sampler helper logic no longer uses `if grep ...` because xv6
    shell/wait status can make unmatched grep look successful. The generated
    helper now captures grep output and tests whether the captured string is
    non-empty before writing `result=PASS`. A local host sanity check of the
    generated zero-sample/no-census branch returns
    `status=DONE result=SKIP reason=zero-samples samples=0 source=harness`.
    The interrupted verification attempt
    `build-x86_64/kde-plasma-desktop-smoke-history/20260630T140602Z-kde-ready-kwin-exception-before-chromium/`
    staged the fixed helper but failed before Chromium with
    `pid 49 kwin_wayland: exception 1`; keep it as KDE startup noise, not
    Chromium evidence.
  - Harness audit note:
    the Chromium-video GBM preprobe completion marker is `KCVEGLDONE`, not
    `echo KCVEGL:$?`, so this path no longer depends on xv6 `sh`'s fake `$?`
    handling. Status files remain completion-only and explicit PASS lines in
    logs remain the source of truth.

- Failed trace attempt:
  - `build-x86_64/kde-plasma-desktop-smoke-history/20260630T042705Z-egl-trace-kwin-ready-crash/`.
  - This failed before Chromium launch and is KDE startup evidence only.

## Authoritative Active Files

- Linux GUI/DRM active direction:
  `docs/linux-drm-abi-compat-plan.md`.
- KDE/Linux performance baseline:
  `docs/linux-kde-performance-baseline.md`.
- Userland de-patching policy:
  `docs/linux-userland-upstream-depatch-plan.md`.
- Kernel-side ABI backlog:
  `docs/linux-userland-abi-kernel-gap-plan.md`.
- VM/memory ABI backlog:
  `docs/linux-vm-abi-compat-plan.md`.

## Historical Or Supporting Files

- Dated evidence files such as
  `docs/linux-drm-abi-compat-evidence-2026-06-27-30.md` and
  `docs/linux-userland-upstream-depatch-evidence-2026-06-27-28.md` are
  historical evidence archives.
- Inventory TSV/CSV files are generated or semi-generated audit inputs. Update
  them with the owning scanner or audit workflow rather than hand-merging them
  into prose plans.
- `docs/alpine-virgl-desktop-handoff-plan.md` is historical reference for the
  older Alpine/virgl lane, not the current KDE target.

## Document Map

Use this map to decide which document to edit. When a file is marked
historical, keep it as evidence unless a later cleanup explicitly moves it to a
dated archive.

| File | Role | Current owner |
| --- | --- | --- |
| `docs/active-work-plan.md` | Active top-level index and canonical diagnostics snapshot | edit for current cross-lane status, KASAN/KLOG/KMEMLEAK evidence, and document map changes |
| `docs/linux-drm-abi-compat-plan.md` | Active KDE/Chromium/DRM GUI plan | edit for GUI evidence and next reducers; link to this file for detailed diagnostics evidence |
| `docs/linux-kde-performance-baseline.md` | Active Linux-vs-xv6 performance baseline | edit when Linux VM or xv6 performance proof changes |
| `docs/linux-userland-upstream-depatch-plan.md` | Active imported-source cleanup policy | edit for upstream-clean/package policy |
| `docs/linux-userland-abi-kernel-gap-plan.md` | Active kernel ABI backlog | edit for Linux ABI syscall/procfs/VFS/socket gaps |
| `docs/linux-vm-abi-compat-plan.md` | Active VM/memory ABI backlog | edit for mmap/VM/memory semantics exposed by GUI apps |
| `docs/linux-drm-abi-compat-evidence-2026-06-27-30.md` | Historical evidence archive | append only when preserving dated proof |
| `docs/linux-userland-upstream-depatch-evidence-2026-06-27-28.md` | Historical evidence archive | append only for dated upstream-clean proof |
| `docs/linux-drm-abi-audit.md` | Historical/supporting DRM audit | preserve; active GUI status lives in DRM compat plan |
| `docs/linux-drm-abi-impl-steps.md` | Historical/supporting implementation notes | preserve; do not let 2026-06-07 state override current plan |
| `docs/alpine-virgl-desktop-handoff-plan.md` | Historical Alpine/virgl lane | preserve; current target is KDE/Chromium |
| `docs/linux-abi-compat-plan.md` | Historical ABI roadmap | preserve; current kernel backlog supersedes stale wrong-dispatch items |
| `docs/linux-abi-audit.md` and `.csv` | Supporting generated ABI audit | update via audit workflow |
| `docs/linux-abi-semantic-audit.md` and `.csv` | Supporting generated semantic audit | update via audit workflow |
| `docs/linux-gui-file-inventory.md` | Supporting GUI file inventory | update via inventory workflow |
| `docs/linux-gui-kde-inventory.md` | Supporting KDE inventory | update via inventory workflow |
| `docs/linux-user-package-inventory.md` | Supporting package inventory | update via inventory workflow |
| `docs/linux-userland-depatch-*.tsv` | Supporting de-patch phase data | update with de-patch scanner/audit |
| `docs/linux-userland-upstream-refs.tsv` | Supporting upstream ref data | update with source-ref audit |
| `docs/linux-userland-user-program-audit.tsv` | Supporting local program audit | update with userland audit workflow |

2026-06-30 consolidation step: `docs/linux-drm-abi-compat-plan.md` now points
to this file for detailed KASAN/KLOG/KMEMLEAK artifacts instead of duplicating
the diagnostics evidence block. Keep GUI-specific effects in the DRM plan and
cross-lane diagnostic proof here.

## Next Work Queue

1. Re-run the normal no-forced-GL Chromium child-admission reducer with
   `KDE_SMOKE_CHROMIUM_PROCESS_FULL_MAPS=1`. This opt-in harness knob leaves
   normal runs compact, but asks `/bin/kde-app-launch-probe` to preserve full
   maps for Chromium process snapshots and adds one final detailed snapshot
   after fast census. Use it to map repeated zygote RIPs before changing kernel
   behavior.
2. Compare the xv6 normal-launch Chromium run against the passing Linux control
   and the passing isolated `chrome-zygote-fd3-probe` by Chromium child/Mojo
   connection admission, renderer creation, media/file load after `before-src`,
   and Wayland buffer attach/commit evidence.
3. Add or refine low-perturbation xv6-owned probes for full Chromium
   multiprocess admission after zygote ack, renderer lifecycle, and Wayland
   buffer delivery before changing kernel behavior.
4. Re-run Chromium evidence only after the comparison points to a kernel, libc,
   rootfs, or harness fix; exact stream `SCM_RIGHTS`, `EPOLLONESHOT` rearm,
   seqpacket fd3 bootstrap, and real Chrome zygote fd3 ack reducers now pass on
   Linux and xv6.
5. Keep forced `egl-angle/opengles` as an explicit reducer only; it is not the
   clean Linux baseline on this host.
6. Continue KASAN maturation with kmemleak reachability scanning and any
   typed-cache redzones that can be added without changing typed object
   semantics. Large `kvmalloc()`/kernel-VM allocations and `kmm_alloc()` slab
   callers now have exact requested-size metadata and boot proofs.
7. Continue kernel deduplication with the next narrow slice: process-name
   matching or tick conversion helpers, only if emitted log prefixes and field
   names stay stable. The shared exact boolean parsing slice for KLOG/KMEMLEAK
   is committed in kernel submodule commit `12ce331`.

## Kernel Deduplication Queue

Do not start with broad Hyper-V monolith cleanup. The first safe candidates are
smaller and easier to gate:

1. Trace/log/cmdline gating:
   - Candidate files: `kernel/kernel/klog.c`,
     `kernel/kernel/vfs/procfs/inode.c`, `kernel/kernel/virtio_gpu.c`,
     `kernel/kernel/dev/fb/fb_drm_dispatch.c`, `kernel/kernel/mm/vm.c`.
   - 2026-06-30 first dedup slice: `kernel/kernel/cmdline.c` and
     `kernel/kernel/inc/cmdline.h` now provide shared exact boolean helpers.
     `kernel/kernel/klog.c` and `kernel/kernel/mm/kmemleak.c` use them for
     `klog=` and `kmemleak=` gates. KASAN and noisy trace gates were left
     unchanged on purpose. Builds passed for `build-x86_64`,
     `build-x86_64-kasan`, `build-x86_64-kmemleak-off`,
     `build-x86_64-klog-off`, and `build-x86_64-diagnostics-off`.
   - Next possible slices: extract process-name matching and tick conversion
     only if emitted log prefixes and field names stay stable. Leave noisy
     trace gates alone until their current first-character parsing semantics
     are deliberately audited.
   - 2026-06-30 second narrow dedup slice:
     kernel submodule commit
     `16d5fd6 kernel: deduplicate Chromium trace process matching` adds
     `chrome_lifecycle_kernel_trace_process_match()` for the shared
     crashpad-excluding Chromium trace process filter. Socket tracing keeps
     `include_exe=1, include_roles=1`, mmap tracing keeps
     `include_exe=0, include_roles=1`, and VFS tracing keeps
     `include_exe=0, include_roles=0`, preserving the previous trace inclusion
     rules and emitted log names. Parfit audited the slice read-only, the
     staged patch was compiled in a temporary clean kernel worktree with
     `cmake --build /tmp/xv6-kernel-dedup-stage-build --target kernel_all -j2`,
     and `git -C kernel diff --check` plus
     `cmake --build build-x86_64 --target kernel -j2` passed. Older Chromium
     diagnostic hunks remain unstaged in the same files.
   - Gate with representative boot flags and `/proc/kmsg` prefix checks.

2. Procfs virtual file generation:
   - Candidate files: `kernel/kernel/vfs/procfs/inode.c`,
     `kernel/kernel/vfs/procfs/superblock.c`.
   - Start with metadata/blob allocation helpers, not field-shape rewrites.
   - Gate with `/proc/self/{status,stat,statm,maps,smaps,fd,fdinfo,ns,wchan,syscall,stack}`,
     `/proc/kmsg`, and `/proc/kmemleak`.

3. Sanitizer/allocation instrumentation:
   - Candidate files: `kernel/kernel/mm/kasan.c`,
     `kernel/kernel/mm/kmemleak.c`, `kernel/kernel/mm/slab.c`,
     `kernel/kernel/mm/page.c`.
   - First safe shape is a tiny alloc/free instrumentation helper and a shared
     slab free path.
   - Gate with normal and KASAN kernels plus KASAN/kmemleak boot proofs.

4. VirtIO PCI capability discovery:
   - Candidate files: `kernel/kernel/virtio_gpu_scanout.c`,
     `kernel/kernel/virtio_input.c`, `kernel/kernel/virtio_snd.c`.
   - Extract only common cap decode/mapping, leaving device-specific feature
     negotiation local.
   - Gate with GPU, input, and audio initialization.

5. GPU/DRM ioctl and fd lifecycle tables:
   - Candidate files: `kernel/kernel/dev/fb/fb_drm_dispatch.c`,
     `kernel/kernel/dev/fb/fb_device_ioctl.c`,
     `kernel/kernel/dev/fb/fb_fd_sync.c`.
   - Highest risk among the small candidates because node-specific admission is
     ABI/security-sensitive.
   - Gate with ioctl admission and fd dup/close reducers before GUI smoke.

## Latest Plasma Responsiveness Evidence

- Baseline clock-bucket proof:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T223257Z-desktop-interaction-clockbucket-proof-pass/`.
  This passed `desktop-interaction-latency` with direct launch
  `elapsed_ms=6387`, `konsole_wait_ms=5007`, `sys_openat_ms=1459`,
  `sys_clock_gettime_ms=1344`, and
  `sys_clock_gettime_monotonic_calls=348940` /
  `sys_clock_gettime_monotonic_ms=1344`. One realtime call was present; all
  other clock buckets were zero.
- Behavior change:
  `kernel/kernel/vfs/vfs_syscall.c` now defers the `openat()` PATH_MAX scratch
  `name` buffer allocation until the `O_CREAT` create-miss or `O_NOFOLLOW`
  parent-lookup branches that actually need it. Common existing-file `openat`
  semantics are unchanged; `name` is initialized to `NULL` so shared cleanup is
  safe.
- Negative/flaky proof preserved:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T223513Z-desktop-interaction-openat-deferral-kwin-gp-fail/`.
  KWin hit an early #GP before desktop readiness. A same-command rerun passed,
  so treat this as a known intermittent startup exception unless it repeats.
- Passing post-change proof:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T223655Z-desktop-interaction-openat-deferral-pass/`.
  Direct launch improved to `elapsed_ms=5051`, `konsole_wait_ms=3759`,
  `sys_openat_calls=3061`, `sys_openat_ms=1022`, `vm_copyin_ms=910`,
  `vm_copyout_ms=1144`, `vm_vma_validate_ms=665`, and
  `sys_clock_gettime_monotonic_calls=340147` /
  `sys_clock_gettime_monotonic_ms=1126`.
- Current interpretation: the openat allocation deferral is a validated win.
  The next hard launch bottleneck is still monotonic `clock_gettime`; the
  likely fix is a libc/sysroot or VDSO/VVAR-style monotonic fast path rather
  than more in-kernel timespec arithmetic. Keep copyin fast opt-in because the
  previous pactl #GP proof still makes it unsafe as a default.
- 2026-06-30 syscall-dispatch fastgate probe:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T224510Z-desktop-interaction-syscall-trace-fastgate-pass/`.
  This passed, but did not prove a responsiveness win over the prior
  openat-deferral pass: direct launch was `elapsed_ms=5531`,
  `konsole_wait_ms=4460`, `sys_openat_ms=1157`, and
  `sys_clock_gettime_monotonic_ms=1280`. Hover Chromium reported
  `status=no-change`; tray open/close remained healthy at `1558ms`/`1084ms`.
  The attempted x86 syscall dispatcher trace-gate tweak was therefore removed
  instead of kept as a performance fix.
- 2026-06-30 `clockbench` reducer:
  `user/programs/clockbench/clockbench.c` is an xv6-owned probe for raw
  `clock_gettime`, `clock_getres`, and `getpid` syscall loops. The valid
  console artifact is
  `build-x86_64/clockbench-proof/20260630T225553Z-syscall-trace-fastgate-echooff/`;
  it records `clock_gettime` at `5302ns/call`, `clock_getres` at
  `5317ns/call`, and raw `getpid` at `2831ns/call` for 100k iterations.
  The earlier
  `build-x86_64/clockbench-proof/20260630T225207Z-syscall-trace-fastgate/`
  is explicitly marked invalid because the harness matched an echoed command.
  Interpretation: clock and clockres cost are essentially identical, so the
  remaining KDE clock flood is syscall/copyout/return-path dominated, not
  monotonic timespec arithmetic dominated.
- 2026-06-30 Linux host vDSO scale check:
  a temporary host benchmark measured glibc/vDSO
  `clock_gettime(CLOCK_MONOTONIC)` at about `15ns/call`, while forced Linux
  raw syscall `clock_gettime` was about `104ns/call`. This strengthens the
  next target: publish a minimal x86_64 vDSO/VVAR-compatible monotonic clock
  surface so imported glibc KDE can stop issuing hundreds of thousands of
  kernel syscalls during desktop interaction.

## Latest Chromium Evidence

- 2026-06-30 normal no-forced-GL child-admission run:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260630T154523Z-chromium-admission-no-forced-gl-capture-skipped/`.
  The top-level label was `chromium-video-capture-skipped`. Capture was
  intentionally zero-sample and wrote `status=DONE result=PASS samples=0`, so
  the useful result is the process/admission evidence rather than FPS.
- The launcher policy was clean:
  `argv_use_gl_count=0`, `argv_use_angle_count=0`, no implicit
  `--use-gl=egl-angle` / `--use-angle=opengles`, and no Chromium GL-request,
  GPU init/config/exit, INT3, or page-fault regression.
- Chromium did not reach media/render delivery: no stable GPU process, no
  renderer process, no EGL entrypoints, no MP4 open, no media perf events, and
  no Chrome execbuffers. The browser opened `/dev/dri/renderD128` and issued
  four DRM ioctls, while only two zygote-role processes survived.
- Kernel thread dumps showed both zygotes repeatedly running at the same user
  RIP `0x7ffffe7eb9dd` from samples 2-5. The preserved process maps were too
  filtered to symbolize that address. The harness now has the opt-in
  `KDE_SMOKE_CHROMIUM_PROCESS_FULL_MAPS=1` / guest
  `KDE_CHROMIUM_EVIDENCE_FULL_MAPS=1` path, which keeps default runs compact
  but captures full Chromium maps and one final detailed process snapshot after
  fast census.
- Current interpretation: do not chase DRM present/FPS from this artifact yet.
  The next evidence target is browser-to-zygote/Mojo admission and why the
  zygotes spin without producing stable GPU/renderer roles.
- 2026-07-01 full-map renderer-payload run:
  `build-x86_64/kde-plasma-desktop-smoke-history/20260701T080932Z-chromium-fullmaps-renderer-payload-no-connection-crash/`.
  This run failed as
  `KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression`, but
  it materially narrowed the blocker. Chromium used normal no-forced-GL policy,
  the GPU child opened `/dev/dri/renderD128`, fast census saw
  `gpu_process_pids=1`, and the EGL preload trace observed 1292 IPC events, 68
  `SCM_RIGHTS` control messages carrying 123 fds, and credential-bearing
  `CHILD_PING` traffic. The browser sent a 1508-byte `--type=renderer`
  launch packet with seven fds to a zygote; the zygote received the payload and
  fds, cloned a child, and that child continued ChildIOT/Mojo traffic. Raw
  AF_UNIX fd/credential delivery is therefore no longer the whole blocker.
  The remaining gap is post-fork renderer/Mojo admission: post-evidence still
  reported `renderer_pids=0`, `exec_renderer=0`, no MP4 open, no media perf
  lines, and three "15 seconds with no connection" children including
  NetworkService. Because Chromium zygote children may not exec a new image,
  treat `exec_renderer=0` as an incomplete role signal, not by itself proof
  that no renderer was requested.

## Next Chromium Reducer

Use a lower-perturbation child-admission run before a behavior-changing kernel
patch. It should keep the normal no-forced-GL policy and zero framebuffer
samples, but avoid broad EGL tracing unless the specific question is EGL. The
next evidence target is the post-fork zygote child after it receives a
`--type=renderer` payload: argv/proctitle rewrite, initial-client-fd setup,
Mojo channel readiness, and media URL handoff.

```sh
KDE_SMOKE_REDUCER=chromium-video \
KDE_SMOKE_KDE_PREFLIGHT=0 \
KDE_SMOKE_TOLERATE_READY_USER_EXCEPTIONS=1 \
KDE_SMOKE_CHROMIUM_MULTIPROCESS=1 \
KDE_SMOKE_CHROMIUM_AUTO_GL_FLAGS=0 \
KDE_SMOKE_CHROMIUM_PROCESS_FULL_MAPS=1 \
KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_PREPROBE=0 \
KDE_SMOKE_CHROMIUM_VIDEO_RELA_PREPROBE=1 \
KDE_SMOKE_CHROMIUM_VIDEO_SAMPLES=0 \
KDE_SMOKE_CHROMIUM_VIDEO_SAMPLER_MS=18000 \
KDE_SMOKE_CHROMIUM_VIDEO_FAST_CENSUS_MS=22000 \
KDE_SMOKE_CHROMIUM_VIDEO_FAST_CENSUS_INTERVAL_MS=50 \
KDE_CHROMIUM_URL='file:///share/webkit/perf-video.html?asset=perf-1280x800-60fps.mp4&ms=8000&startupMs=4000&postFailObserveMs=4000&hud=1&skipCanPlay=1' \
QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto vfs_backend_read_revive=1 poll_notify_full_wait=1 chrome_lifecycle_trace=1 chrome_media_fd_trace=1 chrome_unix_ipc_trace=1 chrome_unix_ipc_payload_trace=1 chrome_unix_rw_trace=1 chrome_syscall_tail_trace=1 chrome_syscall_trace_child_processes=1 chrome_poll_summary=1 chrome_thread_dump=1 chrome_thread_dump_samples=6 chrome_thread_dump_interval_ms=2000 kasan=0 kmemleak=0 klog=0' \
timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Interpretation:

- No renderer payload or child clone: chase browser-to-zygote launch request.
- Renderer payload and child clone but no stable renderer role: inspect
  post-fork argv/proctitle rewrite, initial-client-fd, and Mojo readiness.
- Stable renderer/GPU appears but no media events: split the next reducer
  between MP4 open/read, Chromium media pipeline startup, and renderer IPC.
- Renderer/GPU stable but low or missing Wayland primary commits/callbacks:
  chase Wayland surface or present pacing.
- Wayland commits/callbacks advance but Chrome execbuffers/fences remain near
  one: chase DRM execbuffer/fence admission or per-process render-node behavior.

## Verification Gates

- Static:
  - `git diff --check`
  - `git -C kernel diff --check`
  - `expect -c 'set f [open "scripts/gpu/kde-plasma-desktop-smoke.expect" r]; set s [read $f]; close $f; if {![info complete $s]} {puts incomplete; exit 1}; puts complete'`

- Builds:
  - `cmake --build build-x86_64 --target kernel -j2`
  - `cmake --build build-x86_64-kasan --target kernel -j2` when sanitizer code changes.
  - `cmake --build build-x86_64 --target rootfs-refresh -j2` after rootfs/sysroot/staging changes.

- Runtime:
  - Focused reducer for the changed ABI surface.
  - `timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect` before claiming KDE desktop stability.
  - Chromium-video reducer before claiming FPS improvement.

## Commit Policy

Commit after major, verified steps only when the dirty scope is understood.
Because this repo contains nested submodules and many concurrent edits, do not
commit unrelated changes together and do not push without explicit user approval.
