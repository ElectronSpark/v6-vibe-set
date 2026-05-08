# BuildUser.cmake — drive the user/ sub-repo's standalone CMake build.
#
# The user/ sub-repo handles its own ARCH dispatch + toolchain detection
# (mirroring the kernel sub-repo). We inject TOOLPREFIX via env so its
# auto-discovery picks the cross gcc we built, and pass CMAKE_INSTALL_PREFIX
# pointing at the umbrella-managed sysroot — every program installs as
# bin/_<name> there, ready to be packed into fs.img.
#
# Naming translation: umbrella XV6_ARCH=riscv64 -> sub-repo ARCH=riscv.
#
# user/ depends on the cross gcc but NOT on musl (xv6-native programs
# are -nostdlib + userlib + custom syscall stubs).  Linux ABI probes are
# built separately with the host compiler and host libc, then staged into
# the same sysroot for VM tests.

include(ExternalProject)

if(XV6_ARCH STREQUAL "riscv64")
	set(_user_arch riscv)
else()
	set(_user_arch ${XV6_ARCH})
endif()

set(_user_src "${CMAKE_SOURCE_DIR}/user")
set(_user_obj "${XV6_BUILD_ROOT}/user")
file(MAKE_DIRECTORY "${_user_obj}")

if(_user_arch STREQUAL "x86_64")
	find_program(_linux_host_cc NAMES cc gcc clang REQUIRED)
	set(_user_install_command
		${CMAKE_COMMAND} --install ${_user_obj}
		COMMAND ${CMAKE_COMMAND} -E env HOST_CC=${_linux_host_cc}
			${CMAKE_SOURCE_DIR}/scripts/build-linux-host-probes.sh ${XV6_SYSROOT})
else()
	set(_user_install_command
		${CMAKE_COMMAND} --install ${_user_obj})
endif()

ExternalProject_Add(user
	PREFIX            ${XV6_BUILD_ROOT}/user-driver
	STAMP_DIR         ${XV6_STAMP_DIR}/user
	DEPENDS           toolchain
	SOURCE_DIR        ${_user_src}
	BINARY_DIR        ${_user_obj}
	DOWNLOAD_COMMAND  ""
	CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
	                    PATH=${XV6_TOOLCHAIN_BIN}:$ENV{PATH}
	                    TOOLPREFIX=${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-
	                    ARCH=${_user_arch}
	                  ${CMAKE_COMMAND}
	                    -S ${_user_src}
	                    -B ${_user_obj}
	                    -DARCH=${_user_arch}
	                    -DCMAKE_INSTALL_PREFIX=${XV6_SYSROOT}
	                    -DOPT_LEVEL=2
	CMAKE_CACHE_ARGS  -DCMAKE_INSTALL_PREFIX:PATH=${XV6_SYSROOT}
	BUILD_COMMAND     ${CMAKE_COMMAND} --build ${_user_obj} -j${XV6_PARALLEL_JOBS}
	INSTALL_COMMAND   ${_user_install_command}
	BUILD_ALWAYS      1)
