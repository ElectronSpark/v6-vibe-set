#!/usr/bin/env bash
# Terminate only a QEMU process group whose leader identity and run token match.
set -euo pipefail

usage() {
    echo "usage: $0 [--check-only] PID START_TICKS RUN_TOKEN" >&2
    exit 2
}

check_only=0
if [[ "${1:-}" == "--check-only" ]]; then
    check_only=1
    shift
fi
[[ $# -eq 3 ]] || usage

pid="$1"
expected_start="$2"
token="$3"
[[ "${pid}" =~ ^[1-9][0-9]*$ ]] || usage
[[ "${expected_start}" =~ ^[1-9][0-9]*$ ]] || usage
[[ "${token}" =~ ^[A-Za-z0-9._-]+$ ]] || usage

validate_owned_leader() {
    local exe base stat_line rest actual_start pgid
    local -a fields cmdline

    [[ -r "/proc/${pid}/exe" && -r "/proc/${pid}/stat" &&
       -r "/proc/${pid}/cmdline" ]] || return 1
    exe="$(readlink -f -- "/proc/${pid}/exe")" || return 1
    base="${exe##*/}"
    case "${base}" in
        qemu-system-*|qemu-kvm) ;;
        *) return 1 ;;
    esac

    stat_line="$(</proc/${pid}/stat)"
    rest="${stat_line##*) }"
    read -r -a fields <<<"${rest}"
    pgid="${fields[2]:-0}"
    actual_start="${fields[19]:-0}"
    [[ "${pgid}" == "${pid}" ]] || return 1
    [[ "${actual_start}" == "${expected_start}" ]] || return 1

    mapfile -d '' -t cmdline <"/proc/${pid}/cmdline"
    [[ " ${cmdline[*]} " == *" xv6-${token} "* ]] || return 1
    return 0
}

if ! validate_owned_leader; then
    echo "cleanup-owned-qemu: REFUSED pid=${pid} reason=identity-token-or-pgid-mismatch" >&2
    exit 3
fi

if [[ "${check_only}" == "1" ]]; then
    echo "cleanup-owned-qemu: CHECK-PASS pid=${pid} token=${token}"
    exit 0
fi

echo "cleanup-owned-qemu: TERM pid=${pid} pgid=${pid} token=${token}"
/bin/kill -TERM -- "-${pid}"

for _ in {1..50}; do
    [[ -e "/proc/${pid}/stat" ]] || {
        echo "cleanup-owned-qemu: TERM-PASS pid=${pid}"
        exit 0
    }
    sleep 0.1
done

if ! validate_owned_leader; then
    echo "cleanup-owned-qemu: REFUSED-KILL pid=${pid} reason=identity-changed" >&2
    exit 4
fi

echo "cleanup-owned-qemu: KILL pid=${pid} pgid=${pid} token=${token}"
/bin/kill -KILL -- "-${pid}"
for _ in {1..20}; do
    [[ -e "/proc/${pid}/stat" ]] || {
        echo "cleanup-owned-qemu: KILL-PASS pid=${pid}"
        exit 0
    }
    sleep 0.1
done

echo "cleanup-owned-qemu: FAIL pid=${pid} reason=leader-still-live" >&2
exit 5
