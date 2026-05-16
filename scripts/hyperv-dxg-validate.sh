#!/usr/bin/env bash
# Rebuild, deploy, and validate the Hyper-V DXG/D3DKMT graphics lane.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-/tmp/xv6-hyperv-build}
VM_NAME=${VM_NAME:-xv6-os-hyperv}
DEPLOY_VHDX=${DEPLOY_VHDX:-/mnt/c/Temp/xv6-hyperv.vhdx}
OUT_VHDX=${OUT_VHDX:-${BUILD_DIR}/xv6-hyperv-dxg-validate.vhdx}
LOG=${LOG:-${BUILD_DIR}/hyperv-dxg-validate.log}
SERIAL_SCRIPT=${SERIAL_SCRIPT:-C:\\Temp\\com-tcp-read.ps1}
READ_MS=${READ_MS:-120000}
BOOT_WAIT_SEC=${BOOT_WAIT_SEC:-20}
HYPERV_CMDLINE=${HYPERV_CMDLINE:-BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=1 glsmoke_demo=1 glsmoke_frames=4 wayland_dmabuf=1 wlcomp_gpu_compose=1 video=1024x640}
REQUIRE_BOOT_GLSMOKE=${REQUIRE_BOOT_GLSMOKE:-1}

KERNEL_BIN=${KERNEL_BIN:-${BUILD_DIR}/kernel/build/kernel/xv6.bin}
ROOTFS_IMG=${ROOTFS_IMG:-${BUILD_DIR}/fs.img}

fail() {
    echo "hyperv-dxg-validate: $*" >&2
    exit 1
}

need() {
    command -v "$1" >/dev/null || fail "missing required command: $1"
}

serial_read() {
    local cmd=$1
    local read_ms=${2:-${READ_MS}}

    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "& '${SERIAL_SCRIPT}' -Cmd '${cmd}' -ReadMs ${read_ms}"
}

require_log() {
    local pattern=$1
    local description=$2

    if ! grep -Eq "${pattern}" "${LOG}"; then
        echo "hyperv-dxg-validate: missing ${description}" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

run_guest() {
    local cmd=$1
    local read_ms=${2:-${READ_MS}}

    serial_read "${cmd}" "${read_ms}" | tee -a "${LOG}"
}

log_metadata() {
    local label=$1

    {
        echo "hyperv-dxg-validate: metadata ${label}"
        echo "host_date=$(date -Is)"
        echo "repo_root=${REPO_ROOT}"
        echo "build_dir=${BUILD_DIR}"
        echo "kernel_bin=${KERNEL_BIN}"
        echo "rootfs_img=${ROOTFS_IMG}"
        echo "out_vhdx=${OUT_VHDX}"
        echo "deploy_vhdx=${DEPLOY_VHDX}"
        echo "hyperv_cmdline=${HYPERV_CMDLINE}"
        git -C "${REPO_ROOT}" rev-parse --short HEAD 2>/dev/null |
            sed 's/^/git_head=/'
        git -C "${REPO_ROOT}" status --short 2>/dev/null |
            sed 's/^/git_status=/'
        stat -c 'artifact=%n size=%s mtime=%y' \
            "${KERNEL_BIN}" "${ROOTFS_IMG}" "${OUT_VHDX}" "${DEPLOY_VHDX}" \
            2>/dev/null || true
    } >>"${LOG}"
}

need cmake
need powershell.exe
need cp
need grep

mkdir -p "${BUILD_DIR}"
: >"${LOG}"
log_metadata start

echo "hyperv-dxg-validate: building kernel"
cmake --build "${BUILD_DIR}" --target kernel -j"${JOBS:-2}"

echo "hyperv-dxg-validate: building rootfs"
cmake --build "${BUILD_DIR}" --target rootfs -j"${JOBS:-2}"

echo "hyperv-dxg-validate: building ${OUT_VHDX}"
HYPERV_CMDLINE="${HYPERV_CMDLINE}" \
    "${REPO_ROOT}/scripts/make-hyperv-image.sh" \
    "${KERNEL_BIN}" "${ROOTFS_IMG}" "${OUT_VHDX}" 0

[[ -s "${OUT_VHDX}" ]] || fail "VHDX was not created: ${OUT_VHDX}"
log_metadata built

echo "hyperv-dxg-validate: deploying to ${VM_NAME}"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "Stop-VM -Name '${VM_NAME}' -TurnOff -Force -ErrorAction SilentlyContinue"
cp -f "${OUT_VHDX}" "${DEPLOY_VHDX}"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "Start-VM -Name '${VM_NAME}'; Get-VM -Name '${VM_NAME}' | Select Name,State,Uptime | Format-List" |
    tee -a "${LOG}"
log_metadata deployed

echo "hyperv-dxg-validate: waiting ${BOOT_WAIT_SEC}s for guest shell"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "Start-Sleep -Seconds ${BOOT_WAIT_SEC}"
for attempt in 1 2 3 4; do
    ready_out=$(serial_read 'echo hyperv-dxg-ready' 10000 || true)
    printf '%s\n' "${ready_out}" | tee "${LOG}.ready" >>"${LOG}"
    if grep -q 'hyperv-dxg-ready' "${LOG}.ready"; then
        break
    fi
    if [[ "${attempt}" -eq 4 ]]; then
        fail "guest shell did not become ready"
    fi
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Start-Sleep -Seconds 5"
done

echo "hyperv-dxg-validate: running guest probes"
run_guest 'cat /proc/cmdline; cat /proc/version; cat /proc/uptime' 30000
run_guest 'ps; cat /tmp/glsmoke-status' 30000
run_guest 'cat /tmp/wlcomp-fps; fbstat' 30000
run_guest 'gpubuftest 1; gpubuftest --render-owner; fbstat' 120000
run_guest 'drmprimeprobe; fbstat' 120000
run_guest 'gbmtest' 120000
run_guest 'dmabufsmoke --hold-ms=1200; sleep 2; cat /tmp/wlcomp-fps; dmabufsmoke --nv12 --explicit-sync' 120000
run_guest 'mesaglfeature' 120000
run_guest 'dxgprobe' "${READ_MS}"
run_guest 'dxgprobe --try-submit; cat /dev/dxg' 180000
run_guest 'dxgprobe --owner-isolation' 120000
run_guest 'dxgprobe --leak-close; dxgprobe; cat /dev/dxg' 180000
run_guest 'drmgpuprobe' "${READ_MS}"
run_guest 'cat /dev/dxg' 30000
run_guest 'fbstat' 30000

require_log 'BOOT_IMAGE=/xv6\.bin .*root=/dev/disk0p2' "expected Hyper-V command line"
require_log 'Linux version .*xv6' "guest kernel version"
require_log '^[0-9]+([.][0-9]+)?[[:space:]][0-9]+([.][0-9]+)?[[:space:]]*$' "guest uptime"
require_log 'backend hyperv-dxg flags' "Hyper-V backend"
require_log 'backend_dxg_transport 1' "DXG transport flag"
require_log 'backend_d3dkmt 1' "D3DKMT readiness flag"
require_log 'backend_opengl_submit 0' "Hyper-V OpenGL submit gating"
require_log 'mode=(direct-scanout|bo-present)' \
    "compositor GPU-backed/direct framebuffer mode"
require_log '^display_presents [1-9][0-9]*[[:space:]]*$' \
    "display present completion accounting"
require_log '^display_completions [1-9][0-9]*[[:space:]]*$' \
    "display completion accounting"
require_log '^display_last_complete [1-9][0-9]*[[:space:]]*$' \
    "latest display completion sequence"
require_log 'gpubuftest: closed fence fd rejected' \
    "GPU fence invalid fd rejection"
require_log 'gpubuftest: pending fence query ok' \
    "GPU fence zero-timeout pending query"
require_log 'gpubuftest: completed 1 buffer cycles' \
    "GPU BO/fence buffer cycle"
require_log 'gpubuftest: render fd ownership verified' \
    "GPU render fd object ownership"
require_log 'drmprimeprobe: prime cap value=0x3' \
    "DRM PRIME import/export capability"
require_log 'drmprimeprobe: stale handle rejected on second fd' \
    "DRM render fd stale-handle rejection"
require_log 'drmprimeprobe: closed PRIME fd rejected' \
    "DRM PRIME closed fd rejection"
require_log 'drmprimeprobe: ok' \
    "DRM PRIME fd export/import mmap smoke"
require_log 'gbmtest: passed linear NV12 modifier plane metadata import' \
    "GBM modifier and multi-plane metadata import"
require_log 'gbmtest: passed linear BO create/map/export/import/destroy' \
    "GBM linear BO compatibility smoke"
require_log 'dmabufsmoke: presented linux-dmabuf buffer format=NV12 planes=2' \
    "linux-dmabuf NV12 multi-plane presentation"
require_log 'wlcomp: gpu-compose presented' \
    "compositor GPU composition path"
require_log 'gpu_compose=[1-9][0-9]*' \
    "compositor GPU composition accounting"
require_log 'dmabufsmoke: explicit-sync release=(fenced|immediate)' \
    "linux explicit-sync release smoke"
require_log 'wlcomp: dmabuf buffer .*fmt=0x3231564e planes=2' \
    "compositor imported NV12 dmabuf with two planes"
require_log 'wlcomp: explicit-sync (fenced|immediate) release' \
    "compositor explicit-sync release"
require_log 'mesaglfeature: pass size=32x32' \
    "Mesa GL feature probe first size"
require_log 'mesaglfeature: pass size=64x32' \
    "Mesa GL feature probe resize path"
require_log 'mesaglfeature: ok' \
    "Mesa GL feature probe completion"
require_log 'bo_fd_exports [1-9][0-9]*' "GPU BO fd export accounting"
require_log 'bo_fd_imports [1-9][0-9]*' "GPU BO fd import accounting"
require_log 'fence_fd_exports [1-9][0-9]*' "GPU fence fd export accounting"
require_log 'fence_fd_queries [1-9][0-9]*' "GPU fence fd query accounting"
require_log 'fence_fd_polls [1-9][0-9]*' "GPU fence fd poll accounting"
require_log 'fence_fd_poll_ready [1-9][0-9]*' \
    "GPU ready fence poll accounting"
require_log 'dxgprobe: ok' "dxgprobe success"
require_log 'dxg_adapters3 [1-9][0-9]* handle0=0x[1-9a-fA-F][0-9a-fA-F]*' \
    "DXG enum adapters3 coverage"
require_log 'existing_sysmem_allocation unsupported' \
    "DXG existing-sysmem allocation unsupported marker"
require_log 'dxgprobe: owner isolation rejected foreign destroy' \
    "DXG open-local owner isolation rejection"
require_log 'dxgprobe: owner-isolation ok' \
    "DXG open-local owner isolation success"
require_log 'sync_legacy_signal (ok|failed)' \
    "generic DXG sync signal probe marker"
require_log 'sync_legacy_wait (ok|failed)' \
    "generic DXG sync wait probe marker"
require_log 'sync_gpu2_signal (ok|failed)' \
    "DXG GPU2 sync signal probe marker"
require_log 'dxgprobe: leak_close leaving .* for fd close cleanup' \
    "DXG fd cleanup leak-close marker"
require_log 'dxgprobe: leak-close ok' \
    "DXG fd cleanup leak-close completion"
require_log 'cleanup_attempts:[0-9]+ cleanup_successes:[0-9]+ cleanup_last_ret:0' \
    "DXG fd cleanup success diagnostics"
require_log 'drmgpuprobe: passed' "DRM/GBM probe success"
require_log 'd3dkmt_ioctls=.*ready=1' "D3DKMT ioctl diagnostics"
require_log 'escape_driver_private ok' "DXG escape probe success"
require_log 'dxg_escape_last=.*ret:0' "DXG escape diagnostics"
require_log 'share_object_with_host (ok|failed)' "DXG share-with-host probe"
require_log 'dxg_shareobject_last=len:' "DXG share-with-host diagnostics"
require_log 'share_objects unsupported' "DXG share-objects unsupported marker"
require_log 'open_sync_nt unsupported' "DXG open-sync-from-NT unsupported marker"
require_log 'query_resource_nt unsupported' "DXG query-resource-from-NT unsupported marker"
require_log 'open_resource_nt unsupported' "DXG open-resource-from-NT unsupported marker"
require_log 'dxg_sharedhandle_last=.*ret:-95' "DXG shared-handle diagnostics"
require_log 'change_vidmem_reservation unsupported' "DXG vidmem-reservation unsupported marker"
require_log 'mark_device_error unsupported' "DXG mark-device-error unsupported marker"
require_log 'hwqueue_signal_sync unsupported' "DXG hwqueue signal unsupported marker"
require_log 'hwqueue_wait_sync unsupported' "DXG hwqueue wait unsupported marker"
require_log 'update_alloc_property unsupported' "DXG update-allocation-property unsupported marker"
require_log 'query_clock_calibration unsupported' "DXG clock-calibration unsupported marker"
require_log 'enum_processes unsupported' "DXG enum-processes unsupported marker"
require_log 'dxg_unsupported_last=.*ret:-95' "DXG misc unsupported diagnostics"
require_log 'sync_file_create unsupported' "DXG sync-file create unsupported marker"
require_log 'sync_file_wait unsupported' "DXG sync-file wait unsupported marker"
require_log 'sync_file_open unsupported' "DXG sync-file open unsupported marker"
require_log 'dxg_syncfile_last=.*ret:-95' "DXG sync-file diagnostics"
if [[ "${REQUIRE_BOOT_GLSMOKE}" != "0" ]]; then
    require_log 'glsmoke pid=[0-9]+ exited=1 status=0' \
        "boot-time Mesa Wayland EGL smoke completion"
fi

if grep -Eq 'backend_opengl_submit 1' "${LOG}"; then
    fail "Hyper-V advertised OpenGL submit before real GPUVA submit is validated"
fi
if grep -Eiq 'panic|fatal page fault|coredump|assert' "${LOG}"; then
    fail "guest log contains a crash signature"
fi

echo "hyperv-dxg-validate: passed (${LOG})" | tee -a "${LOG}"
