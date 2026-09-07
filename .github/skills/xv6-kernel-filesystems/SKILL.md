---
name: xv6-kernel-filesystems
description: 'Debug xv6-os filesystem backends: tmpfs, xv6fs, devtmpfs, procfs/sysfs and ext4 via lwext4, including mount operations, inode/file ops, truncation and orphan cleanup.'
argument-hint: 'Describe the filesystem or mount symptom'
---

# xv6 Kernel Filesystems

## When to Use

- A specific filesystem backend fails mount, lookup, read/write, truncate, stat, or cleanup.
- tmpfs memory use, xv6fs block allocation, devtmpfs device nodes, procfs views, or ext4/lwext4 behavior is wrong.
- VFS dispatch reaches backend inode/file/superblock operations and then misbehaves.

## Source Map

- tmpfs: `kernel/kernel/vfs/tmpfs/`.
- xv6fs: `kernel/kernel/vfs/xv6fs/`.
- devtmpfs: `kernel/kernel/vfs/devtmpfs/`.
- procfs: `kernel/kernel/vfs/procfs/`.
- sysfs: `kernel/kernel/vfs/sysfs/`.
- ext4: `kernel/kernel/lwext4/`, `kernel/kernel/lwext4_port/`.
- Generic VFS: `kernel/kernel/vfs/`.

## Workflow

1. Confirm generic VFS reached the expected filesystem operation with the expected inode/file state.
2. Check backend-specific refcounts, dirty state, truncation behavior, and orphan cleanup.
3. For disk filesystems, trace through page cache, buffer heads, and block I/O.
4. For devtmpfs, confirm device registration and cdev/blkdev lifetime first.
5. For procfs/sysfs, verify generated data and referenced process/device lifetimes remain valid while user reads and seeks; do not substitute a cached display value for live Linux discovery semantics.
6. For regular-file transfer refactors, use [VFS data I/O](../xv6-kernel-vfs-data-io/SKILL.md). Its mapping/write-lifecycle interface is separate from backend-owned metadata and transaction operations; old unchecked migration steps need a current source audit.

## Pitfalls

- ext4 support depends on the lwext4 adapter and may not cover every modern ext4 feature.
- tmpfs reclaim can be delayed by page pins and RCU destruction.
- devtmpfs exposes kernel devices; a visible node does not guarantee a working driver.
