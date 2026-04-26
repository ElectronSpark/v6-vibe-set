#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

IMAGE="${XV6_CONTAINER_IMAGE:-xv6-os-base:local}"
NAME="${XV6_CONTAINER_NAME:-xv6-os-dev}"
ARCH="${XV6_ARCH:-x86_64}"
BUILD_DIR="${XV6_BUILD_DIR:-/src/xv6-os/build-${ARCH}}"
JOBS="${XV6_PARALLEL_JOBS:-2}"
PREBUILT_HOST="${XV6_PREBUILT_TOOLCHAIN_HOST:-${ROOT}/build-toolchain-${ARCH}}"
PREBUILT_CONTAINER="${XV6_PREBUILT_TOOLCHAIN_PREFIX:-/opt/xv6-prebuilt-toolchain}"

if ! command -v docker >/dev/null 2>&1; then
    echo "enter-container: docker is not installed or not on PATH" >&2
    exit 1
fi

if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "enter-container: image ${IMAGE} not found; build it with:" >&2
    echo "  docker build --target base -t ${IMAGE} ${ROOT}" >&2
    exit 1
fi

create_container() {
    local docker_args=(
        create
        --name "${NAME}"
        --workdir /src/xv6-os
        -e XV6_SOURCE_DIR=/src/xv6-os
        -e XV6_BUILD_DIR="${BUILD_DIR}"
        -e XV6_ARCH="${ARCH}"
        -e XV6_PARALLEL_JOBS="${JOBS}"
        -v "${ROOT}:/src/xv6-os"
    )

    has_prebuilt=0
    if [[ -d "${PREBUILT_HOST}" ]]; then
        has_prebuilt=1
    fi

    if [[ "${has_prebuilt}" == "1" ]]; then
        docker_args+=(
            -e XV6_PREBUILT_TOOLCHAIN_PREFIX="${PREBUILT_CONTAINER}"
            -v "${PREBUILT_HOST}:${PREBUILT_CONTAINER}:ro"
        )
    fi

    docker_args+=("${IMAGE}" sleep infinity)
    docker "${docker_args[@]}" >/dev/null
}

if ! docker container inspect "${NAME}" >/dev/null 2>&1; then
    create_container
fi

state="$(docker inspect -f '{{.State.Running}}' "${NAME}")"
if [[ "${state}" != "true" ]]; then
    docker start "${NAME}" >/dev/null
fi

if [[ $# -gt 0 ]]; then
    exec_mode=(exec)
    container_command=("$@")
else
    exec_mode=(exec -it)
    container_command=(bash -i)
fi

exec_args=(
    "${exec_mode[@]}"
    -e XV6_SOURCE_DIR=/src/xv6-os
    -e XV6_BUILD_DIR="${BUILD_DIR}"
    -e XV6_ARCH="${ARCH}"
    -e XV6_PARALLEL_JOBS="${JOBS}"
)

if [[ "${has_prebuilt}" == "1" ]]; then
    exec_args+=(-e XV6_PREBUILT_TOOLCHAIN_PREFIX="${PREBUILT_CONTAINER}")
fi

exec_args+=("${NAME}" "${container_command[@]}")
exec docker "${exec_args[@]}"