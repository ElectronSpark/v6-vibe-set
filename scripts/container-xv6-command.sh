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

cmake_args=(
    -S "${source_dir}"
    -B "${build_dir}"
    -G Ninja
    -DXV6_ARCH="${arch}"
    -DXV6_PARALLEL_JOBS="${jobs}"
)

usage() {
    cat <<'USAGE'
xv6-os container usage

Build the OS:
  xv6-build           configure and build kernel, userland, ports, and fs.img
  xv6-kernel-x86      configure x86_64 and build only the kernel
  xv6-user-ports      build user programs and all ports
  xv6-images          build fs.img, initrd.cpio.gz, and boot.img

Launch the OS:
  xv6-launch-nokvm    build kernel/rootfs, then boot QEMU with USE_KVM=0
  xv6-qemu-nokvm      alias for xv6-launch-nokvm

Shell:
  docker run --rm -it <image> bash

Environment:
  XV6_SOURCE_DIR                 source checkout, default: current repo or /src/xv6-os
  XV6_BUILD_DIR                  build directory, default: $XV6_SOURCE_DIR/build-$XV6_ARCH
  XV6_ARCH                       target arch, default: x86_64
  XV6_PARALLEL_JOBS              build jobs, default: 2
  DISPLAY_MODE                   qemu display, default for launch: nographic
  XV6_WEBKIT_REF_SYSROOT         optional mounted WebKitGTK runtime sysroot

WebKit runtime note:
  The repository does not carry ports/webkit/sysroot. To include WebKitGTK,
  provide a host-glibc runtime sysroot through XV6_WEBKIT_REF_SYSROOT or use
  scripts/docker-build-webkit.sh <webkit-ref-sysroot>.
USAGE
}

configure() {
    cmake "${cmake_args[@]}"
}

build_targets() {
    configure
    cmake --build "${build_dir}" --target "$@" -j "${jobs}"
}

case "${command_name}" in
    xv6-help|xv6-command)
        usage
        ;;
    xv6-build)
        build_targets world
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
        build_targets rootfs initrd image
        ;;
    xv6-launch-nokvm|xv6-qemu-nokvm)
        configure
        cmake --build "${build_dir}" --target kernel rootfs -j "${jobs}"
        USE_KVM=0 DISPLAY_MODE="${DISPLAY_MODE:-nographic}" \
            cmake --build "${build_dir}" --target qemu
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
