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
QEMU_MACHINE="${QEMU_MACHINE:-pc,vmport=on}"
QEMU_NET="${QEMU_NET:-1}"
QEMU_NETSURF="${QEMU_NETSURF:-auto}"
QEMU_GPU="${QEMU_GPU:-bochs}"
QEMU_GDB="${QEMU_GDB:-0}"
QEMU_GDB_PORT="${QEMU_GDB_PORT:-1234}"
QEMU_GDB_WAIT="${QEMU_GDB_WAIT:-0}"
QEMU_GDB_ARGS=()
QEMU_VIRTIO_GPU_XRES="${QEMU_VIRTIO_GPU_XRES:-1024}"
QEMU_VIRTIO_GPU_YRES="${QEMU_VIRTIO_GPU_YRES:-768}"
QEMU_GTK_FULLSCREEN="${QEMU_GTK_FULLSCREEN:-off}"
QEMU_GTK_ZOOM_TO_FIT="${QEMU_GTK_ZOOM_TO_FIT:-off}"

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
# KVM enablement.  Currently OPT-IN (USE_KVM=1) because the kernel can
# lock up under KVM after userspace starts, while the same image runs
# under TCG.  Fixing the kernel is tracked separately; until then we
# stay on TCG by default.  Set USE_KVM=1 to try KVM anyway.
# ──────────────────────────────────────────────────────────────────────
USE_KVM="${USE_KVM:-0}"
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
                echo "run-qemu: using KVM acceleration (kernel may lock up under KVM — see scripts/run-qemu.sh)" >&2
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
                #   - grab-on-hover=on    Capture pointer + keyboard as soon
                #                         as the host cursor enters the QEMU
                #                         canvas. Without this, GTK does not
                #                         reliably deliver pointer/button
                #                         events to QEMU's canvas widget on
                #                         GNOME/Wayland and clicks are lost.
                #   - show-cursor=off     Hide the host pointer inside the
                #                         window so only wlcomp's guest
                #                         cursor sprite is visible. While
                #                         grabbed, the host cursor would
                #                         otherwise freeze in place anyway.
                # Press Ctrl-Alt-G to release the grab.
                if [[ "${DISPLAY_MODE}" == "nographic" ]]; then
                        DISPLAY_ARGS=(-nographic -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "gtk" && "${QEMU_GPU}" == *"-gl"* ]]; then
                        DISPLAY_ARGS=(-display "gtk,gl=on,grab-on-hover=on,show-cursor=off,full-screen=${QEMU_GTK_FULLSCREEN},zoom-to-fit=${QEMU_GTK_ZOOM_TO_FIT}"
                                      -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "gtk" ]]; then
                        # Forward pointer motion as soon as the host cursor
                        # enters the GTK window.  Relying on click-to-grab can
                        # leave the guest cursor apparently frozen on Wayland.
                        DISPLAY_ARGS=(-display "gtk,grab-on-hover=on,show-cursor=off,full-screen=${QEMU_GTK_FULLSCREEN},zoom-to-fit=${QEMU_GTK_ZOOM_TO_FIT}"
                                      -serial mon:stdio)
                else
                        DISPLAY_ARGS=(-display "${DISPLAY_MODE}" -serial mon:stdio)
                fi
                NET_ARGS=()
                if [[ "${QEMU_NET}" == "1" ]]; then
                        # User-mode net w/ explicit hostfwd so a guest server on
                        # 8080 is reachable from the host on 18080. Override
                        # via HOSTFWD env (full -netdev user fragment).
                        HOSTFWD="${HOSTFWD:-hostfwd=tcp::18080-:8080,hostfwd=tcp::15001-:5001}"
                        NET_ARGS=(-netdev user,id=n0,${HOSTFWD}
                                  -device e1000,netdev=n0)
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
                exec qemu-system-x86_64 \
                        -machine "${QEMU_MACHINE}" -smp "${QEMU_CPUS}" -m "${QEMU_MEMORY}" \
                        "${KVM_ARGS[@]}" "${CPU_ARGS[@]}" \
                        "${DISPLAY_ARGS[@]}" \
                        -debugcon file:/tmp/xv6-debugcon.log \
                        -global isa-debugcon.iobase=0xe9 \
                        -kernel "${KERNEL}" \
                        -drive file="${FSIMG}",if=none,format=raw,id=x0 \
                        -device virtio-blk-pci,drive=x0 \
                        "${GPU_ARGS[@]}" \
                        "${NET_ARGS[@]}" \
                        -append "${QEMU_APPEND}" \
                        "${QEMU_GDB_ARGS[@]}" \
                        ${QEMU_EXTRA}
                ;;
        *)
                echo "unsupported arch: ${ARCH}" >&2
                exit 2
                ;;
esac
