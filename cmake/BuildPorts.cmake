# BuildPorts.cmake — drive the ports/ sub-repo's standalone CMake build.
#
# ports/ is a single CMake project that auto-discovers each port and
# delegates to its standalone CMakeLists.txt. We pass the cross
# toolchain explicitly (no toolchain file — each port handles its own
# build via xv6_port() wrapping the upstream native build).
#
# Ports install into XV6_SYSROOT (= MUSL_SYSROOT). They depend on
# musl headers/libs being there first, hence the dep on `user` (which
# in xv6-tmp builds musl as part of the user/ tree). For now, depend
# on tc-musl which the umbrella's BuildToolchain.cmake produces.

include(ExternalProject)

set(_ports_src "${CMAKE_SOURCE_DIR}/ports")
set(_ports_obj "${XV6_BUILD_ROOT}/ports")
file(MAKE_DIRECTORY "${_ports_obj}")

# Common port CFLAGS — ports build full userspace against the cross
# toolchain's built-in musl sysroot, plus -isystem into XV6_SYSROOT
# so inter-port headers (e.g. zlib.h for libpng) resolve.
if(XV6_ARCH STREQUAL "riscv64")
	set(_port_arch_cflags "-march=rv64gc -mabi=lp64d -mcmodel=medany")
else()
	set(_port_arch_cflags "")
endif()
set(_port_cflags
	"-O2 -fPIC"
	" ${_port_arch_cflags}"
	" -isystem ${XV6_SYSROOT}/include")
string(REPLACE ";" "" _port_cflags "${_port_cflags}")

ExternalProject_Add(ports
	PREFIX            ${XV6_BUILD_ROOT}/ports-driver
	STAMP_DIR         ${XV6_STAMP_DIR}/ports
	DEPENDS           toolchain
	SOURCE_DIR        ${_ports_src}
	BINARY_DIR        ${_ports_obj}
	DOWNLOAD_COMMAND  ""
	CONFIGURE_COMMAND ${CMAKE_COMMAND}
	                    -S ${_ports_src}
	                    -B ${_ports_obj}
	                    -DCMAKE_C_COMPILER=${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-gcc
	                    -DCMAKE_AR=${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-ar
	                    -DCMAKE_RANLIB=${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-ranlib
	                    -DXV6_SYSROOT=${XV6_SYSROOT}
	                    -DXV6_PORT_CFLAGS=${_port_cflags}
	                    -DPHASE2_LIB=${XV6_TOOLCHAIN_PREFIX}/${XV6_ARCH}/phase2/${XV6_TRIPLE}/lib
	                    -DXV6_TOOLCHAIN_PREFIX=${XV6_TOOLCHAIN_PREFIX}
	                    -DXV6_TRIPLE=${XV6_TRIPLE}
	BUILD_COMMAND     ${CMAKE_COMMAND} --build ${_ports_obj} -j${XV6_PARALLEL_JOBS}
	INSTALL_COMMAND   ""
	BUILD_ALWAYS      1)
