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
# are -nostdlib + userlib + custom syscall stubs).

include(ExternalProject)

if(XV6_ARCH STREQUAL "riscv64")
	set(_user_arch riscv)
else()
	set(_user_arch ${XV6_ARCH})
endif()

set(_user_src "${CMAKE_SOURCE_DIR}/user")
set(_user_obj "${XV6_BUILD_ROOT}/user")
file(MAKE_DIRECTORY "${_user_obj}")

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
	BUILD_COMMAND     ${CMAKE_COMMAND} --build ${_user_obj} -j${XV6_PARALLEL_JOBS}
	INSTALL_COMMAND   ${CMAKE_COMMAND} --install ${_user_obj}
	BUILD_ALWAYS      1)
