---
name: xv6-kernel-event-wait
description: 'Use when: debugging xv6-os kqueue, epoll, kevent, poll callbacks, knotes, EVFILT_READ/WRITE/TIMER/SIGNAL/PROC/VNODE, level-triggered readiness, nested epoll, sleeping threads with CHAN=0, epoll_wait stalls, compositor waits, /dev/mouse readiness, or /dev/kbd readiness.'
argument-hint: 'Describe the blocked wait or readiness issue'
---

# xv6 Kernel Event Waits

## When to Use

- `epoll_wait` or kqueue waits stop returning events.
- Input is queued in kernel rings but the compositor does not wake.
- Threads are `INTERRUPTIBLE` with `CHAN=0` and no obvious wait channel.
- You are changing kqueue, epoll, poll callbacks, file readiness, timer-backed waits, or knote lifecycle.
- Pipes, sockets, eventfd, timerfd, TTY, cdevs, signals, process events, or vnode notifications stop waking waiters.

## Overall Design

- `kernel/kernel/kqueue/kqueue.c` owns kqueue allocation, knote registration, ready-list delivery, wait queues, close, nested kqueue propagation, and producer notification helpers.
- `kernel/kernel/kqueue/kqueue_filters.c` owns per-filter attach/detach/event operations for `EVFILT_READ`, `EVFILT_WRITE`, `EVFILT_TIMER`, `EVFILT_SIGNAL`, `EVFILT_PROC`, and `EVFILT_VNODE`.
- `kernel/kernel/kqueue/kqueue_syscall.c` exposes native `sys_kqueue`, `sys_kevent_register`, and `sys_kevent_wait` with bounded batches of 256 events.
- `kernel/kernel/kqueue/epoll.c` implements Linux epoll over kqueue. The epoll fd is a kqueue fd internally.
- `kernel/kernel/inc/kqueue.h` is the user ABI for `struct kevent`, event filters, event flags, and note flags.
- `kernel/kernel/inc/kqueue_types.h` is the internal ABI for `struct kqueue`, `struct knote`, status bits, and notification helpers.
- `kernel/kernel/inc/vfs/poll.h` defines Linux/POSIX-compatible `POLL*` bits shared by VFS, TTY, sockets, and cdevs.

## Core Objects

- `struct kqueue` owns the registered/ready lists, wait queue, compatibility flags, registration sequence, and `waiters`/`pollers`/`registrars` lifetime counts.
- `struct knote` lives on up to three lists at once: `kq->registered`, `kq->ready`, and one source list such as `vfs_file::knote_list`, `vfs_inode::knote_list`, `thread::kqueue_proc_knotes`, or `sigacts_t::kqueue_signal_knotes[]`.
- Knote identity is `(ident, filter)`. For fd filters, `ident` is the watched fd number in the registering process.
- Knote user fields mirror `struct kevent`: `ident`, `filter`, `flags`, `fflags`, `data`, and `udata`.
- `sfflags` preserves subscribed vnode/proc flags across `EV_CLEAR`; `fflags` is delivered event state.
- Status also tracks edge/level delivery and deferred notifications through `KN_EDGE_ACTIVE`, `KN_LEVEL_SEEN`, `KN_DELIVERING`, and `KN_PENDING`. `poll_refs` and registration generations protect unlocked poll callbacks.
- `kq->file` is a back-pointer to the kqueue's VFS file so nested epoll/kqueue can propagate readiness to watchers of the epoll fd itself.

## Key Findings

- xv6-os implements epoll compatibility over kqueue; epoll read/write interest becomes `EVFILT_READ`/`EVFILT_WRITE` knotes.
- Read/write filter readiness is level-triggered through `.poll`: first `vfs_file_ops.poll`, then cdev `.poll` fallback for files with `f->ops == NULL`.
- `kqueue_wait()` rescans eligible registrations before sleeping. Each rescan has its own registration-ID cursor and high-water snapshot; file polling drops the queue lock, so a list cursor cannot survive across that boundary.
- Explicit producers still matter for instant wakeups: pipes, Unix sockets, lwIP sockets, eventfd, timerfd, file/vnode changes, proc events, and signal events all call kqueue notify helpers.
- `knote_enqueue()` and `knote_enqueue_with_data()` wake `kq->waitq` and propagate read readiness to outer watchers of the kqueue fd.
- `vfs_file_knote_notify()` holds `file->knote_lock`, uses `__knote_enqueue_core()`, then propagates to outer kqueues after dropping the source lock to avoid recursive `knote_lock` re-entry.
- `MAX_KNOTE_PROPAGATE` caps one propagation batch at 16 outer kqueues; excess propagated file refs are dropped after waking the inner kqueue.
- `EVFILT_TIMER` uses `sched_timer_add()` and currently cannot cancel allocated timer work items; detached timer knotes are kept alive to avoid callback use-after-free.
- Native `kevent_wait` and epoll both use `kqueue_wait()` with `timeout_ms`: `-1` blocks forever, `0` polls, positive values are milliseconds.
- A thread with `CHAN=0` can still be in a timed wait because kqueue timeout sleeps use scheduler timer callbacks, not a stable public channel pointer.
- `xv6-syscall <pid-or-name>` dumps the saved x86_64 syscall number and arguments; use it to confirm `epoll_pwait` fd, maxevents, and timeout for sleeping GUI tasks.

## Registration Semantics

- `EV_ADD` creates or updates a knote. New knotes attach to their source, are inserted into `kq->registered`, and are immediately checked with `ops->event()`.
- `EV_DELETE` finds the `(ident, filter)` knote, detaches source/ready/registered links and marks `KN_DETACHED`. Active poll references defer freeing; timer work has its separate deferred-lifetime restriction.
- `EV_ENABLE` clears `KN_DISABLED` and rechecks current readiness outside `kq->lock` before enqueueing.
- `EV_DISABLE` sets `KN_DISABLED`; disabled knotes remain registered but are not queued.
- Native kqueue `EV_ONESHOT` deletes after delivery. On `KQ_EPOLL_COMPAT` queues, it disables both read/write registrations for the fd until `EPOLL_CTL_MOD` rearms them.
- `EV_CLEAR` clears delivered `fflags` and `data` after delivery. Timer filters save their interval in `timer_ms` because `data` may be cleared.
- Per-change errors are reported by rewriting the user change with `EV_ERROR` and negative errno in `data`.

## Filter Details

- `EVFILT_READ`: attaches to `vfs_file::knote_list`, holds a file reference, and calls file or cdev poll with `POLLIN | POLLRDNORM`. `POLLHUP` and `POLLERR` also make it active.
- `EVFILT_WRITE`: attaches to `vfs_file::knote_list`, holds a file reference, and calls file or cdev poll with `POLLOUT | POLLWRNORM`. `POLLERR` also makes it active.
- `EVFILT_TIMER`: requires positive `data` milliseconds, stores the interval in `timer_ms`, enqueues from `kqueue_timer_callback`, and rearms periodically unless `EV_ONESHOT` is set.
- `EVFILT_SIGNAL`: attaches to the current thread's `sigacts` table for one signal number and increments `data` on signal delivery.
- `EVFILT_PROC`: resolves the target pid under RCU, attaches to `thread::kqueue_proc_knotes`, and is notified by fork, exec, and exit paths using `NOTE_FORK`, `NOTE_EXEC`, and `NOTE_EXIT`.
- `EVFILT_VNODE`: holds the watched file, attaches to the inode's knote list, and is notified with vnode `NOTE_*` flags such as write, link, delete, extend, attrib, and rename where producers call it.

## Epoll Bridge Details

- `sys_epoll_create1(flags)` calls `kqueue_create()` and applies `FD_CLOEXEC` when `EPOLL_CLOEXEC` is set.
- `sys_epoll_ctl(epfd, op, fd, event)` resolves `epfd` to the underlying `struct kqueue` and translates interest into one or two `struct kevent` changes.
- `EPOLLIN`, `EPOLLRDNORM`, `EPOLLRDBAND`, `EPOLLPRI`, and `EPOLLRDHUP` map to `EVFILT_READ`.
- `EPOLLOUT`, `EPOLLWRNORM`, and `EPOLLWRBAND` map to `EVFILT_WRITE`.
- `EPOLLET` maps to `EV_CLEAR`; `EPOLLONESHOT` maps to `EV_ONESHOT`.
- `EPOLL_CTL_DEL` tries to delete both read and write knotes and ignores missing knotes.
- `EPOLL_CTL_MOD` adds/enables requested filters and deletes filters removed from the new mask.
- If an ADD/MOD event has no read or write bits, the bridge currently registers a read knote anyway.
- User `epoll_data` is stored in `kevent.udata` and copied back unchanged.
- `struct k_epoll_event` is packed to 12 bytes on x86_64 and naturally 16 bytes on RISC-V. Keep this ABI split intact.
- `epoll_pwait_common()` validates and temporarily installs the optional signal mask, restoring it on exit. It uses a full 256-kevent internal batch and coalesces read/write results by watched fd and `udata`; the output still respects `maxevents`. `epoll_pwait2` validates and rounds its timespec to the supported millisecond resolution.
- Blocking epoll calls currently rescan in 20 ms internal slices, including infinite waits. Treat that as an implementation detail when measuring timeout accuracy, wake latency and CPU use; do not hide lost producer notifications with additional application polling.
- Only `EVFILT_READ` and `EVFILT_WRITE` are mapped back to epoll events. Other kqueue filters are skipped by epoll output conversion.
- Output combines current poll bits with the requested mask. `EV_EOF` becomes `EPOLLHUP` only with `POLLHUP`; requested `EPOLLRDHUP` is distinct. `EV_ERROR` maps to `EPOLLERR`.

## Wait And Timeout Path

- `kqueue_wait()` increments `kq->waiters`, loops until events, close, signal, timeout, or nonblocking poll completion, then decrements `waiters`.
- It checks `kq->closed`, rescans registered knotes, drains `kq->ready`, handles `EV_ONESHOT`, handles `EV_CLEAR`, then decides whether to sleep.
- Timed waits use a stack `timer_node` plus `sched_timer_set()` in `__kq_timed_sleep_cb()` and `sched_timer_done()` in `__kq_timed_wakeup_cb()`.
- `__kq_timed_sleep_cb()` must track whether `sched_timer_set()` succeeded; tiny waits such as `timeout=1` can race the scheduler tick and fail as already expired, and sleeping anyway leaves the waiter stranded until another event.
- `THREAD_INTERRUPTIBLE` waits return `-EINTR` if a signal is pending.
- If a timed wait wakes and the ready list is still empty, `kqueue_wait()` treats it as a timeout and returns 0.
- `kqueue_close()` marks closed, detaches registrations and wakes waiters with `-EBADF`. Freeing requires `waiters`, `pollers`, and `registrars` all to reach zero; the final active operation may perform that free.

## Producer Notification Map

- `vfs_file_knote_notify(file, EVFILT_READ/WRITE, data)` is used by pipes, Unix sockets, lwIP sockets, eventfd, timerfd, and nested kqueue propagation.
- `vfs_inode_knote_notify(inode, NOTE_*)` is used by VFS file/inode operations for vnode changes such as write, link, and delete.
- `kqueue_proc_notify(thread, NOTE_*, data)` is used by clone/fork, exec, and exit.
- `kqueue_signal_notify(thread, signo)` is used during signal delivery.
- `knote_enqueue()` is safe when the caller does not hold `kq->lock`; producer helpers handle the required locking.
- File and inode knote lists are protected by their own `knote_lock` fields in `struct vfs_file` and `struct vfs_inode`.

## Locking And Lifetime Rules

- `kq->lock` protects the kqueue registered list, ready list, counters, closed flag, waiters, and queued status.
- `vfs_file::knote_lock` protects file source lists; `vfs_inode::knote_lock` protects vnode source lists; proc and signal source lists have their own locks.
- The intended file notification nesting is `file->knote_lock` to `kq->lock` inside `__knote_enqueue_core()`.
- Code should not hold `kq->lock` while acquiring `file->knote_lock`; register and detach paths release `kq->lock` before source attach/detach.
- File/cdev `.poll` runs without `kq->lock`. Pin the knote and file, snapshot registration generation/identity, drop the lock, dispatch, then relock and reject stale results after MOD, disable, DEL, close, fd reuse or re-add. Do not restore the old callback-under-lock path.
- Notifications during delivery use pending/coalescing state. Preserve it across unlocked callbacks and reject stale results without leaving `KN_DELIVERING` set forever.
- `vfs_file_knote_notify()` takes file refs for outer propagation and releases them after recursive notification.
- FD filters hold `attached_file` references, but those internal references must not keep a Linux epoll watch alive after the final visible fd closes. Preserve visible-fd checks, closed-file purging and nested-queue cycle/lifetime validation.
- A detached knote with active `poll_refs` remains allocated until its last callback drops the reference.
- Vnode filters hold the file so the inode remains valid while watched.
- Timer knote freeing is deferred because timer callbacks can still hold the knote pointer after detach.

## Procedure

1. Identify the waiter and API:
   - Use `xv6-threads` for state, saved return address, `ONRQ`, `ONCPU`, and name.
   - Use `xv6-bt-blocked` to confirm whether the blocked path is `sys_epoll_pwait`, `sys_kevent_wait`, poll, sleep, or another syscall.
2. Check epoll translation if the caller uses Linux APIs:
   - Confirm `epfd` is a kqueue fd and `fd` interest generated the expected read/write kevent changes.
   - Check x86_64 `struct k_epoll_event` size and user copy offsets if event data looks corrupt.
   - Remember epoll output maps only read/write filters.
3. Check registration and source attachment:
   - Native kqueue: inspect `EV_ADD`, `EV_DELETE`, `EV_ENABLE`, `EV_DISABLE`, `EV_ONESHOT`, and `EV_CLEAR` flags.
   - FD filters: verify `attached_file`, `file->knote_list`, and file reference lifetime.
   - Vnode/proc/signal filters: verify the source list and filter-specific note flags.
4. Check readiness plumbing:
   - File ops `.poll` should return `POLLIN`/`POLLRDNORM`, `POLLOUT`/`POLLWRNORM`, `POLLHUP`, or `POLLERR` consistently.
   - Cdev files with `f->ops == NULL` need cdev `.poll` callbacks for devices such as `/dev/mouse`, `/dev/kbd`, TTY, and framebuffer-like devices.
   - For sockets, pipes, eventfd, and timerfd, check both `.poll` state and explicit `vfs_file_knote_notify()` calls.
5. Check ready-list behavior:
   - `kqueue_rescan_registered_locked()` should enqueue active, enabled, attached, unqueued knotes before sleeping.
   - `knote_enqueue()` should skip disabled, detached, or already queued knotes.
   - `kq->nready` must match the ready list; mismatches cause stuck or phantom readiness.
6. Check timeout and signal interactions:
   - Use `xv6-syscall <same-run-pid-or-name>` to verify whether `epoll_pwait` received `timeout=0`, a positive timeout, or `-1`.
   - If a positive timeout waiter remains asleep beyond its deadline, check whether `sched_timer_set()` failure is handled by immediate wake/self-timeout instead of entering an unarmed wait.
   - For `timeout_ms > 0`, pair with `xv6-kernel-timers` and inspect scheduler timer state.
   - For interrupted waits, inspect `signal_pending(current)` and signal delivery paths.
7. Separate readiness bugs from user-space event-loop bugs using the actual compositor/browser process, syscall arguments and fd graph. The normal desktop policy is maintained in `docs/active-work-plan.md`; an old generated `wlcomp` loop is not the current desktop contract.
8. If a freeze persists, route through `xv6-kernel-freeze-triage` and use this skill after the capture identifies kqueue/epoll/readiness.

## Relevant Files

- `kernel/kernel/kqueue/kqueue.c`
- `kernel/kernel/kqueue/kqueue_filters.c`
- `kernel/kernel/kqueue/epoll.c`
- `kernel/kernel/kqueue/kqueue_syscall.c`
- `kernel/kernel/inc/kqueue.h`
- `kernel/kernel/inc/kqueue_types.h`
- `kernel/kernel/inc/vfs/poll.h`
- `kernel/kernel/vfs/vfs_syscall.c`
- `kernel/kernel/vfs/pipe.c`
- `kernel/kernel/vfs/unix_socket.c`
- `kernel/kernel/vfs/eventfd.c`
- `kernel/kernel/vfs/timerfd.c`
- `kernel/kernel/lwip_port/sys_socket.c`
- `kernel/kernel/dev/ps2mouse.c`
- `kernel/kernel/dev/ps2kbd.c`
- `scripts/debug/xv6.gdb`

## Pitfalls

- Do not treat `CHAN=0` as proof that a thread is not sleeping on a kernel mechanism; timed waits can hide the channel.
- Do not add busy polling in user space until kqueue level-triggered readiness has been checked.
- Do not broaden kqueue locking casually; file/cdev poll must retain its unlocked callback and validated snapshot boundary.
- Do not call `vfs_file_knote_notify()` while holding locks that can invert with `file->knote_lock` or `kq->lock` unless the source path already documents that order.
- Do not free detached timer knotes until the timer path becomes cancelable.
- Test coalescing, one-shot rearm, edge transitions, duplicate fds and close/reuse explicitly; a successful basic wait is not full epoll conformance.
- Do not assume `poll(2)` and epoll have identical wake behavior: inspect each consumer's rescan/deadline path as well as explicit producer notifications.
