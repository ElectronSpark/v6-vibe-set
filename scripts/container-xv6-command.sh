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

if [[ -n "${XV6_PREBUILT_TOOLCHAIN_PREFIX:-}" ]]; then
    cmake_args+=(-DXV6_PREBUILT_TOOLCHAIN_PREFIX="${XV6_PREBUILT_TOOLCHAIN_PREFIX}")
fi

configure() {
    cmake "${cmake_args[@]}"
}

build_targets() {
    configure
    cmake --build "${build_dir}" --target "$@" -j "${jobs}"
}

case "${command_name}" in
    xv6-toolchain)
        build_targets toolchain
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
        if [[ -n "${XV6_PREBUILT_TOOLCHAIN_PREFIX:-}" ]]; then
            cmake_args+=(-DXV6_PREBUILT_TOOLCHAIN_PREFIX="${XV6_PREBUILT_TOOLCHAIN_PREFIX}")
        fi
        build_targets kernel
        ;;
    xv6-user-ports)
        build_targets user ports
        ;;
    xv6-images)
        build_targets rootfs initrd image
        ;;
    xv6-qemu-nokvm)
        configure
        cmake --build "${build_dir}" --target kernel rootfs -j "${jobs}"
        USE_KVM=0 DISPLAY_MODE="${DISPLAY_MODE:-nographic}" \
            cmake --build "${build_dir}" --target qemu
        ;;
    *)
        cat >&2 <<'USAGE'
usage: run through one of these command names:
  xv6-toolchain    configure and build the cross toolchain
  xv6-kernel-x86   configure x86_64 and build the kernel
  xv6-user-ports   build user programs and all ports
  xv6-images       build fs.img, initrd.cpio.gz, and boot.img
  xv6-qemu-nokvm   boot x86_64 in QEMU with USE_KVM=0

Environment:
  XV6_SOURCE_DIR                 source checkout, default: current repo or /src/xv6-os
  XV6_BUILD_DIR                  build directory, default: $XV6_SOURCE_DIR/build-$XV6_ARCH
  XV6_ARCH                       target arch, default: x86_64
  XV6_PARALLEL_JOBS              build jobs, default: 2
  XV6_PREBUILT_TOOLCHAIN_PREFIX  optional prebuilt toolchain root
  DISPLAY_MODE                   qemu display, default for xv6-qemu-nokvm: nographic
USAGE
        exit 2
        ;;
esac