#!/usr/bin/env bash
# launch-gui.sh - one-command x86_64 GUI launch for the current build tree.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-${ARCH}}"
FSIMG="${FSIMG:-${BUILD_DIR}/fs.img}"
DISPLAY_MODE="${DISPLAY_MODE:-gtk}"
export DISPLAY_MODE

if [[ "${ARCH}" != "x86_64" ]]; then
    echo "launch-gui: only x86_64 GUI launch is wired right now (ARCH=${ARCH})" >&2
    exit 2
fi

kernel_candidates=(
    "${KERNEL:-}"
    "${BUILD_DIR}/kernel/build/kernel/kernel"
    "${BUILD_DIR}/kernel/kernel/kernel"
)

KERNEL_PATH=""
for candidate in "${kernel_candidates[@]}"; do
    if [[ -n "${candidate}" && -f "${candidate}" ]]; then
        KERNEL_PATH="${candidate}"
        break
    fi
done

if [[ -z "${KERNEL_PATH}" ]]; then
    echo "launch-gui: kernel image not found. Tried:" >&2
    printf '  %s\n' "${kernel_candidates[@]}" >&2
    exit 1
fi

if [[ ! -f "${FSIMG}" ]]; then
    echo "launch-gui: rootfs image not found: ${FSIMG}" >&2
    echo "launch-gui: build/regenerate it with: cmake --build ${BUILD_DIR} --target rootfs" >&2
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