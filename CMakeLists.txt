# xv6-os — top-level orchestrator.
#
# Responsibilities:
#   1. Validate XV6_ARCH and define the per-arch sysroot/install layout.
#   2. Drive the four sub-projects via ExternalProject_Add with a real
#      dependency graph (toolchain → user/ports/kernel).
#   3. Drive image / initrd / qemu targets that consume the cross-built
#      artifacts.
#
# This CMakeLists is HOST-side. The cross toolchain file produced by the
# toolchain build is consumed by the user/ and ports/ ExternalProjects,
# which re-enter CMake in cross mode.

cmake_minimum_required(VERSION 3.22)
project(xv6_os NONE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CrossOptions)        # XV6_ARCH, paths, triple, sysroot
include(BuildToolchain)      # toolchain target (binutils + gcc + musl)
include(BuildKernel)         # kernel target (native Make, CROSS_COMPILE=...)
include(BuildUser)           # user target (cross CMake)
include(BuildPorts)          # ports target (cross CMake, per-port chain)
include(BuildImage)          # initrd, boot image, qemu

# Convenience aggregate: `cmake --build . --target world`
add_custom_target(world
	DEPENDS toolchain kernel user ports image rootfs)

# Default target prints help.
add_custom_target(help-targets
	COMMAND ${CMAKE_COMMAND} -E echo "Top-level targets:"
	COMMAND ${CMAKE_COMMAND} -E echo "  toolchain  build cross binutils+gcc+musl into ${XV6_SYSROOT}"
	COMMAND ${CMAKE_COMMAND} -E echo "  user       cross-build essential userland into ${XV6_SYSROOT}"
	COMMAND ${CMAKE_COMMAND} -E echo "  ports      cross-build all ports into ${XV6_SYSROOT}"
	COMMAND ${CMAKE_COMMAND} -E echo "  kernel     cross-build kernel"
	COMMAND ${CMAKE_COMMAND} -E echo "  image      build bootable image / initrd"
	COMMAND ${CMAKE_COMMAND} -E echo "  rootfs     build ext4 fs.img from sysroot (boot disk)"
	COMMAND ${CMAKE_COMMAND} -E echo "  qemu       boot image in qemu (see scripts/run-qemu.sh)"
	COMMAND ${CMAKE_COMMAND} -E echo "  world      everything above")

message(STATUS "xv6-os configured for arch: ${XV6_ARCH}")
message(STATUS "  triple        : ${XV6_TRIPLE}")
message(STATUS "  sysroot       : ${XV6_SYSROOT}")
message(STATUS "  toolchain bin : ${XV6_TOOLCHAIN_BIN}")
message(STATUS "  cross cmake   : ${XV6_CROSS_TOOLCHAIN_FILE}")
