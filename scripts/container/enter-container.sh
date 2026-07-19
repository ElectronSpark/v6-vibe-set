#!/usr/bin/env bash
# Thin wrapper — runs the xv6-os dev container via docker run.
# compose.yml is used for 'docker compose build' and as service documentation;
# enter-container.sh uses 'docker run' directly so it can conditionally pass
# --device flags without depending on a minimum docker compose version.
#
# Usage:
#   enter-container.sh                 # interactive bash shell
#   enter-container.sh xv6-build       # clean kernel + KDE image reproduction
#   enter-container.sh xv6-launch      # launch the built VM safely on the host
#   enter-container.sh xv6-help        # list all container commands
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
IMAGE="${XV6_CONTAINER_IMAGE:-xv6-os-dev}"

resolve_docker() {
    if [[ -n "${XV6_DOCKER_BIN:-}" ]]; then
        DOCKER=("${XV6_DOCKER_BIN}")
    elif command -v docker >/dev/null 2>&1; then
        DOCKER=("$(command -v docker)")
    elif command -v docker.exe >/dev/null 2>&1; then
        DOCKER=("$(command -v docker.exe)")
    elif [[ -x '/mnt/c/Program Files/Docker/Docker/resources/bin/docker.exe' ]]; then
        DOCKER=('/mnt/c/Program Files/Docker/Docker/resources/bin/docker.exe')
    else
        echo "enter-container: Docker CLI is not installed or not on PATH" >&2
        exit 1
    fi
    [[ -x "${DOCKER[0]}" ]] || {
        echo "enter-container: Docker CLI is not executable: ${DOCKER[0]}" >&2
        exit 1
    }
}

resolve_docker

# VM launches stay in the host PID namespace. Docker Desktop containers cannot
# reliably observe QEMU processes owned by the surrounding WSL distribution.
case "${1:-}" in
    xv6-launch)
        shift
        exec "${ROOT}/scripts/launch/launch-gui.sh" "$@"
        ;;
    xv6-launch-nokvm|xv6-qemu-nokvm)
        shift
        USE_KVM=0 DISPLAY_MODE="${DISPLAY_MODE:-nographic}" \
            exec "${ROOT}/scripts/launch/launch-gui.sh" "$@"
        ;;
esac

if ! "${DOCKER[@]}" image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "enter-container: image ${IMAGE} not found; build it with:" >&2
    echo "  ${ROOT}/scripts/container/reproduce-workspace.sh" >&2
    exit 1
fi

XDG_RT="${XDG_RUNTIME_DIR:-/run/user/1000}"

# KDE image generation may refresh APT metadata, so run as the container's
# default root user. xv6-command re-owns generated build trees to the source
# bind mount's host owner before returning.
run_args=(
    --rm
    -w /src/xv6-os
    -v "${ROOT}:/src/xv6-os"
    -v "${XDG_RT}:${XDG_RT}"
    -v /tmp/.X11-unix:/tmp/.X11-unix:ro
    -e XV6_SOURCE_DIR=/src/xv6-os
    -e XV6_IN_CONTAINER=1
    -e "XV6_ARCH=${XV6_ARCH:-x86_64}"
    -e "XV6_PARALLEL_JOBS=${XV6_PARALLEL_JOBS:-}"
    -e "DISPLAY=${DISPLAY:-}"
    -e "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}"
    -e "XDG_RUNTIME_DIR=${XDG_RT}"
    -e HOME=/tmp
)

# Add devices only when present on the host (KVM/GPU for in-container QEMU).
for dev in /dev/kvm /dev/dri /dev/udmabuf; do
    [[ -e "${dev}" ]] && run_args+=(--device "${dev}")
done

if [[ $# -eq 0 ]]; then
    exec "${DOCKER[@]}" run -it "${run_args[@]}" "${IMAGE}" bash
else
    exec "${DOCKER[@]}" run -i "${run_args[@]}" "${IMAGE}" "$@"
fi
