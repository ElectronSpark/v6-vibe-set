#!/usr/bin/env bash
# Validate forced accelerated WebKit WebGL on the OpenGL-submit backend.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-/tmp/xv6-hyperv-build}"
LOG="${WEBKIT_VIRGL_VALIDATE_LOG:-${BUILD_DIR}/webkit-virgl-gpu-validate.log}"
TIMEOUT="${WEBKIT_VIRGL_VALIDATE_TIMEOUT:-180s}"
REOPEN="${WEBKIT_VIRGL_VALIDATE_REOPEN:-2}"
TIMEOUT_MS="${WEBKIT_VIRGL_VALIDATE_TIMEOUT_MS:-45000}"

mkdir -p "$(dirname "${LOG}")"
: >"${LOG}"

fail()
{
    echo "webkit-virgl-gpu-validate: $*" >&2
    echo "webkit-virgl-gpu-validate: log: ${LOG}" >&2
    exit 1
}

require_log()
{
    local pattern="$1"
    local why="$2"

    if ! grep -Eq "${pattern}" "${LOG}"; then
        fail "missing ${why}"
    fi
}

reject_log()
{
    local pattern="$1"
    local why="$2"

    if grep -Eq "${pattern}" "${LOG}"; then
        fail "found ${why}"
    fi
}

command -v expect >/dev/null 2>&1 ||
    fail "expect is required for prompt-synchronized guest validation"

cleanup_validation_qemu()
{
    pkill -TERM -f 'qemu-system-x86_64 .*webkit_webgl_smoke=1' 2>/dev/null || true
    pkill -TERM -f 'timeout --foreground .*scripts/launch-gui.sh' 2>/dev/null || true
}

trap cleanup_validation_qemu EXIT

echo "webkit-virgl-gpu-validate: running accelerated WebKit WebGL smoke" |
    tee -a "${LOG}"
expect >>"${LOG}" 2>&1 <<EOF || fail "WebKit virgl VM run failed"
set timeout 180
match_max 500000
set env(BUILD_DIR) "${BUILD_DIR}"
set env(DISPLAY_MODE) "gtk"
set env(USE_KVM) "${USE_KVM:-1}"
set env(QEMU_GPU) "virtio-gpu-gl"
set env(QEMU_INPUT) "${QEMU_INPUT:-virtio}"
set env(QEMU_NET) "${QEMU_NET:-0}"
set env(QEMU_VIRTIO_GPU_XRES) "${WEBKIT_VIRGL_XRES:-1024}"
set env(QEMU_VIRTIO_GPU_YRES) "${WEBKIT_VIRGL_YRES:-640}"
set env(QEMU_ALLOW_WSL_SDL_GL) "${QEMU_ALLOW_WSL_SDL_GL:-1}"
set env(QEMU_APPEND) "root=/dev/disk0 netsurf=0 webkit=1 webkit_accel=1 webkit_api_smoke=1 webkit_webgl_smoke=1 webkit_reopen=${REOPEN} webkit_timeout_ms=${TIMEOUT_MS} desktop_exit_after_smoke=1 webkit_log=1 video=${WEBKIT_VIRGL_XRES:-1024}x${WEBKIT_VIRGL_YRES:-640}"
spawn timeout --foreground ${TIMEOUT} bash scripts/launch-gui.sh
expect -re {wlcomp: entering main loop}
expect {
    -re {client exited \(status [1-9][0-9]*\)} { exit 2 }
    -re {panic|fatal page fault|SIGABRT|coredump: generating|wlcomp exited|WebKit smoke failed} { exit 3 }
    -re {__WEBKIT_API_SMOKE_DONE_0__} { exit 0 }
    -re {wlcomp: shutting down} { exit 0 }
    timeout { exit 4 }
    eof { exit 5 }
}
EOF

require_log 'webkit_gpu_policy .*requested_accel=1 .*effective_accel=1 .*opengl_submit=1 .*fallback=none' \
    "WebKit accelerated policy"
require_log 'webkit_gpu_policy .*d3d12_present=0 .*gpu_contract=virgl-opengl-submit .*fallback=none' \
    "WebKit virgl contract policy separate from D3D12"
require_log 'webkitgpusmoke: gpu-contract backend=virgl .*shared_surface=1 .*d3d12_present=0 .*opengl_submit=1 .*virgl_opengl=1 .*env_contract=virgl-opengl-submit .*env_d3d12=0 .*env_virgl=1 .*env_software=0 .*require=1 .*ok=1' \
    "WebKit in-process virgl OpenGL-submit contract"
require_log 'webkitgpusmoke: title=xv6 WebKit WebGL Spherical Poly: webgl ready' \
    "WebKit WebGL ready title"
require_log 'webkitgpusmoke: title=xv6 WebKit WebGL Spherical Poly: webgl spherical poly' \
    "WebKit first rendered WebGL frame"
require_log 'webkitgpusmoke: title=xv6 WebKit WebGL Spherical Poly: webgl spherical poly complete' \
    "WebKit WebGL completion"
require_log 'relaunched webkitgpusmoke|WebKit API reopen smoke complete' \
    "WebKit reopen cycle"
require_log '__WEBKIT_API_SMOKE_DONE_0__|wlcomp: shutting down' \
    "WebKit completion sentinel or compositor shutdown"
reject_log 'client exited \(status [1-9][0-9]*\)|signal: tgkill signum=6|Could not create GBM EGL display|panic|fatal page fault|coredump: generating|wlcomp exited|WebKit smoke failed' \
    "WebKit crash/failure marker"
reject_log 'backend=hyperv-dxg|gpu_contract=d3d12-shared-surface|env_contract=d3d12-shared-surface|env_d3d12=1|d3d12_present=1' \
    "Hyper-V/D3D12 contract in virgl validation"

echo "webkit-virgl-gpu-validate: passed (${LOG})"
