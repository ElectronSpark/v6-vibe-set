#!/usr/bin/env bash
# Build the opt-in Qt5Multimedia launch probe shim from repo-owned sources.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${1:-${REPO_ROOT}/build-x86_64/qtmm-shim}"
CC_BIN="${CC:-cc}"
SRC="${SCRIPT_DIR}/xv6-qtmm-shim.c"
MAP="${SCRIPT_DIR}/xv6-qtmm-shim.map"
OUT="${OUT_DIR}/libQt5Multimedia.so.5"
PROOF="${OUT_DIR}/qtmm-shim-binary-proof.txt"
CMD_FILE="${OUT_DIR}/build-command.txt"

mkdir -p "${OUT_DIR}"
cp -a "${SRC}" "${OUT_DIR}/xv6-qtmm-shim.c"
cp -a "${MAP}" "${OUT_DIR}/xv6-qtmm-shim.map"

cmd=(
    "${CC_BIN}"
    -O2
    -Wall
    -Wextra
    -fPIC
    -fno-asynchronous-unwind-tables
    -fno-unwind-tables
    -fno-ident
    "-ffile-prefix-map=${REPO_ROOT}=."
    -shared
    -nostdlib
    -nostartfiles
    -nodefaultlibs
    -Wl,--build-id=none
    -Wl,--no-undefined
    -Wl,-soname,libQt5Multimedia.so.5
    "-Wl,--version-script,${MAP}"
    -o "${OUT}"
    "${SRC}"
)

printf '%q ' "${cmd[@]}" >"${CMD_FILE}"
printf '\n' >>"${CMD_FILE}"
SOURCE_DATE_EPOCH=0 "${cmd[@]}"
chmod 0755 "${OUT}"

{
    echo "# Qt5Multimedia shim binary proof"
    echo "source=${SRC}"
    echo "version_script=${MAP}"
    echo "output=${OUT}"
    echo
    echo "## build command"
    cat "${CMD_FILE}"
    echo
    echo "## sha256"
    sha256sum "${OUT}"
    echo
    echo "## dynamic section"
    readelf -d "${OUT}"
    echo
    echo "## version info"
    readelf -V "${OUT}"
    echo
    echo "## defined dynamic symbols"
    nm -D --defined-only --demangle "${OUT}"
    echo
    echo "## NEEDED entries"
    if readelf -d "${OUT}" | grep -q '(NEEDED)'; then
        readelf -d "${OUT}" | grep '(NEEDED)'
    else
        echo "no DT_NEEDED entries"
    fi
} >"${PROOF}"

if readelf -d "${OUT}" |
    grep '(NEEDED)' |
    grep -E 'libQt5Multimedia|libpulse|pulsecommon' >/dev/null; then
    echo "build-qtmm-shim: forbidden DT_NEEDED entry in ${OUT}" >&2
    readelf -d "${OUT}" >&2
    exit 1
fi
if ! readelf -d "${OUT}" | grep -q 'Library soname: \[libQt5Multimedia.so.5\]'; then
    echo "build-qtmm-shim: missing libQt5Multimedia.so.5 SONAME" >&2
    exit 1
fi

echo "build-qtmm-shim: output=${OUT}"
echo "build-qtmm-shim: proof=${PROOF}"
