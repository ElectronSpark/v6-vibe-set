---
name: xv6-kernel-freeze-triage
description: 'Use when: diagnosing xv6-os freezes, KVM hangs, QEMU stalls, all CPUs idle, GDB stub captures, xv6-freeze output, or kernel/user tasks stuck INTERRUPTIBLE. Focuses on kernel-first freeze triage.'
argument-hint: 'Describe the freeze symptom or paste xv6-freeze output'
---

# xv6 Kernel Freeze Triage

## When to Use

- KVM or QEMU GUI becomes unresponsive after boot.
- `xv6-freeze` output shows all CPUs idle, user tasks asleep, or input/network events queued but not consumed.
- The VM was launched with `QEMU_GDB=1`, `QEMU_GDB_WAIT=1`, or `USE_KVM=1`.
- You need to decide whether a freeze is in timers, scheduler, kqueue/epoll, input, networking, or user-space compositor code.

## Key Findings

- Preserve the reproducer's CPU, memory, accelerator, frontend and image settings. The active KDE/media controls use six vCPUs and 8 GiB; other workloads may need different resources. Read `docs/active-work-plan.md` for current acceptance settings.
- A QEMU launched before rebuilding still runs the old kernel/image. Capture the existing failure first; a fresh authorized run is needed to test changed artifacts. A build does not authorize stopping someone else's VM.
- If QEMU is launched with `-S` or `QEMU_GDB_WAIT=1`, it is paused at reset until GDB runs `c`.
- `CHAN=0` on an `INTERRUPTIBLE` thread can be normal for timed waits; do not treat the wait-channel dump alone as complete evidence.
- For a confirmed legacy `/dev/mouse` consumer, queued packets with no successful reads point toward consumption or readiness. KWin/libinput normally uses evdev; those legacy counters alone do not describe its per-open input queue.

## Procedure

1. Identify exact process ownership before interpreting a capture. Follow root `AGENTS.md` for QEMU inventory, single-VM authorization and synchronous owned cleanup; avoid `pgrep` self-matches. Record the running command and actual kernel/image provenance, including `FSIMG` overrides. Leave external VMs untouched.
2. For a fresh debug boot, use the repository scripts:
   - `QEMU_GDB=1 QEMU_GDB_WAIT=1 USE_KVM=1 bash scripts/launch/launch-gui.sh` with the reproducer's remaining settings.
   - Set `KERNEL` to the matching symbol artifact and `GDB_PORT` to the actual stub port when calling `bash scripts/debug/attach-gdb.sh` from a shell. The debugger does not automatically select the GUI launcher's latest receipt.
   - In GDB, run `c`; after the freeze, press `Ctrl-C` and follow [live GDB](../xv6-debug-live-gdb/SKILL.md) for a bounded capture.
3. Start with memory-reading helpers in this order:
   - `xv6-cpus`: identify IRQ/timer hotspots versus all CPUs idle.
   - `xv6-threads`: check `STATE`, `ONRQ`, `ONCPU`, saved `RA`, and process names.
   - `xv6-input`: compare legacy mouse ring positions when that is the active path; select the actual evdev consumer for KWin.
   - `xv6-timers`: compare `sched_ms`, `current_tick`, `next_tick`, and pending timers.
   - `xv6-syscall <same-run-pid>` and `xv6-kqueue <same-run-pid>`: inspect the identified subject rather than their historical `wlcomp` default.
   - `xv6-freeze` includes target kernel calls and hardcodes `wlcomp`; `xv6-bt-blocked` also executes target code. Use such helpers only deliberately with understood stop-state constraints, as described in the live-GDB skill.
4. If all CPUs are idle and `sched_ms` advances but input is queued, pivot to kqueue/epoll and compositor event-loop readiness.
5. If all CPUs are idle and timers are overdue, distinguish monotonic-clock refresh from timer-root processing. x86 timer interrupts scan scheduler timers on any CPU; do not assume only the boot CPU advances them.
6. If a CPU is repeatedly in NIC RX from timer/IRQ context, identify the active NIC first; use e1000 workqueue guidance only for an e1000 path.

## Relevant Files

- `scripts/launch/run-qemu.sh`
- `scripts/debug/attach-gdb.sh`
- `scripts/debug/xv6.gdb`
- `kernel/kernel/timer/timer.c`
- `kernel/kernel/timer/sched_timer.c`
- `kernel/kernel/kqueue/kqueue.c`
- `kernel/kernel/dev/ps2mouse.c`
- `kernel/kernel/e1000.c`

## Validation

- Check helper loading with the selected symbol file:
  `gdb -q -nx /absolute/path/to/matching/kernel.elf -x scripts/debug/xv6.gdb -batch -ex 'help xv6-freeze'`.
  This checks definition loading, not every helper's runtime behavior.
- Build the changed layer and refresh the image only when its staged contents change. Validate in an owned fresh run, synchronously reap it, and finish with the exact no-QEMU check required by `AGENTS.md`.
