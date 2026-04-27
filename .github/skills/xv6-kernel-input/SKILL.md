---
name: xv6-kernel-input
description: 'Use when: debugging xv6-os PS/2 mouse, VMware absolute vmmouse, keyboard, /dev/mouse, /dev/kbd, mouse ring counters, cdev poll callbacks, cursor movement, or input lost after GUI boot.'
argument-hint: 'Describe the input symptom or paste xv6-input output'
---

# xv6 Kernel Input

## When to Use

- Cursor does not move, keyboard input is ignored, or GUI input freezes.
- `/dev/mouse` or `/dev/kbd` readiness is suspicious.
- `xv6-input` shows mouse ring head/tail movement but reads do not succeed.
- You are comparing PS/2 relative mode and VMware absolute pointer behavior.

## Key Findings

- The intended QEMU GUI path is VMware absolute pointer first, with PS/2 relative mode as fallback.
- Mouse event ABI is 8 bytes and currently matches between kernel and compositor: `int16 dx`, `int16 dy`, `uint8 buttons`, `uint8 flags`, `int8 dz`, one byte pad.
- `dbg_mouse_reads` can be high while `dbg_mouse_reads_ok` is zero if user space repeatedly read while the ring was empty and later stopped waking.
- A nonempty mouse ring with no successful reads points toward event-loop or readiness wakeup, not necessarily packet decoding.
- `/dev/kbd` needs a `.poll` callback so epoll/kqueue can report keyboard readability.
- Mouse ring push wakes `&mouse_state.ring`; epoll readiness also depends on cdev `.poll` being consulted.

## Procedure

1. Use `xv6-input` in GDB to capture:
   - `dbg_mouse_irqs`, `dbg_mouse_bytes`, `dbg_mouse_packets`, `dbg_mouse_ringpush`.
   - `dbg_mouse_reads`, `dbg_mouse_reads_ok`.
   - mouse and keyboard ring `head`/`tail`.
2. Interpret the counters:
   - Packets and ringpush increasing: kernel device side is receiving input.
   - Ring nonempty and `reads_ok` not increasing: consumer is not draining or not being woken.
   - Overflow increasing: consumer is too slow or stuck.
3. Verify cdev readiness paths:
   - `kernel/kernel/dev/ps2mouse.c` should expose `.poll = mouse_poll`.
   - `kernel/kernel/dev/ps2kbd.c` should expose `.poll = kbd_poll`.
   - `mouse_poll`/`kbd_poll` should return `POLLIN`/`0x01` only when their rings are nonempty.
4. If input is generated but not consumed, inspect kqueue/epoll next:
   - `kernel/kernel/kqueue/kqueue_filters.c` should consult file ops and cdev poll callbacks.
   - `kernel/kernel/kqueue/kqueue.c` should rescan level-triggered registered knotes before sleeping or returning no events.
5. Confirm user-space ABI before changing kernel structures:
   - Compare `kernel/kernel/inc/dev/ps2mouse.h` and `ports/wayland/src/wlcomp.c`.
   - Check `sizeof(struct mouse_event)` assumptions if either side changes.
6. Use HMP `mouse_move` only as a coarse movement stimulus; manual GTK input is more reliable for click-coordinate tests.

## Relevant Files

- `kernel/kernel/dev/ps2mouse.c`
- `kernel/kernel/dev/ps2kbd.c`
- `kernel/kernel/inc/dev/ps2mouse.h`
- `kernel/kernel/kqueue/kqueue_filters.c`
- `ports/wayland/src/wlcomp.c`
- `user/programs/mousetest/mousetest.c`

## Pitfalls

- Do not revert absolute vmmouse support just because relative PS/2 mode is easier to observe.
- Do not assume high `dbg_mouse_reads` means current draining; check `dbg_mouse_reads_ok` and ring head/tail.
- Do not fix input by adding sleeps in the compositor; first verify readiness and wait semantics.
