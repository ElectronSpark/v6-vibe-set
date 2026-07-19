#!/usr/bin/env bash
# Stage locked KDE/host Xwayland runtime files into the generated sysroot.
set -euo pipefail

SYSROOT="${1:?usage: $0 <sysroot> <xwayland-wrapper> [runtime-root]}"
WRAPPER="${2:?usage: $0 <sysroot> <xwayland-wrapper> [runtime-root]}"
RUNTIME_ROOT="${3:-${XWAYLAND_RUNTIME_ROOT:-${SYSROOT}}}"
# Host binaries can overwrite sysroot libraries and make identical source trees
# produce different images, so keep that compatibility path explicitly opt-in.
ALLOW_HOST_FALLBACK="${XWAYLAND_ALLOW_HOST_FALLBACK:-0}"
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

find_root_tool() {
    local root="$1"
    local name="$2"
    local candidate

    [[ -n "${root}" ]] || return 0
    for candidate in \
        "${root}/bin/${name}.real" \
        "${root}/usr/bin/${name}.real" \
        "${root}/usr/bin/${name}" \
        "${root}/bin/${name}"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
}

select_runtime_tool() {
    local name="$1"
    local selected

    selected="$(find_root_tool "${RUNTIME_ROOT}" "${name}")"
    if [[ -n "${selected}" ]]; then
        printf '%s\truntime\n' "${selected}"
        return 0
    fi

    selected="$(find_root_tool "${SYSROOT}" "${name}")"
    if [[ -n "${selected}" ]]; then
        printf '%s\tsysroot\n' "${selected}"
        return 0
    fi

    if [[ "${ALLOW_HOST_FALLBACK}" == "1" ]]; then
        selected="$(find_host_tool "${name}")"
        if [[ -n "${selected}" ]]; then
            printf '%s\thost\n' "${selected}"
        fi
    fi
}

select_xkb_root() {
    local candidate
    local source

    for source in runtime sysroot; do
        if [[ "${source}" == "runtime" ]]; then
            candidate="${RUNTIME_ROOT}"
        else
            candidate="${SYSROOT}"
        fi
        [[ -n "${candidate}" ]] || continue
        for candidate in \
            "${candidate}/usr/share/X11" \
            "${candidate}/share/X11"; do
            if [[ -d "${candidate}/xkb" ]]; then
                printf '%s\t%s\n' "${candidate}" "${source}"
                return 0
            fi
        done
    done

    if [[ "${ALLOW_HOST_FALLBACK}" == "1" ]]; then
        candidate="${XKB_CONFIG_ROOT:-/usr/share/X11}"
        if [[ -d "${candidate}/xkb" ]]; then
            printf '%s\thost\n' "${candidate}"
        fi
    fi
}

copy_host_file() {
    local src="$1"
    local dst="$2"

    mkdir -p "$(dirname "${dst}")"
    cp -aL "${src}" "${dst}"
    chmod 0755 "${dst}" 2>/dev/null || true
}

help_has_option_token() {
    local option="$1"

    awk -v option="${option}" '
        {
            for (i = 1; i <= NF; i++) {
                if ($i == option)
                    found = 1
            }
        }
        END { exit found ? 0 : 1 }
    '
}

select_xwayland_wrapper() {
    local cc_bin="${CC:-cc}"
    local xwayland_bin="$1"
    local has_glamor_option=0
    local help_output

    if [[ -f "${KDE_WRAPPER_SRC}" ]]; then
        if ! command -v "${cc_bin}" >/dev/null 2>&1; then
            echo "stage-xwayland-runtime: error: ${cc_bin} not found for KDE Xwayland wrapper" >&2
            exit 1
        fi
        help_output="$("${xwayland_bin}" -help 2>&1 || true)"
        if help_has_option_token "-glamor" <<< "${help_output}"; then
            has_glamor_option=1
        fi
        mkdir -p "$(dirname "${KDE_WRAPPER_BIN}")"
        "${cc_bin}" -O2 -Wall -Wextra \
            -DXV6_XWAYLAND_HAS_GLAMOR_OPTION="${has_glamor_option}" \
            "${KDE_WRAPPER_SRC}" -o "${KDE_WRAPPER_BIN}"
        note "Xwayland -glamor option supported=${has_glamor_option}"
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

if [[ "${ALLOW_HOST_FALLBACK}" != "0" && "${ALLOW_HOST_FALLBACK}" != "1" ]]; then
    echo "stage-xwayland-runtime: error: XWAYLAND_ALLOW_HOST_FALLBACK must be 0 or 1" >&2
    exit 1
fi

IFS=$'\t' read -r XWAYLAND XWAYLAND_SOURCE \
    <<< "$(select_runtime_tool Xwayland)"
if [[ -z "${XWAYLAND}" ]]; then
    echo "stage-xwayland-runtime: error: Xwayland not found in runtime root or sysroot; set XWAYLAND_ALLOW_HOST_FALLBACK=1 to permit host fallback" >&2
    exit 1
fi

IFS=$'\t' read -r XKBCOMP XKBCOMP_SOURCE \
    <<< "$(select_runtime_tool xkbcomp)"
if [[ -z "${XKBCOMP}" ]]; then
    echo "stage-xwayland-runtime: error: xkbcomp not found in runtime root or sysroot; set XWAYLAND_ALLOW_HOST_FALLBACK=1 to permit host fallback" >&2
    exit 1
fi

IFS=$'\t' read -r XKB_ROOT XKB_SOURCE <<< "$(select_xkb_root)"
if [[ -z "${XKB_ROOT}" ]]; then
    echo "stage-xwayland-runtime: error: XKB data not found in runtime root or sysroot; set XWAYLAND_ALLOW_HOST_FALLBACK=1 to permit host fallback" >&2
    exit 1
fi

mkdir -p "${SYSROOT}/bin" "${SYSROOT}/usr/bin" "${SYSROOT}/usr/share" \
         "${SYSROOT}/lib" "${SYSROOT}/lib64"

if [[ "${XWAYLAND}" != "${SYSROOT}/bin/Xwayland.real" ]]; then
    copy_host_file "${XWAYLAND}" "${SYSROOT}/bin/Xwayland.real"
fi
XWAYLAND_WRAPPER_SELECTED="$(select_xwayland_wrapper "${XWAYLAND}")"
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
