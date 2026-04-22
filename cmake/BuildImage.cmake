# BuildImage.cmake — boot image / initrd / qemu targets.
#
# Consumes:
#   ${XV6_SYSROOT}        — populated by user + ports
#   ${XV6_KERNEL_ARTIFACTS}/kernel.elf
#
# Produces:
#   ${XV6_BUILD_ROOT}/initrd.cpio.gz
#   ${XV6_BUILD_ROOT}/boot.img        (raw disk image, optional)

set(_initrd "${XV6_BUILD_ROOT}/initrd.cpio.gz")
set(_image  "${XV6_BUILD_ROOT}/boot.img")

add_custom_command(
	OUTPUT  ${_initrd}
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/make-initrd.sh
	            ${XV6_SYSROOT} ${_initrd}
	DEPENDS user ports
	COMMENT "Building initrd from ${XV6_SYSROOT}")

add_custom_command(
	OUTPUT  ${_image}
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/make-image.sh
	            ${XV6_KERNEL_ARTIFACTS}/kernel.elf
	            ${_initrd}
	            ${_image}
	DEPENDS kernel ${_initrd}
	COMMENT "Building boot image ${_image}")

add_custom_target(initrd DEPENDS ${_initrd})
add_custom_target(image  DEPENDS ${_image})

add_custom_target(qemu
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-qemu.sh
	            ${XV6_ARCH}
	            ${XV6_KERNEL_ARTIFACTS}/kernel.elf
	            ${_initrd}
	DEPENDS kernel ${_initrd}
	USES_TERMINAL
	COMMENT "Booting ${XV6_ARCH} kernel in qemu")
