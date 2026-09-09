#!/usr/bin/env bash
# Prove that no exact QEMU executable is using a path before generated cleanup.
# This script never sends a signal.
set -euo pipefail

usage() {
    echo "usage: $0 PATH" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage
candidate="$(realpath -m -- "$1")"
[[ -e "${candidate}" ]] || exit 0

host_prechecked=0
if [[ -e /.dockerenv && "${XV6_HOST_PROC_VISIBLE:-0}" != "1" ]]; then
    authorized_root="$(realpath -m -- "${XV6_HOST_CLEANUP_AUTHORIZED_ROOT:-/nonexistent}")"
    authority_file="${authorized_root}/build-reproductions/x86_64/.host-cleanup-authority"
    case "${candidate}" in
        "${authorized_root}"|"${authorized_root}"/*) ;;
        *)
            echo "assert-qemu-path-unused: candidate is outside the host-authorized workspace" >&2
            exit 2
            ;;
    esac
    [[ "${XV6_REPRODUCTION_LOCK_HELD:-0}" == "1" &&
       -s "${authority_file}" &&
       "$(<"${authority_file}")" == "${XV6_HOST_CLEANUP_AUTHORITY_TOKEN:-}" ]] || {
        echo "assert-qemu-path-unused: refusing cleanup without the host wrapper's live authority" >&2
        exit 2
    }
    host_prechecked=1
fi

declare -A protected_inodes=()
for protected in \
    "${candidate}/fs.img" \
    "${candidate}/artifacts/fs.img" \
    "${candidate}/kernel/build/kernel/xv6.bin" \
    "${candidate}/artifacts/kernel/build/kernel/xv6.bin"; do
    [[ -f "${protected}" ]] || continue
    identity="$(stat -Lc '%d:%i' -- "${protected}" 2>/dev/null || true)"
    [[ -n "${identity}" ]] && protected_inodes["${identity}"]=1
done

used=0
for exe_link in /proc/[0-9]*/exe; do
    exe="$(readlink -f -- "${exe_link}" 2>/dev/null)" || continue
    case "${exe##*/}" in
        qemu-system-*|qemu-kvm) ;;
        *) continue ;;
    esac

    pid="${exe_link#/proc/}"
    pid="${pid%/exe}"
    [[ -r "/proc/${pid}/stat" ]] || continue
    stat_line="$(</proc/${pid}/stat)"
    rest="${stat_line##*) }"
    read -r -a fields <<<"${rest}"
    start_ticks="${fields[19]:-unknown}"

    cwd="$(readlink -f -- "/proc/${pid}/cwd" 2>/dev/null || true)"
    case "${cwd}" in
        "${candidate}"|"${candidate}"/*)
            printf 'assert-qemu-path-unused: IN-USE path=%s pid=%s start_ticks=%s source=cwd\n' \
                "${candidate}" "${pid}" "${start_ticks}" >&2
            used=1
            ;;
    esac

    for fd_link in /proc/"${pid}"/fd/*; do
        [[ -e "${fd_link}" ]] || continue
        target="$(readlink -f -- "${fd_link}" 2>/dev/null || true)"
        [[ -n "${target}" ]] || continue
        case "${target}" in
            "${candidate}"|"${candidate}"/*)
                printf 'assert-qemu-path-unused: IN-USE path=%s pid=%s start_ticks=%s source=fd target=%s\n' \
                    "${candidate}" "${pid}" "${start_ticks}" "${target}" >&2
                used=1
                ;;
        esac
        identity="$(stat -Lc '%d:%i' -- "${target}" 2>/dev/null || true)"
        if [[ -n "${identity}" && -n "${protected_inodes[${identity}]:-}" ]]; then
            printf 'assert-qemu-path-unused: IN-USE path=%s pid=%s start_ticks=%s source=inode target=%s\n' \
                "${candidate}" "${pid}" "${start_ticks}" "${target}" >&2
            used=1
        fi
    done

    if [[ -r "/proc/${pid}/cmdline" ]]; then
        mapfile -d '' -t cmdline <"/proc/${pid}/cmdline"
        for arg in "${cmdline[@]}"; do
            if [[ "${arg}" == *"${candidate}"* ]]; then
                printf 'assert-qemu-path-unused: IN-USE path=%s pid=%s start_ticks=%s source=cmdline\n' \
                    "${candidate}" "${pid}" "${start_ticks}" >&2
                used=1
                break
            fi
        done
    fi
done

if [[ "${used}" == "1" ]]; then
    echo "assert-qemu-path-unused: refusing generated cleanup; no process was signalled" >&2
    exit 75
fi

if [[ "${host_prechecked}" == "1" ]]; then
    printf 'assert-qemu-path-unused: PASS path=%s host_prechecked=1 local_container_scan=1\n' "${candidate}"
else
    printf 'assert-qemu-path-unused: PASS path=%s\n' "${candidate}"
fi
