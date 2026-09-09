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
get_filename_component(XV6_BUILD_ROOT "${XV6_BUILD_ROOT}" ABSOLUTE)

set(_xv6_default_sysroot "${XV6_BUILD_ROOT}/sysroot")
get_filename_component(_xv6_default_sysroot "${_xv6_default_sysroot}" ABSOLUTE)
if(DEFINED CACHE{XV6_SYSROOT} AND NOT "$CACHE{XV6_SYSROOT}" STREQUAL "")
	get_filename_component(_xv6_requested_sysroot "$CACHE{XV6_SYSROOT}" ABSOLUTE)
	if(NOT _xv6_requested_sysroot STREQUAL _xv6_default_sysroot)
		message(FATAL_ERROR
			"XV6_SYSROOT is build-local and must stay under the active "
			"build directory. Requested '${_xv6_requested_sysroot}', "
			"expected '${_xv6_default_sysroot}'. Use a separate CMake "
			"build directory instead of overriding XV6_SYSROOT.")
	endif()
endif()
set(XV6_SYSROOT "${_xv6_default_sysroot}" CACHE PATH
	"Build-local sysroot populated by the umbrella build" FORCE)
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
