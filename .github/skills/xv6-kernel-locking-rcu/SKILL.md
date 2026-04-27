---
name: xv6-kernel-locking-rcu
description: 'Use when: debugging xv6-os spinlocks, rwlocks, mutexes, rwsems, semaphores, completions, RCU, lock ordering, IRQ-safe locking, sleeping locks, or lifetime races.'
argument-hint: 'Describe the lock, deadlock, or lifetime issue'
---

# xv6 Kernel Locking And RCU

## When to Use

- A deadlock, lock inversion, IRQ lock misuse, or lifetime race is suspected.
- You are adding locks around scheduler, VFS, kqueue, device, MM, or RCU-protected state.
- RCU callbacks do not run, run too early, or objects disappear while readers exist.

## Source Map

- IRQ-safe locks: `kernel/kernel/lock/spinlock.c`, `rwlock.c`.
- Sleeping locks: `mutex.c`, `rwsem.c`, `semaphore.c`.
- One-shot waits: `completion.c`.
- RCU: `rcu.c`, `RCU_README.md`, `kernel/kernel/inc/lock/rcu*.h`.

## Workflow

1. Identify context first: hard IRQ, soft/deferred work, scheduler, process context, or early boot.
2. Use spin/rw locks for IRQ-safe critical sections and sleeping locks only where blocking is allowed.
3. Write down lock order across modules before changing nested VFS, kqueue, MM, or scheduler paths.
4. For RCU, check read-side critical sections, grace-period detection, callback queues, and object free path.
5. Pair with the subsystem skill for object-specific invariants.

## Pitfalls

- VFS lock order is `superblock_lock` before `inode_lock` before `file_lock`.
- Event callbacks under kqueue locks are a lock-order risk.
- RCU protects lifetime, not arbitrary mutable state consistency.
