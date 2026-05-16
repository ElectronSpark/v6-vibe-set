#!/usr/bin/env bash
# Validate repeated local WebKit GPU/API smoke without using network pages.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-/tmp/xv6-hyperv-build}"
VM_NAME="${HYPERV_VM_NAME:-xv6-os-hyperv}"
DEPLOY_VHDX="${HYPERV_DEPLOY_VHDX:-/mnt/c/Temp/xv6-hyperv.vhdx}"
OUT_VHDX="${BUILD_DIR}/xv6-hyperv-webkit-gpu-validate.vhdx"
LOG="${WEBKIT_GPU_VALIDATE_LOG:-${BUILD_DIR}/hyperv-webkit-gpu-validate.log}"
READ_MS="${WEBKIT_GPU_VALIDATE_READ_MS:-150000}"
TIMEOUT_MS="${WEBKIT_GPU_VALIDATE_TIMEOUT_MS:-30000}"
REOPEN="${WEBKIT_GPU_VALIDATE_REOPEN:-2}"

mkdir -p "$(dirname "${LOG}")"
: >"${LOG}"

fail()
{
    echo "hyperv-webkit-gpu-validate: $*" >&2
    echo "hyperv-webkit-gpu-validate: log: ${LOG}" >&2
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

run_ps()
{
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$1" |
        tee -a "${LOG}"
}

run_guest()
{
    local cmd="$1"
    local read_ms="${2:-${READ_MS}}"

    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "& 'C:\Temp\com-tcp-read.ps1' -Cmd '${cmd}' -ReadMs ${read_ms}" |
        tee -a "${LOG}"
}

echo "hyperv-webkit-gpu-validate: building kernel/rootfs" | tee -a "${LOG}"
cmake --build "${BUILD_DIR}" --target kernel -j"${WEBKIT_GPU_VALIDATE_JOBS:-2}" |
    tee -a "${LOG}"
cmake --build "${BUILD_DIR}" --target rootfs -j"${WEBKIT_GPU_VALIDATE_JOBS:-2}" |
    tee -a "${LOG}"

echo "hyperv-webkit-gpu-validate: building ${OUT_VHDX}" | tee -a "${LOG}"
HYPERV_CMDLINE="BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=1 webkit_accel=1 webkit_api_smoke=1 webkit_webgl_smoke=1 webkit_reopen=${REOPEN} webkit_timeout_ms=${TIMEOUT_MS} desktop_exit_after_smoke=1 video=1024x640" \
    scripts/make-hyperv-image.sh \
        "${BUILD_DIR}/kernel/build/kernel/xv6.bin" \
        "${BUILD_DIR}/fs.img" \
        "${OUT_VHDX}" 0 | tee -a "${LOG}"

echo "hyperv-webkit-gpu-validate: deploying to ${VM_NAME}" | tee -a "${LOG}"
run_ps "Stop-VM -Name '${VM_NAME}' -TurnOff -Force -ErrorAction SilentlyContinue"
cp -f "${OUT_VHDX}" "${DEPLOY_VHDX}"
run_ps "Start-VM -Name '${VM_NAME}'; Get-VM -Name '${VM_NAME}' | Select Name, State, Uptime | Format-List"

echo "hyperv-webkit-gpu-validate: running local repeated WebKit smoke" |
    tee -a "${LOG}"
sleep 20
run_guest "cat /proc/cmdline; sleep 90; cat /tmp/webkit-title; cat /tmp/webkit-gpu-policy; ps; fbstat" \
    "${READ_MS}"

require_log 'webkit=1 .*webkit_api_smoke=1 .*webkit_webgl_smoke=1' \
    "WebKit local GPU/API smoke command line"
require_log 'backend hyperv-dxg flags' "Hyper-V GPU backend"
require_log 'backend_opengl_submit 0' "Hyper-V OpenGL-submit gate"
require_log 'webkit_gpu_policy .*requested_accel=1 .*effective_accel=0 .*opengl_submit=0 .*fallback=opengl_submit_unavailable' \
    "WebKit acceleration fallback gate"
require_log 'webkitgpusmoke pid=' "WebKit API smoke launch"
require_log 'relaunch(ed)? webkitgpusmoke' "WebKit reopen cycle"
require_log 'WebKit (API reopen|timeout) smoke complete' \
    "WebKit repeated smoke completion"
require_log 'wlcomp' "Wayland compositor remained observable"
reject_log 'panic|fatal page fault|SIGABRT|coredump: generating' \
    "crash marker"
reject_log 'wlcomp exited' "compositor exit"
reject_log 'backend_opengl_submit 1' "unexpected Hyper-V OpenGL-submit advert"

echo "hyperv-webkit-gpu-validate: passed (${LOG})"
