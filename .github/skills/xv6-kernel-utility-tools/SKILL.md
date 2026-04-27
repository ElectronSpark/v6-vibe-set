---
name: xv6-kernel-utility-tools
description: 'Use when: working on xv6-os bits.c, bitmap/bit helpers, string/memory primitives, compiler/cache macros, types, errno, kobject refcounts, accounting, kstats, resource metrics, or power hooks.'
argument-hint: 'Describe the utility helper or call-site issue'
---

# xv6 Kernel Utility Tools

## When to Use

- A shared helper in bits, strings, compiler/cache macros, kobject lifetime, accounting, or power hooks is involved.
- Many modules fail after changing a low-level helper.
- You need to decide whether a helper belongs in generic utilities or a subsystem.

## Source Map

- Bits: `kernel/kernel/bits.c`, `kernel/kernel/inc/bits.h`.
- Strings/memory: `string.c`, `kernel/kernel/inc/string.h`.
- Kobjects: `kobject.c`, `kernel/kernel/inc/kobject.h`.
- Accounting/resources: `accounting.c`, `kernel/kernel/inc/accounting.h`, `kstats.h`, `resource.h`.
- Low-level headers: `compiler.h`, `cache.h`, `types.h`, `errno.h`, `defs.h`.
- Power: `power.c`.

## Workflow

1. Check all major call sites before changing helper semantics.
2. Keep generic helpers freestanding and safe for early boot or kernel-only contexts where applicable.
3. For kobjects, verify refcount acquire/release symmetry and destruction context.
4. For accounting, confirm units, overflow behavior, and user-visible ABI formatting.
5. Do not hide subsystem policy in generic helpers.

## Pitfalls

- A tiny helper change can affect allocators, schedulers, devices, and VFS at once.
- String helpers run without libc safety nets; validate bounds at callers.
- Kobject helpers manage lifetime, not locking.
