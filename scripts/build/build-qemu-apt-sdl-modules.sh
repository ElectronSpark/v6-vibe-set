#!/usr/bin/env bash
# Build corrected SDL/OpenGL frontend modules for the installed APT QEMU.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${QEMU_SDL_VERSION:-9.0.2}"
EXPECTED_APT_VERSION="${QEMU_SDL_APT_VERSION:-1:9.0.2+ds-4ubuntu5.1~backport24.04.202411210348~ubuntu24.04.1}"
APT_BIN="${QEMU_SDL_APT_BIN:-/usr/bin/qemu-system-x86_64}"
APT_SYSTEM_MODULE_DIR="${QEMU_SDL_APT_SYSTEM_MODULE_DIR:-/usr/lib/x86_64-linux-gnu/qemu}"
APT_SDL_MODULE="${APT_SYSTEM_MODULE_DIR}/ui-sdl.so"
APT_OPENGL_MODULE="${APT_SYSTEM_MODULE_DIR}/ui-opengl.so"
QEMU_SDL_BUILD_ROOT="${QEMU_SDL_BUILD_ROOT:-${ROOT}/build-x86_64/qemu-sdl}"
DEVROOT="${QEMU_SDL_BUILD_ROOT}/devroot"
APT_SOURCE_VERSION="${EXPECTED_APT_VERSION#*:}"
APT_SOURCE_ROOT="${QEMU_APT_SOURCE_ROOT:-${ROOT}/build-x86_64/qemu-apt-source}"
APT_SOURCE_TARBALL="${APT_SOURCE_ROOT}/qemu_${APT_SOURCE_VERSION}.tar.xz"
APT_SOURCE_SHA256="7f8082e0e482f47c82d394b9ee4ba16b9c240264d17725ee8563c315025017b3"
APT_SOURCE_URL="https://ppa.launchpadcontent.net/canonical-server/server-backports/ubuntu/pool/main/q/qemu/qemu_${APT_SOURCE_VERSION}.tar.xz"
SOURCE="${APT_SOURCE_ROOT}/recipe"
ASPECT_PATCH="${ROOT}/patches/qemu-9.0.2-sdl-aspect-input.patch"
DISPLAY_ABI_PATCH="${ROOT}/patches/qemu-9.0.2-apt-display-abi.patch"
BUILD_ROOT="${QEMU_APT_SDL_MODULE_BUILD_ROOT:-${ROOT}/build-x86_64/qemu-apt-sdl-module-build}"
BUILD="${BUILD_ROOT}/build"
OUT="${QEMU_APT_SDL_MODULE_OUT:-${ROOT}/build-x86_64/qemu-apt-ui-corrected-modules}"
CONFIG_STAMP="${BUILD}/.xv6-apt-ui-module-v3"

module_stamp() {
        local module="$1"
        local stamps=()

        mapfile -t stamps < <(
                readelf --dyn-syms --wide "${module}" |
                        awk '$8 ~ /^qemu_stamp_[[:xdigit:]]+$/ { print $8 }'
        )
        if [[ ${#stamps[@]} -ne 1 ]]; then
                echo "build-qemu-apt-sdl-modules: expected one dynamic QEMU stamp in ${module}, found ${#stamps[@]}" >&2
                exit 2
        fi
        printf '%s\n' "${stamps[0]}"
}

module_display_type() {
        local module="$1"
        local symbol_hex data_address_hex data_offset_hex file_offset

        symbol_hex="$(readelf --symbols --wide "${module}" |
                awk '$8 == "qemu_display_sdl2" { print $2; exit }')"
        read -r data_address_hex data_offset_hex < <(
                readelf --section-headers --wide "${module}" |
                        awk '$2 == ".data" { print $4, $5; exit }'
        )
        if [[ -z "${symbol_hex}" || -z "${data_address_hex}" ||
              -z "${data_offset_hex}" ]]; then
                echo "build-qemu-apt-sdl-modules: cannot locate SDL display ABI data in ${module}" >&2
                exit 2
        fi
        file_offset=$((16#${data_offset_hex} + 16#${symbol_hex} - 16#${data_address_hex}))
        od -An -tu4 -N4 -j "${file_offset}" "${module}" | tr -d '[:space:]'
}

[[ "${VERSION}" == "9.0.2" ]] || {
        echo "build-qemu-apt-sdl-modules: unsupported QEMU_SDL_VERSION=${VERSION}" >&2
        exit 2
}
[[ -x "${APT_BIN}" ]] || {
        echo "build-qemu-apt-sdl-modules: APT QEMU is missing: ${APT_BIN}" >&2
        exit 2
}
apt_package_version="$(dpkg-query -W -f='${Version}' qemu-system-x86 2>/dev/null || true)"
if [[ "${apt_package_version}" != "${EXPECTED_APT_VERSION}" ]]; then
        echo "build-qemu-apt-sdl-modules: installed qemu-system-x86 ${apt_package_version:-unknown} does not match pinned package ${EXPECTED_APT_VERSION}" >&2
        exit 2
fi
for module in "${APT_SDL_MODULE}" "${APT_OPENGL_MODULE}"; do
        [[ -f "${module}" && ! -L "${module}" ]] || {
                echo "build-qemu-apt-sdl-modules: required APT module is missing or a symlink: ${module}" >&2
                exit 2
        }
done

apt_stamp="$(module_stamp "${APT_SDL_MODULE}")"
if [[ "$(module_stamp "${APT_OPENGL_MODULE}")" != "${apt_stamp}" ]]; then
        echo "build-qemu-apt-sdl-modules: APT SDL/OpenGL module stamps disagree" >&2
        exit 2
fi
apt_hash="${apt_stamp#qemu_stamp_}"

# Reuse the pinned download, patch, and private development sysroot without
# compiling the standalone QEMU executable.  This keeps the workaround on the
# system /usr/bin/qemu-system-x86_64 and supplies one internally consistent
# SDL/OpenGL module pair without replacing any packaged file.
QEMU_SDL_BUILD_ROOT="${QEMU_SDL_BUILD_ROOT}" \
QEMU_SDL_PREPARE_ONLY=1 \
        "${ROOT}/scripts/build/build-qemu-sdl.sh" >/dev/null

# Build against the exact Canonical backport source package.  QEMU's generated
# DisplayType enum is feature-conditional: the APT executable includes GTK
# before SDL, while this focused module build intentionally omits GTK.  The
# module-only ABI patch reserves that GTK enum slot so SDL remains type 3,
# matching the installed executable without building or replacing GTK.
mkdir -p -- "${APT_SOURCE_ROOT}"
if [[ ! -f "${APT_SOURCE_TARBALL}" ]]; then
        curl -L --fail --show-error \
                --output "${APT_SOURCE_TARBALL}.tmp" "${APT_SOURCE_URL}"
        mv -- "${APT_SOURCE_TARBALL}.tmp" "${APT_SOURCE_TARBALL}"
fi
printf '%s  %s\n' "${APT_SOURCE_SHA256}" "${APT_SOURCE_TARBALL}" |
        sha256sum -c - >/dev/null
if [[ ! -d "${SOURCE}" ]]; then
        tar -xJf "${APT_SOURCE_TARBALL}" -C "${APT_SOURCE_ROOT}"
fi
patch_set_hash="$({ sha256sum "${ASPECT_PATCH}" "${DISPLAY_ABI_PATCH}"; } |
        sha256sum | awk '{ print $1 }')"
patch_marker="${SOURCE}/.xv6-apt-sdl-patches-${patch_set_hash}"
if [[ ! -f "${patch_marker}" ]]; then
        if patch --dry-run --forward -d "${SOURCE}" -p1 <"${ASPECT_PATCH}" >/dev/null &&
           patch --dry-run --forward -d "${SOURCE}" -p1 <"${DISPLAY_ABI_PATCH}" >/dev/null; then
                patch --forward -d "${SOURCE}" -p1 <"${ASPECT_PATCH}"
                patch --forward -d "${SOURCE}" -p1 <"${DISPLAY_ABI_PATCH}"
        elif patch --dry-run --reverse -d "${SOURCE}" -p1 <"${ASPECT_PATCH}" >/dev/null &&
             patch --dry-run --reverse -d "${SOURCE}" -p1 <"${DISPLAY_ABI_PATCH}" >/dev/null; then
                :
        else
                echo "build-qemu-apt-sdl-modules: APT source has a stale or partial patch set: ${SOURCE}" >&2
                echo "build-qemu-apt-sdl-modules: remove only that generated source directory and retry" >&2
                exit 2
        fi
        find "${SOURCE}" -maxdepth 1 -type f \
                -name '.xv6-apt-sdl-patches-*' -delete
        touch "${patch_marker}"
fi

mkdir -p -- "${BUILD_ROOT}" "${BUILD}"
if [[ ! -f "${BUILD}/build.ninja" || ! -f "${CONFIG_STAMP}" ]]; then
        (
                cd "${BUILD}"
                export PKG_CONFIG_PATH="${DEVROOT}/usr/lib/x86_64-linux-gnu/pkgconfig:${DEVROOT}/usr/share/pkgconfig"
                export PKG_CONFIG_SYSROOT_DIR="${DEVROOT}"
                export CFLAGS="-I${DEVROOT}/usr/include/x86_64-linux-gnu"
                export LDFLAGS="-L/usr/lib/x86_64-linux-gnu"
                "${SOURCE}/configure" \
                        --target-list=x86_64-softmmu \
                        --prefix=/usr \
                        --libdir=/usr/lib/x86_64-linux-gnu \
                        --enable-kvm \
                        --enable-opengl \
                        --enable-sdl \
                        --enable-modules \
                        --enable-module-upgrades \
                        --enable-slirp \
                        --enable-virglrenderer \
                        --disable-gtk \
                        --disable-xkbcommon \
                        --disable-relocatable \
                        --disable-install-blobs \
                        --disable-fuse \
                        --disable-fuse-lseek \
                        --disable-werror
        )
        touch "${CONFIG_STAMP}"
fi

# QEMU deliberately gives every build a unique module symbol.  Recompile both
# frontend modules with the installed package's symbol so its loader accepts
# them as a pair.
# A package update changes that symbol and therefore fails closed until this
# script is rerun; no system file is modified.
desired_config_stamp="#define CONFIG_STAMP _${apt_hash}"
current_config_stamp="$(sed -n '/^#define CONFIG_STAMP _/p' "${BUILD}/config-host.h")"
if [[ "${current_config_stamp}" != "${desired_config_stamp}" ]]; then
        sed -i -E \
                "s/^#define CONFIG_STAMP _.*/${desired_config_stamp}/" \
                "${BUILD}/config-host.h"
fi
ninja -C "${BUILD}" ui-sdl.so ui-opengl.so

mkdir -p -- "${OUT}"
for module in ui-sdl.so ui-opengl.so; do
        if [[ -L "${OUT}/${module}" ]]; then
                echo "build-qemu-apt-sdl-modules: refusing symlink in output directory: ${OUT}/${module}" >&2
                exit 2
        fi
done

temporary_sdl="$(mktemp "${OUT}/.ui-sdl.so.XXXXXX")"
temporary_opengl="$(mktemp "${OUT}/.ui-opengl.so.XXXXXX")"
cleanup_temporary_modules() {
        local temporary

        for temporary in "${temporary_sdl}" "${temporary_opengl}"; do
                [[ -n "${temporary}" && -f "${temporary}" ]] &&
                        find "${temporary}" -maxdepth 0 -type f -delete
        done
}
trap cleanup_temporary_modules EXIT
cp -- "${BUILD}/ui-sdl.so" "${temporary_sdl}"
cp -- "${BUILD}/ui-opengl.so" "${temporary_opengl}"
chmod 0755 "${temporary_sdl}" "${temporary_opengl}"
[[ "$(module_stamp "${temporary_sdl}")" == "${apt_stamp}" ]] || {
        echo "build-qemu-apt-sdl-modules: generated SDL module stamp mismatch" >&2
        exit 2
}
[[ "$(module_stamp "${temporary_opengl}")" == "${apt_stamp}" ]] || {
        echo "build-qemu-apt-sdl-modules: generated OpenGL module stamp mismatch" >&2
        exit 2
}
if [[ "$(module_display_type "${temporary_sdl}")" != "3" ]]; then
        echo "build-qemu-apt-sdl-modules: generated SDL module does not match APT DisplayType ABI value 3" >&2
        exit 2
fi
if readelf --dyn-syms --wide "${temporary_sdl}" |
        awk '$7 == "UND" && $8 == "egl_fb_blit_rect" { found = 1 } END { exit !found }'; then
        echo "build-qemu-apt-sdl-modules: generated SDL module has a private cross-module egl_fb_blit_rect dependency" >&2
        exit 2
fi
if ! readelf -d "${temporary_sdl}" |
        awk '/Shared library: \[libepoxy\.so\.0\]/ { found = 1 } END { exit !found }'; then
        echo "build-qemu-apt-sdl-modules: generated SDL module is missing its direct libepoxy dependency" >&2
        exit 2
fi
if ! readelf -d "${temporary_opengl}" |
        awk '/Shared library: \[libepoxy\.so\.0\]/ { found = 1 } END { exit !found }'; then
        echo "build-qemu-apt-sdl-modules: generated OpenGL module is missing its direct libepoxy dependency" >&2
        exit 2
fi
# Publish by rename so a running, already-owned QEMU keeps its old mapped
# inode; never truncate a module that another process loaded.
mv -- "${temporary_opengl}" "${OUT}/ui-opengl.so"
temporary_opengl=""
mv -- "${temporary_sdl}" "${OUT}/ui-sdl.so"
temporary_sdl=""
trap - EXIT

printf 'qemu_apt_binary=%s\n' "${APT_BIN}"
printf 'qemu_apt_module_stamp=%s\n' "${apt_stamp}"
printf 'qemu_apt_corrected_sdl_module=%s\n' "${OUT}/ui-sdl.so"
printf 'qemu_apt_corrected_opengl_module=%s\n' "${OUT}/ui-opengl.so"
