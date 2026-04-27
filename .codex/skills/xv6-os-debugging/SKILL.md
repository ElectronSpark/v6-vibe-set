---
name: xv6-os-debugging
description: Use when working in this xv6-os repo on QEMU, kernel symbols, GUI/Wayland ports, NetSurf, rootfs images, or nested submodule commit/push workflows.
metadata:
  short-description: Debug xv6 GUI, ports, rootfs, and symbols
---

# xv6-os Debugging

## Fast Workflow

- Build GUI ports with `cmake --build build-x86_64/ports --target port-wayland -j16`.
- Refresh an image directly when testing port/sysroot changes:
  `scripts/make-rootfs.sh build-x86_64/sysroot /tmp/xv6-test.img 1536 build-x86_64/toolchain/x86_64/phase2/x86_64-xv6-linux-musl/lib`
- If that libdir does not exist, use the local musl libdir shown by `find build-x86_64/toolchain -path '*lib/ld-musl*'`.
- Run GUI tests headlessly with:
  `DISPLAY_MODE=nographic QEMU_NET=0 FSIMG=/tmp/xv6-test.img bash scripts/launch-gui.sh`.
- Check whether `build-x86_64/fs.img` actually changed with `stat`; the generic rootfs target may leave an older image in place.

## Kernel Symbols

- A healthy boot log includes embedded symbol loading and `Kernel symbols initialized: ... entries`.
- If backtraces show missing symbols, inspect `scripts/attach-gdb.sh`, `scripts/launch-gui.sh`, and the kernel artifact being passed to QEMU before chasing runtime unwind code.
- Prefer the ELF kernel with symbols for GDB and the boot artifact with embedded symbols for QEMU.

## NetSurf And Wayland

- In TCG mode NetSurf should launch by default; `USE_KVM=1` or `QEMU_NETSURF=0` can add `netsurf=0`.
- Useful compositor logs include `wlcomp: client app_id: netsurf` and `wlcomp: client title: ... NetSurf`.
- GTK Wayland shared memory needs a tmpfs-backed `/tmp`; ext4-backed `/tmp` can make `ftruncate()` growth fail.
- On xv6, prefer libc wrappers for port syscalls when available. Raw Linux syscall numbers from upstream headers may not match xv6 musl.
- If the taskbar lacks a NetSurf button, inspect xdg toplevel app-id/title handling before assuming the surface never mapped.

## Submodules

- Commit and push from deepest changed submodules upward: for example `ports/gtk3/src`, then `user`, then `ports`, then the top-level repo.
- Do not rewrite unrelated dirty state. Check each repo with `git status --short` before staging.
- After committing a submodule, commit the parent pointer update in the containing repo.
