#!/usr/bin/env bash
# Stage host-package Xwayland runtime files into the generated sysroot.
set -euo pipefail

SYSROOT="${1:?usage: $0 <sysroot> <xwayland-wrapper>}"
WRAPPER="${2:?usage: $0 <sysroot> <xwayland-wrapper>}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KDE_WRAPPER_SRC="${SCRIPT_DIR}/xwayland-kde-wrapper.c"
KDE_WRAPPER_BIN="${SYSROOT}/host-tools/bin/Xwayland-kde-wrapper"

note() {
    echo "stage-xwayland-runtime: $*" >&2
}

need_tool() {
    local name="$1"
    local path

    path="$(command -v "${name}" || true)"
    if [[ -z "${path}" && -x "/usr/bin/${name}" ]]; then
        path="/usr/bin/${name}"
    fi
    if [[ -z "${path}" && -x "/usr/lib/xorg/${name}" ]]; then
        path="/usr/lib/xorg/${name}"
    fi
    if [[ -z "${path}" ]]; then
        echo "stage-xwayland-runtime: error: ${name} not found" >&2
        exit 1
    fi
    printf '%s\n' "${path}"
}

find_host_tool() {
    local name="$1"
    local path

    path="$(command -v "${name}" || true)"
    if [[ -z "${path}" && -x "/usr/bin/${name}" ]]; then
        path="/usr/bin/${name}"
    fi
    if [[ -z "${path}" && -x "/usr/lib/xorg/${name}" ]]; then
        path="/usr/lib/xorg/${name}"
    fi
    printf '%s\n' "${path}"
}

find_sysroot_tool() {
    local name="$1"
    local candidate

    for candidate in \
        "${SYSROOT}/bin/${name}.real" \
        "${SYSROOT}/usr/bin/${name}.real" \
        "${SYSROOT}/usr/bin/${name}" \
        "${SYSROOT}/bin/${name}"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
}

copy_host_file() {
    local src="$1"
    local dst="$2"

    mkdir -p "$(dirname "${dst}")"
    cp -aL "${src}" "${dst}"
    chmod 0755 "${dst}" 2>/dev/null || true
}

select_xwayland_wrapper() {
    local cc_bin="${CC:-cc}"

    if [[ -f "${KDE_WRAPPER_SRC}" ]]; then
        if ! command -v "${cc_bin}" >/dev/null 2>&1; then
            echo "stage-xwayland-runtime: error: ${cc_bin} not found for KDE Xwayland wrapper" >&2
            exit 1
        fi
        mkdir -p "$(dirname "${KDE_WRAPPER_BIN}")"
        "${cc_bin}" -O2 -Wall -Wextra "${KDE_WRAPPER_SRC}" -o "${KDE_WRAPPER_BIN}"
        printf '%s\n' "${KDE_WRAPPER_BIN}"
        return 0
    fi

    printf '%s\n' "${WRAPPER}"
}

copy_elf_runtime() {
    local exe="$1"
    local lib
    local interp

    interp="$(readelf -l "${exe}" |
        sed -nE 's/.*Requesting program interpreter: ([^]]+)\].*/\1/p' |
        head -1)"
    if [[ -n "${interp}" && -e "${interp}" ]]; then
        copy_host_file "${interp}" "${SYSROOT}/lib64/$(basename "${interp}")"
    fi

    while IFS= read -r lib; do
        [[ -n "${lib}" && -e "${lib}" ]] || continue
        copy_host_file "${lib}" "${SYSROOT}/lib/$(basename "${lib}")"
    done < <(
        ldd "${exe}" 2>/dev/null |
        awk '/=> \// { print $3; next } /^[[:space:]]*\// { print $1; next }'
    )
}

XWAYLAND="$(find_host_tool Xwayland)"
XWAYLAND_SOURCE="host"
if [[ -z "${XWAYLAND}" ]]; then
    XWAYLAND="$(find_sysroot_tool Xwayland)"
    XWAYLAND_SOURCE="sysroot"
fi
if [[ -z "${XWAYLAND}" ]]; then
    echo "stage-xwayland-runtime: error: Xwayland not found" >&2
    exit 1
fi

XKBCOMP="$(find_host_tool xkbcomp)"
XKBCOMP_SOURCE="host"
if [[ -z "${XKBCOMP}" ]]; then
    XKBCOMP="$(find_sysroot_tool xkbcomp)"
    XKBCOMP_SOURCE="sysroot"
fi
if [[ -z "${XKBCOMP}" ]]; then
    echo "stage-xwayland-runtime: error: xkbcomp not found" >&2
    exit 1
fi

XKB_ROOT="${XKB_CONFIG_ROOT:-/usr/share/X11}"
XKB_SOURCE="host"
if [[ ! -d "${XKB_ROOT}/xkb" ]]; then
    if [[ -d "${SYSROOT}/share/X11/xkb" ]]; then
        XKB_ROOT="${SYSROOT}/share/X11"
        XKB_SOURCE="sysroot"
    elif [[ -d "${SYSROOT}/usr/share/X11/xkb" ]]; then
        XKB_ROOT="${SYSROOT}/usr/share/X11"
        XKB_SOURCE="sysroot"
    else
        echo "stage-xwayland-runtime: error: XKB data not found" >&2
        exit 1
    fi
fi

mkdir -p "${SYSROOT}/bin" "${SYSROOT}/usr/bin" "${SYSROOT}/usr/share" \
         "${SYSROOT}/lib" "${SYSROOT}/lib64"

if [[ "${XWAYLAND}" != "${SYSROOT}/bin/Xwayland.real" ]]; then
    copy_host_file "${XWAYLAND}" "${SYSROOT}/bin/Xwayland.real"
fi
XWAYLAND_WRAPPER_SELECTED="$(select_xwayland_wrapper)"
copy_host_file "${XWAYLAND_WRAPPER_SELECTED}" "${SYSROOT}/bin/Xwayland"
copy_host_file "${XWAYLAND_WRAPPER_SELECTED}" "${SYSROOT}/usr/bin/Xwayland"
if [[ "${XKBCOMP}" != "${SYSROOT}/usr/bin/xkbcomp" ]]; then
    copy_host_file "${XKBCOMP}" "${SYSROOT}/usr/bin/xkbcomp"
fi
if [[ "${XKB_ROOT}" != "${SYSROOT}/usr/share/X11" ]]; then
    rm -rf "${SYSROOT}/usr/share/X11"
    cp -aL "${XKB_ROOT}" "${SYSROOT}/usr/share/X11"
fi

if [[ "${XWAYLAND_SOURCE}" == "host" ]]; then
    copy_elf_runtime "${XWAYLAND}"
fi
if [[ "${XKBCOMP_SOURCE}" == "host" ]]; then
    copy_elf_runtime "${XKBCOMP}"
fi

note "staged Xwayland from ${XWAYLAND} (${XWAYLAND_SOURCE})"
note "staged xkbcomp from ${XKBCOMP} (${XKBCOMP_SOURCE})"
note "staged XKB data from ${XKB_ROOT} (${XKB_SOURCE})"
