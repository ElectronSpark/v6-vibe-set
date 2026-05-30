#!/usr/bin/env bash
# make-image.sh <kernel.elf> <initrd.cpio.gz> <out.img>
#
# Stub: produces a tiny image-descriptor blob that bundles paths for
# qemu's -kernel + -initrd. A real disk image (GPT + ext-like fs +
# bootloader) is project-specific; replace this stub when you decide
# on a boot model. Until then, run-qemu.sh consumes kernel + initrd
# directly and this file is just a marker.
set -euo pipefail

if [[ $# -ne 3 ]]; then
	echo "usage: $0 <kernel.elf> <initrd.cpio.gz> <out.img>" >&2
	exit 1
fi
KERNEL="$1"; INITRD="$2"; OUT="$3"

cat > "${OUT}" <<EOF
# xv6-os boot descriptor (placeholder; not a real disk image)
kernel=${KERNEL}
initrd=${INITRD}
EOF
echo "[image] descriptor written: ${OUT}"
