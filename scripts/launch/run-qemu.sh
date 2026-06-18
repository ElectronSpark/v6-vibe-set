#!/usr/bin/env bash
# run-qemu.sh <arch> <kernel-image> <fs.img>
#
# Boots the kernel + xv6fs disk image in qemu-system-<arch>.
# Set DISPLAY_MODE=gtk|sdl|nographic (default gtk on x86_64, nographic on riscv64).
# Debugging:
#   QEMU_GDB=1              Enable QEMU's GDB stub on tcp::1234.
#   QEMU_GDB_PORT=2159      Use a different GDB stub port.
#   QEMU_GDB_WAIT=1         Start paused at reset until GDB continues.
#   QEMU_GPU=auto           GPU model: auto, bochs, virtio-gpu,
#                           virtio-gpu-primary, virtio-gpu-gl,
#                           virtio-gpu-gl-primary, virtio-vga-gl-primary,
#                           or none.
#   QEMU_VIRTIO_GPU_BLOB=auto
#                           Enable virtio-gpu blob resources with hostmem when
#                           /dev/udmabuf is available on this launcher path.
#   QEMU_VIRTIO_GPU_HOSTMEM=1G
#                           Host-visible memory size for blob resources.
#   QEMU_VIRTIO_GPU_MAX_HOSTMEM=1G
#                           Maximum host-visible blob memory aperture.
#   QEMU_REQUIRE_HOST_DRI=0 Fail instead of warning when an accelerated GTK
#                           GPU launch cannot see host /dev/dri.
#   QEMU_REQUIRE_UDMABUF=0  Fail instead of warning when blob resources are
#                           requested but /dev/udmabuf is unavailable.
#   QEMU_REQUIRE_KVM=auto   Require KVM for WebKit-accelerated GUI launches.
#                           Set to 0 for deliberate slow-path debugging.
#   QEMU_CPU=auto           Use host CPU features under KVM and qemu64 under TCG.
#   QEMU_HOST_GL=auto       Host OpenGL provider. auto selects WSL D3D12 when
#                           /dev/dxg and Mesa d3d12 are available. Set
#                           default to leave Mesa selection alone.
#   QEMU_WSL_D3D12_ADAPTER=auto
#                           Preferred WSL D3D12 adapter for host GL. auto
#                           selects NVIDIA when nvidia-smi is available;
#                           set Intel, NVIDIA, or empty/default to override.
#   QEMU_WSL_GL_DISPLAY=gtk QEMU display backend to use for WSL D3D12 GL.
#                           The default uses GTK with the virgl adapter as the
#                           visible primary display.
#   QEMU_ALLOW_WSL_SDL_GL=0 SDL GL presents a black QEMU window on WSLg/D3D12
#                           on tested hosts, so virgl launches are switched
#                           back to GTK unless this is set to 1.
#   QEMU_WSL_SDL_VIDEODRIVER=wayland
#                           SDL backend to use on WSL when SDL is selected.
#   QEMU_VMMOUSE=1          Enable VMware absolute pointer. The default input
#                           path is the virtio tablet, which avoids host GTK
#                           pointer-grab scaling ambiguity.
#   QEMU_INPUT=auto         Input device path: auto, virtio, virtio-mouse,
#                           vmmouse, ps2, or none. auto uses virtio-tablet for
#                           GTK and vmmouse for SDL because QEMU's SDL frontend
#                           can fail to deliver absolute tablet motion on WSLg
#                           while relative devices can be host-edge clamped.
#   QEMU_GTK_GDK_SCALE=1    Force QEMU's GTK window to a 1:1 host scale.
#   QEMU_GTK_GL=auto        GTK OpenGL mode for QEMU. auto uses GLES on WSL
#                           virgl because gtk,gl=on can stop at GtkGLArea
#                           DMABUF setup before the xv6 desktop appears.
#   QEMU_GTK_GRAB_ON_HOVER=on
#                           Grab pointer/keyboard as the cursor enters GTK.
#   QEMU_GTK_CURSOR_MODE=host
#                           GTK cursor policy: host keeps QEMU GTK's host
#                           pointer visible and suppresses guest cursor-image
#                           uploads to avoid WSLg black cursor squares; guest
#                           restores guest hardware cursor images and shape
#                           changes for focused cursor debugging.
#   QEMU_GTK_SHOW_CURSOR=on
#                           Keep the host cursor visible in GTK. Default is
#                           on for host cursor mode and off for guest mode.
#   QEMU_SDL_GRAB_MOD=lctrl-lalt
#                           SDL mouse/keyboard ungrab modifier.
#   QEMU_SDL_SHOW_CURSOR=on
#                           Keep the host cursor visible. WSLg's SDL/X11 grab
#                           path can stop delivering focused pointer motion when
#                           the cursor is hidden/captured.
#   QEMU_DRY_RUN=1          Print the resolved qemu command and exit.
#   QEMU_EXTRA='...'        Still accepted for extra raw QEMU args.
set -euo pipefail

if [[ $# -ne 3 ]]; then
        echo "usage: $0 <arch> <kernel-image> <fs.img>" >&2
        exit 1
fi
ARCH="$1"; KERNEL="$2"; FSIMG="$3"

QEMU_EXTRA="${QEMU_EXTRA:-}"
QEMU_CPUS="${QEMU_CPUS:-6}"
QEMU_MEMORY="${QEMU_MEMORY:-4G}"
QEMU_CPU="${QEMU_CPU:-auto}"
QEMU_APPEND="${QEMU_APPEND:-root=/dev/disk0}"
QEMU_VMMOUSE="${QEMU_VMMOUSE:-0}"
QEMU_INPUT="${QEMU_INPUT:-auto}"
if [[ -z "${QEMU_MACHINE:-}" ]]; then
        QEMU_MACHINE_AUTO=1
        if [[ "${QEMU_VMMOUSE}" == "1" ]]; then
                QEMU_MACHINE="pc,vmport=on"
        else
                QEMU_MACHINE="pc,vmport=off"
        fi
else
        QEMU_MACHINE_AUTO=0
fi
QEMU_NET="${QEMU_NET:-1}"
QEMU_NETSURF="${QEMU_NETSURF:-auto}"
QEMU_GPU="${QEMU_GPU:-auto}"
QEMU_GDB="${QEMU_GDB:-0}"
QEMU_GDB_PORT="${QEMU_GDB_PORT:-1234}"
QEMU_GDB_WAIT="${QEMU_GDB_WAIT:-0}"
QEMU_GDB_ARGS=()
QEMU_VIRTIO_GPU_XRES="${QEMU_VIRTIO_GPU_XRES:-1280}"
QEMU_VIRTIO_GPU_YRES="${QEMU_VIRTIO_GPU_YRES:-800}"
QEMU_VIRTIO_GPU_BLOB="${QEMU_VIRTIO_GPU_BLOB:-auto}"
QEMU_VIRTIO_GPU_HOSTMEM="${QEMU_VIRTIO_GPU_HOSTMEM:-1G}"
QEMU_VIRTIO_GPU_MAX_HOSTMEM="${QEMU_VIRTIO_GPU_MAX_HOSTMEM:-${QEMU_VIRTIO_GPU_HOSTMEM}}"
QEMU_REQUIRE_HOST_DRI="${QEMU_REQUIRE_HOST_DRI:-0}"
QEMU_REQUIRE_UDMABUF="${QEMU_REQUIRE_UDMABUF:-0}"
QEMU_REQUIRE_KVM="${QEMU_REQUIRE_KVM:-auto}"
QEMU_HOST_GL="${QEMU_HOST_GL:-auto}"
QEMU_WSL_D3D12_ADAPTER="${QEMU_WSL_D3D12_ADAPTER:-auto}"
QEMU_WSL_GL_DISPLAY="${QEMU_WSL_GL_DISPLAY:-gtk}"
QEMU_ALLOW_WSL_SDL_GL="${QEMU_ALLOW_WSL_SDL_GL:-0}"
QEMU_WSL_SDL_VIDEODRIVER="${QEMU_WSL_SDL_VIDEODRIVER:-x11}"
QEMU_GTK_FULLSCREEN="${QEMU_GTK_FULLSCREEN:-off}"
QEMU_GTK_ZOOM_TO_FIT="${QEMU_GTK_ZOOM_TO_FIT:-off}"
QEMU_GTK_GRAB_ON_HOVER="${QEMU_GTK_GRAB_ON_HOVER:-on}"
QEMU_GTK_CURSOR_MODE="${QEMU_GTK_CURSOR_MODE:-host}"
case "${QEMU_GTK_CURSOR_MODE}" in
        host)
                QEMU_GTK_SHOW_CURSOR="${QEMU_GTK_SHOW_CURSOR:-on}"
                ;;
        guest)
                QEMU_GTK_SHOW_CURSOR="${QEMU_GTK_SHOW_CURSOR:-off}"
                ;;
        *)
                echo "unsupported QEMU_GTK_CURSOR_MODE: ${QEMU_GTK_CURSOR_MODE}" >&2
                exit 2
                ;;
esac
QEMU_GTK_SHOW_MENUBAR="${QEMU_GTK_SHOW_MENUBAR:-off}"
QEMU_GTK_SHOW_TABS="${QEMU_GTK_SHOW_TABS:-off}"
QEMU_GTK_GL="${QEMU_GTK_GL:-auto}"
QEMU_GTK_GDK_SCALE="${QEMU_GTK_GDK_SCALE:-1}"
QEMU_GTK_GDK_DPI_SCALE="${QEMU_GTK_GDK_DPI_SCALE:-1}"
QEMU_SDL_GRAB_MOD="${QEMU_SDL_GRAB_MOD:-lctrl-lalt}"
QEMU_SDL_SHOW_CURSOR="${QEMU_SDL_SHOW_CURSOR:-on}"

if [[ "${ARCH}" == "x86_64" && " ${QEMU_APPEND} " != *" video="* ]]; then
        QEMU_APPEND="${QEMU_APPEND} video=${QEMU_VIRTIO_GPU_XRES}x${QEMU_VIRTIO_GPU_YRES}"
fi

qemu_append_has_enabled_flag() {
        local key="$1"
        [[ " ${QEMU_APPEND} " == *" ${key}=1 "* ]]
}

qemu_append_has_key() {
        local key="$1"
        [[ " ${QEMU_APPEND} " == *" ${key}="* ]]
}

qemu_append_default_flag() {
        local key="$1"
        local value="$2"

        if ! qemu_append_has_key "${key}"; then
                QEMU_APPEND="${QEMU_APPEND} ${key}=${value}"
        fi
}

qemu_prepend_default_flag() {
        local key="$1"
        local value="$2"

        if ! qemu_append_has_key "${key}"; then
                QEMU_APPEND="${key}=${value} ${QEMU_APPEND}"
        fi
}

print_kvm_hint() {
        echo "run-qemu: smooth WebKit video needs KVM; the current launch would fall back to slow TCG." >&2
        echo "run-qemu: make /dev/kvm readable/writable by this user, then restart the shell/WSL session." >&2
        echo "run-qemu: common fix: sudo usermod -aG kvm ${USER}; in WSL, also check /dev/kvm group/udev permissions." >&2
        echo "run-qemu: set QEMU_REQUIRE_KVM=0 only for deliberate non-accelerated debugging." >&2
}

requires_kvm_for_this_launch() {
        if [[ "${QEMU_REQUIRE_KVM}" == "1" ]]; then
                return 0
        fi
        if [[ "${QEMU_REQUIRE_KVM}" != "auto" ]]; then
                return 1
        fi
        [[ "${ARCH}" == "x86_64" && "${DISPLAY_MODE:-gtk}" != "nographic" ]] || return 1
        qemu_append_has_enabled_flag webkit_accel
}

if [[ "${QEMU_GDB}" == "1" ]]; then
        QEMU_GDB_ARGS=(-gdb "tcp::${QEMU_GDB_PORT}")
        if [[ "${QEMU_GDB_WAIT}" == "1" ]]; then
                QEMU_GDB_ARGS+=(-S)
        fi
        echo "run-qemu: GDB stub listening on tcp::${QEMU_GDB_PORT}" >&2
        if [[ "${QEMU_GDB_WAIT}" == "1" ]]; then
                echo "run-qemu: VM is paused at reset; continue from GDB with: c" >&2
        fi
        if [[ "${ARCH}" == "x86_64" ]]; then
                echo "run-qemu: attach with:" >&2
                echo "  gdb ${KERNEL} -ex 'target remote :${QEMU_GDB_PORT}'" >&2
                echo "run-qemu: after a freeze, press Ctrl-C in GDB, then run: thread apply all bt" >&2
        fi
fi

# ──────────────────────────────────────────────────────────────────────
# KVM enablement.  Prefer hardware acceleration on x86_64 when the host
# exposes /dev/kvm; set USE_KVM=0 to force TCG for deterministic debugging.
# ──────────────────────────────────────────────────────────────────────
if [[ -z "${USE_KVM:-}" ]]; then
        if [[ "${ARCH}" == "x86_64" && -e /dev/kvm ]]; then
                USE_KVM=1
        else
                USE_KVM=0
        fi
fi
KVM_ARGS=()
if [[ "${USE_KVM}" == "1" && -e /dev/kvm ]]; then
        if [[ ! -r /dev/kvm || ! -w /dev/kvm ]]; then
                echo "run-qemu: /dev/kvm exists but is not accessible to ${USER}." >&2
                if [[ -t 0 ]]; then
                        echo "run-qemu: requesting sudo to chmod a+rw /dev/kvm ..." >&2
                        if sudo chmod a+rw /dev/kvm; then
                                echo "run-qemu: /dev/kvm is now accessible." >&2
                        else
                                echo "run-qemu: sudo failed; falling back to TCG unless this launch requires KVM." >&2
                        fi
                else
                        echo "run-qemu: noninteractive shell; not prompting for sudo." >&2
                fi
        fi
        if [[ -r /dev/kvm && -w /dev/kvm ]]; then
                KVM_ARGS=(-enable-kvm)
                echo "run-qemu: using KVM acceleration" >&2
        fi
fi
if [[ ${#KVM_ARGS[@]} -eq 0 ]] && requires_kvm_for_this_launch; then
        print_kvm_hint
        exit 2
fi
if [[ "${QEMU_CPU}" == "auto" ]]; then
        if [[ "${ARCH}" == "x86_64" && ${#KVM_ARGS[@]} -gt 0 ]]; then
                QEMU_CPU="host"
        else
                QEMU_CPU="qemu64"
        fi
fi

if [[ "${QEMU_NETSURF}" == "0" || ("${QEMU_NETSURF}" == "auto" && ${#KVM_ARGS[@]} -gt 0) ]]; then
        if [[ " ${QEMU_APPEND} " != *" netsurf="* ]]; then
                QEMU_APPEND="${QEMU_APPEND} netsurf=0"
        fi
fi

host_dri_available() {
        local node

        for node in /dev/dri/renderD*; do
                [[ -e "${node}" ]] || continue
                if [[ -r "${node}" && -w "${node}" ]] &&
                   ! host_dri_node_is_known_software "${node}"; then
                        return 0
                fi
        done
        return 1
}

host_dri_node_is_known_software() {
        local node="$1"
        local name
        local sysdev

        name="$(basename -- "${node}")"
        sysdev="$(readlink -f "/sys/class/drm/${name}/device" 2>/dev/null || true)"
        case "${sysdev}" in
                */vgem|*/vgem/*)
                        return 0
                        ;;
        esac
        if [[ -r "/sys/class/drm/${name}/device/uevent" ]] &&
           grep -Eq '(^|=)(vgem|VKMS|vkms)($|=)' "/sys/class/drm/${name}/device/uevent" 2>/dev/null; then
                return 0
        fi
        return 1
}

host_dri_exists() {
        local node

        for node in /dev/dri/renderD* /dev/dri/card*; do
                [[ -e "${node}" ]] && return 0
        done
        return 1
}

host_dri_has_known_software() {
        local node

        for node in /dev/dri/renderD* /dev/dri/card*; do
                [[ -e "${node}" ]] || continue
                if host_dri_node_is_known_software "${node}"; then
                        return 0
                fi
        done
        return 1
}

host_dri_has_render_node() {
        local node

        for node in /dev/dri/renderD*; do
                [[ -e "${node}" ]] && return 0
        done
        return 1
}

host_is_wsl() {
        grep -qi microsoft /proc/version 2>/dev/null
}

host_wsl_d3d12_available() {
        host_is_wsl || return 1
        [[ -e /dev/dxg ]] || return 1
        [[ -e /usr/lib/x86_64-linux-gnu/dri/d3d12_dri.so ]] || return 1
        [[ -e /usr/lib/wsl/lib/libd3d12.so ]] || return 1
        [[ -e /usr/lib/wsl/lib/libdxcore.so ]] || return 1
        return 0
}

host_wsl_has_nvidia_adapter() {
        [[ -x /usr/lib/wsl/lib/nvidia-smi ]] || return 1
        /usr/lib/wsl/lib/nvidia-smi >/dev/null 2>&1
}

print_host_gpu_hint() {
        echo "run-qemu: expose host GPU acceleration before expecting smooth WebKit video:" >&2
        echo "run-qemu:   bare host: ensure a hardware /dev/dri/renderD* is readable/writable" >&2
        echo "run-qemu:   WSL2: ensure /dev/dxg exists; QEMU_HOST_GL=auto will use Mesa D3D12 with GTK" >&2
        echo "run-qemu:   docker: add --device /dev/dri and, for blobs, --device /dev/udmabuf" >&2
        echo "run-qemu:   optional host setup for blobs: sudo modprobe udmabuf" >&2
}

case "${ARCH}" in
        riscv64)
                DISPLAY_MODE="${DISPLAY_MODE:-nographic}"
                # Use mon:stdio so QEMU intercepts Ctrl-A X to quit (and
                # passes Ctrl-C through to the guest instead of killing qemu).
                if [[ "${DISPLAY_MODE}" == "nographic" ]]; then
                        DISPLAY_ARGS=(-nographic -serial mon:stdio)
                else
                        DISPLAY_ARGS=(-display "${DISPLAY_MODE}" -serial mon:stdio)
                fi
                exec qemu-system-riscv64 \
                        -machine virt -cpu rv64 -smp 2 -m 256M \
                        "${DISPLAY_ARGS[@]}" \
                        -bios default \
                        -kernel "${KERNEL}" \
                        -global virtio-mmio.force-legacy=false \
                        -drive file="${FSIMG}",if=none,format=raw,id=x0 \
                        -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
                        "${QEMU_GDB_ARGS[@]}" \
                        ${QEMU_EXTRA}
                ;;
        x86_64)
                DISPLAY_MODE="${DISPLAY_MODE:-gtk}"
                QEMU_ENV_ARGS=()
                HOST_GL_MODE="${QEMU_HOST_GL}"
                if [[ "${HOST_GL_MODE}" == "auto" ]]; then
                        if host_wsl_d3d12_available; then
                                HOST_GL_MODE="wsl-d3d12"
                        else
                                HOST_GL_MODE="default"
                        fi
                fi
                case "${HOST_GL_MODE}" in
                        default)
                                ;;
                        wsl-d3d12)
                                if ! host_wsl_d3d12_available; then
                                        echo "run-qemu: QEMU_HOST_GL=wsl-d3d12 requested, but /dev/dxg or Mesa d3d12 is missing" >&2
                                        exit 2
                                fi
                                WSL_D3D12_ADAPTER="${QEMU_WSL_D3D12_ADAPTER}"
                                if [[ "${WSL_D3D12_ADAPTER}" == "auto" ]]; then
                                        if host_wsl_has_nvidia_adapter; then
                                                WSL_D3D12_ADAPTER="NVIDIA"
                                        else
                                                WSL_D3D12_ADAPTER=""
                                        fi
                                fi
                                QEMU_ENV_ARGS+=(
                                        MESA_LOADER_DRIVER_OVERRIDE=d3d12
                                        GALLIUM_DRIVER=d3d12
                                        LIBGL_ALWAYS_SOFTWARE=0
                                )
                                if [[ -n "${WSL_D3D12_ADAPTER}" &&
                                      "${WSL_D3D12_ADAPTER}" != "default" ]]; then
                                        QEMU_ENV_ARGS+=(
                                                "MESA_D3D12_DEFAULT_ADAPTER_NAME=${WSL_D3D12_ADAPTER}"
                                        )
                                fi
                                ;;
                        *)
                                echo "unsupported QEMU_HOST_GL: ${QEMU_HOST_GL}" >&2
                                exit 2
                                ;;
                esac
                if [[ "${QEMU_GPU}" == "auto" ]]; then
                        # Prefer accelerated virtio-gpu for GTK GUI launches
                        # whenever the host can provide a GL backend.  On
                        # WSLg/D3D12, QEMU's direct GTK/GL primary scanout can
                        # disappear into a checker/gradient host surface, so
                        # keep Bochs visible and use virtio-gpu-gl as the
                        # render node unless a caller explicitly asks for the
                        # direct primary path.
                        if [[ "${DISPLAY_MODE}" == "gtk" &&
                              "${HOST_GL_MODE}" == "wsl-d3d12" ]]; then
                                QEMU_GPU="virtio-gpu-gl-primary"
                        elif [[ "${DISPLAY_MODE}" == "gtk" &&
                                host_dri_available ]]; then
                                QEMU_GPU="virtio-vga-gl-primary"
                        else
                                QEMU_GPU="bochs"
                        fi
                        echo "run-qemu: auto GPU selected ${QEMU_GPU}" >&2
                fi
                if [[ "${HOST_GL_MODE}" == "wsl-d3d12" && "${QEMU_GPU}" == *"-gl"* ]]; then
                        if [[ "${DISPLAY_MODE}" == "gtk" ]]; then
                                WSL_GL_DISPLAY="${QEMU_WSL_GL_DISPLAY}"
                                if [[ "${WSL_GL_DISPLAY}" == "sdl" &&
                                      "${QEMU_ALLOW_WSL_SDL_GL}" != "1" ]]; then
                                        echo "run-qemu: QEMU_WSL_GL_DISPLAY=sdl is disabled because SDL GL presents a black window on WSLg/D3D12; using gtk (set QEMU_ALLOW_WSL_SDL_GL=1 to force)" >&2
                                        WSL_GL_DISPLAY="gtk"
                                fi
                                if [[ "${DISPLAY_MODE}" != "${WSL_GL_DISPLAY}" ]]; then
                                        echo "run-qemu: WSL D3D12 host GL selected; switching display gtk -> ${WSL_GL_DISPLAY} for virgl" >&2
                                fi
                                DISPLAY_MODE="${WSL_GL_DISPLAY}"
                        elif [[ "${DISPLAY_MODE}" == "sdl" &&
                                "${QEMU_ALLOW_WSL_SDL_GL}" != "1" ]]; then
                                echo "run-qemu: SDL GL presents a black window on WSLg/D3D12; switching display sdl -> gtk (set QEMU_ALLOW_WSL_SDL_GL=1 to force)" >&2
                                DISPLAY_MODE="gtk"
                        fi
                fi
                if host_is_wsl && [[ "${DISPLAY_MODE}" == "sdl" ]]; then
                        QEMU_ENV_ARGS+=("SDL_VIDEODRIVER=${QEMU_WSL_SDL_VIDEODRIVER}")
                fi
                GTK_GL_MODE="${QEMU_GTK_GL}"
                if [[ "${GTK_GL_MODE}" == "auto" ]]; then
                        if host_is_wsl && [[ "${DISPLAY_MODE}" == "gtk" &&
                              "${QEMU_GPU}" == *"-gl"* ]]; then
                                GTK_GL_MODE="es"
                        else
                                GTK_GL_MODE="on"
                        fi
                fi
                if [[ "${DISPLAY_MODE}" == "gtk" && "${QEMU_GPU}" == *"-gl"* &&
                      "${HOST_GL_MODE}" != "wsl-d3d12" ]] &&
                   ! host_dri_available; then
                        if host_dri_has_known_software; then
                                echo "run-qemu: warning: host /dev/dri is software-only (for example vgem); GTK/virgl will use llvmpipe and may freeze under WebKit video" >&2
                        elif host_dri_has_render_node; then
                                echo "run-qemu: warning: host /dev/dri render nodes exist but are not readable/writable; GTK/virgl will use software GL (llvmpipe)" >&2
                        elif host_dri_exists; then
                                echo "run-qemu: warning: host /dev/dri nodes exist but no render node is usable; GTK/virgl will use software GL (llvmpipe)" >&2
                        else
                                echo "run-qemu: warning: no host /dev/dri node detected; GTK/virgl will use software GL (llvmpipe), so WebKit video may jitter" >&2
                        fi
                        print_host_gpu_hint
                        if [[ "${QEMU_REQUIRE_HOST_DRI}" == "1" ]]; then
                                exit 2
                        fi
                fi
                if [[ "${DISPLAY_MODE}" == "gtk" && "${QEMU_GPU}" == *"-gl"* &&
                      "${HOST_GL_MODE}" == "wsl-d3d12" ]]; then
                        echo "run-qemu: WSL D3D12 host GL selected for virgl${WSL_D3D12_ADAPTER:+ (${WSL_D3D12_ADAPTER})}; GTK may still print harmless DMABUF warnings" >&2
                        echo "run-qemu: guest boot log is mirrored to /tmp/xv6-debugcon.log" >&2
                fi
                if [[ "${QEMU_GPU}" == *"-gl"* ]]; then
                        qemu_prepend_default_flag virtio_gpu_3d_scanout 1
                        if [[ "${DISPLAY_MODE}" == "gtk" ]]; then
                                # QEMU GTK's guest cursor pixbuf path can
                                # render Chromium cursor images as a black
                                # square on WSLg.  Keep Weston off the
                                # software cursor path in both modes; host
                                # mode uses the frontend pointer, while guest
                                # mode restores hardware cursor images so
                                # surface-specific shapes can be debugged.
                                if [[ "${QEMU_GTK_CURSOR_MODE}" == "host" ]]; then
                                        qemu_prepend_default_flag virtio_gpu_host_cursor_only 1
                                fi
                                qemu_prepend_default_flag virtio_gpu_cursor_rgba_compat 1
                        fi
                        # Keep the VM desktop at the configured guest mode.
                        # The full-screen pageflip-copy path is useful for
                        # KMS experiments, but it can make the host window
                        # appear to jump between a client-sized surface and
                        # the desktop.  Prefer compositor-owned GPU composition
                        # into the desktop framebuffer by default.
                        qemu_prepend_default_flag virtio_gpu_disable_pageflip_copy 1
                        qemu_prepend_default_flag virtio_gpu_present_no_drain 1
                        # Pipeline the steady-state scanout RESOURCE_FLUSH
                        # instead of blocking the guest present loop on
                        # the host flush-ack (~15ms on the WSL D3D12 virgl
                        # host).  This keeps the displayed FPS in step with the
                        # application's render rate, matching the Alpine/Weston
                        # pipelined-present behaviour.
                        qemu_prepend_default_flag vgpu_async_flush 1
                        qemu_prepend_default_flag vgpu_async_pf 1
                        # Virgl/D3D12 can spend close to a minute compiling and
                        # validating early WebKit GL work.  Do not abort the
                        # guest GL contexts during that one-time warmup.
                        qemu_prepend_default_flag virtio_gpu_irq_wait_ms 60000
                fi
                # Use mon:stdio so QEMU intercepts Ctrl-A X to quit (and
                # passes Ctrl-C through to the guest instead of killing qemu).
                #
                # GTK input notes:
                #   - QEMU_INPUT=virtio   Use QEMU's absolute virtio tablet by
                #                         default so host window scaling does not
                #                         distort pointer-to-guest coordinates.
                #   - QEMU_VMMOUSE=1      VMware absolute pointer remains
                #                         available for explicit debugging.
                #   - grab-on-hover=on    Capture pointer + keyboard as soon
                #                         as the host cursor enters the canvas;
                #                         without this, GTK may keep motion
                #                         events on the host side.
                #   - QEMU_GTK_CURSOR_MODE=host
                #                         Keep the host pointer visible and
                #                         suppress guest cursor-image uploads.
                #   - QEMU_GTK_CURSOR_MODE=guest
                #                         Restore guest hardware cursor images
                #                         and shape changes for cursor debugging.
                # Press Ctrl-Alt-G to release the grab.
                if [[ "${DISPLAY_MODE}" == "nographic" ]]; then
                        DISPLAY_ARGS=(-nographic -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "gtk" && "${QEMU_GPU}" == *"-gl"* ]]; then
                        DISPLAY_ARGS=(-display "gtk,gl=${GTK_GL_MODE},grab-on-hover=${QEMU_GTK_GRAB_ON_HOVER},show-cursor=${QEMU_GTK_SHOW_CURSOR},full-screen=${QEMU_GTK_FULLSCREEN},zoom-to-fit=${QEMU_GTK_ZOOM_TO_FIT},show-menubar=${QEMU_GTK_SHOW_MENUBAR},show-tabs=${QEMU_GTK_SHOW_TABS}"
                                      -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "sdl" && "${QEMU_GPU}" == *"-gl"* ]]; then
                        DISPLAY_ARGS=(-display "sdl,gl=on,show-cursor=${QEMU_SDL_SHOW_CURSOR},grab-mod=${QEMU_SDL_GRAB_MOD}"
                                      -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "gtk" ]]; then
                        # Forward pointer motion as soon as the host cursor
                        # enters the GTK window.  Relying on click-to-grab can
                        # leave the guest cursor apparently frozen on Wayland.
                        DISPLAY_ARGS=(-display "gtk,grab-on-hover=${QEMU_GTK_GRAB_ON_HOVER},show-cursor=${QEMU_GTK_SHOW_CURSOR},full-screen=${QEMU_GTK_FULLSCREEN},zoom-to-fit=${QEMU_GTK_ZOOM_TO_FIT},show-menubar=${QEMU_GTK_SHOW_MENUBAR},show-tabs=${QEMU_GTK_SHOW_TABS}"
                                      -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "sdl" ]]; then
                        DISPLAY_ARGS=(-display "sdl,show-cursor=${QEMU_SDL_SHOW_CURSOR},grab-mod=${QEMU_SDL_GRAB_MOD}"
                                      -serial mon:stdio)
                else
                        DISPLAY_ARGS=(-display "${DISPLAY_MODE}" -serial mon:stdio)
                fi
                NET_ARGS=()
                if [[ "${QEMU_NET}" == "1" ]]; then
                        # Backend selection:
                        #   QEMU_NET_BACKEND=user (default) — user-mode SLIRP
                        #     NAT.  Caps at ~30-40 Mbit/s aggregate (single-
                        #     threaded host TCP stack).  Convenient: no host
                        #     setup, hostfwd works.
                        #   QEMU_NET_BACKEND=tap — bridge to a pre-existing
                        #     tap device (QEMU_NET_TAP_IFNAME, default
                        #     "tap0").  Bypasses SLIRP entirely; expect
                        #     1-10 Gbit/s.  Requires the host to have already
                        #     created and configured the tap device (see
                        #     scripts/setup-tap.sh) and the QEMU binary to
                        #     have permission to open it.
                        case "${QEMU_NET_BACKEND:-user}" in
                                user)
                                        # User-mode net w/ explicit hostfwd so a
                                        # guest server on 8080 is reachable from
                                        # the host on 18080. Override via
                                        # HOSTFWD env (full -netdev user
                                        # fragment).
                                        HOSTFWD="${HOSTFWD:-hostfwd=tcp::18080-:8080,hostfwd=tcp::15001-:5001}"
                                        NET_ARGS=(-netdev user,id=n0,${HOSTFWD}
                                                  -device "${QEMU_NET_MODEL:-e1000},netdev=n0")
                                        ;;
                                tap)
                                        QEMU_NET_TAP_IFNAME="${QEMU_NET_TAP_IFNAME:-tap0}"
                                        QEMU_NET_TAP_SCRIPT="${QEMU_NET_TAP_SCRIPT:-no}"
                                        QEMU_NET_TAP_DOWN="${QEMU_NET_TAP_DOWN:-no}"
                                        NET_ARGS=(-netdev "tap,id=n0,ifname=${QEMU_NET_TAP_IFNAME},script=${QEMU_NET_TAP_SCRIPT},downscript=${QEMU_NET_TAP_DOWN}"
                                                  -device "${QEMU_NET_MODEL:-e1000},netdev=n0")
                                        ;;
                                *)
                                        echo "unsupported QEMU_NET_BACKEND: ${QEMU_NET_BACKEND}" >&2
                                        exit 1
                                        ;;
                        esac
                else
                        NET_ARGS=(-net none)
                fi
                GPU_ARGS=()
                gpu_gl_opts="xres=${QEMU_VIRTIO_GPU_XRES},yres=${QEMU_VIRTIO_GPU_YRES}"
                # virgl ('-gl') GPU types enable 3D via virglrenderer.  QEMU's
                # classic virgl path rejects blob resources at device realize
                # ("blobs and virgl are not compatible (yet)"); only the
                # separate rutabaga/gfxstream backend supports virgl + blob.
                gpu_is_virgl=0
                if [[ "${QEMU_GPU}" == *gl* ]]; then
                        gpu_is_virgl=1
                fi
                if [[ "${QEMU_VIRTIO_GPU_BLOB}" == "auto" ]]; then
                        if [[ -r /dev/udmabuf && -w /dev/udmabuf ]]; then
                                QEMU_VIRTIO_GPU_BLOB=1
                        else
                                QEMU_VIRTIO_GPU_BLOB=0
                        fi
                fi
                # Blob is incompatible with the virgl device on this QEMU; auto
                # disable it for virgl GPUs (override with QEMU_VIRGL_BLOB_OK=1
                # on a QEMU/virglrenderer that supports the combination).
                if [[ "${QEMU_VIRTIO_GPU_BLOB}" == "1" && "${gpu_is_virgl}" == "1" \
                      && "${QEMU_VIRGL_BLOB_OK:-0}" != "1" ]]; then
                        if [[ "${QEMU_REQUIRE_UDMABUF}" == "1" ]]; then
                                echo "run-qemu: QEMU rejects virgl + blob (\"blobs and virgl are not compatible\"); cannot honor QEMU_REQUIRE_UDMABUF=1 with a virgl ('${QEMU_GPU}') GPU." >&2
                                echo "run-qemu: use a non-virgl QEMU_GPU for blob, a rutabaga-capable QEMU, or set QEMU_VIRGL_BLOB_OK=1 to override." >&2
                                exit 2
                        fi
                        echo "run-qemu: disabling virtio-gpu blob: this QEMU's virgl path is incompatible with blob resources (set QEMU_VIRGL_BLOB_OK=1 to override)." >&2
                        QEMU_VIRTIO_GPU_BLOB=0
                fi
                if [[ "${QEMU_VIRTIO_GPU_BLOB}" == "1" ]]; then
                        if [[ ! -r /dev/udmabuf || ! -w /dev/udmabuf ]]; then
                                echo "run-qemu: QEMU_VIRTIO_GPU_BLOB=1 needs /dev/udmabuf on this launcher path." >&2
                                print_host_gpu_hint
                                exit 2
                        fi
                        gpu_gl_opts+=",blob=true,hostmem=${QEMU_VIRTIO_GPU_HOSTMEM},max_hostmem=${QEMU_VIRTIO_GPU_MAX_HOSTMEM}"
                elif [[ "${QEMU_REQUIRE_UDMABUF}" == "1" ]]; then
                        echo "run-qemu: QEMU_REQUIRE_UDMABUF=1 but /dev/udmabuf is unavailable." >&2
                        print_host_gpu_hint
                        exit 2
                fi
                case "${QEMU_GPU}" in
                        bochs)
                                ;;
                        virtio-gpu)
                                GPU_ARGS=(-device "virtio-gpu-pci,id=xv6gpu0,${gpu_gl_opts}")
                                ;;
                        virtio-gpu-primary)
                                GPU_ARGS=(-vga none -device "virtio-gpu-pci,id=xv6gpu0,${gpu_gl_opts}")
                                ;;
                        virtio-gpu-gl)
                                GPU_ARGS=(-device "virtio-gpu-gl-pci,id=xv6gpu0,${gpu_gl_opts}")
                                ;;
                        virtio-gpu-gl-primary)
                                # Historical two-adapter setup: Bochs remains
                                # the visible console while virtio-gpu-gl
                                # supplies render nodes.  This is useful as a
                                # fallback if the primary virtio-vga path
                                # regresses, but it keeps display presentation
                                # on the slower BGA path.
                                GPU_ARGS=(-device "virtio-gpu-gl-pci,id=xv6gpu0,${gpu_gl_opts}")
                                ;;
                        virtio-vga-gl-primary)
                                # Experimental direct GL scanout path.  This
                                # removes the Bochs VGA fallback and asks QEMU
                                # to make the virgl device the visible primary
                                # adapter.  It can reduce host-side display
                                # indirection on native Linux, but xv6 needs
                                # virtio-gpu scanout/fb support for the
                                # desktop to start.
                                GPU_ARGS=(-vga none -device "virtio-vga-gl,id=xv6gpu0,${gpu_gl_opts}")
                                ;;
                        none)
                                GPU_ARGS=(-vga none)
                                ;;
                        *)
                                echo "unsupported QEMU_GPU: ${QEMU_GPU}" >&2
                                exit 2
                                ;;
                esac
                # QEMU's virtio_gpu_have_udmabuf() needs guest RAM backed by a
                # shared, sealable memfd (memory-backend-memfd) in addition to
                # /dev/udmabuf; plain anonymous -m RAM makes blob realize fail
                # with "need rutabaga or udmabuf for blob resources".  Flag the
                # memfd backend only when blob is actually attached to the GPU.
                QEMU_GPU_NEEDS_MEMFD=0
                if [[ " ${GPU_ARGS[*]} " == *"blob=true"* ]]; then
                        QEMU_GPU_NEEDS_MEMFD=1
                fi
                INPUT_ARGS=()
                if [[ "${QEMU_INPUT}" == "auto" ]]; then
                        if [[ "${DISPLAY_MODE}" == "nographic" ]]; then
                                QEMU_INPUT="none"
                        elif [[ "${DISPLAY_MODE}" == "sdl" ]]; then
                                QEMU_INPUT="vmmouse"
                                QEMU_VMMOUSE=1
                                if [[ "${QEMU_MACHINE_AUTO}" == "1" ]]; then
                                        QEMU_MACHINE="pc,vmport=on"
                                fi
                        else
                                QEMU_INPUT="virtio"
                        fi
                fi
                case "${QEMU_INPUT}" in
                        virtio)
                                INPUT_ARGS=(-device virtio-tablet-pci)
                                ;;
                        virtio-mouse)
                                INPUT_ARGS=(-device virtio-mouse-pci)
                                ;;
                        vmmouse)
                                if [[ "${QEMU_MACHINE_AUTO}" == "1" ]]; then
                                        QEMU_MACHINE="pc,vmport=on"
                                elif [[ "${QEMU_MACHINE}" != *"vmport=on"* ]]; then
                                        echo "run-qemu: QEMU_INPUT=vmmouse needs QEMU_MACHINE with vmport=on" >&2
                                        exit 2
                                fi
                                ;;
                        ps2|none)
                                ;;
                        *)
                                echo "unsupported QEMU_INPUT: ${QEMU_INPUT}" >&2
                                exit 2
                                ;;
                esac
                # KVM can expose the real host feature set; the x86 FPU path
                # enables OSXSAVE and saves XSAVE-managed state, so host SIMD
                # features are useful for WebKit/codec-heavy workloads.  TCG
                # keeps the conservative qemu64 fallback via QEMU_CPU=auto.
                CPU_ARGS=(-cpu "${QEMU_CPU}")
                # When virtio-gpu blob resources are enabled, back guest RAM
                # with a shared memfd so udmabuf can export guest pages to the
                # host GL.  The backend size must match -m.
                MEM_BACKEND_ARGS=()
                if [[ "${QEMU_GPU_NEEDS_MEMFD}" == "1" ]]; then
                        QEMU_MEM_BACKEND_ID="${QEMU_MEM_BACKEND_ID:-xv6mem0}"
                        MEM_BACKEND_ARGS=(-object \
                                "memory-backend-memfd,id=${QEMU_MEM_BACKEND_ID},size=${QEMU_MEMORY},share=on")
                        if [[ "${QEMU_MACHINE}" != *memory-backend=* ]]; then
                                QEMU_MACHINE="${QEMU_MACHINE},memory-backend=${QEMU_MEM_BACKEND_ID}"
                        fi
                fi
                QEMU_CMD=(qemu-system-x86_64
                        -machine "${QEMU_MACHINE}" -smp "${QEMU_CPUS}" -m "${QEMU_MEMORY}"
                        "${MEM_BACKEND_ARGS[@]}"
                        "${KVM_ARGS[@]}" "${CPU_ARGS[@]}"
                        "${DISPLAY_ARGS[@]}"
                        -debugcon file:/tmp/xv6-debugcon.log
                        -global isa-debugcon.iobase=0xe9
                        -kernel "${KERNEL}"
                        -drive file="${FSIMG}",if=none,format=raw,id=x0
                        -device virtio-blk-pci,drive=x0
                        "${GPU_ARGS[@]}"
                        "${INPUT_ARGS[@]}"
                        "${NET_ARGS[@]}"
                        -append "${QEMU_APPEND}"
                        "${QEMU_GDB_ARGS[@]}")
                if [[ -n "${QEMU_EXTRA}" ]]; then
                        read -r -a QEMU_EXTRA_ARGS <<< "${QEMU_EXTRA}"
                        QEMU_CMD+=("${QEMU_EXTRA_ARGS[@]}")
                fi
                if [[ "${QEMU_DRY_RUN:-0}" == "1" ]]; then
                        if [[ ${#QEMU_ENV_ARGS[@]} -gt 0 ]]; then
                                printf '%q ' "${QEMU_ENV_ARGS[@]}"
                        fi
                        if [[ "${DISPLAY_MODE}" == "gtk" ]]; then
                                printf 'GDK_SCALE=%q GDK_DPI_SCALE=%q ' \
                                        "${QEMU_GTK_GDK_SCALE}" \
                                        "${QEMU_GTK_GDK_DPI_SCALE}"
                        fi
                        printf '%s\n' "${QEMU_CMD[*]}"
                        exit 0
                fi
                if [[ "${DISPLAY_MODE}" == "gtk" ]]; then
                        exec env \
                                "${QEMU_ENV_ARGS[@]}" \
                                GDK_SCALE="${QEMU_GTK_GDK_SCALE}" \
                                GDK_DPI_SCALE="${QEMU_GTK_GDK_DPI_SCALE}" \
                                "${QEMU_CMD[@]}"
                fi
                exec env "${QEMU_ENV_ARGS[@]}" "${QEMU_CMD[@]}"
                ;;
        *)
                echo "unsupported arch: ${ARCH}" >&2
                exit 2
                ;;
esac
