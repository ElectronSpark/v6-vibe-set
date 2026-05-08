#!/usr/bin/env bash
# Build host Linux x86_64 ABI probes and stage them into the xv6 sysroot.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
HOST_CC="${HOST_CC:-gcc}"
SYSROOT="${1:-${REPO_ROOT}/build-x86_64/sysroot}"
OUT_DIR="${SYSROOT}/bin"
STARTUP_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_startup_probe.c"
RUNTIME_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_runtime_probe.c"
STACK_GUARD_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_stack_guard_probe.c"
NCURSES_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_ncurses_probe.c"
READLINE_SRC="${REPO_ROOT}/tools/linux-abi-probes/host_readline_probe.c"
HOST_SH_SRC="${REPO_ROOT}/user/programs/sh/sh.c"
HOST_USER_PROGRAMS=(
    cat
    cp
    echo
    find
    grep
    kill
    ln
    mkdir
    mv
    ps
    rm
    sleep
    sync
    wc
    xargs
)

mkdir -p "${OUT_DIR}"

"${SCRIPT_DIR}/build-linux-host-libs.sh" "${SYSROOT}"

rm -f "${OUT_DIR}/_sh" "${OUT_DIR}/host-sh"

"${HOST_CC}" -Wall -Wextra -O2 -g "${STARTUP_SRC}" \
    -o "${OUT_DIR}/linux-host-startup-dynamic"

"${HOST_CC}" -Wall -Wextra -O2 -g -pthread "${RUNTIME_SRC}" \
    -o "${OUT_DIR}/linux-host-runtime-dynamic"

"${HOST_CC}" -Wall -Wextra -O2 -g -fstack-protector-all \
    "${STACK_GUARD_SRC}" -o "${OUT_DIR}/linux-host-stack-guard-dynamic"

"${HOST_CC}" -Wall -Wextra -O2 -g -I"${SYSROOT}/include" \
    -L"${SYSROOT}/lib" -Wl,-rpath,/lib -DUSE_NCURSES_SHELL \
    "${HOST_SH_SRC}" -lncurses -ltinfo -o "${OUT_DIR}/sh"
ln -sf sh "${OUT_DIR}/host-sh"

"${HOST_CC}" -Wall -Wextra -O2 -g -I"${SYSROOT}/include" \
    -L"${SYSROOT}/lib" -Wl,-rpath,/lib "${NCURSES_SRC}" \
    -lncurses -ltinfo -o "${OUT_DIR}/linux-host-ncurses-dynamic"

"${HOST_CC}" -Wall -Wextra -O2 -g -I"${SYSROOT}/include" \
    -L"${SYSROOT}/lib" -Wl,-rpath,/lib "${READLINE_SRC}" \
    -lreadline -lhistory -lncurses -ltinfo \
    -o "${OUT_DIR}/linux-host-readline-dynamic"

build_host_user_program() {
    local name="$1"
    shift

    "${HOST_CC}" -Wall -Wextra -O2 -g -DHOST_LIBC_PROGRAM \
        -I"${REPO_ROOT}/user/lib" -I"${REPO_ROOT}/user" \
        "${REPO_ROOT}/user/programs/${name}/${name}.c" "$@" \
        -o "${OUT_DIR}/${name}"
}

for name in "${HOST_USER_PROGRAMS[@]}"; do
    case "${name}" in
        cp|mv|rm)
            build_host_user_program "${name}" "${REPO_ROOT}/user/lib/fsutil.c"
            ;;
        *)
            build_host_user_program "${name}"
            ;;
    esac
done

if "${HOST_CC}" -Wall -Wextra -O2 -g -static "${STARTUP_SRC}" \
    -o "${OUT_DIR}/linux-host-startup-static"; then
    :
else
    echo "build-linux-host-probes: warning: static glibc probe build failed" >&2
    rm -f "${OUT_DIR}/linux-host-startup-static"
fi

if "${HOST_CC}" -Wall -Wextra -O2 -g -static -pthread "${RUNTIME_SRC}" \
    -o "${OUT_DIR}/linux-host-runtime-static"; then
    :
else
    echo "build-linux-host-probes: warning: static glibc runtime probe build failed" >&2
    rm -f "${OUT_DIR}/linux-host-runtime-static"
fi

if "${HOST_CC}" -Wall -Wextra -O2 -g -static -fstack-protector-all \
    "${STACK_GUARD_SRC}" -o "${OUT_DIR}/linux-host-stack-guard-static"; then
    :
else
    echo "build-linux-host-probes: warning: static stack guard probe build failed" >&2
    rm -f "${OUT_DIR}/linux-host-stack-guard-static"
fi

echo "build-linux-host-probes: wrote probes to ${OUT_DIR}"
