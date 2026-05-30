#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

IMAGE="${XV6_CONTAINER_IMAGE:-xv6-os-dev}"
NAME="${XV6_CONTAINER_NAME:-xv6-os-dev}"
ARCH="${XV6_ARCH:-x86_64}"
BUILD_DIR="${XV6_BUILD_DIR:-/src/xv6-os/build-${ARCH}}"
JOBS="${XV6_PARALLEL_JOBS:-$(nproc)}"
if ! command -v docker >/dev/null 2>&1; then
    echo "enter-container: docker is not installed or not on PATH" >&2
    exit 1
fi

if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "enter-container: image ${IMAGE} not found; build it with:" >&2
    echo "  docker build --target dev -t ${IMAGE} ${ROOT}" >&2
    exit 1
fi

append_host_device() {
    local path="$1"

    if [[ -e "${path}" ]]; then
        docker_args+=(--device "${path}")
    fi
}

append_host_display() {
    if [[ -n "${DISPLAY:-}" ]]; then
        docker_args+=(-e "DISPLAY=${DISPLAY}")
        if [[ -d /tmp/.X11-unix ]]; then
            docker_args+=(-v /tmp/.X11-unix:/tmp/.X11-unix)
        fi
    fi

    if [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then
        docker_args+=(-e "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR}")
        if [[ -n "${WAYLAND_DISPLAY:-}" &&
              -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
            docker_args+=(
                -e "WAYLAND_DISPLAY=${WAYLAND_DISPLAY}"
                -v "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}:${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}"
            )
        fi
    fi
}

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

    append_host_device /dev/kvm
    append_host_device /dev/dri
    append_host_device /dev/udmabuf
    append_host_display

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

exec_args+=("${NAME}" "${container_command[@]}")
exec docker "${exec_args[@]}"
