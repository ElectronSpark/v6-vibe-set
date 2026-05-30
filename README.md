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
The default QEMU GUI path uses a virtio tablet for absolute pointer input and
forces QEMU's GTK process to `GDK_SCALE=1`/`GDK_DPI_SCALE=1`, keeping the guest
framebuffer and host window at a predictable 1:1 scale.  VMware absolute pointer
support is still available with `QEMU_VMMOUSE=1` for debugging.  The `rootfs`
target builds
`build-x86_64/fs.img`; `qemu` boots it with GTK display and user-mode
networking.

## Graphics API scope

The desktop is Wayland/EGL-first. Native Wayland EGL clients are the supported
OpenGL presentation path, and backend acceleration is gated by the render
backend's `FB_GPU_BACKEND_F_OPENGL_SUBMIT` capability. GLX is intentionally not
implemented: there is no X server in the normal session, and GLX clients should
be treated as unsupported rather than silently routed through an xv6-private
compatibility path.

## Build and launch

These are the commands to use from a clean checkout. The root filesystem image
is generated from the staged sysroot; no prebuilt sysroot is meant to live in
the repository.

```sh
# fetch sub-repos
git submodule update --init --recursive

# configure
cmake -S . -B build-x86_64 -G Ninja -DXV6_ARCH=x86_64 -DXV6_PARALLEL_JOBS=2

# build the OS: kernel, host-glibc userland, ports, and ext4 rootfs
cmake --build build-x86_64 --target world -j2

# launch the GUI OS
./scripts/launch/launch-gui.sh
```

For a headless/non-KVM launch, use the CMake QEMU target:

```sh
USE_KVM=0 DISPLAY_MODE=nographic cmake --build build-x86_64 --target qemu
```

The QEMU launcher reads `build-x86_64/kernel/kernel.elf` and
`build-x86_64/fs.img` by default. If either file is missing, rebuild `kernel`
and `rootfs`:

```sh
cmake --build build-x86_64 --target kernel rootfs -j2
```

The build-local sysroot is always `${build_dir}/sysroot`. Do not set
`XV6_SYSROOT` to a path outside the CMake build directory; configure will reject
that so generated runtimes and downloaded dependency caches do not spill into
the source tree or another checkout.

## Hyper-V

The x86_64 kernel can also boot as a Generation 2 Hyper-V VM. Build the VHDX
from the same kernel and rootfs artifacts:

```sh
cmake --build build-x86_64 --target hyperv-image -j2
```

This writes `build-x86_64/xv6-hyperv.vhdx`. Create a Generation 2 VM, disable
Secure Boot, attach that VHDX as the boot disk, assign the desired CPU count,
and start it. The Hyper-V path uses a small repo-built EFI loader that passes
the firmware memory map, ramdisk, and GOP framebuffer to the kernel. The VHDX,
ESP image, and loader objects are generated under the build directory only.

See `scripts/README.md` for the maintained helper scripts and what generated
artifacts should stay out of the repository.

## Docker

Build the development image:

```sh
docker compose build
```

Build everything and launch the GUI in one command:

```sh
docker compose run --rm xv6 xv6-launch
```

This builds the full OS (kernel, userland, ports, `fs.img`) and boots QEMU with
KVM and a GTK display window.  `scripts/launch/launch-gui.sh` also triggers this
build automatically when artifacts are missing.

Other useful one-liners:

```sh
docker compose run --rm xv6 xv6-build         # build only (no launch)
docker compose run --rm xv6 bash              # interactive shell
docker compose run --rm xv6 xv6-help          # list all container commands
docker compose run --rm xv6 xv6-hyperv-image  # build Hyper-V VHDX
docker compose run --rm xv6 xv6-launch-nokvm  # build + boot without KVM
```

The `compose.yml` at the repo root defines the container: it bind-mounts the
source tree, forwards the host Wayland/X11 display sockets, and exposes
`/dev/kvm`, `/dev/dri`, and `/dev/udmabuf`.  Remove any `devices:` entry your
host does not have.  If `/dev/udmabuf` is not present, create it first with:

```sh
sudo modprobe udmabuf
```

`scripts/container/enter-container.sh` is a thin wrapper around
`docker compose run --rm xv6` for convenience:

```sh
scripts/container/enter-container.sh xv6-launch   # same as docker compose run
scripts/container/enter-container.sh               # interactive shell
```

For fail-fast accelerated launches from the host, add:

```sh
QEMU_REQUIRE_HOST_DRI=1 QEMU_REQUIRE_UDMABUF=1 ./scripts/launch/launch-gui.sh
```

For WebKitGTK, provide a host-glibc WebKit runtime sysroot explicitly. The repo
does not commit `ports/webkit/sysroot`:

```sh
scripts/container/docker-build-webkit.sh /path/to/host-glibc-webkit-sysroot
```

The Docker image installs the GStreamer media packages needed by WebKit video
playback (`gstreamer1.0-libav`, base/good/bad plugins, and tools). During the
ports build, `ports/webkit/stage-webkit-runtime.sh` stages those installed
packages, or downloads the same packages into a build-local cache, then copies
the needed runtime files into the generated sysroot. Those downloaded packages,
codec libraries, and generated sysroots remain build artifacts and should not be
committed.

## Useful targets

```sh
cmake --build build-x86_64 --target help-targets
cmake --build build-x86_64 --target kernel -j2
cmake --build build-x86_64 --target user -j2
cmake --build build-x86_64 --target ports -j2
cmake --build build-x86_64 --target rootfs -j2
cmake --build build-x86_64 --target webkit-runtime-check -j2
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
