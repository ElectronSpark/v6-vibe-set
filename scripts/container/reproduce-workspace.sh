#!/usr/bin/env bash
# Host one-command entrypoint: rebuild the dev container, then reproduce the
# exact bind-mounted workspace into a clean x86_64 kernel + KDE image receipt.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
IMAGE="${XV6_CONTAINER_IMAGE:-xv6-os-dev}"
ARCH="${XV6_ARCH:-x86_64}"
RECEIPT_ROOT="${ROOT}/build-reproductions/${ARCH}"
BUILD="${ROOT}/build-${ARCH}"
authority_file="${RECEIPT_ROOT}/.host-cleanup-authority"
authority_token="host-$(date -u +%Y%m%dT%H%M%SZ)-$$-${RANDOM}"

resolve_docker() {
    if [[ -n "${XV6_DOCKER_BIN:-}" ]]; then
        [[ -x "${XV6_DOCKER_BIN}" ]] || {
            echo "reproduce-workspace: XV6_DOCKER_BIN is not executable: ${XV6_DOCKER_BIN}" >&2
            exit 1
        }
        DOCKER=("${XV6_DOCKER_BIN}")
    elif command -v docker >/dev/null 2>&1; then
        DOCKER=("$(command -v docker)")
    elif command -v docker.exe >/dev/null 2>&1; then
        DOCKER=("$(command -v docker.exe)")
    elif [[ -x '/mnt/c/Program Files/Docker/Docker/resources/bin/docker.exe' ]]; then
        DOCKER=('/mnt/c/Program Files/Docker/Docker/resources/bin/docker.exe')
    else
        echo "reproduce-workspace: Docker CLI not found" >&2
        exit 1
    fi
}

resolve_docker
"${DOCKER[@]}" info >/dev/null

# The container has its own PID namespace, so only the host wrapper may
# authorize removal of bind-mounted generated paths. Hold the same exclusive
# lock that managed QEMU launches take in shared mode, then prove no exact host
# QEMU process references anything below this workspace. This never signals a
# process; an unrelated external VM is allowed to keep running.
mkdir -p -- "${RECEIPT_ROOT}"
exec 9>"${RECEIPT_ROOT}/.reproduction.lock"
flock -n 9 || {
    echo "reproduce-workspace: a managed launch or another reproduction owns the workspace lock" >&2
    exit 75
}
"${ROOT}/scripts/launch/assert-qemu-path-unused.sh" "${ROOT}"
printf '%s\n' "${authority_token}" >"${authority_file}"
chmod 0600 "${authority_file}"
cleanup_authority() {
    rm -f -- "${authority_file}"
}
trap cleanup_authority EXIT INT TERM HUP

echo "reproduce-workspace: building container image ${IMAGE}"
"${DOCKER[@]}" build --target dev -t "${IMAGE}" "${ROOT}"
image_id="$("${DOCKER[@]}" image inspect "${IMAGE}" --format '{{.Id}}')"

run_args=(
    --rm
    -u "$(id -u):$(id -g)"
    -w /src/xv6-os
    -v "${ROOT}:/src/xv6-os"
    -e XV6_SOURCE_DIR=/src/xv6-os
    -e XV6_IN_CONTAINER=1
    -e "XV6_CONTAINER_IMAGE_ID=${image_id}"
    -e "XV6_ARCH=${ARCH}"
    -e "XV6_PARALLEL_JOBS=${XV6_PARALLEL_JOBS:-2}"
    -e "XV6_KEEP_ITERATIONS=${XV6_KEEP_ITERATIONS:-3}"
    -e XV6_REPRODUCTION_LOCK_HELD=1
    -e XV6_HOST_CLEANUP_AUTHORIZED_ROOT=/src/xv6-os
    -e "XV6_HOST_CLEANUP_AUTHORITY_TOKEN=${authority_token}"
    -e HOME=/tmp
)

echo "reproduce-workspace: starting clean container build"
"${DOCKER[@]}" run "${run_args[@]}" "${IMAGE}" xv6-reproduce

latest="$(realpath -e -- "${RECEIPT_ROOT}/latest")"
echo "reproduce-workspace: PASS receipt=${latest}"
echo "reproduce-workspace: launch safely on the host with scripts/launch/launch-gui.sh"
