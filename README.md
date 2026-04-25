# xv6-os

Umbrella for building a self-hosted xv6-derived OS: cross toolchain,
kernel, essential userland, ported third-party software, and bootable
ext4 root image / qemu launcher - all driven from a single top-level CMake.

## Layout

```
xv6-os/                        umbrella (this repo)
├── CMakeLists.txt             top orchestrator
├── cmake/
│   ├── BuildToolchain.cmake   binutils → gcc-stage1 → musl → gcc-stage2
│   ├── BuildKernel.cmake      drives kernel/ standalone cmake
│   ├── BuildUser.cmake        drives user/ standalone cmake
│   └── BuildPorts.cmake       drives ports/ standalone cmake
│
├── toolchain/                 SUBREPO: cross gcc + musl per arch
├── kernel/                    SUBREPO: kernel sources + own CMake
├── user/                      SUBREPO: ~60 essential userland programs
└── ports/                     SUBREPO: heavy third-party (one CMake per port)
```

The four sub-repos are **independent**:

* `toolchain/` depends on nothing.
* `kernel/` depends only on the cross gcc (freestanding).
* `user/` depends on the cross gcc (xv6-native programs are -nostdlib).
* `ports/` depends on the full toolchain + a populated musl sysroot.

The umbrella is the only place that knows how to wire them together.

## Status

| sub-repo     | state                                                              |
|--------------|--------------------------------------------------------------------|
| `toolchain/` | **populated**: two-phase GCC+musl build script + xv6 musl overlay |
| `kernel/`    | **populated** from xv6-tmp; x86_64 boots in QEMU                   |
| `user/`      | **populated**: ~60 xv6-native programs + userlib                   |
| `ports/`     | **populated**: GUI/Python/NetSurf stack in progress                |

The umbrella `CMakeLists.txt` + `cmake/Build*.cmake` wire all four
together. Phase 2.5 (musl-linked user programs) and Phase 3 expansion
(remaining ~59 ports) are queued in [MIGRATION.md](MIGRATION.md).

Current x86_64 bring-up reaches the Wayland desktop (`/bin/desktop` ->
`wlcomp`) from an ext4 rootfs mounted over virtio-blk. The desktop and
compositor are rebuilt as static binaries by `port-wayland`, so the GUI
does not depend on dynamic loader state during early session startup.
The VMware absolute pointer path is the preferred QEMU input path, with
PS/2 relative packets retained as fallback; `launch-gui.sh` uses GTK
grab-on-hover so pointer motion reaches the guest as soon as the host
pointer enters the window. The `rootfs` target builds
`build-x86_64/fs.img`; `qemu` boots it with GTK display and user-mode
networking.

Launch the current x86_64 GUI image with one command:

```sh
./scripts/launch-gui.sh
```

## Quick start

```sh
# fetch sub-repos (after editing scripts/setup-submodules.sh URLs)
./scripts/setup-submodules.sh

# configure for riscv64 in an out-of-tree build dir
cmake -S . -B build-riscv64 -DXV6_ARCH=riscv64

# build everything (toolchain -> sysroot -> user + ports -> kernel -> rootfs)
cmake --build build-riscv64 -j
```

## Standalone sub-repo builds

Each sub-repo can also be built independently (no umbrella) for
faster iteration on a single layer:

```sh
# Kernel only (uses any RISC-V gcc on PATH, or set TOOLPREFIX):
cmake -S kernel -B kernel/build -DARCH=riscv -DPLATFORM=qemu
cmake --build kernel/build -j --target kernel

# Userland only (cross gcc + sysroot path):
cmake -S user -B user/build -DARCH=riscv \
    -DCMAKE_C_COMPILER=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-gcc \
    -DCMAKE_INSTALL_PREFIX=$XV6_SYSROOT
cmake --build user/build -j --target install

# A single port (e.g. zlib):
cmake -S ports/zlib -B ports/zlib/build \
    -DCMAKE_C_COMPILER=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-gcc \
    -DCMAKE_AR=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-ar \
    -DCMAKE_RANLIB=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-ranlib \
    -DXV6_SYSROOT=$XV6_SYSROOT \
    -DXV6_PORT_CFLAGS="--sysroot=$XV6_SYSROOT -O2 -fPIC -nostdinc -ffreestanding -march=rv64gc -mabi=lp64d -mcmodel=medany -isystem $XV6_SYSROOT/include"
cmake --build ports/zlib/build
```

## Build-time caveat

The full kernel + ports compile expects xv6-tmp's custom
`riscv64-xv6-linux-musl-` toolchain (patched gcc that accepts
`-mcmodel=large` + musl with the xv6 syscall overlay). With a stock
distro `riscv64-unknown-elf-gcc`:

* The kernel **configures** but fails at `-mcmodel=large`.
* The userland **configures** (62 programs detected) but link fails
  without the xv6 user.ld + a matching toolchain.
* Ports work with **any** matching cross toolchain (zlib was validated
  end-to-end with the host gcc to prove the wrapper).

Lifting `xv6-tmp/scripts/build_gcc_toolchain.sh` into `toolchain/` is
**Phase 0** — it produces the real toolchain that unlocks the full
build.
