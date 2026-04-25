#!/usr/bin/env bash
# make-rootfs.sh - build an ext4 root filesystem image from the cross-installed
# sysroot.  The kernel mounts this image as the root filesystem on virtio-blk
# (root=/dev/disk0).
#
# Usage:
#   make-rootfs.sh <sysroot_dir> <out_img> [size_mb] [musl_libdir]
#
# The sysroot is expected to be the install tree produced by `cmake --build user
# --target install`, i.e. it contains bin/_<progname> binaries.  The leading
# underscore is stripped when staging into the image (xv6 convention).
set -euo pipefail

SYSROOT="${1:?usage: $0 <sysroot_dir> <out_img> [size_mb] [musl_libdir]}"
OUT="${2:?usage: $0 <sysroot_dir> <out_img> [size_mb] [musl_libdir]}"
SIZE_MB="${3:-64}"
MUSL_LIBDIR="${4:-}"

if [[ ! -d "${SYSROOT}/bin" ]]; then
    echo "make-rootfs: ${SYSROOT}/bin not found - did you run 'cmake --build user --target install'?" >&2
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
# 2. Any other files in bin/ (no _ prefix) - copy verbatim, e.g. python3.12
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

# 6. Stage the musl dynamic linker / libc into /lib so dynamically linked
#    binaries (vim, python3, netsurf, desktop, ...) can exec.  musl ships a
#    single ELF that serves as both ld.so and libc.so; install it under its
#    canonical PT_INTERP name and symlink libc.so to it.
if [[ -n "${MUSL_LIBDIR}" && -d "${MUSL_LIBDIR}" ]]; then
    shopt -s nullglob
    # libc.so is the actual ELF; ld-musl-<arch>.so.1 is a symlink to it.
    # Copy libc.so first, then materialize the canonical PT_INTERP name as
    # a real file (cp -L on the symlink) so the loader exists in the image
    # even when /lib symlinks aren't resolvable at exec time.
    if [[ -f "${MUSL_LIBDIR}/libc.so" ]]; then
        cp -a "${MUSL_LIBDIR}/libc.so" "${STAGE}/lib/libc.so"
    fi
    for ld in "${MUSL_LIBDIR}"/ld-musl-*.so.1; do
        cp -L "$ld" "${STAGE}/lib/$(basename "$ld")"
        chmod 0755 "${STAGE}/lib/$(basename "$ld")"
    done
    # Stage gcc runtime libs (libatomic, libgcc_s, libstdc++) needed by
    # dynamically linked C/C++ binaries like netsurf.  These live in
    # ${triple}/lib64 (sibling of MUSL_LIBDIR) in our toolchain layout.
    _gcc_libdir="$(dirname "${MUSL_LIBDIR}")/lib64"
    if [[ -d "${_gcc_libdir}" ]]; then
        for so in "${_gcc_libdir}"/libatomic.so* \
                  "${_gcc_libdir}"/libgcc_s.so* \
                  "${_gcc_libdir}"/libstdc++.so*; do
            cp -a "$so" "${STAGE}/lib/$(basename "$so")"
        done
    fi
    shopt -u nullglob
fi

rm -f "${OUT}"
truncate -s "${SIZE_MB}M" "${OUT}"
mkfs.ext4 -F -L xv6root -d "${STAGE}" "${OUT}" >/dev/null

echo "make-rootfs: wrote ${OUT} (${SIZE_MB} MiB ext4, label=xv6root) from ${SYSROOT}"
