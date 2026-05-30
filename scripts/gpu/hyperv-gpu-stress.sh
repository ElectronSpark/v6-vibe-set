#!/usr/bin/env bash
# Run a mixed Hyper-V GUI/GPU stress lane with a live 3D demo.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-/tmp/xv6-hyperv-build}
VM_NAME=${VM_NAME:-xv6-os-hyperv}
DEPLOY_VHDX=${DEPLOY_VHDX:-/mnt/c/Temp/xv6-hyperv.vhdx}
OUT_VHDX=${OUT_VHDX:-${BUILD_DIR}/xv6-hyperv-gpu-stress.vhdx}
LOG=${LOG:-${BUILD_DIR}/hyperv-gpu-stress.log}
SERIAL_SCRIPT=${SERIAL_SCRIPT:-C:\\Temp\\com-tcp-read.ps1}
FRAMES=${FRAMES:-2400}
WAIT_SEC=${WAIT_SEC:-25}
REUSE_RUNNING_VM=${REUSE_RUNNING_VM:-0}
KERNEL_BIN=${KERNEL_BIN:-${BUILD_DIR}/kernel/build/kernel/xv6.bin}
ROOTFS_IMG=${ROOTFS_IMG:-${BUILD_DIR}/fs.img}
HYPERV_CMDLINE=${HYPERV_CMDLINE:-BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=1 glsmoke_demo=1 glsmoke_frames=${FRAMES} wayland_dmabuf=1 video=1024x640 acpi_cpus=6}

fail() {
    echo "hyperv-gpu-stress: $*" >&2
    exit 1
}

serial_read() {
    local cmd=$1
    local read_ms=${2:-240000}

    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "& '${SERIAL_SCRIPT}' -Cmd '${cmd}' -ReadMs ${read_ms}"
}

require_log() {
    local pattern=$1
    local description=$2

    grep -Eq "${pattern}" "${LOG}" || fail "missing ${description}; log=${LOG}"
}

reject_log() {
    local pattern=$1
    local description=$2

    if grep -Eiq "${pattern}" "${LOG}"; then
        fail "found ${description}; log=${LOG}"
    fi
}

command -v powershell.exe >/dev/null || fail "missing powershell.exe"
command -v python3 >/dev/null || fail "missing python3"
mkdir -p "${BUILD_DIR}"
: >"${LOG}"

if [[ "${REUSE_RUNNING_VM}" != "1" ]]; then
    echo "hyperv-gpu-stress: building ${OUT_VHDX}" | tee -a "${LOG}"
    HYPERV_CMDLINE="${HYPERV_CMDLINE}" \
        "${REPO_ROOT}/scripts/image/make-hyperv-image.sh" \
        "${KERNEL_BIN}" "${ROOTFS_IMG}" "${OUT_VHDX}" 0 | tee -a "${LOG}"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Stop-VM -Name '${VM_NAME}' -TurnOff -Force -ErrorAction SilentlyContinue"
    cp -f "${OUT_VHDX}" "${DEPLOY_VHDX}"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Start-VM -Name '${VM_NAME}'; Get-VM -Name '${VM_NAME}' | Select Name,State,Uptime | Format-List" |
        tee -a "${LOG}"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Start-Sleep -Seconds ${WAIT_SEC}"
fi

serial_read 'cat /proc/cmdline; ps' 30000 | tee -a "${LOG}"
OUT_PNG=/mnt/c/Temp/xv6-hyperv-gpu-stress.png \
OUT_RAW=/mnt/c/Temp/xv6-hyperv-gpu-stress.raw \
    "${REPO_ROOT}/scripts/gpu/hyperv-3d-visual-check.sh" 2>&1 | tee -a "${LOG}"
serial_read 'gpubuftest 3; dmabufsmoke --nv12 --explicit-sync; drmgpuprobe; dxgprobe --owner-isolation; fbstat' 240000 |
    tee -a "${LOG}"

require_log 'glsmoke_frames=([0-9]+|2400)' "long 3D demo command line"
require_log 'mesawlegl --demo --frames=' "live 3D demo process"
require_log 'hyperv-3d-visual-check: ok' "Hyper-V screenshot visual check"
require_log 'gpubuftest: completed 3 buffer cycles' "multi-cycle BO/fence smoke"
require_log 'dmabufsmoke: explicit-sync release=(fenced|immediate)' \
    "explicit-sync dmabuf smoke"
require_log 'drmgpuprobe: passed' "DRM/GBM probe"
require_log 'dxgprobe: owner-isolation ok' "DXG owner isolation"
require_log 'backend_opengl_submit 0' "Hyper-V OpenGL-submit remains gated"
reject_log 'backend_opengl_submit 1' \
    "Hyper-V OpenGL-submit claim in stress without strict native-present/FPS gate"
reject_log 'drisw|softpipe|llvmpipe|swrast|xv6-mesa: d3d12 native present unavailable; refusing DRI software/readback present|d3d12_(cpu_readback|cpu_mapping|cpu_copy|readback)=[1-9][0-9]*' \
    "software/readback rendering path"
reject_log 'hyperv-3d-fps-validate: ok|strict_anti_inflation=1|effective_presented_fps=' \
    "FPS pass marker in stress lane; run hyperv-3d-fps-validate.sh for FPS credit"
require_log 'bo_fd_live 0' "BO fd cleanup accounting"
require_log 'fence_fd_live 0' "fence fd cleanup accounting"

if grep -Eiq 'panic|fatal page fault|coredump|assert' "${LOG}"; then
    fail "guest log contains a crash signature"
fi

echo "hyperv-gpu-stress: passed (${LOG})" | tee -a "${LOG}"
