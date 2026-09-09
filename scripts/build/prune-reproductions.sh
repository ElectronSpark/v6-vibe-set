#!/usr/bin/env bash
# Retain only marked, completed build receipts. Never touches source, an
# active QEMU disk, a protected deployment, or the newest known-good receipt.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
ARCH="${XV6_ARCH:-x86_64}"
RECEIPT_ROOT="${XV6_REPRODUCTION_ROOT:-${ROOT}/build-reproductions/${ARCH}}"
LIMIT="${1:-${XV6_KEEP_ITERATIONS:-3}}"

[[ "${LIMIT}" =~ ^[1-3]$ ]] || {
    echo "prune-reproductions: limit must be 1, 2, or 3" >&2
    exit 2
}

mkdir -p -- "${RECEIPT_ROOT}"
receipt_root_real="$(realpath -e -- "${RECEIPT_ROOT}")"
case "${receipt_root_real}" in
    "${ROOT}"/build-reproductions/*) ;;
    *)
        echo "prune-reproductions: refusing unsafe receipt root ${receipt_root_real}" >&2
        exit 2
        ;;
esac

collect_receipts() {
    local path
    receipts=()
    while IFS= read -r -d '' path; do
        [[ -f "${path}/.xv6-reproduction-complete" ]] || continue
        receipts+=("${path}")
    done < <(find "${receipt_root_real}" -mindepth 1 -maxdepth 1 -type d \
        ! -name '.*' -print0 | sort -z)
}

collect_receipts
while (( ${#receipts[@]} > LIMIT )); do
    newest="${receipts[${#receipts[@]}-1]}"
    victim=""
    for candidate in "${receipts[@]}"; do
        [[ "${candidate}" != "${newest}" ]] || continue
        [[ ! -e "${candidate}/.keep" ]] || continue
        if "${ROOT}/scripts/launch/assert-qemu-path-unused.sh" "${candidate}"; then
            victim="${candidate}"
            break
        fi
    done

    if [[ -z "${victim}" ]]; then
        echo "prune-reproductions: no confirmed-unused non-newest receipt can be removed" >&2
        exit 75
    fi
    [[ -f "${victim}/.xv6-reproduction-complete" && ! -L "${victim}" ]] || {
        echo "prune-reproductions: marker/type changed for ${victim}" >&2
        exit 2
    }
    echo "prune-reproductions: removing oldest confirmed-unused receipt ${victim}"
    rm -rf --one-file-system -- "${victim}"
    collect_receipts
done

printf 'prune-reproductions: retained=%d limit=%d root=%s\n' \
    "${#receipts[@]}" "${LIMIT}" "${receipt_root_real}"
