# Linux Userland Upstream De-Patching Plan

Scope: all x86_64 user programs, user libraries, and `ports/*` libraries or
programs. The goal is to remove ABI adaptation from user program/library source
and leave only an explicit `xv6` marker where a source tree needs to identify
the port boundary.

This is a source hygiene and ABI ownership plan. It does not replace
`docs/linux-userland-abi-kernel-gap-plan.md`; it turns userland deltas found
during that work into kernel, libc/sysroot, build-wrapper, or local-shim tasks.

## Target State

Imported user programs and libraries should be upstream-clean:

- Source deltas are limited to a small, auditable `xv6` marker such as a
  comment, package metadata tag, or version suffix.
- No imported source may compensate for missing xv6 Linux ABI behavior with
  alternate syscall paths, struct layout changes, errno rewrites, event-loop
  shortcuts, fd/socket hacks, procfs fallbacks, DRM/Wayland/X11 special cases,
  scheduler assumptions, or feature-disabling runtime workarounds.
- Compatibility differences are fixed below the imported program boundary:
  kernel ABI, staged Linux headers, libc/sysroot behavior, dynamic loader
  staging, package build wrappers, rootfs data, or explicitly local shim
  libraries.
- Local xv6-authored tests and probes may remain xv6-specific, but they must
  test ABI behavior rather than hide missing ABI from imported applications.

Allowed adaptation locations:

- `kernel/`: Linux user/kernel ABI behavior.
- `user/lib` and `user/lib/x86_64`: xv6-owned support library code for local
  programs and ABI probes only.
- `ports/<pkg>/CMakeLists.txt`, Meson/CMake cross files, pkg-config wrappers,
  staging scripts, and rootfs manifests: build and packaging adaptation.
- Local shim packages with no upstream source equivalent, for example a small
  compatibility implementation of a Linux-facing library. These must be marked
  as xv6-owned and should expose upstream-compatible ABI, not application
  shortcuts.

Disallowed adaptation in imported source:

- `#ifdef xv6`, `#ifdef __xv6__`, `__XV6__`, or package-specific xv6 runtime
  branches that change behavior.
- Raw syscall fallbacks in imported libraries or applications.
- xv6-only struct definitions replacing Linux UAPI or libc definitions.
- Silent feature removal to make an application start.
- Application-specific sleeps, polling shortcuts, or retry loops masking kernel
  readiness, futex, poll, epoll, VFS, socket, DRM, Wayland, X11, or procfs bugs.

## Inventory Scope

Every item below is in scope. The initial audit must classify each item as one
of:

- `local-program`: xv6-authored command, test, benchmark, or probe.
- `imported-source`: upstream program or library source that should become
  upstream-clean.
- `local-shim`: xv6-owned library or data package that intentionally implements
  a Linux-facing compatibility surface.
- `build-wrapper`: package metadata, cross-build files, or staging glue.
- `data-or-headers`: imported data/header package where source code deltas
  should normally be zero.

Checklist order is dependency order: support libraries and data packages appear
before their consumers, and local user programs appear after the local user
libraries they use.

### Local User Libraries

- [ ] `user/lib`
- [ ] `user/lib/x86_64`

### Local User Programs

- [ ] `init`
- [ ] `sh`
- [ ] `cat`
- [ ] `echo`
- [ ] `sleep`
- [ ] `kill`
- [ ] `reboot`
- [ ] `shutdown`
- [ ] `sync`
- [ ] `free`
- [ ] `ps`
- [ ] `top`
- [ ] `lsblk`
- [ ] `mount`
- [ ] `umount`
- [ ] `mknod`
- [ ] `mkdir`
- [ ] `cp`
- [ ] `mv`
- [ ] `rm`
- [ ] `ln`
- [ ] `find`
- [ ] `grep`
- [ ] `wc`
- [ ] `xargs`
- [ ] `dd`
- [ ] `losetup`
- [ ] `mkfs_xv6fs`
- [ ] `dumpinode`
- [ ] `dumpchan`
- [ ] `dumppcache`
- [ ] `dumprq`
- [ ] `bigfile`
- [ ] `devtest`
- [ ] `dh`
- [ ] `wallclock`
- [ ] `timerdemo`
- [ ] `pngtest`
- [ ] `keyinject`
- [ ] `mouseinject`
- [ ] `mousetest`
- [ ] `waitgdb`
- [ ] `kprofile`
- [ ] `pingpong`
- [ ] `primes`
- [ ] `randtest`
- [ ] `zombie`
- [ ] `forktest`
- [ ] `vforktest`
- [ ] `clonetest`
- [ ] `cloexectest`
- [ ] `cowtest`
- [ ] `mmaptest`
- [ ] `mmapbigfile`
- [ ] `fdtabletest`
- [ ] `iovectest`
- [ ] `kqueuetest`
- [ ] `timerfdstress`
- [ ] `syscalltest`
- [ ] `linuxsyscallabitest`
- [ ] `testsig`
- [ ] `usertests`
- [ ] `grind`
- [ ] `crashtest`
- [ ] `stressfs`
- [ ] `iobench`
- [ ] `symlinktest`
- [ ] `blocksendwake`
- [ ] `dontwaitsend`
- [ ] `dnsstress`
- [ ] `tcpstress`
- [ ] `fbstat`
- [ ] `gldemo`
- [ ] `virgltest`
- [ ] `gpubuftest`
- [ ] `gpucorevalidate`
- [ ] `drmabitest`
- [ ] `drmiftest`
- [ ] `drmprimeprobe`
- [ ] `nouveauabitest`
- [ ] `dxgprobe`
- [ ] `d3d12probe`
- [ ] `ttmtest`
- [ ] `webkitabitest`
- [ ] `webkitnettest`
- [ ] `preempttest`

### Ports, Libraries, Programs, Data, And Headers

- [ ] `cmake`
- [ ] `linux-uapi-headers`
- [ ] `khronos-headers`
- [ ] `hwdata`
- [ ] `wayland-host`
- [ ] `glib-host`
- [ ] `libpng-host`
- [ ] `zlib`
- [ ] `bzip2`
- [ ] `xz`
- [ ] `libffi`
- [ ] `libexpat`
- [ ] `pcre2`
- [ ] `json-c`
- [ ] `sqlite`
- [ ] `ncurses`
- [ ] `readline`
- [ ] `openssl`
- [ ] `curl`
- [ ] `openssh`
- [ ] `libxml2`
- [ ] `libpng`
- [ ] `libjpeg-turbo`
- [ ] `freetype`
- [ ] `fontconfig`
- [ ] `pixman`
- [ ] `fribidi`
- [ ] `harfbuzz`
- [ ] `glib`
- [ ] `atk`
- [ ] `cairo`
- [ ] `gdk-pixbuf`
- [ ] `pango`
- [ ] `gtk3`
- [ ] `libudev`
- [ ] `libseat`
- [ ] `libevdev`
- [ ] `libinput`
- [ ] `libdrm`
- [ ] `drm_info`
- [ ] `kmscube`
- [ ] `libepoxy`
- [ ] `wayland-libs`
- [ ] `wayland-protocols`
- [ ] `xkeyboard-config`
- [ ] `libxkbcommon`
- [ ] `xv6-gbm`
- [ ] `mesa`
- [ ] `weston`
- [ ] `netsurf-buildsystem`
- [ ] `netsurf-libwapcaplet`
- [ ] `netsurf-libparserutils`
- [ ] `netsurf-libcss`
- [ ] `netsurf-libnsbmp`
- [ ] `netsurf-libnsgif`
- [ ] `netsurf-libnslog`
- [ ] `netsurf-libnspsl`
- [ ] `netsurf-libnsutils`
- [ ] `netsurf-libhubbub`
- [ ] `netsurf-libsvgtiny`
- [ ] `netsurf-nsgenbind`
- [ ] `netsurf-libdom`
- [ ] `netsurf`
- [ ] `cpython`
- [ ] `vim`
- [ ] `peanut-gb`
- [ ] `wayland-src`
- [ ] `webkit`
- [ ] `wayland`

## Audit Artifacts To Add

Create a generated inventory, for example
`build-x86_64/userland-depatch-inventory.tsv`, with one row per item above:

- `name`
- `path`
- `kind`
- `upstream_ref` or `local`
- `source_delta_count`
- `xv6_marker_count`
- `abi_adaptation_hits`
- `patch_files`
- `wrapper_files`
- `status`
- `next_owner`

The inventory scanner should check:

- `ports/<pkg>/patches`, `*.patch`, and package-specific patch application.
- `git diff` or submodule status for every `ports/<pkg>/src` tree.
- `rg` hits for `xv6`, `__xv6__`, `syscall(`, Linux struct redefinitions,
  errno rewrites, `/proc` fallbacks, feature-disabling flags, and GUI-specific
  workaround names.
- local user program includes that pull in compatibility helpers.
- user library helpers that are used by production commands instead of only
  probes/tests.

The scanner should produce an allowlist file for intentional local shims and
tests. The allowlist should be reviewed like source code; no broad directory
exceptions.

## Workstreams

### 1. Classify Local User Programs

Classify each `user/programs/*` entry as a production command, boot/session
program, benchmark, ABI probe, stress test, or xv6 filesystem tool.

Rules:

- Production commands should use ordinary libc-visible Linux behavior and avoid
  private ABI adaptation.
- ABI probes may use raw syscalls and xv6-specific checks, but their names and
  comments must make that explicit.
- Tests should move shared compatibility helpers into test-only files, not
  general-purpose headers.
- User-visible commands should not carry app-specific workarounds for Chromium,
  WebKit, Weston, NetSurf, Mesa, or X11 behavior.

First pass targets:

- Split test/probe-only helpers from `user/lib/host_compat.h`, `user/lib/user.h`,
  and `user/lib/fsutil.h`.
- Verify `sh`, `init`, `mount`, `umount`, `ps`, `top`, and filesystem tools do
  not depend on private ABI behavior that an imported Linux program could not
  use.
- Keep graphics and DRM probes as explicit diagnostics, not compatibility
  layers.

### 2. Make Imported Source Trees Upstream-Clean

For every `ports/<pkg>/src` tree:

1. Record the upstream baseline commit, tag, or tarball checksum.
2. Generate a diff against upstream.
3. Categorize every delta as marker, build-system-only, data refresh,
   portability cleanup, or ABI adaptation.
4. Delete or relocate ABI adaptation:
   - kernel if the delta expects Linux syscall, ioctl, mmap, futex, poll,
     epoll, VFS, socket, procfs, DRM, input, or scheduling behavior;
   - libc/sysroot if the delta expects Linux headers, errno, dynamic loader,
     locale, DNS, resolver, pthread, or C runtime behavior;
   - package wrapper if the delta is only cross-build discovery;
   - local shim if the upstream dependency is unavailable and the package is an
     xv6-owned implementation.
5. Re-run the package build and the relevant smoke test.

No package is complete until the imported source diff contains only the marker
and any accepted upstream-equivalent data refresh.

### 3. Reduce Patch Infrastructure

Current patch infrastructure is itself an audit target:

- `ports/cmake/apply_patches.sh`
- `ports/*/patches`
- package-local `*.patch`
- package-local scripts that rewrite source after checkout

Target state:

- Build wrappers may configure upstream source but must not mutate it for ABI
  behavior.
- Patch directories either disappear or contain only marker/version metadata
  patches.
- Adding a new behavioral patch to imported source should fail the source
  hygiene gate unless the plan explicitly marks it as temporary with a kernel
  or libc bug link.

### 4. Move ABI Gaps Downward

When a source delta exists because xv6 differs from Linux, open or update a
kernel/libc task before removing the delta.

Typical relocation targets:

- VFS and procfs: `openat`, `statx`, `getdents64`, `/proc/<pid>`, fd links,
  mount info, tmpfs behavior, file locks, and `fcntl`.
- Sockets and IPC: AF_UNIX streams, SCM_RIGHTS, poll/epoll readiness,
  nonblocking connect, D-Bus assumptions, TCP behavior, and resolver behavior.
- VM: `mmap`, `mprotect`, `mremap`, shared mappings, `memfd`, seals, hugepage
  fallbacks, and executable mapping permissions.
- Threading and synchronization: `clone`, robust futexes, priority/nice
  semantics, signal masks, timers, and cancellation-visible behavior.
- Graphics and input: DRM ioctls, dma-buf, sync objects, input event devices,
  cursor planes, Wayland/X11 fd passing, and GPU memory mapping.
- Runtime data: locales, compose files, fonts, certificates, time zones,
  mime data, and hardware/device metadata.

### 5. Local Shim Policy

Some packages may be xv6-owned because they represent a compatibility surface
rather than imported upstream code. Examples may include `libudev`, `libseat`,
`libinput` shims, `xv6-gbm`, and small host/helper packages.

Rules for shims:

- Mark as xv6-owned in the inventory.
- Keep ABI and API compatible with the upstream library they stand in for.
- Do not add application-specific branches.
- Cover behavior with Linux-comparison probes where practical.
- Prefer replacing a shim with upstream source once the kernel/sysroot supports
  it.

## Package Migration Order

### Phase 0: Inventory And Gates

- Add the inventory scanner and allowlist.
- Add a source hygiene CI/local gate.
- Record upstream refs for all `ports/*/src` trees.
- Record which `user/programs/*` entries are production commands versus tests
  or probes.

### Phase 1: Build, Header, Data, And Low-Level Library Base

- [ ] `cmake`
- [ ] `linux-uapi-headers`
- [ ] `khronos-headers`
- [ ] `hwdata`
- [ ] `wayland-host`
- [ ] `glib-host`
- [ ] `libpng-host`
- [ ] `zlib`
- [ ] `bzip2`
- [ ] `xz`
- [ ] `libffi`
- [ ] `libexpat`
- [ ] `pcre2`
- [ ] `json-c`
- [ ] `sqlite`
- [ ] `ncurses`
- [ ] `readline`
- [ ] `openssl`
- [ ] `curl`
- [ ] `openssh`
- [ ] `libxml2`

Exit criteria:

- source diffs are marker-only.
- build changes live in package wrappers.
- no package carries syscall, errno, VFS, locale, TLS, DNS, or thread
  workaround code.

### Phase 2: Image, Font, Text, And GTK Base

- [ ] `libpng`
- [ ] `libjpeg-turbo`
- [ ] `freetype`
- [ ] `fontconfig`
- [ ] `pixman`
- [ ] `fribidi`
- [ ] `harfbuzz`
- [ ] `glib`
- [ ] `atk`
- [ ] `cairo`
- [ ] `gdk-pixbuf`
- [ ] `pango`
- [ ] `gtk3`

Exit criteria:

- source diffs are marker-only.
- build changes live in package wrappers.
- locale, compose, font, icon, and cache behavior is supplied by sysroot/rootfs
  data or kernel/libc support, not source conditionals.

### Phase 3: Graphics, Input, And Desktop ABI

- [ ] `libudev`
- [ ] `libseat`
- [ ] `libevdev`
- [ ] `libinput`
- [ ] `libdrm`
- [ ] `drm_info`
- [ ] `kmscube`
- [ ] `libepoxy`
- [ ] `wayland-libs`
- [ ] `wayland-protocols`
- [ ] `xkeyboard-config`
- [ ] `libxkbcommon`
- [ ] `xv6-gbm`
- [ ] `mesa`
- [ ] `weston`

Exit criteria:

- no imported graphics/input source carries xv6 DRM, cursor, epoll, fd-passing,
  procfs, device-enumeration, or compositor workaround code.
- local shims are marked and separately tested.
- Chromium, WebKit, Weston, XWayland, and Wayland smoke runs remain probes of
  kernel/user ABI, not patched applications.

### Phase 4: Browser, Networked App, And Desktop Program Stack

- [ ] `netsurf-buildsystem`
- [ ] `netsurf-libwapcaplet`
- [ ] `netsurf-libparserutils`
- [ ] `netsurf-libcss`
- [ ] `netsurf-libnsbmp`
- [ ] `netsurf-libnsgif`
- [ ] `netsurf-libnslog`
- [ ] `netsurf-libnspsl`
- [ ] `netsurf-libnsutils`
- [ ] `netsurf-libhubbub`
- [ ] `netsurf-libsvgtiny`
- [ ] `netsurf-nsgenbind`
- [ ] `netsurf-libdom`
- [ ] `netsurf`
- [ ] `cpython`
- [ ] `vim`
- [ ] `peanut-gb`
- [ ] `wayland-src`
- [ ] `webkit`
- [ ] `wayland`

Exit criteria:

- network, TLS, DNS, WebKit, and browser behavior is fixed through Linux ABI
  compatibility or sysroot data.
- imported application source contains no xv6 runtime workaround except marker.
- NetSurf/WebKit/Chromium-style runs are validation workloads only.

### Phase 5: Local User Programs And Libraries

- [ ] `user/lib`
- [ ] `user/lib/x86_64`
- [ ] all `user/programs/*` entries listed in this plan

Exit criteria:

- production commands have no imported-app ABI workaround role.
- probes/tests are clearly marked.
- common helpers are either generic libc-style helpers or test-only helpers.

## Validation Gates

Run these after each package group:

- `git diff --check`
- inventory scanner reports zero disallowed ABI adaptation in imported source.
- package build target for each touched package.
- narrow kernel build if a compatibility gap moved into kernel.
- relevant rootfs/image refresh only when staged content changes.
- GUI smoke for desktop-facing changes.
- Chromium/WebKit/NetSurf only as probes, with evidence tied to same-boot logs,
  argv/exe/fd/socket/surface/SCM data, and screenshots where useful.

Final completion gate:

- every item in this plan is classified.
- every imported source tree is marker-only relative to upstream.
- every remaining xv6-owned shim has a documented ABI surface and tests.
- no package patch infrastructure can apply untracked behavioral source changes
  without the hygiene gate failing.

## Tracking Table Template

Use this table for each phase as the generated inventory matures:

| Item | Kind | Upstream ref | Current delta | ABI adaptation? | Target owner | Status |
| --- | --- | --- | --- | --- | --- | --- |
| example | imported-source | tag/commit | marker only | no | wrapper | done |

Temporary ABI workaround status values:

- `found`: source adaptation exists and has not been assigned.
- `assigned-kernel`: fix belongs in kernel ABI work.
- `assigned-libc`: fix belongs in libc/sysroot/runtime data.
- `assigned-wrapper`: fix belongs in build/staging wrapper.
- `local-shim`: xv6-owned compatibility package.
- `marker-only`: source is clean except explicit marker.
- `done`: source is clean and validation passed.
