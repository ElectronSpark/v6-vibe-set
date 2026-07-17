#!/usr/bin/env bash
# Build the version-pinned QEMU SDL frontend with aspect-correct GL and input.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${QEMU_SDL_VERSION:-9.0.2}"
BUILD_ROOT="${QEMU_SDL_BUILD_ROOT:-${ROOT}/build-x86_64/qemu-sdl}"
SOURCE="${BUILD_ROOT}/qemu-${VERSION}"
TARBALL="${BUILD_ROOT}/qemu-${VERSION}.tar.xz"
DEVPKGS="${BUILD_ROOT}/devpkgs"
DEVROOT="${BUILD_ROOT}/devroot"
URL="https://download.qemu.org/qemu-${VERSION}.tar.xz"
PATCH="${ROOT}/patches/qemu-9.0.2-sdl-aspect-input.patch"
BUILD="${SOURCE}/build"
DATA_DIR="${BUILD_ROOT}/share/qemu"
CONFIG_STAMP="${BUILD}/.xv6-sdl-config-v2"
USE_SYSTEM_DEPS="${QEMU_SDL_USE_SYSTEM_DEPS:-0}"
PATCH_SHA="$(sha256sum "${PATCH}" | awk '{print $1}')"
PATCH_STAMP="${SOURCE}/.xv6-sdl-aspect-patched"

if [[ "${VERSION}" != "9.0.2" ]]; then
        echo "build-qemu-sdl: unsupported QEMU_SDL_VERSION=${VERSION}" >&2
        exit 2
fi
mkdir -p -- "${BUILD_ROOT}"
if [[ ! -f "${TARBALL}" ]]; then
        curl -L --fail --show-error --output "${TARBALL}.tmp" "${URL}"
        mv -- "${TARBALL}.tmp" "${TARBALL}"
fi
sha256sum "${TARBALL}" >"${BUILD_ROOT}/qemu-${VERSION}.tar.xz.sha256"
if [[ -d "${SOURCE}" && "$(cat "${PATCH_STAMP}" 2>/dev/null || true)" != "${PATCH_SHA}" ]]; then
        echo "build-qemu-sdl: patch changed; recreating generated QEMU source" >&2
        rm -rf -- "${SOURCE}"
fi
if [[ ! -d "${SOURCE}" ]]; then
        tar -C "${BUILD_ROOT}" -xf "${TARBALL}"
fi
if [[ "$(cat "${PATCH_STAMP}" 2>/dev/null || true)" != "${PATCH_SHA}" ]]; then
        patch -d "${SOURCE}" -p1 --forward <"${PATCH}"
        printf '%s\n' "${PATCH_SHA}" >"${PATCH_STAMP}"
fi

# Keep the build independent of host development-package installation.  The
# runtime libraries are already present on the supported Ubuntu/WSL host; the
# matching headers, pkg-config metadata, and linker symlinks live only in this
# generated private sysroot.
if [[ "${USE_SYSTEM_DEPS}" != "1" ]]; then
        mkdir -p -- "${DEVPKGS}" "${DEVROOT}"
        dev_packages=(
                libglib2.0-dev libpixman-1-dev libsdl2-dev libepoxy-dev libgbm-dev
                libvirglrenderer-dev libglib2.0-0t64 libpixman-1-0 libsdl2-2.0-0
                libepoxy0 libgbm1 libvirglrenderer1 libslirp-dev libslirp0
        )
        for package in "${dev_packages[@]}"; do
                if ! compgen -G "${DEVPKGS}/${package}_*.deb" >/dev/null; then
                        (cd "${DEVPKGS}" && apt-get download "${package}")
                fi
        done
        for deb in "${DEVPKGS}"/*.deb; do
                dpkg-deb -x "${deb}" "${DEVROOT}"
        done
        # QEMU asks Meson for static dependencies where available.  The
        # extracted development packages include static archives whose
        # transitive build dependencies are intentionally absent.
        find "${DEVROOT}/usr/lib/x86_64-linux-gnu" -maxdepth 1 \
                -type f -name '*.a' -delete
fi

mkdir -p -- "${BUILD}"
config_key="mode=${USE_SYSTEM_DEPS} patch=${PATCH_SHA}"
if [[ ! -f "${BUILD}/build.ninja" || "$(cat "${CONFIG_STAMP}" 2>/dev/null || true)" != "${config_key}" ]]; then
        rm -f -- "${BUILD}/build.ninja"
        if [[ "${USE_SYSTEM_DEPS}" == "1" ]]; then
                pkg_path=""
                pkg_root=""
                cflags=""
        else
                pkg_path="${DEVROOT}/usr/lib/x86_64-linux-gnu/pkgconfig:${DEVROOT}/usr/share/pkgconfig"
                pkg_root="${DEVROOT}"
                cflags="-I${DEVROOT}/usr/include/x86_64-linux-gnu"
        fi
        (cd "${BUILD}" &&
         env PKG_CONFIG_PATH="${pkg_path}" \
             PKG_CONFIG_SYSROOT_DIR="${pkg_root}" \
             CFLAGS="${cflags}" \
             LDFLAGS="-L/usr/lib/x86_64-linux-gnu" \
         ../configure \
             --target-list=x86_64-softmmu \
             --enable-kvm \
             --enable-opengl \
             --enable-sdl \
             --enable-slirp \
             --enable-virglrenderer \
             --disable-fuse \
             --disable-fuse-lseek \
             --disable-werror)
        printf '%s\n' "${config_key}" >"${CONFIG_STAMP}"
fi
ninja -C "${BUILD}" qemu-system-x86_64
mkdir -p -- "${BUILD_ROOT}/bin"
cp -- "${BUILD}/qemu-system-x86_64" \
      "${BUILD_ROOT}/bin/qemu-system-x86_64"
mkdir -p -- "${DATA_DIR}"
cp -a -- /usr/share/qemu/. "${DATA_DIR}/"
cp -a -- /usr/share/seabios/. "${DATA_DIR}/"
printf 'qemu_sdl_binary=%s\n' "${BUILD_ROOT}/bin/qemu-system-x86_64"
printf 'qemu_sdl_data=%s\n' "${DATA_DIR}"
