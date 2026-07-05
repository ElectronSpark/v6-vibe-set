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
set(_generated_rootfs_overlay_root "${XV6_BUILD_ROOT}/rootfs-generated-overlays")
set(_host_gui_overlay "${_generated_rootfs_overlay_root}/host-gui")
set(_webkit_media_overlay "${_generated_rootfs_overlay_root}/webkit-media")
set(_kde_runtime_overlay "${_generated_rootfs_overlay_root}/kde-runtime")
set(_gameboy_rom_overlay "${_generated_rootfs_overlay_root}/gameboy-roms")
set(_host_gui_runtime_stamp "${_host_gui_overlay}/.stamp")
set(_webkit_media_stamp "${_webkit_media_overlay}/.stamp")
set(_kde_runtime_stamp "${_kde_runtime_overlay}/.stamp")
set(_gameboy_rom_stamp "${_gameboy_rom_overlay}/.stamp")
set(_gpup_umd_overlay "${XV6_BUILD_ROOT}/gpup-umd-overlay")
set(_gpup_umd_overlay_stamp "${_gpup_umd_overlay}/.stamp")
set(_rootfs_extra_overlays "${_host_gui_overlay}:${_webkit_media_overlay}:${_kde_runtime_overlay}:${_gameboy_rom_overlay}:${_gpup_umd_overlay}")
file(GLOB_RECURSE _rootfs_overlay_sources CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/rootfs-overlay/*")
set(_rootfs_overlay_regular_sources)
foreach(_rootfs_overlay_source IN LISTS _rootfs_overlay_sources)
	if(NOT IS_SYMLINK "${_rootfs_overlay_source}")
		list(APPEND _rootfs_overlay_regular_sources "${_rootfs_overlay_source}")
	endif()
endforeach()
set(_xwayland_stage_script "${CMAKE_SOURCE_DIR}/scripts/image/stage-xwayland-runtime.sh")
set(_xwayland_kde_wrapper_source "${CMAKE_SOURCE_DIR}/scripts/image/xwayland-kde-wrapper.c")
set(_host_gui_runtime_sources
	${CMAKE_SOURCE_DIR}/scripts/image/chromium-egl-trace-preload.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-gtk-smoke.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-gtk-smoke-launcher.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-egl-gbm-gl-smoke.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-egl-gbm-gl-smoke-launcher.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-idle-x11-launcher.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-wlegl-smoke.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-wlegl-smoke-launcher.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-x11-abi-smoke.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-x11-abi-smoke-launcher.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-x11-dri3-present-smoke.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-x11-dri3-present-smoke-launcher.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-x11-egl-smoke.c
	${CMAKE_SOURCE_DIR}/scripts/image/host-x11-egl-smoke-launcher.c
		${CMAKE_SOURCE_DIR}/scripts/image/host-x11-present-trace-preload.c
		${CMAKE_SOURCE_DIR}/scripts/image/host-x11-shm-smoke.c
		${CMAKE_SOURCE_DIR}/scripts/image/host-x11-shm-smoke-launcher.c
		${CMAKE_SOURCE_DIR}/scripts/image/import-host-gui.sh
	${CMAKE_SOURCE_DIR}/scripts/image/stage-host-gui-runtime.sh
	${CMAKE_SOURCE_DIR}/scripts/image/wayland-chromium-launcher.c)
set(_rootfs_image_sources
			${CMAKE_SOURCE_DIR}/scripts/image/make-rootfs.sh
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-ifunc-memcpy-shim.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-qtqml-ifunc-startup-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kwin-alloc-trace-preload.c
			${CMAKE_SOURCE_DIR}/scripts/image/wayland-chromium-launcher.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-login1-shim.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-bluez-shim.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-modemmanager-shim.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-networkmanager-shim.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-network-status-sni.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-document-portal-shim.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-desktop-optional-services-shim.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-desktop-session.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-false.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-dmesg.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-ls.c
			${CMAKE_SOURCE_DIR}/scripts/image/proc-cmdline-rewrite-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/xv6-xdg-settings.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-session.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-plasma-session-child.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-abi-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-dlopen-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-app-launch-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-smoke-agent.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-konsole-shell-wrapper.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-terminal-launcher.c
			${CMAKE_SOURCE_DIR}/scripts/image/qt-wayland-smoke-launcher.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-config-atomic-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-pulse-cookie-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-proc-comm-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-proc-mountinfo-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-process-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-pgroup-kill-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-thread-group-stop-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-pty-shell-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-pty-openpty-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-pty-readiness-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-kwriteconfig-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-trash-stat-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-unix-socket-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/poll-notify-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-libinput-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-drm-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-wayland-seat-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kde-kwin-screenshot-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/icu-elf-tail-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kf5coreaddons-elf-tail-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/chrome-rela-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/chrome-zygote-fd3-probe.c
			${CMAKE_SOURCE_DIR}/scripts/image/kwin-global-slot-probe.c)
set(_rootfs_deps user ports host-gui-runtime webkit-media kde-runtime gameboy-roms gpup-umd-overlay
			${_rootfs_image_sources}
			${_rootfs_overlay_regular_sources}
			${_xwayland_stage_script}
			${_xwayland_kde_wrapper_source})
set(_rootfs_refresh_deps host-gui-runtime webkit-media kde-runtime gameboy-roms gpup-umd-overlay
			${_rootfs_image_sources}
			${_rootfs_overlay_regular_sources}
			${_xwayland_stage_script}
			${_xwayland_kde_wrapper_source})
set(_xwayland_stage_command
	${CMAKE_COMMAND} -E env
		${_xwayland_stage_script}
			${XV6_SYSROOT} ${XV6_SYSROOT}/bin/Xwayland)
set(_rootfs_command
	${CMAKE_COMMAND} -E env
		ROOTFS_EXTRA_OVERLAYS=${_rootfs_extra_overlays}
		${CMAKE_SOURCE_DIR}/scripts/image/make-rootfs.sh
			${XV6_SYSROOT} ${_fsimg} ${_fsimg_size_mb})

add_custom_command(
	OUTPUT ${_host_gui_runtime_stamp}
	COMMAND ${CMAKE_COMMAND} -E rm -rf ${_host_gui_overlay}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${_host_gui_overlay}
	COMMAND ${CMAKE_COMMAND} -E env
		HOST_GUI_BUILD_DIR=${XV6_BUILD_ROOT}/host-gui-runtime
		${CMAKE_SOURCE_DIR}/scripts/image/stage-host-gui-runtime.sh
			${_host_gui_overlay}
			${XV6_SYSROOT}
	COMMAND ${CMAKE_COMMAND} -E touch ${_host_gui_runtime_stamp}
	DEPENDS ${_host_gui_runtime_sources}
	COMMENT "Staging generated host GUI runtime overlay"
	VERBATIM)

add_custom_target(host-gui-runtime DEPENDS ${_host_gui_runtime_stamp})

add_custom_command(
	OUTPUT ${_webkit_media_stamp}
	COMMAND ${CMAKE_COMMAND} -E rm -rf ${_webkit_media_overlay}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${_webkit_media_overlay}
	COMMAND ${CMAKE_COMMAND} -E env
		WEBKIT_MEDIA_BUILD_DIR=${XV6_BUILD_ROOT}/webkit-media
		${CMAKE_SOURCE_DIR}/scripts/image/stage-webkit-media.sh
			${_webkit_media_overlay}
	COMMAND ${CMAKE_COMMAND} -E touch ${_webkit_media_stamp}
	DEPENDS ${CMAKE_SOURCE_DIR}/scripts/image/stage-webkit-media.sh
	COMMENT "Staging generated WebKit media overlay"
	VERBATIM)

add_custom_target(webkit-media DEPENDS ${_webkit_media_stamp})

add_custom_command(
	OUTPUT ${_kde_runtime_stamp}
	COMMAND ${CMAKE_COMMAND} -E rm -rf ${_kde_runtime_overlay}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${_kde_runtime_overlay}
	COMMAND ${CMAKE_COMMAND} -E env
		KDE_ARCHIVES_DIR=${XV6_BUILD_ROOT}/kde-noble-plasma/archives
		${CMAKE_SOURCE_DIR}/scripts/image/stage-kde-runtime.sh
			${_kde_runtime_overlay}
			${XV6_BUILD_ROOT}/kde-noble-plasma
	COMMAND ${CMAKE_COMMAND} -E touch ${_kde_runtime_stamp}
	DEPENDS ${CMAKE_SOURCE_DIR}/scripts/image/stage-kde-runtime.sh
	COMMENT "Staging generated KDE/Qt runtime overlay"
	VERBATIM)

add_custom_target(kde-runtime DEPENDS ${_kde_runtime_stamp})

add_custom_command(
	OUTPUT ${_gameboy_rom_stamp}
	COMMAND ${CMAKE_COMMAND} -E rm -rf ${_gameboy_rom_overlay}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${_gameboy_rom_overlay}
	COMMAND ${CMAKE_COMMAND} -E env
		GAMEBOY_ROM_BUILD_DIR=${XV6_BUILD_ROOT}/gameboy-roms
		${CMAKE_SOURCE_DIR}/scripts/image/stage-gameboy-roms.sh
			${_gameboy_rom_overlay}
	COMMAND ${CMAKE_COMMAND} -E touch ${_gameboy_rom_stamp}
	DEPENDS ${CMAKE_SOURCE_DIR}/scripts/image/stage-gameboy-roms.sh
	COMMENT "Staging generated/downloaded Game Boy ROM overlay"
	VERBATIM)

add_custom_target(gameboy-roms DEPENDS ${_gameboy_rom_stamp})

add_custom_command(
	OUTPUT ${_gpup_umd_overlay_stamp}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${_gpup_umd_overlay}
	COMMAND ${CMAKE_COMMAND} -E touch ${_gpup_umd_overlay_stamp}
	COMMENT "Preparing optional GPU-PV runtime overlay"
	VERBATIM)

add_custom_target(gpup-umd-overlay DEPENDS ${_gpup_umd_overlay_stamp})

# ---------------------------------------------------------------------
# Primary path: ext4 rootfs built from the populated sysroot.
# This is what scripts/launch/run-qemu.sh actually boots, and what the
# session demo (Python + Flask) depends on.
# ---------------------------------------------------------------------
add_custom_target(rootfs
	COMMAND ${_xwayland_stage_command}
	COMMAND ${_rootfs_command}
	DEPENDS ${_rootfs_deps}
	BYPRODUCTS ${_fsimg}
	COMMENT "Building ext4 rootfs ${_fsimg} (${_fsimg_size_mb} MiB) from ${XV6_SYSROOT}")

# Fast diagnostic path: rebuild fs.img from the already-staged sysroot and
# overlay without making CMake walk user or ports first. Use this when a GUI
# ABI loop only needs a fresh rootfs copy, not rebuilt payloads.
add_custom_target(rootfs-refresh
	COMMAND ${_xwayland_stage_command}
	COMMAND ${_rootfs_command}
	DEPENDS ${_rootfs_refresh_deps}
	COMMENT "Refreshing ext4 rootfs ${_fsimg} from existing ${XV6_SYSROOT}")

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
