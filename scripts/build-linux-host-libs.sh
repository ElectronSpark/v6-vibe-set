#!/usr/bin/env bash
# Build host-glibc libraries used by host-built Linux userland tests.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
HOST_CC="${HOST_CC:-gcc}"
HOST_AR="${HOST_AR:-ar}"
HOST_RANLIB="${HOST_RANLIB:-ranlib}"
SYSROOT="${1:-${REPO_ROOT}/build-x86_64/sysroot}"
BUILD_ROOT="${BUILD_ROOT:-${REPO_ROOT}/build-x86_64/host-glibc-libs}"

SYSROOT="$(mkdir -p "${SYSROOT}" && cd "${SYSROOT}" && pwd)"
BUILD_ROOT="$(mkdir -p "${BUILD_ROOT}" && cd "${BUILD_ROOT}" && pwd)"

NCURSES_SRC="${REPO_ROOT}/ports/ncurses/src"
READLINE_SRC="${REPO_ROOT}/ports/readline/src"
NCURSES_BUILD="${BUILD_ROOT}/ncurses"
READLINE_BUILD="${BUILD_ROOT}/readline"

copy_source_tree() {
    local src="$1"
    local dst="$2"

    rm -rf "${dst}"
    mkdir -p "${dst}"
    (cd "${src}" && tar --exclude=.git -cf - .) | (cd "${dst}" && tar -xf -)
}

mkdir -p "${SYSROOT}/lib" "${SYSROOT}/include" "${SYSROOT}/share"
mkdir -p "${BUILD_ROOT}"

copy_source_tree "${NCURSES_SRC}" "${NCURSES_BUILD}"
(
    cd "${NCURSES_BUILD}"
    CC="${HOST_CC}" AR="${HOST_AR}" RANLIB="${HOST_RANLIB}" \
        ./configure \
            --prefix=/usr \
            --libdir=/lib \
            --includedir=/include \
            --with-shared \
            --with-normal \
            --with-termlib \
            --without-debug \
            --without-ada \
            --without-cxx \
            --without-cxx-binding \
            --without-manpages \
            --without-tests \
            --without-progs \
            --with-fallbacks=xterm,xterm-256color,vt100,dumb \
            --with-default-terminfo-dir=/usr/share/terminfo \
            --with-terminfo-dirs=/usr/share/terminfo:/lib/terminfo \
            --enable-pc-files \
            --with-pkg-config-libdir=/lib/pkgconfig
    make -j"${HOST_LIB_JOBS:-2}"
    make DESTDIR="${SYSROOT}" install
)

copy_source_tree "${READLINE_SRC}" "${READLINE_BUILD}"
(
    cd "${READLINE_BUILD}"
    CC="${HOST_CC}" AR="${HOST_AR}" RANLIB="${HOST_RANLIB}" \
    CPPFLAGS="-I${SYSROOT}/include" \
    LDFLAGS="-L${SYSROOT}/lib" \
    bash_cv_termcap_lib=libncurses \
        ./configure \
            --prefix=/usr \
            --libdir=/lib \
            --includedir=/include \
            --enable-shared \
            --enable-static \
            --with-curses
    make -j"${HOST_LIB_JOBS:-2}" SHLIB_LIBS="-L${SYSROOT}/lib -lncurses"
    make DESTDIR="${SYSROOT}" install SHLIB_LIBS="-L${SYSROOT}/lib -lncurses"
)

mkdir -p "${SYSROOT}/lib/pkgconfig"
if [[ -e "${SYSROOT}/include/curses.h" && ! -e "${SYSROOT}/include/ncurses.h" ]]; then
    ln -s curses.h "${SYSROOT}/include/ncurses.h"
fi
if [[ -d "${SYSROOT}/share/terminfo" && ! -e "${SYSROOT}/lib/terminfo" ]]; then
    ln -s ../share/terminfo "${SYSROOT}/lib/terminfo"
fi

echo "build-linux-host-libs: staged ncurses/readline host-glibc libs into ${SYSROOT}"
