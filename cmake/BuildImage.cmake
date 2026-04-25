# BuildImage.cmake — boot image / rootfs / qemu targets.
#
# Consumes:
#   ${XV6_SYSROOT}        — populated by user + ports
#   ${XV6_KERNEL_ARTIFACTS}/build/kernel/kernel
#
# Produces:
#   ${XV6_BUILD_ROOT}/fs.img          — primary ext4 rootfs (boots in qemu)
#   ${XV6_BUILD_ROOT}/initrd.cpio.gz  — legacy initrd (kept for now)
#   ${XV6_BUILD_ROOT}/boot.img        — legacy raw disk image

set(_initrd "${XV6_BUILD_ROOT}/initrd.cpio.gz")
set(_image  "${XV6_BUILD_ROOT}/boot.img")
set(_fsimg  "${XV6_BUILD_ROOT}/fs.img")
set(_fsimg_size_mb "1536" CACHE STRING "Size of fs.img in MiB")

# ---------------------------------------------------------------------
# Primary path: ext4 rootfs built from the populated sysroot.
# This is what scripts/run-qemu.sh actually boots, and what the
# session demo (Python + Flask) depends on.
# ---------------------------------------------------------------------
add_custom_command(
	OUTPUT  ${_fsimg}
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/make-rootfs.sh
	            ${XV6_SYSROOT} ${_fsimg} ${_fsimg_size_mb}
	            ${XV6_TOOLCHAIN_PREFIX}/${XV6_ARCH}/phase2/${XV6_TRIPLE}/lib
	DEPENDS user ports
	COMMENT "Building ext4 rootfs ${_fsimg} (${_fsimg_size_mb} MiB) from ${XV6_SYSROOT}")

add_custom_target(rootfs DEPENDS ${_fsimg})

# ---------------------------------------------------------------------
# Legacy initrd / boot-image path (unused by current run-qemu.sh, but
# kept until scripts/make-initrd.sh and make-image.sh are removed).
# ---------------------------------------------------------------------
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

# ---------------------------------------------------------------------
# qemu boot — uses fs.img (the rootfs target).
# ---------------------------------------------------------------------
add_custom_target(qemu
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-qemu.sh
	            ${XV6_ARCH}
	            ${XV6_KERNEL_ARTIFACTS}/build/kernel/kernel
	            ${_fsimg}
	DEPENDS kernel rootfs
	USES_TERMINAL
	COMMENT "Booting ${XV6_ARCH} kernel in qemu")
