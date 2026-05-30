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
set(_x86_linux_img "${XV6_KERNEL_ARTIFACTS}/build/kernel/xv6.bin")
if(XV6_ARCH STREQUAL "x86_64")
	set(_qemu_kernel "${_x86_linux_img}")
else()
	set(_qemu_kernel "${XV6_KERNEL_ARTIFACTS}/kernel.elf")
endif()
set(_hyperv_vhdx "${XV6_BUILD_ROOT}/xv6-hyperv.vhdx")
set(_fsimg_size_mb "auto" CACHE STRING "Size of fs.img in MiB, or auto to size from the staged sysroot")
set(_hyperv_img_size_mb "0" CACHE STRING
	"Size of Hyper-V VHDX in MiB; 0 chooses a size from fs.img")
set(_hyperv_cmdline "BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=0 video=1024x640"
	CACHE STRING "Kernel command line embedded in the Hyper-V EFI loader")
file(GLOB_RECURSE _rootfs_overlay_files CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/rootfs-overlay/*")

set(_rootfs_deps user ports ${_rootfs_overlay_files} ${CMAKE_SOURCE_DIR}/scripts/image/make-rootfs.sh)
set(_rootfs_command
	${CMAKE_SOURCE_DIR}/scripts/image/make-rootfs.sh
		${XV6_SYSROOT} ${_fsimg} ${_fsimg_size_mb})

# ---------------------------------------------------------------------
# Primary path: ext4 rootfs built from the populated sysroot.
# This is what scripts/launch/run-qemu.sh actually boots, and what the
# session demo (Python + Flask) depends on.
# ---------------------------------------------------------------------
add_custom_target(rootfs
	COMMAND ${_rootfs_command}
	DEPENDS ${_rootfs_deps}
	BYPRODUCTS ${_fsimg}
	COMMENT "Building ext4 rootfs ${_fsimg} (${_fsimg_size_mb} MiB) from ${XV6_SYSROOT}")

# ---------------------------------------------------------------------
# Legacy initrd / boot-image path (unused by current run-qemu.sh, but
# kept until scripts/image/make-initrd.sh and make-image.sh are removed).
# ---------------------------------------------------------------------
add_custom_command(
	OUTPUT  ${_initrd}
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/image/make-initrd.sh
	            ${XV6_SYSROOT} ${_initrd}
	DEPENDS user ports
	COMMENT "Building initrd from ${XV6_SYSROOT}")

add_custom_command(
	OUTPUT  ${_image}
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/image/make-image.sh
	            ${XV6_KERNEL_ARTIFACTS}/kernel.elf
	            ${_initrd}
	            ${_image}
	DEPENDS kernel ${_initrd}
	COMMENT "Building boot image ${_image}")

add_custom_target(initrd DEPENDS ${_initrd})
add_custom_target(image DEPENDS ${_image} rootfs)

add_custom_target(hyperv-image
	COMMAND ${CMAKE_COMMAND} -E env
	            HYPERV_CMDLINE=${_hyperv_cmdline}
	            ${CMAKE_SOURCE_DIR}/scripts/image/make-hyperv-image.sh
	            ${_x86_linux_img}
	            ${_fsimg}
	            ${_hyperv_vhdx}
	            ${_hyperv_img_size_mb}
	DEPENDS kernel rootfs
	BYPRODUCTS ${_hyperv_vhdx}
	COMMENT "Building Hyper-V Gen2 bootable VHDX ${_hyperv_vhdx}")

# ---------------------------------------------------------------------
# qemu boot — uses fs.img (the rootfs target).
# ---------------------------------------------------------------------
add_custom_target(qemu
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/launch/run-qemu.sh
	            ${XV6_ARCH}
	            ${_qemu_kernel}
	            ${_fsimg}
	DEPENDS kernel rootfs
	USES_TERMINAL
	COMMENT "Booting ${XV6_ARCH} kernel in qemu")

add_custom_target(webkit-runtime-check
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/gpu/validate-webkit-runtime.sh
	            ${XV6_SYSROOT} ${_fsimg}
	DEPENDS rootfs
	COMMENT "Validating staged WebKitGTK runtime in sysroot and fs.img")
