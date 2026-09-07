---
name: xv6-kernel-block-storage
description: 'Use when: debugging xv6-os block devices, bio, blkdev, request queues, iosched, gendisk, MBR/GPT partitions, virtio_disk, ramdisk, loop devices, or filesystem I/O hangs.'
argument-hint: 'Describe the block/storage symptom'
---

# xv6 Kernel Block Storage

## When to Use

- Disk reads/writes hang, complete out of order, corrupt data, or partitions are missing.
- VFS, xv6fs, ext4fs, page cache, or buffer heads point toward block I/O.
- You are adding or changing virtio, ramdisk, loop, SDHCI, gendisk, MBR, or GPT behavior.

## Source Map

- Block core: `kernel/kernel/dev/blkdev.c`, `kernel/kernel/dev/bio.c`, `kernel/kernel/dev/iosched.c`.
- Disk topology: `kernel/kernel/dev/gendisk.c`, `kernel/kernel/dev/mbr.c`, `kernel/kernel/dev/gpt.c`.
- Drivers: `kernel/kernel/virtio_disk.c`, `kernel/kernel/ramdisk.c`, `kernel/kernel/dev/loop.c`, `kernel/kernel/dev/x1_sdhci.c`.
- Legacy/shared bio: `kernel/kernel/bio.c`.
- Filesystem consumers: VFS, xv6fs, lwext4 port, page cache, buffer heads.

## Workflow

1. Separate request submission, scheduling, driver dispatch, completion, and filesystem consumption.
2. Verify block size, sector number, partition offset, and bio lifetime before suspecting the filesystem.
3. For cache incoherence, inspect `kernel/kernel/mm/pcache.c` and `kernel/kernel/mm/buffer.c` as well as the driver. Filesystems supply block mappings; generic data I/O constructs BIOs; block drivers execute them. Use [VFS data I/O](../xv6-kernel-vfs-data-io/SKILL.md) when the fault crosses those boundaries.
4. For virtio/QEMU, prefer current x86_64/QEMU driver behavior over xv6-tmp hardware notes.
5. Confirm completions wake every waiter and release all references.

## Pitfalls

- Filesystem bugs often surface as block I/O stalls, and block bugs often surface as VFS corruption.
- Partition offsets can make correct low-level reads look wrong at the filesystem layer.
- Do not sleep from completion paths that run in IRQ context.
