---
name: xv6-kernel-device-core
description: 'Use when: working on xv6-os device registration, dev numbers, cdev read/write/ioctl/poll, devtmpfs exposure, null/random devices, framebuffer cdevs, or generic device lifecycle.'
argument-hint: 'Describe the device registration or cdev symptom'
---

# xv6 Kernel Device Core

## When to Use

- A device node is missing, opens the wrong implementation, or its cdev operations are not called.
- You are adding a character device, ioctl, poll callback, or devtmpfs-visible device.
- Device lifetime, major/minor numbers, or registration order is suspect.

## Source Map

- Registry: `kernel/kernel/dev/dev.c`, `kernel/kernel/inc/dev/dev*.h`.
- Character devices: `kernel/kernel/dev/cdev.c`, `kernel/kernel/inc/dev/cdev.h`.
- Simple drivers: `kernel/kernel/dev/nullrand.c`, `fb.c`, input cdevs, TTY registration.
- Filesystem exposure: `kernel/kernel/vfs/devtmpfs/`.

## Workflow

1. Confirm registration happens before devtmpfs or user-space lookup needs the device.
2. Check device number, name, ops table, private data, and cleanup path.
3. For readiness issues, make `.poll` level-triggered and route to `xv6-kernel-event-wait`.
4. For block devices, switch to `xv6-kernel-block-storage`; for NICs, switch to `xv6-kernel-network-devices`.
5. Validate open/read/write/ioctl behavior through VFS file operations, not only the cdev driver.

## Pitfalls

- Missing `.poll` can look like an epoll/kqueue bug.
- Device lifetime must outlive open file references.
- Do not assume devtmpfs visibility proves the driver backend is initialized.
