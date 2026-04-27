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

- Keep x86_64 runtime assumptions intact while debugging: 6 CPUs, 4GB RAM, KVM opt-in through `USE_KVM=1`.
- A stale QEMU launched before rebuilding still runs the old kernel/image. Restart QEMU after `kernel`, `ports`, `rootfs`, or `image` changes.
- If QEMU is launched with `-S` or `QEMU_GDB_WAIT=1`, it is paused at reset until GDB runs `c`.
- `CHAN=0` on an `INTERRUPTIBLE` thread can be normal for timed waits; do not treat the wait-channel dump alone as complete evidence.
- Mouse packets accumulating while `dbg_mouse_reads_ok` stays zero usually means the compositor stopped reaching `/dev/mouse` reads, not that mouse ABI is mismatched.

## Procedure

1. Check for stale runtime processes before interpreting a capture:
	- Look for `qemu-system-x86_64`, `gdb ... kernel`, and `attach-gdb`.
	- Confirm the running QEMU command uses the rebuilt `build-x86_64/kernel/kernel.elf` and current `build-x86_64/fs.img`.
2. For a fresh debug boot, use the repository scripts:
   - `QEMU_GDB=1 QEMU_GDB_WAIT=1 USE_KVM=1 bash scripts/launch-gui.sh`
   - `bash scripts/attach-gdb.sh` from a shell, not from inside GDB.
   - In GDB, run `c`; after the freeze, press `Ctrl-C`, then run `xv6-freeze`.
3. Read `xv6-freeze` in this order:
   - `xv6-cpus`: identify IRQ/timer hotspots versus all CPUs idle.
   - `xv6-threads`: check `STATE`, `ONRQ`, `ONCPU`, saved `RA`, and process names.
   - `xv6-input`: compare mouse ring positions and `dbg_mouse_reads_ok`.
   - `xv6-timers`: compare `sched_ms`, `current_tick`, `next_tick`, and pending timers.
   - `xv6-bt-blocked`: inspect saved blocked-thread kernel stacks.
4. If all CPUs are idle and `sched_ms` advances but input is queued, pivot to kqueue/epoll and compositor event-loop readiness.
5. If all CPUs are idle and scheduler timers are overdue or not advancing, pivot to scheduler timer processing and boot-hart timer interrupt paths.
6. If CPU0 is repeatedly in NIC RX from timer/IRQ context, pivot to e1000 workqueue deferral.

## Relevant Files

- `scripts/run-qemu.sh`
- `scripts/attach-gdb.sh`
- `scripts/xv6.gdb`
- `kernel/kernel/timer/timer.c`
- `kernel/kernel/timer/sched_timer.c`
- `kernel/kernel/kqueue/kqueue.c`
- `kernel/kernel/dev/ps2mouse.c`
- `kernel/kernel/e1000.c`

## Validation

- Validate GDB helper syntax with the built kernel symbols:
  `gdb -q -nx build-x86_64/kernel/kernel.elf -x scripts/xv6.gdb -batch -ex 'help xv6-freeze'`
- Rebuild the kernel/image through the project build integration when available, then restart QEMU before retesting.
