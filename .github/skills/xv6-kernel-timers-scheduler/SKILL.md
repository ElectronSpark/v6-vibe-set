---
name: xv6-kernel-timers-scheduler
description: 'Use when: debugging xv6-os timer_tick, sched_timer, sleep_ms, timed waits, kqueue timeouts, all CPUs idle, overdue timers, retry_limit, scheduler wakeups, or xv6-timers GDB output.'
argument-hint: 'Describe the timer symptom or paste xv6-timers output'
---

# xv6 Kernel Timers And Scheduler

## When to Use

- Threads stay `INTERRUPTIBLE` after a timeout should have expired.
- `epoll_wait`, `kqueue_wait`, `sleep_ms`, or timerfd-like waits appear stuck.
- `xv6-freeze` shows all CPUs idle and no obvious blocking channel.
- You need to interpret `xv6-timers` output.

## Key Findings

- Hardware jiffies and scheduler milliseconds are separate values to inspect.
- `sched_timer_refresh_ms()` advances `__sched_timer_ms` monotonically from `r_time()` and `__timebase_frequency`, on any CPU. `sched_timer_tick()`, `sched_timer_now_ms()`, arming and `__do_timer_tick()` refresh it; this is not a CPU-0 tick counter.
- Scheduler timer callbacks resolve the stored PID under RCU and call `wakeup()` only for a still-live sleeping thread. Do not replace this with an unpinned thread pointer.
- `timer_node_init()` must store the caller-provided `retry_limit`; otherwise one-shot timed waits can behave incorrectly.
- Many scheduler timer users pass retry limit `1`, so retry-limit initialization is not optional.
- Callers that sleep after `sched_timer_set()` must handle a nonzero return; short waits can expire before insertion and must not continue into an unarmed sleep.
- If all CPUs are idle, verify that scheduler timer processing still happens in a guaranteed path after timer interrupts.

## Procedure

1. Capture timer state with GDB:
   - Run `xv6-timers` directly using the matching-symbol [live-GDB workflow](../xv6-debug-live-gdb/SKILL.md). The legacy `xv6-freeze` composite also executes target kernel functions; it is not needed for a timer snapshot.
   - Inspect `sched_ms`, `jiffies`, `current_tick`, `next_tick`, `next_delta_ms`, and pending timer nodes.
2. Interpret the timer root:
   - `sched_ms` advancing and `current_tick` catching up: scheduler timer processing is likely active.
   - `sched_ms` advancing but `current_tick` stale or `next_tick` overdue: processing path may not be running.
   - Pending timers with negative deltas: expired timers are not being processed or callbacks are not waking targets.
3. Inspect timer initialization:
   - In `kernel/kernel/timer/timer.c`, `timer_node_init()` should set `node->retry_limit = retry_limit > 0 ? retry_limit : TIMER_DEFAULT_RETRY_LIMIT`.
   - Confirm `timer_tick()` removes one-shot timers when `retry >= retry_limit`.
4. Inspect scheduler timer callers:
   - `kernel/kernel/timer/sched_timer.c` should pass retry limit `1` for one-shot scheduler timers.
   - `__sched_timer_callback()` should wake the target only if it is still sleeping.
   - Timed wait callbacks such as kqueue should record `timer_armed = sched_timer_set(...) == 0` and immediately wake or avoid sleeping when the timer is already expired.
5. If timed waits remain stuck:
   - On x86_64, check local timer delivery and `sched_timer_tick()` / `__do_timer_tick()` on each CPU. RISC-V has a different interrupt path; inspect its implementation when that architecture is the target.
   - Check whether `__do_timer_tick()` is reachable from idle/scheduler paths when all runnable work is gone.
   - Avoid moving arbitrary wakeups into hard IRQ context without lock and context-safety review.

## Relevant Files

- `kernel/kernel/timer/timer.c`
- `kernel/kernel/timer/sched_timer.c`
- `kernel/kernel/inc/timer/timer.h`
- `scripts/debug/xv6.gdb`
- `kernel/kernel/proc/`

## Pitfalls

- Do not debug timed waits using only wait channels; timer-backed waits may show `CHAN=0`.
- Do not call kernel functions from GDB unless necessary; prefer reading symbols like `__sched_timer_ms` and `__sched_timer`.
- Do not mask timer bugs with user-space polling sleeps.
