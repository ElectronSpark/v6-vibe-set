---
name: xv6-kernel-timers
description: 'Use when: working on xv6-os timer core, scheduler timers, hardware ticks, LAPIC timer, RISC-V timer, RTC, timerfd, sleep_ms, timeout callbacks, retry_limit, or overdue timers.'
argument-hint: 'Describe the timer or timeout symptom'
---

# xv6 Kernel Timers

## When to Use

- Timeout callbacks are late, early, repeated, or missing.
- `sleep_ms`, futex timeout, kqueue timeout, timerfd, or scheduler ticks misbehave.
- You are changing timer roots, timer nodes, retry limits, or architecture tick sources.

## Source Map

- Generic timer root/node: `kernel/kernel/timer/timer.c`, `kernel/kernel/inc/timer/timer*.h`.
- Scheduler timebase: `kernel/kernel/timer/sched_timer.c`, `sched_timer_private.h`.
- Architecture ticks: `kernel/arch/x86_64/timer/timer.c`, `kernel/arch/riscv/timer/timer.c`.
- RTC: `kernel/kernel/timer/goldfish_rtc.c` where applicable.
- Focused debug skill: `xv6-kernel-timers-scheduler`.

## Workflow

1. Separate hardware tick delivery from generic timer processing and from consumer callbacks.
2. Inspect `sched_ms`, jiffies, timer root `current_tick`, `next_tick`, and pending nodes.
3. Verify `timer_node_init()` preserves caller retry limits; one-shot timers often pass retry limit `1`.
4. For timeout consumers, check whether callbacks wake threads safely and only when still sleeping.
5. For x86_64/QEMU, prefer current LAPIC timer source over xv6-tmp RISC-V timer notes.

## Pitfalls

- Overdue timers can be caused by missing processing even when hardware jiffies advance.
- Do not mask timer bugs with user-space sleeps or busy polling.
- Timer callbacks inherit context constraints from their execution path.
