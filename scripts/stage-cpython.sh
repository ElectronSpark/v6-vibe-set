#!/usr/bin/env bash
# stage-cpython.sh — stage a prebuilt CPython 3.12 + Flask/SQLite stack
# from a reference sysroot into our build sysroot.
#
# This is a pragmatic shortcut: the reference tree (xv6-tmp/build-x86/sysroot)
# was built by the older monolithic xv6-tmp build with the same
# x86_64-xv6-linux-musl toolchain we use today, so the binaries are ABI
# compatible. The proper "build CPython under ports/" recipe is still TODO.
#
# Usage:
#   stage-cpython.sh [reference_sysroot] [dest_sysroot] [phase2_lib_dir]
#
# Defaults assume invocation from the xv6-os/ repo root.
set -euo pipefail

REF="${1:-/home/es/xv6/xv6-tmp/build-x86/sysroot}"
DST="${2:-build-x86_64/sysroot}"
PHASE2_LIB="${3:-build-toolchain-x86_64/x86_64/phase2/x86_64-xv6-linux-musl/lib}"

if [[ ! -d "${REF}/lib/python3.12" ]]; then
    echo "stage-cpython: reference sysroot missing python3.12 at ${REF}" >&2
    exit 1
fi
if [[ ! -f "${PHASE2_LIB}/libc.so" ]]; then
    echo "stage-cpython: musl libc.so not found at ${PHASE2_LIB}" >&2
    exit 1
fi
command -v rsync >/dev/null || { echo "stage-cpython: rsync required" >&2; exit 1; }

mkdir -p "${DST}"/{bin,lib,etc,share}

# 1. musl loader + libc from our phase2 toolchain (single file, two names).
cp -L "${PHASE2_LIB}/libc.so" "${DST}/lib/libc.so"
ln -sfn libc.so "${DST}/lib/ld-musl-x86_64.so.1"

# 2. Runtime shared libraries from reference sysroot.
runtime_libs=(
    libpython3.12.so.1.0
    libpython3.so
    libgcc_s.so.1
    libreadline.so.8.2
    libncurses.so.6.4
    libstdc++.so.6.0.33
    libffi.so.7.1.0
)
for so in "${runtime_libs[@]}"; do
    cp -a "${REF}/lib/${so}" "${DST}/lib/"
done

# Recreate canonical sonames as symlinks (matching what the dynamic
# linker resolves NEEDED entries to).
ln -sfn libpython3.12.so.1.0 "${DST}/lib/libpython3.12.so"
ln -sfn libgcc_s.so.1        "${DST}/lib/libgcc_s.so"
ln -sfn libreadline.so.8.2   "${DST}/lib/libreadline.so.8"
ln -sfn libreadline.so.8.2   "${DST}/lib/libreadline.so"
ln -sfn libncurses.so.6.4    "${DST}/lib/libncurses.so.6"
ln -sfn libstdc++.so.6.0.33  "${DST}/lib/libstdc++.so.6"
ln -sfn libstdc++.so.6.0.33  "${DST}/lib/libstdc++.so"
ln -sfn libffi.so.7.1.0      "${DST}/lib/libffi.so.7"
ln -sfn libffi.so.7.1.0      "${DST}/lib/libffi.so"

# 3. Python stdlib + site-packages.
#    Drop debug ABI variants (-312d-*.so), bytecode caches and the
#    cpython test suite — they roughly halve the payload.
rsync -aH \
      --delete \
      --exclude='*-312d-*.so' \
      --exclude='__pycache__/' \
      --exclude='/test/' \
      --exclude='/tests/' \
      --exclude='idlelib/' \
      --exclude='turtledemo/' \
      "${REF}/lib/python3.12/" "${DST}/lib/python3.12/"

# 3b. Python stdlib (.py source) — the reference sysroot only ships
#     lib-dynload + site-packages, not the pure-Python stdlib. We pull
#     it from the cpython source tree that the reference build used.
CPYTHON_LIB="${CPYTHON_LIB:-/home/es/xv6/xv6-tmp/user/v6-cpython/Lib}"
if [[ -d "${CPYTHON_LIB}" ]]; then
    rsync -a \
        --exclude='__pycache__/' \
        --exclude='test/' \
        --exclude='tests/' \
        --exclude='idlelib/' \
        --exclude='turtledemo/' \
        --exclude='tkinter/' \
        --exclude='ensurepip/' \
        "${CPYTHON_LIB}/" "${DST}/lib/python3.12/"
else
    echo "stage-cpython: WARNING: cpython source Lib not found at ${CPYTHON_LIB}; stdlib *.py will be missing" >&2
fi

# 4. Interpreter binary (+ canonical 'python' symlink).
cp -a "${REF}/bin/python3.12" "${DST}/bin/python3.12"
ln -sfn python3.12 "${DST}/bin/python"

# 5. Terminfo so readline/ncurses can render arrow keys at the prompt.
if [[ -d "${REF}/share/terminfo" ]]; then
    rsync -aH "${REF}/share/terminfo/" "${DST}/share/terminfo/"
fi

# 6. Minimal /etc — Python's site.py reads /etc/python3.12 if present,
#    and Flask/Werkzeug expect /etc/mime.types to exist (warns otherwise).
:> "${DST}/etc/mime.types.placeholder"

echo "stage-cpython: done."
du -sh "${DST}/lib/python3.12" "${DST}/lib/libpython3.12.so.1.0" "${DST}/bin/python3.12" 2>/dev/null
