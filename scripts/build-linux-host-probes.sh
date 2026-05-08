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
HOST_SH_SRC="${REPO_ROOT}/user/programs/sh/sh.c"

mkdir -p "${OUT_DIR}"

"${HOST_CC}" -Wall -Wextra -O2 -g "${STARTUP_SRC}" \
    -o "${OUT_DIR}/linux-host-startup-dynamic"

"${HOST_CC}" -Wall -Wextra -O2 -g -pthread "${RUNTIME_SRC}" \
    -o "${OUT_DIR}/linux-host-runtime-dynamic"

"${HOST_CC}" -Wall -Wextra -O2 -g -fstack-protector-all \
    "${STACK_GUARD_SRC}" -o "${OUT_DIR}/linux-host-stack-guard-dynamic"

"${HOST_CC}" -Wall -Wextra -O2 -g -DUSE_NCURSES_SHELL "${HOST_SH_SRC}" \
    -lncurses -o "${OUT_DIR}/host-sh"

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
