#!/usr/bin/env bash
# Build host Linux x86_64 ABI probes and stage them into the xv6 sysroot.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
HOST_CC="${HOST_CC:-gcc}"
SYSROOT="${1:-${REPO_ROOT}/build-x86_64/sysroot}"
OUT_DIR="${SYSROOT}/bin"
BUILD_BASE="$(mkdir -p "$(dirname "${SYSROOT}")" && cd "$(dirname "${SYSROOT}")" && pwd)"
HOST_PROBE_CFLAGS="${HOST_PROBE_CFLAGS:--O2 -g}"
HOST_PROBE_WARN_CFLAGS="${HOST_PROBE_WARN_CFLAGS:-}"
HOST_USER_CFLAGS="${HOST_USER_CFLAGS:-${HOST_PROBE_CFLAGS}}"
HOST_USER_WARN_CFLAGS="${HOST_USER_WARN_CFLAGS:-${HOST_PROBE_WARN_CFLAGS}}"
STARTUP_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_startup_probe.c"
RUNTIME_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_runtime_probe.c"
STACK_GUARD_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_stack_guard_probe.c"
NCURSES_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_ncurses_probe.c"
READLINE_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_readline_probe.c"
HOST_SH_SRC="${REPO_ROOT}/user/programs/sh/sh.c"
HOST_WAYLAND_REF="${HOST_WAYLAND_REF:-${BUILD_BASE}/ports/wayland-host-glibc-test}"
mapfile -t HOST_USER_PROGRAMS < <(
    find "${REPO_ROOT}/user/programs" -mindepth 1 -maxdepth 1 -type d \
        -printf '%f\n' | sort
)

mkdir -p "${OUT_DIR}"

"${SCRIPT_DIR}/build-linux-host-libs.sh" "${SYSROOT}"

rm -f "${OUT_DIR}"/_* "${OUT_DIR}/host-sh"

"${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} "${STARTUP_SRC}" \
    -o "${OUT_DIR}/linux-host-startup-dynamic"

"${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} -pthread "${RUNTIME_SRC}" \
    -o "${OUT_DIR}/linux-host-runtime-dynamic"

"${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} -fstack-protector-all \
    "${STACK_GUARD_SRC}" -o "${OUT_DIR}/linux-host-stack-guard-dynamic"

"${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} -I"${SYSROOT}/include" \
    -L"${SYSROOT}/lib" -Wl,-rpath,/lib -DUSE_NCURSES_SHELL \
    "${HOST_SH_SRC}" -lncurses -ltinfo -o "${OUT_DIR}/sh"
ln -sf sh "${OUT_DIR}/host-sh"

stage_host_wayland_desktop() {
    local ref="$1"
    if [[ ! -x "${ref}/desktop" || ! -x "${ref}/wlcomp" ]]; then
        echo "build-linux-host-probes: warning: host Wayland desktop artifacts not found in ${ref}" >&2
        echo "build-linux-host-probes: warning: /bin/desktop will not be staged" >&2
        return 0
    fi

    install -m 0755 "${ref}/desktop" "${OUT_DIR}/desktop"
    install -m 0755 "${ref}/wlcomp" "${OUT_DIR}/wlcomp"
    if [[ -x "${ref}/glsmoke" ]]; then
        install -m 0755 "${ref}/glsmoke" "${OUT_DIR}/glsmoke"
    fi
}

stage_host_wayland_desktop "${HOST_WAYLAND_REF}"

"${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} -I"${SYSROOT}/include" \
    -L"${SYSROOT}/lib" -Wl,-rpath,/lib "${NCURSES_SRC}" \
    -lncurses -ltinfo -o "${OUT_DIR}/linux-host-ncurses-dynamic"

"${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} -I"${SYSROOT}/include" \
    -L"${SYSROOT}/lib" -Wl,-rpath,/lib "${READLINE_SRC}" \
    -lreadline -lhistory -lncurses -ltinfo \
    -o "${OUT_DIR}/linux-host-readline-dynamic"

build_host_user_program() {
    local name="$1"
    shift

    "${HOST_CC}" ${HOST_USER_WARN_CFLAGS} ${HOST_USER_CFLAGS} -DHOST_LIBC_PROGRAM \
        -D_GNU_SOURCE -DON_HOST_OS=1 -DCONFIG_ARCH_X86_64=1 \
        -I"${REPO_ROOT}/user/lib" -I"${REPO_ROOT}" -I"${REPO_ROOT}/kernel" \
        -idirafter "${REPO_ROOT}/kernel/kernel/inc" \
        "${REPO_ROOT}/user/programs/${name}/${name}.c" "$@" \
        -o "${OUT_DIR}/${name}"
}

for name in "${HOST_USER_PROGRAMS[@]}"; do
    case "${name}" in
        sh)
            ;;
        cp|mv|rm)
            build_host_user_program "${name}" "${REPO_ROOT}/user/lib/fsutil.c"
            ;;
        pngtest)
            if [[ -f "${SYSROOT}/host-tools/include/png.h" &&
                  -f "${SYSROOT}/host-tools/lib/libpng.a" ]]; then
                build_host_user_program "${name}" \
                    -I"${SYSROOT}/host-tools/include" -I"${SYSROOT}/include" \
                    -L"${SYSROOT}/host-tools/lib" -lpng -lz -lm
            else
                echo "build-linux-host-probes: warning: skipping pngtest until host libpng is staged" >&2
            fi
            ;;
        nouveauabitest)
            if [[ -f "${SYSROOT}/include/libdrm/nouveau/nouveau.h" &&
                  -f "${SYSROOT}/lib/libdrm_nouveau.so" ]]; then
                build_host_user_program "${name}" \
                    -I"${SYSROOT}/include" \
                    -I"${SYSROOT}/include/libdrm" \
                    -I"${SYSROOT}/include/libdrm/nouveau" \
                    -L"${SYSROOT}/lib" -Wl,-rpath,/lib \
                    -ldrm_nouveau -ldrm -pthread -ldl
            else
                echo "build-linux-host-probes: warning: skipping nouveauabitest until libdrm_nouveau is staged" >&2
            fi
            ;;
        d3d12probe)
            # C++ D3D12 GPU-P validation client; needs the vendored DirectX
            # headers + dxguids and links the GPU-PV runtime. Built by its own
            # script so it stays in sync with the host sanity-check build.
            dxh="${REPO_ROOT}/ports/mesa/src/subprojects/DirectX-Headers-1.0"
            host_cxx="${HOST_CXX:-g++}"
            if [[ -d "${dxh}/include/directx" ]] && command -v "${host_cxx}" >/dev/null 2>&1; then
                "${host_cxx}" -std=c++17 -O2 -Wall \
                    -I"${dxh}/include" -I"${dxh}/include/wsl/stubs" \
                    "${REPO_ROOT}/user/programs/d3d12probe/d3d12probe.cpp" \
                    "${dxh}/src/dxguids.cpp" \
                    -ld3d12 -ldxcore -ldl -lpthread \
                    -L/usr/lib/wsl/lib -Wl,-rpath,/usr/lib/wsl/lib \
                    -o "${OUT_DIR}/d3d12probe" \
                    || echo "build-linux-host-probes: warning: d3d12probe build failed; skipping" >&2
            else
                echo "build-linux-host-probes: warning: skipping d3d12probe (DirectX-Headers or g++ missing)" >&2
            fi
            ;;
        gldemo)
            # Offscreen GLES2 "3D demo": renders a shaded triangle to an FBO
            # and reads it back. With GALLIUM_DRIVER=d3d12 it runs on the host
            # GPU over /dev/dxg; otherwise it falls back to Mesa softpipe.
            if [[ -e "${SYSROOT}/lib/libEGL.so" &&
                  -e "${SYSROOT}/lib/libGLESv2.so" ]]; then
                "${HOST_CC}" ${HOST_USER_WARN_CFLAGS} ${HOST_USER_CFLAGS} \
                    -I"${SYSROOT}/include" \
                    "${REPO_ROOT}/user/programs/gldemo/gldemo.c" \
                    -L"${SYSROOT}/lib" \
                    -Wl,-rpath-link,"${SYSROOT}/lib" -Wl,-rpath,/lib \
                    -lEGL -lGLESv2 \
                    -o "${OUT_DIR}/gldemo" \
                    || echo "build-linux-host-probes: warning: gldemo build failed; skipping" >&2
            else
                echo "build-linux-host-probes: warning: skipping gldemo until Mesa EGL/GLESv2 are staged" >&2
            fi
            ;;
        *)
            build_host_user_program "${name}"
            ;;
    esac
done

if "${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} -static "${STARTUP_SRC}" \
    -o "${OUT_DIR}/linux-host-startup-static"; then
    :
else
    echo "build-linux-host-probes: warning: static glibc probe build failed" >&2
    rm -f "${OUT_DIR}/linux-host-startup-static"
fi

if "${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} -static -pthread "${RUNTIME_SRC}" \
    -o "${OUT_DIR}/linux-host-runtime-static"; then
    :
else
    echo "build-linux-host-probes: warning: static glibc runtime probe build failed" >&2
    rm -f "${OUT_DIR}/linux-host-runtime-static"
fi

if "${HOST_CC}" ${HOST_PROBE_WARN_CFLAGS} ${HOST_PROBE_CFLAGS} -static -fstack-protector-all \
    "${STACK_GUARD_SRC}" -o "${OUT_DIR}/linux-host-stack-guard-static"; then
    :
else
    echo "build-linux-host-probes: warning: static stack guard probe build failed" >&2
    rm -f "${OUT_DIR}/linux-host-stack-guard-static"
fi

echo "build-linux-host-probes: wrote probes to ${OUT_DIR}"
