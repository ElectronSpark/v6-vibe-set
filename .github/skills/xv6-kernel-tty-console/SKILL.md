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

- TTY core: `kernel/kernel/tty/tty.c`, `tty_dev.c`, `TTY_DESIGN.md`.
- PTY: `pty.c`, `ptmx.c`.
- Termios/session: `termios.c`, `session.c`.
- Console output: `kernel/kernel/console.c`, `uart.c`, `printf.c`, `diag.c`.
- Related modules: proc process groups/signals and input cdevs.

## Workflow

1. Classify the path as console debug output, real TTY, PTY master/slave, or terminal session logic.
2. Check termios mode before interpreting line discipline behavior.
3. For job control, inspect session, controlling TTY, foreground process group, and signal masks together.
4. For readiness stalls, check TTY `.poll` behavior and route to `xv6-kernel-event-wait`.
5. For keyboard source bugs, route to `xv6-kernel-input` first.

## Pitfalls

- Console printing and TTY behavior are related but not the same subsystem.
- Session foreground group races can look like lost signals.
- PTY master/slave lifetime must survive open file references.
