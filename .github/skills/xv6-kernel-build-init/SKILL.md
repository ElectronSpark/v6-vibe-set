---
name: xv6-kernel-build-init
description: 'Use when: working on xv6-os kernel CMake, object-library aggregation, start_kernel init order, linker scripts, kernel image layout, asm offset generation, ARCH/PLATFORM config, or early init failures.'
argument-hint: 'Describe the build/init/link symptom'
---

# xv6 Kernel Build And Init

## When to Use

- Kernel build, link, image layout, generated offsets, or early initialization order is wrong.
- A subsystem must be initialized before devices, VFS, proc, network, daemons, or first user process.
- You are changing `ARCH`, `PLATFORM`, linker scripts, or object-library composition.

## Source Map

- Init order: `kernel/kernel/start_kernel.c`.
- Build graph: `kernel/kernel/CMakeLists.txt`, `kernel/CMakeLists.txt`, `cmake/BuildKernel.cmake`, `cmake/BuildImage.cmake` and top-level `CMakeLists.txt`.
- Link layout: `kernel/kernel/kernel.ld.in`, `kernel/kernel/kernel_x86_64.ld.in`.
- Entry: `kernel/arch/x86_64/start.c`, `kernel/arch/riscv/start.c`, root entry files.
- Generated offsets: `kernel/scripts/gen_asm_offsets.py`, `kernel/kernel/inc/README.md`.

## Workflow

1. Determine whether failure happens at configure, compile, link, image generation, boot entry, or subsystem init.
2. For missing symbols, inspect object-library aggregation before changing source names.
3. For missing backtrace symbols, check the umbrella graph before the linker script: top-level `kernel` builds `kernel_all`, then copies the build-relative `kernel_with_symbols_elf` artifact to `build-x86_64/kernel/kernel.elf` for debugging. The current GUI launcher selects `build-x86_64/kernel/build/kernel/xv6.bin` by default. Verify embedded symbols in the actual boot artifact and match GDB's ELF to the same build; do not infer the booted file from the install copy.
4. For early boot failures, check linker addresses, entry path, stack setup, and first C init order.
5. For new subsystems, add init after dependencies and before first consumer.
6. Prefer current umbrella CMake behavior over xv6-tmp's older single-tree build assumptions.

## Pitfalls

- Init order bugs can look like device, VFS, or scheduler bugs later in boot.
- Linker script mistakes may boot partly before failing mysteriously.
- Offset generation must stay synchronized with C structs used by assembly.
- A nonempty `.ksymbols` section in `kernel_with_symbols_elf` does not prove the selected QEMU boot artifact contains those symbols; inspect the actual build and launch edges.
- Kernel-only changes normally need a kernel rebuild and a fresh owned run, not a ports/rootfs rebuild. Regenerate staged images when their payload changes and verify the actual boot artifact; follow root `AGENTS.md` for VM authorization and cleanup.
