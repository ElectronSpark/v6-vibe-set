#!/usr/bin/env bash
# run-qemu.sh <arch> <kernel.elf> <fs.img>
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
#   QEMU_VIRTIO_GPU_HOSTMEM=256M
#                           Host-visible memory size for blob resources.
#   QEMU_REQUIRE_HOST_DRI=0 Fail instead of warning when an accelerated GTK
#                           GPU launch cannot see host /dev/dri.
#   QEMU_REQUIRE_UDMABUF=0  Fail instead of warning when blob resources are
#                           requested but /dev/udmabuf is unavailable.
#   QEMU_HOST_GL=auto       Host OpenGL provider. auto selects WSL D3D12 when
#                           /dev/dxg and Mesa d3d12 are available. Set
#                           default to leave Mesa selection alone.
#   QEMU_WSL_GL_DISPLAY=gtk QEMU display backend to use for WSL D3D12 GL.
#                           The default keeps Bochs VGA visible and attaches a
#                           separate virgl GPU for WebKit acceleration; SDL can
#                           be forced for direct virtio scanout experiments.
#   QEMU_VMMOUSE=1          Enable VMware absolute pointer. The default input
#                           path is the virtio tablet, which avoids host GTK
#                           pointer-grab scaling ambiguity.
#   QEMU_INPUT=virtio       Add a virtio tablet for absolute host pointer input.
#   QEMU_GTK_GDK_SCALE=1    Force QEMU's GTK window to a 1:1 host scale.
#   QEMU_GTK_GRAB_ON_HOVER=on
#                           Grab pointer/keyboard as the cursor enters GTK.
#   QEMU_GTK_SHOW_CURSOR=off
#                           Hide the host cursor and use the guest cursor.
#   QEMU_DRY_RUN=1          Print the resolved qemu command and exit.
#   QEMU_EXTRA='...'        Still accepted for extra raw QEMU args.
set -euo pipefail

if [[ $# -ne 3 ]]; then
        echo "usage: $0 <arch> <kernel.elf> <fs.img>" >&2
        exit 1
fi
ARCH="$1"; KERNEL="$2"; FSIMG="$3"

QEMU_EXTRA="${QEMU_EXTRA:-}"
QEMU_CPUS="${QEMU_CPUS:-6}"
QEMU_MEMORY="${QEMU_MEMORY:-4G}"
QEMU_CPU="${QEMU_CPU:-qemu64}"
QEMU_APPEND="${QEMU_APPEND:-root=/dev/disk0}"
QEMU_VMMOUSE="${QEMU_VMMOUSE:-0}"
QEMU_INPUT="${QEMU_INPUT:-virtio}"
if [[ -z "${QEMU_MACHINE:-}" ]]; then
        if [[ "${QEMU_VMMOUSE}" == "1" ]]; then
                QEMU_MACHINE="pc,vmport=on"
        else
                QEMU_MACHINE="pc,vmport=off"
        fi
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
QEMU_VIRTIO_GPU_HOSTMEM="${QEMU_VIRTIO_GPU_HOSTMEM:-256M}"
QEMU_REQUIRE_HOST_DRI="${QEMU_REQUIRE_HOST_DRI:-0}"
QEMU_REQUIRE_UDMABUF="${QEMU_REQUIRE_UDMABUF:-0}"
QEMU_HOST_GL="${QEMU_HOST_GL:-auto}"
QEMU_WSL_GL_DISPLAY="${QEMU_WSL_GL_DISPLAY:-gtk}"
QEMU_GTK_FULLSCREEN="${QEMU_GTK_FULLSCREEN:-off}"
QEMU_GTK_ZOOM_TO_FIT="${QEMU_GTK_ZOOM_TO_FIT:-off}"
QEMU_GTK_GRAB_ON_HOVER="${QEMU_GTK_GRAB_ON_HOVER:-on}"
QEMU_GTK_SHOW_CURSOR="${QEMU_GTK_SHOW_CURSOR:-off}"
QEMU_GTK_SHOW_MENUBAR="${QEMU_GTK_SHOW_MENUBAR:-off}"
QEMU_GTK_SHOW_TABS="${QEMU_GTK_SHOW_TABS:-off}"
QEMU_GTK_GDK_SCALE="${QEMU_GTK_GDK_SCALE:-1}"
QEMU_GTK_GDK_DPI_SCALE="${QEMU_GTK_GDK_DPI_SCALE:-1}"

if [[ "${ARCH}" == "x86_64" && " ${QEMU_APPEND} " != *" video="* ]]; then
        QEMU_APPEND="${QEMU_APPEND} video=${QEMU_VIRTIO_GPU_XRES}x${QEMU_VIRTIO_GPU_YRES}"
fi

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
                echo "run-qemu: requesting sudo to chmod a+rw /dev/kvm ..." >&2
                if sudo chmod a+rw /dev/kvm; then
                        echo "run-qemu: /dev/kvm is now accessible." >&2
                else
                        echo "run-qemu: sudo failed; falling back to TCG." >&2
                fi
        fi
        if [[ -r /dev/kvm && -w /dev/kvm ]]; then
                KVM_ARGS=(-enable-kvm)
                echo "run-qemu: using KVM acceleration" >&2
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

print_host_gpu_hint() {
        echo "run-qemu: expose host GPU acceleration before expecting smooth WebKit video:" >&2
        echo "run-qemu:   bare host: ensure a hardware /dev/dri/renderD* is readable/writable" >&2
        echo "run-qemu:   WSL2: ensure /dev/dxg exists; QEMU_HOST_GL=auto will use Mesa D3D12 with SDL" >&2
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
                                QEMU_ENV_ARGS+=(
                                        SDL_VIDEODRIVER=x11
                                        MESA_LOADER_DRIVER_OVERRIDE=d3d12
                                        GALLIUM_DRIVER=d3d12
                                        LIBGL_ALWAYS_SOFTWARE=0
                                )
                                ;;
                        *)
                                echo "unsupported QEMU_HOST_GL: ${QEMU_HOST_GL}" >&2
                                exit 2
                                ;;
                esac
                if [[ "${QEMU_GPU}" == "auto" ]]; then
                        # Keep plain GUI boots on the simple framebuffer path,
                        # but WebKit acceleration needs the displayed fb0 to be
                        # the virtio-gpu scanout.  Otherwise every video frame
                        # lands in virgl and is then copied through Bochs VGA.
                        if [[ "${DISPLAY_MODE}" == "gtk" &&
                              ( " ${QEMU_APPEND} " == *" webkit_accel=1 "* ||
                                " ${QEMU_APPEND} " == *" glsmoke_accel=1 "* ) ]]; then
                                QEMU_GPU="virtio-vga-gl-primary"
                        else
                                QEMU_GPU="bochs"
                        fi
                        echo "run-qemu: auto GPU selected ${QEMU_GPU}" >&2
                fi
                if [[ "${HOST_GL_MODE}" == "wsl-d3d12" && "${QEMU_GPU}" == *"-gl"* &&
                      "${DISPLAY_MODE}" == "gtk" ]]; then
                        echo "run-qemu: WSL D3D12 host GL selected; switching display gtk -> ${QEMU_WSL_GL_DISPLAY} for virgl" >&2
                        DISPLAY_MODE="${QEMU_WSL_GL_DISPLAY}"
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
                #   - show-cursor=off     Hide the host pointer so wlcomp's
                #                         single guest cursor is the only cursor
                #                         visible in the VM.
                # Press Ctrl-Alt-G to release the grab.
                if [[ "${DISPLAY_MODE}" == "nographic" ]]; then
                        DISPLAY_ARGS=(-nographic -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "gtk" && "${QEMU_GPU}" == *"-gl"* ]]; then
                        DISPLAY_ARGS=(-display "gtk,gl=on,grab-on-hover=${QEMU_GTK_GRAB_ON_HOVER},show-cursor=${QEMU_GTK_SHOW_CURSOR},full-screen=${QEMU_GTK_FULLSCREEN},zoom-to-fit=${QEMU_GTK_ZOOM_TO_FIT},show-menubar=${QEMU_GTK_SHOW_MENUBAR},show-tabs=${QEMU_GTK_SHOW_TABS}"
                                      -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "sdl" && "${QEMU_GPU}" == *"-gl"* ]]; then
                        DISPLAY_ARGS=(-display "sdl,gl=on,show-cursor=${QEMU_GTK_SHOW_CURSOR}"
                                      -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "gtk" ]]; then
                        # Forward pointer motion as soon as the host cursor
                        # enters the GTK window.  Relying on click-to-grab can
                        # leave the guest cursor apparently frozen on Wayland.
                        DISPLAY_ARGS=(-display "gtk,grab-on-hover=${QEMU_GTK_GRAB_ON_HOVER},show-cursor=${QEMU_GTK_SHOW_CURSOR},full-screen=${QEMU_GTK_FULLSCREEN},zoom-to-fit=${QEMU_GTK_ZOOM_TO_FIT},show-menubar=${QEMU_GTK_SHOW_MENUBAR},show-tabs=${QEMU_GTK_SHOW_TABS}"
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
                if [[ "${QEMU_VIRTIO_GPU_BLOB}" == "auto" ]]; then
                        if [[ -r /dev/udmabuf && -w /dev/udmabuf ]]; then
                                QEMU_VIRTIO_GPU_BLOB=1
                        else
                                QEMU_VIRTIO_GPU_BLOB=0
                        fi
                fi
                if [[ "${QEMU_VIRTIO_GPU_BLOB}" == "1" ]]; then
                        if [[ ! -r /dev/udmabuf || ! -w /dev/udmabuf ]]; then
                                echo "run-qemu: QEMU_VIRTIO_GPU_BLOB=1 needs /dev/udmabuf on this launcher path." >&2
                                print_host_gpu_hint
                                exit 2
                        fi
                        gpu_gl_opts+=",blob=true,hostmem=${QEMU_VIRTIO_GPU_HOSTMEM}"
                elif [[ "${QEMU_REQUIRE_UDMABUF}" == "1" ]]; then
                        echo "run-qemu: QEMU_REQUIRE_UDMABUF=1 but /dev/udmabuf is unavailable." >&2
                        print_host_gpu_hint
                        exit 2
                fi
                case "${QEMU_GPU}" in
                        bochs)
                                ;;
                        virtio-gpu)
                                GPU_ARGS=(-device "virtio-gpu-pci,xres=${QEMU_VIRTIO_GPU_XRES},yres=${QEMU_VIRTIO_GPU_YRES}")
                                ;;
                        virtio-gpu-primary)
                                GPU_ARGS=(-vga none -device "virtio-gpu-pci,xres=${QEMU_VIRTIO_GPU_XRES},yres=${QEMU_VIRTIO_GPU_YRES}")
                                ;;
                        virtio-gpu-gl)
                                GPU_ARGS=(-device "virtio-gpu-gl-pci,${gpu_gl_opts}")
                                ;;
                        virtio-gpu-gl-primary)
                                # Compatibility alias for the historical
                                # two-adapter setup: Bochs remains the visible
                                # console while virtio-gpu-gl supplies render
                                # nodes.  For smoother WebKit video, prefer
                                # QEMU_GPU=virtio-vga-gl-primary.
                                GPU_ARGS=(-device "virtio-gpu-gl-pci,${gpu_gl_opts}")
                                ;;
                        virtio-vga-gl-primary)
                                # Experimental direct GL scanout path.  This
                                # removes the Bochs VGA fallback and asks QEMU
                                # to make the virgl device the visible primary
                                # adapter.  It can reduce host-side display
                                # indirection on native Linux, but xv6 needs
                                # virtio-gpu scanout/fb support for the
                                # desktop to start.
                                GPU_ARGS=(-vga none -device "virtio-vga-gl,${gpu_gl_opts}")
                                ;;
                        none)
                                GPU_ARGS=(-vga none)
                                ;;
                        *)
                                echo "unsupported QEMU_GPU: ${QEMU_GPU}" >&2
                                exit 2
                                ;;
                esac
                INPUT_ARGS=()
                case "${QEMU_INPUT}" in
                        virtio)
                                INPUT_ARGS=(-device virtio-tablet-pci)
                                ;;
                        ps2|none)
                                ;;
                        *)
                                echo "unsupported QEMU_INPUT: ${QEMU_INPUT}" >&2
                                exit 2
                                ;;
                esac
                # The kernel does not enable OSXSAVE in CR4, so any
                # CPU feature that requires XSAVE state (AVX, AVX2, ...)
                # will #UD on first use.  Under -cpu host KVM advertises
                # those via CPUID and libc IFUNC dispatch can pick AVX
                # memcpy/strcmp paths — which then fault.  Keep the CPU model
                # conservative in BOTH KVM and TCG modes.
                # KVM exposes hardware PCID when requested, which currently
                # sends the kernel down a lockup-prone ASID/PCID path after
                # userspace starts.  Override with QEMU_CPU=qemu64,+pcid when
                # debugging that path directly.
                CPU_ARGS=(-cpu "${QEMU_CPU}")
                QEMU_CMD=(qemu-system-x86_64
                        -machine "${QEMU_MACHINE}" -smp "${QEMU_CPUS}" -m "${QEMU_MEMORY}"
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
