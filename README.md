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
The default QEMU GUI path is the version-pinned, aspect/input-corrected SDL GL
frontend with a primary virgl adapter. It displays the KDE-only 1280x800 guest
without stretching it to the host window and maps absolute input through the
same fitted viewport.

## Graphics API scope

The desktop is Wayland/EGL-first. Native Wayland EGL clients are the supported
OpenGL presentation path, and backend acceleration is gated by the render
backend's `FB_GPU_BACKEND_F_OPENGL_SUBMIT` capability. GLX is intentionally not
implemented: there is no X server in the normal session, and GLX clients should
be treated as unsupported rather than silently routed through an xv6-private
compatibility path.

## Build and launch

The maintained path is one command. It rebuilds the development container,
performs a clean complete x86_64 build from the bind-mounted workspace,
validates the KDE-only ext4 image, builds the patched QEMU 9.0.2 SDL frontend,
and writes an immutable receipt with source/tool provenance and hashes:

```sh
git submodule update --init --recursive
scripts/container/reproduce-workspace.sh

# Host-only launch from the newest verified receipt.
scripts/launch/launch-gui.sh
```

At most three completed receipts are kept under
`build-reproductions/x86_64/`. Before a fourth is created, the oldest marked,
unprotected, confirmed-unused receipt is removed. Add a `.keep` file to a
deployed receipt to exclude it from automatic cleanup. The mutable CMake tree
is singular at `build-x86_64`, so intermediate objects are not triplicated.

The container base is pinned by digest. Each receipt also records the exact
QEMU source archive, Chrome-for-Testing version/archive checksum, KDE package
install order and archive checksums, Game Boy ROM checksum, generated WebKit
media checksums, and the container package inventory. Cached downloads are
accepted only after those locks pass; changing a lock makes the build fetch and
validate the replacement instead of silently reusing mutable content.
The validated KDE package order and archive hashes are also checked into
`scripts/locks/kde-noble/`, allowing a cold-cache clone to download exact
package versions before enforcing the same receipt lock.

The launcher waits for every exact `qemu-system-*`/`qemu-kvm` executable to
exit naturally and never signals an external VM. Its own QEMU receives a
unique token, PID/start-time/process-group verification, synchronous reap, and
a final exact-zero check. The receipt's rootfs stays immutable: each run uses a
private qcow2 overlay that is deleted only after its owned QEMU exits.

For a deliberate headless/non-KVM launch, keep the same owned host launcher:

```sh
USE_KVM=0 DISPLAY_MODE=nographic scripts/launch/launch-gui.sh
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

Build the container and reproduce the complete kernel/image in one command:

```sh
scripts/container/reproduce-workspace.sh
```

The helper detects either native `docker` or Docker Desktop's `docker.exe` on
WSL. For an already-built image, the equivalent build command is:

```sh
XV6_UID=$(id -u) XV6_GID=$(id -g) docker compose run --rm xv6 xv6-build
```

`enter-container.sh` exports `XV6_UID`/`XV6_GID` automatically, so the
helper is the more convenient interface for interactive use.  Either way the
container runs as your host user and all generated output stays owned by you.
QEMU is intentionally not launched inside Docker because Docker Desktop cannot
reliably expose the surrounding WSL distribution's process inventory. Launch
from the host so the external-VM gate is authoritative.
Before building, the wrapper also requires the bind-mounted source to expose
the expected executable build script and exact Git commit, with a bounded
retry for Docker Desktop's new-path mount-registration delay.

Other useful one-liners (all via the helper or with the UID prefix above):

```sh
scripts/container/enter-container.sh xv6-build         # clean receipted build
scripts/container/enter-container.sh xv6-build-incremental # explicit reuse
scripts/container/enter-container.sh bash              # interactive shell
scripts/container/enter-container.sh xv6-help          # list all container commands
scripts/container/enter-container.sh xv6-hyperv-image  # build Hyper-V VHDX
scripts/launch/launch-gui.sh                            # safe host launch
```

The `compose.yml` at the repo root defines the build/development container and
bind-mounts the source tree.
If `/dev/udmabuf` is not present, create it first with:

```sh
sudo modprobe udmabuf
```

`scripts/container/enter-container.sh` is the interactive/one-shot container
wrapper. It redirects legacy launch aliases back to the safe host launcher:

```sh
scripts/container/enter-container.sh xv6-launch   # host launch, not container QEMU
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
