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

- Object model: `kernel/kernel/vfs/fs.c`, `inode.c`, `dcache.c`, `file.c`.
- Syscalls/fdtable: `fdtable.c`, `vfs_syscall.c`.
- Design docs: `VFS_DESIGN.md`, `UNMOUNT_DESIGN.md`.
- Helpers: `uio.c`, `vfs_permission.c`, `file_lock.c`.
- Special fds: `pipe.c`, `unix_socket.c`, `eventfd.c`, `timerfd.c`, `netlink.c`.

## Workflow

1. Identify the VFS object whose lifetime or state is wrong: fd, file, dentry, inode, superblock, or mount.
2. Follow refcounts and RCU/lazy destruction before freeing or reusing objects.
3. Preserve lock ordering: superblock before inode before file.
4. For unmount bugs, check attached/syncing/unmounting flags and orphan lists under the superblock lock.
5. If the backend is the issue, route to `xv6-kernel-filesystems`; if storage is the issue, route to `xv6-kernel-block-storage`.

## Pitfalls

- VFS lifetime bugs often appear as kqueue, file descriptor, or filesystem corruption.
- Lazy unmount can leave valid open references after a path disappears.
- Do not bypass generic permission, fdtable, or uio helpers without a clear reason.
