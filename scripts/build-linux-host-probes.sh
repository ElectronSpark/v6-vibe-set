#!/usr/bin/env bash
# Build host Linux x86_64 ABI probes and stage them into the xv6 sysroot.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
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
