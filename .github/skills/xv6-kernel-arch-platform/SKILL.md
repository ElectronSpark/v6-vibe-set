---
name: xv6-kernel-arch-platform
description: 'Use when: working on xv6-os architecture code, x86_64 boot, RISC-V boot, AP startup, MMU page tables, LAPIC/IOAPIC/PLIC, SMP, IPI, trapframe ABI, platform discovery, or arch debug hooks.'
argument-hint: 'Describe the arch/platform symptom or file path'
---

# xv6 Kernel Architecture And Platform

## When to Use

- Boot, AP startup, early entry, or platform discovery fails.
- You are changing page table formats, TLB flushes, trapframes, context switch state, or signal trampoline ABI.
- Timer/interrupt behavior depends on LAPIC, IOAPIC, PLIC, or architecture-specific tick code.

## Source Map

- x86_64: `kernel/arch/x86_64/entry.S`, `start.c`, `ap_trampoline.S`, `platform_x86.c`, `irq/`, `mm/`, `timer/`.
- RISC-V: `kernel/arch/riscv/entry.S`, `start.c`, `platform_riscv.c`, `boot/`, `irq/`, `mm/`, `timer/`.
- Generic contracts: `kernel/kernel/inc/arch`, `kernel/kernel/inc/smp`, `kernel/kernel/ipi/ipi.c`.
- Debug hooks: `arch/*/backtrace.c`, `arch/*/gdbstub_arch.c`.

## Workflow

1. Identify whether the issue is generic or architecture-specific before editing shared kernel code.
2. For x86_64/QEMU bugs, prefer the current `kernel/arch/x86_64` source over xv6-tmp RISC-V docs.
3. If a C structure used by assembly changes, verify generated offsets and all assembly users.
4. For SMP issues, inspect per-CPU state, AP startup, IPI backend, and interrupt enablement together.
5. For page table or trap changes, check the generic proc/mm callers that consume the arch ABI.

## Pitfalls

- xv6-tmp is RISC-V and OrangePi-oriented; its boot, PLIC, timer, and device details are not authoritative for x86_64/QEMU.
- Trapframe, context, and signal trampoline layout mismatches often compile cleanly but fail at runtime.
- Do not move sleeping operations into interrupt, IPI, or early boot context.
