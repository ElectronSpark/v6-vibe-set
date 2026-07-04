#!/usr/bin/env bash
# Stage redistributable Game Boy smoke ROMs into a generated rootfs overlay.
set -euo pipefail

OVERLAY="${1:?usage: $0 <overlay>}"
BUILD_DIR="${GAMEBOY_ROM_BUILD_DIR:-build-x86_64/gameboy-roms}"
DEST="${OVERLAY}/root/roms"

DMG_ACID2_URL="${GAMEBOY_DMG_ACID2_URL:-https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb}"
DMG_ACID2_SHA256="${GAMEBOY_DMG_ACID2_SHA256:-464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1}"

mkdir -p "${BUILD_DIR}" "${DEST}"

download() {
    local url="$1"
    local out="$2"

    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --show-error --silent -o "${out}.tmp" "${url}"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "${out}.tmp" "${url}"
    else
        echo "stage-gameboy-roms: curl or wget is required to download ${url}" >&2
        return 1
    fi
    mv "${out}.tmp" "${out}"
}

stage_dmg_acid2() {
    local rom="${BUILD_DIR}/dmg-acid2.gb"
    local actual

    if [[ ! -f "${rom}" ]]; then
        download "${DMG_ACID2_URL}" "${rom}"
    fi

    actual="$(sha256sum "${rom}" | awk '{print $1}')"
    if [[ "${actual}" != "${DMG_ACID2_SHA256}" ]]; then
        echo "stage-gameboy-roms: ${rom} sha256 mismatch" >&2
        echo "  expected ${DMG_ACID2_SHA256}" >&2
        echo "  actual   ${actual}" >&2
        rm -f "${rom}"
        return 1
    fi

    cp -a "${rom}" "${DEST}/dmg-acid2.gb"
}

stage_dmg_acid2
