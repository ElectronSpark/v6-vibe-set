# BuildToolchain.cmake — drive toolchain/scripts/build_gcc_toolchain.sh.
#
# The upstream xv6 toolchain build is a single monolithic bash script
# (toolchain/scripts/build_gcc_toolchain.sh) that handles fetching
# tarballs, building binutils + gcc-stage1, building musl, and
# rebuilding gcc-stage2 — all in two well-defined "phases":
#
#   Phase 1: static-only toolchain (binutils + gcc-stage1 + musl).
#            Output: ${XV6_TOOLCHAIN_PREFIX}/${arch}/phase1/bin/${triple}-*
#   Phase 2: dynamic-capable toolchain (rebuild gcc + musl with shared).
#            Output: ${XV6_TOOLCHAIN_PREFIX}/${arch}/phase2/bin/${triple}-*
#
# We expose three ExternalProject targets so dependent sub-repos can
# depend on the *minimum* stage they need:
#
#   tc-gcc-stage1  → kernel + user (xv6-native -nostdlib programs)
#   tc-musl        → ports that link against musl (libpng etc) — same as Phase 1
#   tc-gcc-stage2  → ports needing C++ / shared libs (CPython, gtk, ...)
#
# Phase 1 produces all three (binutils + static gcc + static musl), so
# tc-gcc-stage1 and tc-musl share a build invocation. Phase 2 is a
# separate pass.

include(ExternalProject)

set(_tc_src "${CMAKE_SOURCE_DIR}/toolchain")
set(_tc_log "${XV6_BUILD_ROOT}/toolchain-build")
file(MAKE_DIRECTORY "${_tc_log}")

if(XV6_PREBUILT_TOOLCHAIN_PREFIX)
	foreach(_tc_required
		"${XV6_TOOLCHAIN_BIN_PHASE1}/${XV6_TRIPLE}-gcc"
		"${XV6_TOOLCHAIN_BIN_PHASE1}/${XV6_TRIPLE}-ld"
		"${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-gcc"
		"${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-g++")
		if(NOT EXISTS "${_tc_required}")
			message(FATAL_ERROR
				"Prebuilt toolchain is missing ${_tc_required}. "
				"Expected layout: ${XV6_TOOLCHAIN_PREFIX}/${XV6_ARCH}/phase{1,2}/bin/${XV6_TRIPLE}-*")
		endif()
	endforeach()

	configure_file(
		${CMAKE_SOURCE_DIR}/cmake/Toolchain.cmake.in
		${XV6_CROSS_TOOLCHAIN_FILE}
		@ONLY)

	add_custom_target(tc-phase1
		COMMAND ${CMAKE_COMMAND} -E echo "Using prebuilt phase-1 toolchain at ${XV6_TOOLCHAIN_BIN_PHASE1}"
		VERBATIM)
	add_custom_target(tc-binutils   DEPENDS tc-phase1)
	add_custom_target(tc-gcc-stage1 DEPENDS tc-phase1)
	add_custom_target(tc-musl       DEPENDS tc-phase1)

	add_custom_target(tc-phase2
		COMMAND ${CMAKE_COMMAND} -E echo "Using prebuilt phase-2 toolchain at ${XV6_TOOLCHAIN_BIN}"
		DEPENDS tc-phase1
		VERBATIM)
	add_custom_target(tc-gcc-stage2 DEPENDS tc-phase2)

	add_custom_target(tc-emit-toolchain-file
		COMMAND ${CMAKE_COMMAND} -E touch ${XV6_CROSS_TOOLCHAIN_FILE}
		BYPRODUCTS ${XV6_CROSS_TOOLCHAIN_FILE}
		COMMENT    "Cross CMake toolchain file at ${XV6_CROSS_TOOLCHAIN_FILE}")

	add_custom_target(toolchain DEPENDS tc-phase2 tc-emit-toolchain-file)
	return()
endif()

set(_tc_script "${_tc_src}/scripts/build_gcc_toolchain.sh")
if(NOT EXISTS "${_tc_script}")
	message(FATAL_ERROR
		"Toolchain build script not found at ${_tc_script}. "
		"Did you forget to populate the toolchain sub-repo?")
endif()

# Map XV6_ARCH (riscv64 | x86_64) → script's --arch=
set(_tc_arch ${XV6_ARCH})

# ---------------------------------------------------------------------
# Phase 1: binutils + gcc (static) + musl.
# ---------------------------------------------------------------------
ExternalProject_Add(tc-phase1
	PREFIX            ${_tc_log}/phase1
	STAMP_DIR         ${XV6_STAMP_DIR}/tc-phase1
	DOWNLOAD_COMMAND  ""
	CONFIGURE_COMMAND ""
	BUILD_COMMAND     bash ${_tc_script}
	                    --arch=${_tc_arch}
	                    --prefix=${XV6_TOOLCHAIN_PREFIX}
	                    --jobs=${XV6_PARALLEL_JOBS}
	                    --phase=1
	BUILD_IN_SOURCE   1
	INSTALL_COMMAND   ""
	BUILD_BYPRODUCTS  ${XV6_TOOLCHAIN_BIN_PHASE1}/${XV6_TRIPLE}-gcc
	                  ${XV6_TOOLCHAIN_BIN_PHASE1}/${XV6_TRIPLE}-ld)

# Aliases so dependent sub-repos can opt into a meaningful name. Both
# Phase 1 stages happen in one script run.
add_custom_target(tc-binutils   DEPENDS tc-phase1)
add_custom_target(tc-gcc-stage1 DEPENDS tc-phase1)
add_custom_target(tc-musl       DEPENDS tc-phase1)

# ---------------------------------------------------------------------
# Phase 2: rebuild gcc + musl with shared-library support.
# ---------------------------------------------------------------------
ExternalProject_Add(tc-phase2
	PREFIX            ${_tc_log}/phase2
	STAMP_DIR         ${XV6_STAMP_DIR}/tc-phase2
	DEPENDS           tc-phase1
	DOWNLOAD_COMMAND  ""
	CONFIGURE_COMMAND ""
	BUILD_COMMAND     bash ${_tc_script}
	                    --arch=${_tc_arch}
	                    --prefix=${XV6_TOOLCHAIN_PREFIX}
	                    --jobs=${XV6_PARALLEL_JOBS}
	                    --phase=2
	BUILD_IN_SOURCE   1
	INSTALL_COMMAND   ""
	BUILD_BYPRODUCTS  ${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-gcc
	                  ${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-g++)

add_custom_target(tc-gcc-stage2 DEPENDS tc-phase2)

# ---------------------------------------------------------------------
# Emit cross CMake toolchain file (used by ports/ for hard-pinned
# CMAKE_TOOLCHAIN_FILE in case we ever need it; kernel/user/ports
# currently set CMAKE_C_COMPILER directly so this is informational).
# ---------------------------------------------------------------------
configure_file(
	${CMAKE_SOURCE_DIR}/cmake/Toolchain.cmake.in
	${XV6_CROSS_TOOLCHAIN_FILE}
	@ONLY)

add_custom_target(tc-emit-toolchain-file
	DEPENDS tc-phase2
	COMMAND ${CMAKE_COMMAND} -E touch ${XV6_CROSS_TOOLCHAIN_FILE}
	BYPRODUCTS ${XV6_CROSS_TOOLCHAIN_FILE}
	COMMENT    "Cross CMake toolchain file at ${XV6_CROSS_TOOLCHAIN_FILE}")

# Public aggregate target.
add_custom_target(toolchain DEPENDS tc-phase2 tc-emit-toolchain-file)
