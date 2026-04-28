---
name: xv6-os-debugging
description: 'Use when: working in this xv6-os repo on QEMU boot, kernel symbols, GUI/Wayland ports, NetSurf, OpenSSL/OpenSSH, rootfs images, or nested submodule commit/push workflows.'
argument-hint: 'Describe the xv6-os build, runtime, or port symptom'
---

# xv6-os Debugging

## Authority

The real repo skill files live under `.github/skills`. Repo-local `.codex/skills` entries are redirects only; migrate durable content here and keep `.codex` from becoming a second source of truth.

## Fast Workflow

- Build a single port from the configured tree, for example:
  - `cmake --build build-x86_64/ports --target port-netsurf -j2`
  - `cmake --build build-x86_64/ports --target port-openssl port-openssh -j2` is not portable to all Make versions; invoke one target at a time if needed.
- Refresh an image directly when testing sysroot/rootfs changes:
  - `scripts/make-rootfs.sh build-x86_64/sysroot /tmp/xv6-test.img 1536 build-x86_64/toolchain/x86_64/phase2/x86_64-xv6-linux-musl/lib`
- If that libdir does not exist, find the local musl dynamic linker with:
  - `find build-x86_64/toolchain -path '*lib/ld-musl*'`
- Run GUI tests headlessly with:
  - `DISPLAY_MODE=nographic QEMU_NET=0 FSIMG=/tmp/xv6-test.img bash scripts/launch-gui.sh`
- Check whether `build-x86_64/fs.img` actually changed with `stat`; a running QEMU session or stale image can hide a successful rebuild.

## Kernel Symbols

- A healthy boot log includes embedded symbol loading and `Kernel symbols initialized: ... entries`.
- If backtraces show missing symbols, inspect the kernel artifact passed to QEMU and GDB before chasing runtime unwind code.
- Prefer the ELF kernel with symbols for GDB and the boot artifact with embedded symbols for QEMU.

## NetSurf, TLS, and Fetch Errors

- NetSurf is expected to build with:
  - `NETSURF_USE_CURL := YES`
  - `NETSURF_USE_OPENSSL := YES`
- `ports/netsurf/CMakeLists.txt` should explicitly depend on `port-curl` and `port-openssl`; do not rely on incidental sysroot build order.
- `ports/curl/CMakeLists.txt` links libcurl to OpenSSL using static `libcrypto.a` and `libssl.a` from `${XV6_SYSROOT}`.
- NetSurf links these statically. `readelf -d build-x86_64/sysroot/bin/netsurf` should not be expected to show `libssl.so` or `libcrypto.so`; use `nm` to look for `Curl_ssl_openssl`, `EVP_*`, or other OpenSSL symbols.
- For NetSurf `Error occurred fetching page`, separate layers:
  - browser mapped and title changed in `wlcomp` logs;
  - socket creation/connect in kernel logs;
  - DNS config in `/etc/resolv.conf` inside the rootfs;
  - TLS/OpenSSL symbols in the binary;
  - CA/certificate path and NetSurf resource staging.
- In QEMU user networking, the fallback DNS server is `10.0.2.3`.

## OpenSSL and OpenSSH Ports

- OpenSSL is not only a library port: `ports/openssl/CMakeLists.txt` should stage `/bin/openssl` as well as headers and static libs.
- OpenSSH lives under `ports/openssh` and stages `ssh`, `sshd`, `ssh-keygen`, `ssh-keyscan`, `scp`, `sftp`, and `libexec/sshd-session`.
- OpenSSH config/build should use the already-staged OpenSSL sysroot and disable unsupported platform integrations such as PAM, SELinux, libedit, zlib, utmp/wtmp/lastlog, PKCS#11, and security keys.
- If OpenSSH configure complains that m4 files are newer than `configure`, build from a copied source tree and touch the copied `configure`; keep the submodule source clean.

## Rootfs Runtime Setup

- `scripts/make-rootfs.sh` mirrors the sysroot and overlays `rootfs-overlay`.
- Runtime files for network clients and SSH belong in the rootfs image, not only the sysroot: `/etc/hosts`, `/etc/resolv.conf`, `/etc/passwd`, `/etc/group`, `/etc/shadow`, `/etc/shells`, `/etc/ssh`, `/var/empty`, and `/var/run`.
- Host-generated SSH keys must be root-owned in the ext4 image, with private keys at `0600`; `mke2fs -d` preserves the host UID/GID, so use `debugfs` fixups when building as a normal user.
- `/etc/daemons` is what init reads. Include `/bin/sshd -D -e` there when validating SSH startup.

## NetSurf and Wayland

- In TCG mode NetSurf should launch by default; `USE_KVM=1` or `QEMU_NETSURF=0` can append `netsurf=0`.
- Useful compositor logs include `wlcomp: client app_id: netsurf` and `wlcomp: client title: ... NetSurf`.
- GTK Wayland shared memory needs a tmpfs-backed `/tmp`; ext4-backed `/tmp` can make `ftruncate()` growth fail.
- On xv6, prefer libc wrappers for port syscalls when available. Raw Linux syscall numbers from upstream headers may not match xv6 musl.
- If the taskbar lacks a NetSurf button, inspect xdg toplevel app-id/title handling before assuming the surface never mapped.
- For Wayland EOF noise, clean client disconnects should be silent. Keep logs for socket errors, nonzero child exits, and signal kills.

## WebKit and VM Faults

- A healthy WebKit smoke boot reaches `wlcomp: client title: WebKitGTK MiniBrowser`, then usually a page title such as `Google`.
- Musl clean exits often show PCs near `_Exit` or `__clone`; do not treat them as faults unless paired with `fatal page fault`, a coredump, or nonzero status.
- `vma_alloc: FAIL unaligned va=...` after WebKit or `brk()` activity is suspicious. Check that mmap free-range search uses page-aligned bounds and that byte-precise heap break values are not fed directly to VMA allocation.
- For unaligned `mprotect`, `munmap`, `msync`, `madvise`, and `mremap` ranges, normalize by rounding the start down and the end up so the covered byte interval is not truncated.
- `MAP_FIXED` addresses should remain page-aligned; reject unaligned fixed mappings instead of silently rounding them to a different address.

## Submodules

- Commit and push from deepest changed submodules upward: for example `ports/openssh/src`, then `ports`, then the top-level repo.
- Do not rewrite unrelated dirty state. Check each repo with `git status --short` before staging.
- After committing a submodule, commit the parent pointer update in the containing repo.
- Push submodule branches before pushing the parent pointer. If a submodule remote is upstream-only and rejects pushes, call that out explicitly rather than pretending all submodules are published.
