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

if [[ "${VERSION}" != "9.0.2" ]]; then
        echo "build-qemu-sdl: unsupported QEMU_SDL_VERSION=${VERSION}" >&2
        exit 2
fi
mkdir -p -- "${BUILD_ROOT}"
if [[ ! -f "${TARBALL}" ]]; then
        curl -L --fail --show-error --output "${TARBALL}.tmp" "${URL}"
        mv -- "${TARBALL}.tmp" "${TARBALL}"
fi
if [[ ! -d "${SOURCE}" ]]; then
        tar -C "${BUILD_ROOT}" -xf "${TARBALL}"
fi
if [[ ! -f "${SOURCE}/.xv6-sdl-aspect-patched" ]]; then
        patch -d "${SOURCE}" -p1 --forward <"${PATCH}"
        touch "${SOURCE}/.xv6-sdl-aspect-patched"
fi

# Keep the build independent of host development-package installation.  The
# runtime libraries are already present on the supported Ubuntu/WSL host; the
# matching headers, pkg-config metadata, and linker symlinks live only in this
# generated private sysroot.
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
# QEMU asks Meson for static dependencies where available.  The extracted
# development packages include static SDL/GLib archives whose transitive
# build dependencies are intentionally absent; force the matching shared ABI.
find "${DEVROOT}/usr/lib/x86_64-linux-gnu" -maxdepth 1 \
        -type f -name '*.a' -delete

mkdir -p -- "${BUILD}"
if [[ ! -f "${BUILD}/build.ninja" || ! -f "${CONFIG_STAMP}" ]]; then
        (cd "${BUILD}" &&
         export PKG_CONFIG_PATH="${DEVROOT}/usr/lib/x86_64-linux-gnu/pkgconfig:${DEVROOT}/usr/share/pkgconfig" &&
         export PKG_CONFIG_SYSROOT_DIR="${DEVROOT}" &&
         export CFLAGS="-I${DEVROOT}/usr/include/x86_64-linux-gnu" &&
         export LDFLAGS="-L/usr/lib/x86_64-linux-gnu" &&
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
        touch "${CONFIG_STAMP}"
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
