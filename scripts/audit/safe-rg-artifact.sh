#!/usr/bin/env bash
# Search only explicitly named, small, regular artifact files.
# Usage: safe-rg-artifact.sh PATTERN FILE [FILE ...]
set -euo pipefail

readonly SELF="$(readlink -f -- "${BASH_SOURCE[0]}")"
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "$SELF")" && pwd -P)"
readonly REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd -P)"
readonly RG=/usr/bin/rg
readonly TIMEOUT=/usr/bin/timeout
readonly FLOCK=/usr/bin/flock
readonly REALPATH=/usr/bin/realpath
readonly LOCK_FILE="/tmp/xv6-os-safe-rg-${UID}.lock"
readonly MAX_KIB=2097152
readonly MAX_BYTES=$((64 * 1024 * 1024))
readonly RUN_SECONDS=45
readonly LOCK_SECONDS=60

die() {
    printf 'safe-rg-artifact: rejected: %s\n' "$*" >&2
    exit 64
}

if (($# < 2)); then
    die "usage: safe-rg-artifact.sh PATTERN FILE [FILE ...]"
fi

pattern=$1
shift
files=()

for file in "$@"; do
    [[ -f "$file" ]] || die "not a regular file: $file"

    IFS='/' read -r -a components <<< "$file"
    for component in "${components[@]}"; do
        [[ "$component" != .. ]] || die "parent-directory traversal is forbidden: $file"
    done

    lexical=$($REALPATH --no-symlinks -m -- "$file") || die "cannot normalize: $file"
    prefix=
    IFS='/' read -r -a components <<< "${lexical#/}"
    for component in "${components[@]}"; do
        [[ -n "$component" ]] || continue
        prefix+=/$component
        [[ ! -L "$prefix" ]] || die "symlinked path component is forbidden: $file ($prefix)"
    done

    resolved=$($REALPATH -e -- "$file") || die "cannot canonicalize: $file"
    case "$resolved" in
        "$REPO_ROOT"/*) ;;
        *) die "file resolves outside $REPO_ROOT: $file ($resolved)" ;;
    esac

    lower=${resolved,,}
    case "$lower" in
        *.img|*.raw|*.qcow|*.qcow2|*.vhd|*.vhdx|*.vmdk|*.iso|*.zip|*.tar|*.gz|*.bz2|*.xz|*.zst|*.7z|*.rar|*.tgz|*.tbz|*.tbz2|*.txz)
            die "disk/image/archive extension is forbidden: $file"
            ;;
    esac

    size=$(stat -Lc '%s' -- "$resolved") || die "cannot stat: $file"
    [[ "$size" =~ ^[0-9]+$ ]] || die "invalid size for: $file"
    ((size <= MAX_BYTES)) || die "file exceeds 64 MiB: $file ($size bytes)"
    files+=("$resolved")
done

unset RIPGREP_CONFIG_PATH
exec 9>"$LOCK_FILE"
$FLOCK --exclusive --wait "$LOCK_SECONDS" 9 || die "global search lock timed out"
ulimit -v "$MAX_KIB" || die "could not set the 2 GiB address-space limit"
exec "$TIMEOUT" --signal=TERM --kill-after=5s "${RUN_SECONDS}s" \
    "$RG" --no-config --threads=1 --max-filesize=64M --no-follow \
    --line-number --color=never -- "$pattern" "${files[@]}"
