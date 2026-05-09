# BuildKernel.cmake — drive the kernel sub-repo's CMake build.
#
# The kernel sub-repo is itself a standalone CMake project. The umbrella now
# drives only the x86_64 build with the host compiler.

include(ExternalProject)

set(_kernel_arch ${XV6_ARCH})

set(_kernel_src "${CMAKE_SOURCE_DIR}/kernel")
set(_kernel_obj "${XV6_KERNEL_ARTIFACTS}/build")
file(MAKE_DIRECTORY "${_kernel_obj}")

find_program(_kernel_host_cc NAMES x86_64-linux-gnu-gcc gcc cc REQUIRED)
set(_kernel_configure_command
	${CMAKE_COMMAND} -E env
		ARCH=${_kernel_arch}
	${CMAKE_COMMAND}
		-S ${_kernel_src}
		-B ${_kernel_obj}
		-DARCH=${_kernel_arch}
		-DPLATFORM=qemu
		-DOPT_LEVEL=2
		-DCMAKE_C_COMPILER=${_kernel_host_cc}
		-DCMAKE_ASM_COMPILER=${_kernel_host_cc})

ExternalProject_Add(kernel
	PREFIX            ${XV6_BUILD_ROOT}/kernel-driver
	STAMP_DIR         ${XV6_STAMP_DIR}/kernel
	DEPENDS
	SOURCE_DIR        ${_kernel_src}
	BINARY_DIR        ${_kernel_obj}
	DOWNLOAD_COMMAND  ""
	# Re-implement configure so we can inject TOOLPREFIX via env (the
	# kernel's auto-discovery runs before project()).
	CONFIGURE_COMMAND ${_kernel_configure_command}
	BUILD_COMMAND     ${CMAKE_COMMAND} --build ${_kernel_obj} -j${XV6_PARALLEL_JOBS} --target kernel_all
	INSTALL_COMMAND   ${CMAKE_COMMAND} -E rm -rf ${XV6_KERNEL_ARTIFACTS}/kernel.elf
	          COMMAND ${CMAKE_COMMAND} -E copy
	                    ${_kernel_obj}/kernel/kernel_with_symbols_elf
	                    ${XV6_KERNEL_ARTIFACTS}/kernel.elf
	BUILD_ALWAYS      1
	BUILD_BYPRODUCTS  ${XV6_KERNEL_ARTIFACTS}/kernel.elf)

add_custom_target(kernel-sparse
	COMMAND ${CMAKE_COMMAND} --build ${_kernel_obj} -j${XV6_PARALLEL_JOBS} --target kernel-sparse
	DEPENDS kernel
	COMMENT "Run sparse over the configured kernel compile database"
	VERBATIM)
