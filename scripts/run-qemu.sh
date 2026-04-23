#!/usr/bin/env bash
# run-qemu.sh <arch> <kernel.elf> <fs.img>
#
# Boots the kernel + xv6fs disk image in qemu-system-<arch>.
# Set DISPLAY_MODE=gtk|sdl|nographic (default gtk on x86_64, nographic on riscv64).
# Override QEMU_EXTRA in env for extra args (e.g. -gdb tcp::1234 -S).
set -euo pipefail

if [[ $# -ne 3 ]]; then
        echo "usage: $0 <arch> <kernel.elf> <fs.img>" >&2
        exit 1
fi
ARCH="$1"; KERNEL="$2"; FSIMG="$3"

QEMU_EXTRA="${QEMU_EXTRA:-}"

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
                        ${QEMU_EXTRA}
                ;;
        x86_64)
                DISPLAY_MODE="${DISPLAY_MODE:-gtk}"
                # Use mon:stdio so QEMU intercepts Ctrl-A X to quit (and
                # passes Ctrl-C through to the guest instead of killing qemu).
                if [[ "${DISPLAY_MODE}" == "nographic" ]]; then
                        DISPLAY_ARGS=(-nographic -serial mon:stdio)
                else
                        DISPLAY_ARGS=(-display "${DISPLAY_MODE}" -serial mon:stdio)
                fi
                # User-mode net w/ explicit hostfwd so a guest server on
                # 8080 is reachable from the host on 18080. Override
                # via HOSTFWD env (full -netdev user fragment).
                HOSTFWD="${HOSTFWD:-hostfwd=tcp::18080-:8080,hostfwd=tcp::15001-:5001}"
                exec qemu-system-x86_64 \
                        -machine pc -cpu qemu64,+pcid -smp 2 -m 256M \
                        "${DISPLAY_ARGS[@]}" \
                        -kernel "${KERNEL}" \
                        -drive file="${FSIMG}",if=none,format=raw,id=x0 \
                        -device virtio-blk-pci,drive=x0 \
                        -netdev user,id=n0,${HOSTFWD} \
                        -device e1000,netdev=n0 \
                        -append "root=/dev/disk0" \
                        ${QEMU_EXTRA}
                ;;
        *)
                echo "unsupported arch: ${ARCH}" >&2
                exit 2
                ;;
esac
