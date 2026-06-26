# Linux GUI File Inventory

Last updated: 2026-06-26.

This is an inventory-only milestone for the active GUI/kernel cleanup goal. It
records the current files in the checkout that are in scope for KDE/Qt,
KWin/Plasma, Chromium, WebKit, Wayland/X11, DRM, and related Linux GUI ABI
work. It does not prescribe a runtime, build, or validation procedure.

Ownership tags:

- `primary-kernel`: kernel files that should own Linux-compatible ABI behavior.
- `support-control-probe`: xv6-owned tests, probes, wrappers, staging glue, or
  package boundaries.
- `runtime-data`: rootfs/sysroot data or generated package inventory.
- `upstream-payload`: imported source or package payload that should stay
  upstream-clean.
- `probe-only`: host/imported GUI applications or harnesses used to expose ABI
  gaps; do not patch them to mask kernel or sysroot defects.

Kernel-preferred ABI ownership is the rule for behavioral compatibility:
reduce KDE/Qt/KWin/Plasma/Chromium/WebKit failures to Linux ABI gaps and fix
them in kernel ABI, libc/sysroot behavior, Linux headers, rootfs data,
build/staging wrappers, or local xv6-owned shims. Imported KDE, Qt, KWin,
Plasma, Chromium, and WebKit source must remain upstream-clean except
marker-only xv6 metadata. Weston is regression/control only for the current KDE
direction.

Evidence used for this refresh: current-tree `find`, `rg --files`, and
`git status --short` checks across the root repo plus `kernel`, `user`, and
`ports`. No builds or VM launches are required by this inventory.

Current highest-priority evidence path from the available runs:
`build-x86_64/kde-plasma-desktop-smoke-history/20260626-043022-x11-glx-fps-probe-start-timeout/`.
The old unqualified Xwayland automatic GLAMOR startup failure is closed by the
xv6-owned wrapper policy that maps `kde_xwayland_glamor=auto` to effective
`-glamor es` for focused runs, and the old `x11-glx-fps-display-env-timeout`
harness failure is gone. The remaining KDE/Xwayland gap is now after the
one-shot GLX FPS probe command is issued: preflight selects `DISPLAY=:0`, the
launch-status wait begins in `glx-fps` mode, then times out before
`phase=start`. Prioritize probe process startup, loader/stdio behavior, or
guest command execution around GLX mode before making Mesa/kernel root-cause
claims. This is a current prioritization note, not a solution plan.

## Work Ownership Split

Legitimate kernel work: files tagged `primary-kernel`, especially DRM/KMS,
virtgpu/virgl, framebuffer, Hyper-V/DXG diagnostics, input, audio, network,
process, VM, VFS, procfs/sysfs/tmpfs, AF_UNIX, poll/epoll, and TTY surfaces.
These are the right place to close Linux ABI and performance gaps exposed by
KDE, Xwayland, Chromium, WebKit, or other GUI probes.

Legitimate sysroot/rootfs/build work: port wrapper `CMakeLists.txt` files,
local xv6 shims, Linux/Khronos/header package boundaries, generated overlay
staging scripts, rootfs overlay policy/data, and harness scripts. These may
stage or configure runtime payloads, but should not carry behavioral patches to
imported GUI source unless a local shim is explicitly listed here.

Probe-only and upstream-clean areas: imported `ports/*/src` trees, generated
KDE/Qt/Plasma/Chrome/WebKit runtime payloads, host GUI applications, Weston
source, Chromium/WebKit/KDE/Qt/KWin/Plasma package contents, and `scripts/gpu`
proof harnesses. Use them to reveal ABI gaps and validate fixes; keep upstream
payload source clean except marker-only xv6 metadata.

## Dependency-Aware Group Metadata

This table is the dependency-aware index for the detailed file lists below. It
is ordered kernel-first, then sysroot/rootfs/scripts/probes, then
ports/userland.

| Group | Owner | Kind | Why in scope | Duplication/generalization candidates |
| --- | --- | --- | --- | --- |
| Kernel DRM/KMS/GEM/Sync/Virtgpu | Kernel | DRM/fb/virtgpu/KMS/BO/sync/dma-buf ABI | Owns `/dev/dri`, KMS/atomic, GEM, PRIME, syncobj, virgl, GLX/DRI3, and scanout semantics exposed by KDE/KWin/Xwayland/Qt/Chromium. | Keep fb/DRM/virtgpu ioctl translation centralized; avoid duplicating BO/fence/fd lifetime logic between DRM, fb, virgl, and probes. |
| Hyper-V/DXG/GPU-P | Kernel | Hyper-V VMBus/vPCI/DXG/D3DKMT/GPU diagnostics | GUI-adjacent GPU transport and native-present evidence path; current KDE/virgl target treats it as out of scope unless explicitly reopened. | Keep DXG object lifetime and present-bind diagnostics in Hyper-V/fb boundaries; do not clone WSL object models as flat metadata blobs. |
| Input | Kernel | evdev, virtio-input, PS/2, seat-visible input | Wayland, KDE, Xwayland, Chromium, and probes require Linux-shaped input devices and readiness. | Generalize event-device identity and capability reporting across evdev, PS/2, virtio-input, and libinput probes. |
| Audio | Kernel | OSS/minimal ALSA-facing playback, virtio-snd | KDE/Plasma, Chromium, WebKit, PipeWire/Pulse, and audio smokes need stable playback enumeration and poll behavior. | Share poll/readiness and device capability paths between OSS, virtio-snd, ALSA probes, and rootfs audio defaults. |
| Process/Futex/Scheduler/Signals | Kernel | clone, futex, pidfd, signal, wait, scheduler, timer | GUI stacks are multi-process and event-loop heavy; missed waits or lifecycle mismatches break launch, IPC, teardown, and frame cadence. | Consolidate Linux lifecycle semantics in proc/futex/pidfd/wait/signal code instead of per-app launch workarounds. |
| VM/Mmap/Pcache/OOM | Kernel | mmap, mprotect, shared memory, page cache, reclaim | GUI payloads depend on file-backed mappings, shared memory, dma-buf mmap, executable/library loading, and memory pressure behavior. | Keep mmap/protection and page-cache behavior shared between tmpfs, file-backed mappings, DRI/BO mappings, and ELF loading. |
| VFS/Socket/Event/Procfs | Kernel | fd tables, VFS, procfs/sysfs/tmpfs, AF_UNIX, eventfd/timerfd, poll/epoll | D-Bus, Wayland, X11, KDE services, Chromium/WebKit IPC, and desktop discovery stress Linux fd, socket, procfs, and readiness semantics. | Generalize fd identity, SCM ancillary handling, poll/epoll readiness, and procfs formatting rather than fixing individual apps. |
| Network/TTY | Kernel | TCP/socket/lwIP glue, netlink, PTY/TTY/termios/session | Browsers and KDE services need fetch paths, terminal/session behavior, and service reachability. | Keep socket readiness/error semantics and terminal session behavior reusable for Chromium, WebKit, Konsole, X11, and probes. |
| Supporting Kernel Storage/I/O | Kernel | block, disk, filesystem support | Rootfs, package payloads, runtime data, and shared libraries depend on reliable image and filesystem I/O. | Keep disk and cache diagnostics shared with VFS/page-cache work; avoid GUI-specific storage exceptions. |
| Sysroot/Rootfs/Scripts/Probes | Rootfs/sysroot/scripts | generated runtime data, image staging, launch wrappers, smoke harnesses, imported host probes | Stages KDE/Qt/Plasma, Xwayland, WebKit, Chromium probes, DBus, ALSA, MIME, and reproducible evidence without patching upstream GUI source. | Collapse duplicated host-X11/Wayland smoke launcher patterns where practical; keep wrappers as xv6-owned probes and rootfs data, not app patches. |
| Ports/Userland Package Boundaries | Ports/userland | CMake wrappers, imported source boundaries, local shims, user probes | Supplies libraries, headers, Wayland/Mesa/X11/WebKit/NetSurf/KDE payload boundaries, and local reducers used to expose ABI gaps. | Keep package wrappers declarative; prefer local shims or kernel ABI fixes over source patches to KDE/Qt/Plasma/Chromium/WebKit/Weston. |

## Kernel DRM/KMS/GEM/Sync/Virtgpu

Responsibility: `/dev/dri`, KMS/atomic, GEM/BO/dma-buf, syncobj, PRIME,
scanout, virgl, Nouveau diagnostics, and the DRM ioctl surface used by
KDE/KWin/Qt/Chromium.

Tag: `primary-kernel`.

```text
kernel/kernel/dev/drm_core.c
kernel/kernel/inc/dev/drm_core.h
kernel/kernel/inc/dev/fb.h
kernel/kernel/inc/dev/dma_fence.h
kernel/kernel/inc/uabi/drm.h
kernel/kernel/dev/fb/module.c
kernel/kernel/dev/fb/dma_fence.c
kernel/kernel/dev/fb/fb_common.c
kernel/kernel/dev/fb/fb_internal.c
kernel/kernel/dev/fb/fb_device_ioctl.c
kernel/kernel/dev/fb/fb_scanout.c
kernel/kernel/dev/fb/fb_init_panic.c
kernel/kernel/dev/fb/fb_bo_shmem_dmabuf.c
kernel/kernel/dev/fb/fb_fd_sync.c
kernel/kernel/dev/fb/fb_drm_core_kms.c
kernel/kernel/dev/fb/fb_drm_dispatch.c
kernel/kernel/dev/fb/fb_kms_atomic.c
kernel/kernel/dev/fb/fb_drm_kms_objects.c
kernel/kernel/dev/fb/fb_drm_kms_properties.c
kernel/kernel/dev/fb/fb_drm_kms_atomic_props.c
kernel/kernel/dev/fb/fb_syncobj_prime_virtgpu.c
kernel/kernel/dev/fb/fb_nouveau.c
kernel/kernel/dev/fb/fb_dxg_present.c
kernel/kernel/dev/fb/fb_non_x86.c
kernel/kernel/virtio_gpu.c
kernel/kernel/virtio_gpu_3d.c
kernel/kernel/virtio_gpu_present.c
kernel/kernel/virtio_gpu_resource.c
kernel/kernel/virtio_gpu_scanout.c
kernel/kernel/virtio_gpu_user.c
kernel/kernel/inc/dev/virtio.h
```

## Hyper-V/DXG Out-Of-Scope-But-GUI

Responsibility: Hyper-V VMBus/vPCI/DXG/D3DKMT/GPU-P transport, diagnostics,
handle/object lifetime, shared resources, and fail-closed native-present
boundaries. These files are GUI/GPU ownership, but Hyper-V DXG/GPU-P
native-present and Nouveau/DDA real-hardware work are out of scope for the
current KVM/virgl KDE target unless explicitly reopened.

Tag: `primary-kernel`, current KDE path `out-of-scope`.

The former `kernel/kernel/dev/hyperv/hyperv_dxg_*.c` pattern is expanded to the
current concrete files:

```text
kernel/kernel/dev/hyperv/module.c
kernel/kernel/dev/hyperv/hyperv_common.c
kernel/kernel/dev/hyperv/hyperv_defs_state.c
kernel/kernel/dev/hyperv/hyperv_vmbus_core.c
kernel/kernel/dev/hyperv/hyperv_vpci_config.c
kernel/kernel/dev/hyperv/hyperv_synth_devices.c
kernel/kernel/dev/hyperv/hyperv_init_public.c
kernel/kernel/dev/hyperv/hyperv_non_x86.c
kernel/kernel/dev/hyperv/hyperv_dxg_allocations.c
kernel/kernel/dev/hyperv/hyperv_dxg_device.c
kernel/kernel/dev/hyperv/hyperv_dxg_diag.c
kernel/kernel/dev/hyperv/hyperv_dxg_handle_manager.c
kernel/kernel/dev/hyperv/hyperv_dxg_ioctl_queryadapter.c
kernel/kernel/dev/hyperv/hyperv_dxg_ioctls.c
kernel/kernel/dev/hyperv/hyperv_dxg_memory.c
kernel/kernel/dev/hyperv/hyperv_dxg_pci_version.c
kernel/kernel/dev/hyperv/hyperv_dxg_queryadapter_hwid.c
kernel/kernel/dev/hyperv/hyperv_dxg_shared_objects.c
kernel/kernel/dev/hyperv/hyperv_dxg_state.c
kernel/kernel/dev/hyperv/hyperv_dxg_status_device.c
kernel/kernel/inc/uabi/d3dkmthk.h
```

## Input

Responsibility: evdev-style keyboard, pointer, tablet, virtio-input, PS/2
fallback, and seat discovery behavior consumed by Wayland, KDE, and Chromium.

Tag: `primary-kernel`.

```text
kernel/kernel/dev/evdev.c
kernel/kernel/inc/dev/evdev.h
kernel/kernel/dev/ps2kbd.c
kernel/kernel/inc/dev/ps2kbd.h
kernel/kernel/dev/ps2mouse.c
kernel/kernel/inc/dev/ps2mouse.h
kernel/kernel/virtio_input.c
```

## Audio

Responsibility: OSS `/dev/dsp`, minimal ALSA-facing playback enumeration,
virtio-snd, and GUI audio smoke support.

Tag: `primary-kernel`.

```text
kernel/kernel/dev/ossaudio.c
kernel/kernel/virtio_snd.c
kernel/kernel/inc/dev/virtio.h
```

## Process/Futex/Scheduler/Signals

Responsibility: `clone`, thread groups, futexes, pidfds, signals, waits,
process lifecycle, scheduler latency, timer cadence, timeout behavior,
poll/timer wakeups, and Chromium/KDE/WebKit process graph behavior. GUI event
loops, network/audio pacing, compositor frame cadence, and large browser
process graphs depend on this layer not introducing avoidable latency or
missed wakeups.

Tag: `primary-kernel`.

```text
kernel/kernel/accounting.c
kernel/kernel/coredump.c
kernel/kernel/exec.c
kernel/kernel/proc/clone.c
kernel/kernel/proc/exit.c
kernel/kernel/proc/futex.c
kernel/kernel/proc/pid.c
kernel/kernel/proc/pidfd.c
kernel/kernel/proc/pgroup.c
kernel/kernel/proc/proc_private.h
kernel/kernel/proc/rq.c
kernel/kernel/proc/sched.c
kernel/kernel/proc/sched_eevdf.c
kernel/kernel/proc/sched_fifo.c
kernel/kernel/proc/sched_idle.c
kernel/kernel/proc/sig_trampoline.S
kernel/kernel/proc/signal.c
kernel/kernel/proc/swtch.S
kernel/kernel/proc/sys_misc.c
kernel/kernel/proc/sys_signal.c
kernel/kernel/proc/sysproc.c
kernel/kernel/proc/thread.c
kernel/kernel/proc/thread_group.c
kernel/kernel/proc/thread_queue.c
kernel/kernel/proc/workqueue.c
kernel/kernel/timer/goldfish_rtc.c
kernel/kernel/timer/sched_timer.c
kernel/kernel/timer/timer.c
kernel/kernel/inc/signal.h
kernel/kernel/inc/signal_types.h
kernel/kernel/inc/proc/chrome_lifecycle.h
kernel/kernel/inc/proc/pgroup.h
kernel/kernel/inc/proc/pgroup_types.h
kernel/kernel/inc/proc/rq.h
kernel/kernel/inc/proc/rq_types.h
kernel/kernel/inc/proc/sched.h
kernel/kernel/inc/proc/tq.h
kernel/kernel/inc/proc/tq_type.h
kernel/kernel/inc/proc/thread_group.h
kernel/kernel/inc/proc/thread_group_types.h
kernel/kernel/inc/proc/thread.h
kernel/kernel/inc/proc/thread_types.h
kernel/kernel/inc/proc/workqueue.h
kernel/kernel/inc/proc/workqueue_types.h
kernel/kernel/inc/timer/goldfish_rtc.h
kernel/kernel/inc/timer/sched_timer_private.h
kernel/kernel/inc/timer/timer.h
kernel/kernel/inc/timer/timer_types.h
kernel/kernel/inc/uabi/signal.h
kernel/arch/x86_64/inc/arch_thread.h
kernel/arch/x86_64/irq/syscall.c
kernel/arch/x86_64/irq/trap.c
kernel/kernel/irq/trap.c
```

## VM/Mmap/Pcache/OOM

Responsibility: mmap/mprotect/munmap/mremap behavior, shared memory,
file-backed mappings, ELF mappings, dma-buf mmap, page cache, reclaim, and
large GUI process memory pressure. Arch VM/TLB synchronization is included
because large GUI processes rely on mmap, mprotect, munmap, and page-table
changes becoming visible consistently across CPUs after TLB shootdowns.

Tag: `primary-kernel`.

```text
kernel/arch/x86_64/mm/vm.c
kernel/arch/x86_64/ipi/ipi.c
kernel/kernel/mm/vm.c
kernel/kernel/mm/sysmm.c
kernel/kernel/mm/pcache.c
kernel/kernel/mm/mm_watermark.c
kernel/kernel/mm/oom_kill.c
kernel/kernel/mm/buffer.c
kernel/kernel/mm/early_allocator.c
kernel/kernel/mm/folio.c
kernel/kernel/mm/kalloc.c
kernel/kernel/mm/page.c
kernel/kernel/mm/page_private.h
kernel/kernel/mm/rmap.c
kernel/kernel/mm/shrinker.c
kernel/kernel/mm/slab.c
kernel/kernel/mm/slab_private.h
kernel/kernel/inc/arch/vm.h
kernel/kernel/inc/mm/buffer_head.h
kernel/kernel/inc/mm/early_allocator.h
kernel/kernel/inc/mm/folio.h
kernel/kernel/inc/mm/folio_types.h
kernel/kernel/inc/mm/mm_watermark.h
kernel/kernel/inc/mm/vm.h
kernel/kernel/inc/mm/vm_types.h
kernel/kernel/inc/mm/oom_kill.h
kernel/kernel/inc/mm/page.h
kernel/kernel/inc/mm/page_type.h
kernel/kernel/inc/mm/pcache.h
kernel/kernel/inc/mm/pcache_types.h
kernel/kernel/inc/mm/pgtable.h
kernel/kernel/inc/mm/rmap.h
kernel/kernel/inc/mm/shrinker.h
kernel/kernel/inc/mm/slab.h
kernel/kernel/inc/mm/slab_type.h
kernel/kernel/inc/uabi/mman.h
kernel/kernel/inc/uabi/memstat.h
kernel/kernel/lwext4_port/ext4fs_file.c
```

## VFS/Procfs/Sysfs/Tmpfs/AF_UNIX/Poll/Epoll

Responsibility: fd tables, file identity, procfs, sysfs, devtmpfs, tmpfs,
pipes, eventfd, timerfd, `fcntl`, AF_UNIX/SCM_RIGHTS/SCM_CREDENTIALS,
D-Bus/Wayland/X11 sockets, and poll/epoll/kqueue readiness.

Tag: `primary-kernel`.

The previous `kernel/kernel/vfs/tmpfs/*` pattern is expanded to the current
concrete top-level tmpfs files because tmpfs is a small self-contained
subsystem in this checkout.

```text
kernel/kernel/vfs/fdtable.c
kernel/kernel/vfs/file.c
kernel/kernel/vfs/address_space.c
kernel/kernel/vfs/dcache.c
kernel/kernel/vfs/file_lock.c
kernel/kernel/vfs/fs.c
kernel/kernel/vfs/inode.c
kernel/kernel/vfs/pipe.c
kernel/kernel/vfs/uio.c
kernel/kernel/vfs/vfs_permission.c
kernel/kernel/vfs/vfs_private.h
kernel/kernel/vfs/vfs_syscall.c
kernel/kernel/vfs/unix_socket.c
kernel/kernel/vfs/eventfd.c
kernel/kernel/vfs/timerfd.c
kernel/kernel/vfs/tmpfs/CMakeLists.txt
kernel/kernel/vfs/tmpfs/file.c
kernel/kernel/vfs/tmpfs/inode.c
kernel/kernel/vfs/tmpfs/superblock.c
kernel/kernel/vfs/tmpfs/tmpfs_private.h
kernel/kernel/vfs/tmpfs/tmpfs_smoketest.c
kernel/kernel/vfs/tmpfs/tmpfs_smoketest.h
kernel/kernel/vfs/tmpfs/truncate.c
kernel/kernel/vfs/devtmpfs/CMakeLists.txt
kernel/kernel/vfs/devtmpfs/devtmpfs_private.h
kernel/kernel/vfs/devtmpfs/superblock.c
kernel/kernel/vfs/procfs/file.c
kernel/kernel/vfs/procfs/inode.c
kernel/kernel/vfs/procfs/superblock.c
kernel/kernel/vfs/procfs/procfs_private.h
kernel/kernel/vfs/sysfs/file.c
kernel/kernel/vfs/sysfs/inode.c
kernel/kernel/vfs/sysfs/superblock.c
kernel/kernel/vfs/sysfs/sysfs_private.h
kernel/kernel/inc/uabi/fcntl.h
kernel/kernel/inc/vfs/fcntl.h
kernel/kernel/inc/vfs/pipe.h
kernel/kernel/inc/vfs/pipe_types.h
kernel/kernel/inc/vfs/unix_socket.h
kernel/kernel/inc/devtmpfs.h
kernel/kernel/inc/kqueue.h
kernel/kernel/inc/kqueue_types.h
kernel/kernel/kqueue/epoll.c
kernel/kernel/kqueue/kqueue.c
kernel/kernel/kqueue/kqueue_filters.c
kernel/kernel/kqueue/kqueue_syscall.c
kernel/kernel/inc/uabi/poll.h
kernel/kernel/inc/vfs/poll.h
```

## Network/TTY

Responsibility: sockets, TCP/DNS paths, lwIP glue, PTY/TTY/termios/session
behavior, Chromium/WebKit fetch paths, D-Bus service reachability, and
Konsole/terminal support.

Tag: `primary-kernel`. The xv6-owned network device/socket glue and UAPI
headers below are legitimate kernel work. The imported lwIP source tree is a
network ABI dependency but should be treated like upstream payload unless a
change is isolated to the xv6 port glue.

```text
kernel/kernel/e1000.c
kernel/kernel/inc/dev/e1000_dev.h
kernel/kernel/dev/netdev.c
kernel/kernel/inc/dev/net.h
kernel/kernel/inc/dev/netconf.h
kernel/kernel/inc/dev/netdev.h
kernel/kernel/virtio_net.c
kernel/kernel/net.c
kernel/kernel/sysnet.c
kernel/kernel/vfs/netlink.c
kernel/kernel/inc/netlink.h
kernel/kernel/lwip_port/lwip_glue.c
kernel/kernel/lwip_port/sys_socket.c
kernel/kernel/lwip_port/sys_arch.c
kernel/kernel/lwip_port/CMakeLists.txt
kernel/kernel/lwip_port/arch/cc.h
kernel/kernel/lwip_port/arch/sys_arch.h
kernel/kernel/lwip_port/compat/stdint.h
kernel/kernel/lwip_port/compat/stdio.h
kernel/kernel/lwip_port/compat/stdlib.h
kernel/kernel/lwip_port/compat/time.h
kernel/kernel/lwip_port/lwipopts.h
kernel/kernel/tty/ptmx.c
kernel/kernel/tty/pty.c
kernel/kernel/tty/session.c
kernel/kernel/tty/termios.c
kernel/kernel/tty/tty.c
kernel/kernel/tty/tty_dev.c
kernel/kernel/inc/tty/session.h
kernel/kernel/inc/tty/session_types.h
kernel/kernel/inc/tty/termios.h
kernel/kernel/inc/tty/tty.h
kernel/kernel/inc/tty/tty_types.h
kernel/kernel/inc/uabi/termios.h
```

Imported lwIP source is intentionally not expanded file-by-file here:

```text
kernel/kernel/lwip/
```

## Supporting Kernel Storage/I/O

Responsibility: rootfs, package payload, executable/library, cache, and disk
image I/O that GUI workloads depend on while launching, paging file-backed
content, loading runtime data, and preserving filesystem integrity. This is a
supporting kernel path rather than a GUI ABI endpoint, but disk regressions can
break KDE/Qt/Chromium/WebKit bring-up and must remain visible in this
inventory.

Tag: `primary-kernel`, supporting I/O.

```text
kernel/kernel/virtio_disk.c
kernel/kernel/dev/iosched.c
kernel/kernel/inc/dev/iosched.h
kernel/kernel/inc/dev/iosched_types.h
```

## Current Tree Cross-Check

Before this documentation edit, `git status --short` reported no local source
changes in the root repo, `kernel`, `user`, or `ports`. Keep using the
ownership sections as the source of truth for GUI or GUI-support relevance when
future dirty state appears; the disk path remains tracked as supporting
critical I/O because rootfs/image I/O must not regress even though it is not
itself a display, input, or GUI syscall ABI file.

## User/Local GUI Probes

Responsibility: xv6-owned commands, tests, and reducers. These can be
xv6-specific only when they are explicit probes or local commands. They should
not hide missing Linux ABI behavior from imported GUI programs.

Tag: `support-control-probe`.

Local user libraries:

```text
user/lib/fsutil.c
user/lib/fsutil.h
user/lib/host_compat.h
user/lib/initcode.S
user/lib/printf.c
user/lib/ulib.c
user/lib/umalloc.c
user/lib/user.h
user/lib/user.ld
user/lib/usys.pl
user/lib/x86_64/initcode.S
user/lib/x86_64/user.ld
user/lib/x86_64/usys_x86_64.pl
```

Production commands, filesystem diagnostics, stress tests, and ABI/GUI probes
currently identified by the KDE inventory:

```text
user/programs/alsapcmpoll/alsapcmpoll.c
user/programs/bigfile/bigfile.c
user/programs/blocksendwake/blocksendwake.c
user/programs/cat/cat.c
user/programs/cloexectest/cloexectest.c
user/programs/clonetest/clonetest.c
user/programs/cowtest/cowtest.c
user/programs/cp/cp.c
user/programs/crashtest/crashtest.c
user/programs/d3d12probe/README.md
user/programs/d3d12probe/build-host.sh
user/programs/d3d12probe/d3d12probe.cpp
user/programs/dd/dd.c
user/programs/devtest/devtest.c
user/programs/dh/dh.c
user/programs/dnsstress/dnsstress.c
user/programs/dontwaitsend/dontwaitsend.c
user/programs/drmabitest/drmabitest.c
user/programs/drmiftest/drmiftest.c
user/programs/drmprimeprobe/drmprimeprobe.c
user/programs/dumpchan/dumpchan.c
user/programs/dumpinode/dumpinode.c
user/programs/dumppcache/dumppcache.c
user/programs/dumprq/dumprq.c
user/programs/dxgprobe/dxgprobe.c
user/programs/echo/echo.c
user/programs/fbstat/fbstat.c
user/programs/fdtabletest/fdtabletest.c
user/programs/find/find.c
user/programs/forktest/forktest.c
user/programs/free/free.c
user/programs/gldemo/gldemo.c
user/programs/gpubuftest/gpubuftest.c
user/programs/gpucorevalidate/gpucorevalidate.c
user/programs/grep/grep.c
user/programs/grind/grind.c
user/programs/init/init.c
user/programs/iobench/iobench.c
user/programs/iovectest/iovectest.c
user/programs/keyinject/keyinject.c
user/programs/kill/kill.c
user/programs/kprofile/kprofile.c
user/programs/kqueuetest/kqueuetest.c
user/programs/linuxsyscallabitest/linuxsyscallabitest.c
user/programs/ln/ln.c
user/programs/losetup/losetup.c
user/programs/lsblk/lsblk.c
user/programs/mkdir/mkdir.c
user/programs/mkfs_xv6fs/mkfs_xv6fs.c
user/programs/mknod/mknod.c
user/programs/mmapbigfile/mmapbigfile.c
user/programs/mmaptest/mmaptest.c
user/programs/mount/mount.c
user/programs/mouseinject/mouseinject.c
user/programs/mousetest/mousetest.c
user/programs/mv/mv.c
user/programs/nouveauabitest/nouveauabitest.c
user/programs/pingpong/pingpong.c
user/programs/pngtest/pngtest.c
user/programs/preempttest/preempttest.c
user/programs/primes/primes.c
user/programs/ps/ps.c
user/programs/randtest/randtest.c
user/programs/reboot/reboot.c
user/programs/regpreservetest/regpreservetest.c
user/programs/rm/rm.c
user/programs/sh/sh.c
user/programs/shutdown/shutdown.c
user/programs/sleep/sleep.c
user/programs/stressfs/stressfs.c
user/programs/symlinktest/symlinktest.c
user/programs/sync/sync.c
user/programs/syscalltest/syscalltest.c
user/programs/tcpstress/tcpstress.c
user/programs/testsig/testsig.c
user/programs/timerdemo/timerdemo.c
user/programs/timerfdstress/timerfdstress.c
user/programs/top/top.c
user/programs/ttmtest/ttmtest.c
user/programs/umount/umount.c
user/programs/usertests/usertests.c
user/programs/vforktest/vforktest.c
user/programs/virgltest/virgltest.c
user/programs/waitgdb/waitgdb.c
user/programs/wallclock/wallclock.c
user/programs/wc/wc.c
user/programs/webkitabitest/webkitabitest.c
user/programs/webkitnettest/webkitnettest.c
user/programs/xargs/xargs.c
user/programs/zombie/zombie.c
```

`user/programs/alsapcmpoll/alsapcmpoll.c` is a local audio probe relevant to
GUI audio ABI work, although it is not listed in
`docs/linux-gui-kde-inventory.md`.

`user/programs/regpreservetest/regpreservetest.c` is tracked in `user` and
remains inventoried as a local support-control probe.

## Ports And Package Boundaries

Responsibility: package wrapper, imported source, local shim, and generated
runtime payload boundaries. Imported source should remain upstream-clean; build
and staging adaptation belongs in wrapper files, sysroot/header data, rootfs
data, or explicitly local xv6-owned shims.

Tag: `support-control-probe` for wrappers/local shims,
`upstream-payload` for imported source.

Base Linux ABI/sysroot package wrappers:

```text
ports/cmake/AddPort.cmake
ports/cmake/ExposePkgConfigLibs.cmake
ports/cmake/apply_patches.sh
ports/linux-uapi-headers/CMakeLists.txt
ports/khronos-headers/CMakeLists.txt
ports/hwdata/CMakeLists.txt
ports/zlib/CMakeLists.txt
ports/bzip2/CMakeLists.txt
ports/xz/CMakeLists.txt
ports/libffi/CMakeLists.txt
ports/libexpat/CMakeLists.txt
ports/pcre2/CMakeLists.txt
ports/json-c/CMakeLists.txt
ports/sqlite/CMakeLists.txt
ports/ncurses/CMakeLists.txt
ports/readline/CMakeLists.txt
ports/openssl/CMakeLists.txt
ports/curl/CMakeLists.txt
ports/openssh/CMakeLists.txt
ports/libxml2/CMakeLists.txt
ports/cpython/CMakeLists.txt
ports/vim/CMakeLists.txt
ports/vim/xv6-vim-launcher.c
```

Graphics, input, Wayland, DRM, Mesa, and Weston control package wrappers:

```text
ports/libudev/CMakeLists.txt
ports/libseat/CMakeLists.txt
ports/libevdev/CMakeLists.txt
ports/libinput/CMakeLists.txt
ports/libdrm/CMakeLists.txt
ports/drm_info/CMakeLists.txt
ports/kmscube/CMakeLists.txt
ports/libepoxy/CMakeLists.txt
ports/wayland-host/CMakeLists.txt
ports/wayland-libs/CMakeLists.txt
ports/wayland-protocols/CMakeLists.txt
ports/xkeyboard-config/CMakeLists.txt
ports/libxkbcommon/CMakeLists.txt
ports/mesa/CMakeLists.txt
ports/wayland/CMakeLists.txt
ports/weston/CMakeLists.txt
ports/weston/xv6-weston.ini
ports/weston/xwayland-glamor-wrapper.c
ports/mesa/gl.pc
```

Directory-level package boundaries without active wrapper files in this
checkout:

```text
ports/xv6-gbm
ports/wayland-src/src
ports/peanut-gb/src
```

`ports/xv6-gbm` is an allowlisted local compatibility boundary and is currently
a placeholder directory. `ports/wayland-src/src` is shared upstream source for
Wayland-related ports. `ports/peanut-gb/src` is a demo dependency referenced by
`ports/wayland`, not a KDE/Qt bring-up target.

Text, font, image, GTK/WebKit, and NetSurf package wrappers:

```text
ports/libpng/CMakeLists.txt
ports/libpng-host/CMakeLists.txt
ports/libjpeg-turbo/CMakeLists.txt
ports/freetype/CMakeLists.txt
ports/fontconfig/CMakeLists.txt
ports/pixman/CMakeLists.txt
ports/fribidi/CMakeLists.txt
ports/harfbuzz/CMakeLists.txt
ports/glib/CMakeLists.txt
ports/glib-host/CMakeLists.txt
ports/atk/CMakeLists.txt
ports/cairo/CMakeLists.txt
ports/gdk-pixbuf/CMakeLists.txt
ports/pango/CMakeLists.txt
ports/gtk3/CMakeLists.txt
ports/webkit/CMakeLists.txt
ports/netsurf-buildsystem/CMakeLists.txt
ports/netsurf-libwapcaplet/CMakeLists.txt
ports/netsurf-libparserutils/CMakeLists.txt
ports/netsurf-libcss/CMakeLists.txt
ports/netsurf-libdom/CMakeLists.txt
ports/netsurf-libhubbub/CMakeLists.txt
ports/netsurf-libnsbmp/CMakeLists.txt
ports/netsurf-libnsgif/CMakeLists.txt
ports/netsurf-libnslog/CMakeLists.txt
ports/netsurf-libnspsl/CMakeLists.txt
ports/netsurf-libnsutils/CMakeLists.txt
ports/netsurf-libsvgtiny/CMakeLists.txt
ports/netsurf-nsgenbind/CMakeLists.txt
ports/netsurf/CMakeLists.txt
```

## Staging/Rootfs/Runtime Data/Harnesses

Responsibility: generated rootfs overlay policy, wrapper policy, local shims,
runtime data, and regression/control harnesses. These are support files, not
behavioral patches to imported GUI source.

Tags: `support-control-probe` and `runtime-data`.

Generated KDE/Qt package evidence:

```text
build-x86_64/kde-noble-plasma/kde-qt-package-inventory.tsv
build-x86_64/kde-noble-plasma/packages.apt-order.txt
build-x86_64/rootfs-generated-overlays/kde-runtime/opt/xv6-kde/kde-qt-package-inventory.tsv
build-x86_64/rootfs-generated-overlays/kde-runtime/opt/xv6-kde/packages.apt-order.txt
```

These generated files record the current KDE/Qt runtime package order and are
build artifacts, not committed source. The generated KDE/Qt/Plasma overlay
payload is a runtime payload boundary and should remain upstream-clean.

Staging and launcher sources. The old `scripts/image/kde-*.c` pattern is
expanded here because the current source set is concrete and small enough to
track explicitly:

```text
scripts/image/stage-kde-runtime.sh
scripts/image/make-rootfs.sh
scripts/image/make-image.sh
scripts/image/make-initrd.sh
scripts/image/make-hyperv-image.sh
scripts/image/stage-host-gui-runtime.sh
scripts/image/stage-webkit-media.sh
scripts/image/import-host-gui.sh
scripts/image/stage-xwayland-runtime.sh
scripts/image/host-gtk-smoke.c
scripts/image/host-gtk-smoke-launcher.c
scripts/image/host-idle-x11-launcher.c
scripts/image/host-wlegl-smoke.c
scripts/image/host-wlegl-smoke-launcher.c
scripts/image/host-x11-abi-smoke.c
scripts/image/host-x11-abi-smoke-launcher.c
scripts/image/host-x11-dri3-present-smoke.c
scripts/image/host-x11-dri3-present-smoke-launcher.c
scripts/image/host-x11-egl-smoke.c
scripts/image/host-x11-egl-smoke-launcher.c
scripts/image/host-x11-shm-smoke.c
scripts/image/host-x11-shm-smoke-launcher.c
scripts/image/hyperv-efiloader.c
scripts/image/kde-abi-probe.c
scripts/image/kde-app-launch-probe.c
scripts/image/kde-config-atomic-probe.c
scripts/image/kde-dlopen-probe.c
scripts/image/kde-drm-probe.c
scripts/image/kde-konsole-shell-wrapper.c
scripts/image/kde-kwin-screenshot-probe.c
scripts/image/kde-kwriteconfig-probe.c
scripts/image/kde-libinput-probe.c
scripts/image/kde-plasma-session-child.c
scripts/image/kde-proc-comm-probe.c
scripts/image/kde-proc-mountinfo-probe.c
scripts/image/kde-process-probe.c
scripts/image/kde-pty-openpty-probe.c
scripts/image/kde-pty-readiness-probe.c
scripts/image/kde-pty-shell-probe.c
scripts/image/kde-pulse-cookie-probe.c
scripts/image/kde-session.c
scripts/image/kde-smoke-agent.c
scripts/image/kde-terminal-launcher.c
scripts/image/kde-trash-stat-probe.c
scripts/image/kde-unix-socket-probe.c
scripts/image/kde-wayland-seat-probe.c
scripts/image/qt-wayland-smoke-launcher.c
scripts/image/xv6-desktop-session.c
scripts/image/xv6-login1-shim.c
scripts/image/xwayland-kde-wrapper.c
scripts/image/wayland-chromium-launcher.c
scripts/image/xv6-bluez-shim.c
scripts/image/xv6-false.c
scripts/launch/launch-gui.sh
scripts/launch/run-qemu.sh
scripts/launch/place-qemu-window.py
scripts/launch/place-qemu-window-windows.ps1
scripts/container/check-gui-accel.sh
scripts/container/container-hints.sh
scripts/container/container-xv6-command.sh
scripts/container/docker-build-webkit.sh
scripts/container/enter-container.sh
ports/webkit/stage-webkit-runtime.sh
ports/webkit/apply-xv6-overrides.sh
ports/webkit/overrides/README.md
```

`scripts/image/xv6-false.c` is tracked and is inventoried here as an xv6-owned
support-control probe.

Regression/control harnesses named by the KDE inventory:

```text
scripts/gpu/kde-plasma-desktop-smoke.expect
scripts/gpu/qt-wayland-smoke.expect
scripts/gpu/gpu-validate.sh
scripts/gpu/virgl-kms-validate.sh
```

Additional concrete GUI control/runtime harnesses present in this checkout:

```text
scripts/gpu/baseline-desktop-entry-smoke.expect
scripts/gpu/alpine-virgl-desktop-capture.sh
scripts/gpu/capture-host-screen.sh
scripts/gpu/chromium-menu-proof.expect
scripts/gpu/chromium-youtube-smoothness.expect
scripts/gpu/host-gtk-smoke-proof.expect
scripts/gpu/host-gui-proof-verify.py
scripts/gpu/host-idle-x11-proof.expect
scripts/gpu/host-python-repl-proof.expect
scripts/gpu/host-wlegl-smoke-proof.expect
scripts/gpu/host-x11-abi-smoke-proof.expect
scripts/gpu/host-x11-dri3-present-smoke-proof.expect
scripts/gpu/host-x11-egl-smoke-proof.expect
scripts/gpu/host-x11-shm-smoke-proof.expect
scripts/gpu/hyperv-3d-fps-validate.sh
scripts/gpu/hyperv-3d-visual-check.sh
scripts/gpu/hyperv-dxg-validate.sh
scripts/gpu/hyperv-gpu-core-validate.sh
scripts/gpu/hyperv-gpu-stress.sh
scripts/gpu/hyperv-webkit-gpu-validate.sh
scripts/gpu/linux-kde-virgl-baseline.sh
scripts/gpu/perf-video-gate-matrix.sh
scripts/gpu/perf-video-gate.expect
scripts/gpu/stage-gpup-umd.sh
scripts/gpu/titlebar-control-matrix.sh
scripts/gpu/titlebar-control-report.py
scripts/gpu/ttm-sg-table-proof.expect
scripts/gpu/validate-webkit-runtime.sh
scripts/gpu/virgl-async-soak.expect
scripts/gpu/virgl-desktop-validate.sh
scripts/gpu/wayland-chromium-supervisor-low-noise.expect
scripts/gpu/webkit-cadence-report.py
scripts/gpu/webkit-qos-report.py
scripts/gpu/webkit-typed-url-enter.expect
scripts/gpu/webkit-virgl-gpu-validate.sh
scripts/gpu/webkit-youtube-smoothness-report.sh
scripts/gpu/x11-idle-dri3-teardown-proof.expect
```

Rootfs overlay data. The old D-Bus and WebKit globs are expanded to concrete
current files:

```text
rootfs-overlay/etc/startup
rootfs-overlay/etc/daemons
rootfs-overlay/etc/gtk-3.0/settings.ini
rootfs-overlay/etc/machine-id
rootfs-overlay/usr/share/alsa/alsa.conf
rootfs-overlay/bin/start-dbus-system
rootfs-overlay/etc/ld.so.conf.d/gpup-wsl.conf
rootfs-overlay/etc/profile.d/gpup-d3d12.sh
rootfs-overlay/etc/udisks2/udisks2.conf
rootfs-overlay/run/dbus/.gitkeep
rootfs-overlay/usr/share/dbus-1/xv6-session.conf
rootfs-overlay/usr/share/dbus-1/xv6-system.conf
rootfs-overlay/var/run/dbus/.gitkeep
rootfs-overlay/share/chromium-audio-smoke.html
rootfs-overlay/share/mime/globs
rootfs-overlay/share/mime/globs2
rootfs-overlay/share/mime/text/html.xml
rootfs-overlay/share/webkit/fetch-stream.html
rootfs-overlay/share/webkit/google-js-load.html
rootfs-overlay/share/webkit/google-th-script.html
rootfs-overlay/share/webkit/gpu-smoke.html
rootfs-overlay/share/webkit/gpu-webgl-smoke.html
rootfs-overlay/share/webkit/human-button.html
rootfs-overlay/share/webkit/input-smoke.html
rootfs-overlay/share/webkit/lifecycle-jobset-signal.html
rootfs-overlay/share/webkit/media-codecs.html
rootfs-overlay/share/webkit/media-init-steps.html
rootfs-overlay/share/webkit/mse-audio-stress.html
rootfs-overlay/share/webkit/mse-dual-stress.html
rootfs-overlay/share/webkit/mse-mp4-video.html
rootfs-overlay/share/webkit/mse-playback.html
rootfs-overlay/share/webkit/mse-repeat.html
rootfs-overlay/share/webkit/mse-streaming-loop.html
rootfs-overlay/share/webkit/parser-yield.html
rootfs-overlay/share/webkit/passive-signin-frame.html
rootfs-overlay/share/webkit/perf-video.html
rootfs-overlay/share/webkit/redirect-under-load.html
rootfs-overlay/share/webkit/request-idle-timeout-busy.html
rootfs-overlay/share/webkit/video-direct.html
rootfs-overlay/share/webkit/webkit-animated-content-native-present.html
rootfs-overlay/share/webkit/youtube-chain-load.html
rootfs-overlay/share/webkit/youtube-eocs-delay.html
rootfs-overlay/share/webkit/youtube-idle-scheduler.html
```

## Guardrails Carried Forward

- This file is an inventory, not an instruction to build, run, or validate.
- No Weston modifications are part of this inventory step; Weston remains a
  regression/control boundary.
- Kernel files are the preferred owner for Linux ABI behavior.
- KDE, Qt, KWin, Plasma, Chromium, and WebKit source stay upstream-clean except
  marker-only xv6 metadata.
- Imported GUI applications are probes for ABI gaps, not patch targets for
  masking kernel or sysroot defects.
- Generated KDE/Qt package payloads and Chrome for Testing payloads are runtime
  payload boundaries, not committed source.
- The missing `docs/linux-kde-minimal-integration-plan.md` is intentionally not
  referenced as an authority in this checkout.
