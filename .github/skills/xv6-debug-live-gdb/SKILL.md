---
name: xv6-debug-live-gdb
description: 'Use when: debugging a live xv6-os QEMU/GDB session, GDB attach state, QEMU_GDB, QEMU_GDB_WAIT, xv6-freeze, xv6-syscall, xv6-kqueue, HMP monitor captures, stale VM sessions, or post-patch runtime validation.'
argument-hint: 'Describe the live VM/GDB state or paste the capture'
---

# xv6 Live GDB Debugging

## Fluidity Notice

This skill captures current live-debugging practice. It is not ground truth and can become deprecated without notice when scripts, GDB helpers, QEMU flags, or kernel debug structures change. Prefer `scripts/xv6.gdb`, current source, and fresh captures over this text.

## When to Use

- A VM is already running and you need to decide whether it is useful evidence.
- You are attaching to QEMU with `scripts/attach-gdb.sh`.
- You need to sample a running GUI or freeze without perturbing it more than necessary.
- You are validating a patch with live GDB commands.

## Workflow

1. Identify the runtime before sampling:
   - `pgrep -af 'qemu-system-x86_64|gdb .*kernel|scripts/attach-gdb.sh'`
   - Confirm the QEMU command, `USE_KVM`, `QEMU_GDB`, `QEMU_GDB_WAIT`, kernel path, and image path.
2. If QEMU was started before the last rebuild, stop and relaunch before using it for validation.
3. Attach with `bash scripts/attach-gdb.sh`; if started with `QEMU_GDB_WAIT=1`, run `c` once to boot.
4. For a freeze, press Ctrl-C in GDB and run `xv6-freeze` first.
5. For targeted GUI/event bugs, run the narrow helpers after the first capture:
   - `xv6-syscall wlcomp`
   - `xv6-kqueue wlcomp`
   - `xv6-input`
   - `xv6-timers`
6. After a healthy sample, continue the VM with `c` unless you need it paused for inspection.

## Methodology

- Start every live-debug session by proving which binary is running. A precise GDB capture from the wrong VM is worse than no capture.
- Use broad captures once, then narrow helpers repeatedly. `xv6-freeze` gives the map; `xv6-syscall`, `xv6-kqueue`, `xv6-input`, and `xv6-timers` test specific theories.
- Sample the same target more than once before calling it stuck. A rendering loop, timer path, or network path may be caught in a hot function by chance.
- Prefer saved trapframe arguments over source assumptions when debugging syscalls.
- Keep GDB interaction low-impact: interrupt, inspect, then continue unless the VM must remain paused.
- Write down whether the sample is a freeze capture, a healthy control sample, or a post-patch validation sample.

## Common Problems

- **Paused-at-reset confusion**: `QEMU_GDB_WAIT=1` leaves the VM stopped until GDB runs `c`.
- **Wrong thread interpretation**: GDB's selected thread is a QEMU host thread, not automatically the xv6 process of interest.
- **Concatenated commands**: sending multiple GDB commands in one terminal input can turn `wlcomp` plus the next command into one invalid selector.
- **False stuck samples**: one interrupt during framebuffer copy, timer tick, or idle loop is not enough to prove a freeze.
- **Unpublished helper drift**: `scripts/xv6.gdb` and kernel structs can get out of sync; helper failures may be tooling bugs.
- **Forgotten continue**: leaving GDB paused can look like a VM hang from the GUI side.

## Interpretation Notes

- `wlcomp` in framebuffer `ioctl` during repeated samples usually means the compositor is rendering, not stuck in internal event wait.
- `epoll_pwait` trapframe arguments are more reliable than source-level assumptions about timeout values.
- `waiters=0` on both internal and outer compositor kqueues is a useful healthy sample for the KVM GUI freeze class.
- A stopped GDB thread is a host/QEMU CPU thread, not automatically the xv6 thread that owns the symptom.

## Pitfalls

- Sending multiple GDB commands in one terminal input can concatenate into one invalid helper argument.
- A live VM that reached desktop before a rebuild cannot validate the rebuilt kernel.
- Do not call arbitrary kernel functions from GDB when read-only helpers can answer the question.
