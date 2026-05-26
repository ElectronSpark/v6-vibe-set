#!/usr/bin/env bash
# Rebuild, deploy, and validate the Hyper-V DXG/D3DKMT graphics lane.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-/tmp/xv6-hyperv-build}
VM_NAME=${VM_NAME:-xv6-os-hyperv}
DEPLOY_VHDX=${DEPLOY_VHDX:-/mnt/c/Temp/xv6-hyperv.vhdx}
OUT_VHDX=${OUT_VHDX:-${BUILD_DIR}/xv6-hyperv-dxg-validate.vhdx}
LOG=${LOG:-${BUILD_DIR}/hyperv-dxg-validate.log}
VALIDATION_RUN_ID=${VALIDATION_RUN_ID:-dxg-$(date +%s)-$$}
SERIAL_SCRIPT=${SERIAL_SCRIPT:-C:\\Temp\\com-tcp-read.ps1}
READ_MS=${READ_MS:-120000}
BOOT_WAIT_SEC=${BOOT_WAIT_SEC:-20}
STOP_VM_TIMEOUT_SEC=${STOP_VM_TIMEOUT_SEC:-60}
GUEST_MARKER_RETRY_MS=${GUEST_MARKER_RETRY_MS:-5000}
GUEST_MARKER_RETRIES=${GUEST_MARKER_RETRIES:-3}
GUEST_CMD_MAX_CHARS=${GUEST_CMD_MAX_CHARS:-150}
DXG_VALIDATE_SEGMENT=${DXG_VALIDATE_SEGMENT:-full}
if [[ -z "${HYPERV_CMDLINE+x}" ]]; then
    case "${DXG_VALIDATE_SEGMENT}" in
        pure-c-lifetime)
            HYPERV_CMDLINE='BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=0 glsmoke_demo=0 wayland_dmabuf=1 wlcomp_gpu_compose=1 video=1024x640 acpi_cpus=6'
            ;;
        *)
            HYPERV_CMDLINE='BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=1 glsmoke_demo=1 glsmoke_frames=4 wayland_dmabuf=1 wlcomp_gpu_compose=1 video=1024x640 acpi_cpus=6'
            ;;
    esac
fi
if [[ -z "${REQUIRE_BOOT_GLSMOKE+x}" ]]; then
    case "${DXG_VALIDATE_SEGMENT}" in
        pure-c-lifetime) REQUIRE_BOOT_GLSMOKE=0 ;;
        *) REQUIRE_BOOT_GLSMOKE=1 ;;
    esac
fi
REQUIRE_WDDM_TRACE_COMPARE=${REQUIRE_WDDM_TRACE_COMPARE:-1}
WDDM_TRACE_CAPTURE_PLAN=${WDDM_TRACE_CAPTURE_PLAN:-0}
WSL_DXG_ADAPTER_NAME=${WSL_DXG_ADAPTER_NAME:-NVIDIA}
WSL_DXG_TRACE_DIR=${WSL_DXG_TRACE_DIR:-/tmp/xv6-wsl-probe}
WSL_DXG_TRACE_LIBRARY=${WSL_DXG_TRACE_LIBRARY:-${WSL_DXG_TRACE_DIR}/libwsl_dxg_ioctl_trace.so}
WSL_MESAGLFEATURE=${WSL_MESAGLFEATURE:-mesaglfeature}
WSL_DXG_TRACE=${WSL_DXG_TRACE:-${WSL_DXG_TRACE_DIR}/mesaglfeature-${WSL_DXG_ADAPTER_NAME,,}-live.trace}
HYPERV_D3D12_ADAPTER_NAME=${HYPERV_D3D12_ADAPTER_NAME:-${WSL_DXG_ADAPTER_NAME}}
MESA_DXCORE_DIAGNOSTIC_VERSION=${MESA_DXCORE_DIAGNOSTIC_VERSION:-20260519}

KERNEL_BIN=${KERNEL_BIN:-${BUILD_DIR}/kernel/build/kernel/xv6.bin}
ROOTFS_IMG=${ROOTFS_IMG:-${BUILD_DIR}/fs.img}
DXGPROBE_HOST=${DXGPROBE_HOST:-${BUILD_DIR}/sysroot/bin/dxgprobe}
GUEST_TX_SEQ=0

fail() {
    echo "hyperv-dxg-validate: $*" >&2
    exit 1
}

print_wddm_trace_capture_plan() {
    cat >&2 <<EOF
hyperv-dxg-validate: WDDM same-adapter trace compare is required.
hyperv-dxg-validate: expected WSL_DXG_TRACE=${WSL_DXG_TRACE}
hyperv-dxg-validate: expected WSL_DXG_ADAPTER_NAME=${WSL_DXG_ADAPTER_NAME}
hyperv-dxg-validate: expected LD_PRELOAD trace library=${WSL_DXG_TRACE_LIBRARY}
hyperv-dxg-validate: WSL capture command:
  mkdir -p '${WSL_DXG_TRACE_DIR}' &&
  env GALLIUM_DRIVER=d3d12 D3D12_DEBUG=verbose MESA_D3D12_DEFAULT_ADAPTER_NAME='${WSL_DXG_ADAPTER_NAME}' LD_PRELOAD='${WSL_DXG_TRACE_LIBRARY}' '${WSL_MESAGLFEATURE}' > '${WSL_DXG_TRACE}' 2>&1
hyperv-dxg-validate: paired xv6 guest capture command:
  mesad3d12probe --adapter ${HYPERV_D3D12_ADAPTER_NAME}; dxgprobe --wddm-payload-validate; cat /dev/dxg
hyperv-dxg-validate: adapter/LUID requirement:
  The WSL trace must contain dxgtrace: open_adapter_luid luid=HHHHHHHH:LLLLLLLL.
  The paired xv6 log must contain dxg_luid_equivalence and dxg_openadapterfromluid.
  Same-VM captures require exact UMD LUID equality; WSL/xv6 cross-VM captures require
  host-equivalent synthetic-LUID evidence plus matching physical adapter diagnostics.
  Cross-adapter traces are rejected.
hyperv-dxg-validate: compare command:
  DXGPROBE_HOST='${DXGPROBE_HOST}' WSL_DXG_TRACE='${WSL_DXG_TRACE}' '${DXGPROBE_HOST}' --wddm-trace-compare '${WSL_DXG_TRACE}' '${LOG}'
hyperv-dxg-validate: to print this no-build plan only:
  WDDM_TRACE_CAPTURE_PLAN=1 scripts/hyperv-dxg-validate.sh
EOF
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
    if [[ ! "${read_ms}" =~ ^[0-9]+$ ]]; then
        fail "invalid serial read timeout: ${read_ms}"
    fi

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File \
        "${SERIAL_SCRIPT}" -Cmd "${cmd}" -ReadMs "${read_ms}"
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
    if [[ -z "${state}" ]]; then
        fail "Hyper-V VM state query returned no state for ${VM_NAME}"
    fi
    printf '%s\n' "${state}"
}

stop_vm_for_deploy() {
    local state
    local rc

    state=$(query_vm_state | tail -n 1)
    echo "hyperv-dxg-validate: ${VM_NAME} pre-deploy state=${state}" |
        tee -a "${LOG}"
    if [[ "${state}" == "Off" ]]; then
        echo "hyperv-dxg-validate: ${VM_NAME} already Off; skipping Stop-VM" |
            tee -a "${LOG}"
        return
    fi

    echo "hyperv-dxg-validate: stopping ${VM_NAME} timeout=${STOP_VM_TIMEOUT_SEC}s" |
        tee -a "${LOG}"
    set +e
    timeout "${STOP_VM_TIMEOUT_SEC}s" \
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Stop-VM -Name '${VM_NAME}' -TurnOff -Force -ErrorAction Stop" \
        >>"${LOG}" 2>&1
    rc=$?
    set -e
    if [[ "${rc}" -ne 0 ]]; then
        echo "hyperv-dxg-validate: Stop-VM failed or timed out rc=${rc}; current VM state:" >&2
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
            "Get-VM -Name '${VM_NAME}' | Select Name,State,Uptime | Format-List" 2>&1 |
            tee -a "${LOG}" >&2 || true
        fail "failed to stop ${VM_NAME} before deploy"
    fi

    state=$(query_vm_state | tail -n 1)
    echo "hyperv-dxg-validate: ${VM_NAME} post-stop state=${state}" |
        tee -a "${LOG}"
    if [[ "${state}" != "Off" ]]; then
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
            "Get-VM -Name '${VM_NAME}' | Select Name,State,Uptime | Format-List" 2>&1 |
            tee -a "${LOG}" >&2 || true
        fail "Stop-VM completed but ${VM_NAME} state is ${state}, expected Off"
    fi
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

normalized_log_lines() {
    LC_ALL=C sed -E \
        -e $'s/\r//g' \
        -e $'s/\x1b\\[[0-9;?]*[ -/]*[@-~]//g' \
        -e 's/[[:space:]]*$//' \
        "${LOG}" |
        LC_ALL=C tr -d '\000-\010\013\014\016-\037\177'
}

require_log_line() {
    local literal=$1
    local description=$2

    if ! normalized_log_lines |
        awk -v literal="${literal}" '$0 == literal { found = 1 } END { exit found ? 0 : 1 }'; then
        echo "hyperv-dxg-validate: missing ${description}" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

require_no_guest_exec_failures() {
    if grep -Eq '(^|[[:space:]])exec [^[:space:]]+ failed' "${LOG}"; then
        echo "hyperv-dxg-validate: guest failed to exec a validator command" >&2
        grep -E '(^|[[:space:]])exec [^[:space:]]+ failed' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

require_no_fatal_dxg_log_signatures() {
    if grep -Eq 'wddm_payload_validate makeresident_mismatch' "${LOG}"; then
        fail "WDDM make-resident diagnostics reported a hard mismatch"
    fi
    if grep -Eiq 'D3D12: Removing Device|DXGI_ERROR_DEVICE_REMOVED|DEVICE_REMOVED|device removed' "${LOG}"; then
        fail "D3D12 device removal was reported"
    fi
    if grep -Eiq 'panic|fatal page fault|coredump|assert' "${LOG}"; then
        fail "guest log contains a crash signature"
    fi
}

require_split_transport_policy() {
    require_log 'dxg_transport_policy=normal-v40-ext/type31-pre-v40-v27-cache active:v40 host_v40:true pci_host:([4-9][0-9]|[1-9][0-9][0-9]+) pci_neg:40 ext:1 type31:pre-v40-v27-cache-first/host-v40-short-honest' \
        "DXG split v40/ext transport policy status"
    require_log 'dxg_negotiation=active:40 compat:[0-9]+ .*pci_host:([4-9][0-9]|[1-9][0-9][0-9]+) pci_neg:40 .*open:40/([4-9][0-9]|[1-9][0-9][0-9]+)/[0-9]+ ext:1' \
        "DXG normal transport negotiated active v40/ext"
    require_log 'dxg_adapter_hardware=vendor:0x[1-9a-fA-F][0-9a-fA-F]* device:0x[1-9a-fA-F][0-9a-fA-F]* .*source_name:(pre-v40-v27-type31-cache|cached-real)' \
        "DXG real physical adapter IDs served from cache under v40/ext"

    if grep -Eq 'dxg_transport_policy=initial-v27' "${LOG}"; then
        fail "normal DXG status still reports the old initial-v27 policy"
    fi
    if grep -Eq 'dxg_transport_policy=.*active:v27 .*host_v40:true' "${LOG}" ||
       grep -Eq 'dxg_negotiation=.*active:27 .*pci_host:([4-9][0-9]|[1-9][0-9][0-9]+)' "${LOG}"; then
        fail "normal DXG status fell back to active v27 despite host v40 support"
    fi
    if grep -Eq 'dxg_adapter_hardware=vendor:0x1414 device:0x0*8e .*source_name:(cached-real|pre-v40-v27-type31-cache|rejected-synthetic-pci)|dxg_adapter_hardware=.*source_name:rejected-synthetic-pci' "${LOG}"; then
        fail "DXG type31 physical adapter IDs used or exposed synthetic Microsoft PCI IDs"
    fi
}

require_no_malformed_validator_markers() {
    local bad

    bad=$(normalized_log_lines |
        grep -E '^[VD]:' |
        grep -Ev '^[VD]:(sl|sp|bl|eo|rp|pe|dx|lc|in|cp|oh|hl|sr|sf)$' || true)
    if [[ -n "${bad}" ]]; then
        echo "hyperv-dxg-validate: malformed or merged validator marker" >&2
        printf '%s\n' "${bad}" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

require_wddm_payload_residency() {
    if grep -Eq 'wddm_payload_validate ok .*make_flags=0x1 make_count=1 .*in=[1-9a-fA-F][0-9a-fA-F]*,0 wire=[1-9a-fA-F][0-9a-fA-F]*,0' "${LOG}"; then
        return
    fi
    if grep -Eq 'wddm_payload_validate ok .*make_flags=0x1 make_count=([2-9]|[1-9][0-9]+) .*sorted=1 .*in=[1-9a-fA-F][0-9a-fA-F]*,[1-9a-fA-F][0-9a-fA-F]* wire=[1-9a-fA-F][0-9a-fA-F]*,[1-9a-fA-F][0-9a-fA-F]*' "${LOG}"; then
        return
    fi
    if grep -Eq 'wddm_payload_validate predevice_pending .*make_ret=259 make_status=0x103 .*pending_ok=1 .*make_flags=0x1 make_count=1 .*in=[1-9a-fA-F][0-9a-fA-F]*,0 wire=[1-9a-fA-F][0-9a-fA-F]*,0 .*process_ret=' "${LOG}"; then
        return
    fi

    echo "hyperv-dxg-validate: missing WDDM make-resident diagnostics" >&2
    echo "hyperv-dxg-validate: expected single-allocation ok, WSL-order multi-allocation ok, or pre-device STATUS_PENDING count=1" >&2
    echo "hyperv-dxg-validate: log: ${LOG}" >&2
    exit 1
}

require_makeresident_count2_packet_shape() {
    require_log 'residency_batch_matrix .*requested_count=2 requested_flags=0x1 .*actual_count=2 actual_flags=0x1 .*rc=(0|259) .*status=PASS' \
        "MakeResident count=2 flags=0x1 residency-batch matrix PASS"
    require_log 'dxg_makeresident_shape=cmd:48 wsl_cmd:48 result:24 actual:24 owner_ok:2 tracked:2 order:1' \
        "MakeResident count=2 WSL-sized packet-shape diagnostics"
    require_log 'dxg_residency_last=.*make_(ret|user_ret):(0|259) .*make_status:0x(0|103) .*flags:0x1 count:2 sorted:0' \
        "MakeResident count=2 host return/order diagnostics"
    echo "hyperv-dxg-validate: MakeResident count=2 packet-shape proof ok" |
        tee -a "${LOG}"
}

require_wddm_layout_or_predevice_pending() {
    if grep -Eq 'wddm_layout_validate ok .*allocation_count=[1-9][0-9]* .*in_priv=[1-9][0-9]* out_priv=[1-9][0-9]* .*map_fence=[1-9][0-9]* .*fence_cpu=0x[1-9a-fA-F][0-9a-fA-F]* .*layout=1' "${LOG}"; then
        return
    fi
    if grep -Eq 'wddm_payload_validate predevice_pending .*make_ret=259 make_status=0x103 .*pending_ok=1 .*make_flags=0x1 make_count=1 .*in=[1-9a-fA-F][0-9a-fA-F]*,0 wire=[1-9a-fA-F][0-9a-fA-F]*,0 .*process_ret=' "${LOG}"; then
        return
    fi

    echo "hyperv-dxg-validate: missing WDDM return-layout diagnostics" >&2
    echo "hyperv-dxg-validate: expected layout ok or pending_ok single-allocation pre-device marker" >&2
    echo "hyperv-dxg-validate: log: ${LOG}" >&2
    exit 1
}

require_existing_sysmem_allocation_marker() {
    if grep -Eq 'existing_sysmem_allocation (unsupported|ok allocation=0x[1-9a-fA-F][0-9a-fA-F]*)' "${LOG}"; then
        return
    fi

    echo "hyperv-dxg-validate: missing DXG existing-sysmem allocation coverage marker" >&2
    echo "hyperv-dxg-validate: expected explicit unsupported marker or a nonzero allocation handle" >&2
    echo "hyperv-dxg-validate: log: ${LOG}" >&2
    exit 1
}

require_wave44_scanout_boundary_proof() {
    require_log 'scanout_d3d12_bridge_matrix .*status=PASS_FAIL_CLOSED' \
        "Wave44 scanout/D3D12 bridge matrix fail-closed boundary proof"
    require_log 'scanout_d3d12_bridge_matrix .*(pfnmap|pfn_map)[=:]1' \
        "Wave44 scanout bridge PFNMAP proof"
    require_log 'scanout_d3d12_bridge_matrix .*vram[=:]1' \
        "Wave44 scanout bridge VRAM proof"
    echo "hyperv-dxg-validate: Wave44 scanout/D3D12 boundary proof ok; native present remains gated until strict present evidence passes" |
        tee -a "${LOG}"
}

require_wave45_lifetime_rows_if_present() {
    if grep -Eq 'shared_resource_exporter_close_lifetime_matrix' "${LOG}"; then
        require_log 'shared_resource_exporter_close_lifetime_matrix .*status=PASS' \
            "Wave45 shared-resource exporter-close lifetime matrix PASS"
        require_log 'shared_resource_exporter_close_lifetime_matrix .*sealed_blob_valid_after_destroy[=:]1' \
            "Wave45 sealed shared-resource blob remains valid after exporter destroy"
        require_log 'shared_resource_exporter_close_lifetime_matrix .*global_handle_valid_after_destroy[=:]1' \
            "Wave45 global shared-resource handle remains valid after exporter destroy"
        echo "hyperv-dxg-validate: Wave45 shared-resource exporter-close lifetime proof ok; native present/FPS/WebKit remain gated until strict present evidence passes" |
            tee -a "${LOG}"
    fi

    if grep -Eq 'scanout_existing_sysmem_pin_lifetime_matrix' "${LOG}"; then
        require_log 'scanout_existing_sysmem_pin_lifetime_matrix .*status=PASS_FAIL_CLOSED' \
            "Wave45 scanout existing-sysmem pin lifetime fail-closed matrix"
        require_log 'scanout_existing_sysmem_pin_lifetime_matrix .*pfnmap_registered[=:]1' \
            "Wave45 scanout existing-sysmem PFNMAP registration proof"
        require_log 'scanout_existing_sysmem_pin_lifetime_matrix .*vram_registered[=:]1' \
            "Wave45 scanout existing-sysmem VRAM registration proof"
        require_log 'scanout_existing_sysmem_pin_lifetime_matrix .*destroy_coherent[=:]1' \
            "Wave45 scanout existing-sysmem destroy-coherence proof"
        echo "hyperv-dxg-validate: Wave45 scanout existing-sysmem pin lifetime boundary proof ok; native present/FPS/WebKit remain gated until strict present evidence passes" |
            tee -a "${LOG}"
    fi
}

require_shared_resource_seal_provenance_if_present() {
    local has_dxg_diag
    local has_row
    local row_pattern

    has_dxg_diag=0
    has_row=0
    row_pattern='shared_resource_(seal|provenance|seal_provenance).*matrix|shared_resource_seal_provenance'
    if grep -Eq "${row_pattern}" "${LOG}"; then
        require_log "(${row_pattern}).*status=PASS" \
            "shared-resource seal/provenance matrix PASS"
        if grep -Eq "(${row_pattern}).*status=FAIL" "${LOG}"; then
            fail "shared-resource seal/provenance matrix reported FAIL"
        fi
        has_row=1
    fi

    if grep -Eq 'dxg_sharedresource_seal_verify=tracked:1 .*resource:0x[1-9a-fA-F][0-9a-fA-F]*' "${LOG}" &&
       grep -Eq 'dxg_ntshared_runtime_resource=res:0x[1-9a-fA-F][0-9a-fA-F]* .*meta:1 .*seal:[01]->1 .*host_seal:[01]->1' "${LOG}"; then
        has_dxg_diag=1
    fi

    if [[ "${has_dxg_diag}" == "1" ]]; then
        require_log 'dxg_sharedresource_seal_verify=tracked:1 .*allocs:[1-9][0-9]* .*expected_priv:[1-9][0-9]* .*actual_priv:[1-9][0-9]* .*ret:0 .*missing:0x0 .*extra:0x0 .*resource:0x[1-9a-fA-F][0-9a-fA-F]*' \
            "DXG shared-resource seal/private-data verification diagnostics"
        require_log 'dxg_sharedresource_lifetime=.*seals:[1-9][0-9]* .*seal_allocs:[1-9][0-9]* .*seal_priv:[1-9][0-9]*' \
            "DXG shared-resource lifetime seal counters"
        require_log 'dxg_ntshared_runtime_resource=res:0x[1-9a-fA-F][0-9a-fA-F]* .*alloc:0x[1-9a-fA-F][0-9a-fA-F]* .*alloc_size:[1-9][0-9]* .*meta:1 .*seal:[01]->1 .*host_seal:[01]->1' \
            "DXG NT shared-resource runtime provenance seal diagnostics"
        require_log 'dxg_ntshared_pre_classes=res:type:[1-9][0-9]*/local:0x[1-9a-fA-F][0-9a-fA-F]*/host:0x[1-9a-fA-F][0-9a-fA-F]*.*alloc:type:[1-9][0-9]*/local:0x[1-9a-fA-F][0-9a-fA-F]*/host:0x[1-9a-fA-F][0-9a-fA-F]*.*shared_owner:found:1/.*sealed:[01]' \
            "DXG NT shared-resource pre-class provenance diagnostics"
        require_log 'dxg_sharedresource_ntdiag=res_host:0x[1-9a-fA-F][0-9a-fA-F]* alloc_host:0x[1-9a-fA-F][0-9a-fA-F]* .*alloc_hash:[0-9a-fA-F]{8}/[0-9a-fA-F]{8} .*alloc_size:[1-9][0-9]* .*meta:1->1 .*seal:[01]->1 .*host_seal:[01]->1' \
            "DXG shared-resource NT diagnostic seal/provenance state"
        if grep -Eq 'dxg_sharedresource_seal_verify=.*ret:-|dxg_sharedresource_seal_verify=.*missing:0x[1-9a-fA-F][0-9a-fA-F]*|dxg_sharedresource_seal_verify=.*extra:0x[1-9a-fA-F][0-9a-fA-F]*' "${LOG}"; then
            fail "DXG shared-resource seal/provenance diagnostics reported missing or extra private-data state"
        fi
    fi

    if [[ "${has_row}" == "1" || "${has_dxg_diag}" == "1" ]]; then
        echo "hyperv-dxg-validate: shared-resource seal/provenance proof consumed; this closes sealing/provenance only, not native present/FPS/WebKit/OpenGL-submit" |
            tee -a "${LOG}"
    fi
}

require_existing_sysmem_ntbridge_evidence_if_present() {
    if ! grep -Eq 'dxg_existing_sysmem_ntbridge|existing_sysmem_ntbridge' "${LOG}"; then
        return
    fi
    require_log '(dxg_existing_sysmem_ntbridge|existing_sysmem_ntbridge).*shareable[=:][01]' \
        "DXG existing-sysmem NT bridge shareable decision"
    require_log '(dxg_existing_sysmem_ntbridge|existing_sysmem_ntbridge).*reason[=:][A-Za-z0-9_.:-]+' \
        "DXG existing-sysmem NT bridge reason"
}

require_import_negative_matrix() {
    require_log 'resource_import_negative_matrix .*status=PASS' \
        "D3D12 resource import-negative matrix PASS"
    require_log 'resource_import_negative_matrix .*resource_returned_fd_valid=1' \
        "D3D12 resource import-negative returned fd validity proof"
    require_log 'resource_import_negative_matrix .*resource_fd_open_before_close=1 .*resource_fd_open_after_close=0' \
        "D3D12 resource import-negative fd close lifecycle proof"
    require_log 'resource_import_negative_matrix .*stale_resource_fd_valid=1' \
        "D3D12 resource import-negative stale fd setup proof"
    require_log 'resource_import_negative_matrix .*missing_query_rc=-1 .*missing_open_rc=-1' \
        "D3D12 resource import-negative missing fd rejection"
    require_log 'resource_import_negative_matrix .*wrong_fd_kind=sync_fd .*wrong_kind_query_rc=-1 .*wrong_kind_open_rc=-1' \
        "D3D12 resource import-negative wrong fd kind rejection"
    require_log 'resource_import_negative_matrix .*stale_query_rc=-1 .*stale_open_rc=-1' \
        "D3D12 resource import-negative stale fd rejection"
    require_log 'resource_import_negative_matrix .*present_attempted=0 .*native_present_claim=0' \
        "D3D12 resource import-negative produced no present credit"

    require_log 'sync_import_negative_matrix .*status=PASS' \
        "D3D12 sync import-negative matrix PASS"
    require_log 'sync_import_negative_matrix .*sync_returned_fd_valid=1' \
        "D3D12 sync import-negative returned fd validity proof"
    require_log 'sync_import_negative_matrix .*sync_fd_open_before_close=1 .*sync_fd_open_after_close=0' \
        "D3D12 sync import-negative fd close lifecycle proof"
    require_log 'sync_import_negative_matrix .*stale_sync_fd_valid=1' \
        "D3D12 sync import-negative stale fd setup proof"
    require_log 'sync_import_negative_matrix .*missing_open_rc=-1' \
        "D3D12 sync import-negative missing fd rejection"
    require_log 'sync_import_negative_matrix .*wrong_fd_kind=resource_fd .*wrong_kind_open_rc=-1' \
        "D3D12 sync import-negative wrong fd kind rejection"
    require_log 'sync_import_negative_matrix .*zero_device_open_rc=-1 .*stale_open_rc=-1' \
        "D3D12 sync import-negative zero-device/stale fd rejection"
    require_log 'sync_import_negative_matrix .*child_parent_device_control=expected_reject .*child_parent_device_rc=-1 .*child_status=0' \
        "D3D12 sync import-negative inherited parent-device rejection proof"
    require_log 'sync_import_negative_matrix .*present_attempted=0 .*native_present_claim=0' \
        "D3D12 sync import-negative produced no present credit"
    require_log 'ntshare_copyout_cleanup_matrix kind=resource .*status=PASS' \
        "D3D12 resource NT share copyout-failure cleanup PASS"
    require_log 'ntshare_copyout_cleanup_matrix kind=resource .*fault_share_rc=-1 .*reclaimed=1 .*refs_after=0 .*valid_share_after_fault=1 .*returned_fd_valid=1' \
        "D3D12 resource NT share copyout failure reclaimed fd/ref and recovered"
    require_log 'ntshare_copyout_cleanup_matrix kind=sync .*status=PASS' \
        "D3D12 sync NT share copyout-failure cleanup PASS"
    require_log 'ntshare_copyout_cleanup_matrix kind=sync .*fault_share_rc=-1 .*reclaimed=1 .*refs_after=0 .*valid_share_after_fault=1 .*returned_fd_valid=1' \
        "D3D12 sync NT share copyout failure reclaimed fd/ref and recovered"
    require_log 'createallocation_unwind_matrix .*create_rc=-[0-9]+ .*copyout_fault=1 .*destroy_target_match=1 .*destroy_ctx=5 .*destroy_count=0 .*destroy_ret=0 .*same_process_cleanup=1 .*pin_balanced=1 .*no_local_leak=1 .*status=PASS' \
        "D3D12 CREATEALLOCATION copyout-failure resource cleanup PASS"
    require_log 'openresource_unwind_matrix .*open_rc=-[0-9]+ .*copyout_fault=1 .*destroy_ctx=1 .*destroy_ret=0 .*same_process_cleanup=1 .*pin_balanced=1 .*no_local_leak=1 .*parent_same=1 .*parent_refs_balanced=1 .*parent_child_unlinked=1 .*sealed_generation_coherent=1 .*status=PASS' \
        "D3D12 OPENRESOURCE copyout-failure cleanup and parent rollback PASS"
    require_log 'dxg_tgid_pre_dispatch_matrix .*status=PASS' \
        "DXG inherited fd pre-dispatch TGID gate matrix PASS"
    require_log 'dxg_tgid_pre_dispatch_matrix .*inherited_enum_rc=-1 .*inherited_openadapter_rc=-1 .*inherited_query_rc=-1 .*inherited_create_rc=-1 .*inherited_opensync_rc=-1' \
        "DXG inherited fd rejected discovery, query, create, and opensync before dispatch"
    require_log 'dxg_tgid_pre_dispatch_matrix .*kernel_namespace_diag_present=1' \
        "DXG inherited fd pre-dispatch TGID kernel diagnostic"

    if grep -Eq '(resource|sync)_import_negative_matrix .*status=FAIL' "${LOG}"; then
        fail "D3D12 import-negative matrix reported FAIL; failing negative-import rows cannot close provenance or native-present checklist items"
    fi
    if grep -Eq 'dxg_tgid_pre_dispatch_matrix .*status=FAIL' "${LOG}"; then
        fail "DXG pre-dispatch TGID matrix reported FAIL"
    fi
    if grep -Eq 'ntshare_copyout_cleanup_matrix .*status=FAIL' "${LOG}"; then
        fail "NT share copyout cleanup matrix reported FAIL"
    fi
    if grep -Eq '(createallocation|openresource)_unwind_matrix .*status=FAIL' "${LOG}"; then
        fail "DXG allocation/openresource unwind matrix reported FAIL"
    fi
    require_opensync_child_split_matrices
    echo "hyperv-dxg-validate: D3D12 import-negative proof ok; this closes rejection/provenance only, not native present/FPS/WebKit" |
        tee -a "${LOG}"
}

require_opensync_child_split_matrices() {
    if grep -Eq 'opensync_child_isolation_matrix' "${LOG}" &&
       ! grep -Eq 'opensync_child_(own_dxg|inherited_parent_dxg)_matrix' "${LOG}"; then
        fail "old ambiguous opensync_child_isolation_matrix is not sufficient; require split own-dxg and inherited-parent-dxg OpenSync child rows"
    fi
    if grep -Eq 'opensync_child_(own_dxg|inherited_parent_dxg)_matrix .*status=FAIL' "${LOG}"; then
        fail "OpenSync split child namespace matrix reported FAIL"
    fi

    require_log 'opensync_child_own_dxg_matrix .*status=PASS' \
        "OpenSync child own-dxg matrix PASS"
    require_log 'opensync_child_own_dxg_matrix .*child_open_rc=0' \
        "OpenSync child own-dxg same-numeric child device allowed"
    require_log 'opensync_child_own_dxg_matrix .*expected=allow_same_numeric_child_device' \
        "OpenSync child own-dxg WSL-consistent expected allowance"
    require_log 'opensync_child_own_dxg_matrix .*reason=wsl_consistent_same_numeric_child_device' \
        "OpenSync child own-dxg WSL-consistent reason"
    require_log 'opensync_child_own_dxg_matrix .*used_parent_device=0' \
        "OpenSync child own-dxg did not use inherited parent device"
    require_log 'opensync_child_own_dxg_matrix .*used_child_device=1' \
        "OpenSync child own-dxg used child-opened device"

    require_log 'opensync_child_inherited_parent_dxg_matrix .*status=PASS' \
        "OpenSync child inherited-parent-dxg matrix PASS"
    require_log 'opensync_child_inherited_parent_dxg_matrix .*child_open_rc=-1' \
        "OpenSync child inherited parent device rejected"
    require_log 'opensync_child_inherited_parent_dxg_matrix .*kernel_namespace_diag_present=1' \
        "OpenSync child inherited parent device kernel namespace diagnostic"
    require_log 'opensync_child_inherited_parent_dxg_matrix .*expected=reject_inherited_parent_dxg_fd_tgid_guard' \
        "OpenSync child inherited parent device expected TGID guard"
    require_log 'opensync_child_inherited_parent_dxg_matrix .*used_parent_device=1' \
        "OpenSync child inherited-parent test used parent device"
    require_log 'opensync_child_inherited_parent_dxg_matrix .*used_child_device=0' \
        "OpenSync child inherited-parent test did not use child-opened device"
}

require_opensyncobject_source_matrix_if_present() {
    if ! grep -Eq 'opensyncobject_source_matrix' "${LOG}"; then
        return
    fi

    require_log 'opensyncobject_source_matrix .*status=PASS' \
        "OpenSyncObject source matrix PASS"
    require_log 'opensyncobject_source_matrix .*sync_returned_fd_valid=1 .*sync_fd_open_after_close=0' \
        "OpenSyncObject source sync-fd close lifecycle proof"
    require_log 'opensyncobject_source_matrix .*resource_returned_fd_valid=1' \
        "OpenSyncObject source resource-fd provenance proof"
    require_log 'opensyncobject_source_matrix .*stale_sync_fd_valid=1' \
        "OpenSyncObject source stale sync-fd setup proof"
    require_log 'opensyncobject_source_matrix .*missing_open_rc=-1 .*wrong_kind_resource_fd_rc=-1 .*zero_device_open_rc=-1 .*stale_open_rc=-1' \
        "OpenSyncObject source negative open rejection proof"
    require_log 'opensyncobject_source_matrix .*child_parent_device_control=expected_reject .*child_parent_device_rc=-1 .*child_status=0' \
        "OpenSyncObject source inherited parent-device rejection proof"
    if grep -Eq 'opensyncobject_source_matrix .*status=FAIL' "${LOG}"; then
        fail "OpenSyncObject source matrix reported FAIL"
    fi
    require_opensync_child_split_matrices
    echo "hyperv-dxg-validate: OpenSyncObject source matrix proof ok; this closes provenance only, not native present/FPS/WebKit" |
        tee -a "${LOG}"
}

require_opensync_handle_source_matrix_if_present() {
    if ! grep -Eq 'opensync_handle_source_matrix' "${LOG}"; then
        return
    fi

    require_log 'opensync_handle_source_matrix .*status=PASS' \
        "OpenSync handle-source matrix PASS"
    require_log 'opensync_handle_source_matrix .*host_shared_rc=0 .*host_shared_sync=0x[1-9a-f][0-9a-f]*' \
        "OpenSync handle-source host-shared success"
    require_log 'opensync_handle_source_matrix .*host_nt_attempted=1 .*host_nt_rc=-?[0-9]+' \
        "OpenSync handle-source host-NT variant attempted"
    require_log 'opensync_handle_source_matrix .*source_varied=1 .*present_attempted=0 .*native_present_claim=0' \
        "OpenSync handle-source matrix produced no present credit"
    if grep -Eq 'opensync_handle_source_matrix .*status=FAIL' "${LOG}"; then
        fail "OpenSync handle-source matrix reported FAIL"
    fi
    echo "hyperv-dxg-validate: OpenSync handle-source matrix proof ok; this closes only the handle-source variant, not D3D12 fence/native-present/FPS/WebKit" |
        tee -a "${LOG}"
}

require_ntshared_close_behavior_matrix_if_present() {
    if ! grep -Eq 'ntshared_close_behavior_matrix' "${LOG}"; then
        return
    fi

    require_log 'ntshared_close_behavior_matrix .*status=(PASS|SKIP_MISSING_CONTRACT)' \
        "NT shared close behavior matrix status"
    require_log 'ntshared_close_behavior_matrix .*resource_returned_fd_valid=1 .*resource_fd_open_after_close=0 .*resource_close_attempted=1' \
        "NT shared close behavior resource-fd close lifecycle proof"
    require_log 'ntshared_close_behavior_matrix .*sync_returned_fd_valid=1 .*sync_fd_open_after_close=0 .*sync_close_attempted=1' \
        "NT shared close behavior sync-fd close lifecycle proof"
    if grep -Eq 'ntshared_close_behavior_matrix .*status=SKIP_MISSING_CONTRACT' "${LOG}"; then
        require_log 'ntshared_close_behavior_matrix .*missing_contract=LX_DXDESTROYNTSHAREDOBJECT .*close_is_user_trigger=1' \
            "NT shared close behavior missing-contract diagnostic"
    fi
    if grep -Eq 'ntshared_close_behavior_matrix .*status=FAIL' "${LOG}"; then
        fail "NT shared close behavior matrix reported FAIL"
    fi
    echo "hyperv-dxg-validate: NT shared close behavior proof consumed; this closes close-lifecycle diagnostics only, not native present/FPS/WebKit" |
        tee -a "${LOG}"
}

fail_known_d3d12_shared_blocker() {
    local pattern='d3d12sharedsmoke: (shareobjects failed|CreateSharedHandle\(resource\) failed|query shared resource failed|direct open shared resource (failed|invalid query|alloc failed|allocation metadata invalid|private blob out of range|private blob empty|destroy failed)|present validation failed|GPU present evidence (file missing or unreadable|missing/unchanged)|could not remove stale present evidence|compositor reported rejected D3D12 present path before commit)|shared_(lifetime|private) (create_failed|share_failed)|dxg_sharedhandle_last=.*ret:-75|dxg_ntshared_(last|create_attempts)=.*(len:0|create_len:0).*ret:-75|dxg_ntshared_last=.*create_nt_zero_len:1 .*hard_fail:1|cat: cannot open /tmp/wlcomp-d3d12-present'

    fail_dxg_present_accounting_state
    fail_protocol_only_strict_present_artifact
    fail_wave41_fail_closed_present
    if grep -Eq 'd3d12sharedsmoke: runtime CreateSharedHandle\(resource\) ok' "${LOG}" &&
       grep -Eq 'd3d12sharedsmoke: runtime CreateSharedHandle\(fence\) ok' "${LOG}" &&
       ! grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${LOG}" &&
       grep -Eq 'GPU present evidence (file missing or unreadable|missing/unchanged)|cat: cannot open /tmp/wlcomp-d3d12-present|Open.*[Ff]ence.*(failed|0x80070057)|0x80070057' "${LOG}"; then
        echo "hyperv-dxg-validate: Wave38-shaped fail-closed state: resource/fence export succeeded, but native-present evidence is missing or incomplete" >&2
        grep -E 'd3d12sharedsmoke: runtime CreateSharedHandle\((resource|fence)\) ok|GPU present evidence (file missing or unreadable|missing/unchanged)|cat: cannot open /tmp/wlcomp-d3d12-present|Open.*[Ff]ence.*(failed|0x80070057)|0x80070057|backend_opengl_submit 0' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: no native-present/FPS/WebKit credit without /tmp/wlcomp-d3d12-present run-id, content CRC/frame, completion, display-correlation, and backend_opengl_submit=1 evidence" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi

    if grep -Eq "${pattern}" "${LOG}"; then
        echo "hyperv-dxg-validate: D3D12 shared-resource export/open/present blocker is still active" >&2
        grep -E "${pattern}" "${LOG}" >&2 || true
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

require_fail_closed_gpup_dda_diagnostic() {
    if grep -Eq 'host-display-helper/resource-scanout-bind|runtime-created-d3d12-resource-to-host-display-helper|custom_host_tool:[1-9][0-9]*|custom_host_tool=1' "${LOG}"; then
        fail "D3D12 present evidence used a custom host display helper; only represented GPU-P/DXG or DDA/Nouveau handoff is accepted"
    fi
    if ! grep -Eq 'missing host ABI=(dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)|ABI=(dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)|gpu_p_or_dda_bind=gpu-p-dxg-resource-scanout-bind|d3d12_present_source_no_gpu_p_or_dda_display_bind=1|d3d12_display_handoff_requires_kernel_host_protocol=1' "${LOG}"; then
        fail "D3D12 present fail-closed without named missing GPU-P/DDA display-bind diagnostic"
    fi
    if grep -Eq 'gpu_p_or_dda_bind=gpu-p-dxg-resource-scanout-bind|missing host ABI=gpu-p-dxg-resource-scanout-bind|ABI=gpu-p-dxg-resource-scanout-bind' "${LOG}"; then
        require_log 'candidate_cmds[:=]presenthistory=34,redirected_flip_fence=35,blt=38,propagate_presenthistory=1' \
            "Wave49 GPU-P/DXG candidate command diagnostics"
    fi
}

require_d3d12_current_run_display_provenance_if_present() {
    if ! grep -Eq 'd3d12_(final_handoff_|display_completion_required)' "${LOG}"; then
        return
    fi

    require_log 'd3d12_run_id[ =][A-Za-z0-9_.:-]+' \
        "D3D12 current-run compositor evidence run id"
    require_log 'd3d12_client_pid[ =][1-9][0-9]*' \
        "D3D12 current-run client process identity"
    require_log 'd3d12_client_buffer_id[ =][1-9][0-9]*' \
        "D3D12 current-run client buffer identity"
    require_log 'd3d12_manager_resource_id[ =][1-9][0-9]*' \
        "D3D12 current-run manager resource identity"
    require_log 'd3d12_present_resource[ =]0x[1-9a-fA-F][0-9a-fA-F]*' \
        "D3D12 current-run present resource identity"
    require_log 'd3d12_buffer_generation[ =][1-9][0-9]*' \
        "D3D12 current-run buffer generation identity"

    require_log 'd3d12_display_completion_required[ =]1' \
        "D3D12 WSLg-display/native display-completion requirement"
    require_log 'd3d12_final_handoff_lane[ =]runtime-created-d3d12-resource-through-gpu-p-or-dda' \
        "D3D12 final handoff GPU-P/DDA lane"
    require_log 'd3d12_final_handoff_selected[ =](dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)' \
        "D3D12 final handoff selected GPU-P/DDA contract"
    require_log 'd3d12_final_handoff_source[ =]runtime-created-d3d12-resource' \
        "D3D12 final handoff runtime-created resource source"
    require_log 'd3d12_final_handoff_destination[ =]host-display-channel' \
        "D3D12 final handoff host-display destination"
    require_log 'd3d12_final_handoff_runtime_resource_required[ =]1' \
        "D3D12 final handoff runtime-resource requirement"
    require_log 'd3d12_final_handoff_kernel_abi[ =]FB_GPU_DXG_PRESENT_SOURCE_COMMIT/runtime-resource-scanout' \
        "D3D12 final handoff kernel ABI name"
    require_log 'd3d12_final_handoff_callbacks_release_gate[ =]native-display-completion' \
        "D3D12 final handoff callback/release display-completion gate"
    require_log 'd3d12_final_handoff_user_display_channel_selected[ =]0' \
        "D3D12 final handoff rejects user-display-channel shortcut"

    if grep -Eq 'd3d12_final_handoff_host_display_commit_success[ =]1' "${LOG}"; then
        require_log 'd3d12_final_handoff_present_id[ =][1-9][0-9]*' \
            "D3D12 final handoff nonzero present id"
        require_log 'd3d12_final_handoff_completed[ =][1-9][0-9]*' \
            "D3D12 final handoff nonzero completed counter"
    else
        echo "hyperv-dxg-validate: D3D12 final handoff GPU-P/DDA contract evidence consumed; this closes current-run provenance only, not native present/FPS/WebKit" |
            tee -a "${LOG}"
    fi
}

fail_wave41_fail_closed_present() {
    require_backend_opengl_submit_gated_for_failed_present
    if grep -Eq 'd3d12_present_errno=95|present_errno=95|(^|[[:space:]])(commit_errno|commit_status|d3d12_.*commit_(errno|status))[ =]95($|[[:space:]])|callbacks_blocked=1|releases_blocked=1|present_id=0|completed=0|d3d12_present_source_(commit_rejected_eopnotsupp|no_present_id_completed|gpu_p_or_dda_transport_absent|no_gpu_p_or_dda_display_bind|same_frame_callbacks_blocked|same_frame_releases_blocked)[ =]1' "${LOG}" &&
       ! grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${LOG}"; then
        require_fail_closed_gpup_dda_diagnostic
        echo "hyperv-dxg-validate: fail-closed present: missing GPU-P/DDA resource-scanout-bind dependency; native-present/FPS/WebKit remain gated" >&2
        grep -E 'missing host ABI=(dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)|ABI=(dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)|gpu_p_or_dda_bind=gpu-p-dxg-resource-scanout-bind|candidate_cmds[:=]presenthistory=34,redirected_flip_fence=35,blt=38,propagate_presenthistory=1|d3d12_display_handoff_requires_kernel_host_protocol=1|d3d12_present_errno=95|present_errno=95|present_id=0|completed=0|callbacks_blocked=1|releases_blocked=1|backend_opengl_submit 0' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

require_backend_opengl_submit_gated_for_failed_present() {
    if grep -Eq '(^|[[:space:]])(commit_errno|commit_status|d3d12_.*commit_(errno|status))[ =]95($|[[:space:]])|(^|[[:space:]])(present_id|completed|d3d12_dxg_present_id|d3d12_dxg_present_completed|d3d12_present_source_buffer_present_id|d3d12_present_source_buffer_completed|d3d12_final_handoff_present_id|d3d12_final_handoff_completed)[ =]0($|[[:space:]])' "${LOG}" &&
       grep -Eq 'backend_opengl_submit 1' "${LOG}"; then
        fail "Hyper-V OpenGL-submit was advertised while D3D12 present-source commit failed closed or present_id/completed stayed zero"
    fi
}

reject_wave60_registration_only_present() {
    if grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${LOG}"; then
        return
    fi

    require_backend_opengl_submit_gated_for_failed_present
    if grep -Eq 'd3d12_(dxg_present_source|present_source_buffer)_register_successes[ =][1-9][0-9]*' "${LOG}" &&
       grep -Eq 'd3d12_(dxg_present_source|present_source_buffer)_commit_successes[ =]0' "${LOG}"; then
        echo "hyperv-dxg-validate: D3D12 present-source registration succeeded but commit did not; registration-only proof is not native present" >&2
        grep -E 'd3d12_(native_present_requirements_satisfied|dxg_present_source_(register|commit|present_id|completed)|present_source_(buffer_|)(registered|query|commit|no_present_id|gpu_p_or_dda_transport|same_frame|luid)|present_errno|present_missing)|backend_opengl_submit [01]' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

fail_dxg_present_accounting_state() {
    reject_wave60_registration_only_present
    if grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${LOG}" ||
       ! grep -Eq 'd3d12_evidence_stage=|stage=present-failed-before-success|d3d12_present_path=fail-closed|d3d12sharedsmoke: runtime-present-control-flow|d3d12_dxg_present_source_commit_attempts' "${LOG}"; then
        return
    fi

    if grep -Eq 'dxg_present_register_copyin_failures [1-9][0-9]*|dxg_present_commit_copyin_failures [1-9][0-9]*' "${LOG}"; then
        echo "hyperv-dxg-validate: D3D12 present fail-closed classification=copyin-failed" >&2
        grep -E 'dxg_present_(register|commit)_(ioctl_entries|copyin_failures|attempts|rejects)|d3d12_(evidence_stage|present_path|present_missing|present_errno)|backend_opengl_submit 0' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: kernel present ioctl was entered but copyin failed; this is fail-closed accounting only, not native completion" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi

    if grep -Eq 'dxg_present_register_ioctl_entries 0' "${LOG}" &&
       grep -Eq 'dxg_present_commit_ioctl_entries 0' "${LOG}"; then
        echo "hyperv-dxg-validate: D3D12 present fail-closed classification=kernel-not-entered" >&2
        grep -E 'dxg_present_(register|commit)_(ioctl_entries|copyin_failures|attempts|rejects)|d3d12_(evidence_stage|present_path|present_missing|present_errno)|backend_opengl_submit 0' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: no kernel present ioctl entry was observed; this cannot satisfy native present/FPS/WebKit closure" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi

    if grep -Eq 'dxg_present_register_ioctl_entries [1-9][0-9]*' "${LOG}" &&
       grep -Eq 'dxg_present_commit_ioctl_entries [1-9][0-9]*' "${LOG}" &&
       grep -Eq 'dxg_present_register_copyin_failures 0' "${LOG}" &&
       grep -Eq 'dxg_present_commit_copyin_failures 0' "${LOG}" &&
       grep -Eq 'dxg_present_commit_attempts [1-9][0-9]*' "${LOG}" &&
       grep -Eq 'dxg_present_host_handoff_missing [1-9][0-9]*|dxg_present_missing_host_abi [1-9][0-9]*|dxg_present_last_ret 95' "${LOG}"; then
        echo "hyperv-dxg-validate: D3D12 present fail-closed classification=validated-missing-gpup-or-dda-display-bind" >&2
        grep -E 'dxg_present_(register|commit)_(ioctl_entries|copyin_failures|attempts|successes|rejects)|dxg_present_(host_handoff_missing|missing_host_abi|last_ret|requires_host_protocol|gpu_p_or_dda_contract_version|gpu_p_or_dda_required_metadata|gpu_p_or_dda_transport_present|gpu_p_or_dda_source_live|gpu_p_or_dda_requires_completion)|d3d12_(evidence_stage|present_path|present_missing|present_errno|dxg_present_source_commit_attempts|dxg_present_source_commit_successes|dxg_present_id|dxg_present_completed)|callbacks_blocked=1|releases_blocked=1|backend_opengl_submit 0' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: present ioctl/copyin reached validated kernel handling, but the required GPU-P/DDA display bind is missing; fail-closed accounting is not native completion" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

fail_protocol_only_strict_present_artifact() {
    if ! grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${LOG}" &&
       grep -Eq 'd3d12_wayland_(resource_buffer_admission|present_failclosed_identity)_matrix|d3d12_evidence_stage=protocol_accepted|stage=present-failed-before-success|d3d12_protocol_accepts[ =][1-9][0-9]*|d3d12_present_identity_current_run_valid[ =]0|d3d12_gpu_copy_composite_starts[ =]0|d3d12_gpu_copy_composite_completes[ =]0|d3d12_gpu_copy_completes[ =]0|d3d12_dxg_present_source_commit_attempts[ =]0|dxg_present_commit_attempts 0|d3d12_frame_callback_observed[ =]0|d3d12_buffer_release_observed[ =]0' "${LOG}"; then
        echo "hyperv-dxg-validate: intermediate strict-present artifact rejected" >&2
        grep -E 'd3d12_wayland_(resource_buffer_admission|present_failclosed_identity)_matrix|d3d12_evidence_stage=protocol_accepted|stage=present-failed-before-success|d3d12_protocol_accepts[ =][1-9][0-9]*|d3d12_(run_id|client_pid|client_buffer_id|manager_resource_id|buffer_generation|present_identity_(compositor_run_id|client_pid|client_buffer_id|manager_resource_id|buffer_generation|current_run_valid))|d3d12_gpu_present_complete[s]?[ =]0|d3d12_(gpu_copy_composite_starts|gpu_copy_composite_completes|gpu_copy_completes|compositor_gpu_copy_submits|compositor_gpu_copy_waits)[ =]0|d3d12_dxg_present_source_commit_attempts[ =]0|dxg_present_commit_attempts 0|d3d12_(frame_callback_observed|buffer_release_observed)[ =]0|backend_opengl_submit 0' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: strict-present evidence is incomplete: protocol/failure accounting may exist, but native credit requires a fresh current-run identity, compositor GPU copy start+completion, present-source commit attempt, native present completion, and frame callback/release; fail-closed accounting is not native completion" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

fail_import_only_d3d12_evidence() {
    local pattern

    if grep -Eq 'd3d12sharedsmoke: present validation ok frame=1 release=1 gpu_present=[0-9]+->[1-9][0-9]*' "${LOG}" &&
       grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${LOG}"; then
        return
    fi

    fail_dxg_present_accounting_state
    fail_protocol_only_strict_present_artifact

    pattern='native_present_claim=0|present_claim=requires-compositor-completion|d3d12sharedsmoke: committed D3D12 shared resource buffer release=0|d3d12sharedsmoke: wayland D3D12 submit plan .*require_present=0'
    if grep -Eq "${pattern}" "${LOG}"; then
        echo "hyperv-dxg-validate: D3D12 resource/fence import did not reach native present completion" >&2
        grep -E "${pattern}|d3d12_protocol_accepts|d3d12_native_import_completes|d3d12_resource_import_successes|d3d12_fence_import_successes" "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: missing required Phase 3 native-present evidence: release=1, frame=1, gpu_present delta, /tmp/wlcomp-d3d12-present completion counters, run id, and content hash/frame counters" >&2
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi
}

first_log_line_number() {
    local pattern=$1

    grep -En "${pattern}" "${LOG}" | sed -n '1s/:.*//p'
}

require_no_premature_opengl_submit_claim() {
    local first_submit
    local first_contract

    first_submit=$(first_log_line_number 'backend_opengl_submit 1')
    if [[ -z "${first_submit}" ]]; then
        return
    fi

    first_contract=$(first_log_line_number 'd3d12sharedsmoke: shared-surface OpenGL-submit contract ok')
    if [[ -z "${first_contract}" || "${first_submit}" -lt "${first_contract}" ]]; then
        fail "Hyper-V claimed OpenGL-submit before the D3D12 shared-surface contract passed"
    fi
    fail "Hyper-V advertised OpenGL submit before the full Hyper-V OpenGL lane is validated"
}

require_d3d12_ntshare_diagnostics() {
    local create_success_pattern
    local create_zero_len_pattern

    create_success_pattern='dxg_ntshared_last=.*result_len:[1-9][0-9]* .*create_ret:0 .*handle:0x[1-9a-fA-F][0-9a-fA-F]*'
    create_zero_len_pattern='dxg_ntshared_last=.*create_len:0 .*create_ret:-75 .*zero_len:1 .*create_nt_zero_len:1 .*hard_fail:1 .*side_effect:0 .*share_fallback:0 .*share_valid:0'

    require_log 'dxg_ntshared_create_attempts=count:[1-9][0-9]* .*a0:cmd:32/off:24/wire:48/ext:1/eoff:16' \
        "WSL-natural v40/ext CreateNtSharedObject attempt diagnostics"
    require_log 'dxg_ntshared_attempt_meta=.*label:12.*wsl_ext32_zero_luid_natural' \
        "CreateNtSharedObject v40/ext attempt-label diagnostics"
    if grep -Eq 'dxg_ntshared_last=.*create_nt_zero_len:1' "${LOG}"; then
        require_log 'dxg_ntshared_wsl_exact=.*ext32_zero_luid_natural:1/a0' \
            "CreateNtSharedObject WSL-exact ext32 natural packet diagnostics"
    fi
    require_log 'dxg_sharedresource_metadata=.*track_host:0 .*match_in:1 .*match_out:0 .*logical_flags:0x47 .*host_result_flags:0x4047' \
        "D3D12 shared-resource UMD/input metadata diagnostics"
    require_log 'dxg_sharedresource_preseal=mode:deferred applied:0 before:0 after:0' \
        "D3D12 shared-resource deferred preseal diagnostics"

    if grep -Eq 'dxg_ntshared_last=.*share_fallback:1' "${LOG}"; then
        echo "hyperv-dxg-validate: unexpected ShareObjectWithHost fallback in LX_DXSHAREOBJECTS resource path" >&2
        grep -E 'dxg_ntshared_(create_attempts|last|attempt_meta|wsl_exact)|dxg_shareobject_last|dxg_global_send_sharewire|dxg_sharedresource_(metadata|preseal)|dxg_sharedhandle_last' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi

    if grep -Eq "${create_zero_len_pattern}" "${LOG}"; then
        echo "hyperv-dxg-validate: create_nt_zero_len hard failure: WSL-natural CreateNtSharedObject completed with zero bytes and no NT handle" >&2
        grep -E 'dxg_ntshared_(create_attempts|last|attempt_meta|wsl_exact)|dxg_sharedresource_(metadata|preseal)|dxg_sharedhandle_last' "${LOG}" >&2 ||
            true
        echo "hyperv-dxg-validate: log: ${LOG}" >&2
        exit 1
    fi

    if grep -Eq "${create_success_pattern}" "${LOG}"; then
        require_log 'dxg_ntshared_cache=.*inserts:[1-9][0-9]* .*last:kind:2/.*nt:0x[1-9a-fA-F][0-9a-fA-F]*' \
            "tracked NT shared-resource cache insertion"
        return
    fi

    echo "hyperv-dxg-validate: CreateNtSharedObject did not produce a valid NT shared resource" >&2
    grep -E 'dxg_ntshared_(create_attempts|last|attempt_meta|wsl_exact)|dxg_shareobject_last|dxg_global_send_sharewire|dxg_sharedresource_(metadata|preseal)|dxg_sharedhandle_last' "${LOG}" >&2 ||
        true
    echo "hyperv-dxg-validate: log: ${LOG}" >&2
    exit 1
}

require_no_d3d12_cpu_or_partial_present_path() {
    if grep -Eq 'd3d12_(cpu_readback|cpu_mapping|cpu_copy|readback|native_present_unimplemented|present_state_only|present_fence_only|present_import_only|present_open_only|present_callback_only|present_release_only)=[1-9][0-9]*' "${LOG}" ||
       grep -Eq 'd3d12_display_handoff_implemented=0' "${LOG}" ||
       grep -Eq '(callbacks_blocked|releases_blocked)=[1-9][0-9]*' "${LOG}" ||
       grep -Eq 'd3d12_present_errno=([1-9][0-9]*)' "${LOG}" ||
       grep -Eq 'd3d12_native_present_requirements_satisfied[ =]0|native_requirements=0|d3d12_present_source_(commit_rejected_eopnotsupp|no_present_id_completed|gpu_p_or_dda_transport_absent|same_frame_callbacks_blocked|same_frame_releases_blocked)[ =]1|d3d12_present_source_buffer_query_skipped_commit_failed[ =]1|d3d12_present_source_query_skipped_reason=commit-failed' "${LOG}"; then
        fail "D3D12 present validation used an incomplete or CPU/readback path"
    fi
}

require_wave60_native_present_keys() {
    require_log 'd3d12_native_present_requirements_satisfied[ =]1' \
        "Wave60 native-present requirements satisfied marker"
    require_log 'd3d12_present_source_luid_valid[ =]1' \
        "Wave60 present-source LUID validity"
    require_log 'd3d12_present_source_registered[ =]1' \
        "Wave60 present-source registration proof"
    require_log 'd3d12_present_source_query_attempted[ =]1' \
        "Wave60 present-source query after commit proof"
    require_log 'd3d12_present_source_buffer_commit_successes[ =][1-9][0-9]*' \
        "Wave60 present-source buffer commit success"
    require_log 'd3d12_present_source_buffer_present_id[ =][1-9][0-9]*' \
        "Wave60 present-source buffer nonzero present id"
    require_log 'd3d12_present_source_buffer_completed[ =][1-9][0-9]*' \
        "Wave60 present-source buffer nonzero completed counter"
    require_log 'd3d12_present_source_buffer_completion_correlated[ =]1' \
        "Wave60 present-source buffer completion correlation"
    require_log 'd3d12_present_source_buffer_query_attempted_after_commit_success[ =]1' \
        "Wave60 present-source buffer query after successful commit"
    require_log 'd3d12_buffer_release_present_id[ =][1-9][0-9]*' \
        "Wave60 buffer release present-id correlation"
    require_log 'd3d12_buffer_release_same_present_id[ =]1' \
        "Wave60 buffer release same-present-id proof"
    require_log 'd3d12_frame_callback_present_id[ =][1-9][0-9]*' \
        "Wave60 frame callback present-id correlation"
    require_log 'd3d12_frame_callback_same_present_id[ =]1' \
        "Wave60 frame callback same-present-id proof"
}

require_d3d12_gpup_dda_commit_accepted() {
    require_any_counter_at_least 1 "D3D12 GPU-P/DDA present-source commit accepted" \
        d3d12_dxg_present_source_commit_successes \
        d3d12_present_source_commit_successes \
        d3d12_present_source_buffer_commit_successes \
        dxg_present_commit_successes \
        d3d12_final_handoff_host_display_commit_success \
        d3d12_present_source_commit_accepted \
        d3d12_dxg_present_source_commit_accepted \
        d3d12_host_display_commit_accepted
}

require_d3d12_same_frame_callback_release() {
    require_log 'd3d12sharedsmoke: present validation ok frame=1 release=1 gpu_present=[0-9]+->[1-9][0-9]*' \
        "D3D12 same-frame callback/release present validation"

    if grep -Eq 'd3d12_.*same_frame|same_frame_.*d3d12' "${LOG}"; then
        require_any_counter_at_least 1 "D3D12 same-frame callback/release proof" \
            d3d12_callback_release_same_frame_observed \
            d3d12_same_frame_callback_release_observed \
            d3d12_same_frame_callback_release \
            d3d12_present_same_frame_callback_release \
            d3d12_present_same_frame_callback_release_observed
        if grep -Eq 'd3d12_.*callback.*same_frame|d3d12_.*same_frame.*callback' "${LOG}"; then
            require_any_counter_at_least 1 "D3D12 same-frame callback proof" \
                d3d12_same_frame_callback_observed \
                d3d12_present_same_frame_callback \
                d3d12_present_same_frame_callback_observed
        fi
        if grep -Eq 'd3d12_.*release.*same_frame|d3d12_.*same_frame.*release' "${LOG}"; then
            require_any_counter_at_least 1 "D3D12 same-frame release proof" \
                d3d12_same_frame_release_observed \
                d3d12_present_same_frame_release \
                d3d12_present_same_frame_release_observed
        fi
    fi
}

note_d3d12_copy_subgate() {
    if grep -Eq 'd3d12_gpu_copy_completes=[1-9][0-9]*' "${LOG}" &&
       ! grep -Eq 'd3d12_display_handoff_implemented[ =]1' "${LOG}"; then
        echo "hyperv-dxg-validate: D3D12 compositor-copy sub-gate observed; native-present/FPS/WebKit credit still requires d3d12_display_handoff_implemented=1, present completion, frame callback/release, display correlation, visible FPS, and backend flags" |
            tee -a "${LOG}"
    fi
}

last_counter_value() {
    local key=$1

    sed -nE \
        "s/.*(^|[[:space:]])${key}([ =])(0x[0-9a-fA-F]+|[0-9]+).*/\\3/p" \
        "${LOG}" | tail -n 1
}

last_any_counter_value() {
    local key
    local value

    for key in "$@"; do
        value=$(last_counter_value "${key}")
        if [[ -n "${value}" ]]; then
            printf '%s\n' "${value}"
            return 0
        fi
    done
}

require_counter_at_least() {
    local key=$1
    local min=$2
    local description=$3
    local value

    value=$(last_counter_value "${key}")
    if [[ -z "${value}" ]]; then
        fail "missing ${description}: ${key}"
    fi
    if (( value < min )); then
        fail "${description} too small: ${key}=${value} required>=${min}"
    fi
}

require_any_counter_at_least() {
    local min=$1
    local description=$2
    local value
    shift 2

    value=$(last_any_counter_value "$@")
    if [[ -z "${value}" ]]; then
        fail "missing ${description}: expected one of $*"
    fi
    if (( value < min )); then
        fail "${description} too small: value=${value} required>=${min}"
    fi
}

require_validation_run_id() {
    local description=$1

    if ! grep -Eq "^(validation_run_id|xv6_validation_run_id|d3d12_validation_run_id|fps_validation_run_id|d3d12_run_id)=${VALIDATION_RUN_ID}($|[[:space:]])" "${LOG}"; then
        fail "missing ${description} validation run id: ${VALIDATION_RUN_ID}"
    fi
}

require_d3d12_present_display_correlation() {
    local starts
    local copies
    local completes
    local callbacks
    local releases
    local present_id
    local present_completed
    local display_completions
    local display_last_complete

    require_counter_at_least d3d12_gpu_present_starts 1 \
        "D3D12 native-present start counter"
    require_counter_at_least d3d12_gpu_copy_completes 1 \
        "D3D12 GPU-copy completion counter"
    require_counter_at_least d3d12_display_handoff_implemented 1 \
        "D3D12 display-handoff implementation marker"
    require_wave60_native_present_keys
    require_d3d12_gpup_dda_commit_accepted
    require_d3d12_same_frame_callback_release
    require_counter_at_least display_completions 1 \
        "display completion counter"
    require_counter_at_least display_last_complete 1 \
        "latest display completion sequence"
    require_any_counter_at_least 1 "D3D12 frame callback counter" \
        d3d12_frame_callbacks d3d12_frame_callback_observed \
        d3d12_present_callbacks \
        d3d12_gpu_present_callbacks d3d12_callback_count
    require_any_counter_at_least 1 "D3D12 buffer release counter" \
        d3d12_buffer_releases d3d12_buffer_release_observed \
        d3d12_present_releases \
        d3d12_gpu_present_releases d3d12_release_count
    require_counter_at_least d3d12_evidence_generation 1 \
        "D3D12 evidence generation counter"
    require_counter_at_least d3d12_present_evidence_time_us 1 \
        "D3D12 evidence timestamp"
    require_counter_at_least d3d12_present_resource 1 \
        "D3D12 native-present resource identity"
    require_counter_at_least d3d12_runtime_created_d3d12_resource_required 1 \
        "D3D12 runtime-created resource strict-present marker"
    require_any_counter_at_least 1 "D3D12 content CRC counter" \
        d3d12_present_content_crc d3d12_present_region_crc \
        d3d12_client_content_crc d3d12_visible_content_crc \
        d3d12_present_crc
    require_any_counter_at_least 1 "D3D12 content frame/change counter" \
        d3d12_present_content_frame d3d12_present_content_frames \
        d3d12_present_content_change d3d12_present_content_changes \
        d3d12_visible_content_frame d3d12_visible_content_frames

    starts=$(last_counter_value d3d12_gpu_present_starts)
    copies=$(last_counter_value d3d12_gpu_copy_completes)
    callbacks=$(last_any_counter_value \
        d3d12_frame_callbacks d3d12_frame_callback_observed \
        d3d12_present_callbacks \
        d3d12_gpu_present_callbacks d3d12_callback_count)
    releases=$(last_any_counter_value \
        d3d12_buffer_releases d3d12_buffer_release_observed \
        d3d12_present_releases \
        d3d12_gpu_present_releases d3d12_release_count)
    present_id=$(last_any_counter_value d3d12_dxg_present_id present_id)
    present_completed=$(last_any_counter_value d3d12_dxg_present_completed completed)
    completes=$(last_counter_value d3d12_gpu_present_completes)
    if [[ -z "${completes}" ]]; then
        completes=$(last_counter_value d3d12_gpu_present_complete)
    fi
    if [[ -z "${completes}" || "${completes}" -lt 1 ]]; then
        fail "missing D3D12 native-present completion counter"
    fi
    if [[ -z "${present_id}" || "${present_id}" -lt 1 ]]; then
        fail "missing nonzero D3D12 DXG present_id"
    fi
    if [[ -z "${present_completed}" || "${present_completed}" -lt 1 ]]; then
        fail "missing nonzero D3D12 DXG completed counter"
    fi
    if (( present_completed < present_id )); then
        fail "D3D12 DXG completed counter does not cover present_id: present_id=${present_id} completed=${present_completed}"
    fi
    display_completions=$(last_counter_value display_completions)
    display_last_complete=$(last_counter_value display_last_complete)
    if (( completes > starts )); then
        fail "D3D12 native-present completions exceed starts: completes=${completes} starts=${starts}"
    fi
    if (( completes > copies )); then
        fail "D3D12 native-present completions exceed GPU-copy completions: completes=${completes} copies=${copies}"
    fi
    if (( completes > callbacks )); then
        fail "D3D12 native-present completions exceed frame callbacks: completes=${completes} callbacks=${callbacks}"
    fi
    if (( completes > releases )); then
        fail "D3D12 native-present completions exceed buffer releases: completes=${completes} releases=${releases}"
    fi
    if (( completes > display_completions )); then
        fail "D3D12 native-present completions exceed display completions: completes=${completes} display=${display_completions}"
    fi
    if (( display_last_complete < display_completions )); then
        fail "display completion sequence regressed: last=${display_last_complete} completions=${display_completions}"
    fi
    require_log 'mode=(direct-scanout|bo-present)' \
        "native-present evidence tied to GPU-backed compositor mode"
    require_log 'd3d12_present_path=d3d12-dxg-present-source-display-handoff' \
        "D3D12 native-present path counter file"
    require_log '(^|[[:space:]])(d3d12_)?existing_sysmem_is_d3d12_com_resource[ =]0' \
        "existing-sysmem boundary excluded from D3D12 COM-resource strict-present credit"
    require_validation_run_id "D3D12 native-present"
    require_no_d3d12_cpu_or_partial_present_path
}

require_d3d12_shared_surface_contract() {
    fail_import_only_d3d12_evidence
    note_d3d12_copy_subgate
    require_log 'd3d12sharedsmoke: shared resource=.*allocations=[1-9][0-9]*' \
        "D3D12 raw shared-resource export/query smoke"
    require_log 'd3d12sharedsmoke: direct open shared resource ok .*allocations=[1-9][0-9]* total_priv=[1-9][0-9]* .*copied_priv=1' \
        "D3D12 direct shared-resource open/private metadata smoke"
    require_log 'd3d12sharedsmoke: shared fence=.*cpu=0x[1-9a-fA-F]' \
        "D3D12 raw shared-fence export smoke"
    require_log 'd3d12sharedsmoke: mismatched adapter LUID rejected' \
        "D3D12 Wayland adapter-LUID mismatch rejection"
    require_log 'd3d12sharedsmoke: stale present evidence cleared path=/tmp/wlcomp-d3d12-present existed=[01]' \
        "D3D12 stale present evidence removal"
    require_log 'd3d12sharedsmoke: runtime CreateSharedHandle\(resource\) ok fd=[0-9]+ handle=.*access=generic-all/0x10000000' \
        "D3D12 runtime resource export success"
    require_log 'd3d12sharedsmoke: runtime OpenSharedHandle\(resource\) ok desc=256x256 fmt=[0-9]+ flags=0x[0-9a-fA-F]+ layout=[0-9]+ samples=1' \
        "D3D12 runtime resource self-open success"
    require_log 'd3d12sharedsmoke: runtime CreateSharedHandle\(fence\) ok phase=(normal|after-resource|before-resource) fd=[0-9]+ handle=.* flags=SHARED' \
        "D3D12 runtime fence export success"
    require_log 'd3d12sharedsmoke: runtime OpenSharedHandle\(fence\) ok phase=(normal|after-resource|before-resource) completed=[0-9]+ expected_at_least=[0-9]+' \
        "D3D12 runtime fence self-open success"
    require_log 'd3d12sharedsmoke: runtime shared resource fd=[0-9]+ desc=256x256 fmt=[0-9]+ fence_fd=[0-9]+ completed=1' \
        "D3D12 runtime shared-resource export/open and fence signal"
    require_log 'd3d12sharedsmoke: runtime query adapter=0x[1-9a-fA-F][0-9a-fA-F]* allocations=[1-9][0-9]* total_priv=[1-9][0-9]*' \
        "D3D12 runtime shared-resource metadata query"
    require_log 'd3d12sharedsmoke: using gpu-manager-v5 fence-value path' \
        "D3D12 Wayland adapter-LUID/fence-value protocol path"
    require_log 'wlcomp: d3d12 shared buffer .*source_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8}).*matched_luid=\1:\2.*opened_fence=' \
        "compositor D3D12 resource/fence same-adapter import"
    require_log 'wlcomp: d3d12 shared buffer fence target=1 current=1' \
        "D3D12 Wayland monitored-fence value import"
    require_log 'd3d12sharedsmoke: committed D3D12 shared resource buffer release=1' \
        "D3D12 wl_buffer release callback"
    require_log 'd3d12sharedsmoke: present validation ok frame=1 release=1 gpu_present=[0-9]+->[1-9][0-9]*' \
        "D3D12 native-present/shared-surface precondition validation"
    require_log 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff .*display_bind_backend=gpup_dxg_scanout_bind .*display_bind_transport=gpu-p-dxg-resource-scanout-bind .*display_bind_transport_source=non_wsl_linux_dxgkrnl_extension .*host_saw_display_bind_packet=1 .*wsl_presenthistory_completion_credit=0 .*display_bind_present_id=[1-9][0-9]* .*display_bind_completed_id=[1-9][0-9]* .*display_bind_resource_generation=[1-9][0-9]* .*display_bind_completion_source=display .*present_id=[1-9][0-9]* completed=[1-9][0-9]* .*mtime_ms=[1-9][0-9]* min_mtime_ms=[1-9][0-9]* .*starts=[1-9][0-9]* copy=[1-9][0-9]* completes=[1-9][0-9]* .*resource=0x[1-9a-fA-F][0-9a-fA-F]* allocations=[1-9][0-9]* fence=0x[1-9a-fA-F][0-9a-fA-F]* target=1 release=[1-9][0-9]* .*source_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8}) matched_luid=\1:\2 no_cpu_readback=1' \
        "D3D12 pure-C native-present evidence contract"
    require_log 'd3d12_runtime_created_d3d12_resource_required[ =]1' \
        "strict-present requires runtime-created D3D12 resource"
    require_log '(^|[[:space:]])(d3d12_)?existing_sysmem_is_d3d12_com_resource[ =]0' \
        "strict-present rejects existing-sysmem as a D3D12 COM resource"
    require_log 'd3d12_display_handoff_implemented[ =]1' \
        "D3D12 display handoff implementation marker"
    require_log 'd3d12sharedsmoke: shared-surface OpenGL-submit contract ok mode=d3d12-runtime allocations=[1-9][0-9]* total_priv=[1-9][0-9]* fence_value=1' \
        "D3D12 shared-surface contract marker before any OpenGL-submit claim"
    require_log 'd3d12_gpu_present_complete[s]?=[1-9][0-9]*' \
        "compositor D3D12 native GPU present evidence"
    require_d3d12_present_display_correlation
    require_no_d3d12_cpu_or_partial_present_path
}

require_compositor_gpu_compose_path() {
    if grep -Eq 'wlcomp: gpu-compose presented' "${LOG}"; then
        return
    fi
    if grep -Eq 'gpu_compose=[1-9][0-9]*.*mode=(direct-scanout|bo-present)' "${LOG}"; then
        return
    fi
    if grep -Eq 'mode=(direct-scanout|bo-present).*gpu_compose=[1-9][0-9]*' "${LOG}"; then
        return
    fi

    echo "hyperv-dxg-validate: missing compositor GPU composition path" >&2
    echo "hyperv-dxg-validate: expected wlcomp: gpu-compose presented or gpu_compose>0 with GPU-backed mode" >&2
    echo "hyperv-dxg-validate: log: ${LOG}" >&2
    exit 1
}

require_nv12_multiplane_presentation() {
    require_log 'dmabufsmoke: linux-dmabuf NV12 xv6-gbm backend ok path=.* backend=xv6-gbm planes=2 modifier=0x0' \
        "linux-dmabuf NV12 uses xv6 GBM backend"
    require_log 'dmabufsmoke: NV12 metadata planes=2 y_stride=[1-9][0-9]* uv_stride=[1-9][0-9]* uv_offset=[1-9][0-9]* modifier=0x0' \
        "linux-dmabuf NV12 plane metadata"
    require_log 'wlcomp: dmabuf buffer .*fmt=0x3231564e planes=2 .*flags=0x0' \
        "compositor imported NV12 dmabuf with two planes"
    require_log 'dmabufsmoke: explicit-sync release=(fenced|immediate)' \
        "linux explicit-sync release smoke"
    require_log 'wlcomp: explicit-sync (fenced|immediate) release' \
        "compositor explicit-sync release"
}

require_dxg_fd_cleanup_after_leak_close() {
    local cleanup_line
    local cleanup_attempts
    local cleanup_successes
    local cleanup_last_ret
    local cleanup_failed_op
    local cleanup_failed_handle
    local cleanup_had_tracked

    require_log_line 'V:lc' \
        "DXG leak-close validator invocation"
    require_log 'd3dkmt_open_files=opens:[1-9][0-9]* live:[0-9]+ cleanup_attempts:[1-9][0-9]* cleanup_successes:[1-9][0-9]* cleanup_last_ret:0 .*cleanup_had_tracked:1' \
        "DXG fd cleanup advanced after leak-close"
    require_log 'd3dkmt_cleanup_wsl_order=.*sync:[0-9]+ .*allocation:[0-9]+ .*resource:[0-9]+ .*context:[0-9]+ .*hwqueue:[0-9]+ .*paging:[0-9]+ .*device:[0-9]+ .*process:[0-9]+ .*valid:1' \
        "DXG fd cleanup preserved WSL-style teardown order"
    require_log 'dxg_host_cmd_counts=.*destroyprocess:0' \
        "DXG fd cleanup kept host destroyprocess suppressed"
    require_log 'dxg_closeadapter_order=.*destroyprocess:0' \
        "DXG close-adapter ordering kept destroyprocess suppressed"

    cleanup_line=$(grep -E 'd3dkmt_open_files=.*cleanup_attempts:' "${LOG}" |
        tail -n 1 || true)
    if [[ ! "${cleanup_line}" =~ cleanup_attempts:([0-9]+).*cleanup_successes:([0-9]+).*cleanup_last_ret:([-0-9]+).*cleanup_failed_op:([0-9]+).*cleanup_failed_handle:0x([0-9a-fA-F]+).*cleanup_had_tracked:([0-9]+) ]]; then
        fail "DXG fd cleanup diagnostics were malformed after leak-close"
    fi

    cleanup_attempts=${BASH_REMATCH[1]}
    cleanup_successes=${BASH_REMATCH[2]}
    cleanup_last_ret=${BASH_REMATCH[3]}
    cleanup_failed_op=${BASH_REMATCH[4]}
    cleanup_failed_handle=${BASH_REMATCH[5]}
    cleanup_had_tracked=${BASH_REMATCH[6]}
    if (( cleanup_attempts == 0 )); then
        fail "DXG fd cleanup did not advance after leak-close"
    fi
    if (( cleanup_successes == 0 || cleanup_successes < cleanup_attempts )); then
        fail "DXG fd cleanup successes did not cover cleanup attempts"
    fi
    if (( cleanup_last_ret != 0 || cleanup_failed_op != 0 ||
          16#${cleanup_failed_handle} != 0 || cleanup_had_tracked == 0 )); then
        fail "DXG fd cleanup reported failure fields after leak-close"
    fi

    if grep -Eq 'd3dkmt_open_files=.*cleanup_last_ret:-?[1-9][0-9]*' "${LOG}"; then
        fail "DXG fd cleanup reported a failing cleanup_last_ret"
    fi
    if grep -Eq 'd3dkmt_open_files=.*cleanup_failed_op:[1-9][0-9]*' "${LOG}"; then
        fail "DXG fd cleanup reported a failed cleanup operation"
    fi
    if grep -Eq 'd3dkmt_open_files=.*cleanup_failed_handle:0x[1-9a-fA-F][0-9a-fA-F]*' "${LOG}"; then
        fail "DXG fd cleanup reported a failed cleanup handle"
    fi
    if grep -Eq 'dxg_host_cmd_counts=.*destroyprocess:[1-9][0-9]*' "${LOG}"; then
        fail "DXG fd cleanup unexpectedly sent host destroyprocess"
    fi
    if grep -Eq 'dxg_closeadapter_order=.*destroyprocess:[1-9][0-9]*' "${LOG}"; then
        fail "DXG close-adapter ordering recorded unexpected destroyprocess"
    fi
}

run_guest() {
    local cmd=$1
    local read_ms=${2:-${READ_MS}}
    local tx
    local begin
    local end
    local wrapped
    local out
    local clean
    local rc
    local begin_count
    local end_count
    local poll_count
    local retry
    local retry_out
    local retry_clean
    local poll

    GUEST_TX_SEQ=$((GUEST_TX_SEQ + 1))
    tx=$(printf 'HV%03d' "${GUEST_TX_SEQ}")
    begin="B${tx#HV}"
    end="E${tx#HV}"
    poll="P${tx#HV}"
    wrapped="echo ${begin}; ${cmd}; echo ${end}; echo ${end}"
    if (( ${#wrapped} > GUEST_CMD_MAX_CHARS )); then
        fail "guest_tx ${tx} command too long (${#wrapped} > ${GUEST_CMD_MAX_CHARS}); split this run_guest command"
    fi

    echo "hyperv-dxg-validate: guest_tx ${tx} begin" | tee -a "${LOG}"
    set +e
    out=$(serial_read "${wrapped}" "${read_ms}" 2>&1)
    rc=$?
    set -e
    printf '%s\n' "${out}" | tee -a "${LOG}"
    clean=$(printf '%s\n' "${out}" | tr -d '\r')

    if [[ "${rc}" -ne 0 ]]; then
        fail "guest_tx ${tx} serial helper failed rc=${rc}"
    fi
    begin_count=$(printf '%s\n' "${clean}" |
        awk -v marker="${begin}" '$0 == marker { count++ } END { print count + 0 }')
    end_count=$(printf '%s\n' "${clean}" |
        awk -v marker="${end}" '$0 == marker { count++ } END { print count + 0 }')
    poll_count=0
    if (( begin_count < 1 || end_count < 2 )) &&
       { printf '%s\n' "${clean}" | grep -Fq "${begin}" ||
         printf '%s\n' "${clean}" | grep -Fq "${end}" ||
         printf '%s\n' "${clean}" | grep -Fq "${cmd}"; }; then
        for ((retry = 1; retry <= GUEST_MARKER_RETRIES; retry++)); do
            echo "hyperv-dxg-validate: guest_tx ${tx} marker retry ${retry}/${GUEST_MARKER_RETRIES}" |
                tee -a "${LOG}"
            set +e
            retry_out=$(serial_read "echo ${poll}" "${GUEST_MARKER_RETRY_MS}" 2>&1)
            rc=$?
            set -e
            printf '%s\n' "${retry_out}" | tee -a "${LOG}"
            if [[ "${rc}" -ne 0 ]]; then
                echo "hyperv-dxg-validate: guest_tx ${tx} marker retry ${retry} rc=${rc}" |
                    tee -a "${LOG}"
            fi
            retry_clean=$(printf '%s\n' "${retry_out}" | tr -d '\r')
            clean=$(printf '%s\n%s\n' "${clean}" "${retry_clean}")
            begin_count=$(printf '%s\n' "${clean}" |
                awk -v marker="${begin}" '$0 == marker { count++ } END { print count + 0 }')
            end_count=$(printf '%s\n' "${clean}" |
                awk -v marker="${end}" '$0 == marker { count++ } END { print count + 0 }')
            poll_count=$(printf '%s\n' "${clean}" |
                awk -v marker="${poll}" '$0 == marker { count++ } END { print count + 0 }')
            if (( begin_count >= 1 && (end_count >= 2 ||
                  (end_count >= 1 && poll_count >= 1)) )); then
                break
            fi
        done
    fi
    if (( begin_count < 1 )); then
        fail "guest_tx ${tx} missing begin marker; command may have been truncated or merged"
    fi
    if (( end_count < 2 && !(end_count >= 1 && poll_count >= 1) )); then
        fail "guest_tx ${tx} missing repeated end marker; command may still be running, truncated, or merged"
    fi
    if (( end_count < 2 )); then
        echo "hyperv-dxg-validate: guest_tx ${tx} accepted single end marker with poll evidence" |
            tee -a "${LOG}"
    fi
    echo "hyperv-dxg-validate: guest_tx ${tx} end" | tee -a "${LOG}"
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

if [[ "${WDDM_TRACE_CAPTURE_PLAN}" != "0" ]]; then
    print_wddm_trace_capture_plan
    exit 0
fi

if [[ "${DXG_VALIDATE_SEGMENT}" == "full" &&
      "${REQUIRE_WDDM_TRACE_COMPARE}" != "0" &&
      ! -s "${WSL_DXG_TRACE}" ]]; then
    print_wddm_trace_capture_plan
    fail "missing required same-adapter WSL_DXG_TRACE: ${WSL_DXG_TRACE}"
fi

need cmake
need powershell.exe
need cp
need grep
need sleep
need timeout

if [[ "${HYPERV_D3D12_ADAPTER_NAME}" =~ [[:space:]] ]]; then
    fail "HYPERV_D3D12_ADAPTER_NAME must not contain shell whitespace; use a stable substring such as NVIDIA"
fi
if [[ ! "${VALIDATION_RUN_ID}" =~ ^[A-Za-z0-9_.:-]+$ ]]; then
    fail "VALIDATION_RUN_ID contains unsupported characters: ${VALIDATION_RUN_ID}"
fi
case "${DXG_VALIDATE_SEGMENT}" in
    full|pure-c-lifetime) ;;
    *)
        fail "unsupported DXG_VALIDATE_SEGMENT=${DXG_VALIDATE_SEGMENT}"
        ;;
esac

mkdir -p "${BUILD_DIR}"
: >"${LOG}"
echo "hyperv-dxg-validate: validation_run_id=${VALIDATION_RUN_ID}" |
    tee -a "${LOG}"
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
stop_vm_for_deploy
cp -f "${OUT_VHDX}" "${DEPLOY_VHDX}"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "Start-VM -Name '${VM_NAME}'; Get-VM -Name '${VM_NAME}' | Select Name,State,Uptime | Format-List" |
    tee -a "${LOG}"
log_metadata deployed

echo "hyperv-dxg-validate: waiting ${BOOT_WAIT_SEC}s for guest shell"
sleep "${BOOT_WAIT_SEC}"
for attempt in 1 2 3 4; do
    ready_clean=
    ready_out=$(serial_read 'echo __HVRDYB__; echo hyperv-dxg-ready; echo __HVRDYE__' 10000 || true)
    printf '%s\n' "${ready_out}" | tee "${LOG}.ready" >>"${LOG}"
    ready_clean=$(printf '%s\n' "${ready_out}" | tr -d '\r')
    printf '%s\n' "${ready_clean}" >"${LOG}.ready.normalized"
    if printf '%s\n' "${ready_clean}" | grep -Fxq '__HVRDYB__' &&
       printf '%s\n' "${ready_clean}" | grep -Fxq 'hyperv-dxg-ready' &&
       printf '%s\n' "${ready_clean}" | grep -Fxq '__HVRDYE__'; then
        break
    fi
    if [[ "${attempt}" -eq 4 ]]; then
        echo "hyperv-dxg-validate: raw readiness transcript:" >&2
        sed -n '1,40p' "${LOG}.ready" >&2 || true
        echo "hyperv-dxg-validate: normalized readiness transcript:" >&2
        sed -n '1,40p' "${LOG}.ready.normalized" >&2 || true
        fail "guest shell did not become ready"
    fi
    sleep 5
done

if [[ "${DXG_VALIDATE_SEGMENT}" == "pure-c-lifetime" ]]; then
    echo "hyperv-dxg-validate: running pure-C DXG lifetime segment"
    run_guest 'cat /proc/cmdline; cat /proc/version; cat /proc/uptime' 30000
    run_guest 'cat /tmp/wlcomp-fps; fbstat' 30000
    run_guest 'dxgprobe' "${READ_MS}"
    run_guest 'dxgprobe --wddm-payload-validate; dxgprobe --residency-batch-validate; cat /dev/dxg' 180000
    run_guest 'dxgprobe --try-submit; cat /dev/dxg' 180000
    run_guest 'dxgprobe --owner-isolation' 120000
    run_guest 'echo V:cp; dxgprobe --create-publication-faults-validate; cat /dev/dxg; echo D:cp' 180000
    run_guest 'echo V:hl; dxgprobe --handle-lifetime-validate; cat /dev/dxg; echo D:hl' 180000
    run_guest 'echo V:sl; dxgprobe --shared-lifetime-validate; cat /dev/dxg' 180000
    run_guest 'echo V:sr; dxgprobe --shared-seal-provenance-validate; cat /dev/dxg; echo D:sr' 180000
    run_guest 'echo V:sp; dxgprobe --shared-private-validate; cat /dev/dxg' 180000
    run_guest 'echo V:in; dxgprobe --import-negative; cat /dev/dxg; echo D:in' 180000
    run_guest 'echo V:oh; dxgprobe --sync-handle-source-matrix; cat /dev/dxg; echo D:oh' 180000
    run_guest 'echo V:sf; dxgprobe --syncfile; cat /dev/dxg; echo D:sf' 180000

    require_log 'BOOT_IMAGE=/xv6\.bin .*root=/dev/disk0p2' "expected Hyper-V command line"
    require_log 'Linux version .*xv6' "guest kernel version"
    require_log 'backend hyperv-dxg flags' "Hyper-V backend"
    require_log 'backend_dxg_transport 1' "DXG transport flag"
    require_log 'backend_d3dkmt 1' "D3DKMT readiness flag"
    require_log 'backend_opengl_submit 0' "Hyper-V OpenGL submit gating"
    require_no_premature_opengl_submit_claim
    require_log 'dxgprobe: ok' "dxgprobe success"
    require_log 'dxg_adapters3 [1-9][0-9]* handle[0-9]+=0x[1-9a-fA-F][0-9a-fA-F]*' \
        "DXG enum-adapters3 diagnostics"
    require_existing_sysmem_allocation_marker
    require_d3d12_current_run_display_provenance_if_present
    require_log 'dxgprobe: owner isolation rejected (foreign|exec-child) destroy' \
        "DXG owner-isolation rejection"
    require_log 'dxgprobe: owner-isolation ok' \
        "DXG owner-isolation validator"
    require_log 'dxg_createdevice_copyout_unwind_matrix .*rc=-14 .*no_local_publication=1 .*status=PASS' \
        "DXG create-device copyout unwind"
    require_log 'dxg_createcontext_copyout_unwind_matrix .*rc=-14 .*no_local_publication=1 .*status=PASS' \
        "DXG create-context copyout unwind"
    require_log 'dxg_createhwqueue_copyout_unwind_matrix .*rc=-14 .*no_local_publication=1 .*status=PASS' \
        "DXG create-HW-queue copyout unwind"
    require_log 'dxg_create_publication_faults_matrix device=PASS context=PASS hwqueue=PASS status=PASS' \
        "DXG create-publication fault aggregate"
    require_log 'sync_legacy_signal (ok|failed)' \
        "DXG legacy sync signal probe marker"
    require_log 'sync_legacy_wait (ok|failed)' \
        "DXG legacy sync wait probe marker"
    require_log 'sync_legacy_wait_multi_matrix rc=-22 expected=-22 status=PASS' \
        "WSL legacy wait multi-object fail-closed validator"
    require_log 'sync_signal_cpu_event_matrix .*rc=0 .*objects=0 contexts=1 .*flags=0x2 .*status=PASS' \
        "WSL enqueue_cpu_event signal validator"
    require_log 'sync_fromcpu_cpu_event_failclosed_matrix .*rc=-22 expected=-22 status=PASS' \
        "DXG FROMCPU enqueue_cpu_event fail-closed validator"
    require_log 'sync_gpu2_cpu_event_matrix .*rc=0 .*objects=0 contexts=1 .*flags=0x2 .*status=PASS' \
        "WSL enqueue_cpu_event GPU2 signal validator"
    require_log 'dxg_async_message_matrix .*status=(PASS|DEFERRED)' \
        "WSL async-message send-path validator"
    require_log 'd3dkmt_ioctls=.*ready=1' "D3DKMT ioctl diagnostics"
    require_log 'escape_driver_private ok' "DXG escape probe success"
    require_log 'dxg_escape_last=.*ret:0' "DXG escape diagnostics"
    require_log 'share_object_with_host (ok|failed)' "DXG share-with-host probe"
    require_log 'dxg_shareobject_last=len:' "DXG share-with-host diagnostics"
    require_no_guest_exec_failures
    require_no_malformed_validator_markers
    require_log_line 'V:cp' \
        "DXG create-publication fault validator invocation"
    require_log_line 'D:cp' \
        "DXG create-publication fault validator completion marker"
    require_log_line 'V:hl' \
        "WSL DXG handle/process lifetime validator invocation"
    require_log_line 'D:hl' \
        "WSL DXG handle/process lifetime validator completion marker"
    require_log_line 'V:sl' \
        "DXG shared-resource lifetime validator invocation"
    require_log_line 'V:sr' \
        "DXG shared-resource seal/provenance validator invocation"
    require_log_line 'D:sr' \
        "DXG shared-resource seal/provenance validator completion marker"
    require_log_line 'V:sp' \
        "DXG real-private-payload validator invocation"
    require_log_line 'V:in' \
        "D3D12 import-negative validator invocation"
    require_log_line 'D:in' \
        "D3D12 import-negative validator completion marker"
    require_log_line 'V:oh' \
        "OpenSync handle-source validator invocation"
    require_log_line 'D:oh' \
        "OpenSync handle-source validator completion marker"
    require_log_line 'V:sf' \
        "DXG sync-file lifetime validator invocation"
    require_log_line 'D:sf' \
        "DXG sync-file lifetime validator completion marker"
    require_log 'shared_lifetime ok' "DXG shared-resource lifetime validation"
    require_log 'shared_lifetime sealed_add denied' \
        "DXG sealed shared-resource mutation rejection"
    require_log 'shared_private ok' \
        "DXG real-private-payload shared-resource validation"
    require_log 'dxg_process_mem_lifetime_matrix .*object_release_delta=[1-9][0-9]* .*mem_release_delta=[1-9][0-9]* .*mem_free_delta=[1-9][0-9]* .*status=PASS' \
        "WSL dxgprocess object/memory lifetime release matrix"
    require_log 'dxgprocess_adapter_matrix .*raw_host_create_rc=-1 .*local_create_rc=0 .*close_adapter_rc=0 .*child_destroy_after_final_close_rc=-1 .*status=PASS' \
        "WSL dxgprocess local-adapter namespace matrix"
    require_log 'dxgprocess_adapter_parent_matrix child_status=0 status=PASS' \
        "WSL dxgprocess adapter child completion matrix"
    require_log 'handle_lifetime_stale_matrix .*device_second_fd_rejected=1 .*context_rejected=1 .*hwqueue_rejected=1 .*hwqueue_sync_rejected=1 .*sync_rejected=1 .*paging_queue_rejected=1 .*paging_queue_sync_rejected=1 .*resource_rejected=1 .*allocation_rejected=1 .*gpuva_rejected=1 .*device_final_rejected=1 .*status=PASS' \
        "WSL hmgr stale-handle rejection matrix"
    require_log 'handle_lifetime ok .*min_free:128' \
        "WSL hmgr handle lifetime/free-list diagnostics"
    require_log 'shared_resource_parent_lifetime_matrix .*fd_refs=[0-9]+/[0-9]+/0 .*children=[1-9][0-9]*/[2-9][0-9]*/[2-9][0-9]* .*sealed_gen=[1-9][0-9]*/[1-9][0-9]*/[1-9][0-9]* .*status=PASS' \
        "WSL shared-resource parent lifetime matrix"
    require_log 'shared_resource_sealed_alloc_metadata_matrix .*pages0=[1-9][0-9]*/[1-9][0-9]*/[1-9][0-9]* .*model_valid=1/1/1 .*status=PASS' \
        "WSL shared-resource sealed allocation metadata matrix"
    require_log 'shared_resource_seal_provenance_matrix .*refcounts_coherent=1 .*record_generation_coherent=1 .*canonical_record_coherent=1 .*shared_model_coherent=1 .*no_present_credit=1 .*status=PASS' \
        "WSL shared-resource seal/provenance zero-credit matrix"
    require_shared_resource_seal_provenance_if_present
    require_import_negative_matrix
    require_opensyncobject_source_matrix_if_present
    require_opensync_handle_source_matrix_if_present
    require_ntshared_close_behavior_matrix_if_present
    require_log 'wddm_payload_validate (ok|predevice_pending) context_len=[1-9][0-9]* context_priv=[1-9][0-9]* .* hwqueue_priv=[1-9][0-9]* .* submit_priv=[1-9][0-9]*' \
        "real UMD WDDM private payload diagnostics"
    require_log 'dxg_packet_shape_matrix .*createprocess=1 .*createdevice=1 .*createcontext=1 .*createhwqueue=1 .*createallocation=1 .*makeresident=1 .*openresource=1 .*sync_create=1 .*opensync=1 .*shareobject=1 .*waitgpu=1 .*unwind=1 .*status=PASS' \
        "DXG packet-shape aggregate validator"
    require_wddm_payload_residency
    require_makeresident_count2_packet_shape
    require_wddm_layout_or_predevice_pending
    require_log 'sync_file_matrix .*create_rc=0 .*sync_file=[1-9][0-9]* .*open_rc=0 .*open_sync=0x[1-9a-fA-F][0-9a-fA-F]* .*child_status=0 .*child_rc=0' \
        "DXG sync-file create/open/child-open matrix"
    require_log 'dxg_syncfile_create_unwind_matrix create_fault_rc=-[0-9]+ .*fd_visible=0 .*balanced=1 .*status=PASS' \
        "DXG sync-file create copyout-failure unwind matrix"
    require_log 'dxg_syncfile_open_unwind_matrix open_fault_rc=-[0-9]+ .*destroy_ret=0 .*source_fd_valid=1 .*no_local_leak=1 .*status=PASS' \
        "DXG sync-file open copyout-failure unwind matrix"
    require_log 'dxg_syncfile_lifetime=.*create_faults:[1-9][0-9]* .*fd_reclaimed:[1-9][0-9]* .*open_faults:[1-9][0-9]* .*open_destroy:[1-9][0-9]*/[1-9][0-9]*/0' \
        "DXG sync-file lifetime cleanup diagnostics"
    require_no_guest_exec_failures
    require_no_fatal_dxg_log_signatures
    require_no_malformed_validator_markers
    echo "hyperv-dxg-validate: passed pure-C DXG lifetime segment (${LOG})" |
        tee -a "${LOG}"
    exit 0
fi

echo "hyperv-dxg-validate: running guest probes"
run_guest 'cat /proc/cmdline; cat /proc/version; cat /proc/uptime' 30000
run_guest 'ps; cat /tmp/glsmoke-status' 30000
run_guest 'cat /tmp/wlcomp-fps; fbstat' 30000
run_guest 'gpubuftest 1; gpubuftest --render-owner; fbstat' 120000
run_guest 'drmprimeprobe; fbstat' 120000
run_guest 'gbmtest' 120000
run_guest 'dmabufsmoke --hold-ms=1200' 120000
run_guest 'sleep 2; cat /tmp/wlcomp-fps' 30000
run_guest 'dmabufsmoke --nv12 --explicit-sync' 120000
run_guest "mesad3d12probe --adapter ${HYPERV_D3D12_ADAPTER_NAME}; dxgprobe --wddm-payload-validate; dxgprobe --residency-batch-validate; cat /dev/dxg" 180000
if [[ "${REQUIRE_WDDM_TRACE_COMPARE}" != "0" ]]; then
    if [[ ! -x "${DXGPROBE_HOST}" ]]; then
        print_wddm_trace_capture_plan
        fail "missing host dxgprobe validator: ${DXGPROBE_HOST}"
    fi
    "${DXGPROBE_HOST}" --wddm-trace-compare "${WSL_DXG_TRACE}" "${LOG}" |
        tee -a "${LOG}"
fi
run_guest 'dxgprobe' "${READ_MS}"
run_guest 'dxgprobe --try-submit; cat /dev/dxg' 180000
run_guest 'dxgprobe --owner-isolation' 120000
run_guest 'echo V:cp; dxgprobe --create-publication-faults-validate; cat /dev/dxg; echo D:cp' 180000
run_guest 'echo V:hl; dxgprobe --handle-lifetime-validate; cat /dev/dxg; echo D:hl' 180000
run_guest 'echo V:sl; dxgprobe --shared-lifetime-validate; cat /dev/dxg' 180000
run_guest 'echo V:sr; dxgprobe --shared-seal-provenance-validate; cat /dev/dxg; echo D:sr' 180000
run_guest 'echo V:sp; dxgprobe --shared-private-validate; cat /dev/dxg' 180000
run_guest 'echo V:in; dxgprobe --import-negative; cat /dev/dxg; echo D:in' 180000
run_guest 'echo V:oh; dxgprobe --sync-handle-source-matrix; cat /dev/dxg; echo D:oh' 180000
run_guest 'echo V:sf; dxgprobe --syncfile; cat /dev/dxg; echo D:sf' 180000
run_guest 'echo V:bl; d3d12sharedsmoke --bad-luid' 120000
run_guest 'echo V:eo; d3d12sharedsmoke' 180000
run_guest 'echo V:rp' 10000
run_guest "rm -f /tmp/wlcomp-d3d12-present; XV6_GPU_VALIDATE_RUN_ID=${VALIDATION_RUN_ID} XV6_WLCOMP_D3D12_RUN_ID=${VALIDATION_RUN_ID} d3d12sharedsmoke --runtime --require-present" 180000
run_guest 'echo D:rp' 10000
run_guest 'echo V:pe' 10000
run_guest 'cat /tmp/wlcomp-d3d12-present' 30000
run_guest 'echo D:pe' 10000
run_guest 'echo V:dx' 10000
run_guest 'cat /dev/dxg' 30000
run_guest 'echo D:dx' 10000
run_guest 'echo V:lc; dxgprobe --leak-close; dxgprobe; cat /dev/dxg' 180000
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
require_no_premature_opengl_submit_claim
require_split_transport_policy
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
require_nv12_multiplane_presentation
require_compositor_gpu_compose_path
require_log 'gpu_compose=[1-9][0-9]*' \
    "compositor GPU composition accounting"
require_log 'mesad3d12probe: exec mesaglfeature with GALLIUM_DRIVER=d3d12 .*MESA_LOADER_DRIVER_OVERRIDE=d3d12 .*D3D12_DEBUG=verbose' \
    "Mesa D3D12 mesaglfeature launch wrapper"
require_log "xv6-mesa-dxcore: candidate diagnostics begin version=${MESA_DXCORE_DIAGNOSTIC_VERSION}" \
    "Mesa D3D12 DXCore candidate diagnostics from rebuilt libgallium"
require_log 'mesaglfeature: D3D12 renderer confirmed renderer=.*[Dd]3[Dd]12' \
    "Mesa GL feature probe D3D12 renderer selection"
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
require_log 'dxg_adapters3 [1-9][0-9]* handle[0-9]+=0x[1-9a-fA-F][0-9a-fA-F]*' \
    "DXG enum adapters3 coverage"
require_existing_sysmem_allocation_marker
if grep -Eq 'existing_sysmem_allocation destroy_failed' "${LOG}"; then
    fail "DXG existing-sysmem allocation cleanup failed"
fi
require_wave44_scanout_boundary_proof
require_wave45_lifetime_rows_if_present
require_existing_sysmem_ntbridge_evidence_if_present
require_d3d12_current_run_display_provenance_if_present
require_log 'dxgprobe: owner isolation rejected (foreign|exec-child) destroy' \
    "DXG open-local owner isolation rejection"
require_log 'dxgprobe: owner-isolation ok' \
    "DXG open-local owner isolation success"
require_log 'dxg_createdevice_copyout_unwind_matrix .*rc=-14 .*no_local_publication=1 .*status=PASS' \
    "DXG CREATEDEVICE post-host-create copyout unwind"
require_log 'dxg_createcontext_copyout_unwind_matrix .*rc=-14 .*no_local_publication=1 .*status=PASS' \
    "DXG CREATECONTEXTVIRTUAL post-host-create copyout unwind"
require_log 'dxg_createhwqueue_copyout_unwind_matrix .*rc=-14 .*no_local_publication=1 .*status=PASS' \
    "DXG CREATEHWQUEUE post-host-create copyout unwind"
require_log 'dxg_create_publication_faults_matrix device=PASS context=PASS hwqueue=PASS status=PASS' \
    "DXG create-publication aggregate validator"
require_log 'sync_legacy_signal (ok|failed)' \
    "generic DXG sync signal probe marker"
require_log 'sync_legacy_wait (ok|failed)' \
    "generic DXG sync wait probe marker"
require_log 'sync_legacy_wait_multi_matrix rc=-22 expected=-22 status=PASS' \
    "WSL legacy GPU wait rejects multi-object packet shape"
require_log 'sync_signal_cpu_event_matrix .*rc=0 .*objects=0 contexts=1 .*flags=0x2 .*status=PASS' \
    "WSL enqueue_cpu_event signal-object validator"
require_log 'sync_fromcpu_cpu_event_failclosed_matrix .*rc=-22 expected=-22 status=PASS' \
    "DXG FROMCPU enqueue_cpu_event fail-closed validator"
require_log 'sync_gpu2_cpu_event_matrix .*rc=0 .*objects=0 contexts=1 .*flags=0x2 .*status=PASS' \
    "WSL enqueue_cpu_event GPU2 signal validator"
require_log 'dxg_synccpuevent_signal=.*successes:[1-9][0-9]* .*ret:0 .*flags:0x2 objects:0 contexts:1 .*event:[1-9][0-9]*' \
    "DXG enqueue_cpu_event packet diagnostics"
require_log 'dxg_async_message_matrix .*status=(PASS|DEFERRED)' \
    "WSL async-message send-path validator"
require_log 'dxg_async_send_last=enabled:[01] attempts:[0-9]+ successes:[0-9]+ fallback_sync:[0-9]+ cmd:[0-9]+ cmd_len:[0-9]+ wire_len:[0-9]+ async_bit:[01] route_global:[01] .*packet_type:[0-9]+ ret:0' \
    "DXG async-message packet diagnostics"
require_log 'sync_gpu2_signal (ok|failed)' \
    "DXG GPU2 sync signal probe marker"
require_dxg_fd_cleanup_after_leak_close
require_log 'drmgpuprobe: passed' "DRM/GBM probe success"
require_log 'd3dkmt_ioctls=.*ready=1' "D3DKMT ioctl diagnostics"
require_log 'escape_driver_private ok' "DXG escape probe success"
require_log 'dxg_escape_last=.*ret:0' "DXG escape diagnostics"
require_log 'share_object_with_host (ok|failed)' "DXG share-with-host probe"
require_log 'dxg_shareobject_last=len:' "DXG share-with-host diagnostics"
require_no_guest_exec_failures
require_no_malformed_validator_markers
require_log_line 'V:cp' \
    "DXG create-publication fault validator invocation"
require_log_line 'D:cp' \
    "DXG create-publication fault validator completion marker"
require_log_line 'V:hl' \
    "WSL DXG handle/process lifetime validator invocation"
require_log_line 'D:hl' \
    "WSL DXG handle/process lifetime validator completion marker"
require_log_line 'V:sl' \
    "DXG shared-resource lifetime validator invocation"
require_log_line 'V:sr' \
    "DXG shared-resource seal/provenance validator invocation"
require_log_line 'D:sr' \
    "DXG shared-resource seal/provenance validator completion marker"
require_log_line 'V:sp' \
    "DXG real-private-payload validator invocation"
require_log_line 'V:in' \
    "D3D12 import-negative validator invocation"
require_log_line 'D:in' \
    "D3D12 import-negative validator completion marker"
require_log_line 'V:oh' \
    "OpenSync handle-source validator invocation"
require_log_line 'D:oh' \
    "OpenSync handle-source validator completion marker"
require_log_line 'V:sf' \
    "DXG sync-file lifetime validator invocation"
require_log_line 'D:sf' \
    "DXG sync-file lifetime validator completion marker"
require_log_line 'V:bl' \
    "D3D12 bad-LUID validator invocation"
require_log_line 'V:eo' \
    "D3D12 export/open/fence validator invocation"
require_log_line 'V:rp' \
    "D3D12 runtime present validator invocation"
require_log_line 'D:rp' \
    "D3D12 runtime present validator completion marker"
require_log_line 'V:pe' \
    "D3D12 native-present evidence collection invocation"
require_log_line 'D:pe' \
    "D3D12 native-present evidence collection completion marker"
require_log_line 'V:dx' \
    "DXG status collection after D3D12 native-present validator"
require_log_line 'D:dx' \
    "DXG status collection after D3D12 native-present completion marker"
require_log_line 'V:lc' \
    "DXG leak-close validator invocation"
require_d3d12_ntshare_diagnostics
fail_known_d3d12_shared_blocker
require_log 'shared_lifetime ok' "DXG shared-resource lifetime validation"
require_log 'shared_lifetime sealed_add denied' \
    "DXG sealed shared-resource mutation rejection"
require_log 'shared_private ok' \
    "DXG real-private-payload shared-resource validation"
require_log 'dxg_process_mem_lifetime_matrix .*object_release_delta=[1-9][0-9]* .*mem_release_delta=[1-9][0-9]* .*mem_free_delta=[1-9][0-9]* .*status=PASS' \
    "WSL dxgprocess object/memory lifetime release matrix"
require_log 'dxgprocess_adapter_matrix .*raw_host_create_rc=-1 .*local_create_rc=0 .*close_adapter_rc=0 .*child_destroy_after_final_close_rc=-1 .*status=PASS' \
    "WSL dxgprocess local-adapter namespace matrix"
require_log 'dxgprocess_adapter_parent_matrix child_status=0 status=PASS' \
    "WSL dxgprocess adapter child completion matrix"
require_log 'handle_lifetime_stale_matrix .*device_second_fd_rejected=1 .*context_rejected=1 .*hwqueue_rejected=1 .*hwqueue_sync_rejected=1 .*sync_rejected=1 .*paging_queue_rejected=1 .*paging_queue_sync_rejected=1 .*resource_rejected=1 .*allocation_rejected=1 .*gpuva_rejected=1 .*device_final_rejected=1 .*status=PASS' \
    "WSL hmgr stale-handle rejection matrix"
require_log 'handle_lifetime ok .*min_free:128' \
    "WSL hmgr handle lifetime/free-list diagnostics"
require_log 'shared_resource_parent_lifetime_matrix .*fd_refs=[0-9]+/[0-9]+/0 .*children=[1-9][0-9]*/[2-9][0-9]*/[2-9][0-9]* .*sealed_gen=[1-9][0-9]*/[1-9][0-9]*/[1-9][0-9]* .*status=PASS' \
    "WSL shared-resource parent lifetime matrix"
require_log 'shared_resource_sealed_alloc_metadata_matrix .*pages0=[1-9][0-9]*/[1-9][0-9]*/[1-9][0-9]* .*model_valid=1/1/1 .*status=PASS' \
    "WSL shared-resource sealed allocation metadata matrix"
require_log 'shared_resource_seal_provenance_matrix .*refcounts_coherent=1 .*record_generation_coherent=1 .*canonical_record_coherent=1 .*shared_model_coherent=1 .*no_present_credit=1 .*status=PASS' \
    "WSL shared-resource seal/provenance zero-credit matrix"
require_shared_resource_seal_provenance_if_present
require_import_negative_matrix
require_opensyncobject_source_matrix_if_present
require_opensync_handle_source_matrix_if_present
require_ntshared_close_behavior_matrix_if_present
require_log 'wddm_payload_validate (ok|predevice_pending) context_len=[1-9][0-9]* context_priv=[1-9][0-9]* .* hwqueue_priv=[1-9][0-9]* .* submit_priv=[1-9][0-9]*' \
    "real UMD WDDM private payload diagnostics"
require_log 'dxg_packet_shape_matrix .*createprocess=1 .*createdevice=1 .*createcontext=1 .*createhwqueue=1 .*createallocation=1 .*makeresident=1 .*openresource=1 .*sync_create=1 .*opensync=1 .*shareobject=1 .*waitgpu=1 .*unwind=1 .*status=PASS' \
    "DXG packet-shape aggregate validator"
require_wddm_payload_residency
require_makeresident_count2_packet_shape
require_wddm_layout_or_predevice_pending
require_log 'dxg_openadapterfromluid=input:[0-9a-fA-F]+:[0-9a-fA-F]+ um_adapter:[0-9a-fA-F]+:[0-9a-fA-F]+ mapped_host:[0-9a-fA-F]+:[0-9a-fA-F]+ host_basis:[1-9][0-9]* match:1 ret:0 .*handle:0x[1-9a-fA-F][0-9a-fA-F]*' \
    "open-by-LUID synthetic to host-equivalent adapter mapping"
require_log 'dxg_lock2_detail=ioctls:[1-9][0-9]* forwarded:[1-9][0-9]* .*last_len:[1-9][0-9]* .*last_status:0x0' \
    "LOCK2 host-forward diagnostics after kernel rewrite"
require_log 'dxg_unlock2_detail=ioctls:[1-9][0-9]* forwarded:[1-9][0-9]* .*last_len:[1-9][0-9]* .*last_status:0x0' \
    "UNLOCK2 host-forward diagnostics after kernel rewrite"
require_log 'dxg_closeadapter_order=.*close:[1-9][0-9]* last_destroy:[1-9][0-9]* .*after_destroy:[1-9][0-9]*' \
    "close-adapter cleanup ordering diagnostics"
if [[ "${REQUIRE_WDDM_TRACE_COMPARE}" != "0" ]]; then
    require_log 'wddm_trace_compare ok .*cleanup_mask=0x[1-9a-fA-F][0-9a-fA-F]*' \
        "same-adapter WSL trace versus kernel host-seen WDDM comparison"
fi
require_d3d12_shared_surface_contract
require_log 'd3d12sharedsmoke: shared resource=.*allocations=[1-9][0-9]*' \
    "D3D12 shared-resource export/query smoke"
require_log 'd3d12sharedsmoke: direct open shared resource ok .*allocations=[1-9][0-9]* total_priv=[1-9][0-9]* .*copied_priv=1' \
    "D3D12 direct shared-resource open/private metadata smoke"
require_log 'd3d12sharedsmoke: shared fence=.*cpu=0x[1-9a-fA-F]' \
    "D3D12 shared-fence export smoke"
require_log 'd3d12sharedsmoke: using gpu-manager-v5 fence-value path' \
    "D3D12 Wayland adapter-LUID/fence-value protocol path"
require_log 'wlcomp: d3d12 shared buffer fence target=1 current=1' \
    "D3D12 Wayland monitored-fence value import"
require_log 'd3d12sharedsmoke: mismatched adapter LUID rejected' \
    "D3D12 Wayland adapter-LUID mismatch rejection"
require_log 'd3d12sharedsmoke: committed D3D12 shared resource buffer' \
    "D3D12 Wayland shared-resource buffer commit"
require_log 'd3d12sharedsmoke: runtime shared resource fd=[0-9]+ desc=256x256 fmt=[0-9]+ fence_fd=[0-9]+ completed=1' \
    "D3D12 runtime shared-resource export/open and fence signal"
require_log 'd3d12sharedsmoke: runtime query adapter=0x[1-9a-fA-F][0-9a-fA-F]* allocations=[1-9][0-9]* total_priv=[1-9][0-9]*' \
    "D3D12 runtime shared-resource metadata query"
require_log 'd3d12sharedsmoke: present validation ok frame=1 release=1 gpu_present=[0-9]+->[1-9][0-9]*' \
    "D3D12 native-present/shared-surface precondition validation"
require_log 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff .*display_bind_backend=gpup_dxg_scanout_bind .*display_bind_transport=gpu-p-dxg-resource-scanout-bind .*display_bind_transport_source=non_wsl_linux_dxgkrnl_extension .*host_saw_display_bind_packet=1 .*wsl_presenthistory_completion_credit=0 .*display_bind_present_id=[1-9][0-9]* .*display_bind_completed_id=[1-9][0-9]* .*display_bind_resource_generation=[1-9][0-9]* .*display_bind_completion_source=display .*present_id=[1-9][0-9]* completed=[1-9][0-9]* .*mtime_ms=[1-9][0-9]* min_mtime_ms=[1-9][0-9]* .*starts=[1-9][0-9]* copy=[1-9][0-9]* completes=[1-9][0-9]* .*resource=0x[1-9a-fA-F][0-9a-fA-F]* allocations=[1-9][0-9]* fence=0x[1-9a-fA-F][0-9a-fA-F]* target=1 release=[1-9][0-9]* .*source_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8}) matched_luid=\1:\2 no_cpu_readback=1' \
    "D3D12 pure-C native-present evidence contract"
require_log 'd3d12_runtime_created_d3d12_resource_required[ =]1' \
    "strict-present requires runtime-created D3D12 resource"
require_log '(^|[[:space:]])(d3d12_)?existing_sysmem_is_d3d12_com_resource[ =]0' \
    "strict-present rejects existing-sysmem as a D3D12 COM resource"
require_log 'wlcomp: d3d12 shared buffer .*source_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8}).*matched_luid=\1:\2.*opened_fence=' \
    "compositor D3D12 resource/fence same-adapter import"
require_log 'd3d12_gpu_present_complete[s]?=[1-9][0-9]*' \
    "compositor D3D12 native GPU present evidence"
require_log 'dxg_sharedresource_lifetime=seals:[1-9][0-9]* .*open_tracked:[1-9][0-9]*' \
    "DXG shared-resource lifetime diagnostics"
require_log 'dxg_sharedhandle_last=.*ret:0' "DXG shared-handle success diagnostics"
require_log 'change_vidmem_reservation unsupported' "DXG vidmem-reservation unsupported marker"
require_log 'mark_device_error unsupported' "DXG mark-device-error unsupported marker"
require_log 'hwqueue_signal_sync unsupported' "DXG hwqueue signal unsupported marker"
require_log 'hwqueue_wait_sync unsupported' "DXG hwqueue wait unsupported marker"
require_log 'update_alloc_property unsupported' "DXG update-allocation-property unsupported marker"
require_log 'query_clock_calibration unsupported' "DXG clock-calibration unsupported marker"
require_log 'enum_processes unsupported' "DXG enum-processes unsupported marker"
require_log 'dxg_unsupported_last=.*ret:-95' "DXG misc unsupported diagnostics"
require_log 'sync_file_matrix .*create_rc=0 .*sync_file=[1-9][0-9]* .*open_rc=0 .*open_sync=0x[1-9a-fA-F][0-9a-fA-F]* .*child_status=0 .*child_rc=0' \
    "DXG sync-file create/open/child-open matrix"
require_log 'dxg_syncfile_create_unwind_matrix create_fault_rc=-[0-9]+ .*fd_visible=0 .*balanced=1 .*status=PASS' \
    "DXG sync-file create copyout-failure unwind matrix"
require_log 'dxg_syncfile_open_unwind_matrix open_fault_rc=-[0-9]+ .*destroy_ret=0 .*source_fd_valid=1 .*no_local_leak=1 .*status=PASS' \
    "DXG sync-file open copyout-failure unwind matrix"
require_log 'dxg_syncfile_lifetime=.*create_faults:[1-9][0-9]* .*fd_reclaimed:[1-9][0-9]* .*open_faults:[1-9][0-9]* .*open_destroy:[1-9][0-9]*/[1-9][0-9]*/0' \
    "DXG sync-file lifetime cleanup diagnostics"
if [[ "${REQUIRE_BOOT_GLSMOKE}" != "0" ]]; then
    require_log 'glsmoke pid=[0-9]+ exited=1 status=0' \
        "boot-time Mesa Wayland EGL smoke completion"
fi

if grep -Eq 'backend_opengl_submit 1' "${LOG}"; then
    require_no_premature_opengl_submit_claim
fi
if grep -Eiq 'mesaglfeature: EGL .*renderer=.*(softpipe|llvmpipe)|mesaglfeature: required D3D12 renderer but got' "${LOG}"; then
    fail "mesaglfeature used a software renderer in the D3D12 validation lane"
fi
require_no_fatal_dxg_log_signatures
require_no_d3d12_cpu_or_partial_present_path
require_no_guest_exec_failures
require_no_malformed_validator_markers

echo "hyperv-dxg-validate: passed (${LOG})" | tee -a "${LOG}"
