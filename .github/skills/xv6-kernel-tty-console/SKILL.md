---
name: xv6-kernel-tty-console
description: 'Use when: debugging xv6-os TTY, PTY, ptmx, termios, sessions, foreground process groups, job-control signals, console, UART, keyboard-to-terminal input, or terminal poll readiness.'
argument-hint: 'Describe the TTY/console/session symptom'
---

# xv6 Kernel TTY And Console

## When to Use

- Terminal input/output, PTY, `/dev/ptmx`, termios, echoing, canonical/raw mode, or console output is wrong.
- Job-control signals, foreground process groups, or controlling terminal behavior is involved.
- Keyboard input reaches the kernel but not the terminal consumer.

## Source Map

- TTY sources: `kernel/kernel/tty/` (`tty.c`, `tty_dev.c`, `pty.c`, `ptmx.c`, `termios.c`, `session.c`, `TTY_DESIGN.md`).
- Linux ioctl marshalling: `kernel/kernel/vfs/vfs_syscall.c`; internal termios: `kernel/kernel/inc/tty/termios.h`.
- Console output: `kernel/kernel/console.c`, `kernel/kernel/uart.c`, `kernel/kernel/printf.c`, `kernel/kernel/diag.c`.
- KDE child setup, when debugging Konsole: `scripts/image/kde-konsole-shell-wrapper.c`, `scripts/image/kde-session.c`.
- Related modules: proc process groups/signals and input cdevs.

## Workflow

1. Classify the path as console debug output, real TTY, PTY master/slave, or terminal session logic.
2. Check termios mode before interpreting line discipline behavior. Linux x86_64 TCGETS/TCSETS uses the 36-byte `linux_ioctl_termios` wire layout in the syscall adapter; do not copy the larger internal/libc structure directly to userspace.
3. For job control, inspect session, controlling TTY, foreground process group, and signal masks together.
4. For readiness stalls, check the opened file's `.poll` and notification target. PTY slave output must wake the master file's knotes, not just the internal output pipe. Preserve the pinned master-file reference across notification; route delivery to [event-wait](../xv6-kernel-event-wait/SKILL.md).
5. For keyboard source bugs, route to `xv6-kernel-input` first.

## Pitfalls

- Console printing and TTY behavior are related but not the same subsystem.
- Session foreground group races can look like lost signals.
- PTY master/slave lifetime must survive open file references.
- A working terminal window does not prove its shell started. For blank Konsole with input working elsewhere, correlate child exec/exit, slave fd setup, controlling TTY and foreground group before blaming global keyboard delivery. The [September GUI audit](../../../docs/gui-progress-audit-20260907.md) leaves that cause unresolved; it is not proof of a kernel PTY bug.
