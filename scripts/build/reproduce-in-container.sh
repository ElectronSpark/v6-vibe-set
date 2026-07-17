#!/usr/bin/env bash
# Clean, one-command container reproduction of the current workspace.
# Produces one mutable build tree plus at most three immutable receipts.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
ARCH="${XV6_ARCH:-x86_64}"
JOBS="${XV6_PARALLEL_JOBS:-2}"
KEEP="${XV6_KEEP_ITERATIONS:-3}"
BUILD="${XV6_BUILD_DIR:-${ROOT}/build-${ARCH}}"
RECEIPT_ROOT="${XV6_REPRODUCTION_ROOT:-${ROOT}/build-reproductions/${ARCH}}"
QEMU_CACHE="${XV6_QEMU_SDL_CACHE:-${ROOT}/build-qemu-sdl-cache}"
KDE_CACHE="${XV6_KDE_PACKAGE_CACHE:-${ROOT}/build-package-cache/kde-noble}"
CHROME_VERSION="151.0.7922.34"
CHROME_URL="https://storage.googleapis.com/chrome-for-testing-public/${CHROME_VERSION}/linux64/chrome-linux64.zip"
CHROME_SHA256="ae8736ac28bc69278551500f219fc749575648263c43ec5990749eff43b9fcf8"
CHROME_CACHE="${ROOT}/build-package-cache/chrome-for-testing"
CHROME_ARCHIVE="${CHROME_CACHE}/chrome-linux64-${CHROME_VERSION}.zip"

[[ "${ARCH}" == "x86_64" ]] || {
    echo "reproduce-in-container: only x86_64 is supported" >&2
    exit 2
}
[[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || {
    echo "reproduce-in-container: XV6_PARALLEL_JOBS must be positive" >&2
    exit 2
}
[[ "${KEEP}" =~ ^[23]$ ]] || {
    echo "reproduce-in-container: XV6_KEEP_ITERATIONS must be 2 or 3 so a rollback survives a clean rebuild" >&2
    exit 2
}
if [[ ! -e /.dockerenv && "${XV6_IN_CONTAINER:-0}" != "1" ]]; then
    echo "reproduce-in-container: run through scripts/container/reproduce-workspace.sh" >&2
    exit 2
fi

case "$(realpath -m -- "${BUILD}")" in
    "${ROOT}"/build-*) ;;
    *)
        echo "reproduce-in-container: refusing build directory outside ${ROOT}/build-*" >&2
        exit 2
        ;;
esac
[[ ! -L "${BUILD}" ]] || {
    echo "reproduce-in-container: refusing symlink build directory ${BUILD}" >&2
    exit 2
}

mkdir -p -- "${RECEIPT_ROOT}"
if [[ "${XV6_REPRODUCTION_LOCK_HELD:-0}" != "1" ]]; then
    exec 9>"${RECEIPT_ROOT}/.reproduction.lock"
    flock -n 9 || {
        echo "reproduce-in-container: another reproduction is active" >&2
        exit 75
    }
fi

source_epoch="${SOURCE_DATE_EPOCH:-$(git -C "${ROOT}" log -1 --format=%ct)}"
export SOURCE_DATE_EPOCH="${source_epoch}"
build_id="$(date -u +%Y%m%dT%H%M%SZ)-$(git -C "${ROOT}" rev-parse --short=12 HEAD)-$$"
failed_log="${RECEIPT_ROOT}/last-failed-build.log"
build_started=0

cleanup_failure() {
    rc=$?
    if [[ "${rc}" -ne 0 && "${build_started}" == "1" ]]; then
        if [[ -f "${BUILD}/reproduction-build.log" ]]; then
            cp -- "${BUILD}/reproduction-build.log" "${failed_log}" 2>/dev/null || true
        fi
        if [[ "${XV6_KEEP_FAILED_BUILD:-0}" != "1" ]] && \
           "${ROOT}/scripts/launch/assert-qemu-path-unused.sh" "${BUILD}" >/dev/null 2>&1; then
            rm -rf --one-file-system -- "${BUILD}"
        fi
    fi
    exit "${rc}"
}
trap cleanup_failure EXIT

write_source_state() {
    local out="$1"
    {
        printf 'source_root=%s\n' "${ROOT}"
        printf 'source_date_epoch=%s\n' "${source_epoch}"
        printf 'top_commit=%s\n' "$(git -C "${ROOT}" rev-parse HEAD)"
        printf 'kernel_commit=%s\n' "$(git -C "${ROOT}/kernel" rev-parse HEAD)"
        printf 'user_commit=%s\n' "$(git -C "${ROOT}/user" rev-parse HEAD)"
        printf 'ports_commit=%s\n' "$(git -C "${ROOT}/ports" rev-parse HEAD)"
        printf 'container_image=%s\n' "${XV6_CONTAINER_IMAGE_ID:-unknown}"
        printf 'architecture=%s\nparallel_jobs=%s\n' "${ARCH}" "${JOBS}"
        printf 'cmake=%s\n' "$(cmake --version | sed -n '1p')"
        printf 'ninja=%s\n' "$(ninja --version)"
        printf 'compiler=%s\n' "$(cc --version | sed -n '1p')"
        printf 'qemu_sdl_version=9.0.2\n'
        printf 'qemu_sdl_patch_sha256=%s\n' \
            "$(sha256sum "${ROOT}/patches/qemu-9.0.2-sdl-aspect-input.patch" | awk '{print $1}')"
        printf '\n[submodules]\n'
        git -C "${ROOT}" submodule status --recursive
        printf '\n[top-status]\n'
        git -C "${ROOT}" status --porcelain=v1 --untracked-files=all
        printf '\n[kernel-status]\n'
        git -C "${ROOT}/kernel" status --porcelain=v1 --untracked-files=all
        printf '\n[user-status]\n'
        git -C "${ROOT}/user" status --porcelain=v1 --untracked-files=all
        printf '\n[ports-status]\n'
        git -C "${ROOT}/ports" status --porcelain=v1 --untracked-files=all
    } >"${out}"
}

write_workspace_hashes() {
    local out="$1"
    local label repo path digest target

    : >"${out}"
    for entry in \
        "top:${ROOT}" \
        "kernel:${ROOT}/kernel" \
        "user:${ROOT}/user" \
        "ports:${ROOT}/ports"; do
        label="${entry%%:*}"
        repo="${entry#*:}"
        while IFS= read -r -d '' path; do
            [[ ! -d "${repo}/${path}" ]] || continue
            if [[ -L "${repo}/${path}" ]]; then
                target="$(readlink -- "${repo}/${path}")"
                digest="$(printf '%s' "${target}" | sha256sum | awk '{print $1}')"
            elif [[ -f "${repo}/${path}" ]]; then
                digest="$(sha256sum -- "${repo}/${path}" | awk '{print $1}')"
            else
                continue
            fi
            printf '%s  %s/%s\n' "${digest}" "${label}" "${path}" >>"${out}"
        done < <(git -C "${repo}" ls-files -z --cached --others --exclude-standard)
    done
}

seed_kde_cache() {
    local source_archives="${BUILD}/kde-noble-plasma/archives"
    local source_order="${BUILD}/kde-noble-plasma/packages.apt-order.txt"
    local archive target

    mkdir -p -- "${KDE_CACHE}/archives"
    if [[ ! -s "${KDE_CACHE}/archives.sha256" && -d "${source_archives}" ]]; then
        for archive in "${source_archives}"/*.deb; do
            [[ -f "${archive}" ]] || continue
            target="${KDE_CACHE}/archives/${archive##*/}"
            [[ -e "${target}" ]] || ln -- "${archive}" "${target}"
        done
    fi
    if [[ ! -s "${KDE_CACHE}/packages.apt-order.txt" && -s "${source_order}" ]]; then
        cp -- "${source_order}" "${KDE_CACHE}/packages.apt-order.txt"
    fi
    [[ -s "${KDE_CACHE}/packages.apt-order.txt" ]] || {
        echo "reproduce-in-container: cannot lock KDE package order; no validated order file is available" >&2
        exit 1
    }
    if [[ ! -s "${KDE_CACHE}/archives.sha256" ]]; then
        (
            cd "${KDE_CACHE}/archives"
            sha256sum -- *.deb >"${KDE_CACHE}/archives.sha256"
        )
    fi
    shopt -s nullglob
    cached_archives=("${KDE_CACHE}/archives"/*.deb)
    shopt -u nullglob
    lock_count="$(awk 'NF >= 2 { count++ } END { print count + 0 }' \
        "${KDE_CACHE}/archives.sha256")"
    if (( ${#cached_archives[@]} != lock_count )); then
        echo "reproduce-in-container: KDE cache has untracked or missing archives actual=${#cached_archives[@]} expected=${lock_count}" >&2
        exit 1
    fi
    (
        cd "${KDE_CACHE}/archives"
        sha256sum -c "${KDE_CACHE}/archives.sha256"
    )
}

seed_chrome_cache() {
    local source="${BUILD}/host-gui-runtime/wayland-chromium/chrome-linux64.zip"
    local actual

    mkdir -p -- "${CHROME_CACHE}"
    if [[ ! -f "${CHROME_ARCHIVE}" && -f "${source}" ]]; then
        actual="$(sha256sum "${source}" | awk '{print $1}')"
        [[ "${actual}" == "${CHROME_SHA256}" ]] || {
            echo "reproduce-in-container: existing Chrome archive does not match the workspace lock" >&2
            exit 1
        }
        ln -- "${source}" "${CHROME_ARCHIVE}"
    fi
    if [[ -f "${CHROME_ARCHIVE}" ]]; then
        actual="$(sha256sum "${CHROME_ARCHIVE}" | awk '{print $1}')"
        [[ "${actual}" == "${CHROME_SHA256}" ]] || {
            echo "reproduce-in-container: cached Chrome archive sha256 mismatch" >&2
            exit 1
        }
    fi
}

receipt_matches_build() {
    local receipt="$1"
    [[ -f "${receipt}/fs.img" && -f "${BUILD}/fs.img" ]] || return 1
    [[ -f "${receipt}/kernel/build/kernel/xv6.bin" &&
       -f "${BUILD}/kernel/build/kernel/xv6.bin" ]] || return 1
    [[ "$(sha256sum "${receipt}/fs.img" | awk '{print $1}')" == \
       "$(sha256sum "${BUILD}/fs.img" | awk '{print $1}')" ]] || return 1
    [[ "$(sha256sum "${receipt}/kernel/build/kernel/xv6.bin" | awk '{print $1}')" == \
       "$(sha256sum "${BUILD}/kernel/build/kernel/xv6.bin" | awk '{print $1}')" ]]
}

create_receipt() {
    local source_build="$1"
    local id="$2"
    local origin="$3"
    local tmp="${RECEIPT_ROOT}/.${id}.tmp"
    local final="${RECEIPT_ROOT}/${id}"
    local item

    rm -rf -- "${tmp}"
    mkdir -p \
        "${tmp}/kernel/build/kernel" \
        "${tmp}/qemu-sdl/bin" \
        "${tmp}/qemu-sdl/share"
    ln -- "${source_build}/fs.img" "${tmp}/fs.img"
    ln -- "${source_build}/kernel/kernel.elf" "${tmp}/kernel/kernel.elf"
    ln -- "${source_build}/kernel/build/kernel/xv6.bin" \
        "${tmp}/kernel/build/kernel/xv6.bin"
    if [[ -f "${source_build}/kernel/build/kernel/kernel" ]]; then
        ln -- "${source_build}/kernel/build/kernel/kernel" \
            "${tmp}/kernel/build/kernel/kernel"
    fi
    for item in boot.img initrd.cpio.gz; do
        [[ -f "${source_build}/${item}" ]] || continue
        ln -- "${source_build}/${item}" "${tmp}/${item}"
    done
    ln -- "${QEMU_CACHE}/bin/qemu-system-x86_64" \
        "${tmp}/qemu-sdl/bin/qemu-system-x86_64"
    cp -a -- "${QEMU_CACHE}/share/qemu" "${tmp}/qemu-sdl/share/qemu"

    write_source_state "${tmp}/source-state.txt"
    write_workspace_hashes "${tmp}/workspace-source.sha256"
    mkdir -p "${tmp}/dependency-locks"
    cp -- "${KDE_CACHE}/packages.apt-order.txt" \
        "${tmp}/dependency-locks/kde-packages.apt-order.txt"
    cp -- "${KDE_CACHE}/archives.sha256" \
        "${tmp}/dependency-locks/kde-archives.sha256"
    cp -- "${QEMU_CACHE}/qemu-9.0.2.tar.xz.sha256" \
        "${tmp}/dependency-locks/qemu-9.0.2.tar.xz.sha256"
    {
        printf 'version=%s\nurl=%s\nsha256=%s\narchive=%s\n' \
            "${CHROME_VERSION}" "${CHROME_URL}" "${CHROME_SHA256}" \
            "${CHROME_ARCHIVE##*/}"
    } >"${tmp}/dependency-locks/chrome-for-testing.lock"
    printf '%s  %s\n' \
        '464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1' \
        'dmg-acid2.gb' >"${tmp}/dependency-locks/gameboy-roms.sha256"
    dpkg-query -W -f='${binary:Package}\t${Version}\n' \
        >"${tmp}/dependency-locks/container-dpkg.tsv"
    if [[ -d "${source_build}/rootfs-generated-overlays/webkit-media/share/webkit" ]]; then
        (
            cd "${source_build}/rootfs-generated-overlays/webkit-media/share/webkit"
            sha256sum -- *
        ) >"${tmp}/dependency-locks/webkit-media.sha256"
    fi
    if [[ -f "${source_build}/kde-noble-plasma/kde-qt-package-inventory.tsv" ]]; then
        cp -- "${source_build}/kde-noble-plasma/kde-qt-package-inventory.tsv" \
            "${tmp}/dependency-locks/kde-qt-package-inventory.tsv"
    fi
    {
        printf 'receipt_id=%s\norigin=%s\ncreated_utc=%s\n' \
            "${id}" "${origin}" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        printf 'build_command=scripts/container/reproduce-workspace.sh\n'
        printf 'desktop=kde-only\n'
        printf 'display=sdl-opengl\ngpu=virtio-vga-gl-primary\n'
        printf 'receipt_is_immutable_base=1\nlaunch_uses_qcow2_overlay=1\n'
    } >"${tmp}/reproduction-manifest.txt"
    (
        cd "${tmp}"
        sha256sum \
            kernel/kernel.elf \
            kernel/build/kernel/xv6.bin \
            fs.img \
            qemu-sdl/bin/qemu-system-x86_64 \
            >artifacts.sha256
    )
    "${ROOT}/scripts/build/validate-reproduction.sh" "${tmp}" \
        >"${tmp}/validation.txt" 2>&1
    touch "${tmp}/.xv6-reproduction-complete"
    mv -- "${tmp}" "${final}"
    ln -sfn -- "${id}" "${RECEIPT_ROOT}/.latest.new"
    mv -Tf -- "${RECEIPT_ROOT}/.latest.new" "${RECEIPT_ROOT}/latest"
    printf 'reproduce-in-container: receipt=%s\n' "${final}"
}

# Build the version-pinned, aspect/input-corrected SDL frontend once in a
# shared generated cache. Receipts copy the executable and firmware they use.
QEMU_SDL_USE_SYSTEM_DEPS=1 \
QEMU_SDL_BUILD_ROOT="${QEMU_CACHE}" \
    "${ROOT}/scripts/build/build-qemu-sdl.sh"
seed_kde_cache
seed_chrome_cache

# Preserve a valid pre-existing image once before replacing the only mutable
# build tree. Hard links avoid duplicating its 8+ GiB fs.img immediately.
if [[ -s "${BUILD}/fs.img" && -s "${BUILD}/kernel/kernel.elf" &&
      -s "${BUILD}/kernel/build/kernel/xv6.bin" ]]; then
    latest="$(realpath -e -- "${RECEIPT_ROOT}/latest" 2>/dev/null || true)"
    if [[ -z "${latest}" ]] || ! receipt_matches_build "${latest}"; then
        "${ROOT}/scripts/build/prune-reproductions.sh" "$((KEEP - 1))"
        "${ROOT}/scripts/build/validate-reproduction.sh" "${BUILD}" >/dev/null
        create_receipt "${BUILD}" "preexisting-${build_id}" "preexisting-validated"
    fi
fi

# Remove the oldest receipt before creating the next iteration, and never
# clean a build directory referenced by an exact QEMU process.
"${ROOT}/scripts/build/prune-reproductions.sh" "$((KEEP - 1))"
"${ROOT}/scripts/launch/assert-qemu-path-unused.sh" "${BUILD}"
rm -rf --one-file-system -- "${BUILD}"
mkdir -p -- "${BUILD}"
touch "${BUILD}/.xv6-reproduction-in-progress"
build_started=1

{
    echo "reproduce-in-container: configure clean build=${BUILD} jobs=${JOBS}"
    cmake -S "${ROOT}" -B "${BUILD}" -G Ninja \
        -DXV6_ARCH=x86_64 \
        -DXV6_PARALLEL_JOBS="${JOBS}" \
        -DXV6_KDE_ARCHIVES_DIR="${KDE_CACHE}/archives" \
        -DXV6_KDE_PACKAGE_ORDER_FILE="${KDE_CACHE}/packages.apt-order.txt" \
        -DXV6_KDE_ARCHIVE_SHA256_FILE="${KDE_CACHE}/archives.sha256" \
        -DXV6_KDE_PACKAGE_LOCKED=ON \
        -DXV6_CHROME_FOR_TESTING_URL="${CHROME_URL}" \
        -DXV6_CHROME_FOR_TESTING_SHA256="${CHROME_SHA256}" \
        -DXV6_CHROME_FOR_TESTING_ARCHIVE="${CHROME_ARCHIVE}"
    echo "reproduce-in-container: build complete image target"
    cmake --build "${BUILD}" --target image -j "${JOBS}"
} 2>&1 | tee "${BUILD}/reproduction-build.log"

"${ROOT}/scripts/build/validate-reproduction.sh" "${BUILD}" \
    | tee "${BUILD}/reproduction-validation.log"
rm -f -- "${BUILD}/.xv6-reproduction-in-progress"
create_receipt "${BUILD}" "${build_id}" "clean-container-build"
"${ROOT}/scripts/build/prune-reproductions.sh" "${KEEP}"
rm -f -- "${failed_log}"

build_started=0
trap - EXIT
latest="$(realpath -e -- "${RECEIPT_ROOT}/latest")"
printf 'XV6_REPRODUCTION_DONE receipt=%s\n' "${latest}"
