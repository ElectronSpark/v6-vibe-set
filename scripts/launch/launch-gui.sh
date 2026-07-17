#!/usr/bin/env bash
# launch-gui.sh - one-command x86_64 GUI launch for the current build tree.
#
# Prefer the newest immutable reproduction receipt. Set AUTO_BUILD=1 to create
# one through the dev container when no complete receipt exists.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

ARCH="${ARCH:-x86_64}"
RECEIPT_ROOT="${XV6_REPRODUCTION_ROOT:-${ROOT}/build-reproductions/${ARCH}}"
if [[ -n "${XV6_RECEIPT:-}" ]]; then
    BUILD_DIR="$(realpath -e -- "${XV6_RECEIPT}")"
elif [[ -f "${RECEIPT_ROOT}/latest/.xv6-reproduction-complete" ]]; then
    BUILD_DIR="$(realpath -e -- "${RECEIPT_ROOT}/latest")"
else
    BUILD_DIR="${BUILD_DIR:-${ROOT}/build-${ARCH}}"
fi
FSIMG="${FSIMG:-${BUILD_DIR}/fs.img}"
DISPLAY_MODE="${DISPLAY_MODE:-sdl}"
QEMU_GPU="${QEMU_GPU:-virtio-vga-gl-primary}"
QEMU_APPEND="${QEMU_APPEND:-root=/dev/disk0 netsurf=0 webkit=0}"
QEMU_GTK_CURSOR_MODE="${QEMU_GTK_CURSOR_MODE:-guest}"
QEMU_GTK_SHOW_CURSOR="${QEMU_GTK_SHOW_CURSOR:-off}"
AUTO_BUILD="${AUTO_BUILD:-0}"
QEMU_SDL_PATCHED_BIN="${QEMU_SDL_PATCHED_BIN:-${BUILD_DIR}/qemu-sdl/bin/qemu-system-x86_64}"
QEMU_SDL_DATA_DIR="${QEMU_SDL_DATA_DIR:-${BUILD_DIR}/qemu-sdl/share/qemu}"
export DISPLAY_MODE
export QEMU_GPU
export QEMU_APPEND
export QEMU_GTK_CURSOR_MODE QEMU_GTK_SHOW_CURSOR
export QEMU_SDL_PATCHED_BIN QEMU_SDL_DATA_DIR

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
        "${ROOT}/scripts/container/reproduce-workspace.sh"
        return 0
    fi

    if newer_than_fsimg "${ROOT}/rootfs-overlay"; then
        echo "launch-gui: rootfs overlay is newer than ${FSIMG}; refreshing rootfs from existing sysroot..." >&2
        "${ROOT}/scripts/container/reproduce-workspace.sh"
    fi
}

if [[ "${ARCH}" != "x86_64" ]]; then
    echo "launch-gui: only x86_64 GUI launch is wired right now (ARCH=${ARCH})" >&2
    exit 2
fi

kernel_candidates=("${KERNEL:-}" "${BUILD_DIR}/kernel/build/kernel/xv6.bin")

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
        echo "launch-gui: build artifacts missing — running clean reproduction..." >&2
        "${ROOT}/scripts/container/reproduce-workspace.sh"
        if [[ -f "${RECEIPT_ROOT}/latest/.xv6-reproduction-complete" ]]; then
            BUILD_DIR="$(realpath -e -- "${RECEIPT_ROOT}/latest")"
            FSIMG="${BUILD_DIR}/fs.img"
            kernel_candidates=("${BUILD_DIR}/kernel/build/kernel/xv6.bin")
            QEMU_SDL_PATCHED_BIN="${BUILD_DIR}/qemu-sdl/bin/qemu-system-x86_64"
            QEMU_SDL_DATA_DIR="${BUILD_DIR}/qemu-sdl/share/qemu"
            export QEMU_SDL_PATCHED_BIN QEMU_SDL_DATA_DIR
        fi
        find_kernel || true
    fi
fi

if [[ -z "${KERNEL_PATH:-}" ]]; then
    echo "launch-gui: kernel image not found. Tried:" >&2
    printf '  %s\n' "${kernel_candidates[@]}" >&2
    echo "launch-gui: build first with: scripts/container/enter-container.sh xv6-build" >&2
    exit 1
fi

if [[ ! -f "${FSIMG}" ]]; then
    echo "launch-gui: rootfs image not found: ${FSIMG}" >&2
    echo "launch-gui: build first with: scripts/container/enter-container.sh xv6-build" >&2
    exit 1
fi

maybe_rebuild_stale_gui_image

if [[ -f "${BUILD_DIR}/.xv6-reproduction-complete" ]]; then
    (
        cd "${BUILD_DIR}"
        sha256sum -c artifacts.sha256
    )
    [[ -x "${QEMU_SDL_PATCHED_BIN}" ]] || {
        echo "launch-gui: receipt lacks corrected SDL QEMU: ${QEMU_SDL_PATCHED_BIN}" >&2
        exit 1
    }
fi

cmd=(bash "${SCRIPT_DIR}/run-owned-qemu.sh" "${ARCH}" "${KERNEL_PATH}" "${FSIMG}")

if [[ "${DRY_RUN:-0}" == "1" ]]; then
    printf 'DISPLAY_MODE=%q' "${DISPLAY_MODE}"
    printf ' QEMU_GPU=%q' "${QEMU_GPU}"
    printf ' %q' "${cmd[@]}"
    printf '\n'
    exit 0
fi

exec "${cmd[@]}"
