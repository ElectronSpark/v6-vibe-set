---
name: xv6-kernel-traps-syscalls
description: 'Use when: debugging xv6-os traps, exceptions, page faults, syscall dispatch, syscall ABI, trapframes, return-to-user paths, interrupt handoff, or driver IRQ registration.'
argument-hint: 'Describe the trap/syscall failure or paste the fault output'
---

# xv6 Kernel Traps And Syscalls

## When to Use

- User programs fault, syscalls return the wrong value, or return-to-user is unstable.
- A device interrupt is not dispatched, is dispatched repeatedly, or misses EOI/ack behavior.
- You are touching syscall numbers, trapframes, exception handlers, or architecture syscall entry.

## Source Map

- Generic IRQ dispatch: `kernel/kernel/irq/irq.c`.
- Syscall contract: `kernel/kernel/inc/syscall.h`, `kernel/kernel/inc/uabi/syscall.h`, arch `irq/syscall.c`.
- Trap contract: `kernel/kernel/inc/trap.h`, `kernel/kernel/inc/trapframe.h`, arch `irq/trap.c` and trap vectors.

## Workflow

1. Start from the architecture entry path, then follow handoff into generic trap or syscall dispatch.
2. Check syscall number definitions before changing dispatch tables or user ABI names.
3. For faults, identify whether the consumer is mm, proc/signal, or a fatal exception path.
4. For IRQ bugs, verify handler registration, interrupt-controller routing, EOI/ack, and generic accounting.
5. If changing trapframe layout, check assembly offsets and signal/context switch assembly.

## Pitfalls

- x86_64 and RISC-V register ABI details differ; do not transfer trapframe assumptions between them.
- A bad return-to-user path may look like an unrelated scheduler or signal bug.
- Do not hold sleeping locks or call blocking code from hard interrupt context.
