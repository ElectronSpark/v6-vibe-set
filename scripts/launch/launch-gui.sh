#!/usr/bin/env bash
# launch-gui.sh - one-command x86_64 GUI launch for the current build tree.
#
# If the kernel or rootfs image is missing, the script auto-builds via the
# dev container (scripts/container/enter-container.sh xv6-build) when Docker
# is available.  Set AUTO_BUILD=0 to disable.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-${ARCH}}"
FSIMG="${FSIMG:-${BUILD_DIR}/fs.img}"
DISPLAY_MODE="${DISPLAY_MODE:-gtk}"
AUTO_BUILD="${AUTO_BUILD:-1}"
export DISPLAY_MODE

if [[ "${ARCH}" != "x86_64" ]]; then
    echo "launch-gui: only x86_64 GUI launch is wired right now (ARCH=${ARCH})" >&2
    exit 2
fi

kernel_candidates=(
    "${KERNEL:-}"
    "${BUILD_DIR}/kernel/build/kernel/xv6.bin"
    "${BUILD_DIR}/kernel/xv6.bin"
    "${BUILD_DIR}/kernel/kernel.elf"
    "${BUILD_DIR}/kernel/build/kernel/kernel"
    "${BUILD_DIR}/kernel/kernel/kernel"
)

find_kernel() {
    KERNEL_PATH=""
    for candidate in "${kernel_candidates[@]}"; do
        if [[ -n "${candidate}" && -f "${candidate}" ]]; then
            KERNEL_PATH="${candidate}"
            return 0
        fi
    done
    return 1
}

if ! find_kernel || [[ ! -f "${FSIMG}" ]]; then
    if [[ "${AUTO_BUILD}" == "1" ]] && command -v docker >/dev/null 2>&1 \
            && [[ -x "${ROOT}/scripts/container/enter-container.sh" ]]; then
        echo "launch-gui: build artifacts missing — running xv6-build via container..." >&2
        "${ROOT}/scripts/container/enter-container.sh" xv6-build
        find_kernel || true
    fi
fi

if [[ -z "${KERNEL_PATH:-}" ]]; then
    echo "launch-gui: kernel image not found. Tried:" >&2
    printf '  %s\n' "${kernel_candidates[@]}" >&2
    echo "launch-gui: build first with: scripts/container/enter-container.sh xv6-build" >&2
    exit 1
fi

if [[ -z "${KERNEL:-}" && "${KERNEL_PATH}" != "${BUILD_DIR}/kernel/build/kernel/xv6.bin" &&
      "${KERNEL_PATH}" != "${BUILD_DIR}/kernel/xv6.bin" ]]; then
    echo "launch-gui: warning: using non-Linux-boot kernel artifact: ${KERNEL_PATH}" >&2
    echo "launch-gui: build the umbrella kernel target to install: ${BUILD_DIR}/kernel/build/kernel/xv6.bin" >&2
fi

if [[ ! -f "${FSIMG}" ]]; then
    echo "launch-gui: rootfs image not found: ${FSIMG}" >&2
    echo "launch-gui: build first with: scripts/container/enter-container.sh xv6-build" >&2
    exit 1
fi

cmd=(bash "${SCRIPT_DIR}/run-qemu.sh" "${ARCH}" "${KERNEL_PATH}" "${FSIMG}")

if [[ "${DRY_RUN:-0}" == "1" ]]; then
    printf 'DISPLAY_MODE=%q' "${DISPLAY_MODE}"
    printf ' %q' "${cmd[@]}"
    printf '\n'
    exit 0
fi

exec "${cmd[@]}"
