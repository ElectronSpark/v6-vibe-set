# xv6-os

Umbrella for building a self-hosted xv6-derived OS: kernel, host-glibc
Linux userland, ported third-party software, and bootable ext4 root
image / qemu launcher - all driven from a single top-level CMake.

## Layout

```
xv6-os/                        umbrella (this repo)
├── CMakeLists.txt             top orchestrator
├── cmake/
│   ├── BuildKernel.cmake      drives kernel/ standalone cmake
│   ├── BuildUser.cmake        builds user programs with host glibc
│   └── BuildPorts.cmake       drives ports/ standalone cmake
│
├── kernel/                    SUBREPO: kernel sources + own CMake
├── user/                      SUBREPO: essential host-glibc user programs
└── ports/                     SUBREPO: heavy third-party (one CMake per port)
```

The three sub-repos are **independent**:

* `kernel/` builds freestanding for x86_64 with the host compiler.
* `user/` builds Linux ABI executables with the host compiler and host glibc.
* `ports/` builds third-party software against the staged host-glibc sysroot.

The umbrella is the only place that knows how to wire them together.

## Status

| sub-repo     | state                                                              |
|--------------|--------------------------------------------------------------------|
| `kernel/`    | **populated** from xv6-tmp; x86_64 boots in QEMU                   |
| `user/`      | **populated**: host-glibc Linux executables                        |
| `ports/`     | **populated**: GUI/Python/NetSurf stack built with host glibc      |

The umbrella `CMakeLists.txt` + `cmake/Build*.cmake` wire them together.
The repo-local cross toolchain and musl libc overlay have been removed from
the x86_64 build and runtime path.

Current x86_64 bring-up reaches the Wayland desktop (`/bin/desktop` ->
`wlcomp`) from an ext4 rootfs mounted over virtio-blk. The desktop and
compositor are rebuilt as static binaries by `port-wayland`, so the GUI
does not depend on dynamic loader state during early session startup.
The default QEMU GUI path uses grabbed PS/2 relative mouse motion because it
continues to work when GTK constrains or scales the host window.  VMware
absolute pointer support is still available with `QEMU_VMMOUSE=1` for hosts
where absolute mapping is known-good.  `launch-gui.sh` uses GTK grab-on-hover
so pointer motion reaches the guest as soon as the host pointer enters the
window. The `rootfs` target builds
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

# configure for x86_64 in an out-of-tree build dir
cmake -S . -B build-x86_64 -DXV6_ARCH=x86_64

# build the kernel, host-glibc userland, ports, and rootfs
cmake --build build-x86_64 --target kernel user -j2
cmake --build build-x86_64/ports -j2
cmake --build build-x86_64 --target rootfs -j2
```

## Standalone sub-repo builds

Each sub-repo can also be built independently (no umbrella) for
faster iteration on a single layer:

```sh
# Kernel only:
cmake -S kernel -B kernel/build -DARCH=x86_64 -DPLATFORM=qemu
cmake --build kernel/build -j --target kernel

# Userland is built by the umbrella host-glibc probe/program builder:
cmake --build build-x86_64 --target user -j2

# A single port (e.g. zlib):
cmake -S ports/zlib -B ports/zlib/build \
    -DCMAKE_C_COMPILER=cc \
    -DCMAKE_AR=ar \
    -DCMAKE_RANLIB=ranlib \
    -DXV6_SYSROOT=$XV6_SYSROOT \
    -DXV6_PORT_CFLAGS="-O2 -fPIC -isystem $XV6_SYSROOT/include"
cmake --build ports/zlib/build
```
