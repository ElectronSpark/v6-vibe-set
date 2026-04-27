#!/usr/bin/env bash
# stage-wayland.sh - pragmatic staging from a prebuilt reference sysroot.
#
# Copies the prebuilt wlcomp Wayland compositor and desktop session
# manager binaries from a reference sysroot into ${DEST_SYSROOT}.
#
# wlcomp is statically linked against libwayland-server (.a) but
# dynamically linked against libffi.so.7 + libc.so. Those dynamic
# deps are already staged by the cpython port.
#
# Kernel side is already in place in xv6-os:
#   /dev/fb0   (fb.c, virtio-gpu / VGA / EFI fb)
#   /dev/kbd   (ps2kbd.c)
#   /dev/mouse (ps2mouse.c)
#
# Usage:  stage-wayland.sh <ref_sysroot> <dest_sysroot>
# Or set REF_SYSROOT=<ref_sysroot> and pass only <dest_sysroot>.
set -euo pipefail

if [[ $# -ge 2 ]]; then
    REF="$1"
    DEST="$2"
else
    REF="${REF_SYSROOT:-}"
    DEST="${1:-}"
fi
if [[ -z "${REF}" || -z "${DEST}" ]]; then
    echo "usage: $0 <ref_sysroot> <dest_sysroot>  (or REF_SYSROOT=<ref> $0 <dest>)" >&2
    exit 1
fi

if [[ ! -x "${REF}/bin/wlcomp" ]]; then
    echo "stage-wayland: ${REF}/bin/wlcomp not found" >&2
    exit 1
fi
if [[ ! -x "${REF}/bin/desktop" ]]; then
    echo "stage-wayland: ${REF}/bin/desktop not found" >&2
    exit 1
fi

install -d "${DEST}/bin" "${DEST}/lib"
install -m 0755 "${REF}/bin/wlcomp"  "${DEST}/bin/wlcomp"
install -m 0755 "${REF}/bin/desktop" "${DEST}/bin/desktop"

# wlcomp has DT_NEEDED libffi.so.7. Stage it (and any sonames it follows)
# from the reference sysroot so the runtime loader can resolve it.
shopt -s nullglob
for f in "${REF}/lib"/libffi.so*; do
    cp -a "$f" "${DEST}/lib/$(basename "$f")"
done
shopt -u nullglob

echo "stage-wayland: installed wlcomp + desktop into ${DEST}/bin"
