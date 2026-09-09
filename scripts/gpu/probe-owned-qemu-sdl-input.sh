#!/usr/bin/env bash
# Move/click the exact owned QEMU SDL viewport through the real Win32 cursor.
set -euo pipefail

if [[ $# -ne 5 ]]; then
        echo "usage: $0 <exact-window-title> <guest-width> <guest-height> <surface-width> <surface-height>" >&2
        exit 2
fi
title=$1
guest_width=$2
guest_height=$3
surface_width=$4
surface_height=$5
case "${guest_width}:${guest_height}:${surface_width}:${surface_height}" in
        *[!0-9:]*|:*|*:)
                echo "probe-owned-qemu-sdl-input: invalid guest geometry" >&2
                exit 2
                ;;
esac
if ! command -v powershell.exe >/dev/null 2>&1 ||
   ! command -v wslpath >/dev/null 2>&1; then
        echo "status=SKIP reason=no-powershell"
        exit 77
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ps1_win=$(wslpath -w "${script_dir}/probe-owned-qemu-sdl-input.ps1")
wslenv="XV6_QEMU_WINDOW_TITLE:XV6_GUEST_WIDTH:XV6_GUEST_HEIGHT:XV6_SURFACE_WIDTH:XV6_SURFACE_HEIGHT"
if [[ -n "${WSLENV:-}" ]]; then
        wslenv="${wslenv}:${WSLENV}"
fi
XV6_QEMU_WINDOW_TITLE="${title}" \
XV6_GUEST_WIDTH="${guest_width}" \
XV6_GUEST_HEIGHT="${guest_height}" \
XV6_SURFACE_WIDTH="${surface_width}" \
XV6_SURFACE_HEIGHT="${surface_height}" \
WSLENV="${wslenv}" \
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${ps1_win}"
