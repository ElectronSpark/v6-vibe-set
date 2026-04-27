---
name: xv6-kernel-sleep-wakeup
description: 'Use when: debugging xv6-os sleep/wakeup races, thread_queue, futex waits, timed sleeps, wait channels, INTERRUPTIBLE threads, missed wakeups, or blocked kernel threads.'
argument-hint: 'Describe the blocked wait or paste thread/timer state'
---

# xv6 Kernel Sleep And Wakeup

## When to Use

- A thread sleeps forever, wakes too early, or misses a wakeup.
- Futex, kqueue timeout, `sleep_ms`, TTY, IPC, or device waits are blocked.
- `xv6-freeze` shows `INTERRUPTIBLE` tasks with unclear channels.

## Source Map

- Core queues: `kernel/kernel/proc/thread_queue.c`, `THREAD_QUEUE_DESIGN.md`.
- Scheduler integration: `kernel/kernel/proc/sched.c`.
- Futex: `kernel/kernel/proc/futex.c`.
- Timer-backed waits: `kernel/kernel/timer/sched_timer.c`.
- Consumers: kqueue, VFS, TTY, IPC, devices, lwIP port.

## Workflow

1. Classify the sleep as channel-based, queue-based, futex, or timer-backed.
2. Check the lock held during enqueue and whether the wake path uses the same protected state.
3. For timed waits, inspect scheduler timer node setup and callback behavior.
4. Confirm wakeup target state before and after the wake path: sleeping, runnable, on run queue, or already running.
5. If the user-visible symptom is epoll or GUI input, route to `xv6-kernel-event-wait` after basic wait-state checks.

## Pitfalls

- Wait-channel output alone is insufficient for timer-backed waits.
- Waking before enqueue or after state transition can lose the event even when the wake function is called.
- Avoid wakeups from contexts that cannot safely acquire the required locks.
