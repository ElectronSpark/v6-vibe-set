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
command -v rsync    >/dev/null || { echo "make-rootfs: rsync not found"               >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

mkdir -p "${STAGE}"/{bin,dev,proc,sys,tmp,etc,root,lib,usr,share}

# 1. xv6-style user binaries: bin/_<name> -> /bin/<name>
shopt -s nullglob
for f in "${SYSROOT}/bin"/_*; do
    base="$(basename "$f")"
    cp "$f" "${STAGE}/bin/${base#_}"
done
# 2. Any other files in bin/ (no _ prefix) — copy verbatim, e.g. python3.12
for f in "${SYSROOT}/bin"/*; do
    base="$(basename "$f")"
    [[ "${base}" == _* ]] && continue
    cp -a "$f" "${STAGE}/bin/${base}"
done
shopt -u nullglob

# 3. Mirror dynamic-linker tree subdirs verbatim if present, preserving
#    symlinks/perms (these hold musl loader, libpython, stdlib, etc.).
for sub in lib usr share etc; do
    if [[ -d "${SYSROOT}/${sub}" ]]; then
        rsync -aH "${SYSROOT}/${sub}/" "${STAGE}/${sub}/"
    fi
done

# 4. Copy top-level regular files in sysroot/ (e.g. diag.py, test_flask.py)
shopt -s nullglob
for f in "${SYSROOT}"/*; do
    [[ -f "$f" ]] || continue
    cp -a "$f" "${STAGE}/$(basename "$f")"
done
shopt -u nullglob

# 5. Apply the in-tree rootfs overlay (e.g. /etc/startup) on top of
#    everything staged so far. Path is resolved relative to this script
#    so it works regardless of the caller's cwd.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
OVERLAY="${SCRIPT_DIR}/../rootfs-overlay"
if [[ -d "${OVERLAY}" ]]; then
    rsync -aH "${OVERLAY}/" "${STAGE}/"
fi

rm -f "${OUT}"
truncate -s "${SIZE_MB}M" "${OUT}"
mkfs.ext4 -F -L xv6root -d "${STAGE}" "${OUT}" >/dev/null

echo "make-rootfs: wrote ${OUT} (${SIZE_MB} MiB ext4, label=xv6root) from ${SYSROOT}"
