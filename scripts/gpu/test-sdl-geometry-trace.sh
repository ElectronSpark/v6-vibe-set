#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
OUTDIR="${ROOT}/build-x86_64/sdl-geometry-probe/trace-selftest"
TRACE_LIB="${OUTDIR}/libsdl-geometry-trace.so"
SELFTEST="${OUTDIR}/sdl-geometry-trace-selftest"
LOG="${OUTDIR}/trace.log"
CC="${CC:-cc}"

mkdir -p -- "${OUTDIR}"
"${SCRIPT_DIR}/build-sdl-geometry-trace.sh" "${TRACE_LIB}"
"${CC}" \
    -std=c11 -O2 -Wall -Wextra -Werror \
    -Wl,-z,defs \
    -o "${SELFTEST}" \
    "${SCRIPT_DIR}/sdl-geometry-trace-selftest.c" \
    -ldl
: >"${LOG}"
SDL_VIDEODRIVER=dummy \
LD_PRELOAD="${TRACE_LIB}" \
XV6_SDL_GEOMETRY_TRACE_LOG="${LOG}" \
    "${SELFTEST}" >"${OUTDIR}/selftest.out" 2>"${OUTDIR}/selftest.err"

trace="$(<"${LOG}")"
[[ "${trace}" == *"op=create-call"* ]]
[[ "${trace}" == *"requested=320x240"* ]]
[[ "${trace}" == *"op=create-return"* ]]
[[ "${trace}" == *"logical=320x240"* ]]
[[ "${trace}" == *"driver=dummy"* ]]
printf 'sdl-geometry-trace-selftest: PASS log=%s\n' "${LOG}"
