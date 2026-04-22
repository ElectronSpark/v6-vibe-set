#!/usr/bin/env bash
# make-rootfs.sh — build an ext4 root filesystem image from the cross-installed
# sysroot.  The kernel mounts this image as the root filesystem on virtio-blk
# (root=/dev/disk0).
#
# Usage:
#   make-rootfs.sh <sysroot_dir> <out_img> [size_mb]
#
# The sysroot is expected to be the install tree produced by `cmake --build user
# --target install`, i.e. it contains bin/_<progname> binaries.  The leading
# underscore is stripped when staging into the image (xv6 convention).
set -euo pipefail

SYSROOT="${1:?usage: $0 <sysroot_dir> <out_img> [size_mb]}"
OUT="${2:?usage: $0 <sysroot_dir> <out_img> [size_mb]}"
SIZE_MB="${3:-64}"

if [[ ! -d "${SYSROOT}/bin" ]]; then
    echo "make-rootfs: ${SYSROOT}/bin not found — did you run 'cmake --build user --target install'?" >&2
    exit 1
fi

command -v mkfs.ext4 >/dev/null || { echo "make-rootfs: mkfs.ext4 not found (install e2fsprogs)" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

mkdir -p "${STAGE}"/{bin,dev,proc,sys,tmp,etc,root}

# Copy user binaries, stripping the xv6 "_" prefix on the way in.
shopt -s nullglob
for f in "${SYSROOT}/bin"/_*; do
    base="$(basename "$f")"
    cp "$f" "${STAGE}/bin/${base#_}"
done
shopt -u nullglob

rm -f "${OUT}"
truncate -s "${SIZE_MB}M" "${OUT}"
mkfs.ext4 -F -L xv6root -d "${STAGE}" "${OUT}" >/dev/null

echo "make-rootfs: wrote ${OUT} (${SIZE_MB} MiB ext4, label=xv6root) from ${SYSROOT}"
