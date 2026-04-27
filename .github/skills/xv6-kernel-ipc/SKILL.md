---
name: xv6-kernel-ipc
description: 'Use when: debugging xv6-os System V IPC, message queues, semaphores, shared memory, IPC keys, IDs, permissions, sleeping senders/receivers, semop, shm attach, or IPC cleanup.'
argument-hint: 'Describe the IPC syscall or blocked operation'
---

# xv6 Kernel IPC

## When to Use

- `msgget`, send/receive, `semget`, `semop`, `shmget`, attach/detach, or IPC cleanup is wrong.
- Processes block on IPC queues or wake the wrong peer.
- Shared memory interacts with VM, process exit, or fork/clone behavior.

## Source Map

- Shared utilities: `kernel/kernel/ipc/ipc_util.c`, `kernel/kernel/inc/ipc.h`.
- Messages: `kernel/kernel/ipc/msg.c`.
- Semaphores: `kernel/kernel/ipc/sem.c`.
- Shared memory: `kernel/kernel/ipc/shm.c`.
- Related modules: proc sleep/wakeup, VM, fd/process lifecycle.

## Workflow

1. Check key/ID lookup and object lifetime before changing operation semantics.
2. For blocked senders/receivers or semops, inspect wait queues and wake conditions.
3. For shared memory, verify VM mappings, attach counts, detach on exit, and object removal rules.
4. Review permission-like checks and cleanup paths together.
5. Pair with `xv6-kernel-sleep-wakeup` for missed wakeups and `xv6-kernel-memory-management` for shm mapping bugs.

## Pitfalls

- IPC IDs can outlive processes; cleanup must handle delayed destruction.
- Shared memory bugs often appear as generic VM faults.
- Queue wakeups must match the precise condition that made the operation possible.
