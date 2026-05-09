# xv6-os — top-level orchestrator.
#
# Responsibilities:
#   1. Validate the supported architecture and define the sysroot layout.
#   2. Build/stage x86_64 Linux ABI userland with the host compiler + glibc.
#   3. Drive kernel, rootfs, image, and qemu targets from those artifacts.

cmake_minimum_required(VERSION 3.22)
project(xv6_os NONE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CrossOptions)        # XV6_ARCH, paths, sysroot
include(BuildKernel)         # kernel target
include(BuildUser)           # user target
include(BuildPorts)          # optional ports target
include(BuildImage)          # initrd, boot image, qemu

# Convenience aggregate: `cmake --build . --target world`
add_custom_target(world
	DEPENDS kernel user rootfs)

# Default target prints help.
add_custom_target(help-targets
	COMMAND ${CMAKE_COMMAND} -E echo "Top-level targets:"
	COMMAND ${CMAKE_COMMAND} -E echo "  user       build x86_64 userland with host compiler/glibc"
	COMMAND ${CMAKE_COMMAND} -E echo "  ports      build host-glibc ports into the sysroot"
	COMMAND ${CMAKE_COMMAND} -E echo "  kernel     build kernel"
	COMMAND ${CMAKE_COMMAND} -E echo "  kernel-sparse  run sparse lock/context checks for kernel"
	COMMAND ${CMAKE_COMMAND} -E echo "  image      build bootable image / initrd"
	COMMAND ${CMAKE_COMMAND} -E echo "  rootfs     build ext4 fs.img from sysroot (boot disk)"
	COMMAND ${CMAKE_COMMAND} -E echo "  qemu       boot image in qemu (see scripts/run-qemu.sh)"
	COMMAND ${CMAKE_COMMAND} -E echo "  world      everything above"
	VERBATIM)

message(STATUS "xv6-os configured for arch: ${XV6_ARCH}")
message(STATUS "  sysroot       : ${XV6_SYSROOT}")
