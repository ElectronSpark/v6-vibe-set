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

- Block core: `kernel/kernel/dev/blkdev.c`, `bio.c`, `iosched.c`.
- Disk topology: `gendisk.c`, `mbr.c`, `gpt.c`.
- Drivers: `kernel/kernel/virtio_disk.c`, `ramdisk.c`, `kernel/kernel/dev/loop.c`, `x1_sdhci.c`.
- Legacy/shared bio: `kernel/kernel/bio.c`.
- Filesystem consumers: VFS, xv6fs, lwext4 port, page cache, buffer heads.

## Workflow

1. Separate request submission, scheduling, driver dispatch, completion, and filesystem consumption.
2. Verify block size, sector number, partition offset, and bio lifetime before suspecting the filesystem.
3. For cache incoherence, inspect `mm/pcache.c` and `mm/buffer.c` as well as the driver.
4. For virtio/QEMU, prefer current x86_64/QEMU driver behavior over xv6-tmp hardware notes.
5. Confirm completions wake every waiter and release all references.

## Pitfalls

- Filesystem bugs often surface as block I/O stalls, and block bugs often surface as VFS corruption.
- Partition offsets can make correct low-level reads look wrong at the filesystem layer.
- Do not sleep from completion paths that run in IRQ context.
