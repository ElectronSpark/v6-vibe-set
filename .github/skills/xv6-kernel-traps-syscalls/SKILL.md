---
name: xv6-kernel-traps-syscalls
description: 'Use when: debugging xv6-os traps, exceptions, page faults, syscall dispatch, syscall ABI, trapframes, return-to-user paths, interrupt handoff, or driver IRQ registration.'
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
- x86_64 SYSCALL entry: `kernel/arch/x86_64/irq/trapvec.S` (`syscall_entry`),
  C handler `usertrap_syscall()` and return path `usertrapret()` in
  `kernel/arch/x86_64/irq/trap.c`, final return `userret` in
  `kernel/arch/x86_64/irq/trampoline.S`.

## x86_64 Per-Syscall Cost Map

The x86_64 syscall path carries a large fixed cost per call relative to
Linux, plus a hidden TLB-refill amplification that grows with the user
working set. Current measured numbers and proof archives live in
`docs/active-work-plan.md`; the durable mechanism is:

1. `usertrap_syscall()` writes CR3 to the kernel page table (full non-global
   TLB discard).
2. `usertrapret()` builds the user CR3 with forced `noflush=0` and
   `trampoline_userret`/`userret` writes it (second full user-TLB discard).
   The in-code comment explains why: `vm_cpu_online()`
   (`kernel/arch/x86_64/mm/vm.c`) rewrites the per-CPU TRAPFRAME-slot PTE on
   every return without TLB invalidation; enabling `noflush=1` without
   fixing that has produced stale-TLB fork() return-value corruption under
   KVM.
3. `rdmsr` of FS_BASE and KERNEL_GS_BASE on entry, multiple `wrmsr` on exit.
4. A serializing CR0.TS read/modify/write for lazy FPU.
5. Trapframe copies between the entry stack frame and the per-process
   utrapframe.
6. Atomic VM-cpumask RMWs (`vm_cpu_offline`/`vm_cpu_online`) for both the
   user and kernel VMs.

How to measure (copy-paste):

```sh
# Host Linux control:
gcc -O2 -DHOST_LIBC_PROGRAM -o /tmp/syscalltlb-host \
    user/programs/syscalltlb/syscalltlb.c && /tmp/syscalltlb-host 2000
# xv6 guest: boot nographic and run `syscalltlb 2000` at the root:/# prompt.
# The binary is staged by: cmake --build build-x86_64 --target user, then
# cmake --build build-x86_64 --target rootfs-refresh.
```

The benchmark reports pure syscall cost (`getpid_ns`), a TLB-warm page-sweep
baseline (`sweep_ns`), and the extra cost a syscall induces on the following
sweep (`tlb_amplification_ns`). Compare guest and host output before and
after any syscall-path change.

Rules before changing this path:

- Do NOT flip `noflush=1` or PCID defaults without first making the
  TRAPFRAME-slot mapping precise (targeted `invlpg`, or skip the PTE rewrite
  when the same thread resumes on the same CPU).
- PCID (`x86_pcid=1`) is opt-in and has documented GUI corruption history
  under SMP load. Any PCID A/B MUST verify the boot log line
  `vm_asid_init: max ASID = <nonzero>`; a quoting mistake in `QEMU_APPEND`
  silently leaves PCID off (`max ASID = 0`).
- Gate any change with fork/exec stress (`forktest`, `clonetest`, `cowtest`)
  because the historical noflush bug corrupted fork return values.

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
