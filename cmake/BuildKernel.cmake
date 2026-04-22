# BuildKernel.cmake — drive the kernel sub-repo's CMake build.
#
# The kernel sub-repo is itself a standalone CMake project (it owns
# toolchain detection, ARCH/PLATFORM/OPT/LAB cache vars, and the global
# CFLAGS used by both kernel and bootloader). We invoke its cmake from
# the umbrella, passing TOOLPREFIX via env so its detection picks our
# cross compiler (built by the toolchain target).
#
# ARCH naming difference: the umbrella uses XV6_ARCH=riscv64 / x86_64;
# the kernel sub-repo uses ARCH=riscv (xv6 historical). Translate here.
#
# The kernel build is independent of the sysroot — it's freestanding —
# so we depend only on tc-gcc-stage1 (no need to wait for musl).

include(ExternalProject)

if(XV6_ARCH STREQUAL "riscv64")
	set(_kernel_arch riscv)
else()
	set(_kernel_arch ${XV6_ARCH})
endif()

set(_kernel_src "${CMAKE_SOURCE_DIR}/kernel")
set(_kernel_obj "${XV6_KERNEL_ARTIFACTS}/build")
file(MAKE_DIRECTORY "${_kernel_obj}")

ExternalProject_Add(kernel
	PREFIX            ${XV6_BUILD_ROOT}/kernel-driver
	STAMP_DIR         ${XV6_STAMP_DIR}/kernel
	DEPENDS           tc-gcc-stage1
	SOURCE_DIR        ${_kernel_src}
	BINARY_DIR        ${_kernel_obj}
	DOWNLOAD_COMMAND  ""
	# Re-implement configure so we can inject TOOLPREFIX via env (the
	# kernel's auto-discovery runs before project()).
	CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
	                    PATH=${XV6_TOOLCHAIN_BIN}:$ENV{PATH}
	                    TOOLPREFIX=${XV6_TOOLCHAIN_BIN}/${XV6_TRIPLE}-
	                    ARCH=${_kernel_arch}
	                  ${CMAKE_COMMAND}
	                    -S ${_kernel_src}
	                    -B ${_kernel_obj}
	                    -DARCH=${_kernel_arch}
	                    -DPLATFORM=qemu
	                    -DOPT_LEVEL=2
	BUILD_COMMAND     ${CMAKE_COMMAND} --build ${_kernel_obj} -j${XV6_PARALLEL_JOBS} --target kernel
	INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy
	                    ${_kernel_obj}/kernel
	                    ${XV6_KERNEL_ARTIFACTS}/kernel.elf
	BUILD_ALWAYS      1
	BUILD_BYPRODUCTS  ${XV6_KERNEL_ARTIFACTS}/kernel.elf)
