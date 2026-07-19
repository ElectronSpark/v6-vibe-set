#!/usr/bin/env bash
# Stage host-package GUI runtime controls into a generated rootfs overlay.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
OVERLAY="${1:?usage: $0 <overlay> <sysroot>}"
SYSROOT="${2:?usage: $0 <overlay> <sysroot>}"
BUILD_DIR="${HOST_GUI_BUILD_DIR:-${REPO_ROOT}/build-x86_64/host-gui-runtime}"
CC_BIN="${CC:-cc}"

mkdir -p "${OVERLAY}/bin" "${OVERLAY}/opt/host-gui" "${BUILD_DIR}"

note() {
    echo "stage-host-gui-runtime: $*" >&2
}

has_pkg_config() {
    pkg-config --exists "$@" >/dev/null 2>&1
}

can_link_xcb_dri3_present() {
    local out="${BUILD_DIR}/.check-xcb-dri3-present"

    has_pkg_config xcb || return 1
    # shellcheck disable=SC2046
    if printf 'int main(void){return 0;}\n' |
        "${CC_BIN}" -x c - -o "${out}" \
            $(pkg-config --cflags --libs xcb) \
            -Wl,-l:libxcb-dri3.so.0 -Wl,-l:libxcb-present.so.0 \
            >/dev/null 2>&1; then
        rm -f "${out}"
        return 0
    fi
    rm -f "${out}"
    return 1
}

stage_x11_present_trace_preload() {
    local out="${BUILD_DIR}/host-x11-present-trace-preload.so"
    local dst="${OVERLAY}/opt/host-gui/host-x11-egl-smoke/lib/host-x11-present-trace-preload.so"

    if ! has_pkg_config xcb; then
        note "warning: xcb development files not found; host X11 Present trace preload not staged"
        return 0
    fi

    # shellcheck disable=SC2046
    if "${CC_BIN}" -O2 -Wall -Wextra -fPIC -shared \
        $(pkg-config --cflags xcb) \
        -o "${out}" \
        "${REPO_ROOT}/scripts/image/host-x11-present-trace-preload.c" \
        -ldl -pthread >/dev/null 2>&1; then
        mkdir -p "$(dirname "${dst}")"
        cp -aL "${out}" "${dst}"
        chmod 0755 "${dst}" 2>/dev/null || true
        note "staged host X11 Present trace preload at ${dst#${OVERLAY}}"
    else
        note "warning: failed to build host X11 Present trace preload; not staged"
    fi
}

stage_chromium_egl_trace_preload() {
    local out="${BUILD_DIR}/chromium-egl-trace-preload.so"
    local dst="${OVERLAY}/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so"

    if ! has_pkg_config egl; then
        note "warning: EGL development files not found; Chromium EGL trace preload not staged"
        return 0
    fi

    # shellcheck disable=SC2046
    if "${CC_BIN}" -O2 -Wall -Wextra -fPIC -shared \
        $(pkg-config --cflags egl) \
        -o "${out}" \
        "${REPO_ROOT}/scripts/image/chromium-egl-trace-preload.c" \
        -ldl -pthread >/dev/null 2>&1; then
        mkdir -p "$(dirname "${dst}")"
        cp -aL "${out}" "${dst}"
        chmod 0755 "${dst}" 2>/dev/null || true
        note "staged Chromium EGL trace preload at ${dst#${OVERLAY}}"
    else
        note "warning: failed to build Chromium EGL trace preload; not staged"
    fi
}

stage_chromium_pulse_trace_preload() {
    local out="${BUILD_DIR}/chromium-pulse-trace-preload.so"
    local dst="${OVERLAY}/opt/host-gui/wayland-chromium/lib/chromium-pulse-trace-preload.so"

    if "${CC_BIN}" -O2 -Wall -Wextra -Werror -fPIC -shared \
        -o "${out}" \
        "${REPO_ROOT}/scripts/image/chromium-pulse-trace-preload.c" \
        -ldl -pthread >/dev/null 2>&1; then
        mkdir -p "$(dirname "${dst}")"
        cp -aL "${out}" "${dst}"
        chmod 0755 "${dst}" 2>/dev/null || true
        note "staged default-off Chromium Pulse trace preload at ${dst#${OVERLAY}}"
    else
        note "failed to build required default-off Chromium Pulse trace preload"
        return 1
    fi
}

stage_chromium_host_atk() {
    local chrome="${OVERLAY}/opt/host-gui/wayland-chromium/chrome-linux64/chrome"
    local host_atk
    local dst="${OVERLAY}/opt/host-gui/wayland-chromium/lib/libatk-1.0.so.0"

    [[ -x "${chrome}" ]] || {
        note "Chromium binary missing while staging its host ATK runtime"
        return 1
    }
    host_atk="$(
        ldd "${chrome}" 2>/dev/null |
        awk '$1 == "libatk-1.0.so.0" && $2 == "=>" { print $3; exit }'
    )"
    if [[ -z "${host_atk}" || ! -e "${host_atk}" ]]; then
        note "Chromium host libatk-1.0.so.0 dependency is unresolved"
        return 1
    fi

    # Chromium and libatk-bridge come from the host distribution, while xv6's
    # GTK port uses the older ATK 2.38 ABI.  Keep the imported browser on its
    # matching host ATK without moving host Mesa/DRM/Wayland ahead of the
    # guest graphics stack.  GTK must remain dynamically linked to ATK so this
    # single SONAME instance serves both callers in the Chromium process.
    stage_host_file "${host_atk}" "${dst}"
    note "staged Chromium host ATK runtime from ${host_atk}"
}

stage_host_file() {
    local src="$1"
    local dst="$2"

    [[ -e "${src}" ]] || return 0
    mkdir -p "$(dirname "${dst}")"
    cp -aL "${src}" "${dst}"
    chmod 0755 "${dst}" 2>/dev/null || true
}

is_guest_graphics_lib() {
    case "$(basename "$1")" in
        libEGL.so*|libGL.so*|libGLX.so*|libGLdispatch.so*|libGLES*.so*|\
        libOpenGL.so*|libgbm.so*|libdrm.so*|libdrm_*.so*|libwayland-*.so*|\
        libweston-*.so*)
            return 0
            ;;
    esac
    return 1
}

bundle_elf() {
    local id="$1"
    local exe="$2"
    local launcher_src="$3"
    local app_root="${OVERLAY}/opt/host-gui/${id}"
    local manifest="${app_root}/manifest.tsv"
    local interp lib

    rm -rf "${app_root}"
    mkdir -p "${app_root}/bin" "${app_root}/lib"
    printf 'kind\thost_path\tguest_path\n' > "${manifest}"

    cp -aL "${exe}" "${app_root}/bin/${id}"
    printf 'executable\t%s\t/opt/host-gui/%s/bin/%s\n' "${exe}" "${id}" "${id}" >> "${manifest}"

    interp="$(readelf -l "${exe}" |
        sed -nE 's/.*Requesting program interpreter: ([^]]+)\].*/\1/p' |
        head -1)"
    if [[ -n "${interp}" && -e "${interp}" ]]; then
        cp -aL "${interp}" "${app_root}/lib/$(basename "${interp}")"
        printf 'interpreter\t%s\t/opt/host-gui/%s/lib/%s\n' \
            "${interp}" "${id}" "$(basename "${interp}")" >> "${manifest}"
    fi

    while IFS= read -r lib; do
        [[ -n "${lib}" && -e "${lib}" ]] || continue
        if is_guest_graphics_lib "${lib}"; then
            printf 'skipped-guest-runtime\t%s\tguest-provided\n' "${lib}" >> "${manifest}"
            continue
        fi
        cp -aL "${lib}" "${app_root}/lib/$(basename "${lib}")"
        printf 'library\t%s\t/opt/host-gui/%s/lib/%s\n' \
            "${lib}" "${id}" "$(basename "${lib}")" >> "${manifest}"
    done < <(
        ldd "${exe}" 2>/dev/null |
        awk '/=> \// { print $3; next } /^[[:space:]]*\// { print $1; next }'
    )

    "${CC_BIN}" -O2 -Wall -Wextra -o "${OVERLAY}/bin/${id}" "${launcher_src}"
    printf 'launcher\t%s\t/bin/%s\n' "${launcher_src}" "${id}" >> "${manifest}"
}

build_simple() {
    local id="$1"
    local src="$2"
    local launcher="$3"
    shift 3
    local out="${BUILD_DIR}/${id}"

    "${CC_BIN}" -O2 -Wall -Wextra -o "${out}" "${src}" "$@"
    bundle_elf "${id}" "${out}" "${launcher}"
}

stage_dbus() {
    if command -v dbus-daemon >/dev/null 2>&1; then
        stage_host_file "$(command -v dbus-daemon)" "${OVERLAY}/bin/dbus-daemon-host"
        note "staged dbus-daemon-host from $(command -v dbus-daemon)"
    else
        note "warning: dbus-daemon not found; /bin/dbus-daemon-host not staged"
    fi
}

stage_c_probes() {
    build_simple chromium-pulse-stream-reducer \
        "${REPO_ROOT}/scripts/image/chromium-pulse-stream-reducer.c" \
        "${REPO_ROOT}/scripts/image/chromium-pulse-stream-reducer-launcher.c" \
        -ldl -pthread

    if has_pkg_config gtk+-3.0; then
        # shellcheck disable=SC2046
        build_simple host-gtk-smoke \
            "${REPO_ROOT}/scripts/image/host-gtk-smoke.c" \
            "${REPO_ROOT}/scripts/image/host-gtk-smoke-launcher.c" \
            $(pkg-config --libs gtk+-3.0)
    else
        note "warning: gtk+-3.0 development files not found; host-gtk-smoke not staged"
    fi

    if has_pkg_config xcb; then
        # shellcheck disable=SC2046
        build_simple host-x11-abi-smoke \
            "${REPO_ROOT}/scripts/image/host-x11-abi-smoke.c" \
            "${REPO_ROOT}/scripts/image/host-x11-abi-smoke-launcher.c" \
            $(pkg-config --cflags --libs xcb)
    else
        note "warning: xcb development files not found; X11 XCB probe not staged"
    fi

    if can_link_xcb_dri3_present; then
        # shellcheck disable=SC2046
        build_simple host-x11-dri3-present-smoke \
            "${REPO_ROOT}/scripts/image/host-x11-dri3-present-smoke.c" \
            "${REPO_ROOT}/scripts/image/host-x11-dri3-present-smoke-launcher.c" \
            $(pkg-config --cflags --libs xcb) \
            -Wl,-l:libxcb-dri3.so.0 -Wl,-l:libxcb-present.so.0
    else
        note "warning: xcb development files or linkable libxcb-dri3.so.0/libxcb-present.so.0 runtime libraries not found; X11 DRI3/Present probe not staged"
    fi

    if has_pkg_config x11 xext; then
        # shellcheck disable=SC2046
        build_simple host-x11-shm-smoke \
            "${REPO_ROOT}/scripts/image/host-x11-shm-smoke.c" \
            "${REPO_ROOT}/scripts/image/host-x11-shm-smoke-launcher.c" \
            $(pkg-config --cflags --libs x11 xext)
    fi

	if has_pkg_config x11 egl glesv2 gl; then
		# shellcheck disable=SC2046
		build_simple host-x11-egl-smoke \
			"${REPO_ROOT}/scripts/image/host-x11-egl-smoke.c" \
			"${REPO_ROOT}/scripts/image/host-x11-egl-smoke-launcher.c" \
			$(pkg-config --cflags --libs x11 egl glesv2 gl)
		stage_x11_present_trace_preload
	fi

	local mesa_build_dir="${SYSROOT%/}/../ports/mesa-build"
	local gbm_pc="${SYSROOT}/lib/pkgconfig/gbm.pc"
	local gbm_lib="${mesa_build_dir}/src/gbm/libgbm.so.1.0.0"
	local egl_lib="${mesa_build_dir}/src/egl/libEGL.so.1.0.0"
	local gles_lib="${SYSROOT}/lib/libGLESv2.so.2.0.0"
	if PKG_CONFIG_PATH= \
	   PKG_CONFIG_LIBDIR="${SYSROOT}/lib/pkgconfig" \
	   PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
	   pkg-config --exists egl glesv2 gbm gl >/dev/null 2>&1; then
		if [[ -e "${gbm_pc}" && -e "${gbm_lib}" && -e "${egl_lib}" &&
		      -e "${gles_lib}" ]]; then
			if [[ ! -e "${SYSROOT}/lib/libEGL.so.1.0.0" ]]; then
				cp -aL "${egl_lib}" "${SYSROOT}/lib/libEGL.so.1.0.0"
				ln -sfn libEGL.so.1.0.0 "${SYSROOT}/lib/libEGL.so.1"
				ln -sfn libEGL.so.1 "${SYSROOT}/lib/libEGL.so"
			fi
			# shellcheck disable=SC2046
			build_simple host-egl-gbm-gl-smoke \
				"${REPO_ROOT}/scripts/image/host-egl-gbm-gl-smoke.c" \
				"${REPO_ROOT}/scripts/image/host-egl-gbm-gl-smoke-launcher.c" \
				$(PKG_CONFIG_PATH= \
				  PKG_CONFIG_LIBDIR="${SYSROOT}/lib/pkgconfig" \
				  PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
				  pkg-config --cflags --libs egl glesv2 gbm gl) \
				-Wl,--allow-shlib-undefined
		else
			note "warning: Mesa EGL/GLES/GBM runtime files incomplete; host EGL/GBM reducer not staged"
		fi
	else
		note "warning: EGL/GLES/GBM development files not found; host EGL/GBM reducer not staged"
	fi
}

stage_wayland_probes() {
    local xdg_xml="${REPO_ROOT}/ports/wayland-protocols/src/stable/xdg-shell/xdg-shell.xml"
    local xdg_c="${BUILD_DIR}/xdg-shell-protocol.c"
    local xdg_h="${BUILD_DIR}/xdg-shell-client-protocol.h"

    if ! has_pkg_config wayland-client wayland-egl egl glesv2; then
        note "warning: Wayland/EGL development files not found; host Wayland probes not staged"
        return 0
    fi
    if ! command -v wayland-scanner >/dev/null 2>&1; then
        note "warning: wayland-scanner not found; host Wayland probes not staged"
        return 0
    fi

    wayland-scanner private-code "${xdg_xml}" "${xdg_c}"
    wayland-scanner client-header "${xdg_xml}" "${xdg_h}"

    # shellcheck disable=SC2046
    "${CC_BIN}" -O2 -Wall -Wextra -I"${BUILD_DIR}" \
        $(pkg-config --cflags wayland-client wayland-egl egl glesv2) \
        -o "${BUILD_DIR}/host-wlegl-smoke" \
        "${REPO_ROOT}/scripts/image/host-wlegl-smoke.c" "${xdg_c}" \
        $(pkg-config --libs wayland-client wayland-egl egl glesv2)
    bundle_elf host-wlegl-smoke "${BUILD_DIR}/host-wlegl-smoke" \
        "${REPO_ROOT}/scripts/image/host-wlegl-smoke-launcher.c"

    if [[ -f "${REPO_ROOT}/config-temp/host-python-repl/host-python-repl.c" ]] &&
       command -v python3-config >/dev/null 2>&1; then
        # shellcheck disable=SC2046
        "${CC_BIN}" -O2 -Wall -Wextra -I"${BUILD_DIR}" \
            -I"${REPO_ROOT}/ports/wayland/src" \
            $(pkg-config --cflags wayland-client) \
            $(python3-config --includes) \
            -o "${BUILD_DIR}/host-python-repl" \
            "${REPO_ROOT}/config-temp/host-python-repl/host-python-repl.c" \
            "${REPO_ROOT}/ports/wayland/src/xv6_present_buffer.c" \
            "${REPO_ROOT}/ports/wayland/src/xv6_draw.c" "${xdg_c}" \
            $(pkg-config --libs wayland-client) $(python3-config --embed --ldflags)
        bundle_elf host-python-repl "${BUILD_DIR}/host-python-repl" \
            "${REPO_ROOT}/config-temp/host-python-repl/host-python-repl-launcher.c"
    fi
}

stage_idle_if_available() {
    local idle_src="${HOST_IDLE_BINARY:-}"

    if [[ -z "${idle_src}" || ! -x "${idle_src}" ]]; then
        note "warning: HOST_IDLE_BINARY not set; host-idle-x11 not staged"
        return 0
    fi
    rm -rf "${OVERLAY}/opt/host-gui/host-idle"
    mkdir -p "${OVERLAY}/opt/host-gui/host-idle"
    cp -aL "${idle_src}" "${OVERLAY}/opt/host-gui/host-idle/host-idle"
    "${CC_BIN}" -O2 -Wall -Wextra -o "${OVERLAY}/bin/host-idle-x11" \
        "${REPO_ROOT}/scripts/image/host-idle-x11-launcher.c"
}

chrome_for_testing_url() {
    local channel="${CHROME_FOR_TESTING_CHANNEL:-Stable}"
    local platform="${CHROME_FOR_TESTING_PLATFORM:-linux64}"
    local json_url="${CHROME_FOR_TESTING_JSON_URL:-https://googlechromelabs.github.io/chrome-for-testing/last-known-good-versions-with-downloads.json}"

    if [[ -n "${CHROME_FOR_TESTING_URL:-}" ]]; then
        printf '%s\n' "${CHROME_FOR_TESTING_URL}"
        return 0
    fi

    python3 - "${json_url}" "${channel}" "${platform}" <<'PY'
import json
import sys
from urllib.request import urlopen

json_url, channel, platform = sys.argv[1:]
with urlopen(json_url, timeout=30) as response:
    data = json.load(response)
downloads = data["channels"][channel]["downloads"]["chrome"]
for item in downloads:
    if item.get("platform") == platform:
        print(item["url"])
        break
else:
    raise SystemExit(f"no chrome-for-testing download for {channel}/{platform}")
PY
}

download_file() {
    local url="$1"
    local out="$2"
    local tmp="${out}.tmp"

    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --retry-delay 2 -o "${tmp}" "${url}"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "${tmp}" "${url}"
    else
        echo "stage-host-gui-runtime: curl or wget is required to download Chromium" >&2
        exit 1
    fi
    mv "${tmp}" "${out}"
}

prune_chromium_bundled_gl_stack() {
    local chrome_runtime="$1"
    local lib
    local target
    local disabled

    if [[ "${XV6_KEEP_CHROMIUM_BUNDLED_EGL:-0}" == "1" ]]; then
        note "keeping Chromium bundled EGL/GLES libraries by request"
        return 0
    fi

    for lib in libEGL.so libGLESv2.so; do
        if [[ ! -e "${chrome_runtime}/${lib}" ]]; then
            continue
        fi
        disabled="${chrome_runtime}/${lib}.xv6-disabled"
        rm -f "${disabled}"
        mv "${chrome_runtime}/${lib}" "${disabled}"
        case "${lib}" in
            libEGL.so)
                target="/lib/libEGL.so"
                ;;
            libGLESv2.so)
                target="/lib/libGLESv2.so"
                ;;
            *)
                echo "stage-host-gui-runtime: unexpected Chromium GL library ${lib}" >&2
                exit 1
                ;;
        esac
        ln -s "${target}" "${chrome_runtime}/${lib}"
        note "disabled Chromium bundled ${lib}; ${chrome_runtime}/${lib#${chrome_runtime}/} links to guest Mesa ${target}"
    done
}

stage_chromium_for_testing() {
    local cache_root="${BUILD_DIR}/wayland-chromium"
    local chrome_dir="${cache_root}/chrome-linux64"
    local chrome_bin="${chrome_dir}/chrome"
    local zip="${CHROME_FOR_TESTING_ARCHIVE:-${cache_root}/chrome-linux64.zip}"
    local expected_sha="${CHROME_FOR_TESTING_SHA256:-}"
    local extract="${cache_root}/extract"
    local url
    local actual_sha
    local app_root="${OVERLAY}/opt/host-gui/wayland-chromium"

    if [[ -n "${expected_sha}" && ! "${expected_sha}" =~ ^[0-9a-f]{64}$ ]]; then
        echo "stage-host-gui-runtime: invalid CHROME_FOR_TESTING_SHA256" >&2
        exit 1
    fi

    if [[ ! -x "${chrome_bin}" ]]; then
        command -v python3 >/dev/null 2>&1 || {
            echo "stage-host-gui-runtime: python3 is required to resolve Chrome for Testing downloads" >&2
            exit 1
        }
        command -v unzip >/dev/null 2>&1 || {
            echo "stage-host-gui-runtime: unzip is required to extract Chrome for Testing" >&2
            exit 1
        }
        mkdir -p "${cache_root}" "$(dirname "${zip}")"
        if [[ ! -f "${zip}" ]]; then
            url="$(chrome_for_testing_url)"
            note "downloading Chrome for Testing from ${url}"
            download_file "${url}" "${zip}"
        fi
        if [[ -n "${expected_sha}" ]]; then
            actual_sha="$(sha256sum "${zip}" | awk '{print $1}')"
            if [[ "${actual_sha}" != "${expected_sha}" ]]; then
                echo "stage-host-gui-runtime: Chrome archive sha256 mismatch" >&2
                echo "  expected ${expected_sha}" >&2
                echo "  actual   ${actual_sha}" >&2
                exit 1
            fi
        fi
        rm -rf "${extract}" "${chrome_dir}"
        mkdir -p "${extract}"
        unzip -q "${zip}" -d "${extract}"
        if [[ ! -x "${extract}/chrome-linux64/chrome" ]]; then
            echo "stage-host-gui-runtime: Chrome for Testing archive did not contain chrome-linux64/chrome" >&2
            exit 1
        fi
        mv "${extract}/chrome-linux64" "${chrome_dir}"
        rm -rf "${extract}"
    fi

    if [[ -n "${expected_sha}" ]]; then
        [[ -f "${zip}" ]] || {
            echo "stage-host-gui-runtime: locked Chrome archive is missing: ${zip}" >&2
            exit 1
        }
        actual_sha="$(sha256sum "${zip}" | awk '{print $1}')"
        [[ "${actual_sha}" == "${expected_sha}" ]] || {
            echo "stage-host-gui-runtime: locked Chrome archive changed after extraction" >&2
            exit 1
        }
    fi

    rm -rf "${app_root}/chrome-linux64"
    mkdir -p "${app_root}"
    rsync -aH --delete "${chrome_dir}/" "${app_root}/chrome-linux64/"
    prune_chromium_bundled_gl_stack "${app_root}/chrome-linux64"
    note "staged wayland-chromium from ${chrome_bin}"
}

stage_dbus
stage_c_probes
stage_wayland_probes
stage_idle_if_available
stage_chromium_egl_trace_preload
stage_chromium_pulse_trace_preload
stage_chromium_for_testing
stage_chromium_host_atk

note "overlay=${OVERLAY}"
