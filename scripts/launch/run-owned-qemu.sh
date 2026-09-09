#!/usr/bin/env bash
# Exclusive, identity-bound QEMU launcher. It waits for unrelated VMs to exit
# naturally, signals only its exact tokenized process group, and reaps it.
set -euo pipefail

usage() {
    echo "usage: $0 <arch> <kernel-image> <fs.img>" >&2
    exit 2
}

[[ $# -eq 3 ]] || usage
ARCH="$1"
KERNEL="$(realpath -e -- "$2")"
BASE_FSIMG="$(realpath -e -- "$3")"
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"

if [[ -e /.dockerenv ]]; then
    echo "run-owned-qemu: refusing container launch; host /proc must remain authoritative" >&2
    exit 75
fi

# Reproduction owns this lock exclusively while it may replace generated
# images. A managed VM keeps a shared lock for its complete lifetime, so the
# host-side cleanup authorization and launch cannot race.
receipt_root="${ROOT}/build-reproductions/${ARCH}"
mkdir -p -- "${receipt_root}"
exec 8>"${receipt_root}/.reproduction.lock"
flock -s -n 8 || {
    echo "run-owned-qemu: reproduction owns the workspace; refusing launch" >&2
    exit 75
}

wait_seconds="${QEMU_NATURAL_ZERO_TIMEOUT:-120}"
[[ "${wait_seconds}" =~ ^[0-9]+$ ]] || {
    echo "run-owned-qemu: QEMU_NATURAL_ZERO_TIMEOUT must be a nonnegative integer" >&2
    exit 2
}
if (( wait_seconds > 0 )); then
    "${ROOT}/scripts/launch/qemu-wait-natural-zero.sh" "${wait_seconds}"
else
    "${ROOT}/scripts/launch/qemu-exact-inventory.sh" --require-zero
fi
"${ROOT}/scripts/launch/qemu-exact-inventory.sh" --require-zero

token="${QEMU_RUN_TOKEN:-gui-$(date -u +%Y%m%dT%H%M%SZ)-$$}"
[[ "${token}" =~ ^[A-Za-z0-9._-]+$ ]] || {
    echo "run-owned-qemu: invalid run token" >&2
    exit 2
}
pidfile="${QEMU_PIDFILE:-/tmp/xv6-${token}.pid}"
overlay=""
disk="${BASE_FSIMG}"
disk_format="${QEMU_DISK_FORMAT:-raw}"
owned_pid=""
owned_start=""
launcher_pid=""
interrupted=0

rm -f -- "${pidfile}"
if [[ "${QEMU_IMMUTABLE_BASE:-1}" == "1" ]]; then
    command -v qemu-img >/dev/null 2>&1 || {
        echo "run-owned-qemu: qemu-img is required for immutable receipt launch" >&2
        exit 1
    }
    overlay="${QEMU_OVERLAY:-/tmp/xv6-${token}.qcow2}"
    [[ ! -e "${overlay}" ]] || {
        echo "run-owned-qemu: refusing existing overlay ${overlay}" >&2
        exit 1
    }
    qemu-img create -q -f qcow2 -F raw -b "${BASE_FSIMG}" "${overlay}"
    disk="${overlay}"
    disk_format=qcow2
fi

cleanup_owned() {
    if [[ -n "${owned_pid}" && -n "${owned_start}" && -e "/proc/${owned_pid}/stat" ]]; then
        "${ROOT}/scripts/launch/cleanup-owned-qemu.sh" \
            "${owned_pid}" "${owned_start}" "${token}" || true
    fi
}

handle_signal() {
    interrupted=1
    cleanup_owned
}
trap handle_signal INT TERM HUP

QEMU_PREFLIGHT_DONE=1 \
QEMU_RUN_TOKEN="${token}" \
QEMU_PIDFILE="${pidfile}" \
QEMU_DISK_FORMAT="${disk_format}" \
    setsid --wait bash "${ROOT}/scripts/launch/run-qemu.sh" \
        "${ARCH}" "${KERNEL}" "${disk}" &
launcher_pid=$!

for _ in {1..200}; do
    if [[ -s "${pidfile}" ]]; then
        owned_pid="$(<"${pidfile}")"
        if [[ "${owned_pid}" =~ ^[1-9][0-9]*$ && -r "/proc/${owned_pid}/stat" ]]; then
            stat_line="$(</proc/${owned_pid}/stat)"
            rest="${stat_line##*) }"
            read -r -a fields <<<"${rest}"
            owned_start="${fields[19]:-}"
            if "${ROOT}/scripts/launch/cleanup-owned-qemu.sh" --check-only \
                    "${owned_pid}" "${owned_start}" "${token}"; then
                break
            fi
        fi
    fi
    if ! kill -0 "${launcher_pid}" 2>/dev/null; then
        break
    fi
    sleep 0.05
done

set +e
wait "${launcher_pid}"
rc=$?
set -e
if [[ "${interrupted}" == "1" && "${rc}" == "0" ]]; then
    rc=130
fi
cleanup_owned
trap - INT TERM HUP

rm -f -- "${pidfile}"
if [[ -n "${overlay}" ]]; then
    rm -f -- "${overlay}"
fi
if ! "${ROOT}/scripts/launch/qemu-exact-inventory.sh" --require-zero; then
    echo "run-owned-qemu: final exact inventory is nonzero; no unrelated process was signalled" >&2
    [[ "${rc}" != "0" ]] || rc=75
fi
exit "${rc}"
