#!/usr/bin/env bash
# Build the repo-owned kwin_wayland DRM ioctl-timing LD_PRELOAD shim from
# repo-owned sources, deterministically, with a binary proof file.
#
# Modeled on scripts/image/build-qtmm-shim.sh. The qtmm shim is a -nostdlib
# SONAME stand-in that must have ZERO DT_NEEDED; this shim is different in kind
# -- it is a genuine LD_PRELOAD interposer that calls libc (dlsym, clock_gettime,
# write, strtoull, vsnprintf) and dlfcn (dlsym/RTLD_NEXT). It therefore links
# libc and libdl instead of using -nostdlib/-nostartfiles/-nodefaultlibs, and
# has no --version-script / -soname (an LD_PRELOAD object needs neither an
# exported-symbol allowlist nor a SONAME; the loader preloads it by path). All
# determinism flags carry over unchanged: SOURCE_DATE_EPOCH=0, -fno-ident,
# -Wl,--build-id=none, ffile-prefix-map. The forbidden-DT_NEEDED check is
# inverted accordingly: here libc/libdl are REQUIRED and anything heavier
# (Qt/KF5/drm/pulse) is forbidden.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${1:-${REPO_ROOT}/build-x86_64/kwin-ioctl-trace}"
CC_BIN="${CC:-cc}"
SRC="${SCRIPT_DIR}/kwin-ioctl-trace-preload.c"
OUT="${OUT_DIR}/kwin-ioctl-trace-preload.so"
PROOF="${OUT_DIR}/kwin-ioctl-trace-binary-proof.txt"
CMD_FILE="${OUT_DIR}/build-command.txt"

mkdir -p "${OUT_DIR}"
cp -a "${SRC}" "${OUT_DIR}/kwin-ioctl-trace-preload.c"

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
    -Wl,--build-id=none
    -o "${OUT}"
    "${SRC}"
    -ldl
)

printf '%q ' "${cmd[@]}" >"${CMD_FILE}"
printf '\n' >>"${CMD_FILE}"
SOURCE_DATE_EPOCH=0 "${cmd[@]}"
chmod 0755 "${OUT}"

{
    echo "# kwin ioctl-trace shim binary proof"
    echo "source=${SRC}"
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
    echo "## interposer symbols (defined, dynamic)"
    nm -D --defined-only "${OUT}" | grep -E ' (open|open64|openat|openat64|ioctl|read|close)$' || true
    echo
    echo "## NEEDED entries"
    if readelf -d "${OUT}" | grep -q '(NEEDED)'; then
        readelf -d "${OUT}" | grep '(NEEDED)'
    else
        echo "no DT_NEEDED entries"
    fi
} >"${PROOF}"

# An LD_PRELOAD interposer must export the wrappers it interposes.
for sym in open openat ioctl read close; do
    if ! nm -D --defined-only "${OUT}" | grep -qE " ${sym}\$"; then
        echo "build-kwin-ioctl-trace: missing interposer symbol '${sym}' in ${OUT}" >&2
        nm -D --defined-only "${OUT}" >&2
        exit 1
    fi
done

# Forbidden DT_NEEDED: this shim may depend ONLY on libc, libdl (merged into
# libc on glibc >= 2.34), and the dynamic linker itself. Any Qt/KF5/drm/pulse/
# glib dependency means it accidentally pulled in a real toolkit and would
# perturb the very launch path it is meant to observe.
if readelf -d "${OUT}" |
    grep '(NEEDED)' |
    grep -Ev 'libc\.so|libdl\.so|ld-linux' >/dev/null; then
    echo "build-kwin-ioctl-trace: forbidden DT_NEEDED entry in ${OUT}" >&2
    readelf -d "${OUT}" | grep '(NEEDED)' >&2
    exit 1
fi

echo "build-kwin-ioctl-trace: output=${OUT}"
echo "build-kwin-ioctl-trace: proof=${PROOF}"
