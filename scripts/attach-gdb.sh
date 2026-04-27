#!/usr/bin/env bash
# attach-gdb.sh - attach GDB to a QEMU xv6 kernel GDB stub.
#
# Start QEMU first, for example:
#   QEMU_GDB=1 USE_KVM=1 bash scripts/launch-gui.sh
#
# Then in another terminal:
#   bash scripts/attach-gdb.sh
#
# Useful overrides:
#   GDB_PORT=2159          Connect to a non-default QEMU_GDB_PORT.
#   GDB_BIN=gdb-multiarch  Use a different GDB executable.
#   GDB_BT=1              Print all thread backtraces immediately after attach.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-${ARCH}}"
GDB_PORT="${GDB_PORT:-1234}"
GDB_BIN="${GDB_BIN:-gdb}"
GDB_BT="${GDB_BT:-0}"

kernel_candidates=(
    "${KERNEL:-}"
    "${BUILD_DIR}/kernel/build/kernel/kernel"
    "${BUILD_DIR}/kernel/kernel/kernel"
)

KERNEL_PATH=""
for candidate in "${kernel_candidates[@]}"; do
    if [[ -n "${candidate}" && -f "${candidate}" ]]; then
        KERNEL_PATH="${candidate}"
        break
    fi
done

if [[ -z "${KERNEL_PATH}" ]]; then
    echo "attach-gdb: kernel image not found. Tried:" >&2
    printf '  %s\n' "${kernel_candidates[@]}" >&2
    exit 1
fi

if ! command -v "${GDB_BIN}" >/dev/null 2>&1; then
    echo "attach-gdb: ${GDB_BIN} not found; set GDB_BIN=/path/to/gdb" >&2
    exit 127
fi

cmd=("${GDB_BIN}" "${KERNEL_PATH}"
     -ex "set pagination off"
     -ex "set confirm off"
    -x "${SCRIPT_DIR}/xv6.gdb"
     -ex "target remote :${GDB_PORT}")

if [[ "${GDB_BT}" == "1" ]]; then
    cmd+=(-ex "thread apply all bt")
fi

printf 'attach-gdb: connecting to :%s with symbols from %s\n' "${GDB_PORT}" "${KERNEL_PATH}" >&2
printf 'attach-gdb: after a freeze, press Ctrl-C here, then run: xv6-freeze\n' >&2
exec "${cmd[@]}"
