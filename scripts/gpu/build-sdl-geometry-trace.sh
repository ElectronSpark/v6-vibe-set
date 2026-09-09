#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
OUT="${1:-${ROOT}/build-x86_64/sdl-geometry-probe/libsdl-geometry-trace.so}"
CC="${CC:-cc}"

mkdir -p -- "$(dirname -- "${OUT}")"
"${CC}" \
    -std=c11 -O2 -fPIC -shared \
    -Wall -Wextra -Werror \
    -Wl,-z,defs \
    -o "${OUT}" \
    "${SCRIPT_DIR}/sdl-geometry-trace.c" \
    -ldl
printf 'sdl-geometry-trace: built %s\n' "${OUT}"
