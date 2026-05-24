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

## Kernel Static Analysis

- Use `cmake --build build-x86_64 --target kernel-sparse -j2` to run Sparse over the kernel compile database.
- The Sparse target is a developer-time check: it uses `kernel/scripts/run_sparse.py`, enables `__CHECKER__`, and activates lock/context annotations from `compiler.h`.
- Fresh containers include the `sparse` host package. On a host without it, install `sparse` or run with `SPARSE=/path/to/sparse`.
- Sparse context annotations currently cover spinlocks, page locks, page ref unlocked helpers, and RCU read sections. Use this before long GUI/WebKit VM runs when changing pcache, VM, RCU, or page-table code.

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

## Hyper-V GPU Bring-Up

- Treat Hyper-V GPU work as a transport, UMD, compositor-present, and validation problem. Do not mark `FB_GPU_BACKEND_F_OPENGL_SUBMIT` true just because `/dev/dri/renderD128`, DXG transport, D3DKMT readiness, or a real HW queue exists.
- The former large `kernel/dev/fb.c` and `kernel/dev/hyperv_input.c` files are
  split under subsystem roots:
  - `kernel/dev/fb/module.c` is the framebuffer/GPU root. Its fragments group
    scanout, BO/GEM/TTM/dmabuf, DXG present-source glue, exported fd/fence
    lifecycle, DRM/KMS, syncobj/PRIME/virtgpu, Nouveau, dispatch, init, and
    panic-screen code.
  - `kernel/dev/hyperv/module.c` is the Hyper-V root. Its fragments group
    common protocol state, vPCI config, DXG diagnostics/status, DXG object and
    shared-resource lifetime, D3DKMT ioctl forwarding, DXG device exports,
    VMBus core, synthetic devices, and public init/stub code.
  Keep new work in the narrow fragment that owns the behavior. Promote a
  fragment to a separately compiled `.c` file only after its shared state and
  static helper dependencies have explicit internal APIs.
- Known honest capability split:
  - Hyper-V may expose `FB_GPU_BACKEND_F_DXG_TRANSPORT` and `FB_GPU_BACKEND_F_D3DKMT`.
  - Hyper-V must keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT` false until Mesa D3D12 creates real render contexts, submits real UMD command buffers, presents without the software readback lane, and the 480p 3D demo sustains more than 60 FPS after warmup.
  - KVM/virgl is the current OpenGL-submit backend.
- Before editing GPU code, check dirty state in all nested repos:
  - `git -C /home/es/xv6-os status --short`
  - `git -C /home/es/xv6-os/kernel status --short`
  - `git -C /home/es/xv6-os/user status --short`
  - `git -C /home/es/xv6-os/ports status --short`
- Use `/tmp/xv6-hyperv-build` directly for Hyper-V builds. Do not rely on VS Code CMake Tools when it reports no configured targets.
- Build focused Hyper-V test images with explicit command lines instead of reusing a stale VHDX. Keep the VM at 6 vCPUs when the current workflow asks for reduced cores:
  - `cmake --build /tmp/xv6-hyperv-build --target kernel -j2`
  - `cmake --build /tmp/xv6-hyperv-build --target rootfs -j2`
  - `HYPERV_CMDLINE='BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=0 video=1024x640 acpi_cpus=6 wlcomp_gpu_compose=1 wlcomp_gpu_direct_scanout=1' scripts/make-hyperv-image.sh /tmp/xv6-hyperv-build/kernel/build/kernel/xv6.bin /tmp/xv6-hyperv-build/fs.img /tmp/xv6-hyperv-build/xv6-hyperv-gpu-test.vhdx 0`
- Serial helper:
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'C:\Temp\com-tcp-read.ps1' -Cmd '<guest command>' -ReadMs <ms>"`
- Serial silence is not proof of inactivity. Long Mesa/DXG traces often buffer output until the command exits. Keep polling the running command; if it times out, check VM state with `Get-VM` and then run a fresh short serial command such as `cat /proc/cmdline`.
- If a follow-up serial command only echoes the command text, do not infer that `/dev/dxg` or `fbstat` is empty. The guest shell may not be ready after a long trace; collect a fresh shell prompt or use a split command.
- Do not reboot or redeploy over a running freeze or long-probe sample until evidence has been collected, unless the user explicitly asks for a reset.
- Keep WSL comparisons adapter-matched. A WSL Intel trace is not a reliable reference for an xv6 NVIDIA Hyper-V run. Capture same-adapter traces when possible, for example:
  - `env GALLIUM_DRIVER=d3d12 D3D12_DEBUG=verbose MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA LD_PRELOAD=/tmp/xv6-wsl-probe/libwsl_dxg_ioctl_trace.so /tmp/tmp.PHsSKWCqgl/bin/mesaglfeature > /tmp/xv6-wsl-probe/mesaglfeature-nvidia-live.trace 2>&1`
- WSL2 `dxgkrnl` lives under `drivers/hv/dxgkrnl` in Microsoft's WSL2 Linux
  kernel, with UAPI in `include/uapi/misc/d3dkmthk.h`. Use it as the DXG/D3DKMT
  process, handle-table, shared-resource, sync-file, and VMBus packet
  reference; do not treat it as the DRM/KMS/Nouveau display-stack reference.
- Keep the reference tracks separate: WSL parity can close Hyper-V DXG object
  and wire-layout gates, while Linux DRM/GEM/TTM/KMS/Nouveau sources govern
  `/dev/dri`, PRIME/dma-buf, KMS atomic, PCI runtime, and Nouveau behavior.
- Remember what the trace layers mean:
  - `LD_PRELOAD` ioctl traces show the UMD's user-space ioctl arguments before the xv6 kernel rewrites or validates them.
  - `/dev/dxg` shows the kernel's recorded host-return state after forwarding.
  - If you transform a packet in the kernel, add or consult kernel-side diagnostics before claiming the host saw the transformed packet.
- Current Hyper-V D3D12 state from May 17, 2026:
  - Real Mesa/NVIDIA D3D12 reaches DXCore enumeration, `CREATECONTEXTVIRTUAL` with 3200-byte private data, `CREATEHWQUEUE` with 124-byte private data, many allocations/GPUVA maps/locks, and real `SUBMITCOMMANDTOHWQUEUE` calls that return success.
  - The blocker is later than "submit does not work": `mesaglfeature` passes the first 32x32 FBO draw/readback, then the second 64x32 FBO draw path fails when `LX_DXMAKERESIDENT` receives a multi-allocation batch (`count=2`, `flags=0x1`) and the host returns `STATUS_INVALID_PARAMETER` / `-EINVAL`, causing `D3D12: Removing Device`.
  - Sorting or otherwise rewriting residency lists is not a substitute for proof. Compare WSL and xv6 by same adapter, same private payloads, kernel-side packet contents, host status, fence values, and cleanup state.
- The 3D demo can render through Mesa D3D12 but still presents through a software/readback Wayland lane and is below the 60 FPS target. FPS validation must use an in-surface RTC-based overlay plus finite post-warmup measurement, not only stderr or window-title updates.
- The source-level Hyper-V native-present skeleton is
  `FB_GPU_DXG_PRESENT_BIND_CONTRACT_QUERY`. It lives in
  `kernel/dev/fb/fb_dxg_present.c` and must remain fail-closed until a real
  GPU-P/DDA display lane exists. Treat its registered present source,
  source/resource generations, required metadata, selected bind lane, and
  display-completion source as the handoff contract; do not infer native
  present from loose D3DKMT handles or `/dev/dxg` readiness alone.
- The selected native-present handoff lane is GPU-P/DDA
  `dxg-resource-scanout-bind`, not WSLg display channel emulation and not a
  synthvid GPA-dirty bridge. `fbstat` should report
  `dxg_present_lane_selection_matrix` with WSLg disabled, synthvid limited to
  GPA dirty VRAM, `custom_host_tool=0`, and zero native-present/OpenGL-submit
  credit until the real host ABI and completion source exist.
- The next durable Hyper-V GPU milestones are:
  - WSL-style typed per-open DXG object graph and teardown ordering.
  - Exact WDDM private payload and host return layout parity for real UMD sequences.
  - D3D12 shared-resource/fence export/import between a Mesa client and compositor.
  - A non-readback Wayland present path for Hyper-V.
  - A finite GUI performance validator that fails below 60 FPS after warmup.

### Hyper-V GPU Validation Discipline

- Keep validation hierarchical:
  - build `/tmp/xv6-hyperv-build` first;
  - run focused pure-C guest sections for the implemented slice;
  - only then run heavier GUI/FPS/WebKit validation for a completed segment.
- For KMS/DRM format work, separate framebuffer metadata from scanout capability:
  - `ADDFB2`/`GETFB2` may accept metadata for formats that the primary scanout
    plane cannot present yet;
  - `GETPLANE` must advertise only formats that the primary plane can actually
    scan out;
  - `SETCRTC`, page flip, atomic commit, and atomic `TEST_ONLY` must reject an
    unsupported framebuffer before queuing events, taking in-fence refs,
    exporting or cleaning out-fences, mutating plane/current-FB state, or
    advancing display/DXG/native/OpenGL-submit credit.
- Treat zero-credit matrices as real contracts, not decorative logging:
  validators should prove `native_present_credit=0`, `opengl_submit_credit=0`,
  and no DXG-present/display deltas whenever a path is still software,
  synthetic, or fail-closed.
- For KMS vblank/page-flip work, keep event-source provenance separate from
  native-present proof:
  - pre-native KMS events may be display-completion-correlated only when
    validators report `kms_vblank_synthetic=0`,
    `kms_vblank_display_correlated=1`, and
    `display_completion_correlated=PASS`;
  - display-correlated KMS events still grant no native-present or
    OpenGL-submit credit until atomic OUT_FENCE/native display handoff is real;
  - software/immediate atomic OUT_FENCE provenance remains an open gate even
    after vblank/page-flip event timing is display-correlated.
- For sync_file/fence work, keep the hierarchy explicit:
  - live pending sync_file export/import is one layer;
  - callback lifecycle is a stricter layer and must prove poll arms, source
    signal fires, close-before-signal cancels, late fires stay zero, and live
    syncobj/fd state balances;
  - syncobj waits are stricter again when they prove per-state wait callbacks:
    a real sleeping wait arms a callback, signal/transfer fires it, finite
    timeout or interruption cancels it, late-fire count stays zero, and
    native/OpenGL-submit credit stays zero;
  - on xv6 custom fds, visible close-before-signal cancellation belongs in the
    `.last_fd_close` hook; `.release` can run later and should only be backup
    cleanup;
  - software fence-fd callbacks should prove the same add/fire/remove/late
    lifecycle on `fb_gpu_fence` objects before claiming broader dma-fence
    parity; the software fence fd uses VFS `early_release_on_close` so exact
    object lifetime can be validated when no hidden/concurrent references
    remain;
  - do not mark the broad Linux `dma_fence` gate complete until callback
    removal, timeline lifetime, poll wakeups, and reservation iteration are
    validated across GEM, PRIME, KMS, and syncobj.

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
