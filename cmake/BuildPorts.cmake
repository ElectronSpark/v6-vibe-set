# BuildPorts.cmake — drive the ports/ sub-repo's standalone CMake build.
#
# ports/ is a single CMake project that auto-discovers each port and delegates
# to its standalone CMakeLists.txt. Ports now build with the host compiler and
# host glibc into XV6_SYSROOT.

include(ExternalProject)

set(_ports_src "${CMAKE_SOURCE_DIR}/ports")
set(_ports_obj "${XV6_BUILD_ROOT}/ports")
file(MAKE_DIRECTORY "${_ports_obj}")

if(DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
	set(_webkit_ref_sysroot_default "$ENV{XV6_WEBKIT_REF_SYSROOT}")
else()
	set(_webkit_ref_sysroot_default "")
endif()
set(XV6_WEBKIT_REF_SYSROOT
	"${_webkit_ref_sysroot_default}"
	CACHE PATH "Optional host-glibc WebKitGTK runtime sysroot to stage")
set(XV6_WEBKIT_STRICT_STAGE OFF
	CACHE BOOL "Fail port-webkit when the selected runtime sysroot is incomplete")
if(DEFINED ENV{XV6_WEBKIT_REF_SYSROOT}
   AND NOT XV6_WEBKIT_REF_SYSROOT STREQUAL _webkit_ref_sysroot_default)
	set(XV6_WEBKIT_REF_SYSROOT "${_webkit_ref_sysroot_default}" CACHE PATH
		"Optional host-glibc WebKitGTK runtime sysroot to stage" FORCE)
endif()

find_program(_host_cc NAMES cc gcc clang REQUIRED)
find_program(_host_cxx NAMES c++ g++ clang++ REQUIRED)
find_program(_host_ar NAMES ar REQUIRED)
find_program(_host_ranlib NAMES ranlib REQUIRED)

set(_port_cflags
	"-O2 -fPIC"
	" -DHAVE_CTIME_R=1"
	" -isystem ${XV6_SYSROOT}/include")
string(REPLACE ";" "" _port_cflags "${_port_cflags}")

set(_ports_configure_env ${CMAKE_COMMAND} -E env)
if(DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
	list(APPEND _ports_configure_env
		"XV6_WEBKIT_REF_SYSROOT=$ENV{XV6_WEBKIT_REF_SYSROOT}")
endif()

ExternalProject_Add(ports
	PREFIX            ${XV6_BUILD_ROOT}/ports-driver
	STAMP_DIR         ${XV6_STAMP_DIR}/ports
	DEPENDS           user
	SOURCE_DIR        ${_ports_src}
	BINARY_DIR        ${_ports_obj}
	DOWNLOAD_COMMAND  ""
	CONFIGURE_COMMAND ${_ports_configure_env}
	                    ${CMAKE_COMMAND}
	                    -S ${_ports_src}
	                    -B ${_ports_obj}
	                    -DCMAKE_C_COMPILER=${_host_cc}
	                    -DCMAKE_CXX_COMPILER=${_host_cxx}
	                    -DCMAKE_AR=${_host_ar}
	                    -DCMAKE_RANLIB=${_host_ranlib}
	                    -DXV6_SYSROOT=${XV6_SYSROOT}
	                    -DXV6_PORT_CFLAGS=${_port_cflags}
	                    -DXV6_PORT_CROSS=OFF
	                    -DXV6_WEBKIT_REF_SYSROOT=${XV6_WEBKIT_REF_SYSROOT}
	                    -DXV6_WEBKIT_STRICT_STAGE=${XV6_WEBKIT_STRICT_STAGE}
	BUILD_COMMAND     ${CMAKE_COMMAND} --build ${_ports_obj} -j${XV6_PARALLEL_JOBS}
	INSTALL_COMMAND   ""
	BUILD_ALWAYS      1)
