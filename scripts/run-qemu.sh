#!/usr/bin/env bash
# run-qemu.sh <arch> <kernel.elf> <fs.img>
#
# Boots the kernel + xv6fs disk image in qemu-system-<arch>.
# Set DISPLAY_MODE=gtk|sdl|nographic (default gtk on x86_64, nographic on riscv64).
# Debugging:
#   QEMU_GDB=1              Enable QEMU's GDB stub on tcp::1234.
#   QEMU_GDB_PORT=2159      Use a different GDB stub port.
#   QEMU_GDB_WAIT=1         Start paused at reset until GDB continues.
#   QEMU_GPU=bochs          GPU model: bochs, virtio-gpu,
#                           virtio-gpu-primary, virtio-gpu-gl,
#                           virtio-gpu-gl-primary, or none.
#   QEMU_VMMOUSE=1          Enable VMware absolute pointer. The default is
#                           grabbed relative PS/2 input because vmport
#                           absolute coordinates are host/GTK-version fragile.
#   QEMU_INPUT=virtio       Add a virtio tablet for absolute host pointer input.
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
QEMU_GPU="${QEMU_GPU:-bochs}"
QEMU_GDB="${QEMU_GDB:-0}"
QEMU_GDB_PORT="${QEMU_GDB_PORT:-1234}"
QEMU_GDB_WAIT="${QEMU_GDB_WAIT:-0}"
QEMU_GDB_ARGS=()
QEMU_VIRTIO_GPU_XRES="${QEMU_VIRTIO_GPU_XRES:-1280}"
QEMU_VIRTIO_GPU_YRES="${QEMU_VIRTIO_GPU_YRES:-800}"
QEMU_GTK_FULLSCREEN="${QEMU_GTK_FULLSCREEN:-off}"
QEMU_GTK_ZOOM_TO_FIT="${QEMU_GTK_ZOOM_TO_FIT:-off}"
QEMU_GTK_GRAB_ON_HOVER="${QEMU_GTK_GRAB_ON_HOVER:-on}"
QEMU_GTK_SHOW_CURSOR="${QEMU_GTK_SHOW_CURSOR:-off}"
QEMU_GTK_SHOW_MENUBAR="${QEMU_GTK_SHOW_MENUBAR:-off}"
QEMU_GTK_SHOW_TABS="${QEMU_GTK_SHOW_TABS:-off}"

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
                # Use mon:stdio so QEMU intercepts Ctrl-A X to quit (and
                # passes Ctrl-C through to the guest instead of killing qemu).
                #
                # GTK input notes:
                #   - QEMU_VMMOUSE=0      Use grabbed PS/2 relative motion by
                #                         default. QEMU's vmmouse remains
                #                         available with QEMU_VMMOUSE=1, but
                #                         has shown host/GTK-version dependent
                #                         coordinate stalls.
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
                                                  -device e1000,netdev=n0)
                                        ;;
                                tap)
                                        QEMU_NET_TAP_IFNAME="${QEMU_NET_TAP_IFNAME:-tap0}"
                                        QEMU_NET_TAP_SCRIPT="${QEMU_NET_TAP_SCRIPT:-no}"
                                        QEMU_NET_TAP_DOWN="${QEMU_NET_TAP_DOWN:-no}"
                                        NET_ARGS=(-netdev "tap,id=n0,ifname=${QEMU_NET_TAP_IFNAME},script=${QEMU_NET_TAP_SCRIPT},downscript=${QEMU_NET_TAP_DOWN}"
                                                  -device e1000,netdev=n0)
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
                                GPU_ARGS=(-device "virtio-gpu-gl-pci,xres=${QEMU_VIRTIO_GPU_XRES},yres=${QEMU_VIRTIO_GPU_YRES}")
                                ;;
                        virtio-gpu-gl-primary)
                                GPU_ARGS=(-vga none -device "virtio-gpu-gl-pci,xres=${QEMU_VIRTIO_GPU_XRES},yres=${QEMU_VIRTIO_GPU_YRES}")
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
                # those via CPUID and musl's IFUNC dispatch picks the
                # AVX memcpy/strcmp paths — which then fault.  Keep the
                # CPU model conservative in BOTH KVM and TCG modes.
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
                        printf '%s\n' "${QEMU_CMD[*]}"
                        exit 0
                fi
                exec "${QEMU_CMD[@]}"
                ;;
        *)
                echo "unsupported arch: ${ARCH}" >&2
                exit 2
                ;;
esac
