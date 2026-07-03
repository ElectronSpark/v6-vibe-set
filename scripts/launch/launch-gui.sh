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
# GUI freezes are usually debugged after the guest is already running.  Keep
# QEMU's GDB stub available by default, but never pause at reset here.
QEMU_GDB=1
QEMU_GDB_WAIT=0
QEMU_GDB_PORT="${QEMU_GDB_PORT:-1234}"
# KDE is started from /etc/startup with the rootfs' loader guardrails
# (notably LD_BIND_NOW=1).  Passing desktop=kde here bypasses that proven
# startup path and can make KWin abort during Plasma startup.
QEMU_APPEND="${QEMU_APPEND:-root=/dev/disk0 netsurf=0 webkit=0}"
AUTO_BUILD="${AUTO_BUILD:-0}"

export DISPLAY_MODE
export QEMU_GPU
export QEMU_GDB
export QEMU_GDB_WAIT
export QEMU_GDB_PORT
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
    printf ' QEMU_GDB=%q' "${QEMU_GDB}"
    printf ' QEMU_GDB_WAIT=%q' "${QEMU_GDB_WAIT}"
    printf ' QEMU_GDB_PORT=%q' "${QEMU_GDB_PORT}"
    printf ' %q' "${cmd[@]}"
    printf '\n'
    exit 0
fi

# WSLg's PulseAudio socket can accept connections but never complete the
# client handshake (server hung on the Windows side).  run-qemu's auto
# audio detection then picks "pa" and QEMU exits 1 with
# "could not connect to PulseAudio server".  A plain socket probe cannot
# detect this, so retry once with a WAV sink when the first attempt fails
# with the PulseAudio signature and the backend was not explicitly forced
# by the caller.  The WAV backend keeps guest playback drainable without
# requiring host audio; a virtio-sound card backed by "none" can leave
# Chromium with AUDIO_RENDERER_ERROR on media pages.
if [[ "${QEMU_AUDIO_BACKEND:-auto}" == "auto" ]]; then
    stderr_log="$(mktemp /tmp/launch-gui-stderr.XXXXXX)"
    trap 'rm -f "${stderr_log}" "${audio_wav_path:-}"' EXIT
    set +e
    "${cmd[@]}" 2> >(tee "${stderr_log}" >&2)
    rc=$?
    set -e
    if [[ ${rc} -ne 0 ]] && grep -q \
            -e "could not connect to PulseAudio" \
            -e "Failed to initialize PA context" \
            "${stderr_log}"; then
        audio_wav_path="$(mktemp /tmp/xv6-qemu-audio.XXXXXX.wav)"
        echo "launch-gui: PulseAudio backend failed (WSLg server hung?); retrying with QEMU_AUDIO_BACKEND=wav,path=${audio_wav_path}" >&2
        QEMU_AUDIO_BACKEND="wav,path=${audio_wav_path}" "${cmd[@]}"
        rc=$?
    fi
    exit "${rc}"
fi

exec "${cmd[@]}"
