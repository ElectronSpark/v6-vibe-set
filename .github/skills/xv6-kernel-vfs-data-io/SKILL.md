---
name: xv6-kernel-vfs-data-io
description: 'Use when: implementing or debugging xv6-os regular-file data I/O separation, generic VFS/page-cache read/write helpers, address-space block mapping, readahead, writeback, mmap file faults, xv6fs/ext4fs data path conversion, or VFS_DATA_IO_TODO.md updates.'
argument-hint: 'Describe the regular-file data I/O change or bug'
---

# xv6 Kernel VFS Data I/O

## Use With

- `xv6-kernel-vfs-core` for syscall, fd, inode, file-position, and mmap dispatch.
- `xv6-kernel-filesystems` for xv6fs/ext4fs mapping, allocation, truncate, and metadata transactions.
- `xv6-kernel-memory-management` for pcache, folios, VM faults, writeback, and reclaim.
- `xv6-kernel-block-storage` for BIO, blkdev, iosched, virtio, ramdisk, loop, and partitions.

## Source Map

- Plan and progress tracker: `kernel/kernel/vfs/VFS_DATA_IO_TODO.md`.
- Public mapping contract: `kernel/kernel/inc/vfs/address_space.h`.
- VFS inode/file types: `kernel/kernel/inc/vfs/vfs_types.h`.
- VFS file dispatch: `kernel/kernel/vfs/file.c`, `vfs_syscall.c`.
- Page cache: `kernel/kernel/mm/pcache.c`, `kernel/kernel/inc/mm/pcache*.h`.
- mmap faults/writeback: `kernel/kernel/mm/vm.c`.
- xv6fs data paths: `kernel/kernel/vfs/xv6fs/file.c`, `superblock.c`, `truncate.c`.
- ext4 data paths: `kernel/kernel/lwext4_port/ext4fs_file.c`, `ext4fs_inode.c`.

## Migration Rules

1. Keep VFS core thin: syscall semantics, file position, access checks, object lifetime, and dispatch only.
2. Put regular-file byte transfer, pcache population, BIO construction, batching, readahead, writeback, mmap fault, prefault, writepage, EOF clamp, and hole zero-fill in generic VFS/page-cache code.
3. Keep filesystem drivers responsible for block mapping, allocation, exact size commit, truncate, metadata transactions, directory/symlink operations, and filesystem-specific metadata I/O.
4. Keep block drivers pure BIO executors. Do not push file offsets, EOF, sparse behavior, inode state, or user-copy into blkdev or hardware drivers.
5. Preserve lock ordering. xv6fs writes that allocate blocks must be able to begin a transaction before inode locking.
6. Do not remove existing filesystem-local data paths until generic helpers and the corresponding mapping hooks are tested for that filesystem.
7. After every implementation slice, update `VFS_DATA_IO_TODO.md` in the same change to mark completed items and record new follow-up items.

## Correctness Checks

- Pages become uptodate only after all required read BIOs complete.
- Dirty pages become clean only after all required write BIOs complete.
- Sparse holes read as zeroes and do not submit BIOs.
- Partial writes preserve untouched bytes and commit only successfully copied ranges.
- `fsync` flushes data, then filesystem metadata, then device cache when available.
- Truncate invalidates or clips cached pages before freeing blocks.
- `RWF_NOWAIT` returns `-EAGAIN` for cache misses, blocking I/O, allocation, transaction waits, or lock waits.

## Validation

- Build the kernel after interface changes.
- Run focused user programs as behavior moves: `bigfile`, `stressfs`, `grind`, `iovectest`, `mmaptest`, `mmapbigfile`, `dd`, `cp`, and `iobench`.
- Compare BIO counts and throughput before and after replacing filesystem-local paths.
