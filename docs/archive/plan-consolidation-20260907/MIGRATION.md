# xv6-os Migration Plan — remaining work

Phases 1, 2, 3 (template) are **done**. This file tracks what's left.

## What's done

### Phase 1 — kernel sub-repo ✅
- `kernel/`, `arch/`, `conf/` lifted verbatim from `xv6-tmp`.
- Standalone `kernel/CMakeLists.txt` (top-level CFLAGS, ARCH/PLATFORM/
  OPT/LAB cache vars, toolchain detection — trimmed from xv6-tmp's
  monolithic top CMakeLists). User/mkfs/fs.img/qemu sections removed.
- `kernel/Makefile` is a thin cmake wrapper.
- Validated: configures cleanly with stock `riscv64-unknown-elf-gcc`.

### Phase 2 — user (essentials) sub-repo ✅
- 62 xv6-native programs under `user/programs/<name>/<name>.c`.
- `user/lib/`: ulib + printf + umalloc + fsutil + linker scripts +
  usys.pl (riscv + x86_64 variants).
- Standalone `user/CMakeLists.txt` with auto-discovery of
  `programs/*` subdirs (no manual list to maintain).
- `user/cmake/AddXv6Program.cmake` defines the `xv6_user_program()`
  helper.
- Validated: configures cleanly, 62 programs detected.

### Phase 3 — ports sub-repo (zlib template) ✅
- `ports/cmake/AddPort.cmake` — generic helper supporting
  `BUILD_SYSTEM=cmake|autoconf|make`.
- `ports/CMakeLists.txt` — auto-discovers `ports/<name>/` subdirs.
- `ports/zlib/` — vendored from `xv6-tmp/user/zlib/`, standalone
  `CMakeLists.txt`, **validated end-to-end with host gcc**: produces
  `libz.a` + `zlib.h` in the sysroot.

## Phase 0 — toolchain sub-repo ✅

- `toolchain/scripts/build_gcc_toolchain.sh` lifted verbatim from
  `xv6-tmp/scripts/`, with the musl-xv6 overlay path retargeted to
  `toolchain/musl-xv6/`.
- `toolchain/musl-xv6/` lifted from `xv6-tmp/user/musl-xv6/`.
- `cmake/BuildToolchain.cmake` rewritten to invoke the script in two
  phases (`tc-phase1`, `tc-phase2`) with aliases `tc-binutils`,
  `tc-gcc-stage1`, `tc-musl`, `tc-gcc-stage2` so kernel/user/ports
  pick the right minimum stage.
- `XV6_TOOLCHAIN_BIN` updated to `${PREFIX}/${arch}/phase2/bin`
  (matches the script's actual layout).
- Validated: `--help` parses, overlay files resolve, umbrella
  configures cleanly. Actual end-to-end build is hours-long and
  needs the host prereqs (gmp, mpfr, mpc, expat dev) — left to CI.

## Image / GUI bring-up ✅

- `scripts/make-rootfs.sh` is wired into `cmake/BuildImage.cmake` as the
  `rootfs` target; it builds `fs.img` from the populated sysroot.
- The rootfs builder mirrors `bin/`, `lib/`, `usr/`, `share/`, `etc/`,
  top-level sysroot files, and `rootfs-overlay/`, then stages the musl
  loader/libc and GCC runtime shared libraries from the phase-2 toolchain.
- Default x86_64 image size is 1536 MiB for the current GUI, Python, and
  ports payload.
- `rootfs-overlay/etc/startup` starts `/bin/desktop`; the Wayland port
  stages `desktop` and rebuilds patched `wlcomp` as static binaries so
  the GUI reaches a stable desktop in QEMU GTK. NetSurf launch points are
  inert until the browser is stable.
- QEMU GUI input prefers the kernel VMware absolute pointer path and falls
  back to PS/2 relative packets. A polling drain backs up delivery, and
  `scripts/run-qemu.sh` requests
  `-display gtk,grab-on-hover=on` so pointer movement is delivered without
  requiring a fragile click-to-grab sequence.
- The internal file manager reads directories directly with
  `opendir()`/`readdir()`/`stat()` and avoids overlapping title/path
  formatting during directory navigation.
- GTK/Wayland runtime data is staged into the sysroot: xkeyboard-config
  under `/share/X11/xkb` and the Adwaita/default cursor theme under
  `/share/icons`, matching the paths compiled into libxkbcommon and
  libwayland-cursor.
- The kernel routes musl `SYS_fallocate` (990) to a compatibility stub that
  returns `-EOPNOTSUPP`; Wayland cursor SHM setup then falls back to
  `ftruncate()`, avoiding the GTK cursor-theme assertion seen during NetSurf
  startup.
- `user/programs/mousetest/` provides a small `/dev/mouse` diagnostic.

## Phase 2.5 — musl-linked user programs (DEFERRED)

`xv6-tmp/user/CMakeLists.txt` defines an `add_musl_program()` family
(in `arch/${ARCH}/cmake/musl.cmake`) that builds programs linking
against musl. These are a **separate** set from the 62 xv6-native
programs already moved. Examples: `dash`, the dynamic linker test
programs, etc.

**Why deferred**: they need (a) the real custom musl-aware toolchain
(Phase 0), (b) the `musl-xv6/` overlay tree, and (c) the
`musl_sysroot` cmake target. None are available locally.

**Action when ready**: copy `xv6-tmp/arch/${ARCH}/cmake/musl.cmake`
into `xv6-os/cmake/musl.cmake`, copy `xv6-tmp/user/musl-xv6/` into
`toolchain/musl-xv6/`, then add an `add_musl_program()` family to
`user/cmake/AddXv6Program.cmake`. The musl-program list lives further
down in `xv6-tmp/user/CMakeLists.txt` (search for
`add_musl_program(` and `add_musl_dynamic_program(`).

## Phase 3 expansion — remaining ~59 ports

To enumerate every port in xv6-tmp:

```sh
find xv6-tmp/user -maxdepth 1 -type d \
    -not -name user -not -name x86_64 -not -name musl-xv6 \
    -not -name __pycache__ -printf '%f\n' | sort
```

**Per-port recipe** (validated on zlib):

1. Copy `xv6-tmp/user/<name>/` into `xv6-os/ports/<name>/src/` (and
   `rm -rf <name>/src/.git` if upstream included one).
2. Read its build section in `xv6-tmp/user/CMakeLists.txt` (use
   `grep -n "<name>" user/CMakeLists.txt`).
3. Write `xv6-os/ports/<name>/CMakeLists.txt`:
   ```cmake
   cmake_minimum_required(VERSION 3.16)
   project(xv6-port-<name> LANGUAGES NONE)
   include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)
   xv6_port(
       NAME            <name>
       SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
       BUILD_SYSTEM    cmake             # or autoconf, make
       OUTPUT_FILES    lib/libfoo.a include/foo.h
       CMAKE_ARGS      -D...
       DEPENDS         zlib              # if any inter-port deps
   )
   ```
4. Build standalone with the same incantation as zlib (see top README).
5. Verify outputs land in `${XV6_SYSROOT}`.

### Port dependency graph (partial, from libpng's xv6-tmp section)

```
musl_sysroot
├── zlib                  ✅ done
│   └── libpng
├── libjpeg-turbo
├── libexpat
├── freetype
├── curl
└── netsurf  (composite — depends on the above 5)
```

Full graph requires reading every port section in
`xv6-tmp/user/CMakeLists.txt` (lines 1280–3834). Encode each edge via
`xv6_port(... DEPENDS ...)`.

## Why incremental?

The faithful refactor of `user/CMakeLists.txt` (3,834 lines, ~60
ports + bespoke meson cross-files for python/gtk/webkit + sysroot
population logic) is a multi-day effort that risks breaking the
working xv6-tmp build. The scaffold is now structurally complete and
each phase has been validated; the remaining ports are mechanical
copies guided by the recipe above.
