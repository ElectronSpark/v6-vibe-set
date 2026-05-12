#!/usr/bin/env bash
# Build a Hyper-V Gen2 bootable VHDX containing an EFI loader, xv6.bin, and
# a writable ext4 root filesystem partition.

set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: scripts/make-hyperv-image.sh <xv6.bin> <fs.img> <out.vhdx> [size_mb]

Environment:
  GNU_EFI_ROOT  optional root containing usr/include/efi and usr/lib gnu-efi
  GNU_EFI_CACHE optional package extraction cache used when gnu-efi is absent
  HYPERV_CMDLINE
                kernel command line embedded in the EFI loader

The output VHDX is generated in the build directory. Nothing is staged into the
source tree or committed to git. If gnu-efi is not installed, the builder
downloads the distro package into the build directory and uses it from there.
EOF
}

if [[ $# -lt 3 || $# -gt 4 ]]; then
    usage
    exit 2
fi

KERNEL_BIN=$1
ROOTFS_IMG=$2
OUT_VHDX=$3
SIZE_MB=${4:-0}

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
LOADER_SRC="${SCRIPT_DIR}/hyperv-efiloader.c"

for tool in gcc ld objcopy sgdisk mformat mmd mcopy qemu-img stat; do
    command -v "${tool}" >/dev/null ||
        { echo "make-hyperv-image: missing ${tool}" >&2; exit 1; }
done

[[ -f "${KERNEL_BIN}" ]] ||
    { echo "make-hyperv-image: kernel image not found: ${KERNEL_BIN}" >&2; exit 1; }
[[ -f "${ROOTFS_IMG}" ]] ||
    { echo "make-hyperv-image: rootfs image not found: ${ROOTFS_IMG}" >&2; exit 1; }

out_dir=$(dirname "${OUT_VHDX}")
mkdir -p "${out_dir}"
work="${out_dir}/hyperv-image-work"
mkdir -p "${work}"

find_gnu_efi() {
    local roots=()
    [[ -n "${GNU_EFI_ROOT:-}" ]] && roots+=("${GNU_EFI_ROOT}")
    [[ -n "${GNU_EFI_CACHE:-}" ]] && roots+=("${GNU_EFI_CACHE}")
    roots+=("${out_dir}/hyperv-gnu-efi-cache/root")
    roots+=("/")

    local root
    for root in "${roots[@]}"; do
        local inc="${root%/}/usr/include/efi"
        local lib
        for lib in "${root%/}/usr/lib" \
                   "${root%/}/usr/lib/x86_64-linux-gnu" \
                   "${root%/}/usr/lib64"; do
            if [[ -f "${inc}/efi.h" &&
                  -f "${inc}/x86_64/efibind.h" &&
                  -f "${lib}/crt0-efi-x86_64.o" &&
                  -f "${lib}/elf_x86_64_efi.lds" &&
                  -f "${lib}/libefi.a" &&
                  -f "${lib}/libgnuefi.a" ]]; then
                printf '%s\n%s\n' "${inc}" "${lib}"
                return 0
            fi
        done
    done

    return 1
}

download_gnu_efi() {
    local cache="${GNU_EFI_CACHE:-${out_dir}/hyperv-gnu-efi-cache}"
    local root="${cache}/root"
    local pkgdir="${cache}/packages"
    local deb

    if GNU_EFI_ROOT="${root}" find_gnu_efi >/dev/null; then
        return 0
    fi

    command -v apt-get >/dev/null ||
        { echo "make-hyperv-image: gnu-efi not found and apt-get unavailable" >&2; return 1; }
    command -v dpkg-deb >/dev/null ||
        { echo "make-hyperv-image: gnu-efi not found and dpkg-deb unavailable" >&2; return 1; }

    mkdir -p "${pkgdir}" "${root}"
    (
        cd "${pkgdir}"
        rm -f gnu-efi_*.deb
        apt-get download gnu-efi
    )
    deb=$(find "${pkgdir}" -maxdepth 1 -name 'gnu-efi_*.deb' -print -quit)
    if [[ -z "${deb}" ]]; then
        echo "make-hyperv-image: failed to download gnu-efi package" >&2
        return 1
    fi

    rm -rf "${root}"
    mkdir -p "${root}"
    dpkg-deb -x "${deb}" "${root}"
    GNU_EFI_CACHE="${root}" find_gnu_efi >/dev/null
}

mapfile -t _gnu_efi < <(find_gnu_efi)
if ((${#_gnu_efi[@]} < 2)); then
    echo "make-hyperv-image: gnu-efi not found; downloading package into ${out_dir}/hyperv-gnu-efi-cache" >&2
    if ! download_gnu_efi; then
        echo "make-hyperv-image: gnu-efi unavailable; install package 'gnu-efi' or set GNU_EFI_ROOT" >&2
        exit 1
    fi
    mapfile -t _gnu_efi < <(find_gnu_efi)
    if ((${#_gnu_efi[@]} < 2)); then
        echo "make-hyperv-image: downloaded gnu-efi package did not contain usable x86_64 EFI objects" >&2
        exit 1
    fi
fi
EFI_INC=${_gnu_efi[0]}
EFI_LIB=${_gnu_efi[1]}

c_string_escape() {
    sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

HYPERV_CMDLINE="${HYPERV_CMDLINE:-BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=0}"
HYPERV_CMDLINE_C=$(printf '%s' "${HYPERV_CMDLINE}" | c_string_escape)

file_size_mb() {
    local bytes
    bytes=$(stat -c '%s' "$1")
    echo $(((bytes + 1024 * 1024 - 1) / (1024 * 1024)))
}

loader_obj="${work}/hyperv-efiloader.o"
loader_so="${work}/hyperv-efiloader.so"
loader_efi="${work}/BOOTX64.EFI"
startup_nsh="${work}/startup.nsh"

gcc -I"${EFI_INC}" -I"${EFI_INC}/x86_64" \
    -fpic -fshort-wchar -mno-red-zone -DEFI_FUNCTION_WRAPPER \
    -DXV6_HYPERV_CMDLINE="\"${HYPERV_CMDLINE_C}\"" \
    -Wall -Wextra -Werror -c "${LOADER_SRC}" -o "${loader_obj}"
ld -nostdlib -znocombreloc -T "${EFI_LIB}/elf_x86_64_efi.lds" \
    -shared -Bsymbolic "${EFI_LIB}/crt0-efi-x86_64.o" "${loader_obj}" \
    -L"${EFI_LIB}" -lefi -lgnuefi -o "${loader_so}"
objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
    -j .rel -j .rela -j .reloc --target=efi-app-x86_64 \
    "${loader_so}" "${loader_efi}"

if [[ "${SIZE_MB}" -le 0 ]]; then
    rootfs_mb=$(file_size_mb "${ROOTFS_IMG}")
    kernel_mb=$(file_size_mb "${KERNEL_BIN}")
    SIZE_MB=$((rootfs_mb + kernel_mb + 256))
    if [[ "${SIZE_MB}" -lt 768 ]]; then
        SIZE_MB=768
    fi
fi

RAW="${OUT_VHDX%.vhdx}.raw"
ESP="${work}/esp.fat"
ESP_MB=128
rootfs_mb=$(file_size_mb "${ROOTFS_IMG}")
if [[ "${SIZE_MB}" -lt $((ESP_MB + rootfs_mb + 16)) ]]; then
    echo "make-hyperv-image: image size too small: ${SIZE_MB} MiB" >&2
    exit 1
fi

rm -f "${RAW}" "${ESP}" "${OUT_VHDX}"
truncate -s "${SIZE_MB}M" "${RAW}"
sgdisk --zap-all "${RAW}" >/dev/null
sgdisk -n 1:2048:+${ESP_MB}M -t 1:ef00 -c 1:ESP \
       -n 2:0:0 -t 2:8300 -c 2:rootfs "${RAW}" >/dev/null

truncate -s "${ESP_MB}M" "${ESP}"
mformat -i "${ESP}" -F -v XV6HYPERV ::
mmd -i "${ESP}" ::/EFI ::/EFI/BOOT
mcopy -i "${ESP}" "${loader_efi}" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "${ESP}" "${KERNEL_BIN}" ::/xv6.bin
printf 'FS0:\\EFI\\BOOT\\BOOTX64.EFI\r\n' > "${startup_nsh}"
mcopy -i "${ESP}" "${startup_nsh}" ::/startup.nsh

dd if="${ESP}" of="${RAW}" bs=1M seek=1 conv=notrunc status=none
rootfs_start=$(sgdisk -i 2 "${RAW}" | awk '/First sector:/ {print $3}')
if [[ -z "${rootfs_start}" ]]; then
    echo "make-hyperv-image: failed to locate rootfs partition start" >&2
    exit 1
fi
dd if="${ROOTFS_IMG}" of="${RAW}" bs=512 seek="${rootfs_start}" \
   conv=notrunc status=none
qemu-img convert -f raw -O vhdx -o subformat=dynamic "${RAW}" "${OUT_VHDX}"

echo "make-hyperv-image: wrote ${OUT_VHDX} (${SIZE_MB} MiB VHDX, rootfs partition at sector ${rootfs_start})"
