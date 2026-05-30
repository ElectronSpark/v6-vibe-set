#!/usr/bin/env bash
# Thin wrapper — forwards to docker compose run for the xv6-os dev container.
#
# Usage:
#   enter-container.sh                 # interactive bash shell
#   enter-container.sh xv6-launch      # build everything + boot QEMU (KVM + GTK)
#   enter-container.sh xv6-build       # build kernel, userland, ports, fs.img
#   enter-container.sh xv6-help        # list all container commands
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE="${ROOT}/compose.yml"

if ! command -v docker >/dev/null 2>&1; then
    echo "enter-container: docker is not installed or not on PATH" >&2
    exit 1
fi

if ! docker image inspect "${XV6_CONTAINER_IMAGE:-xv6-os-dev}" >/dev/null 2>&1; then
    echo "enter-container: image xv6-os-dev not found; build it with:" >&2
    echo "  docker compose -f ${COMPOSE} build" >&2
    exit 1
fi

# Run as the calling user so build artifacts are not root-owned on the host.
export XV6_UID="$(id -u)"
export XV6_GID="$(id -g)"

# Pass --device for each node that exists on the host (KVM/GPU for in-container QEMU)
device_args=()
for dev in /dev/kvm /dev/dri /dev/udmabuf; do
    [[ -e "${dev}" ]] && device_args+=(--device "${dev}")
done

if [[ $# -eq 0 ]]; then
    exec docker compose -f "${COMPOSE}" run --rm "${device_args[@]}" xv6 bash
else
    exec docker compose -f "${COMPOSE}" run --rm "${device_args[@]}" xv6 "$@"
fi
