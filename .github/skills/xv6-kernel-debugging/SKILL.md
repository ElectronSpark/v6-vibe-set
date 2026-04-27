---
name: xv6-kernel-debugging
description: 'Use when: working on xv6-os GDB stub, gdbstub_arch, scripts/xv6.gdb, backtraces, symbols, ksymbols, panic diagnostics, coredump, asm offsets, cmdline, or freeze captures.'
argument-hint: 'Describe the debugging artifact or diagnostic failure'
---

# xv6 Kernel Debugging

## When to Use

- GDB attach, remote stub, backtrace, symbols, panic output, coredump, or generated offsets fail.
- You need a kernel-first capture of QEMU/KVM freezes.
- Kernel command-line parsing or debug feature switches are involved.

## Source Map

- GDB stub: `kernel/kernel/gdbstub/gdbstub.c`, `kernel/arch/*/gdbstub_arch.c`.
- Symbols/backtrace: `kernel/kernel/backtrace.c`, `ksymbols.c`, `ksymbols_placeholder.S`.
- Diagnostics: `diag.c`, `printf.c`, `console.c`, `uart.c`.
- Coredump/cmdline: `coredump.c`, `cmdline.c`.
- Offset generation: `kernel/kernel/inc/README.md`, `kernel/scripts/gen_asm_offsets.py`.
- Runtime scripts: `scripts/run-qemu.sh`, `scripts/attach-gdb.sh`, `scripts/xv6.gdb`.

## Workflow

1. Verify whether the problem is debug transport, symbol generation, stack unwinding, or diagnostic output.
2. For freezes, use `xv6-kernel-freeze-triage` first, then route to subsystem skills from the capture.
3. If C structs used by assembly change, regenerate/check offsets before debugging runtime symptoms.
4. For GDB stub bugs, inspect architecture register encoding and memory access safety.
5. Keep debug helpers read-oriented unless there is a clear reason to call kernel functions from GDB.

## Pitfalls

- Stale QEMU sessions can invalidate every capture after a rebuild.
- Backtrace quality depends on architecture unwinder assumptions and symbols.
- Generated offsets are a contract between C and assembly, not documentation only.
