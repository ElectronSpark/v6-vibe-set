#!/usr/bin/env bash
# Wait for exact QEMU executables to exit naturally.  Never signals a VM.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
limit=${1:-120}
[[ "$limit" =~ ^[1-9][0-9]*$ ]] || {
    echo "usage: $0 [positive-timeout-seconds]" >&2
    exit 2
}

deadline=$((SECONDS + limit))
while :; do
    identities=()
    for exe_link in /proc/[0-9]*/exe; do
        exe="$(readlink -f -- "$exe_link" 2>/dev/null)" || continue
        case "${exe##*/}" in
            qemu-system-*|qemu-kvm) ;;
            *) continue ;;
        esac
        pid=${exe_link#/proc/}
        pid=${pid%/exe}
        [[ -r "/proc/$pid/stat" ]] || continue
        stat_line="$(<"/proc/$pid/stat")"
        rest=${stat_line##*) }
        read -r -a fields <<<"$rest"
        start=${fields[19]:-}
        [[ "$start" =~ ^[0-9]+$ ]] || continue
        identities+=("$pid:$start:$exe")
    done

    if ((${#identities[@]} == 0)); then
        exec "$ROOT/scripts/launch/qemu-exact-inventory.sh" --require-zero
    fi

    for identity in "${identities[@]}"; do
        IFS=: read -r pid start exe <<<"$identity"
        [[ -r "/proc/$pid/stat" ]] || continue
        current_exe="$(readlink -f -- "/proc/$pid/exe" 2>/dev/null)" || continue
        stat_line="$(<"/proc/$pid/stat")"
        rest=${stat_line##*) }
        read -r -a fields <<<"$rest"
        [[ "$current_exe" == "$exe" && "${fields[19]:-}" == "$start" ]] || continue

        remaining=$((deadline - SECONDS))
        if ((remaining <= 0)); then
            echo "qemu-wait-natural-zero: timeout while preserving pid=$pid" >&2
            exit 75
        fi
        printf 'qemu-wait-natural-zero: waiting without signal pid=%s start_ticks=%s exe=%s\n' \
            "$pid" "$start" "$exe"
        if ! timeout "${remaining}s" tail -s 0.05 --pid="$pid" -f /dev/null; then
            echo "qemu-wait-natural-zero: timeout while preserving pid=$pid" >&2
            exit 75
        fi
    done
done
