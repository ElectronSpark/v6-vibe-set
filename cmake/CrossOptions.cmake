# CrossOptions.cmake
#
# Defines the x86_64 host-glibc build layout. The umbrella no longer builds or
# consumes a repo-local gcc/binutils/libc toolchain.

set(XV6_ARCH "x86_64" CACHE STRING "Target architecture: x86_64")
set_property(CACHE XV6_ARCH PROPERTY STRINGS x86_64)

if(NOT XV6_ARCH STREQUAL "x86_64")
	message(FATAL_ERROR "Only XV6_ARCH=x86_64 is supported by the host-glibc build")
endif()

set(XV6_BUILD_ROOT       "${CMAKE_BINARY_DIR}")
set(XV6_SYSROOT          "${XV6_BUILD_ROOT}/sysroot")
set(XV6_STAMP_DIR        "${XV6_BUILD_ROOT}/stamps")
set(XV6_KERNEL_ARTIFACTS "${XV6_BUILD_ROOT}/kernel")

# Convenience: number of parallel jobs for nested make/ninja invocations.
include(ProcessorCount)
ProcessorCount(_jobs)
if(_jobs EQUAL 0)
	set(_jobs 1)
endif()
set(XV6_PARALLEL_JOBS "${_jobs}" CACHE STRING "Parallelism for nested builds")

file(MAKE_DIRECTORY
	"${XV6_SYSROOT}"
	"${XV6_STAMP_DIR}"
	"${XV6_KERNEL_ARTIFACTS}")
