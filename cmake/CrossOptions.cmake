# CrossOptions.cmake
#
# Defines the per-arch directory layout and target triple. Single source
# of truth for paths consumed by every other cmake/Build*.cmake module.
#
# Inputs (cache):
#   XV6_ARCH        : riscv64 | x86_64
#
# Outputs (cache, parent scope):
#   XV6_TRIPLE              GNU triple, e.g. riscv64-xv6-linux-musl
#   XV6_BUILD_ROOT          ${CMAKE_BINARY_DIR}
#   XV6_SYSROOT             cross install prefix (musl, libgcc, ports, ...)
#   XV6_TOOLCHAIN_PREFIX    where binutils+gcc install (its --prefix)
#   XV6_TOOLCHAIN_BIN       ${XV6_TOOLCHAIN_PREFIX}/bin
#   XV6_STAMP_DIR           ExternalProject stamp directory
#   XV6_DOWNLOAD_DIR        toolchain tarball download cache (shared across arches)
#   XV6_CROSS_TOOLCHAIN_FILE  generated CMake toolchain file for cross builds
#   XV6_KERNEL_ARTIFACTS    where the kernel image lands

set(XV6_ARCH "riscv64" CACHE STRING "Target architecture: riscv64 | x86_64")
set_property(CACHE XV6_ARCH PROPERTY STRINGS riscv64 x86_64)

if(NOT XV6_ARCH MATCHES "^(riscv64|x86_64)$")
	message(FATAL_ERROR "XV6_ARCH must be 'riscv64' or 'x86_64', got '${XV6_ARCH}'")
endif()

# Vendor "xv6" so you can tell our cross-built binaries from a host
# riscv64-linux-musl or x86_64-linux-musl. Anything is fine here as
# long as it's consistent across binutils/gcc/musl --target.
if(XV6_ARCH STREQUAL "riscv64")
	set(XV6_TRIPLE "riscv64-xv6-linux-musl")
elseif(XV6_ARCH STREQUAL "x86_64")
	set(XV6_TRIPLE "x86_64-xv6-linux-musl")
endif()

set(XV6_BUILD_ROOT       "${CMAKE_BINARY_DIR}")
set(XV6_SYSROOT          "${XV6_BUILD_ROOT}/sysroot")
set(XV6_TOOLCHAIN_PREFIX "${XV6_BUILD_ROOT}/toolchain")
# The build_gcc_toolchain.sh script lays out binaries as
# ${PREFIX}/${arch}/phase{1,2}/bin. Phase 2 is the full C/C++ compiler;
# Phase 1 (static bootstrap) is enough for kernel/user (xv6-native).
# We point XV6_TOOLCHAIN_BIN at phase2 so ports get the dynamic-capable
# compiler; kernel/user only need phase1 but pickng phase2 is fine too.
set(XV6_TOOLCHAIN_BIN        "${XV6_TOOLCHAIN_PREFIX}/${XV6_ARCH}/phase2/bin")
set(XV6_TOOLCHAIN_BIN_PHASE1 "${XV6_TOOLCHAIN_PREFIX}/${XV6_ARCH}/phase1/bin")
set(XV6_STAMP_DIR        "${XV6_BUILD_ROOT}/stamps")
set(XV6_KERNEL_ARTIFACTS "${XV6_BUILD_ROOT}/kernel")

# Tarball cache is intentionally OUTSIDE the per-arch build dir so that
# building riscv64 and x86_64 in parallel doesn't redownload sources.
# Override on command line for CI etc.
set(XV6_DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/downloads"
	CACHE PATH "Shared toolchain tarball download cache")

# Path to the generated cross-CMake toolchain file. It doesn't exist
# until the toolchain is built; user/ports ExternalProjects depend on
# the toolchain stamp so they never see a stale file.
set(XV6_CROSS_TOOLCHAIN_FILE "${XV6_BUILD_ROOT}/cmake/toolchain-cross.cmake")

# Convenience: number of parallel jobs for nested make/ninja invocations.
include(ProcessorCount)
ProcessorCount(_jobs)
if(_jobs EQUAL 0)
	set(_jobs 1)
endif()
set(XV6_PARALLEL_JOBS "${_jobs}" CACHE STRING "Parallelism for nested builds")

file(MAKE_DIRECTORY
	"${XV6_SYSROOT}"
	"${XV6_TOOLCHAIN_PREFIX}"
	"${XV6_STAMP_DIR}"
	"${XV6_KERNEL_ARTIFACTS}"
	"${XV6_DOWNLOAD_DIR}"
	"${XV6_BUILD_ROOT}/cmake")
