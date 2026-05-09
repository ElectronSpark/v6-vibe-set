# BuildUser.cmake — build and stage user programs.
#
# x86_64 userland is Linux ABI only: host compiler, host glibc, no repo-local
# libc/toolchain runtime.

include(ExternalProject)

set(_user_arch ${XV6_ARCH})

set(_user_src "${CMAKE_SOURCE_DIR}/user")
set(_user_obj "${XV6_BUILD_ROOT}/user")
file(MAKE_DIRECTORY "${_user_obj}")

find_program(_linux_host_cc NAMES cc gcc clang REQUIRED)
add_custom_target(user
	COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/bin
	COMMAND ${CMAKE_COMMAND} -E env HOST_CC=${_linux_host_cc}
		${CMAKE_SOURCE_DIR}/scripts/build-linux-host-probes.sh ${XV6_SYSROOT}
	DEPENDS ${CMAKE_SOURCE_DIR}/scripts/build-linux-host-probes.sh
	        ${CMAKE_SOURCE_DIR}/scripts/build-linux-host-libs.sh
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	COMMENT "Building x86_64 user programs with host compiler/glibc"
	VERBATIM)
