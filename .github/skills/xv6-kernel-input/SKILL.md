---
name: xv6-kernel-input
description: 'Debug xv6-os PS/2, VMware or virtio pointer/keyboard delivery, evdev and legacy input queues, ABI translation and poll wakeups. Distinguish device delivery from GUI coordinate or application failures.'
argument-hint: 'Describe the input symptom or paste xv6-input output'
---

# xv6 Kernel Input

## When to Use

- Cursor does not move, keyboard input is ignored, or GUI input freezes.
- `/dev/input/event*`, `/dev/mouse` or `/dev/kbd` readiness is suspicious.
- `xv6-input` shows mouse ring head/tail movement but reads do not succeed.
- You are comparing virtio tablet, PS/2 relative mode and VMware absolute pointer behavior.

## Key Findings

- `scripts/launch/run-qemu.sh` currently resolves `QEMU_INPUT=auto` to vmmouse for SDL, virtio-tablet for GTK and none for nographic. Verify actual arguments and explicit overrides; vmmouse requires `vmport=on`. A historical GTK setting is not a universal input policy.
- The legacy `/dev/mouse` ABI is an 8-byte `struct mouse_event`: `int16 dx`, `int16 dy`, `uint8 buttons`, `uint8 flags`, `int8 dz`, one pad byte. With the absolute flag, decode dx/dy as unsigned 16-bit coordinates. Linux clients consume `struct input_event` through evdev instead; do not conflate the two layouts.
- `dbg_mouse_reads` can be high while `dbg_mouse_reads_ok` is zero if user space repeatedly read while the ring was empty and later stopped waking.
- A nonempty mouse ring with no successful reads points toward event-loop or readiness wakeup, not necessarily packet decoding.
- `/dev/kbd` needs a `.poll` callback so epoll/kqueue can report keyboard readability.
- evdev has per-open client queues and file operations. Its waiters attach to the file knote list; cdev notification alone misses them. The notification helper in `evdev.c` pins open files under the state lock, then notifies outside it to avoid inversion with poll.

## Procedure

1. Identify the consumer's actual input fd and device. For the PS/2/vmmouse path, use `xv6-input` with matching GDB symbols to capture:
   - `dbg_mouse_irqs`, `dbg_mouse_bytes`, `dbg_mouse_packets`, `dbg_mouse_ringpush`.
   - `dbg_mouse_reads`, `dbg_mouse_reads_ok`.
   - mouse and keyboard ring `head`/`tail`.
2. Interpret legacy counters only for that path; inspect the corresponding per-open evdev queue when the consumer uses `/dev/input/event*`:
   - Packets and ringpush increasing: kernel device side is receiving input.
   - Ring nonempty and `reads_ok` not increasing: consumer is not draining or not being woken.
   - Overflow increasing: consumer is too slow or stuck.
3. Verify the active readiness path:
   - `kernel/kernel/dev/evdev.c` provides `evdev_fops_poll` and file-level notification. Preserve event timestamps, absolute ranges, button/wheel translation and SYN_REPORT grouping; do not reintroduce duplicate pointer motion.
   - `kernel/kernel/dev/ps2mouse.c` should expose `.poll = mouse_poll`.
   - `kernel/kernel/dev/ps2kbd.c` should expose `.poll = kbd_poll`.
   - `mouse_poll`/`kbd_poll` should return `POLLIN`/`0x01` only when their rings are nonempty.
4. If input is generated but not consumed, inspect kqueue/epoll next:
   - `kernel/kernel/kqueue/kqueue_filters.c` should consult file ops and cdev poll callbacks.
   - `kernel/kernel/kqueue/kqueue.c` should rescan level-triggered registered knotes before sleeping or returning no events.
5. Confirm the consumer ABI before changing structures: compare `kernel/kernel/inc/dev/ps2mouse.h` with legacy clients such as `user/programs/mousetest/mousetest.c`, or inspect evdev's Linux event/ioctl shapes for libinput clients.
6. For coordinate or click behavior, use the [GUI runtime workflow](../xv6-debug-gui-runtime/SKILL.md) to establish the foreground VM, host-client/guest geometry, stimulus and visible result. HMP movement or successful host-input delivery alone does not prove the intended GUI action occurred.

## Relevant Files

- `kernel/kernel/dev/ps2mouse.c`
- `kernel/kernel/dev/ps2kbd.c`
- `kernel/kernel/inc/dev/ps2mouse.h`
- `kernel/kernel/virtio_input.c`
- `kernel/kernel/dev/evdev.c`
- `kernel/kernel/inc/dev/evdev.h`
- `kernel/kernel/kqueue/kqueue_filters.c`
- `scripts/launch/run-qemu.sh`
- `user/programs/mousetest/mousetest.c`

## Pitfalls

- Do not revert absolute vmmouse support just because relative PS/2 mode is easier to observe.
- Do not assume high `dbg_mouse_reads` means current draining; check `dbg_mouse_reads_ok` and ring head/tail.
- Do not fix input by adding sleeps in the compositor; first verify readiness and wait semantics.
- App disappearance after a click is not evidence of an input or GPU fault. The [September mouse audit](../../../docs/gui-mouse-audit-20260907.md) reproduced Chromium New Tab disappearance while other controls remained usable; establish same-run application/thread ownership and exit evidence before assigning the cause. Its mixed hover results do not establish a universal hover delay or toolkit policy.
