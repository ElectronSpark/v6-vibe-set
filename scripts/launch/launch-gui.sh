#!/usr/bin/env bash
# launch-gui.sh - one-command x86_64 GUI launch for the current build tree.
#
# If the kernel or rootfs image is missing, the script reports the missing
# artifact by default.  Set AUTO_BUILD=1 to rebuild via the dev container.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-${ARCH}}"
FSIMG="${FSIMG:-${BUILD_DIR}/fs.img}"
DISPLAY_MODE="${DISPLAY_MODE:-gtk}"
QEMU_GPU="${QEMU_GPU:-virtio-vga-gl-primary}"
QEMU_APPEND="${QEMU_APPEND:-root=/dev/disk0 desktop=kde netsurf=0 webkit=0}"
AUTO_BUILD="${AUTO_BUILD:-0}"

qemu_append_if_missing() {
    local key="$1"
    local value="$2"

    if [[ " ${QEMU_APPEND} " != *" ${key}="* ]]; then
        QEMU_APPEND="${QEMU_APPEND} ${key}=${value}"
    fi
}

if [[ " ${QEMU_APPEND} " == *" desktop=kde "* ]]; then
    qemu_append_if_missing virtio_gpu_disable_pageflip_copy 0
    qemu_append_if_missing virtio_gpu_pageflip_copy 1
    qemu_append_if_missing virtio_gpu_present_minimal_drain 1
    qemu_append_if_missing virtio_gpu_present_no_drain 0
fi

export DISPLAY_MODE
export QEMU_GPU
export QEMU_APPEND

newer_than_fsimg() {
    local path

    [[ -f "${FSIMG}" ]] || return 1
    for path in "$@"; do
        [[ -e "${path}" ]] || continue
        if find "${path}" -newer "${FSIMG}" -print -quit 2>/dev/null | grep -q .; then
            return 0
        fi
    done
    return 1
}

maybe_rebuild_stale_gui_image() {
    [[ "${AUTO_BUILD}" == "1" ]] || return 0
    [[ "${DRY_RUN:-0}" != "1" ]] || return 0
    [[ -f "${FSIMG}" ]] || return 0
    command -v docker >/dev/null 2>&1 || return 0
    [[ -x "${ROOT}/scripts/container/enter-container.sh" ]] || return 0

    if newer_than_fsimg \
            "${ROOT}/ports/wayland/src" \
            "${ROOT}/ports/wayland/CMakeLists.txt" \
            "${ROOT}/ports/gtk3/CMakeLists.txt" \
            "${ROOT}/ports/gtk3/src/gdk/wayland/gdkscreen-wayland.c" \
            "${ROOT}/ports/gtk3/src/gtk/gtkheaderbar.c"; then
        echo "launch-gui: GUI port sources are newer than ${FSIMG}; rebuilding ports image..." >&2
        "${ROOT}/scripts/container/enter-container.sh" xv6-user-ports
        "${ROOT}/scripts/container/enter-container.sh" xv6-images
        return 0
    fi

    if newer_than_fsimg "${ROOT}/rootfs-overlay"; then
        echo "launch-gui: rootfs overlay is newer than ${FSIMG}; refreshing rootfs from existing sysroot..." >&2
        "${ROOT}/scripts/container/enter-container.sh" xv6-rootfs-refresh
    fi
}

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

maybe_rebuild_stale_gui_image

cmd=(bash "${SCRIPT_DIR}/run-qemu.sh" "${ARCH}" "${KERNEL_PATH}" "${FSIMG}")

if [[ "${DRY_RUN:-0}" == "1" ]]; then
    printf 'DISPLAY_MODE=%q' "${DISPLAY_MODE}"
    printf ' QEMU_GPU=%q' "${QEMU_GPU}"
    printf ' %q' "${cmd[@]}"
    printf '\n'
    exit 0
fi

exec "${cmd[@]}"
