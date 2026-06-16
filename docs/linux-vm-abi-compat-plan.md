# Linux VM / Memory ABI Compatibility Plan

Last updated: 2026-06-16.

This plan tracks Linux VM behavior that is visible to large Linux GUI programs,
especially Chromium. It is separate from the DRM/GPU plan so VM lock,
user-copy, page-fault, mmap, madvise, rseq, and VMA-lifetime work can move
without burying the graphics ABI state.

## Current Focus

- [ ] Reduce Chromium input/new-tab stalls caused by process-address-space VM
      locking differences from Linux.
- [ ] Preserve Linux-visible syscall and fault semantics while improving VM
      concurrency. Do not paper over the issue with Chromium launcher flags or
      app-specific workarounds.
- [ ] Keep kernel-only iteration unless a trace proves rootfs, imported host
      binaries, or userland staging is involved.

## Linux Reference Shape

- Linux tracks userspace ranges as VMAs inside an `mm_struct`, with VMAs stored
      in a maple tree.
- Linux has a process-wide `mmap_lock` plus finer-grained VMA locks, rmap
      locks, and page-table locks. Most VMA metadata writes require
      `mmap_write_lock()`, and many readers can use `mmap_read_lock()` or a
      VMA read lock under RCU.
- This layering lets unrelated VMA readers and writers overlap more often than
      a single address-space rwsem allows.
- Linux rseq can avoid unconditional work in optimized modes; legacy behavior
      still updates/checks state around context switches and signal delivery.

References:

- Linux process address locking:
      <https://docs.kernel.org/mm/process_addrs.html>
- Linux rseq behavior:
      <https://docs.kernel.org/userspace-api/rseq.html>

## xv6 Current Shape

- [ ] One `vm->rw_lock` protects the userspace VMA maple tree.
- [ ] User `vm->rw_lock` now uses `RWLOCK_PRIO_WRITE` so queued VM writers
      cannot be starved by short VM readers.
- [ ] Kernel VM still uses its spinlock path; its rwsem priority flag is not on
      the user hot path.
- [ ] `vm_copyin()`, `vm_copyout()`, `vm_copyinstr()`, procfs/user-copy helpers,
      page-fault validation, rseq return handling, and mmap/madvise/munmap all
      meet at the process-wide VM lock.
- [ ] No per-VMA read/write lock or RCU VMA lookup path currently exists.

Key local files:

- `kernel/kernel/inc/mm/vm_types.h`
- `kernel/kernel/mm/vm.c`
- `kernel/kernel/lock/rwsem.c`
- `kernel/kernel/proc/sys_misc.c`
- `kernel/kernel/mm/sysmm.c`

## Evidence

- [x] Old plain Chromium delayed-freeze trace reached:
      `rwsem_acquire_write -> vm_wlock -> vm_madvise -> sys_madvise`.
- [x] User VM rwsem priority changed from read-priority to writer-priority.
- [x] `cmake --build build-x86_64 --target kernel -j2` passed after the
      writer-priority change.
- [x] `chromium-newtab-serial-wprio-20260616a` launched plain
      `/bin/wayland-chromium`, injected Ctrl+T through QEMU monitor, returned
      both `PRE_NEWTAB_ALIVE` and `POST_NEWTAB_ALIVE`, and did not reproduce
      the old `vm_wlock -> vm_madvise` stack.
- [x] Added opt-in VM rwsem caller diagnostics:
      `vm_rwsem_trace=1`, `vm_rwsem_trace_ms=<ms>`,
      `vm_rwsem_trace_limit=<n>`.
- [x] `chromium-newtab-vmrwsem-caller-20260616a` showed multi-second
      read-side waits in `rseq_user_return()` / `vm_copyin()` while VM writers
      from `sys_mprotect()` held the address-space writer lock for about
      3.8-4.6 seconds.
- [x] Added an rseq same-CPU/no-event fast skip so ordinary returns can avoid
      user rseq-area copyin/copyout when no signal, preempt, or migration event
      exists.
- [x] `chromium-newtab-rseqfast-vmrwsem-caller-20260616a` confirmed the
      remaining worst waits still lined up with rseq return work and
      intermittent `sys_mprotect()` writer holds.
- [x] Added opt-in `vm_mprotect_trace=1` stage timing. In
      `chromium-newtab-mprotectstage-20260616a`, the only slow mprotect above
      500 ms spent 539 ms waiting for the VM lock and 0 ms in the PTE sweep,
      with no present pages in the target range.
- [ ] Validate the refined rseq event model: `usertrapret()` should not report
      `RSEQ_EVENT_PREEMPT` on every return, only after a real reschedule path
      or when another event such as signal/migration applies.
- [x] `chromium-newtab-rseqevent-20260616a` confirmed the dominant slow path:
      a 16 KiB `mprotect(PROT_READ|PROT_WRITE)` in `Chrome_InProcRe` held the
      VM writer lock for 4327 ms. `vm_mprotect_trace` attributed 4304 ms to
      metadata work, 0 ms to PTE scanning, 0 ms to TLB flush, one split, four
      pages, and zero present pages.
- [ ] The next concrete gap is VMA split/update shape. Current `vma_split()`
      rewrites maple ranges through `__mt_update_vma()`, which erases and
      re-stores the old VMA range before inserting the new split VMA. For
      Chromium's large reserved VMAs, a tiny `mprotect()` can therefore rewrite
      a large maple-tree range under the process-wide VM writer lock.
- [x] Added an experimental keep-right split path for tiny left-side
      `mprotect()` fragments. It preserves the large right-hand VMA in the
      maple tree and stores only the small left VMA, avoiding a full tail-range
      rewrite when no external metadata is keyed by `vma->start`.
- [x] `chromium-newtab-keepright-trace-20260616a` completed and reduced the
      worst observed metadata holds from multi-second to sub-second rows, but
      the slow rows still reported `fast_splits=0`.
- [x] `chromium-newtab-keepright-skipreason-20260616a` completed and showed
      every slow `vm-mprotect-trace` row skipped the keep-right path with
      `fast_skip=2`, meaning the VMA had anon-rmap state. The slow rows were
      16-32 KiB `mprotect()` calls with zero present pages; one NetworkService
      row spent 1115 ms waiting for the VM lock behind another writer.
- [ ] The new concrete gap is anon-vma/rmap keying. `anon_vma->vma_tree` is
      keyed by `vma->start`, `anon_vma_unlink()` deletes by that key, and the
      bintree insert helper rejects duplicate keys despite the rmap comment
      claiming duplicates are allowed. Moving a VMA's start therefore needs a
      safe AVC rekey or a different split representation before the keep-right
      optimization can cover Chromium's anonymous VMAs.
- [x] Rmap AVC keys were changed from mutable `vma->start` to a stable
      per-AVC key, unlink now removes the exact AVC node, and the keep-right
      split path now clones anon-rmap links for the new left VMA.
- [x] `chromium-newtab-anon-keepright-20260616a` completed and showed the
      anonymous fast split working: the only `mprotect()` row above 500 ms had
      `fast_splits=1`, `fast_skip=0`, `meta_ms=0`; remaining cost was lock
      wait.
- [x] Added opt-in VM rwsem read-batch hold tracing. In
      `chromium-newtab-readhold-20260616a`, all five slow `mprotect()` rows
      used `fast_splits=1` with `meta_ms` 0-1 ms, while 39 read-batch releases
      exceeded 200 ms. Most were page-fault-side VM read holds from the x86
      trap handler; the worst held the VM read lock for 817 ms.
- [ ] The new concrete gap is page-fault batch size under the process-wide VM
      read lock. x86 read faults were validating up to 2048 pages, an 8 MiB
      fault-around window, and write faults up to 128 pages. That turns a
      single page fault into a long read-side VM lock hold; when a VM writer
      queues, short readers such as rseq and `clock_gettime()` wait behind the
      writer-priority handoff.
- [x] Reduced x86 user fault-around windows to 32 read pages and 8 write
      pages. `chromium-newtab-faultaround32-20260616a` reduced read-batch
      VM lock holds above 200 ms from 39 rows with an 817 ms worst case to one
      253 ms row. The trace still showed a writer-priority convoy: ordinary
      faults were calling `vm_try_growstack()` before proving the fault address
      was stack-adjacent, briefly queuing VM writers for non-stack faults.
- [x] `vm_try_growstack()` now rejects non-stack addresses before taking
      `vm_rw_lock` for write. `chromium-newtab-growprecheck-20260616a`
      completed with normal post-Ctrl+T process states by the 10 second mark,
      one `mprotect()` trace row with `fast_splits=1` and `meta_ms=1`, and
      only two VM rwsem waits above 200 ms.
- [x] The next concrete gap was remaining file-fault batch size under the
      process-wide VM read lock. `chromium-newtab-growprecheck-20260616a`
      still had one page-fault-side read batch at 510 ms, which queued a
      390 ms NetworkService `mprotect()` and one 404 ms reader wait.
      Until xv6 has finer VMA/page locking, file-backed faults must not hold
      `vm_rw_lock` across blocking filesystem I/O.
- [x] `chromium-newtab-faultaround8-growprecheck-20260616a` showed that
      simply shrinking fault-around is not enough. It completed, but blocked
      snapshots showed a renderer page fault stuck in
      `vma_validate() -> pcache_read_folio() -> ext4fs_file_readv()` while
      many rseq/usercopy readers queued on `vm_rw_lock`. The underlying gap is
      blocking file I/O under the process-wide VM read lock.
- [x] Added a narrow unlocked read-fault path for full-page file-backed read
      faults: pin the VMA's file/page-cache page, drop `vm_rw_lock` while
      doing the blocking read, then reacquire and install the PTE only if the
      VMA still maps the same file offset. EOF and partial-tail page faults
      still fall back to the existing filesystem fault path for zero-fill
      semantics.
- [x] `chromium-newtab-unlocked-filefault-20260616a` completed with both
      `PRE_NEWTAB_ALIVE` and `POST_NEWTAB_ALIVE`. A blocked snapshot still
      showed `vm_file_read_fault_unlocked() -> pcache_read_folio() ->
      ext4fs_file_readv()`, but no `vm-rwsem-trace` wait rows were emitted:
      the blocking file I/O was no longer pinning `vm_rw_lock`. The only slow
      VM hold above the trace threshold was an early launch `thread_clone()`
      reader/writer hold around 465-487 ms, with no queued readers or writers.
- [x] Fresh tail-guard validation
      `chromium-newtab-unlocked-filefault-tailguard-20260616a` used the
      current kernel with `vm_rwsem_trace=1` and `vm_mprotect_trace=1`,
      preserved `PRE_NEWTAB_ALIVE` and `POST_NEWTAB_ALIVE`, emitted zero
      `vm-rwsem` wait rows, zero slow `mprotect()` rows, and only one early
      `thread_clone() -> vm_copy()` VM hold at 200-222 ms with no queued
      readers or writers.
- [x] Stack-growth probing shape. `vm_try_growstack()`
      must reject non-stack addresses before taking `vm_rw_lock` for write, so
      normal Chromium file/heap/anon faults do not block rseq/usercopy readers
      behind an unnecessary writer handoff.
- [ ] The next concrete gap is VM lifetime/copy work in process creation.
      Once page faults stop holding the address-space lock across disk I/O,
      Chromium launch/new-tab traces expose `thread_clone() -> vm_copy()`
      holding `vm_rw_lock` for hundreds of milliseconds. Compare this with
      Linux's fork/clone COW and per-mm locking shape before changing policy.
- [x] Added opt-in `vm_copy_trace=1` stage/count diagnostics. Baseline
      `chromium-newtab-vmcopytrace-20260616a` showed `vm_copy()` time was
      dominated by sparse VMA page probing: Chromium children scanned
      12.58-12.72 million 4 KiB slots while only 651-21,648 4 KiB PTEs were
      present. A new-tab action also emitted a 438 ms `vm_rw_lock` read wait
      behind a `ThreadPoolForeg` mmap-side holder.
- [x] `vm_copy()` now copies COW mappings through an architecture page-table
      present-leaf visitor instead of linearly walking every page in every VMA.
      The visitor exposes mutable parent PTE pointers so the existing fork COW
      behavior still write-protects parent PTEs, bumps page refs, and updates
      anon rmap state.
- [x] `chromium-newtab-vmcopy-sparse-20260616a` completed with both
      `PRE_NEWTAB_ALIVE` and `POST_NEWTAB_ALIVE`. `vm_copy()` traces now show
      `pte_walks` equal to resident leaves instead of VMA span: the main
      Chromium copy dropped from 101 ms / 12,662,709 PTE probes to
      27 ms / 15,421 present leaves, and the larger ThreadPool copy dropped
      from 143 ms / 12,722,890 probes to 55 ms / 21,569 present leaves. The
      rerun emitted zero `vm-rwsem-trace` wait rows.
- [ ] Continue digging from the remaining Chromium responsiveness delay after
      sparse `vm_copy()` is fixed. The next traces should look for file-cache
      I/O latency outside `vm_rw_lock`, futex/scheduler delays, epoll/poll
      readiness, and any residual VM lock holds below the previous 200 ms
      threshold.
- [x] Added opt-in unlocked file-fault stage tracing:
      `vm_file_fault_trace=1`, `vm_file_fault_trace_ms=<ms>`, and
      `vm_file_fault_trace_limit=<n>`. It reports lookup/get/read/relock/
      install timing plus inode, file offset, and page-cache state.
- [x] `chromium-newtab-filefault-nextblocker-20260616a` completed with both
      alive markers and showed the next stall source after sparse `vm_copy()`:
      cold file-backed mmap faults outside `vm_rw_lock`. The worst rows were
      539 ms and 511 ms, both entirely in `read_ms`, mostly inode 4024
      (`isize=276917464`, the large Chromium mapping). No slow `mprotect()`
      rows appeared, and the slow blocked snapshot no longer implicated a VM
      writer convoy.
- [x] Locally tried, then did not retain, a small unlocked mmap readahead
      window through
      `pcache_readahead()` in `vm_file_read_fault_unlocked()`. The diagnostic
      run `chromium-newtab-filefault-readahead-20260616a` completed with both
      alive markers, lowered the worst file-fault row to 356 ms, and removed
      the old D-state file-fault snapshot, but still hit the 256-row trace cap.
      The reason: ext4's generic `submit_readahead` path ignores current
      order-0 disk pcache pages, so many adjacent 4 KiB pages still fault cold.
- [x] A quick experiment allowing ext4 `submit_readahead` to merge order-0
      pages was backed out after `chromium-newtab-filefault-ext4ra-20260616a`
      caused early D-Bus crashes in dynamic-linker code. Do not re-enable that
      shortcut without a reducer that proves BIO vector offsets, completion
      ownership, and page-cache data contents for multiple order-0 pages.
- [x] Re-enabled order-0 ext4 mmap readahead conservatively: only complete
      file pages are submitted, BIO success is required before marking pages
      uptodate, and merged runs verify each page's physical block continuity
      instead of trusting a first/last-block shortcut. The temp-image reducer
      `/tmp/xv6-mmap-ra-proof/mmap_ra.py` booted the current kernel, mmap-read
      seven 256 KiB regions of Chromium inode 4024, and matched host-image
      SHA-256 hashes in `/tmp/xv6-mmap-ra-proof/run.log`.
- [x] Follow-up gradient regression: an ungated synchronous mmap readahead hook
      made Weston miss the wrapper's 4 second `/tmp/wayland-0.lock` startup
      timeout, leaving the QEMU window at the gradient background. The retained
      hook now advances `pc->ra_pos` and defaults to files at least 64 MiB
      (`vm_file_fault_ra_min_bytes=` can tune it), so normal Weston/shared
      library startup avoids the synchronous 4 MiB readahead path.
- [x] Chromium-visible benefit from safe filemap readahead: after freeing
      generated proof `*.fs.img` copies from
      `build-x86_64/chromium-normal-desktop-proof`, the gated current-kernel
      Weston probe `weston-ready-ra-gated-20260616a` reached desktop icons.
      The comparable Ctrl+T run
      `chromium-newtab-filefault-ra-gated-20260616b` used
      `vm_file_fault_trace=1 vm_file_fault_trace_ms=25` and reduced Chromium
      inode 4024 slow file-fault rows from 33 rows / 2966 ms sum / 539 ms max
      in `chromium-newtab-filefault-nextblocker-20260616a` to 9 rows / 327 ms
      sum / 45 ms max. `ra_ms` stayed 0 in sampled rows because the remaining
      visible rows are normal fills/cached frontier effects, not long
      synchronous readahead waits.
- [ ] Next concrete gap: decide whether adjacent PTE install after VMA
      revalidation is warranted, using the remaining post-Ctrl+T rows and a
      reducer that covers unaligned ELF mappings and partial-tail pages.

## Gap Checklist

- [x] Add opt-in VM rwsem wait/hold diagnostics that identify:
      operation label, pid/tgid/thread name, reader or writer mode, wait time,
      hold time, current writer holder, reader count, and queue sizes.
- [ ] Label VM lock sites by semantic operation:
      `copyin`, `copyout`, `copyinstr`, `pagefault`, `mmap`, `munmap`,
      `mprotect`, `mremap`, `madvise`, `brk`, `rseq`, procfs/fdtable usercopy,
      and driver/usercopy paths.
- [x] Re-run plain Chromium new-tab probe with only the VM file-fault
      diagnostic enabled. `chromium-newtab-filefault-ra-gated-20260616b`
      completed with desktop startup, Chrome launch, Ctrl+T injection, and
      improved inode 4024 file-fault timing; QEMU monitor screendumps returned
      `Error: no surface`, so this is serial/trace evidence only.
- [ ] Re-run long-idle probe with only the VM
      diagnostic enabled. Avoid broad syscall tracing unless the VM trace is
      empty.
- [ ] Build a small reducer once the trace names the contended operation:
      examples include many short `clock_gettime()`/rseq returns racing with
      `madvise()`, thread-pool `mmap()` churn racing with user copies, or
      `munmap()` of profile/cache mappings racing with input event processing.
- [ ] Add a focused VMA split reducer and/or split-stage trace:
      large anonymous/file-backed reservation, repeated 16 KiB `mprotect()`
      inside it, concurrent `clock_gettime()`/rseq/poll user-copy readers.
- [ ] Decide the first Linux-shaped concurrency fix after rseq validation:
      shorter VM writer critical sections, split user-copy from full VMA-tree
      lock where safe, per-VMA read locks for page-fault/usercopy fast paths,
      RCU lookup plus retry, or a fairer VM rwsem handoff policy that avoids
      reader convoys behind a queued writer without restoring writer starvation.
- [ ] Optimize `vma_split()` / maple updates so splitting one small subrange
      does not erase and re-store the entire old VMA range. Preserve rollback
      safety for partial `mtree_store_range()` failures before replacing the
      current conservative update path.
- [ ] Make anon-vma AVC tracking safe for start-moving splits, or avoid
      start-moving VMAs with anon-rmap state by introducing an alternate VMA
      metadata update that still avoids rewriting huge unaffected ranges.
- [x] Reduce and validate x86 user fault-around windows so page-fault readers
      do not monopolize the process VM lock during Chromium input/new-tab
      bursts.
- [x] Validate the stack-growth precheck with the same Chromium Ctrl+T probe
      and confirm `vm-rwsem-trace` no longer reports non-stack page-fault
      writers from `vm_try_growstack()` / x86 page-fault handling.
- [x] Validate an 8-page read / 4-page write x86 fault-around window with the
      Chromium Ctrl+T probe and compare read-hold rows against the 32/8-page
      run.
- [x] Validate the unlocked file-backed read-fault helper with the Chromium
      Ctrl+T probe. Success means no long blocked stack in
      `vma_validate() -> pcache_read_folio()` and no broad rseq/usercopy pileup
      behind a file-fault reader.
- [ ] Compare and reduce clone/fork VM-copy behavior:
      parent VM read-lock hold time, child VM write-lock hold time, VMA copy
      cost, rmap/COW cost, and whether unrelated user-copy/rseq readers can
      proceed during process creation the way Linux permits through finer
      locking and COW-oriented fork setup.
- [x] Avoid sparse VMA hole scanning during `vm_copy()` by visiting present
      page-table leaves in range and preserving the existing COW/rmap/refcount
      rules.
- [x] Build a focused ext4/page-cache mmap readahead reducer before touching
      the order-0 BIO path again: map the known Chromium binary, fault
      representative neighboring full-page regions, and verify bytes against
      host-image SHA-256 hashes.
- [ ] Extend the mmap readahead reducer for unaligned ELF-style mappings,
      partial-tail pages, and Linux filemap fault-around comparison before
      attempting adjacent PTE installation.
- [ ] Validate against:
      focused VM reducer, plain Chromium Ctrl+T latency probe, long-idle
      Chromium probe, AF_UNIX fd-passing smoke, and mandatory video gate if a
      desktop-visible path changes.

## Guardrails

- Do not restore read-priority VM locking; that reopens writer starvation.
- Do not infer roles from fixed PIDs/TIDs. Use same-run argv, thread name,
      exec path, socket/fd graph, and lifecycle evidence.
- Do not chase DNS, desktop launchers, or Chromium packaging while the trace
      continues to identify VM lock contention.
- Keep diagnostics opt-in and bounded by counters/thresholds so normal boot and
      GUI runs stay quiet.
