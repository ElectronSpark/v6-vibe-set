#!/usr/bin/env bash
# Fail-closed ripgrep wrapper for recursive source searches.
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
readonly MAX_FILESIZE=64M
readonly RUN_SECONDS=45
readonly LOCK_SECONDS=60

die() {
    printf 'safe-rg: rejected: %s\n' "$*" >&2
    exit 64
}

reject_short_cluster() {
    local token=${1#-}
    local char
    local i

    for ((i = 0; i < ${#token}; i++)); do
        char=${token:i:1}
        case "$char" in
            a) die "-a/--text is forbidden" ;;
            u) die "-u/-uuu/--unrestricted is forbidden" ;;
            L) die "-L/--follow is forbidden" ;;
            z) die "-z/--search-zip is forbidden" ;;
            j) die "caller-controlled threads are forbidden" ;;
            f) die "pattern files are forbidden; pass a bounded pattern directly" ;;
            # These options consume the rest of this token (or the next token),
            # so letters in their values are not clustered options.
            A|B|C|E|M|e|g|m|r|t|T) break ;;
        esac
    done
}

reject_option() {
    local option=${1%%=*}
    case "$option" in
        --text) die "-a/--text is forbidden" ;;
        --unrestricted) die "-u/-uuu/--unrestricted is forbidden" ;;
        --no-ignore*) die "--no-ignore and all variants are forbidden" ;;
        --binary) die "--binary is forbidden" ;;
        --follow) die "following symlinks is forbidden" ;;
        --search-zip) die "compressed-file search is forbidden" ;;
        --pre|--pre-glob) die "preprocessors are forbidden" ;;
        --file) die "pattern files are forbidden; pass a bounded pattern directly" ;;
        --threads) die "caller-controlled threads are forbidden" ;;
        --max-filesize) die "caller-controlled file-size limits are forbidden" ;;
        --ignore-file) die "caller-controlled ignore files are forbidden" ;;
        --mmap) die "caller-controlled mmap is forbidden" ;;
    esac
}

reject_existing_path() {
    local value=$1
    local resolved
    local lexical
    local lower
    local cursor
    local component
    local prefix
    local -a components

    [[ -e "$value" || -L "$value" ]] || return 0

    IFS='/' read -r -a components <<< "$value"
    for component in "${components[@]}"; do
        [[ "$component" != .. ]] || die "parent-directory traversal is forbidden: $value"
    done

    lexical=$($REALPATH --no-symlinks -m -- "$value") || die "cannot normalize operand: $value"
    prefix=
    IFS='/' read -r -a components <<< "${lexical#/}"
    for component in "${components[@]}"; do
        [[ -n "$component" ]] || continue
        prefix+=/$component
        [[ ! -L "$prefix" ]] || die "symlinked path component is forbidden: $value ($prefix)"
    done

    resolved=$($REALPATH -e -- "$value") || die "cannot canonicalize operand: $value"
    case "$resolved" in
        "$REPO_ROOT"|"$REPO_ROOT"/*) ;;
        *) die "operand resolves outside $REPO_ROOT: $value ($resolved)" ;;
    esac

    case "$resolved" in
        "$REPO_ROOT"/build|"$REPO_ROOT"/build/*|"$REPO_ROOT"/build-*|"$REPO_ROOT"/build-*/*|"$REPO_ROOT"/cmake-build-*|"$REPO_ROOT"/cmake-build-*/*|"$REPO_ROOT"/out|"$REPO_ROOT"/out/*)
            die "generated build-tree operand is forbidden: $value"
            ;;
    esac

    cursor=$resolved
    [[ -d "$cursor" ]] || cursor=${cursor%/*}
    while [[ -n "$cursor" && "$cursor" != / ]]; do
        component=${cursor##*/}
        case "$component" in
            build)
                [[ "$cursor" == "$REPO_ROOT/scripts/build" ]] ||
                    die "generated build-tree operand is forbidden: $value"
                ;;
            build-*|cmake-build-*|CMakeFiles|out|dist|target)
                die "generated build-tree operand is forbidden: $value"
                ;;
        esac
        cursor=${cursor%/*}
    done

    if [[ -f "$resolved" ]]; then
        lower=${resolved,,}
        case "$lower" in
            *.img|*.raw|*.qcow|*.qcow2|*.vhd|*.vhdx|*.vmdk|*.iso|*.zip|*.tar|*.gz|*.bz2|*.xz|*.zst|*.7z|*.rar|*.tgz|*.tbz|*.tbz2|*.txz)
                die "disk/image/archive artifact operands are forbidden: $value"
                ;;
        esac
    fi
}

has_explicit_path_operand() {
    local arg
    local token
    local char
    local consume=
    local i
    local after_delimiter=0
    local pattern_from_option=0
    local pattern_seen=0

    for arg in "$@"; do
        if [[ -n "$consume" ]]; then
            if [[ "$consume" == pattern ]]; then
                pattern_from_option=1
            fi
            consume=
            continue
        fi

        if (( ! after_delimiter )); then
            if [[ "$arg" == -- ]]; then
                after_delimiter=1
                continue
            fi
            case "$arg" in
                --regexp=*)
                    pattern_from_option=1
                    continue
                    ;;
                --regexp)
                    consume=pattern
                    continue
                    ;;
                --after-context|--before-context|--color|--colors|--context|--context-separator|--dfa-size-limit|--encoding|--engine|--field-context-separator|--field-match-separator|--generate|--glob|--hostname-bin|--hyperlink-format|--iglob|--max-columns|--max-count|--max-depth|--path-separator|--regex-size-limit|--replace|--sort|--sortr|--type|--type-add|--type-clear|--type-not)
                    consume=option
                    continue
                    ;;
                --*=*|--*)
                    continue
                    ;;
                -?*)
                    token=${arg#-}
                    for ((i = 0; i < ${#token}; i++)); do
                        char=${token:i:1}
                        case "$char" in
                            e)
                                pattern_from_option=1
                                if ((i + 1 == ${#token})); then
                                    consume=pattern
                                fi
                                break
                                ;;
                            A|B|C|E|M|d|g|m|r|t|T)
                                if ((i + 1 == ${#token})); then
                                    consume=option
                                fi
                                break
                                ;;
                        esac
                    done
                    continue
                    ;;
            esac
        fi

        if ((pattern_from_option || pattern_seen)); then
            return 0
        fi
        pattern_seen=1
    done
    return 1
}

[[ -x "$RG" ]] || die "$RG is unavailable"
[[ -x "$TIMEOUT" ]] || die "$TIMEOUT is unavailable"
[[ -x "$FLOCK" ]] || die "$FLOCK is unavailable"

args=("$@")
before_delimiter=()
after_delimiter=()
delimiter_seen=0

for arg in "${args[@]}"; do
    if ((delimiter_seen)); then
        after_delimiter+=("$arg")
        reject_existing_path "$arg"
        continue
    fi
    if [[ "$arg" == -- ]]; then
        delimiter_seen=1
        continue
    fi

    case "$arg" in
        --*) reject_option "$arg" ;;
        -?*) reject_short_cluster "$arg" ;;
    esac
    reject_existing_path "$arg"
    before_delimiter+=("$arg")
done

if ! has_explicit_path_operand "${args[@]}"; then
    [[ -n "${PWD:-}" && -d "$PWD" ]] || die "current working directory is unavailable"
    reject_existing_path "$PWD"
    physical_cwd=$($REALPATH -e -- .) || die "cannot canonicalize current working directory"
    logical_cwd=$($REALPATH -e -- "$PWD") || die "cannot canonicalize PWD: $PWD"
    [[ "$physical_cwd" == "$logical_cwd" ]] ||
        die "PWD does not identify the actual current working directory"
fi

# Config files can inject options before this wrapper sees them.
unset RIPGREP_CONFIG_PATH

safe_options=(
    --no-config
    --threads=1
    --max-filesize="$MAX_FILESIZE"
    --no-follow
    --glob='!/build/**'
    --glob='!**/build-*/**'
    --glob='!**/cmake-build-*/**'
    --glob='!**/out/**'
    --glob='!**/dist/**'
    --glob='!**/target/**'
    --glob='!**/.git/**'
    --glob='!**/CMakeFiles/**'
    --iglob='!*.img'
    --iglob='!*.raw'
    --iglob='!*.qcow'
    --iglob='!*.qcow2'
    --iglob='!*.vhd'
    --iglob='!*.vhdx'
    --iglob='!*.vmdk'
    --iglob='!*.iso'
    --iglob='!*.zip'
    --iglob='!*.tar'
    --iglob='!*.gz'
    --iglob='!*.bz2'
    --iglob='!*.xz'
    --iglob='!*.zst'
    --iglob='!*.7z'
    --iglob='!*.rar'
    --iglob='!*.tgz'
    --iglob='!*.tbz'
    --iglob='!*.tbz2'
    --iglob='!*.txz'
)

command=("$RG" "${before_delimiter[@]}" "${safe_options[@]}")
if ((delimiter_seen)); then
    command+=(-- "${after_delimiter[@]}")
fi

# Serialize before starting ripgrep. The lock wait and ripgrep runtime are both
# finite; the address-space limit is inherited by timeout and ripgrep.
exec 9>"$LOCK_FILE"
$FLOCK --exclusive --wait "$LOCK_SECONDS" 9 || die "global search lock timed out"
ulimit -v "$MAX_KIB" || die "could not set the 2 GiB address-space limit"
exec "$TIMEOUT" --signal=TERM --kill-after=5s "${RUN_SECONDS}s" "${command[@]}"
