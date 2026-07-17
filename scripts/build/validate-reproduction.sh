#!/usr/bin/env bash
# Host-side structural validation for a complete x86_64 KDE-only build.
set -euo pipefail

usage() {
    echo "usage: $0 BUILD_OR_RECEIPT_DIR" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
BUILD="$(realpath -e -- "$1")"

kernel_elf="${BUILD}/kernel/kernel.elf"
kernel_boot="${BUILD}/kernel/build/kernel/xv6.bin"
fsimg="${BUILD}/fs.img"

for required in "${kernel_elf}" "${kernel_boot}" "${fsimg}"; do
    [[ -s "${required}" ]] || {
        echo "validate-reproduction: missing or empty artifact ${required}" >&2
        exit 1
    }
done

file_output="$(file -b -- "${kernel_elf}")"
case "${file_output}" in
    *ELF*64-bit*x86-64*) ;;
    *)
        echo "validate-reproduction: kernel ELF contract failed: ${file_output}" >&2
        exit 1
        ;;
esac

e2fsck -fn "${fsimg}"

debugfs_has() {
    local path="$1"
    local output
    output="$(debugfs -R "stat ${path}" "${fsimg}" 2>&1 || true)"
    case "${output}" in
        *'File not found'*|*'not found by ext2_lookup'*) return 1 ;;
        *'Inode:'*) return 0 ;;
        *)
            echo "validate-reproduction: inconclusive debugfs stat for ${path}: ${output}" >&2
            return 1
            ;;
    esac
}

required_paths=(
    /bin/kde-session
    /bin/xv6-desktop-session
    /bin/kde-terminal-launcher
    /bin/wayland-chromium
    /usr/bin/kwin_wayland
    /usr/bin/plasmashell
)
for path in "${required_paths[@]}"; do
    if ! debugfs_has "${path}"; then
        echo "validate-reproduction: required KDE path missing: ${path}" >&2
        exit 1
    fi
done

for path in /bin/weston /bin/weston-session /usr/bin/weston /usr/bin/weston-terminal; do
    if debugfs_has "${path}"; then
        echo "validate-reproduction: deprecated Weston runtime path present: ${path}" >&2
        exit 1
    fi
done

startup="$(debugfs -R 'cat /etc/startup' "${fsimg}" 2>/dev/null || true)"
case "${startup}" in
    *'XV6_DESKTOP_DEFAULT=kde /bin/xv6-desktop-session'*) ;;
    *)
        echo "validate-reproduction: KDE startup contract is missing" >&2
        exit 1
        ;;
esac
case "${startup}" in
    *weston*|*Weston*)
        echo "validate-reproduction: deprecated Weston reference found in /etc/startup" >&2
        exit 1
        ;;
esac

sha256sum "${kernel_elf}" "${kernel_boot}" "${fsimg}"
printf 'validate-reproduction: PASS build=%s desktop=kde-only\n' "${BUILD}"
