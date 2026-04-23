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

# ──────────────────────────────────────────────────────────────────────
# KVM enablement.  Currently OPT-IN (USE_KVM=1) because the kernel
# trips a kernel-mode #GP under KVM that does not reproduce on TCG —
# almost certainly a swapgs / syscall / iretq path that TCG accepts
# leniently but real CPU semantics (enforced by KVM) reject.  Fixing
# the kernel is tracked separately; until then we stay on TCG by
# default.  Set USE_KVM=1 to try KVM anyway.
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
                echo "run-qemu: using KVM acceleration (kernel currently #GPs under KVM — see scripts/run-qemu.sh)" >&2
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
                        ${QEMU_EXTRA}
                ;;
        x86_64)
                DISPLAY_MODE="${DISPLAY_MODE:-gtk}"
                # Use mon:stdio so QEMU intercepts Ctrl-A X to quit (and
                # passes Ctrl-C through to the guest instead of killing qemu).
                #
                # For GTK, use grab-on-hover so the cursor is captured
                # by the guest as soon as the pointer enters the QEMU
                # window — without it the user has to click first, which
                # makes wlcomp appear unresponsive to mouse input.
                if [[ "${DISPLAY_MODE}" == "nographic" ]]; then
                        DISPLAY_ARGS=(-nographic -serial mon:stdio)
                elif [[ "${DISPLAY_MODE}" == "gtk" ]]; then
                        DISPLAY_ARGS=(-display gtk,grab-on-hover=on -serial mon:stdio)
                else
                        DISPLAY_ARGS=(-display "${DISPLAY_MODE}" -serial mon:stdio)
                fi
                # User-mode net w/ explicit hostfwd so a guest server on
                # 8080 is reachable from the host on 18080. Override
                # via HOSTFWD env (full -netdev user fragment).
                HOSTFWD="${HOSTFWD:-hostfwd=tcp::18080-:8080,hostfwd=tcp::15001-:5001}"
                # The kernel does not enable OSXSAVE in CR4, so any
                # CPU feature that requires XSAVE state (AVX, AVX2, ...)
                # will #UD on first use.  Under -cpu host KVM advertises
                # those via CPUID and musl's IFUNC dispatch picks the
                # AVX memcpy/strcmp paths — which then fault.  Pin the
                # CPU model to qemu64,+pcid in BOTH KVM and TCG modes;
                # PCID is required by the kernel's ASID code path.
                CPU_ARGS=(-cpu qemu64,+pcid)
                exec qemu-system-x86_64 \
                        -machine pc -smp 2 -m 256M \
                        "${KVM_ARGS[@]}" "${CPU_ARGS[@]}" \
                        "${DISPLAY_ARGS[@]}" \
                        -debugcon file:/tmp/xv6-debugcon.log \
                        -global isa-debugcon.iobase=0xe9 \
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
