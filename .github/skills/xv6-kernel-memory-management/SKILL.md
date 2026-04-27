---
name: xv6-kernel-memory-management
description: 'Use when: debugging xv6-os physical pages, kalloc, slab, folios, VMAs, mmap/brk, page faults, rmap, page cache, buffer heads, reclaim, shrinkers, watermarks, or OOM.'
argument-hint: 'Describe the memory symptom or fault path'
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

## Pitfalls

- Page cache pins can make reclaim look broken while ownership is correct.
- Rmap/VMA lock ordering can deadlock with fault handling if changed casually.
- Slab, RCU, and shrinker interactions can delay frees well after logical release.
