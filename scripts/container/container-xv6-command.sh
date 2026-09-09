#!/usr/bin/env bash
set -euo pipefail

command_name="$(basename "$0")"

source_dir="${XV6_SOURCE_DIR:-}"
if [[ -z "${source_dir}" ]]; then
    if [[ -f "./CMakeLists.txt" && -d "./kernel" && -d "./user" && -d "./ports" ]]; then
        source_dir="${PWD}"
    else
        source_dir="/src/xv6-os"
    fi
fi

arch="${XV6_ARCH:-x86_64}"
build_dir="${XV6_BUILD_DIR:-${source_dir}/build-${arch}}"
jobs="${XV6_PARALLEL_JOBS:-2}"
apt_metadata_refreshed=0

cmake_args=(
    -S "${source_dir}"
    -B "${build_dir}"
    -G Ninja
    -DXV6_ARCH="${arch}"
    -DXV6_PARALLEL_JOBS="${jobs}"
)
if [[ -n "${XV6_WEBKIT_REF_SYSROOT:-}" ]]; then
    cmake_args+=(
        -DXV6_WEBKIT_REF_SYSROOT="${XV6_WEBKIT_REF_SYSROOT}"
        -DXV6_WEBKIT_STRICT_STAGE=ON
    )
fi

usage() {
    cat <<'USAGE'
xv6-os container usage

Quick start (from host):
  scripts/container/enter-container.sh xv6-build   # one-shot: start container and build everything
  scripts/container/enter-container.sh             # interactive shell inside container

Build commands (inside container or via enter-container.sh <cmd>):
  xv6-build           clean container reproduction with an immutable receipt
  xv6-reproduce       alias for xv6-build
  xv6-build-incremental  reuse build tree and build the world target
  xv6-kernel-x86      configure x86_64 and build only the kernel
  xv6-user-ports      build user programs and all ports
  xv6-images          build fs.img, initrd.cpio.gz, and boot.img
  xv6-rootfs-refresh  refresh fs.img from the existing staged sysroot
  xv6-hyperv-image    build a Hyper-V Gen2 bootable xv6-hyperv.vhdx

Launch commands (inside container):
  xv6-launch          refused in-container; launch on the host for VM safety
  xv6-check-gui-accel check host/container KVM, DRI, and udmabuf devices

Environment:
  XV6_SOURCE_DIR        source checkout          default: current repo or /src/xv6-os
  XV6_BUILD_DIR         build directory          default: \$XV6_SOURCE_DIR/build-\$XV6_ARCH
  XV6_ARCH              target arch              default: x86_64
  XV6_PARALLEL_JOBS     build parallelism        default: 2
  XV6_KEEP_ITERATIONS   completed receipts       default/max: 3
  DISPLAY_MODE          qemu display mode        default for launch: nographic
  XV6_WEBKIT_REF_SYSROOT  optional WebKitGTK runtime sysroot

GUI acceleration:
  Pass --device /dev/dri --device /dev/udmabuf --device /dev/kvm and mount the
  host display socket. Without /dev/dri QEMU falls back to llvmpipe (slow).
  Without /dev/udmabuf, virtio-gpu blob hostmem is disabled.
  enter-container.sh forwards all of these automatically when present.

WebKit:
  The repo does not carry ports/webkit/sysroot. Provide a host-glibc runtime via
  XV6_WEBKIT_REF_SYSROOT or build one with scripts/container/docker-build-webkit.sh.
USAGE
}

configure() {
    cmake "${cmake_args[@]}"
}

cleanup_refreshed_apt_metadata() {
    if [[ "${apt_metadata_refreshed}" == "1" &&
          "${XV6_KEEP_APT_LISTS:-0}" != "1" ]]; then
        rm -rf /var/lib/apt/lists/*
    fi
}

ensure_apt_metadata() {
    if compgen -G '/var/lib/apt/lists/*_Packages' >/dev/null; then
        return 0
    fi
    if [[ "$(id -u)" -ne 0 || ! -x /usr/bin/apt-get ]]; then
        echo "xv6-command: KDE image generation needs APT package metadata; run apt-get update" >&2
        return 1
    fi

    echo "xv6-command: refreshing temporary APT metadata for the KDE runtime image" >&2
    apt-get update
    apt_metadata_refreshed=1
    trap cleanup_refreshed_apt_metadata EXIT
}

fix_build_ownership() {
    # The dev container runs as root, so artifacts written into the
    # bind-mounted build dir end up root-owned on the host and block
    # host-side tools (e.g. qemu reading fs.img). Re-own the build dir to
    # match the bind-mounted source tree's owner.
    [[ "$(id -u)" -eq 0 ]] || return 0
    local owner
    owner="$(stat -c '%u:%g' "${source_dir}" 2>/dev/null)" || return 0
    [[ -n "${owner}" && "${owner}" != "0:0" ]] || return 0
    [[ -d "${build_dir}" ]] && chown -R "${owner}" "${build_dir}" 2>/dev/null || true
}

build_targets() {
    configure
    cmake --build "${build_dir}" --target "$@" -j "${jobs}"
    fix_build_ownership
}

build_complete_x86() {
    ensure_apt_metadata
    build_targets world
    if [[ "${arch}" == "x86_64" ]]; then
        "${source_dir}/scripts/build/build-qemu-apt-sdl-modules.sh"
        fix_build_ownership
    fi
}

case "${command_name}" in
    xv6-help|xv6-command)
        usage
        ;;
    xv6-build|xv6-reproduce)
        "${source_dir}/scripts/build/reproduce-in-container.sh"
        if [[ "${arch}" == "x86_64" ]]; then
            "${source_dir}/scripts/build/build-qemu-apt-sdl-modules.sh"
            fix_build_ownership
        fi
        ;;
    xv6-build-incremental)
        build_complete_x86
        ;;
    xv6-kernel-x86)
        arch="x86_64"
        build_dir="${XV6_BUILD_DIR:-${source_dir}/build-x86_64}"
        cmake_args=(
            -S "${source_dir}"
            -B "${build_dir}"
            -G Ninja
            -DXV6_ARCH="x86_64"
            -DXV6_PARALLEL_JOBS="${jobs}"
        )
        build_targets kernel
        ;;
    xv6-user-ports)
        build_targets user ports
        ;;
    xv6-images)
        ensure_apt_metadata
        build_targets rootfs initrd image
        ;;
    xv6-rootfs-refresh)
        ensure_apt_metadata
        build_targets rootfs-refresh
        ;;
    xv6-hyperv-image)
        ensure_apt_metadata
        build_targets hyperv-image
        ;;
    xv6-launch|xv6-launch-nokvm|xv6-qemu-nokvm)
        echo "xv6-command: refusing in-container QEMU because it cannot reliably inventory external host VMs" >&2
        echo "xv6-command: run scripts/launch/launch-gui.sh on the host" >&2
        exit 75
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
