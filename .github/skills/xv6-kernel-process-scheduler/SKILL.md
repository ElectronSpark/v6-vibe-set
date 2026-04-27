---
name: xv6-kernel-process-scheduler
description: 'Use when: debugging xv6-os scheduler, run queues, EEVDF, FIFO, idle scheduling, clone/exit, PID tables, process groups, workqueues, signals, or runnable threads not running.'
argument-hint: 'Describe the process/scheduler symptom or paste xv6-threads output'
---

# xv6 Kernel Process Scheduler

## When to Use

- Threads are runnable but not scheduled, stuck on a run queue, or have inconsistent `on_cpu`/`on_rq` state.
- You are changing clone, exit, PID/process-group lifecycle, signals, or workqueue execution.
- CPU affinity, scheduler class selection, or SMP wakeup behavior is suspicious.

## Source Map

- Scheduler: `kernel/kernel/proc/sched.c`, `rq.c`, `sched_idle.c`, `sched_fifo.c`, `sched_eevdf.c`.
- Design docs: `kernel/kernel/proc/SCHEDULER_DESIGN.md`, `THREAD_QUEUE_DESIGN.md`.
- Lifecycle: `thread.c`, `clone.c`, `exit.c`, `thread_group.c`, `pid.c`, `pgroup.c`.
- Async work: `workqueue.c`, `kernel/kernel/inc/proc/workqueue*.h`.
- Signals and futexes: `signal.c`, `sys_signal.c`, `futex.c`.

## Workflow

1. Use `xv6-threads` or equivalent dumps to classify each thread as runnable, running, sleeping, zombie, or stopped.
2. Check run queue invariants before changing policy code: enqueue/dequeue state, CPU affinity, and class callbacks.
3. For wakeup races, pair this skill with `xv6-kernel-sleep-wakeup` and `xv6-kernel-timers`.
4. For deferred driver work, verify workqueue initialization, pending flags, worker liveness, and scheduler visibility.
5. For signal or process-group issues, include `tty/session` only if terminal job control is involved.

## Pitfalls

- The scheduler is a direct context-switch model, not a separate scheduler thread.
- `CHAN=0` can be a timed sleep; inspect timer state before assuming a lost wait channel.
- Do not solve run queue corruption by adding broad locks without checking lock order and IRQ context.
