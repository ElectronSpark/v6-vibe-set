#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: scripts/container/docker-build-webkit.sh <webkit-ref-sysroot> [docker-build-args...]

Build the Docker image with a local host-glibc WebKit runtime available to
CMake, without committing or uploading that runtime to GitHub. The repository
does not contain ports/webkit/sysroot; pass that runtime explicitly here.

Environment:
  IMAGE_TAG             Docker image tag (default: xv6-os-webkit)
  BUILD_TARGET          CMake target inside Docker (default: world)
  XV6_ARCH              xv6 arch (default: x86_64)
  XV6_PARALLEL_JOBS     CMake parallel jobs setting (default: 2)
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || $# -lt 1 ]]; then
    usage
    exit 2
fi

ref_sysroot="$1"
shift

if [[ ! -x "${ref_sysroot}/libexec/webkit2gtk-4.1/MiniBrowser" ]]; then
    echo "docker-build-webkit: ${ref_sysroot} is missing libexec/webkit2gtk-4.1/MiniBrowser" >&2
    exit 1
fi

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
image_tag="${IMAGE_TAG:-xv6-os-webkit}"
build_target="${BUILD_TARGET:-world}"
xv6_arch="${XV6_ARCH:-x86_64}"
xv6_parallel_jobs="${XV6_PARALLEL_JOBS:-2}"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/xv6-docker-webkit.XXXXXX")"

cleanup() {
    rm -rf "${tmpdir}"
}
trap cleanup EXIT

rsync -a --delete \
    --exclude .git \
    --exclude '.docker-webkit-ref-sysroot' \
    --exclude 'build-*' \
    --exclude build \
    --exclude 'cmake-build-*' \
    "${repo_root}/" "${tmpdir}/"

mkdir -p "${tmpdir}/.docker-webkit-ref-sysroot"
rsync -a --delete "${ref_sysroot}/" "${tmpdir}/.docker-webkit-ref-sysroot/"

exec docker build \
    --target build \
    --build-arg "BUILD_TARGET=${build_target}" \
    --build-arg "XV6_ARCH=${xv6_arch}" \
    --build-arg "XV6_PARALLEL_JOBS=${xv6_parallel_jobs}" \
    --build-arg "XV6_WEBKIT_REF_SYSROOT=/src/xv6-os/.docker-webkit-ref-sysroot" \
    -t "${image_tag}" \
    "$@" \
    "${tmpdir}"
