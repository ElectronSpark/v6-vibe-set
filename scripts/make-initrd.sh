#!/usr/bin/env bash
# make-initrd.sh <sysroot> <out.cpio.gz>
#
# Build a cpio.gz initrd from the cross-installed sysroot. Excludes
# headers / static libs / pkgconfig / locale data — runtime only.
set -euo pipefail

if [[ $# -ne 2 ]]; then
	echo "usage: $0 <sysroot> <out.cpio.gz>" >&2
	exit 1
fi
SYSROOT="$1"
OUT="$2"

if [[ ! -d "${SYSROOT}/usr/bin" ]]; then
	echo "no userland in ${SYSROOT}/usr/bin — did user/ports build?" >&2
	exit 2
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

# Layout the runtime view of the sysroot.
mkdir -p "${STAGE}"/{bin,sbin,lib,usr,etc,proc,sys,dev,tmp,var,root}
cp -a "${SYSROOT}/usr/bin"  "${STAGE}/usr/" 2>/dev/null || true
cp -a "${SYSROOT}/usr/sbin" "${STAGE}/usr/" 2>/dev/null || true
cp -a "${SYSROOT}/usr/lib"  "${STAGE}/usr/" 2>/dev/null || true
# Make /lib + /bin point at /usr/* (merged-/usr layout).
ln -sfn usr/bin "${STAGE}/bin"
ln -sfn usr/lib "${STAGE}/lib"

# Strip development cruft.
rm -rf "${STAGE}/usr/lib/pkgconfig"  || true
rm -rf "${STAGE}/usr/include"        || true
find "${STAGE}/usr/lib" -name '*.a' -delete 2>/dev/null || true
find "${STAGE}/usr/lib" -name '*.la' -delete 2>/dev/null || true

# /init -> /usr/bin/init (or /sbin/init).
if [[ -x "${STAGE}/usr/bin/init" ]]; then
	ln -sfn usr/bin/init "${STAGE}/init"
else
	echo "warning: ${STAGE}/usr/bin/init missing; initrd will not boot" >&2
fi

(cd "${STAGE}" && find . -print0 | cpio --null -o --format=newc) \
	| gzip -9 > "${OUT}"

echo "[initrd] $(du -h "${OUT}" | cut -f1)  ${OUT}"
