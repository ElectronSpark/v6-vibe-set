---
name: xv6-kernel-memory-management
description: 'Use when: debugging xv6-os physical pages, kalloc, slab, folios, VMAs, mmap/brk, page faults, rmap, page cache, buffer heads, reclaim, shrinkers, watermarks, or OOM.'
---

# xv6 Kernel Memory Management

## When to Use

- Page faults, `mmap`, `brk`, copyin/copyout, page cache, reclaim, or OOM behavior is wrong.
- You are changing allocators, folios, VMAs, reverse mapping, or file-backed memory.
- Memory pressure interacts with VFS, ext4/xv6fs, lwIP pbufs, or page cache pins.

## Source Map

- Physical pages: `early_allocator.c`, `page.c`, `kalloc.c`.
- Slab/folios: `slab.c`, `folio.c`.
- VM and syscalls: `vm.c`, `sysmm.c`, `kernel/kernel/inc/mm/vm*.h`.
- Rmap/cache/buffers: `rmap.c`, `pcache.c`, `buffer.c`.
- Reclaim/OOM: `shrinker.c`, `mm_watermark.c`, `oom_kill.c`.
- Arch MMU: `kernel/arch/*/mm/` and page-table headers.

## Workflow

1. Classify the object: physical page, slab object, folio, VMA, page-cache entry, or buffer head.
2. Trace ownership and reference counts before freeing or reusing memory.
3. For faults, follow the arch trap into generic VM and then into VFS/page-cache paths if file-backed.
4. For reclaim, check pinning, shrinker callbacks, RCU delay, and page-cache dirty/writeback state.
5. For architecture differences, use current source; xv6-tmp RISC-V PTE notes are reference material, not x86_64 authority.

## User Copy Path Facts

- `vm_copyin()`/`vm_copyout()` in `kernel/kernel/mm/vm.c` use a per-page
  software walk (`vm_find_area` + `vma_validate` + `walk` + direct-map
  `memmove`) because the kernel cannot dereference user VAs under the kernel
  CR3. A CR3-switching fast path exists in-source but is intentionally
  disabled (`0 &&`) after a documented #DF; read its long comment before
  re-enabling anything.
- `vm_copyout_present_fast=1` / `vm_copyin_present_fast=1` are OPT-IN ONLY.
  Enabling the copyout fast path by default has caused user-visible pointer
  corruption in GUI processes (evidence chain in `docs/active-work-plan.md`).
  Do not re-enable by default without a lifetime/concurrency proof.
- The `vm_copy_present_skip` kstats counters show whether copies are
  hitting already-present PTEs; when the skip ratio is high, remaining copy
  cost is dominated by per-call rlock/lookup overhead plus the per-syscall
  CR3/TLB tax documented in `xv6-kernel-traps-syscalls`.
- `vm_cpu_online()` (`kernel/arch/x86_64/mm/vm.c`) rewrites the per-CPU
  TRAPFRAME-slot PTE on every return to user. This is what forces
  `noflush=0` CR3 writes each syscall. Making this precise (targeted
  `invlpg`, or skip when the same thread resumes on the same CPU) is the
  prerequisite for any `noflush`/PCID return-path optimization.

## Pitfalls

- Page cache pins can make reclaim look broken while ownership is correct.
- Rmap/VMA lock ordering can deadlock with fault handling if changed casually.
- Slab, RCU, and shrinker interactions can delay frees well after logical release.
