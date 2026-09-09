#!/usr/bin/env bash
# List exact qemu-system-* / qemu-kvm executables without name matching.
set -euo pipefail

require_zero=0
require_no_x86=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --require-zero) require_zero=1 ;;
        --require-no-x86) require_no_x86=1 ;;
        *)
            echo "usage: $0 [--require-zero] [--require-no-x86]" >&2
            exit 2
            ;;
    esac
    shift
done
[[ $# -eq 0 ]] || {
    echo "usage: $0 [--require-zero] [--require-no-x86]" >&2
    exit 2
}

count=0
x86_count=0
for exe_link in /proc/[0-9]*/exe; do
    exe="$(readlink -f -- "${exe_link}" 2>/dev/null)" || continue
    case "${exe##*/}" in
        qemu-system-*|qemu-kvm) ;;
        *) continue ;;
    esac
    pid="${exe_link#/proc/}"
    pid="${pid%/exe}"
    [[ -r "/proc/${pid}/stat" && -r "/proc/${pid}/cmdline" ]] || continue
    stat_line="$(</proc/${pid}/stat)"
    rest="${stat_line##*) }"
    read -r -a fields <<<"${rest}"
    mapfile -d '' -t cmdline <"/proc/${pid}/cmdline"
    printf 'qemu pid=%s pgid=%s start_ticks=%s exe=%s command=' \
        "${pid}" "${fields[2]:-unknown}" "${fields[19]:-unknown}" "${exe}"
    printf '%q ' "${cmdline[@]}"
    printf '\n'
    count=$((count + 1))
    case "${exe##*/}" in
        qemu-system-x86*|qemu-kvm) x86_count=$((x86_count + 1)) ;;
    esac
done
printf 'exact_qemu_count=%s\n' "${count}"
printf 'exact_x86_qemu_count=%s\n' "${x86_count}"

if [[ "${require_zero}" == "1" && "${count}" -ne 0 ]]; then
    exit 1
fi
if [[ "${require_no_x86}" == "1" && "${x86_count}" -ne 0 ]]; then
    exit 1
fi
