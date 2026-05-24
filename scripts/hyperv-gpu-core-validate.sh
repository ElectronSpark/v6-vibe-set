#!/usr/bin/env bash
# Build, deploy, and validate the core Hyper-V GPU substrate before FPS/browser
# claims. This lane intentionally uses pure-C guest validators and keeps
# Hyper-V OpenGL-submit gated unless the separate native-present path proves it.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-/tmp/xv6-hyperv-build}
VM_NAME=${VM_NAME:-xv6-os-hyperv}
DEPLOY_VHDX=${DEPLOY_VHDX:-/mnt/c/Temp/xv6-hyperv.vhdx}
OUT_VHDX=${OUT_VHDX:-${BUILD_DIR}/xv6-hyperv-gpu-core-validate.vhdx}
LOG=${GPU_CORE_VALIDATE_LOG:-${BUILD_DIR}/hyperv-gpu-core-validate.log}
VALIDATION_RUN_ID=${VALIDATION_RUN_ID:-core-$(date +%s)-$$}
SERIAL_SCRIPT=${SERIAL_SCRIPT:-C:\\Temp\\com-tcp-read.ps1}
READ_MS=${READ_MS:-240000}
CORE_C_READ_MS=${CORE_C_READ_MS:-180000}
CORE_C_MODE=${CORE_C_MODE:-whole-section}
CORE_C_SECTIONS=${CORE_C_SECTIONS:-"preflight drm dxg-share dxg-sync buffers final"}
BOOT_WAIT_SEC=${BOOT_WAIT_SEC:-20}
STOP_VM_TIMEOUT_SEC=${STOP_VM_TIMEOUT_SEC:-60}
JOBS=${JOBS:-2}
REUSE_RUNNING_VM=${REUSE_RUNNING_VM:-0}
VERBOSE=${VERBOSE:-1}
USE_C_VALIDATOR=${USE_C_VALIDATOR:-1}
KERNEL_BIN=${KERNEL_BIN:-${BUILD_DIR}/kernel/build/kernel/xv6.bin}
ROOTFS_IMG=${ROOTFS_IMG:-${BUILD_DIR}/fs.img}
HYPERV_CMDLINE=${HYPERV_CMDLINE:-BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=0 video=1024x640 acpi_cpus=6 wlcomp_gpu_compose=1 wlcomp_gpu_direct_scanout=1}

fail() {
    echo "hyperv-gpu-core-validate: $*" >&2
    echo "hyperv-gpu-core-validate: log: ${LOG}" >&2
    exit 1
}

need() {
    command -v "$1" >/dev/null || fail "missing required command: $1"
}

serial_read() {
    local cmd=$1
    local read_ms=${2:-${READ_MS}}

    if [[ "${cmd}" == *$'\n'* || "${cmd}" == *$'\r'* ]]; then
        fail "guest command contains a newline and cannot be sent as one transaction"
    fi
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "& '${SERIAL_SCRIPT}' -Cmd '${cmd}' -ReadMs ${read_ms}"
}

log_verbose() {
    if [[ "${VERBOSE}" != "0" ]]; then
        echo "hyperv-gpu-core-validate: $*" | tee -a "${LOG}"
    fi
}

log_guest_step() {
    local state=$1
    local name=$2
    local detail=${3:-}

    log_verbose "guest-step ${state} name=${name} ${detail} host_time=$(date -Is)"
}

core_c_marker() {
    case "$1" in
        preflight) printf 'PF' ;;
        drm) printf 'DRM' ;;
        dxg-share) printf 'DS' ;;
        dxg-sync) printf 'SYNC' ;;
        present-source) printf 'PS' ;;
        buffers) printf 'BUF' ;;
        final) printf 'FIN' ;;
        *)
            printf '%s' "$1" |
                tr 'abcdefghijklmnopqrstuvwxyz-' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ_' |
                cut -c1-8
            ;;
    esac
}

serial_step() {
    local name=$1
    local proof=$2
    local read_ms=$3
    local cmd=$4
    local out
    local rc

    log_guest_step start "${name}" "read_ms=${read_ms} proof='${proof}'"
    log_verbose "guest-step command name=${name} cmd=${cmd}"
    set +e
    out=$(serial_read "${cmd}" "${read_ms}" 2>&1)
    rc=$?
    set -e
    printf '%s\n' "${out}" | tee -a "${LOG}"
    log_guest_step end "${name}" "rc=${rc}"
    if [[ "${rc}" -ne 0 ]]; then
        echo "hyperv-gpu-core-validate: guest-step failed name=${name} rc=${rc}" >&2
        echo "hyperv-gpu-core-validate: log: ${LOG}" >&2
        exit "${rc}"
    fi
}

query_vm_state() {
    local out
    local rc
    local state

    set +e
    out=$(powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Get-VM -Name '${VM_NAME}' -ErrorAction Stop | Select-Object -ExpandProperty State" 2>&1)
    rc=$?
    set -e
    printf '%s\n' "${out}" | tee -a "${LOG}"
    if [[ "${rc}" -ne 0 ]]; then
        fail "failed to query Hyper-V VM state for ${VM_NAME}"
    fi
    state=$(printf '%s\n' "${out}" | tr -d '\r' |
        sed -n '/[^[:space:]]/{$p;}' |
        sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//')
    [[ -n "${state}" ]] || fail "Hyper-V VM state query returned no state"
    printf '%s\n' "${state}"
}

stop_vm_for_deploy() {
    local state
    local rc

    state=$(query_vm_state | tail -n 1)
    echo "hyperv-gpu-core-validate: ${VM_NAME} pre-deploy state=${state}" |
        tee -a "${LOG}"
    if [[ "${state}" == "Off" ]]; then
        return
    fi

    set +e
    timeout "${STOP_VM_TIMEOUT_SEC}s" \
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Stop-VM -Name '${VM_NAME}' -TurnOff -Force -ErrorAction Stop" \
        >>"${LOG}" 2>&1
    rc=$?
    set -e
    if [[ "${rc}" -ne 0 ]]; then
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
            "Get-VM -Name '${VM_NAME}' | Select Name,State,Uptime | Format-List" 2>&1 |
            tee -a "${LOG}" >&2 || true
        fail "failed to stop ${VM_NAME} before deploy"
    fi

    state=$(query_vm_state | tail -n 1)
    [[ "${state}" == "Off" ]] ||
        fail "Stop-VM completed but ${VM_NAME} state is ${state}, expected Off"
}

require_log() {
    local pattern=$1
    local why=$2

    if ! grep -Eq "${pattern}" < <(tr -d '\r' <"${LOG}"); then
        {
            echo "hyperv-gpu-core-validate: missing ${why}"
            echo "hyperv-gpu-core-validate: searched_regex=${pattern}"
            echo "hyperv-gpu-core-validate: recent section markers:"
            tr -d '\r' <"${LOG}" | grep -En '__CORE_|__CV_|guest-step|status=FAIL|status=PASS' | tail -40
            echo "hyperv-gpu-core-validate: related validator summaries:"
            tr -d '\r' <"${LOG}" |
                grep -Ein 'admission|seal|mutation|lifetime|opensync|nouveau|backend_opengl|bo_fd_live|fence_fd_live' |
                tail -80
            echo "hyperv-gpu-core-validate: log tail:"
            tail -120 "${LOG}"
        } >&2
        fail "missing ${why}"
    fi
}

reject_log() {
    local pattern=$1
    local why=$2

    if grep -Eiq "${pattern}" < <(tr -d '\r' <"${LOG}"); then
        fail "found ${why}"
    fi
}

reject_guest_log() {
    local pattern=$1
    local why=$2

    if awk '
        /hyperv-gpu-core-validate: running pure-C GPU validators/ { guest=1 }
        guest { print }
    ' "${LOG}" | tr -d '\r' | grep -Eiq "${pattern}"; then
        fail "found ${why}"
    fi
}

last_log_uint() {
    local key=$1

    sed -nE "s/^${key}[[:space:]]+([0-9]+).*$/\\1/p" "${LOG}" |
        tail -n 1
}

require_nouveau_dda_or_gpup_fail_closed() {
    local accepts

    require_log 'nouveau_pci_registered 1' "Nouveau PCI driver registration"
    accepts=$(last_log_uint nouveau_pci_probe_accepts)
    [[ -n "${accepts}" ]] ||
        fail "missing Nouveau PCI acceptance counter"

    if (( accepts > 0 )); then
        require_log 'nouveau_pci_bar[01]_len [1-9][0-9]*' \
            "accepted DDA/Nouveau PCI function has a usable BAR aperture"
        require_log 'backend_dda_nouveau 1' \
            "backend advertises accepted DDA/Nouveau PCI capability"
        require_log 'drm_node (primary|render) path=/dev/dri/(card0|renderD128) driver=nouveau unique=pci:' \
            "DRM identity switched to native Nouveau PCI device"
        echo "hyperv-gpu-core-validate: DDA/Nouveau PCI function accepted; GPU-P-only fail-closed checks skipped" |
            tee -a "${LOG}"
        return
    fi

    require_log 'nouveau_pci_probe_accepts 0' \
        "no DDA/Nouveau PCI function accepted on the GPU-P Hyper-V image"
    require_log 'backend_dda_nouveau 0' \
        "backend does not advertise DDA/Nouveau on GPU-P-only image"
    require_log 'nouveau_pci_probe_reject_(dxg_present|no_bars) [1-9][0-9]*' \
        "Nouveau DDA probe rejected because no BAR-backed DDA device is assigned"
    require_log 'dxg_present_dxg_state_names .*paravirtualized.*no_display.*no_sources' \
        "GPU-P adapter is render/paravirtualized but has no display sources"
    require_log 'dxg_present_dxg_adapter_type_wsl 0x[1-9a-f][0-9a-f]*' \
        "WSL-shaped GPU-P adapter-type diagnostic"
    require_log 'dxg_present_dxg_adapter_render_supported 1' \
        "WSL-shaped GPU-P adapter remains render-capable"
    require_log 'dxg_present_dxg_adapter_display_supported 0' \
        "WSL-shaped GPU-P adapter display capability is suppressed"
    require_log 'dxg_present_dxg_adapter_paravirtualized 1' \
        "WSL-shaped GPU-P adapter is paravirtualized"
    require_log 'dxg_present_dxg_adapter_sources_known 1' \
        "DXG adapter source-count diagnostic is current"
    require_log 'dxg_present_dxg_adapter_sources 0' \
        "GPU-P adapter exposes zero DXG display sources"
    require_log 'dxg_present_dda_nouveau_present 0' \
        "no DDA/Nouveau PCI candidate is present on GPU-P-only image"
    require_log 'dxg_present_dda_nouveau_import_path_present 0' \
        "DDA/Nouveau D3D12 import path is not fabricated"
    require_log 'dxg_present_dda_nouveau_scanout_bind_present 0' \
        "DDA/Nouveau scanout bind path is not fabricated"
    require_log 'nouveau_gpup_failclosed_matrix .*accepts=0 .*backend_dda_nouveau=0 .*reject_reason=PASS .*no_fake_bar=PASS .*no_fake_dma=PASS .*no_fake_irq=PASS .*no_fake_getparam=PASS .*no_fake_present=PASS .*native_present_credit=0 .*opengl_submit_credit=0 .*status=PASS' \
        "GPU-P-only Nouveau fail-closed matrix"
}

log_metadata() {
    local label=$1

    {
        echo "hyperv-gpu-core-validate: metadata ${label}"
        echo "validation_run_id=${VALIDATION_RUN_ID}"
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

log_host_gpu_assignment() {
    {
        echo "hyperv-gpu-core-validate: host GPU-P partition adapter"
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
            "Get-VMGpuPartitionAdapter -VMName '${VM_NAME}' -ErrorAction SilentlyContinue | Format-List VMName,InstancePath,CurrentPartitionVRAM,CurrentPartitionEncode,CurrentPartitionDecode,CurrentPartitionCompute,PartitionId,PartitionVfLuid"
        echo "hyperv-gpu-core-validate: host partitionable GPUs"
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
            "Get-VMHostPartitionableGpu -ErrorAction SilentlyContinue | Format-List Name,ValidPartitionCounts,PartitionCount,TotalVRAM,AvailableVRAM,TotalCompute,AvailableCompute"
        echo "hyperv-gpu-core-validate: host DDA assignable devices"
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
            "Get-VMAssignableDevice -VMName '${VM_NAME}' -ErrorAction SilentlyContinue | Format-List *"
    } >>"${LOG}" 2>&1 || true
}

need cmake
need powershell.exe
need cp
need grep
need sed
need sleep
need timeout

[[ "${VALIDATION_RUN_ID}" =~ ^[A-Za-z0-9_.:-]+$ ]] ||
    fail "VALIDATION_RUN_ID contains unsupported characters: ${VALIDATION_RUN_ID}"

mkdir -p "${BUILD_DIR}"
: >"${LOG}"
echo "hyperv-gpu-core-validate: validation_run_id=${VALIDATION_RUN_ID}" |
    tee -a "${LOG}"
log_metadata start

if [[ "${REUSE_RUNNING_VM}" != "1" ]]; then
    echo "hyperv-gpu-core-validate: building kernel" | tee -a "${LOG}"
    cmake --build "${BUILD_DIR}" --target kernel -j"${JOBS}" | tee -a "${LOG}"

    echo "hyperv-gpu-core-validate: building rootfs" | tee -a "${LOG}"
    cmake --build "${BUILD_DIR}" --target rootfs -j"${JOBS}" | tee -a "${LOG}"

    echo "hyperv-gpu-core-validate: building ${OUT_VHDX}" | tee -a "${LOG}"
    HYPERV_CMDLINE="${HYPERV_CMDLINE}" \
        "${REPO_ROOT}/scripts/make-hyperv-image.sh" \
        "${KERNEL_BIN}" "${ROOTFS_IMG}" "${OUT_VHDX}" 0 | tee -a "${LOG}"
    [[ -s "${OUT_VHDX}" ]] || fail "VHDX was not created: ${OUT_VHDX}"
    log_metadata built

    echo "hyperv-gpu-core-validate: deploying to ${VM_NAME}" | tee -a "${LOG}"
    stop_vm_for_deploy
    cp -f "${OUT_VHDX}" "${DEPLOY_VHDX}"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Set-VMProcessor -VMName '${VM_NAME}' -Count 6; Start-VM -Name '${VM_NAME}'; Get-VM -Name '${VM_NAME}' | Select Name,State,Uptime,ProcessorCount | Format-List" |
        tee -a "${LOG}"
    log_metadata deployed

    echo "hyperv-gpu-core-validate: waiting ${BOOT_WAIT_SEC}s for guest shell" |
        tee -a "${LOG}"
    sleep "${BOOT_WAIT_SEC}"
else
    echo "hyperv-gpu-core-validate: reusing running ${VM_NAME}" | tee -a "${LOG}"
fi

log_host_gpu_assignment

for attempt in 1 2 3 4; do
    ready=$(serial_read 'echo __CORE_READY_BEGIN__; echo hyperv-gpu-core-ready; echo __CORE_READY_END__' 10000 || true)
    printf '%s\n' "${ready}" | tee -a "${LOG}"
    clean=$(printf '%s\n' "${ready}" | tr -d '\r')
    if printf '%s\n' "${clean}" | grep -Fxq '__CORE_READY_BEGIN__' &&
       printf '%s\n' "${clean}" | grep -Fxq 'hyperv-gpu-core-ready' &&
       printf '%s\n' "${clean}" | grep -Fxq '__CORE_READY_END__'; then
        break
    fi
    [[ "${attempt}" -lt 4 ]] || fail "guest shell did not become ready"
    sleep 5
done

echo "hyperv-gpu-core-validate: running pure-C GPU validators" | tee -a "${LOG}"
if [[ "${USE_C_VALIDATOR}" == "1" ]]; then
    if [[ "${CORE_C_MODE}" == "sections" ]]; then
        for section in ${CORE_C_SECTIONS}; do
            marker=$(core_c_marker "${section}")
            serial_step "gpu-core-c-${section}" \
                "guest C section validator owns ${section} GPU pass/fail checks" \
                "${CORE_C_READ_MS}" \
                "echo __CV_${marker}_B__; gpucorevalidate --section ${section}; echo __CV_${marker}_E__"

            require_log "^__CV_${marker}_B__$" \
                "C ${section} section begin marker"
            require_log "^__CV_${marker}_E__$" \
                "C ${section} section end marker"
            require_log "^gpu_core_c_validator section=${section} status=PASS$" \
                "guest C ${section} section pass"
            require_log "^gpu_core_c_validator status=PASS section=${section}$" \
                "guest C ${section} aggregate pass"
        done
    elif [[ "${CORE_C_MODE}" == "whole-section" ]]; then
        serial_step "gpu-core-c" \
            "guest C whole-section validator runs after implementation section completion" \
            "${CORE_C_READ_MS}" \
            'echo __CORE_CVALIDATE_BEGIN__; gpucorevalidate; echo __CORE_CVALIDATE_END__'

        require_log '^__CORE_CVALIDATE_BEGIN__$' \
            "C whole-section validator begin marker"
        require_log '^__CORE_CVALIDATE_END__$' \
            "C whole-section validator end marker"
        require_log '^gpu_core_c_validator status=PASS$' \
            "guest C whole-section validator pass"
    else
        fail "unknown CORE_C_MODE=${CORE_C_MODE} (expected whole-section or sections)"
    fi

    reject_guest_log '^gpu_core_c_validator .*status=FAIL' \
        "guest C aggregate validator failure"
    reject_guest_log 'panic|fatal page fault|coredump|(^|[^[:alpha:]])assert([^[:alpha:]]|$)|device removal|Removing Device' \
        "guest crash or D3D12 device-removal signature"

    log_metadata passed
    echo "hyperv-gpu-core-validate: passed validation_run_id=${VALIDATION_RUN_ID} (${LOG})" |
        tee -a "${LOG}"
    exit 0
fi

serial_step "cmdline" \
    "focused boot line, uptime, and 6-vCPU evidence" 30000 \
    'echo __CORE_CMDLINE_BEGIN__; cat /proc/cmdline; cat /proc/uptime; echo __CORE_CMDLINE_END__'
serial_step "ttm" \
    "TTM placement, eviction, and accounting validator" 90000 \
    'echo __CORE_TTM_BEGIN__; ttmtest; echo __CORE_TTM_END__'
serial_step "drm-gem-kms" \
    "DRM node policy, GEM, KMS framebuffer, and syncobj validator" 120000 \
    'echo __CORE_DRM_BEGIN__; drmiftest; echo __CORE_DRM_END__'
serial_step "prime" \
    "DRM PRIME export/import fd lifetime validator" 90000 \
    'echo __CORE_PRIME_BEGIN__; drmprimeprobe; echo __CORE_PRIME_END__'
serial_step "dxg-admission" \
    "negative admission, wrong-kind fd stability, and valid NT resource share" 180000 \
    'echo __CORE_DXG_ADMISSION_BEGIN__; dxgprobe --shared-admission-negative-validate; cat /dev/dxg; echo __CORE_DXG_ADMISSION_END__'
serial_step "dxg-qai-admission" \
    "direct and adapter-list QAI admission evidence with OpenGL-submit gate" 120000 \
    'echo __CORE_DXG_QAI_BEGIN__; dxgprobe --qai-admission-validate; cat /dev/dxg; fbstat; echo __CORE_DXG_QAI_END__'
serial_step "dxg-handle-lifetime" \
    "WSL-shaped local adapter and object handle tombstone validator" 180000 \
    'echo __CORE_DXG_HANDLE_BEGIN__; dxgprobe --handle-lifetime-validate; cat /dev/dxg; echo __CORE_DXG_HANDLE_END__'
serial_step "dxg-create-publication" \
    "WSL-shaped create copyout unwind and no-local-publication validator" 180000 \
    'echo __CORE_DXG_CREATEPUB_BEGIN__; dxgprobe --create-publication-faults-validate; cat /dev/dxg; echo __CORE_DXG_CREATEPUB_END__'
serial_step "dxg-seal-provenance" \
    "shared-resource seal, metadata provenance, and no-present-credit validator" 180000 \
    'echo __CORE_DXG_SEAL_BEGIN__; dxgprobe --shared-seal-provenance-validate; cat /dev/dxg; echo __CORE_DXG_SEAL_END__'
serial_step "dxg-mutation" \
    "sealed shared-resource mutation rejection and canonical record immutability" 180000 \
    'echo __CORE_DXG_MUTATION_BEGIN__; dxgprobe --shared-mutation-validate; cat /dev/dxg; echo __CORE_DXG_MUTATION_END__'
serial_step "dxg-lifetime" \
    "shared-resource repeated open/query, child import, destroy, and close lifetime" 180000 \
    'echo __CORE_DXG_LIFETIME_BEGIN__; dxgprobe --shared-lifetime-validate; cat /dev/dxg; echo __CORE_DXG_LIFETIME_END__'
serial_step "dxg-sync-nt" \
    "WSL-natural shared sync-object export/open/fence validator" 180000 \
    'echo __CORE_DXG_SYNCNT_BEGIN__; dxgprobe --sync-only; cat /dev/dxg; echo __CORE_DXG_SYNCNT_END__'
serial_step "dxg-cpu-event-signal" \
    "WSL-shaped enqueue_cpu_event signal packet validator" 180000 \
    'echo __CORE_DXG_CPUEVENT_BEGIN__; dxgprobe --try-submit; cat /dev/dxg; echo __CORE_DXG_CPUEVENT_END__'
serial_step "dxg-syncfile" \
    "WSL-style DXG sync-file open/wait/unwind validator" 180000 \
    'echo __CORE_DXG_SYNCFILE_BEGIN__; dxgprobe --syncfile; cat /dev/dxg; echo __CORE_DXG_SYNCFILE_END__'
serial_step "dxg-wsl-replay" \
    "same-adapter WSL-shaped D3DKMT replay packet/signature validator" 180000 \
    'echo __CORE_DXG_WSL_REPLAY_BEGIN__; dxgprobe --wsl-trace-replay; cat /dev/dxg; echo __CORE_DXG_WSL_REPLAY_END__'
serial_step "dxg-present-source" \
    "fail-closed DXG present-source and bind-contract validator" 180000 \
    'echo __CORE_DXG_PRESENT_BEGIN__; dxgprobe --present-source-failclosed-validate; cat /dev/dxg; fbstat; echo __CORE_DXG_PRESENT_END__'
serial_step "gpubuf" \
    "BO/fence fd lifecycle validator" 120000 \
    'echo __CORE_GPUBUF_BEGIN__; gpubuftest 3; echo __CORE_GPUBUF_END__'
serial_step "render-owner" \
    "render fd ownership and imported BO isolation validator" 90000 \
    'echo __CORE_OWNER_BEGIN__; gpubuftest --render-owner; echo __CORE_OWNER_END__'
serial_step "nouveau" \
    "Nouveau ABI or GPU-P/DDA fail-closed validator" 120000 \
    'echo __CORE_NOUVEAU_BEGIN__; nouveauabitest; echo __CORE_NOUVEAU_END__'
serial_step "fbstat" \
    "final backend gates, fd cleanup, DDA/GPU-P evidence, and OpenGL-submit state" 30000 \
    'echo __CORE_FBSTAT_BEGIN__; fbstat; cat /proc/uptime; echo __CORE_FBSTAT_END__'

require_log '^__CORE_CMDLINE_BEGIN__$' "command-line begin marker"
require_log '^__CORE_CMDLINE_END__$' "command-line end marker"
require_log '^__CORE_TTM_BEGIN__$' "TTM begin marker"
require_log '^__CORE_TTM_END__$' "TTM end marker"
require_log '^__CORE_DRM_BEGIN__$' "DRM begin marker"
require_log '^__CORE_DRM_END__$' "DRM end marker"
require_log '^__CORE_PRIME_BEGIN__$' "PRIME begin marker"
require_log '^__CORE_PRIME_END__$' "PRIME end marker"
require_log '^__CORE_DXG_ADMISSION_BEGIN__$' "DXG shared-resource admission begin marker"
require_log '^__CORE_DXG_ADMISSION_END__$' "DXG shared-resource admission end marker"
require_log '^__CORE_DXG_QAI_BEGIN__$' "DXG QAI admission begin marker"
require_log '^__CORE_DXG_QAI_END__$' "DXG QAI admission end marker"
require_log '^__CORE_DXG_HANDLE_BEGIN__$' "DXG handle lifetime begin marker"
require_log '^__CORE_DXG_HANDLE_END__$' "DXG handle lifetime end marker"
require_log '^__CORE_DXG_SEAL_BEGIN__$' "DXG seal-provenance begin marker"
require_log '^__CORE_DXG_SEAL_END__$' "DXG seal-provenance end marker"
require_log '^__CORE_DXG_MUTATION_BEGIN__$' "DXG seal-mutation begin marker"
require_log '^__CORE_DXG_MUTATION_END__$' "DXG seal-mutation end marker"
require_log '^__CORE_DXG_LIFETIME_BEGIN__$' "DXG lifetime begin marker"
require_log '^__CORE_DXG_LIFETIME_END__$' "DXG lifetime end marker"
require_log '^__CORE_DXG_SYNCNT_BEGIN__$' "DXG sync NT-sharing begin marker"
require_log '^__CORE_DXG_SYNCNT_END__$' "DXG sync NT-sharing end marker"
require_log '^__CORE_DXG_SYNCFILE_BEGIN__$' "DXG sync-file begin marker"
require_log '^__CORE_DXG_SYNCFILE_END__$' "DXG sync-file end marker"
require_log '^__CORE_DXG_PRESENT_BEGIN__$' "DXG present-source begin marker"
require_log '^__CORE_DXG_PRESENT_END__$' "DXG present-source end marker"
require_log '^__CORE_GPUBUF_BEGIN__$' "BO/fence begin marker"
require_log '^__CORE_GPUBUF_END__$' "BO/fence end marker"
require_log '^__CORE_OWNER_BEGIN__$' "render-owner begin marker"
require_log '^__CORE_OWNER_END__$' "render-owner end marker"
require_log '^__CORE_NOUVEAU_BEGIN__$' "Nouveau begin marker"
require_log '^__CORE_NOUVEAU_END__$' "Nouveau end marker"
require_log '^__CORE_FBSTAT_BEGIN__$' "fbstat begin marker"
require_log '^__CORE_FBSTAT_END__$' "fbstat end marker"
require_log 'netsurf=0' "focused command line with NetSurf disabled"
require_log 'webkit=0' "focused command line with WebKit disabled"
require_log 'glsmoke=0' "focused command line with boot demo disabled"
require_log 'acpi_cpus=6' "6 vCPU command line"
require_log 'ttmtest: ok' "TTM validator"
require_log 'drmiftest: ok' "DRM/GEM/KMS validator"
require_log 'drmiftest: kms fb ok' "KMS framebuffer/atomic validator"
require_log 'drmprimeprobe: ok' "DRM PRIME validator"
require_log 'shared_admission_negative_matrix .*wrong_kind_fd_unchanged=1 .*foreign_process_device_status=0 .*stale_fd_unchanged=1 .*owner_allocation_present=1 .*failed_admission_no_fd_publish=1 .*failed_partial_record_absent=1 .*failed_partial_record_reusable=0 .*status=PASS' \
    "shared-resource local admission negative validator"
require_log 'qai_admission_selection .*direct_count=[1-9][0-9]* .*list_count=[1-9][0-9]* .*direct_list_luid_match=1 .*create_adapter_list_d3d12_graphics=modelled-by-enum3' \
    "DXG QAI direct/list adapter selection evidence"
require_log 'qai_admission_row route=direct-openadapterfromluid type=0 requested_size=9300 ' \
    "DXG QAI direct type0 row"
require_log 'qai_admission_row route=direct-openadapterfromluid type=27 requested_size=4 ' \
    "DXG QAI direct type27 row"
require_log 'qai_admission_row route=direct-openadapterfromluid type=48 requested_size=4096 ' \
    "DXG QAI direct type48 row"
require_log 'qai_admission_row route=create-adapter-list-d3d12-graphics type=0 requested_size=9300 ' \
    "DXG QAI adapter-list type0 row"
require_log 'qai_admission_row route=create-adapter-list-d3d12-graphics type=27 requested_size=4 ' \
    "DXG QAI adapter-list type27 row"
require_log 'qai_admission_row route=create-adapter-list-d3d12-graphics type=48 requested_size=4096 ' \
    "DXG QAI adapter-list type48 row"
require_log 'qai_admission_matrix .*direct_list_luid_match=1 .*backend_opengl_submit=0 .*wsl_trace=/tmp/xv6-wsl-probe/wave77-wsl-qai-admission-type0-9300.trace .*divergence_class=nonblocking-admission-proven-before-export .*status=PASS' \
    "DXG QAI xv6-vs-WSL admission classification"
require_log 'local_adapter_reuse ok .*delayed:[0-9]+->[1-9][0-9]* .*min_free=128' \
    "WSL-style local adapter min-free reuse delay"
require_log 'handle_lifetime_stale_matrix .*device_second_fd_rejected=1 .*context_rejected=1 .*hwqueue_rejected=1 .*hwqueue_sync_rejected=1 .*sync_rejected=1 .*paging_queue_rejected=1 .*paging_queue_sync_rejected=1 .*resource_rejected=1 .*allocation_rejected=1 .*gpuva_rejected=1 .*device_final_rejected=1 .*status=PASS' \
    "WSL-style stale DXG object rejection matrix"
require_log 'handle_lifetime_stale_matrix .*object_classes=device,context,hwqueue,hwqueue_sync,sync,paging_queue,paging_queue_sync,resource,allocation,gpuva' \
    "WSL-style stale DXG object matrix names every tracked class"
require_log 'dxg_process_mem_lifetime_matrix .*child_status=0 .*object_release_delta=[1-9][0-9]* .*mem_release_delta=[1-9][0-9]* .*mem_free_delta=[1-9][0-9]* .*status=PASS' \
    "WSL-style split DXG process object and memory lifetime"
require_log 'dxgprocess_adapter_matrix .*raw_host_create_rc=-1 .*local_create_rc=0 .*close_adapter_rc=0 .*child_destroy_after_final_close_rc=-1 .*status=PASS' \
    "WSL-style per-process adapter rejects raw host adapter and destroys child device on final close"
require_log 'dxgprocess_adapter_parent_matrix .*child_status=0 .*status=PASS' \
    "WSL-style per-process adapter validator child exited cleanly"
require_log 'dxg_createdevice_copyout_unwind_matrix .*rc=-14 .*destroy_retry_rc=-[0-9]+ .*unwind_ret=0 .*no_local_publication=1 .*status=PASS' \
    "WSL-style CREATEDEVICE post-host-create copyout unwind"
require_log 'dxg_createcontext_copyout_unwind_matrix .*rc=-14 .*destroy_retry_rc=-[0-9]+ .*unwind_ret=0 .*no_local_publication=1 .*status=PASS' \
    "WSL-style CREATECONTEXTVIRTUAL post-host-create copyout unwind"
require_log 'dxg_createhwqueue_copyout_unwind_matrix .*rc=-14 .*destroy_queue_retry_rc=-[0-9]+ .*destroy_fence_retry_rc=-[0-9]+ .*unwind_ret=0 .*no_local_publication=1 .*status=PASS' \
    "WSL-style CREATEHWQUEUE post-host-create copyout unwind"
require_log 'dxg_create_publication_faults_matrix device=PASS context=PASS hwqueue=PASS status=PASS' \
    "aggregate create-publication fault validator"
require_log 'd3dkmt_cleanup_wsl_order=.*sync:[0-9]+ .*allocation:[0-9]+ .*resource:[0-9]+ .*context:[0-9]+ .*hwqueue:[0-9]+ .*paging:[0-9]+ .*device:[0-9]+ .*process:[0-9]+ .*valid:1' \
    "WSL-style final-close DXG teardown order diagnostics"
require_log 'createallocation_unwind_matrix .*create_rc=-14 .*destroy_ctx=5 .*destroy_count=1 .*destroy_ret=0 .*same_process_cleanup=1 .*pin_balanced=1 .*no_local_leak=1 .*status=PASS' \
    "WSL-style CREATEALLOCATION copyout-failure cleanup validator"
require_log 'openresource_unwind_matrix .*open_rc=-14 .*destroy_ctx=1 .*destroy_ret=0 .*same_process_cleanup=1 .*pin_balanced=1 .*no_local_leak=1 .*status=PASS' \
    "WSL-style OPENRESOURCE copyout-failure cleanup validator"
require_log 'handle_lifetime ok .*reuse_delayed:[0-9]+ .*min_free:128 .*free_count:[0-9]+ .*free_head:[0-9]+ .*free_tail:[0-9]+' \
    "WSL-style object handle tombstone lifetime"
require_log 'shared_resource_seal_provenance_matrix .*fd_cloexec=1 .*record_generation_coherent=1 .*canonical_record_coherent=1 .*shared_model_coherent=1 .*status=PASS' \
    "canonical shared-resource record seal/open/close validator"
require_log 'shared_mutation_rejection_matrix .*append_rc=-[0-9]+ .*private_rc=-[0-9]+ .*size_flag_rc=-[0-9]+ .*owner_status=0 .*private_rewrite_rejects=[0-9]+->[1-9][0-9]* .*size_flag_rewrite_rejects=[0-9]+->[1-9][0-9]* .*owner_rewrite_rejects=[0-9]+->[1-9][0-9]* .*record_same=1 .*record_mutated=0 .*status=PASS' \
    "sealed shared-resource mutation rejection validator"
require_log 'shared_lifetime_record_matrix .*livefd=[0-9]+->[0-9]+->0 .*same_record=1 .*owner_preserved=1 .*exporter_destroyed=1 .*status=PASS' \
    "canonical shared-resource record repeated/child/destroy/close validator"
require_log 'opensync_layout_source_matrix .*nt_handle_source=shareobjects_fd .*same_open_rc=0 .*same_open_ok=1 .*same_fence_cpu=0x[1-9a-f][0-9a-f]* .*same_fence_gpu=0x[1-9a-f][0-9a-f]* .*child_process_attempted=1 .*child_status=0 .*status=PASS' \
    "WSL-natural shared sync-object export/open validator"
require_log 'sync_signal_cpu_event_matrix .*rc=0 .*objects=0 contexts=1 .*flags=0x2 .*host_events=[0-9]+/[0-9]+/[0-9]+->[0-9]+/[1-9][0-9]*/[0-9]+ .*status=PASS' \
    "WSL-style SIGNALSYNCHRONIZATIONOBJECT enqueue_cpu_event validator"
require_log 'sync_fromcpu_cpu_event_failclosed_matrix .*rc=-22 expected=-22 status=PASS' \
    "DXG FROMCPU enqueue_cpu_event fail-closed validator"
require_log 'sync_gpu2_cpu_event_matrix .*rc=0 .*objects=0 contexts=1 .*flags=0x2 .*host_events=[0-9]+/[0-9]+/[0-9]+->[0-9]+/[1-9][0-9]*/[0-9]+ .*status=PASS' \
    "WSL-style SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 enqueue_cpu_event validator"
require_log 'dxg_synccpuevent_signal=.*successes:[1-9][0-9]* .*ret:0 .*flags:0x2 objects:0 contexts:1 .*event:[1-9][0-9]*' \
    "DXG CPU-event signal packet diagnostics"
require_log 'dxg_async_message_matrix .*status=(PASS|DEFERRED)' \
    "WSL async-message send-path validator"
require_log 'dxg_async_send_last=enabled:[01] attempts:[0-9]+ successes:[0-9]+ fallback_sync:[0-9]+ cmd:[0-9]+ cmd_len:[0-9]+ wire_len:[0-9]+ async_bit:[01] route_global:[01] .*packet_type:[0-9]+ ret:0' \
    "DXG async-message packet diagnostics"
require_log 'sync_file_matrix .*create_rc=0 .*wait_rc=0 .*open_rc=0 .*open_sync=0x[1-9a-f][0-9a-f]* .*child_status=0 .*child_rc=0' \
    "WSL-style sync-file create/wait/open child validator"
require_log 'dxg_syncfile_create_unwind_matrix .*create_fault_rc=-14 .*fd_visible=0 .*fd_reclaimed=[0-9]+->[1-9][0-9]* .*event_removed=[0-9]+->[1-9][0-9]* .*balanced=1 .*status=PASS' \
    "WSL-style sync-file create copyout-failure cleanup validator"
require_log 'dxg_syncfile_open_unwind_matrix .*open_fault_rc=-14 .*open_destroy=[0-9]+/[0-9]+->[1-9][0-9]*/[1-9][0-9]* .*destroy_ret=0 .*source_fd_valid=1 .*no_local_leak=1 .*status=PASS' \
    "WSL-style sync-file open copyout-failure cleanup validator"
require_log 'dxg_syncfile_lifetime=.*open_faults:[1-9][0-9]* .*open_destroy:[1-9][0-9]*/[1-9][0-9]*/0 .*host_events:0/[1-9][0-9]*/[1-9][0-9]*' \
    "DXG sync-file lifetime and host-event diagnostics"
require_log '^__CORE_DXG_WSL_REPLAY_BEGIN__$' \
    "WSL replay begin marker"
require_log '^__CORE_DXG_WSL_REPLAY_END__$' \
    "WSL replay end marker"
require_log 'wsl_trace_replay_packet_matrix stage=create_device .*status=PASS' \
    "WSL replay CREATEDEVICE packet matrix"
require_log 'wsl_trace_replay_packet_matrix stage=create_allocation .*status=PASS' \
    "WSL replay CREATEALLOCATION packet matrix"
require_log 'wsl_trace_replay_packet_matrix stage=submit_hwqueue .*expected_reject:1 .*status=PASS' \
    "WSL replay synthetic HWQUEUE submit rejection matrix"
require_log 'wsl_trace_replay_signature .*trace=/tmp/xv6-wsl-probe/mesaglfeature-nvidia-live.trace .*equivalence=synthetic_invalid_parameter .*same_adapter_source=selected_openadapter_luid .*status=PASS' \
    "same-adapter WSL trace replay signature"
require_log 'dxg_opensync_envelope=route:global cmd:40 wire:56 ext:1 eoff:16 result:24 actual:24 ret:0 status:0x0 .* fd_kind:1 fd_refs:[1-9][0-9]*' \
    "WSL-natural OPENSYNCOBJECT envelope"
require_log 'dxg_opensync_shape=.* off_dev:24 off_global:28 off_flags:36' \
    "WSL-natural OPENSYNCOBJECT field offsets"
require_log 'dxg_sharedfd_close=kind:[12] .*destroy_ret:0 status:0x0 actual:[1-9][0-9]* cmd:32 wire:48 ext:1 eoff:16 result_ntstatus:4 handle_off:24' \
    "WSL-natural DESTROYNTSHAREDOBJECT close path"
require_log 'dxg_sharedresource_record=.* sealed:1 .* query:[1-9][0-9]* open:[1-9][0-9]* fd:[1-9][0-9]* livefd:0 imports:[1-9][0-9]* .* mutated:0' \
    "canonical shared-resource record live counters"
require_log 'present_bind_contract_skeleton_matrix .*source=0x[1-9a-fA-F][0-9a-fA-F]* .*source_live=1 .*source_generation=[1-9][0-9]* .*resource_generation=[1-9][0-9]* .*completion_source=3 .*present_id=0 completed=0 .*native_present_claim=0 .*status=PASS' \
    "fail-closed source-owned DXG bind-contract skeleton"
require_log 'present_bind_contract_stale_source_matrix .*source=0x[1-9a-fA-F][0-9a-fA-F]* .*source_live=0 .*block_reason=0x[1-9a-fA-F][0-9a-fA-F]* .*completion_source=3 .*present_id=0 completed=0 .*native_present_claim=0 .*status=PASS' \
    "stale present-source bind-contract rejection"
require_log 'present_bind_contract_foreign_source_matrix .*source=0x[1-9a-fA-F][0-9a-fA-F]* .*source_live=0 .*source_generation=0 .*block_reason=0x[1-9a-fA-F][0-9a-fA-F]* .*completion_source=3 .*present_id=0 completed=0 .*native_present_claim=0 .*status=PASS' \
    "foreign present-source bind-contract rejection"
require_log 'present_source_failclosed_matrix .*bind_contract_rc=-[0-9]+ .*hyperv_opengl_submit=0 .*failclosed=1 .*no_present_credit=1 .*owner_cleanup=1 .*native_present_claim=0 .*status=PASS' \
    "present-source fail-closed no-credit validator"
require_log 'gpubuftest: completed 3 buffer cycles' "BO/fence validator"
require_log 'gpubuftest: render fd ownership verified' "render-fd ownership validator"
require_log 'nouveauabitest: .*ok' "Nouveau ABI validator"
require_log 'nouveau_mesa_smoke_gate_matrix .*synthetic_gpup_rejected=PASS .*mesa_nvif_enabled=0 .*status=PASS' \
    "Nouveau Mesa smoke gate"
require_log 'gpu_diagnostics_separation_matrix .*generic_scanout=drm-kms-fb .*d3d12_present=dxg-present .*webkit_policy=separate .*ioctl_trace_label=fb-gpu-trace .*generic_scanout_native_present_credit=0 .*opengl_submit_credit=0 .*status=PASS' \
    "generic DRM/KMS diagnostics stay separate from D3D12/WebKit gates"
require_log 'dxg_present_lane_selection_matrix .*selected=gpup_dxg_scanout_bind .*working_model=dxg_resource_scanout_bind .*wslg_display_channel=0 .*synthvid_vram_bridge=gpa_dirty_only .*gpup_or_dda_required=1 .*custom_host_tool=0 .*native_present_credit=0 .*opengl_submit_credit=0 .*status=PASS' \
    "selected native-present display handoff lane"
require_log 'backend_opengl_submit 0' "Hyper-V OpenGL-submit remains gated"
require_log 'backend_opengl_submit_gate closed' "Hyper-V OpenGL-submit gate closed"
require_nouveau_dda_or_gpup_fail_closed
require_log 'bo_fd_live 0' "BO fd cleanup"
require_log 'fence_fd_live 0' "fence fd cleanup"
reject_log 'backend_opengl_submit 1' \
    "Hyper-V OpenGL-submit claim before native-present/FPS/WebKit contract"
reject_guest_log 'dxg_ntshared_(runtime_resource|runtime_process|wsl_model)=.*alloc_owner:0x0/0' \
    "zero allocation-owner diagnostics in NT-share runtime/provenance lines"
reject_guest_log 'webkit-gpu:' \
    "legacy WebKit-specific label on generic GPU/DRM ioctl trace"
reject_guest_log 'panic|fatal page fault|coredump|(^|[^[:alpha:]])assert([^[:alpha:]]|$)|device removal|Removing Device' \
    "guest crash or D3D12 device-removal signature"

log_metadata passed
echo "hyperv-gpu-core-validate: passed validation_run_id=${VALIDATION_RUN_ID} (${LOG})" |
    tee -a "${LOG}"
