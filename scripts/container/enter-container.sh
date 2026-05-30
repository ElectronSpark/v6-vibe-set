#!/usr/bin/env bash
# Thin wrapper — runs the xv6-os dev container via docker run.
# compose.yml is used for 'docker compose build' and as service documentation;
# enter-container.sh uses 'docker run' directly so it can conditionally pass
# --device flags without depending on a minimum docker compose version.
#
# Usage:
#   enter-container.sh                 # interactive bash shell
#   enter-container.sh xv6-launch      # build everything + boot QEMU (KVM + GTK)
#   enter-container.sh xv6-build       # build kernel, userland, ports, fs.img
#   enter-container.sh xv6-help        # list all container commands
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
IMAGE="${XV6_CONTAINER_IMAGE:-xv6-os-dev}"

if ! command -v docker >/dev/null 2>&1; then
    echo "enter-container: docker is not installed or not on PATH" >&2
    exit 1
fi

if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "enter-container: image ${IMAGE} not found; build it with:" >&2
    echo "  docker compose -f ${ROOT}/compose.yml build" >&2
    exit 1
fi

XDG_RT="${XDG_RUNTIME_DIR:-/run/user/1000}"

# Run as the calling user so build artifacts are not root-owned on the host.
run_args=(
    --rm
    -u "$(id -u):$(id -g)"
    -w /src/xv6-os
    -v "${ROOT}:/src/xv6-os"
    -v "${XDG_RT}:${XDG_RT}"
    -v /tmp/.X11-unix:/tmp/.X11-unix:ro
    -e XV6_SOURCE_DIR=/src/xv6-os
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
    exec docker run -it "${run_args[@]}" "${IMAGE}" bash
else
    exec docker run -i "${run_args[@]}" "${IMAGE}" "$@"
fi
