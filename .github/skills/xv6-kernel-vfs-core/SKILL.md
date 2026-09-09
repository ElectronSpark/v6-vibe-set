---
name: xv6-kernel-vfs-core
description: 'Use when: debugging xv6-os VFS, path lookup, mounts, unmounts, superblocks, inodes, dentries, files, fdtable, file locks, uio, pipes, unix sockets, eventfd, or timerfd.'
argument-hint: 'Describe the VFS symptom or file operation'
---

# xv6 Kernel VFS Core

## When to Use

- Open, close, read, write, stat, path lookup, mount, or unmount behaves incorrectly.
- File descriptors, inodes, dentries, superblocks, file locks, pipes, or special fds are involved.
- A filesystem backend works in isolation but fails through generic VFS paths.

## Source Map

- Source directory: `kernel/kernel/vfs/`.
- Object model there: `fs.c`, `inode.c`, `dcache.c`, `file.c`.
- Syscalls/fdtable there: `fdtable.c`, `vfs_syscall.c`.
- Design references: `kernel/kernel/vfs/VFS_DESIGN.md`, `kernel/kernel/vfs/UNMOUNT_DESIGN.md`; current source and the [active VFS plan](../../../docs/active-work-plan.md#vfs-data-io) supersede historical implementation examples.
- Helpers there: `uio.c`, `vfs_permission.c`, `file_lock.c`.
- Special fds there: `pipe.c`, `unix_socket.c`, `eventfd.c`, `timerfd.c`, `netlink.c`.

## Workflow

1. Identify the VFS object whose lifetime or state is wrong: fd, file, dentry, inode, superblock, or mount.
2. Follow refcounts and RCU/lazy destruction before freeing or reusing objects.
3. Preserve lock ordering where locks nest: superblock before inode before file. Destruction and allocation may need a filesystem transaction before those locks; retain the existing unlock/revalidate sequence instead of extending lock coverage across transaction waits.
4. For unmount bugs, check attached/syncing/unmounting flags and orphan lists under the superblock lock.
5. If the backend is the issue, route to `xv6-kernel-filesystems`; if storage is the issue, route to `xv6-kernel-block-storage`.

## Pitfalls

- VFS lifetime bugs often appear as kqueue, file descriptor, or filesystem corruption.
- Lazy unmount can leave valid open references after a path disappears.
- A destruction lock gap is not ownership transfer: preserve the destroying marker, drain in-flight references and confirm cache removal before final free. A concurrent remover must not cause a second free.
- Do not bypass generic permission, fdtable, or uio helpers without a clear reason.
