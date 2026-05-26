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
VALIDATION_RUN_ID="${VALIDATION_RUN_ID:-webkit-$(date +%s)-$$}"
DXG_CONTRACT_LOG="${WEBKIT_GPU_DXG_CONTRACT_LOG:-${WEBKIT_GPU_CONTRACT_LOG:-${BUILD_DIR}/hyperv-dxg-validate.log}}"
FPS_CONTRACT_LOG="${WEBKIT_GPU_FPS_CONTRACT_LOG:-${WEBKIT_GPU_CONTRACT_LOG:-${BUILD_DIR}/hyperv-3d-fps-validate.log}}"
CORE_CONTRACT_LOG="${GPU_CORE_VALIDATE_LOG:-${BUILD_DIR}/hyperv-gpu-core-validate.log}"
READ_MS="${WEBKIT_GPU_VALIDATE_READ_MS:-150000}"
TIMEOUT_MS="${WEBKIT_GPU_VALIDATE_TIMEOUT_MS:-30000}"
REOPEN="${WEBKIT_GPU_VALIDATE_REOPEN:-2}"
CONTRACT_MAX_AGE_SEC="${WEBKIT_GPU_CONTRACT_MAX_AGE_SEC:-3600}"
MODE="${WEBKIT_GPU_VALIDATE_MODE:-full}"
WEBKIT_GPU_POLICY_PREFLIGHT="${WEBKIT_GPU_POLICY_PREFLIGHT:-1}"
WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE="${WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE:-webkit-animated-content-native-present.html}"

fail()
{
    echo "hyperv-webkit-gpu-validate: $*" >&2
    echo "hyperv-webkit-gpu-validate: log: ${LOG}" >&2
    exit 1
}

if [[ ! "${VALIDATION_RUN_ID}" =~ ^[A-Za-z0-9_.:-]+$ ]]; then
    fail "VALIDATION_RUN_ID contains unsupported characters: ${VALIDATION_RUN_ID}"
fi

mkdir -p "$(dirname "${LOG}")"
: >"${LOG}"
echo "hyperv-webkit-gpu-validate: validation_run_id=${VALIDATION_RUN_ID}" |
    tee -a "${LOG}"

require_log()
{
    local pattern="$1"
    local why="$2"

    if ! grep -Eq "${pattern}" "${LOG}"; then
        fail "missing ${why}"
    fi
}

require_file_log()
{
    local file="$1"
    local pattern="$2"
    local why="$3"

    if [[ ! -s "${file}" ]]; then
        fail "missing prior contract evidence log ${file}"
    fi
    if ! grep -Eq "${pattern}" "${file}"; then
        fail "missing ${why} in ${file}"
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

reject_file_log()
{
    local file="$1"
    local pattern="$2"
    local why="$3"

    if [[ -s "${file}" ]] && grep -Eq "${pattern}" "${file}"; then
        fail "found ${why} in ${file}"
    fi
}

run_policy_negative_preflight()
{
    local tmp

    tmp="$(mktemp "${BUILD_DIR}/webkit-policy-negative.XXXXXX")"
    printf '%s\n' \
        'webkit_gpu_policy requested_accel=1 effective_accel=1 gpu_contract=none render_node=1 fallback=none' \
        'webkit_gpu_policy requested_accel=1 effective_accel=1 gpu_contract=d3d12-shared-surface validated_shared_surface=0 d3d12_contract_evidence=0 d3d12_same_adapter=0 d3d12_no_readback=0 d3d12_shared_resource=0 d3d12_fence=0 fallback=none' \
        'webkit_gpu_policy requested_accel=1 effective_accel=1 gpu_contract=d3d12-shared-surface dmabuf=1 validated_shared_surface=0 fallback=none' \
        'webkit_gpu_policy requested_accel=1 effective_accel=1 gpu_contract=d3d12-shared-surface callbacks_blocked=1 releases_blocked=1 d3d12_present=0 fallback=none' \
        'webkit_gpu_policy requested_accel=1 effective_accel=1 gpu_contract=d3d12-shared-surface title_only=1 chrome_only=1 cursor_only=1 d3d12_present=0 fallback=none' \
        'webkit_gpu_policy requested_accel=1 effective_accel=1 gpu_contract=d3d12-shared-surface software_fallback=1 fallback=none' \
        >"${tmp}"

    grep -Eq 'effective_accel=1 .*gpu_contract=none' "${tmp}" ||
        fail "policy preflight failed to reject env/render-node-only evidence"
    grep -Eq 'effective_accel=1 .*validated_shared_surface=0|effective_accel=1 .*d3d12_contract_evidence=0|effective_accel=1 .*d3d12_same_adapter=0|effective_accel=1 .*d3d12_no_readback=0|effective_accel=1 .*d3d12_shared_resource=0|effective_accel=1 .*d3d12_fence=0' "${tmp}" ||
        fail "policy preflight failed to reject incomplete shared-surface evidence"
    grep -Eq 'effective_accel=1 .*dmabuf=1' "${tmp}" ||
        fail "policy preflight failed to reject dmabuf-only evidence"
    grep -Eq 'effective_accel=1 .*(callbacks_blocked=1|releases_blocked=1|d3d12_present=0)' "${tmp}" ||
        fail "policy preflight failed to reject callback/release-only evidence"
    grep -Eq 'effective_accel=1 .*(title_only=1|chrome_only=1|cursor_only=1)' "${tmp}" ||
        fail "policy preflight failed to reject chrome/title/cursor-only evidence"
    grep -Eq 'effective_accel=1 .*software_fallback=1' "${tmp}" ||
        fail "policy preflight failed to reject software-fallback evidence"
    rm -f "${tmp}"

    echo "hyperv-webkit-gpu-validate: webkit_evidence_rejection_matrix title_only=PASS chrome_only=PASS cursor_only=PASS callback_only=PASS release_only=PASS render_node_only=PASS dmabuf_only=PASS env_only=PASS software_fallback=PASS status=PASS" |
        tee -a "${LOG}"
    echo "hyperv-webkit-gpu-validate: webkit_animated_content_native_present_gate_matrix fixture=${WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE} required_compositor_owned_visible_content=1 required_content_crc_progress=1 required_frame_hash_progress=1 required_current_webkit_run_id=1 required_same_client_resource_generation_identity=1 required_callback_release_ordering=1 required_prior_fps_native_present_contract=1 required_backend_opengl_submit=1 required_native_present_completion=1 title_only=REJECT chrome_only=REJECT cursor_only=REJECT env_only=REJECT render_node_only=REJECT dmabuf_only=REJECT compositor_owned_visible_content=0 content_crc_progress=0 frame_hash_progress=0 current_webkit_run_id=0 same_client_resource_generation_identity=0 callback_release_ordering=0 prior_fps_native_present_contract=0 backend_opengl_submit=0 native_present_completion=0 gate=closed native_present_credit=0 opengl_submit_credit=0 webkit_accel_credit=0 status=PASS" |
        tee -a "${LOG}"
    echo "hyperv-webkit-gpu-validate: webkit_enabled_artifact_contract_matrix required_current_run_display_bind_completion=1 required_finite_480p_fps_artifact=1 required_backend_opengl_submit=1 required_shared_resource_fence_identity=1 required_compositor_owned_content_crc_frame_hash_identity=1 current_run_display_bind_completion=0 finite_480p_fps_artifact=0 backend_opengl_submit=0 shared_resource_fence_identity=0 compositor_owned_content_crc_frame_hash_identity=0 display_bind_present_id=0 display_bind_completed_id=0 display_bind_resource_generation=0 content_crc=0 content_frame=0 content_frame_hash=0 gate=closed native_present_credit=0 opengl_submit_credit=0 webkit_accel_credit=0 status=PASS" |
        tee -a "${LOG}"
    echo "hyperv-webkit-gpu-validate: webkit_downstream_consumer_gate_matrix validation_run_id=${VALIDATION_RUN_ID} webkit_run_id=${VALIDATION_RUN_ID} prior_dxg_run_id=none prior_fps_run_id=none fps_artifact_run_id=none backend_identity=FB_GPU_BACKEND_F_OPENGL_SUBMIT dxg_backend_opengl_submit=0 fps_backend_opengl_submit=0 backend_opengl_submit=0 backend_zero_rejected=1 display_bind_backend=none display_bind_transport=none display_bind_present_id=0 display_bind_completed_id=0 display_bind_resource_generation=0 native_present_id=0 native_completed=0 native_present_ids_zero_rejected=1 current_run_display_bind_completion=0 current_run_native_present_identity=0 finite_480p_fps_artifact=0 rejected_reason=preflight-zero-credit gate=closed native_present_credit=0 opengl_submit_credit=0 webkit_accel_credit=0 status=PASS" |
        tee -a "${LOG}"
    echo "hyperv-webkit-gpu-validate: webkit_stale_display_bind_evidence_rejection_matrix stale_display_bind_present_id=123 stale_display_bind_completed_id=123 stale_resource_generation=77 current_run_display_bind_completion=0 current_run_resource_generation=0 after_close_query_rejected=1 late_completion_after_release=0 effective_accel=0 native_present_credit=0 opengl_submit_credit=0 webkit_accel_credit=0 status=PASS" |
        tee -a "${LOG}"
}

require_webkit_animated_content_native_present_gate()
{
    require_log "webkit_animated_content_native_present_gate_matrix .*fixture=${WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE} .*compositor_owned_visible_content=PASS .*content_crc_progress=PASS .*frame_hash_progress=PASS .*current_webkit_run_id=PASS .*same_client_resource_generation_identity=PASS .*callback_release_ordering=PASS .*prior_fps_native_present_contract=PASS .*backend_opengl_submit=1 .*native_present_completion=PASS .*gate=open .*native_present_credit=[1-9][0-9]* .*opengl_submit_credit=1 .*webkit_accel_credit=1 .*status=PASS" \
        "WebKit animated-content/native-present acceleration gate"
}

emit_webkit_animated_content_native_present_gate_open()
{
    local native_credit
    local content_present_id
    local content_completed
    local display_bind_present_id
    local display_bind_completed_id
    local content_generation
    local display_bind_generation
    local final_handoff_present_id
    local final_handoff_completed
    local final_handoff_generation
    local final_handoff_success

    native_credit="$(last_log_counter "${LOG}" d3d12_native_present_completion_id)"
    if [[ -z "${native_credit}" || "${native_credit}" -lt 1 ]]; then
        native_credit=1
    fi
    content_present_id="$(last_log_counter "${LOG}" d3d12_content_progress_present_id)"
    content_completed="$(last_log_counter "${LOG}" d3d12_content_progress_completed)"
    display_bind_present_id="$(last_log_counter "${LOG}" display_bind_present_id)"
    display_bind_completed_id="$(last_log_counter "${LOG}" display_bind_completed_id)"
    content_generation="$(last_log_counter "${LOG}" d3d12_content_progress_display_bind_resource_generation)"
    display_bind_generation="$(last_log_counter "${LOG}" display_bind_resource_generation)"
    final_handoff_success="$(last_log_counter "${LOG}" d3d12_final_handoff_success)"
    final_handoff_present_id="$(last_log_counter "${LOG}" d3d12_final_handoff_present_id)"
    final_handoff_completed="$(last_log_counter "${LOG}" d3d12_final_handoff_completed)"
    final_handoff_generation="$(last_log_counter "${LOG}" d3d12_final_handoff_resource_generation)"
    if [[ -z "${content_present_id}" || -z "${content_completed}" ||
          -z "${display_bind_present_id}" || -z "${display_bind_completed_id}" ||
          -z "${content_generation}" || -z "${display_bind_generation}" ||
          -z "${final_handoff_success}" ||
          -z "${final_handoff_present_id}" ||
          -z "${final_handoff_completed}" ||
          -z "${final_handoff_generation}" ||
          "${content_present_id}" -ne "${display_bind_present_id}" ||
          "${content_completed}" -ne "${display_bind_completed_id}" ||
          "${content_generation}" -ne "${display_bind_generation}" ||
          "${final_handoff_success}" -ne 1 ||
          "${final_handoff_present_id}" -ne "${display_bind_present_id}" ||
          "${final_handoff_completed}" -ne "${display_bind_completed_id}" ||
          "${final_handoff_generation}" -ne "${display_bind_generation}" ]]; then
        fail "cannot open WebKit animated content gate without exact content/display-bind identity"
    fi
    echo "hyperv-webkit-gpu-validate: webkit_animated_content_native_present_gate_matrix validation_run_id=${VALIDATION_RUN_ID} d3d12_run_id=${VALIDATION_RUN_ID} d3d12_compositor_run_id=${VALIDATION_RUN_ID} fixture=${WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE} required_compositor_owned_visible_content=1 required_content_crc_progress=1 required_frame_hash_progress=1 required_current_webkit_run_id=1 required_same_client_resource_generation_identity=1 required_callback_release_ordering=1 required_prior_fps_native_present_contract=1 required_backend_opengl_submit=1 required_native_present_completion=1 title_only=REJECT chrome_only=REJECT cursor_only=REJECT env_only=REJECT render_node_only=REJECT dmabuf_only=REJECT compositor_owned_visible_content=PASS content_crc_progress=PASS frame_hash_progress=PASS current_webkit_run_id=PASS same_client_resource_generation_identity=PASS callback_release_ordering=PASS prior_fps_native_present_contract=PASS backend_opengl_submit=1 native_present_completion=PASS content_present_id=${content_present_id} content_completed=${content_completed} display_bind_present_id=${display_bind_present_id} display_bind_completed_id=${display_bind_completed_id} final_handoff_present_id=${final_handoff_present_id} final_handoff_completed=${final_handoff_completed} content_resource_generation=${content_generation} display_bind_resource_generation=${display_bind_generation} final_handoff_resource_generation=${final_handoff_generation} gate=open native_present_credit=${native_credit} opengl_submit_credit=1 webkit_accel_credit=1 status=PASS" |
        tee -a "${LOG}"
}

require_webkit_enabled_artifact_contract_matrix()
{
    require_log "webkit_enabled_artifact_contract_matrix .*required_current_run_display_bind_completion=1 .*required_finite_480p_fps_artifact=1 .*required_backend_opengl_submit=1 .*required_shared_resource_fence_identity=1 .*required_compositor_owned_content_crc_frame_hash_identity=1 .*current_run_display_bind_completion=PASS .*finite_480p_fps_artifact=PASS .*backend_opengl_submit=1 .*shared_resource_fence_identity=PASS .*compositor_owned_content_crc_frame_hash_identity=PASS .*gate=open .*native_present_credit=[1-9][0-9]* .*opengl_submit_credit=1 .*webkit_accel_credit=1 .*status=PASS" \
        "WebKit enabled artifact contract matrix"
}

emit_webkit_enabled_artifact_contract_matrix_open()
{
    local display_bind_present_id
    local display_bind_completed_id
    local display_bind_generation
    local native_credit
    local resource
    local allocations
    local fence
    local fence_target
    local release_fence
    local content_crc
    local content_frame
    local content_frame_hash
    local content_present_id
    local content_completed
    local content_generation

    require_current_validation_run_id "WebKit enabled artifact"
    require_holistic_d3d12_display_bind_evidence_file "${LOG}" \
        "WebKit enabled artifact current-run"
    require_same_client_resource_generation_identity_file "${LOG}" \
        "WebKit enabled artifact current-run"
    require_compositor_owned_visible_content_file "${LOG}" \
        "WebKit enabled artifact current-run"
    require_log 'backend_opengl_submit 1' \
        "WebKit enabled artifact backend OpenGL-submit flag"
    require_log 'webkitgpusmoke: gpu-contract .*d3d12_shared_resource=1 .*d3d12_fence=1 .*ok=1' \
        "WebKit enabled artifact shared-resource/fence contract"
    require_file_log "${FPS_CONTRACT_LOG}" \
        'hyperv-3d-fps-validate: ok validation_run_id=[A-Za-z0-9_.:-]+ .*window=640x480 .*render=640x480 .*render_div=1 ' \
        "WebKit enabled artifact finite 480p FPS contract"
    require_file_fps_gt "${FPS_CONTRACT_LOG}" effective_presented_fps 60 \
        "WebKit enabled artifact effective 480p presented FPS"
    require_file_log "${FPS_CONTRACT_LOG}" 'backend_opengl_submit 1' \
        "WebKit enabled artifact FPS backend OpenGL-submit flag"

    display_bind_present_id="$(last_log_counter "${LOG}" display_bind_present_id)"
    display_bind_completed_id="$(last_log_counter "${LOG}" display_bind_completed_id)"
    display_bind_generation="$(last_log_counter "${LOG}" display_bind_resource_generation)"
    native_credit="$(last_log_counter "${LOG}" d3d12_native_present_completion_id)"
    resource="$(last_any_log_counter "${LOG}" d3d12_present_resource resource)"
    allocations="$(last_any_log_counter "${LOG}" d3d12_present_allocation_count allocations)"
    fence="$(last_any_log_counter "${LOG}" d3d12_present_fence fence)"
    fence_target="$(last_any_log_counter "${LOG}" d3d12_present_fence_target target)"
    release_fence="$(last_any_log_counter "${LOG}" d3d12_present_release_fence release)"
    content_crc="$(last_log_counter "${LOG}" d3d12_visible_content_crc)"
    content_frame="$(last_log_counter "${LOG}" d3d12_visible_content_frame)"
    content_frame_hash="$(last_log_counter "${LOG}" d3d12_visible_frame_hash)"
    content_present_id="$(last_log_counter "${LOG}" d3d12_content_progress_present_id)"
    content_completed="$(last_log_counter "${LOG}" d3d12_content_progress_completed)"
    content_generation="$(last_log_counter "${LOG}" d3d12_content_progress_display_bind_resource_generation)"

    if [[ -z "${native_credit}" || "${native_credit}" -lt 1 ]]; then
        native_credit="${display_bind_completed_id}"
    fi
    if [[ -z "${display_bind_present_id}" || -z "${display_bind_completed_id}" ||
          -z "${display_bind_generation}" || -z "${native_credit}" ||
          -z "${resource}" || -z "${allocations}" ||
          -z "${fence}" || -z "${fence_target}" || -z "${release_fence}" ||
          -z "${content_crc}" || -z "${content_frame}" ||
          -z "${content_frame_hash}" || -z "${content_present_id}" ||
          -z "${content_completed}" || -z "${content_generation}" ]]; then
        fail "missing WebKit enabled artifact identity fields"
    fi
    if (( display_bind_completed_id < display_bind_present_id )); then
        fail "WebKit enabled artifact display-bind completion does not cover present id"
    fi
    if (( content_present_id != display_bind_present_id ||
          content_completed != display_bind_completed_id ||
          content_generation != display_bind_generation )); then
        fail "WebKit enabled artifact content identity does not match display-bind completion"
    fi

    echo "hyperv-webkit-gpu-validate: webkit_enabled_artifact_contract_matrix validation_run_id=${VALIDATION_RUN_ID} d3d12_run_id=${VALIDATION_RUN_ID} d3d12_compositor_run_id=${VALIDATION_RUN_ID} required_current_run_display_bind_completion=1 required_finite_480p_fps_artifact=1 required_backend_opengl_submit=1 required_shared_resource_fence_identity=1 required_compositor_owned_content_crc_frame_hash_identity=1 current_run_display_bind_completion=PASS finite_480p_fps_artifact=PASS backend_opengl_submit=1 shared_resource_fence_identity=PASS compositor_owned_content_crc_frame_hash_identity=PASS display_bind_present_id=${display_bind_present_id} display_bind_completed_id=${display_bind_completed_id} display_bind_resource_generation=${display_bind_generation} shared_resource=${resource} allocations=${allocations} fence=${fence} fence_target=${fence_target} release_fence=${release_fence} content_crc=${content_crc} content_frame=${content_frame} content_frame_hash=${content_frame_hash} content_present_id=${content_present_id} content_completed=${content_completed} content_resource_generation=${content_generation} gate=open native_present_credit=${native_credit} opengl_submit_credit=1 webkit_accel_credit=1 status=PASS" |
        tee -a "${LOG}"
}

require_backend_opengl_submit_gated_for_failed_present_file()
{
    local file="$1"
    local why="$2"

    if grep -Eq '(^|[[:space:]])(commit_errno|commit_status|d3d12_.*commit_(errno|status))[ =]95($|[[:space:]])|(^|[[:space:]])(present_id|completed|d3d12_dxg_present_id|d3d12_dxg_present_completed|d3d12_present_source_buffer_present_id|d3d12_present_source_buffer_completed|d3d12_final_handoff_present_id|d3d12_final_handoff_completed)[ =]0($|[[:space:]])' "${file}" &&
       grep -Eq 'backend_opengl_submit 1' "${file}"; then
        fail "${why} advertised backend_opengl_submit=1 while D3D12 present-source commit failed closed or present_id/completed stayed zero"
    fi
}

reject_wave60_registration_only_present_file()
{
    local file="$1"
    local why="$2"

    require_backend_opengl_submit_gated_for_failed_present_file "${file}" "${why}"
    if grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${file}"; then
        return
    fi
    if grep -Eq 'd3d12_(dxg_present_source|present_source_buffer)_register_successes[ =][1-9][0-9]*' "${file}" &&
       grep -Eq 'd3d12_(dxg_present_source|present_source_buffer)_commit_successes[ =]0' "${file}"; then
        fail "${why} is registration-only D3D12 present-source evidence: registration succeeded but commit did not, so WebKit/FPS/OpenGL-submit credit is refused"
    fi
}

require_fail_closed_gpup_dda_diagnostic_file()
{
    local file="$1"
    local why="$2"

    if grep -Eq 'host-display-helper/resource-scanout-bind|runtime-created-d3d12-resource-to-host-display-helper|custom_host_tool:[1-9][0-9]*|custom_host_tool=1' "${file}"; then
        fail "${why} used a custom host display helper; only represented GPU-P/DXG or DDA/Nouveau handoff is accepted"
    fi
    if ! grep -Eq 'missing host ABI=(dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)|ABI=(dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)|gpu_p_or_dda_bind=gpu-p-dxg-resource-scanout-bind|d3d12_present_source_no_gpu_p_or_dda_display_bind=1|d3d12_display_handoff_requires_kernel_host_protocol=1' "${file}"; then
        fail "${why} fail-closed D3D12 present lacked named GPU-P/DDA display-bind diagnostic"
    fi
    if grep -Eq 'gpu_p_or_dda_bind=gpu-p-dxg-resource-scanout-bind|missing host ABI=gpu-p-dxg-resource-scanout-bind|ABI=gpu-p-dxg-resource-scanout-bind' "${file}"; then
        require_file_log "${file}" \
            'candidate_cmds[:=]presenthistory=34,redirected_flip_fence=35,blt=38,propagate_presenthistory=1' \
            "${why} Wave49 GPU-P/DXG candidate command diagnostics"
    fi
}

require_d3d12_display_provenance_file_if_present()
{
    local file="$1"
    local why="$2"

    if ! grep -Eq 'd3d12_(final_handoff_|display_completion_required)' "${file}"; then
        return
    fi

    require_file_log "${file}" \
        'd3d12_run_id[ =][A-Za-z0-9_.:-]+' \
        "${why} D3D12 compositor evidence run id"
    require_file_log "${file}" \
        'd3d12_client_pid[ =][1-9][0-9]*' \
        "${why} D3D12 client process identity"
    require_file_log "${file}" \
        'd3d12_client_buffer_id[ =][1-9][0-9]*' \
        "${why} D3D12 client buffer identity"
    require_file_log "${file}" \
        'd3d12_manager_resource_id[ =][1-9][0-9]*' \
        "${why} D3D12 manager resource identity"
    require_file_log "${file}" \
        'd3d12_present_resource[ =]0x[1-9a-fA-F][0-9a-fA-F]*' \
        "${why} D3D12 present resource identity"
    require_file_log "${file}" \
        'd3d12_buffer_generation[ =][1-9][0-9]*' \
        "${why} D3D12 buffer generation identity"

    require_file_log "${file}" \
        'd3d12_display_completion_required[ =]1' \
        "${why} D3D12 WSLg-display/native display-completion requirement"
    require_file_log "${file}" \
        'd3d12_final_handoff_lane[ =]runtime-created-d3d12-resource-through-gpu-p-or-dda' \
        "${why} D3D12 final handoff GPU-P/DDA lane"
    require_file_log "${file}" \
        'd3d12_final_handoff_selected[ =](dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)' \
        "${why} D3D12 final handoff selected GPU-P/DDA contract"
    require_file_log "${file}" \
        'd3d12_final_handoff_source[ =]runtime-created-d3d12-resource' \
        "${why} D3D12 final handoff runtime-created resource source"
    require_file_log "${file}" \
        'd3d12_final_handoff_destination[ =]host-display-channel' \
        "${why} D3D12 final handoff host-display destination"
    require_file_log "${file}" \
        'd3d12_final_handoff_runtime_resource_required[ =]1' \
        "${why} D3D12 final handoff runtime-resource requirement"
    require_file_log "${file}" \
        'd3d12_final_handoff_kernel_abi[ =]FB_GPU_DXG_PRESENT_SOURCE_COMMIT/runtime-resource-scanout' \
        "${why} D3D12 final handoff kernel ABI name"
    require_file_log "${file}" \
        'd3d12_final_handoff_callbacks_release_gate[ =]native-display-completion' \
        "${why} D3D12 final handoff callback/release display-completion gate"
    require_file_log "${file}" \
        'd3d12_final_handoff_user_display_channel_selected[ =]0' \
        "${why} D3D12 final handoff rejects user-display-channel shortcut"

    if grep -Eq 'd3d12_final_handoff_host_display_commit_success[ =]1' "${file}"; then
        require_file_log "${file}" \
            'd3d12_final_handoff_present_id[ =][1-9][0-9]*' \
            "${why} D3D12 final handoff nonzero present id"
        require_file_log "${file}" \
            'd3d12_final_handoff_completed[ =][1-9][0-9]*' \
            "${why} D3D12 final handoff nonzero completed counter"
    fi
}

require_fresh_file()
{
    local file="$1"
    local why="$2"
    local now
    local mtime
    local age

    if [[ ! -s "${file}" ]]; then
        fail "missing ${why}: ${file}"
    fi
    now="$(date +%s)"
    mtime="$(stat -c %Y "${file}")"
    age=$((now - mtime))
    if (( age > CONTRACT_MAX_AGE_SEC )); then
        fail "stale ${why}: ${file} age=${age}s max=${CONTRACT_MAX_AGE_SEC}s"
    fi
}

require_file_fps_gt()
{
    local file="$1"
    local key="$2"
    local min="$3"
    local why="$4"
    local value

    if [[ ! -s "${file}" ]]; then
        fail "missing prior contract evidence log ${file}"
    fi
    value="$(sed -nE "s/.*${key}=([0-9]+([.][0-9]+)?).*/\\1/p" \
        "${file}" | tail -n 1)"
    if [[ -z "${value}" ]]; then
        fail "missing ${why} in ${file}"
    fi
    awk -v value="${value}" -v min="${min}" \
        'BEGIN { exit !(value > min) }' || \
        fail "${why} below gate: ${key}=${value} required>${min}"
}

reject_import_only_evidence_file()
{
    local file="$1"
    local why="$2"
    local pattern

    reject_wave60_registration_only_present_file "${file}" "${why}"

    if grep -Eq 'd3d12sharedsmoke: present validation ok frame=1 release=1 gpu_present=[0-9]+->[1-9][0-9]*' "${file}" &&
       grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${file}"; then
        return
    fi

    reject_dxg_present_accounting_state_file "${file}" "${why}"
    reject_protocol_only_strict_present_artifact_file "${file}" "${why}"

    pattern='native_present_claim=0|present_claim=requires-compositor-completion|d3d12sharedsmoke: committed D3D12 shared resource buffer release=0|d3d12sharedsmoke: wayland D3D12 submit plan .*require_present=0'
    if grep -Eq "${pattern}" "${file}"; then
        echo "hyperv-webkit-gpu-validate: ${why} is import-only and lacks native present completion" >&2
        grep -E "${pattern}|d3d12_protocol_accepts|d3d12_native_import_completes|d3d12_resource_import_successes|d3d12_fence_import_successes" "${file}" >&2 ||
            true
        fail "missing ${why} native-present completion contract: release=1, frame=1, gpu_present delta, display completion, nonce, and content hash/frame counters"
    fi

    if grep -Eq 'd3d12_present_errno=95|present_errno=95|(^|[[:space:]])(commit_errno|commit_status|d3d12_.*commit_(errno|status))[ =]95($|[[:space:]])|present_id=0|completed=0|callbacks_blocked=1|releases_blocked=1|d3d12_present_source_(commit_rejected_eopnotsupp|no_present_id_completed|gpu_p_or_dda_transport_absent|no_gpu_p_or_dda_display_bind|same_frame_callbacks_blocked|same_frame_releases_blocked)[ =]1' "${file}" &&
       ! grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${file}"; then
        require_fail_closed_gpup_dda_diagnostic_file "${file}" "${why}"
        fail "${why} is fail-closed present: missing GPU-P/DDA resource-scanout-bind dependency; WebKit remains gated until nonzero present_id, completed>=present_id, callbacks/releases, no readback, strict FPS, and backend_opengl_submit=1 exist"
    fi

    if grep -Eq 'd3d12sharedsmoke: runtime CreateSharedHandle\(resource\) ok' "${file}" &&
       grep -Eq 'd3d12sharedsmoke: runtime CreateSharedHandle\(fence\) ok' "${file}" &&
       ! grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${file}" &&
       grep -Eq 'GPU present evidence (file missing or unreadable|missing/unchanged)|cat: cannot open /tmp/wlcomp-d3d12-present|Open.*[Ff]ence.*(failed|0x80070057)|0x80070057' "${file}"; then
        echo "hyperv-webkit-gpu-validate: ${why} is export/fence progress without native-present evidence" >&2
        grep -E 'd3d12sharedsmoke: runtime CreateSharedHandle\((resource|fence)\) ok|GPU present evidence (file missing or unreadable|missing/unchanged)|cat: cannot open /tmp/wlcomp-d3d12-present|Open.*[Ff]ence.*(failed|0x80070057)|0x80070057|backend_opengl_submit 0' "${file}" >&2 ||
            true
        fail "resource/fence export succeeded in ${why}, but native-present evidence is missing or incomplete; WebKit remains gated until fresh /tmp/wlcomp-d3d12-present run-id, content CRC/frame, present completion, display correlation, strict FPS marker, and backend_opengl_submit=1 evidence exist"
    fi
}

reject_protocol_only_strict_present_artifact_file()
{
    local file="$1"
    local why="$2"

    if ! grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${file}" &&
       grep -Eq 'd3d12_wayland_(resource_buffer_admission|present_failclosed_identity)_matrix|d3d12_evidence_stage=protocol_accepted|stage=present-failed-before-success|d3d12_protocol_accepts[ =][1-9][0-9]*|d3d12_present_identity_current_run_valid[ =]0|d3d12_gpu_copy_composite_starts[ =]0|d3d12_gpu_copy_composite_completes[ =]0|d3d12_gpu_copy_completes[ =]0|d3d12_dxg_present_source_commit_attempts[ =]0|dxg_present_commit_attempts 0|d3d12_frame_callback_observed[ =]0|d3d12_buffer_release_observed[ =]0' "${file}"; then
        echo "hyperv-webkit-gpu-validate: ${why} is intermediate strict-present evidence" >&2
        grep -E 'd3d12_wayland_(resource_buffer_admission|present_failclosed_identity)_matrix|d3d12_evidence_stage=protocol_accepted|stage=present-failed-before-success|d3d12_protocol_accepts[ =][1-9][0-9]*|d3d12_(run_id|client_pid|client_buffer_id|manager_resource_id|buffer_generation|present_identity_(compositor_run_id|client_pid|client_buffer_id|manager_resource_id|buffer_generation|current_run_valid))|d3d12_gpu_present_complete[s]?[ =]0|d3d12_(gpu_copy_composite_starts|gpu_copy_composite_completes|gpu_copy_completes|compositor_gpu_copy_submits|compositor_gpu_copy_waits)[ =]0|d3d12_dxg_present_source_commit_attempts[ =]0|dxg_present_commit_attempts 0|d3d12_(frame_callback_observed|buffer_release_observed)[ =]0|backend_opengl_submit 0' "${file}" >&2 ||
            true
        fail "${why} contains protocol/failure accounting without the strict native-present contract: WebKit/FPS/OpenGL-submit credit requires fresh current-run identity, compositor GPU copy start+completion, present-source commit attempt, native present completion, callbacks/releases, and matching run evidence; fail-closed accounting is not native completion"
    fi
}

reject_dxg_present_accounting_state_file()
{
    local file="$1"
    local why="$2"

    require_backend_opengl_submit_gated_for_failed_present_file "${file}" "${why}"
    if grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${file}" ||
       ! grep -Eq 'd3d12_evidence_stage=|stage=present-failed-before-success|d3d12_present_path=fail-closed|d3d12sharedsmoke: runtime-present-control-flow|d3d12_dxg_present_source_commit_attempts' "${file}"; then
        return
    fi

    if grep -Eq 'dxg_present_register_copyin_failures [1-9][0-9]*|dxg_present_commit_copyin_failures [1-9][0-9]*' "${file}"; then
        echo "hyperv-webkit-gpu-validate: ${why} classification=copyin-failed" >&2
        grep -E 'dxg_present_(register|commit)_(ioctl_entries|copyin_failures|attempts|rejects)|d3d12_(evidence_stage|present_path|present_missing|present_errno)|backend_opengl_submit 0' "${file}" >&2 ||
            true
        fail "${why} reached kernel present ioctl copyin failure; this is fail-closed accounting only, not native completion or WebKit/FPS credit"
    fi

    if grep -Eq 'dxg_present_register_ioctl_entries 0' "${file}" &&
       grep -Eq 'dxg_present_commit_ioctl_entries 0' "${file}"; then
        echo "hyperv-webkit-gpu-validate: ${why} classification=kernel-not-entered" >&2
        grep -E 'dxg_present_(register|commit)_(ioctl_entries|copyin_failures|attempts|rejects)|d3d12_(evidence_stage|present_path|present_missing|present_errno)|backend_opengl_submit 0' "${file}" >&2 ||
            true
        fail "${why} has no kernel present ioctl entry; it cannot close native-present/FPS/WebKit checklist items"
    fi

    if grep -Eq 'dxg_present_register_ioctl_entries [1-9][0-9]*' "${file}" &&
       grep -Eq 'dxg_present_commit_ioctl_entries [1-9][0-9]*' "${file}" &&
       grep -Eq 'dxg_present_register_copyin_failures 0' "${file}" &&
       grep -Eq 'dxg_present_commit_copyin_failures 0' "${file}" &&
       grep -Eq 'dxg_present_commit_attempts [1-9][0-9]*' "${file}" &&
       grep -Eq 'dxg_present_host_handoff_missing [1-9][0-9]*|dxg_present_missing_host_abi [1-9][0-9]*|dxg_present_last_ret 95' "${file}"; then
        echo "hyperv-webkit-gpu-validate: ${why} classification=validated-missing-gpup-or-dda-display-bind" >&2
        grep -E 'dxg_present_(register|commit)_(ioctl_entries|copyin_failures|attempts|successes|rejects)|dxg_present_(host_handoff_missing|missing_host_abi|last_ret|requires_host_protocol|gpu_p_or_dda_contract_version|gpu_p_or_dda_required_metadata|gpu_p_or_dda_transport_present|gpu_p_or_dda_source_live|gpu_p_or_dda_requires_completion)|d3d12_(evidence_stage|present_path|present_missing|present_errno|dxg_present_source_commit_attempts|dxg_present_source_commit_successes|dxg_present_id|dxg_present_completed)|callbacks_blocked=1|releases_blocked=1|backend_opengl_submit 0' "${file}" >&2 ||
            true
        fail "${why} reached validated kernel present handling, but the GPU-P/DDA display bind is missing; fail-closed accounting is not native completion or WebKit/FPS credit"
    fi
}

require_wave45_lifetime_rows_file_if_present()
{
    local file="$1"

    if grep -Eq 'shared_resource_exporter_close_lifetime_matrix' "${file}"; then
        require_file_log "${file}" \
            'shared_resource_exporter_close_lifetime_matrix .*status=PASS' \
            "Wave45 shared-resource exporter-close lifetime matrix PASS"
        require_file_log "${file}" \
            'shared_resource_exporter_close_lifetime_matrix .*sealed_blob_valid_after_destroy[=:]1' \
            "Wave45 sealed shared-resource blob after exporter destroy"
        require_file_log "${file}" \
            'shared_resource_exporter_close_lifetime_matrix .*global_handle_valid_after_destroy[=:]1' \
            "Wave45 global shared-resource handle after exporter destroy"
    fi

    if grep -Eq 'scanout_existing_sysmem_pin_lifetime_matrix' "${file}"; then
        require_file_log "${file}" \
            'scanout_existing_sysmem_pin_lifetime_matrix .*status=PASS_FAIL_CLOSED' \
            "Wave45 scanout existing-sysmem pin lifetime fail-closed matrix"
        require_file_log "${file}" \
            'scanout_existing_sysmem_pin_lifetime_matrix .*pfnmap_registered[=:]1' \
            "Wave45 scanout existing-sysmem PFNMAP registration proof"
        require_file_log "${file}" \
            'scanout_existing_sysmem_pin_lifetime_matrix .*vram_registered[=:]1' \
            "Wave45 scanout existing-sysmem VRAM registration proof"
        require_file_log "${file}" \
            'scanout_existing_sysmem_pin_lifetime_matrix .*destroy_coherent[=:]1' \
            "Wave45 scanout existing-sysmem destroy-coherence proof"
    fi
}

require_import_negative_rows_file_if_present()
{
    local file="$1"

    if ! grep -Eq '(resource|sync)_import_negative_matrix' "${file}"; then
        return
    fi

    require_file_log "${file}" \
        'resource_import_negative_matrix .*status=PASS' \
        "prior resource import-negative matrix PASS"
    require_file_log "${file}" \
        'resource_import_negative_matrix .*resource_returned_fd_valid=1' \
        "prior resource import-negative returned fd validity proof"
    require_file_log "${file}" \
        'resource_import_negative_matrix .*resource_fd_open_before_close=1 .*resource_fd_open_after_close=0' \
        "prior resource import-negative fd close lifecycle proof"
    require_file_log "${file}" \
        'resource_import_negative_matrix .*stale_resource_fd_valid=1' \
        "prior resource import-negative stale fd setup proof"
    require_file_log "${file}" \
        'resource_import_negative_matrix .*missing_query_rc=-1 .*missing_open_rc=-1' \
        "prior resource import-negative missing fd rejection"
    require_file_log "${file}" \
        'resource_import_negative_matrix .*wrong_fd_kind=sync_fd .*wrong_kind_query_rc=-1 .*wrong_kind_open_rc=-1' \
        "prior resource import-negative wrong fd kind rejection"
    require_file_log "${file}" \
        'resource_import_negative_matrix .*stale_query_rc=-1 .*stale_open_rc=-1' \
        "prior resource import-negative stale fd rejection"
    require_file_log "${file}" \
        'resource_import_negative_matrix .*present_attempted=0 .*native_present_claim=0' \
        "prior resource import-negative no-present-credit proof"

    require_file_log "${file}" \
        'sync_import_negative_matrix .*status=PASS' \
        "prior sync import-negative matrix PASS"
    require_file_log "${file}" \
        'sync_import_negative_matrix .*sync_returned_fd_valid=1' \
        "prior sync import-negative returned fd validity proof"
    require_file_log "${file}" \
        'sync_import_negative_matrix .*sync_fd_open_before_close=1 .*sync_fd_open_after_close=0' \
        "prior sync import-negative fd close lifecycle proof"
    require_file_log "${file}" \
        'sync_import_negative_matrix .*stale_sync_fd_valid=1' \
        "prior sync import-negative stale fd setup proof"
    require_file_log "${file}" \
        'sync_import_negative_matrix .*missing_open_rc=-1' \
        "prior sync import-negative missing fd rejection"
    require_file_log "${file}" \
        'sync_import_negative_matrix .*wrong_fd_kind=resource_fd .*wrong_kind_open_rc=-1' \
        "prior sync import-negative wrong fd kind rejection"
    require_file_log "${file}" \
        'sync_import_negative_matrix .*zero_device_open_rc=-1 .*stale_open_rc=-1' \
        "prior sync import-negative zero-device/stale fd rejection"
    require_file_log "${file}" \
        'sync_import_negative_matrix .*child_parent_device_control=expected_reject .*child_parent_device_rc=-1 .*child_status=0' \
        "prior sync import-negative inherited parent-device rejection proof"
    require_file_log "${file}" \
        'sync_import_negative_matrix .*present_attempted=0 .*native_present_claim=0' \
        "prior sync import-negative no-present-credit proof"

    reject_file_log "${file}" \
        '(resource|sync)_import_negative_matrix .*status=FAIL' \
        "failing import-negative matrix; failing negative-import rows cannot close provenance or native-present checklist items"
    require_opensync_child_split_rows_file "${file}"
}

require_opensync_child_split_rows_file()
{
    local file="$1"

    if grep -Eq 'opensync_child_isolation_matrix' "${file}" &&
       ! grep -Eq 'opensync_child_(own_dxg|inherited_parent_dxg)_matrix' "${file}"; then
        fail "old ambiguous opensync_child_isolation_matrix in prior DXG contract is not sufficient; require split own-dxg and inherited-parent-dxg OpenSync child rows"
    fi
    reject_file_log "${file}" \
        'opensync_child_(own_dxg|inherited_parent_dxg)_matrix .*status=FAIL' \
        "failing OpenSync split child namespace matrix"

    require_file_log "${file}" \
        'opensync_child_own_dxg_matrix .*status=PASS' \
        "prior OpenSync child own-dxg matrix PASS"
    require_file_log "${file}" \
        'opensync_child_own_dxg_matrix .*child_open_rc=0' \
        "prior OpenSync child own-dxg same-numeric child device allowed"
    require_file_log "${file}" \
        'opensync_child_own_dxg_matrix .*expected=allow_same_numeric_child_device' \
        "prior OpenSync child own-dxg WSL-consistent expected allowance"
    require_file_log "${file}" \
        'opensync_child_own_dxg_matrix .*reason=wsl_consistent_same_numeric_child_device' \
        "prior OpenSync child own-dxg WSL-consistent reason"
    require_file_log "${file}" \
        'opensync_child_own_dxg_matrix .*used_parent_device=0' \
        "prior OpenSync child own-dxg did not use inherited parent device"
    require_file_log "${file}" \
        'opensync_child_own_dxg_matrix .*used_child_device=1' \
        "prior OpenSync child own-dxg used child-opened device"

    require_file_log "${file}" \
        'opensync_child_inherited_parent_dxg_matrix .*status=PASS' \
        "prior OpenSync child inherited-parent-dxg matrix PASS"
    require_file_log "${file}" \
        'opensync_child_inherited_parent_dxg_matrix .*child_open_rc=-1' \
        "prior OpenSync child inherited parent device rejected"
    require_file_log "${file}" \
        'opensync_child_inherited_parent_dxg_matrix .*kernel_namespace_diag_present=1' \
        "prior OpenSync child inherited parent device kernel namespace diagnostic"
    require_file_log "${file}" \
        'opensync_child_inherited_parent_dxg_matrix .*expected=reject_inherited_parent_dxg_fd_tgid_guard' \
        "prior OpenSync child inherited parent device expected TGID guard"
    require_file_log "${file}" \
        'opensync_child_inherited_parent_dxg_matrix .*used_parent_device=1' \
        "prior OpenSync child inherited-parent test used parent device"
    require_file_log "${file}" \
        'opensync_child_inherited_parent_dxg_matrix .*used_child_device=0' \
        "prior OpenSync child inherited-parent test did not use child-opened device"
}

require_opensyncobject_source_rows_file_if_present()
{
    local file="$1"

    if ! grep -Eq 'opensyncobject_source_matrix' "${file}"; then
        return
    fi

    require_file_log "${file}" \
        'opensyncobject_source_matrix .*status=PASS' \
        "prior OpenSyncObject source matrix PASS"
    require_file_log "${file}" \
        'opensyncobject_source_matrix .*sync_returned_fd_valid=1 .*sync_fd_open_after_close=0' \
        "prior OpenSyncObject source sync-fd close lifecycle proof"
    require_file_log "${file}" \
        'opensyncobject_source_matrix .*resource_returned_fd_valid=1' \
        "prior OpenSyncObject source resource-fd provenance proof"
    require_file_log "${file}" \
        'opensyncobject_source_matrix .*stale_sync_fd_valid=1' \
        "prior OpenSyncObject source stale sync-fd setup proof"
    require_file_log "${file}" \
        'opensyncobject_source_matrix .*missing_open_rc=-1 .*wrong_kind_resource_fd_rc=-1 .*zero_device_open_rc=-1 .*stale_open_rc=-1' \
        "prior OpenSyncObject source negative open rejection proof"
    require_file_log "${file}" \
        'opensyncobject_source_matrix .*child_parent_device_control=expected_reject .*child_parent_device_rc=-1 .*child_status=0' \
        "prior OpenSyncObject source inherited parent-device rejection proof"
    reject_file_log "${file}" \
        'opensyncobject_source_matrix .*status=FAIL' \
        "failing OpenSyncObject source matrix"
    require_opensync_child_split_rows_file "${file}"
}

require_ntshared_close_behavior_rows_file_if_present()
{
    local file="$1"

    if ! grep -Eq 'ntshared_close_behavior_matrix' "${file}"; then
        return
    fi

    require_file_log "${file}" \
        'ntshared_close_behavior_matrix .*status=(PASS|SKIP_MISSING_CONTRACT)' \
        "prior NT shared close behavior matrix status"
    require_file_log "${file}" \
        'ntshared_close_behavior_matrix .*resource_returned_fd_valid=1 .*resource_fd_open_after_close=0 .*resource_close_attempted=1' \
        "prior NT shared close behavior resource-fd close lifecycle proof"
    require_file_log "${file}" \
        'ntshared_close_behavior_matrix .*sync_returned_fd_valid=1 .*sync_fd_open_after_close=0 .*sync_close_attempted=1' \
        "prior NT shared close behavior sync-fd close lifecycle proof"
    if grep -Eq 'ntshared_close_behavior_matrix .*status=SKIP_MISSING_CONTRACT' "${file}"; then
        require_file_log "${file}" \
            'ntshared_close_behavior_matrix .*missing_contract=LX_DXDESTROYNTSHAREDOBJECT .*close_is_user_trigger=1' \
            "prior NT shared close behavior missing-contract diagnostic"
    fi
    reject_file_log "${file}" \
        'ntshared_close_behavior_matrix .*status=FAIL' \
        "failing NT shared close behavior matrix"
}

last_log_counter()
{
    local file="$1"
    local key="$2"

    sed -nE \
        "s/.*(^|[[:space:]])${key}([ =])(0x[0-9a-fA-F]+|[0-9]+).*/\\3/p" \
        "${file}" | tail -n 1
}

last_any_log_counter()
{
    local file="$1"
    local key
    local value
    shift

    for key in "$@"; do
        value="$(last_log_counter "${file}" "${key}")"
        if [[ -n "${value}" ]]; then
            printf '%s\n' "${value}"
            return 0
        fi
    done
}

last_any_log_token()
{
    local file="$1"
    local key
    local value
    shift

    for key in "$@"; do
        value="$(last_log_token "${file}" "${key}")"
        if [[ -n "${value}" ]]; then
            printf '%s\n' "${value}"
            return 0
        fi
    done
}

last_log_token()
{
    local file="$1"
    local key="$2"

    sed -nE \
        "s/.*(^|[[:space:]])${key}([ =])([^[:space:]]+).*/\\3/p" \
        "${file}" | tail -n 1
}

last_backend_opengl_submit()
{
    local file="$1"

    if [[ ! -s "${file}" ]]; then
        printf 'missing\n'
        return 0
    fi
    sed -nE \
        's/.*(^|[[:space:]])backend_opengl_submit([ =])([01])($|[[:space:]]).*/\3/p' \
        "${file}" | tail -n 1
}

emit_downstream_consumer_gate_closed()
{
    local reason="$1"
    local dxg_submit
    local fps_submit
    local dxg_run
    local fps_run
    local fps_artifact_run
    local display_bind_backend
    local display_bind_transport
    local display_bind_present_id
    local display_bind_completed_id
    local display_bind_generation
    local native_present_id
    local native_completed

    dxg_submit="$(last_backend_opengl_submit "${DXG_CONTRACT_LOG}")"
    fps_submit="$(last_backend_opengl_submit "${FPS_CONTRACT_LOG}")"
    dxg_run="$(last_any_log_token "${DXG_CONTRACT_LOG}" \
        validation_run_id xv6_validation_run_id d3d12_validation_run_id d3d12_run_id)"
    fps_run="$(last_any_log_token "${FPS_CONTRACT_LOG}" \
        validation_run_id xv6_validation_run_id d3d12_validation_run_id d3d12_run_id)"
    fps_artifact_run="$(last_any_log_token "${FPS_CONTRACT_LOG}" \
        fps_artifact_run_id validation_run_id)"
    display_bind_backend="$(last_log_token "${FPS_CONTRACT_LOG}" display_bind_backend)"
    display_bind_transport="$(last_log_token "${FPS_CONTRACT_LOG}" display_bind_transport)"
    display_bind_present_id="$(last_log_counter "${FPS_CONTRACT_LOG}" display_bind_present_id)"
    display_bind_completed_id="$(last_log_counter "${FPS_CONTRACT_LOG}" display_bind_completed_id)"
    display_bind_generation="$(last_log_counter "${FPS_CONTRACT_LOG}" display_bind_resource_generation)"
    native_present_id="$(last_any_log_counter "${FPS_CONTRACT_LOG}" \
        native_present_id d3d12_dxg_present_id present_id)"
    native_completed="$(last_any_log_counter "${FPS_CONTRACT_LOG}" \
        native_completed d3d12_dxg_present_completed completed)"

    dxg_submit="${dxg_submit:-missing}"
    fps_submit="${fps_submit:-missing}"
    dxg_run="${dxg_run:-none}"
    fps_run="${fps_run:-none}"
    fps_artifact_run="${fps_artifact_run:-none}"
    display_bind_backend="${display_bind_backend:-none}"
    display_bind_transport="${display_bind_transport:-none}"
    display_bind_present_id="${display_bind_present_id:-0}"
    display_bind_completed_id="${display_bind_completed_id:-0}"
    display_bind_generation="${display_bind_generation:-0}"
    native_present_id="${native_present_id:-0}"
    native_completed="${native_completed:-0}"

    echo "hyperv-webkit-gpu-validate: webkit_downstream_consumer_gate_matrix validation_run_id=${VALIDATION_RUN_ID} webkit_run_id=${VALIDATION_RUN_ID} prior_dxg_run_id=${dxg_run} prior_fps_run_id=${fps_run} fps_artifact_run_id=${fps_artifact_run} backend_identity=FB_GPU_BACKEND_F_OPENGL_SUBMIT dxg_backend_opengl_submit=${dxg_submit} fps_backend_opengl_submit=${fps_submit} backend_opengl_submit=0 backend_zero_rejected=1 display_bind_backend=${display_bind_backend} display_bind_transport=${display_bind_transport} display_bind_present_id=${display_bind_present_id} display_bind_completed_id=${display_bind_completed_id} display_bind_resource_generation=${display_bind_generation} native_present_id=${native_present_id} native_completed=${native_completed} native_present_ids_zero_rejected=1 current_run_display_bind_completion=0 current_run_native_present_identity=0 finite_480p_fps_artifact=0 rejected_reason=${reason} gate=closed native_present_credit=0 opengl_submit_credit=0 webkit_accel_credit=0 status=PASS" |
        tee -a "${LOG}"
}

require_last_token_eq()
{
    local file="$1"
    local key="$2"
    local expected="$3"
    local why="$4"
    local value

    value="$(last_log_token "${file}" "${key}")"
    if [[ -z "${value}" ]]; then
        fail "missing ${why}: ${key} in ${file}"
    fi
    if [[ "${value}" != "${expected}" ]]; then
        fail "${why} mismatch: ${key}=${value} expected=${expected}"
    fi
}

require_counter_ge()
{
    local file="$1"
    local key="$2"
    local min="$3"
    local why="$4"
    local value

    value="$(last_log_counter "${file}" "${key}")"
    if [[ -z "${value}" ]]; then
        fail "missing ${why}: ${key} in ${file}"
    fi
    if (( value < min )); then
        fail "${why} too small: ${key}=${value} required>=${min}"
    fi
}

require_any_counter_ge()
{
    local file="$1"
    local min="$2"
    local why="$3"
    local value
    shift 3

    value="$(last_any_log_counter "${file}" "$@")"
    if [[ -z "${value}" ]]; then
        fail "missing ${why}: expected one of $* in ${file}"
    fi
    if (( value < min )); then
        fail "${why} too small: value=${value} required>=${min}"
    fi
}

require_compositor_owned_visible_content_file()
{
    local file="$1"
    local why="$2"
    local present_id
    local completed
    local content_present_id
    local content_completed
    local display_bind_generation
    local content_generation
    local display_bind_present_id
    local display_bind_completed_id
    local content_display_bind_present_id
    local content_display_bind_completed_id
    local final_handoff_present_id
    local final_handoff_completed
    local final_handoff_generation
    local final_handoff_success

    require_file_log "${file}" \
        'd3d12_visible_content_crc[ =](0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' \
        "${why} compositor-owned visible content CRC"
    require_file_log "${file}" \
        'd3d12_visible_content_frame[ =](0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' \
        "${why} compositor-owned visible content frame counter"
    require_file_log "${file}" \
        'd3d12_visible_frame_hash[ =](0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' \
        "${why} compositor-owned visible content frame hash"
    require_file_log "${file}" \
        '(^|[[:space:]])d3d12_content_progress_source_owned[ =]1($|[[:space:]])' \
        "${why} compositor-owned content source"
    require_file_log "${file}" \
        '(^|[[:space:]])d3d12_content_progress_state[ =]NATIVE_PRESENT_COMPLETE($|[[:space:]])' \
        "${why} D3D12 content progress state"
    require_file_log "${file}" \
        '(^|[[:space:]])d3d12_visible_content_progress[ =]NATIVE_PRESENT_COMPLETE($|[[:space:]])' \
        "${why} D3D12 visible content progress"
    require_counter_ge "${file}" d3d12_content_progress_native_present_complete 1 \
        "${why} content progress native present completion"
    require_counter_ge "${file}" d3d12_content_progress_visible_credit 1 \
        "${why} visible content credit"
    require_counter_ge "${file}" d3d12_content_progress_native_present_credit 1 \
        "${why} native content credit"
    require_counter_ge "${file}" d3d12_content_progress_current_run_valid 1 \
        "${why} content current-run identity"
    require_counter_ge "${file}" d3d12_content_progress_identity_complete 1 \
        "${why} exact content/display-bind identity"

    present_id="$(last_any_log_counter "${file}" d3d12_dxg_present_id present_id)"
    completed="$(last_any_log_counter "${file}" d3d12_dxg_present_completed completed)"
    content_present_id="$(last_log_counter "${file}" d3d12_content_progress_present_id)"
    content_completed="$(last_log_counter "${file}" d3d12_content_progress_completed)"
    display_bind_present_id="$(last_log_counter "${file}" display_bind_present_id)"
    display_bind_completed_id="$(last_log_counter "${file}" display_bind_completed_id)"
    content_display_bind_present_id="$(last_log_counter "${file}" d3d12_content_progress_display_bind_present_id)"
    content_display_bind_completed_id="$(last_log_counter "${file}" d3d12_content_progress_display_bind_completed_id)"
    display_bind_generation="$(last_log_counter "${file}" display_bind_resource_generation)"
    content_generation="$(last_log_counter "${file}" d3d12_content_progress_display_bind_resource_generation)"
    final_handoff_success="$(last_log_counter "${file}" d3d12_final_handoff_success)"
    final_handoff_present_id="$(last_log_counter "${file}" d3d12_final_handoff_present_id)"
    final_handoff_completed="$(last_log_counter "${file}" d3d12_final_handoff_completed)"
    final_handoff_generation="$(last_log_counter "${file}" d3d12_final_handoff_resource_generation)"
    if [[ -z "${present_id}" || -z "${completed}" ||
          -z "${content_present_id}" || -z "${content_completed}" ||
          -z "${display_bind_present_id}" ||
          -z "${display_bind_completed_id}" ||
          -z "${content_display_bind_present_id}" ||
          -z "${content_display_bind_completed_id}" ||
          -z "${display_bind_generation}" || -z "${content_generation}" ||
          -z "${final_handoff_success}" ||
          -z "${final_handoff_present_id}" ||
          -z "${final_handoff_completed}" ||
          -z "${final_handoff_generation}" ]]; then
        fail "missing ${why} content/native-present id correlation"
    fi
    if (( content_present_id != present_id )); then
        fail "${why} content present id does not match native present id: content=${content_present_id} present=${present_id}"
    fi
    if (( content_completed != completed )); then
        fail "${why} content completed counter does not exactly match native completed counter: content=${content_completed} completed=${completed}"
    fi
    if (( completed < present_id )); then
        fail "${why} native completed counter does not cover present id: completed=${completed} present=${present_id}"
    fi
    if (( display_bind_present_id != present_id ||
          content_display_bind_present_id != display_bind_present_id )); then
        fail "${why} display-bind/content present id mismatch: present=${present_id} display_bind=${display_bind_present_id} content_display_bind=${content_display_bind_present_id}"
    fi
    if (( display_bind_completed_id != completed ||
          content_display_bind_completed_id != display_bind_completed_id )); then
        fail "${why} display-bind/content completed id mismatch: completed=${completed} display_bind=${display_bind_completed_id} content_display_bind=${content_display_bind_completed_id}"
    fi
    if (( content_generation != display_bind_generation )); then
        fail "${why} content resource generation does not match display-bind generation: content=${content_generation} display_bind=${display_bind_generation}"
    fi
    if (( final_handoff_success != 1 ||
          final_handoff_present_id != present_id ||
          final_handoff_completed != completed ||
          final_handoff_generation != display_bind_generation )); then
        fail "${why} final handoff ids do not exactly match DXG/display-bind/content identity: success=${final_handoff_success} final_present=${final_handoff_present_id} present=${present_id} final_completed=${final_handoff_completed} completed=${completed} final_generation=${final_handoff_generation} display_bind_generation=${display_bind_generation}"
    fi
}

require_same_client_resource_generation_identity_file()
{
    local file="$1"
    local why="$2"
    local client_pid
    local identity_client_pid
    local client_buffer_id
    local identity_client_buffer_id
    local manager_resource_id
    local identity_manager_resource_id
    local buffer_generation
    local identity_buffer_generation

    client_pid="$(last_log_counter "${file}" d3d12_client_pid)"
    identity_client_pid="$(last_log_counter "${file}" d3d12_present_identity_client_pid)"
    client_buffer_id="$(last_log_counter "${file}" d3d12_client_buffer_id)"
    identity_client_buffer_id="$(last_log_counter "${file}" d3d12_present_identity_client_buffer_id)"
    manager_resource_id="$(last_log_counter "${file}" d3d12_manager_resource_id)"
    identity_manager_resource_id="$(last_log_counter "${file}" d3d12_present_identity_manager_resource_id)"
    buffer_generation="$(last_log_counter "${file}" d3d12_buffer_generation)"
    identity_buffer_generation="$(last_log_counter "${file}" d3d12_present_identity_buffer_generation)"
    if [[ -z "${client_pid}" || -z "${identity_client_pid}" ||
          -z "${client_buffer_id}" || -z "${identity_client_buffer_id}" ||
          -z "${manager_resource_id}" || -z "${identity_manager_resource_id}" ||
          -z "${buffer_generation}" || -z "${identity_buffer_generation}" ]]; then
        fail "missing ${why} same-client/resource/generation identity fields"
    fi
    if (( client_pid < 1 || client_pid != identity_client_pid )); then
        fail "${why} client pid identity mismatch: client=${client_pid} identity=${identity_client_pid}"
    fi
    if (( client_buffer_id < 1 || client_buffer_id != identity_client_buffer_id )); then
        fail "${why} client buffer id identity mismatch: client=${client_buffer_id} identity=${identity_client_buffer_id}"
    fi
    if (( manager_resource_id < 1 || manager_resource_id != identity_manager_resource_id )); then
        fail "${why} manager resource id identity mismatch: resource=${manager_resource_id} identity=${identity_manager_resource_id}"
    fi
    if (( buffer_generation < 1 || buffer_generation != identity_buffer_generation )); then
        fail "${why} buffer generation identity mismatch: generation=${buffer_generation} identity=${identity_buffer_generation}"
    fi
    require_file_log "${file}" \
        '(^|[[:space:]])d3d12_present_identity_current_run_valid[ =]1($|[[:space:]])' \
        "${why} current-run identity proof"
}

require_d3d12_gpup_dda_commit_accepted_file()
{
    local file="$1"
    local why="$2"

    require_any_counter_ge "${file}" 1 \
        "${why} D3D12 GPU-P/DDA present-source commit accepted" \
        d3d12_dxg_present_source_commit_successes \
        d3d12_present_source_commit_successes \
        d3d12_present_source_buffer_commit_successes \
        dxg_present_commit_successes \
        d3d12_final_handoff_host_display_commit_success \
        d3d12_present_source_commit_accepted \
        d3d12_dxg_present_source_commit_accepted \
        d3d12_host_display_commit_accepted
}

require_holistic_d3d12_display_bind_evidence_file()
{
    local file="$1"
    local why="$2"
    local display_bind_present_id
    local display_bind_completed_id
    local present_id
    local completed

    require_file_log "${file}" \
        '(^|[[:space:]])display_bind_backend[ =](gpup_dxg_scanout_bind|gpu-p-dxg-resource-scanout-bind)($|[[:space:]])' \
        "${why} D3D12 display_bind_backend"
    require_file_log "${file}" \
        '(^|[[:space:]])display_bind_transport[ =]gpu-p-dxg-resource-scanout-bind($|[[:space:]])' \
        "${why} D3D12 display_bind_transport"
    require_counter_ge "${file}" display_bind_present_id 1 \
        "${why} D3D12 display_bind_present_id"
    require_counter_ge "${file}" display_bind_completed_id 1 \
        "${why} D3D12 display_bind_completed_id"
    require_counter_ge "${file}" display_bind_resource_generation 1 \
        "${why} D3D12 display_bind_resource_generation"
    require_file_log "${file}" \
        '(^|[[:space:]])completion_source[ =](3|display|native-display|native-display-completion)($|[[:space:]])' \
        "${why} D3D12 native display completion_source"
    require_file_log "${file}" \
        '(^|[[:space:]])d3d12_content_progress_state[ =]NATIVE_PRESENT_COMPLETE($|[[:space:]])' \
        "${why} D3D12 content progress state"
    require_file_log "${file}" \
        '(^|[[:space:]])d3d12_visible_content_progress[ =]NATIVE_PRESENT_COMPLETE($|[[:space:]])' \
        "${why} D3D12 visible content progress"
    require_counter_ge "${file}" d3d12_content_progress_native_present_complete 1 \
        "${why} D3D12 content progress native present completion"
    require_counter_ge "${file}" d3d12_content_progress_visible_credit 1 \
        "${why} D3D12 visible content credit"
    require_counter_ge "${file}" d3d12_native_present_completion_id 1 \
        "${why} D3D12 native present completion id"
    require_counter_ge "${file}" d3d12_final_handoff_success 1 \
        "${why} D3D12 final handoff success"
    require_counter_ge "${file}" d3d12_final_handoff_present_id 1 \
        "${why} D3D12 final handoff present id"
    require_counter_ge "${file}" d3d12_final_handoff_completed 1 \
        "${why} D3D12 final handoff completed counter"
    require_counter_ge "${file}" d3d12_final_handoff_resource_generation 1 \
        "${why} D3D12 final handoff resource generation"
    require_compositor_owned_visible_content_file "${file}" "${why}"

    display_bind_present_id="$(last_log_counter "${file}" display_bind_present_id)"
    display_bind_completed_id="$(last_log_counter "${file}" display_bind_completed_id)"
    present_id="$(last_any_log_counter "${file}" d3d12_dxg_present_id present_id)"
    completed="$(last_any_log_counter "${file}" d3d12_dxg_present_completed completed)"
    if (( display_bind_completed_id < display_bind_present_id )); then
        fail "${why} display_bind_completed_id does not cover display_bind_present_id: present_id=${display_bind_present_id} completed_id=${display_bind_completed_id}"
    fi
    if [[ -n "${present_id}" && "${display_bind_present_id}" -ne "${present_id}" ]]; then
        fail "${why} display_bind_present_id does not match D3D12 present_id: display_bind_present_id=${display_bind_present_id} present_id=${present_id}"
    fi
    if [[ -n "${completed}" && "${display_bind_completed_id}" -ne "${completed}" ]]; then
        fail "${why} display_bind_completed_id does not match D3D12 completed counter: display_bind_completed_id=${display_bind_completed_id} completed=${completed}"
    fi
}

require_wave60_native_present_keys_file()
{
    local file="$1"
    local why="$2"

    require_file_log "${file}" \
        'd3d12_native_present_requirements_satisfied[ =]1' \
        "${why} Wave60 native-present requirements satisfied marker"
    require_file_log "${file}" \
        'd3d12_present_source_luid_valid[ =]1' \
        "${why} Wave60 present-source LUID validity"
    require_file_log "${file}" \
        'd3d12_present_source_registered[ =]1' \
        "${why} Wave60 present-source registration proof"
    require_file_log "${file}" \
        'd3d12_present_source_query_attempted[ =]1' \
        "${why} Wave60 present-source query after commit proof"
    require_file_log "${file}" \
        'd3d12_present_source_buffer_commit_successes[ =][1-9][0-9]*' \
        "${why} Wave60 present-source buffer commit success"
    require_file_log "${file}" \
        'd3d12_present_source_buffer_present_id[ =][1-9][0-9]*' \
        "${why} Wave60 present-source buffer nonzero present id"
    require_file_log "${file}" \
        'd3d12_present_source_buffer_completed[ =][1-9][0-9]*' \
        "${why} Wave60 present-source buffer nonzero completed counter"
    require_file_log "${file}" \
        'd3d12_present_source_buffer_completion_correlated[ =]1' \
        "${why} Wave60 present-source buffer completion correlation"
    require_file_log "${file}" \
        'd3d12_present_source_buffer_query_attempted_after_commit_success[ =]1' \
        "${why} Wave60 present-source buffer query after successful commit"
    require_file_log "${file}" \
        'd3d12_buffer_release_present_id[ =][1-9][0-9]*' \
        "${why} Wave60 buffer release present-id correlation"
    require_file_log "${file}" \
        'd3d12_buffer_release_same_present_id[ =]1' \
        "${why} Wave60 buffer release same-present-id proof"
    require_file_log "${file}" \
        'd3d12_frame_callback_present_id[ =][1-9][0-9]*' \
        "${why} Wave60 frame callback present-id correlation"
    require_file_log "${file}" \
        'd3d12_frame_callback_same_present_id[ =]1' \
        "${why} Wave60 frame callback same-present-id proof"
    reject_file_log "${file}" \
        'd3d12_native_present_requirements_satisfied[ =]0|native_requirements=0|d3d12_present_source_(commit_rejected_eopnotsupp|no_present_id_completed|gpu_p_or_dda_transport_absent|same_frame_callbacks_blocked|same_frame_releases_blocked)[ =]1|d3d12_present_source_buffer_query_skipped_commit_failed[ =]1|d3d12_present_source_query_skipped_reason=commit-failed' \
        "${why} Wave60 fail-closed/pass-by-registration native-present evidence"
}

require_d3d12_same_frame_callback_release_file()
{
    local file="$1"
    local why="$2"

    require_file_log "${file}" \
        'd3d12sharedsmoke: present validation ok frame=1 release=1 gpu_present=[0-9]+->[1-9][0-9]*' \
        "${why} D3D12 same-frame callback/release present validation"

    if grep -Eq 'd3d12_.*same_frame|same_frame_.*d3d12' "${file}"; then
        require_any_counter_ge "${file}" 1 \
            "${why} D3D12 same-frame callback/release proof" \
            d3d12_callback_release_same_frame_observed \
            d3d12_same_frame_callback_release_observed \
            d3d12_same_frame_callback_release \
            d3d12_present_same_frame_callback_release \
            d3d12_present_same_frame_callback_release_observed
        if grep -Eq 'd3d12_.*callback.*same_frame|d3d12_.*same_frame.*callback' "${file}"; then
            require_any_counter_ge "${file}" 1 \
                "${why} D3D12 same-frame callback proof" \
                d3d12_same_frame_callback_observed \
                d3d12_present_same_frame_callback \
                d3d12_present_same_frame_callback_observed
        fi
        if grep -Eq 'd3d12_.*release.*same_frame|d3d12_.*same_frame.*release' "${file}"; then
            require_any_counter_ge "${file}" 1 \
                "${why} D3D12 same-frame release proof" \
                d3d12_same_frame_release_observed \
                d3d12_present_same_frame_release \
                d3d12_present_same_frame_release_observed
        fi
    fi
}

require_current_validation_run_id()
{
    local why="$1"

    require_last_token_eq "${LOG}" validation_run_id "${VALIDATION_RUN_ID}" \
        "${why} validation run id"
    require_last_token_eq "${LOG}" d3d12_run_id "${VALIDATION_RUN_ID}" \
        "${why} D3D12 run id"
    require_last_token_eq "${LOG}" d3d12_present_identity_compositor_run_id \
        "${VALIDATION_RUN_ID}" "${why} compositor run id"
    require_last_token_eq "${LOG}" d3d12_content_progress_run_id \
        "${VALIDATION_RUN_ID}" "${why} content progress run id"
    require_last_token_eq "${LOG}" d3d12_content_progress_compositor_run_id \
        "${VALIDATION_RUN_ID}" "${why} content progress compositor run id"
}

require_latest_d3d12_luid_match()
{
    local source_luid
    local matched_luid

    source_luid="$(grep -E 'd3d12_present_luid=' "${LOG}" |
        tail -n 1 | sed -E 's/.*d3d12_present_luid=([0-9a-fA-F]{8}:[0-9a-fA-F]{8}).*/\1/')"
    matched_luid="$(grep -E 'd3d12_present_matched_luid=' "${LOG}" |
        tail -n 1 | sed -E 's/.*d3d12_present_matched_luid=([0-9a-fA-F]{8}:[0-9a-fA-F]{8}).*/\1/')"
    if [[ -z "${source_luid}" || -z "${matched_luid}" ||
          "${source_luid}" != "${matched_luid}" ]]; then
        fail "D3D12 native present source/matched LUID mismatch source=${source_luid:-missing} matched=${matched_luid:-missing}"
    fi
}

require_prior_shared_surface_contract()
{
    require_prior_core_gpu_contract
    require_fresh_file "${DXG_CONTRACT_LOG}" \
        "prior DXG/native-present contract evidence log"
    require_fresh_file "${FPS_CONTRACT_LOG}" \
        "prior FPS/OpenGL-submit contract evidence log"
    reject_import_only_evidence_file "${DXG_CONTRACT_LOG}" \
        "prior DXG/native-present contract evidence"
    reject_import_only_evidence_file "${FPS_CONTRACT_LOG}" \
        "prior FPS/OpenGL-submit contract evidence"
    require_wave45_lifetime_rows_file_if_present "${DXG_CONTRACT_LOG}"
    require_import_negative_rows_file_if_present "${DXG_CONTRACT_LOG}"
    require_opensyncobject_source_rows_file_if_present "${DXG_CONTRACT_LOG}"
    require_ntshared_close_behavior_rows_file_if_present "${DXG_CONTRACT_LOG}"
    require_d3d12_display_provenance_file_if_present "${DXG_CONTRACT_LOG}" \
        "prior DXG/native-present contract evidence"
    require_d3d12_display_provenance_file_if_present "${FPS_CONTRACT_LOG}" \
        "prior FPS/OpenGL-submit contract evidence"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_fence_sharing_policy_matrix .*decision=dxg_syncfile_acquire .*direct_d3d12_open_required=0 .*dxg_syncfile_acquire_required=1 .*same_adapter_trace_required=1 .*native_present_claim=0 .*opengl_submit_credit=0 .*status=PASS' \
        "prior DXG/native-present D3D12 fence-sharing policy"
    if grep -Eq 'backend_opengl_submit 0' "${FPS_CONTRACT_LOG}" ||
       grep -Eq 'backend_opengl_submit 0' "${DXG_CONTRACT_LOG}"; then
        emit_downstream_consumer_gate_closed \
            "FB_GPU_BACKEND_F_OPENGL_SUBMIT-zero"
        reject_file_log "${DXG_CONTRACT_LOG}" \
            'backend_opengl_submit 1|d3d12sharedsmoke: shared-surface OpenGL-submit contract ok|hyperv-3d-fps-validate: ok' \
            "prior accelerated shared-surface contract while Hyper-V OpenGL-submit is gated"
        reject_file_log "${FPS_CONTRACT_LOG}" \
            'backend_opengl_submit 1|d3d12sharedsmoke: shared-surface OpenGL-submit contract ok|hyperv-3d-fps-validate: ok' \
            "prior accelerated FPS contract while Hyper-V OpenGL-submit is gated"
        return
    fi
    require_file_log "${DXG_CONTRACT_LOG}" \
        'mesaglfeature: D3D12 renderer confirmed renderer=.*[Dd]3[Dd]12' \
        "prior Mesa native D3D12 renderer selection"
    require_file_log "${DXG_CONTRACT_LOG}" 'mesaglfeature: ok' \
        "prior Mesa D3D12 feature smoke"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12sharedsmoke: present validation ok frame=1 release=1 gpu_present=[0-9]+->[1-9][0-9]*' \
        "prior native D3D12 shared-resource present"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'hyperv-dxg-validate: validation_run_id=[A-Za-z0-9_.:-]+' \
        "prior DXG validator nonce"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff .*present_id=[1-9][0-9]* completed=[1-9][0-9]* .*mtime_ms=[1-9][0-9]* min_mtime_ms=[1-9][0-9]* .*starts=[1-9][0-9]* copy=[1-9][0-9]* completes=[1-9][0-9]* .*resource=0x[1-9a-fA-F][0-9a-fA-F]* allocations=[1-9][0-9]* fence=0x[1-9a-fA-F][0-9a-fA-F]* target=1 release=[1-9][0-9]* .*source_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8}) matched_luid=\1:\2 no_cpu_readback=1' \
        "prior D3D12 native-present/shared-surface precondition"
    require_file_log "${DXG_CONTRACT_LOG}" \
        '^(validation_run_id|xv6_validation_run_id|d3d12_validation_run_id|fps_validation_run_id|d3d12_run_id)=[A-Za-z0-9_.:-]+($|[[:space:]])' \
        "prior DXG D3D12 native-present evidence nonce"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_evidence_generation[ =][1-9][0-9]*' \
        "prior DXG D3D12 evidence generation"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_present_evidence_time_us[ =][1-9][0-9]*' \
        "prior DXG D3D12 evidence timestamp"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_display_handoff_implemented[ =]1' \
        "prior DXG D3D12 display-handoff implementation marker"
    require_wave60_native_present_keys_file "${DXG_CONTRACT_LOG}" \
        "prior DXG strict-present"
    require_holistic_d3d12_display_bind_evidence_file "${DXG_CONTRACT_LOG}" \
        "prior DXG strict-present"
    require_d3d12_gpup_dda_commit_accepted_file "${DXG_CONTRACT_LOG}" \
        "prior DXG strict-present"
    require_d3d12_same_frame_callback_release_file "${DXG_CONTRACT_LOG}" \
        "prior DXG strict-present"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'scanout_d3d12_bridge_matrix .*status=PASS_FAIL_CLOSED.*(pfnmap|pfn_map)[=:]1.*vram[=:]1|scanout_d3d12_bridge_matrix .*status=PASS_FAIL_CLOSED.*vram[=:]1.*(pfnmap|pfn_map)[=:]1' \
        "prior Wave44 scanout/D3D12 boundary proof"
    if grep -Eq 'dxg_existing_sysmem_ntbridge|existing_sysmem_ntbridge' "${DXG_CONTRACT_LOG}"; then
        require_file_log "${DXG_CONTRACT_LOG}" \
            '(dxg_existing_sysmem_ntbridge|existing_sysmem_ntbridge).*shareable[=:][01].*reason[=:][A-Za-z0-9_.:-]+|(dxg_existing_sysmem_ntbridge|existing_sysmem_ntbridge).*reason[=:][A-Za-z0-9_.:-]+.*shareable[=:][01]' \
            "prior existing-sysmem NT bridge shareable/reason evidence"
    fi
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_runtime_created_d3d12_resource_required[ =]1' \
        "prior strict-present runtime-created D3D12 resource marker"
    require_file_log "${DXG_CONTRACT_LOG}" \
        '(^|[[:space:]])(d3d12_)?existing_sysmem_is_d3d12_com_resource[ =]0' \
        "prior strict-present existing-sysmem exclusion marker"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_visible_content_crc[ =](0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' \
        "prior DXG compositor-owned visible content CRC"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_visible_content_frame[ =](0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' \
        "prior DXG compositor-owned visible content frame counter"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_visible_frame_hash[ =](0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' \
        "prior DXG compositor-owned visible content frame hash"
    require_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_content_progress_source_owned[ =]1' \
        "prior DXG compositor-owned content source"
    require_file_log "${FPS_CONTRACT_LOG}" \
        'hyperv-3d-fps-validate: ok validation_run_id=[A-Za-z0-9_.:-]+ .*window=640x480 .*render=640x480 .*render_div=1 ' \
        "prior finite 480p FPS validation"
    require_file_log "${FPS_CONTRACT_LOG}" \
        'd3d12sharedsmoke: stale present evidence cleared path=/tmp/wlcomp-d3d12-present existed=[01]' \
        "prior FPS stale native-present evidence removal"
    require_file_log "${FPS_CONTRACT_LOG}" \
        'd3d12_runtime_created_d3d12_resource_required[ =]1' \
        "prior FPS strict-present runtime-created D3D12 resource marker"
    require_file_log "${FPS_CONTRACT_LOG}" \
        '(^|[[:space:]])(d3d12_)?existing_sysmem_is_d3d12_com_resource[ =]0' \
        "prior FPS strict-present existing-sysmem exclusion marker"
    require_wave60_native_present_keys_file "${FPS_CONTRACT_LOG}" \
        "prior FPS strict-present"
    require_holistic_d3d12_display_bind_evidence_file "${FPS_CONTRACT_LOG}" \
        "prior FPS strict-present"
    require_d3d12_gpup_dda_commit_accepted_file "${FPS_CONTRACT_LOG}" \
        "prior FPS strict-present"
    require_d3d12_same_frame_callback_release_file "${FPS_CONTRACT_LOG}" \
        "prior FPS strict-present"
    require_file_fps_gt "${FPS_CONTRACT_LOG}" native_present_fps 60 \
        "prior native-present FPS"
    require_file_fps_gt "${FPS_CONTRACT_LOG}" display_completion_fps 60 \
        "prior display-completion FPS"
    require_file_fps_gt "${FPS_CONTRACT_LOG}" effective_presented_fps 60 \
        "prior effective real-presented FPS"
    require_file_fps_gt "${FPS_CONTRACT_LOG}" sample_content_frame_fps 60 \
        "prior sample-window compositor content FPS"
    require_file_fps_gt "${FPS_CONTRACT_LOG}" visual_window_content_frame_fps 60 \
        "prior visual-window compositor content FPS"
    require_file_log "${FPS_CONTRACT_LOG}" \
        'hyperv-3d-fps-validate: visual cadence validation_run_id=[A-Za-z0-9_.:-]+ .*visual_samples_inside_sample_window=1 .*d3d12_display_handoff_implemented=1 .*native_present_delta=[1-9][0-9]* .*present_id_delta=[1-9][0-9]* .*completed_delta=[1-9][0-9]* .*evidence_generation_delta=[1-9][0-9]* .*frame_callback_delta=[1-9][0-9]* .*buffer_release_delta=[1-9][0-9]* .*content_crc_changes=[1-9][0-9]* .*content_frame_hash_changes=[1-9][0-9]* .*content_frame_delta=[1-9][0-9]* .*outside_overlay_crc_changes=[1-9][0-9]* .*visual_progress_fps=[0-9]+([.][0-9]+)? .*changed=[0-9,]*[1-9][0-9]*' \
        "prior visible-scene progression tied to native-present callback/release/content counters"
    require_file_log "${FPS_CONTRACT_LOG}" \
        'hyperv-3d-fps-validate: reported FPS evidence validation_run_id=[A-Za-z0-9_.:-]+ .*visual_samples_inside_sample_window=1 .*d3d12_display_handoff_implemented=1 .*native_present_fps=[0-9]+([.][0-9]+)? .*display_completion_fps=[0-9]+([.][0-9]+)? .*sample_present_id_delta=[1-9][0-9]* .*sample_completed_delta=[1-9][0-9]* .*sample_display_bind_present_id_delta=[1-9][0-9]* .*sample_display_bind_completed_id_delta=[1-9][0-9]* .*sample_evidence_generation_delta=[1-9][0-9]* .*sample_content_crc_changes=[1-9][0-9]* .*sample_content_frame_hash_changes=[1-9][0-9]* .*sample_content_frame_delta=[1-9][0-9]* .*active_native_intervals=[1-9][0-9]* .*active_display_intervals=[1-9][0-9]* .*active_callback_intervals=[1-9][0-9]* .*active_release_intervals=[1-9][0-9]* .*active_present_id_intervals=[1-9][0-9]* .*active_completed_intervals=[1-9][0-9]* .*active_content_frame_intervals=[1-9][0-9]* .*active_native_display_callback_release_intervals=[1-9][0-9]* .*active_full_evidence_intervals=[1-9][0-9]* .*visual_ceiling=[0-9]+([.][0-9]+)?' \
        "prior anti-inflation FPS evidence with nonce/content/callback/release correlation"
    require_file_log "${FPS_CONTRACT_LOG}" \
        'hyperv-3d-fps-validate: strict anti-inflation contract validation_run_id=[A-Za-z0-9_.:-]+ .*strict_anti_inflation=1 .*demo_client_pid_match=1 .*demo_resource_match=1 .*demo_buffer_generation_match=1 .*sample_window_or_timestamped=1 .*accepted_demo_samples=[1-9][0-9]* .*timestamped_post_window_samples=[0-9]+ .*ignored_post_window_samples=[0-9]+ .*process_id=[1-9][0-9]* .*d3d12_client_pid=[1-9][0-9]* .*demo_resources=0x[1-9a-fA-F][0-9a-fA-F]*(,0x[1-9a-fA-F][0-9a-fA-F]*)* .*wlcomp_resources=0x[1-9a-fA-F][0-9a-fA-F]*(,0x[1-9a-fA-F][0-9a-fA-F]*)* .*demo_buffer_generations=[1-9][0-9]*(,[0-9]+)* .*wlcomp_buffer_generations=[1-9][0-9]*(,[0-9]+)* .*visual_min_fps=[0-9]+([.][0-9]+)? .*low_visual_cadence_fps=[0-9]+([.][0-9]+)? .*visual_progress_fps=[0-9]+([.][0-9]+)? .*visual_report_ratio_limit=[0-9]+([.][0-9]+)? .*visual_stale_report_ratio_limit=[0-9]+([.][0-9]+)? .*visual_report_margin=[0-9]+([.][0-9]+)? .*sample_present_id_delta=[1-9][0-9]* .*sample_completed_delta=[1-9][0-9]* .*sample_display_bind_present_id_delta=[1-9][0-9]* .*sample_display_bind_completed_id_delta=[1-9][0-9]* .*visual_present_id_delta=[1-9][0-9]* .*visual_completed_delta=[1-9][0-9]* .*visual_display_bind_present_id_delta=[1-9][0-9]* .*visual_display_bind_completed_id_delta=[1-9][0-9]* .*effective_presented_fps=[0-9]+([.][0-9]+)?' \
        "prior strict FPS anti-inflation marker with demo/compositor identity correlation"
    require_file_log "${FPS_CONTRACT_LOG}" 'backend_opengl_submit 1' \
        "prior Hyper-V OpenGL-submit gate"
    reject_file_log "${DXG_CONTRACT_LOG}" \
        'd3d12_display_handoff_implemented=0|callbacks_blocked=1|releases_blocked=1|d3d12_(cpu_readback|cpu_mapping|cpu_copy|readback|present_state_only|present_fence_only|present_import_only|present_open_only|present_callback_only|present_release_only|present_errno)=[1-9][0-9]*' \
        "prior CPU/readback or partial-present D3D12 fallback"
    reject_file_log "${FPS_CONTRACT_LOG}" \
        'd3d12_display_handoff_implemented=0|callbacks_blocked=1|releases_blocked=1|d3d12_(cpu_readback|cpu_mapping|cpu_copy|readback|present_state_only|present_fence_only|present_import_only|present_open_only|present_callback_only|present_release_only|present_errno)=[1-9][0-9]*' \
        "prior CPU/readback or partial-present D3D12 fallback"
    reject_file_log "${DXG_CONTRACT_LOG}" \
        'xv6-mesa: d3d12 native present unavailable; refusing DRI software/readback present' \
        "prior D3D12 DRI software/readback fallback"
    reject_file_log "${FPS_CONTRACT_LOG}" \
        'xv6-mesa: d3d12 native present unavailable; refusing DRI software/readback present' \
        "prior D3D12 DRI software/readback fallback"
}

require_prior_core_gpu_contract()
{
    require_fresh_file "${CORE_CONTRACT_LOG}" \
        "prior core GPU validator evidence log"
    require_file_log "${CORE_CONTRACT_LOG}" \
        'hyperv-gpu-core-validate: passed validation_run_id=' \
        "prior core GPU validator pass marker"
    require_file_log "${CORE_CONTRACT_LOG}" 'ttmtest: ok' \
        "prior TTM validator"
    require_file_log "${CORE_CONTRACT_LOG}" 'drmiftest: ok' \
        "prior DRM/GEM/KMS validator"
    require_file_log "${CORE_CONTRACT_LOG}" 'nouveauabitest: .*ok' \
        "prior Nouveau ABI validator"
    require_file_log "${CORE_CONTRACT_LOG}" 'backend_opengl_submit 0' \
        "prior Hyper-V OpenGL-submit gated state"
    reject_file_log "${CORE_CONTRACT_LOG}" \
        'backend_opengl_submit 1|panic|fatal page fault|coredump|assert|device removal|Removing Device' \
        "prior invalid core GPU validator acceleration/crash marker"
}

webkit_shared_surface_contract_validated()
{
	    grep -Eq 'webkit_gpu_policy .*requested_accel=1 .*effective_accel=1 .*shared_surface=1 .*validated_shared_surface=1 .*d3d12_present=1 .*opengl_submit=1 .*d3d12_contract_evidence=1 .*d3d12_same_adapter=1 .*d3d12_no_readback=1 .*d3d12_shared_resource=1 .*d3d12_fence=1 .*gpu_contract=d3d12-shared-surface .*fallback=none' "${LOG}" &&
		    grep -Eq 'webkitgpusmoke: gpu-contract backend=hyperv-dxg .*render_node=1 .*shared_surface=1 .*d3d12_present=1 .*opengl_submit=1 .*dxg_transport=1 .*d3dkmt=1 .*d3d12_contract_evidence=1 .*d3d12_same_adapter=1 .*d3d12_no_readback=1 .*d3d12_shared_resource=1 .*d3d12_fence=1 .*d3d12_identity_ok=1 .*d3d12_callback_release_ok=1 .*d3d12_content_progress=1 .*d3d12_content_source_owned=1 .*env_contract=d3d12-shared-surface .*env_d3d12=1 .*env_virgl=0 .*env_software=0 .*env_d3d12_driver=1 .*env_d3d12_loader=1 .*env_d3d12_xv6gpu=1 .*env_d3d12_inplace=1 .*env_d3d12_throttle0=1 .*env_d3d12_perf0=1 .*env_d3d12_vblank0=1 .*env_egl_wayland=1 .*env_libgl_dri=1 .*env_d3d12_native_present_enabled=1 .*env_d3d12_native_present_required=1 .*env_d3d12_native_present_disabled=0 .*env_d3d12_copy_export=0 .*force_compositing=1 .*require=1 .*ok=1' "${LOG}" &&
	    grep -Eq "webkitgpusmoke: gpu-contract .*d3d12_run_id=${VALIDATION_RUN_ID} .*d3d12_compositor_run_id=${VALIDATION_RUN_ID} .*env_run_id=${VALIDATION_RUN_ID} .*d3d12_run_id_match=1 .*ok=1" "${LOG}" &&
	    grep -Eq 'd3d12sharedsmoke: present validation ok frame=1 release=1 gpu_present=[0-9]+->[1-9][0-9]*' "${LOG}" &&
    grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff .*present_id=[1-9][0-9]* completed=[1-9][0-9]* .*mtime_ms=[1-9][0-9]* min_mtime_ms=[1-9][0-9]* .*starts=[1-9][0-9]* copy=[1-9][0-9]* completes=[1-9][0-9]* .*resource=0x[1-9a-fA-F][0-9a-fA-F]* allocations=[1-9][0-9]* fence=0x[1-9a-fA-F][0-9a-fA-F]* target=1 release=[1-9][0-9]* .*source_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8}) matched_luid=\1:\2 no_cpu_readback=1' "${LOG}" &&
    grep -Eq 'd3d12_gpu_present_complete[s]?=[1-9][0-9]*' "${LOG}" &&
    grep -Eq 'd3d12_gpu_present_starts=[1-9][0-9]*' "${LOG}" &&
    grep -Eq 'd3d12_gpu_copy_completes=[1-9][0-9]*' "${LOG}" &&
    grep -Eq 'd3d12_display_handoff_implemented[ =]1' "${LOG}" &&
    grep -Eq '(^|[[:space:]])display_bind_backend[ =](gpup_dxg_scanout_bind|gpu-p-dxg-resource-scanout-bind)($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])display_bind_transport[ =]gpu-p-dxg-resource-scanout-bind($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])display_bind_present_id[ =][1-9][0-9]*($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])display_bind_completed_id[ =][1-9][0-9]*($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])display_bind_resource_generation[ =][1-9][0-9]*($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])completion_source[ =](3|display|native-display|native-display-completion)($|[[:space:]])' "${LOG}" &&
    grep -Eq 'd3d12_evidence_generation=[1-9][0-9]*' "${LOG}" &&
    grep -Eq 'd3d12_present_evidence_time_us=[1-9][0-9]*' "${LOG}" &&
    grep -Eq 'd3d12_visible_content_crc=(0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' "${LOG}" &&
    grep -Eq 'd3d12_visible_content_frame=(0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' "${LOG}" &&
    grep -Eq 'd3d12_visible_frame_hash=(0x[1-9a-fA-F][0-9a-fA-F]*|[1-9][0-9]*)' "${LOG}" &&
    grep -Eq '(^|[[:space:]])d3d12_content_progress_state[ =]NATIVE_PRESENT_COMPLETE($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])d3d12_visible_content_progress[ =]NATIVE_PRESENT_COMPLETE($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])d3d12_content_(native_complete|native_present_complete|progress_native_present_complete)[ =]1($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])d3d12_content_visible_credit[ =]1($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])d3d12_content_native_credit[ =]1($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])d3d12_content_progress_source_owned[ =]1($|[[:space:]])' "${LOG}" &&
    grep -Eq '(^|[[:space:]])d3d12_present_identity_current_run_valid[ =]1($|[[:space:]])' "${LOG}" &&
    grep -Eq "^(validation_run_id|xv6_validation_run_id|d3d12_validation_run_id|fps_validation_run_id|d3d12_run_id)=${VALIDATION_RUN_ID}($|[[:space:]])" "${LOG}" &&
    grep -Eq 'd3d12_present_release_fence=[1-9][0-9]*' "${LOG}"
}

require_current_native_present_contract()
{
    local starts
    local copies
    local completes
    local callbacks
    local releases
    local present_id
    local present_completed
    local display_completions

    require_log 'd3d12sharedsmoke: stale present evidence cleared path=/tmp/wlcomp-d3d12-present existed=[01]' \
        "fresh WebKit preflight removed stale D3D12 present evidence"
    require_log 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff .*present_id=[1-9][0-9]* completed=[1-9][0-9]* .*mtime_ms=[1-9][0-9]* min_mtime_ms=[1-9][0-9]* .*starts=[1-9][0-9]* copy=[1-9][0-9]* completes=[1-9][0-9]* .*source_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8}) matched_luid=\1:\2 no_cpu_readback=1' \
        "fresh current-run D3D12 native-present evidence"
    require_counter_ge "${LOG}" d3d12_gpu_present_starts 1 \
        "WebKit current-run D3D12 present starts"
    require_counter_ge "${LOG}" d3d12_gpu_copy_completes 1 \
        "WebKit current-run D3D12 GPU-copy completions"
    require_counter_ge "${LOG}" d3d12_display_handoff_implemented 1 \
        "WebKit current-run D3D12 display-handoff implementation marker"
    require_wave60_native_present_keys_file "${LOG}" \
        "WebKit current-run"
    require_holistic_d3d12_display_bind_evidence_file "${LOG}" \
        "WebKit current-run"
    require_d3d12_gpup_dda_commit_accepted_file "${LOG}" \
        "WebKit current-run"
    require_d3d12_same_frame_callback_release_file "${LOG}" \
        "WebKit current-run"
    require_any_counter_ge "${LOG}" 1 \
        "WebKit current-run D3D12 frame callback counter" \
        d3d12_frame_callbacks d3d12_frame_callback_observed \
        d3d12_present_callbacks \
        d3d12_gpu_present_callbacks d3d12_callback_count
    require_any_counter_ge "${LOG}" 1 \
        "WebKit current-run D3D12 buffer release counter" \
        d3d12_buffer_releases d3d12_buffer_release_observed \
        d3d12_present_releases \
        d3d12_gpu_present_releases d3d12_release_count
    require_counter_ge "${LOG}" d3d12_evidence_generation 1 \
        "WebKit current-run D3D12 evidence generation"
    require_counter_ge "${LOG}" d3d12_present_evidence_time_us 1 \
        "WebKit current-run D3D12 evidence timestamp"
    require_counter_ge "${LOG}" d3d12_present_resource 1 \
        "WebKit current-run D3D12 present resource identity"
    require_same_client_resource_generation_identity_file "${LOG}" \
        "WebKit current-run D3D12 evidence"
    require_counter_ge "${LOG}" d3d12_runtime_created_d3d12_resource_required 1 \
        "WebKit current-run runtime-created D3D12 resource strict-present marker"
    require_log '(^|[[:space:]])(d3d12_)?existing_sysmem_is_d3d12_com_resource[ =]0' \
        "WebKit current-run strict-present existing-sysmem exclusion marker"
    require_d3d12_display_provenance_file_if_present "${LOG}" \
        "WebKit current-run evidence"
    require_compositor_owned_visible_content_file "${LOG}" \
        "WebKit current-run D3D12 evidence"
    require_current_validation_run_id "WebKit current-run D3D12 evidence"
    require_log 'webkitgpusmoke: gpu-contract .*d3d12_identity_ok=1 .*d3d12_run_id_match=1 .*d3d12_callback_release_ok=1 .*d3d12_content_progress=1 .*d3d12_content_source_owned=1 .*ok=1' \
        "WebKit in-process compositor-owned native-present content gate"
    completes="$(last_log_counter "${LOG}" d3d12_gpu_present_completes)"
    if [[ -z "${completes}" ]]; then
        completes="$(last_log_counter "${LOG}" d3d12_gpu_present_complete)"
    fi
    if [[ -z "${completes}" || "${completes}" -lt 1 ]]; then
        fail "missing WebKit current-run D3D12 present completions"
    fi
    require_counter_ge "${LOG}" display_completions 1 \
        "WebKit current-run display completions"
    starts="$(last_log_counter "${LOG}" d3d12_gpu_present_starts)"
    copies="$(last_log_counter "${LOG}" d3d12_gpu_copy_completes)"
    callbacks="$(last_any_log_counter "${LOG}" \
        d3d12_frame_callbacks d3d12_frame_callback_observed \
        d3d12_present_callbacks \
        d3d12_gpu_present_callbacks d3d12_callback_count)"
    releases="$(last_any_log_counter "${LOG}" \
        d3d12_buffer_releases d3d12_buffer_release_observed \
        d3d12_present_releases \
        d3d12_gpu_present_releases d3d12_release_count)"
    present_id="$(last_any_log_counter "${LOG}" d3d12_dxg_present_id present_id)"
    present_completed="$(last_any_log_counter "${LOG}" d3d12_dxg_present_completed completed)"
    display_completions="$(last_log_counter "${LOG}" display_completions)"
    if [[ -z "${present_id}" || "${present_id}" -lt 1 ]]; then
        fail "missing WebKit current-run nonzero D3D12 present_id"
    fi
    if [[ -z "${present_completed}" || "${present_completed}" -lt 1 ]]; then
        fail "missing WebKit current-run nonzero D3D12 completed counter"
    fi
    if (( present_completed < present_id )); then
        fail "WebKit current-run D3D12 completed counter does not cover present_id: present_id=${present_id} completed=${present_completed}"
    fi
    if (( completes > starts )); then
        fail "WebKit current-run D3D12 present completions exceed starts: completes=${completes} starts=${starts}"
    fi
    if (( completes > copies )); then
        fail "WebKit current-run D3D12 present completions exceed GPU-copy completions: completes=${completes} copies=${copies}"
    fi
    if (( completes > callbacks )); then
        fail "WebKit current-run D3D12 present completions exceed frame callbacks: completes=${completes} callbacks=${callbacks}"
    fi
    if (( completes > releases )); then
        fail "WebKit current-run D3D12 present completions exceed buffer releases: completes=${completes} releases=${releases}"
    fi
    if (( completes > display_completions )); then
        fail "WebKit current-run D3D12 present completions exceed display completions: completes=${completes} display=${display_completions}"
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

run_webkit_animated_fixture_liveness()
{
    local fixture_uri="file:///share/webkit/${WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE}"

    echo "hyperv-webkit-gpu-validate: running animated WebKit fixture ${fixture_uri}" |
        tee -a "${LOG}"
    run_guest "rm -f /tmp/wlcomp-d3d12-present /tmp/webkit-gpu-policy /tmp/webkit-title /tmp/webkit-animated-fixture.log; webkitgpusmoke ${fixture_uri} ${TIMEOUT_MS} >/tmp/webkit-animated-fixture.log 2>&1; echo webkit_animated_fixture_status=\$?; cat /tmp/webkit-animated-fixture.log; cat /tmp/webkit-title; if test -f /tmp/webkit-gpu-policy; then cat /tmp/webkit-gpu-policy; else echo webkit_gpu_policy_absent=1; fi" \
        "${READ_MS}"
    require_log "webkitgpusmoke: complete uri=${fixture_uri} .*title_complete=1" \
        "WebKit animated fixture liveness"
    if grep -q 'webkit_gpu_policy ' "${LOG}"; then
        require_log 'webkit_gpu_policy .*effective_accel=0 .*validated_shared_surface=0 .*d3d12_present=0 .*gpu_contract=none' \
            "WebKit animated fixture liveness cannot inherit stale GPU policy"
    else
        require_log 'webkit_gpu_policy_absent=1' \
            "WebKit animated fixture liveness has no stale GPU policy"
    fi
    echo "hyperv-webkit-gpu-validate: webkit_animated_content_fixture_liveness_matrix fixture=${WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE} source_contained_fixture=1 animated_title_frame=PASS title_only_liveness=1 compositor_owned_visible_content=MISSING content_crc_progress=MISSING frame_hash_progress=MISSING current_webkit_run_id=MISSING same_client_resource_generation_identity=MISSING callback_release_ordering=MISSING native_present_completion=MISSING gate=closed native_present_credit=0 opengl_submit_credit=0 webkit_accel_credit=0 status=PASS" |
        tee -a "${LOG}"
}

run_contract_negative_validate()
{
    echo "hyperv-webkit-gpu-validate: mode=contract-negative" | tee -a "${LOG}"
    echo "hyperv-webkit-gpu-validate: building kernel/rootfs" | tee -a "${LOG}"
    cmake --build "${BUILD_DIR}" --target kernel -j"${WEBKIT_GPU_VALIDATE_JOBS:-2}" |
        tee -a "${LOG}"
    cmake --build "${BUILD_DIR}" --target rootfs -j"${WEBKIT_GPU_VALIDATE_JOBS:-2}" |
        tee -a "${LOG}"

    echo "hyperv-webkit-gpu-validate: building ${OUT_VHDX}" | tee -a "${LOG}"
    HYPERV_CMDLINE="BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=1 webkit_accel=1 webkit_contract_wait_ms=1000 webkit_api_smoke=0 webkit_webgl_smoke=0 webkit_logging=1 webkit_timeout_ms=15000 desktop_exit_after_smoke=1 video=1024x640 acpi_cpus=6" \
        scripts/make-hyperv-image.sh \
            "${BUILD_DIR}/kernel/build/kernel/xv6.bin" \
            "${BUILD_DIR}/fs.img" \
            "${OUT_VHDX}" 0 | tee -a "${LOG}"

    echo "hyperv-webkit-gpu-validate: deploying to ${VM_NAME}" | tee -a "${LOG}"
    run_ps "Stop-VM -Name '${VM_NAME}' -TurnOff -Force -ErrorAction SilentlyContinue"
    cp -f "${OUT_VHDX}" "${DEPLOY_VHDX}"
    run_ps "Start-VM -Name '${VM_NAME}'; Get-VM -Name '${VM_NAME}' | Select Name, State, Uptime | Format-List"

    echo "hyperv-webkit-gpu-validate: running contract-negative C validator" |
        tee -a "${LOG}"
    sleep 20
    run_guest "echo __WEBKIT_CONTRACT_NEG_BEGIN__; cat /proc/cmdline; cat /proc/uptime" \
        "${WEBKIT_GPU_CONTRACT_NEGATIVE_READ_MS:-60000}"
    run_guest "rm -f /tmp/wlcomp-d3d12-present; sleep 8; cat /tmp/webkit-gpu-policy" \
        "${WEBKIT_GPU_CONTRACT_NEGATIVE_READ_MS:-60000}"
    run_guest "webkitgpusmoke --expect-d3d12-gpu-contract-fail; echo webkit_contract_negative_status=\$?" \
        "${WEBKIT_GPU_CONTRACT_NEGATIVE_READ_MS:-60000}"
    run_guest "fbstat; ps; cat /proc/uptime; echo __WEBKIT_CONTRACT_NEG_END__" \
        "${WEBKIT_GPU_CONTRACT_NEGATIVE_READ_MS:-60000}"

    require_log '__WEBKIT_CONTRACT_NEG_BEGIN__' \
        "WebKit contract-negative begin marker"
    require_log '__WEBKIT_CONTRACT_NEG_END__' \
        "WebKit contract-negative end marker"
    require_log 'webkit=1 .*webkit_accel=1 .*webkit_contract_wait_ms=1000 .*webkit_api_smoke=0 .*webkit_webgl_smoke=0' \
        "WebKit contract-negative command line"
    require_log 'backend hyperv-dxg flags' "Hyper-V GPU backend"
    require_log 'backend_opengl_submit 0' \
        "Hyper-V OpenGL-submit remains gated"
    require_log "webkit_animated_content_native_present_gate_matrix .*fixture=${WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE} .*required_compositor_owned_visible_content=1 .*required_content_crc_progress=1 .*required_frame_hash_progress=1 .*required_current_webkit_run_id=1 .*required_same_client_resource_generation_identity=1 .*required_callback_release_ordering=1 .*required_prior_fps_native_present_contract=1 .*required_backend_opengl_submit=1 .*required_native_present_completion=1 .*title_only=REJECT .*chrome_only=REJECT .*cursor_only=REJECT .*env_only=REJECT .*render_node_only=REJECT .*dmabuf_only=REJECT .*gate=closed .*native_present_credit=0 .*opengl_submit_credit=0 .*webkit_accel_credit=0 .*status=PASS" \
        "WebKit animated-content/native-present gate closed on Hyper-V"
    require_log 'webkit_enabled_artifact_contract_matrix .*required_current_run_display_bind_completion=1 .*required_finite_480p_fps_artifact=1 .*required_backend_opengl_submit=1 .*required_shared_resource_fence_identity=1 .*required_compositor_owned_content_crc_frame_hash_identity=1 .*current_run_display_bind_completion=0 .*finite_480p_fps_artifact=0 .*backend_opengl_submit=0 .*shared_resource_fence_identity=0 .*compositor_owned_content_crc_frame_hash_identity=0 .*gate=closed .*native_present_credit=0 .*opengl_submit_credit=0 .*webkit_accel_credit=0 .*status=PASS' \
        "WebKit enabled artifact contract remains closed on Hyper-V"
    require_log 'webkit_downstream_consumer_gate_matrix .*backend_identity=FB_GPU_BACKEND_F_OPENGL_SUBMIT .*backend_opengl_submit=0 .*backend_zero_rejected=1 .*display_bind_present_id=0 .*display_bind_completed_id=0 .*native_present_id=0 .*native_completed=0 .*native_present_ids_zero_rejected=1 .*gate=closed .*native_present_credit=0 .*opengl_submit_credit=0 .*webkit_accel_credit=0 .*status=PASS' \
        "WebKit downstream consumer gate rejects zero backend/native-present ids"
    require_log 'webkit_stale_display_bind_evidence_rejection_matrix .*stale_display_bind_present_id=[1-9][0-9]* .*current_run_display_bind_completion=0 .*after_close_query_rejected=1 .*late_completion_after_release=0 .*effective_accel=0 .*native_present_credit=0 .*opengl_submit_credit=0 .*webkit_accel_credit=0 .*status=PASS' \
        "WebKit rejects stale display-bind evidence"
    require_log 'webkit_gpu_policy .*requested_accel=1 .*effective_accel=0 .*opengl_submit=0 .*shared_surface=0 .*validated_shared_surface=0 .*d3d12_present=0 .*gpu_contract=none .*fallback=opengl_submit_unavailable' \
        "WebKit desktop policy stays gated without shared-surface contract"
	    require_log 'webkitgpusmoke: gpu-contract backend=hyperv-dxg .*shared_surface=0 .*d3d12_present=0 .*opengl_submit=0 .*env_contract=d3d12-shared-surface .*env_d3d12=1 .*env_virgl=0 .*env_software=0 .*env_d3d12_driver=1 .*env_d3d12_loader=1 .*env_d3d12_xv6gpu=1 .*env_d3d12_inplace=1 .*env_d3d12_throttle0=1 .*env_d3d12_perf0=1 .*env_d3d12_vblank0=1 .*env_egl_wayland=1 .*env_libgl_dri=1 .*env_d3d12_native_present_enabled=1 .*env_d3d12_native_present_required=1 .*env_d3d12_native_present_disabled=0 .*env_d3d12_copy_export=0 .*force_compositing=1 .*require=1 .*ok=0' \
	        "WebKit C contract validator rejects unavailable D3D12 shared-surface contract"
	    require_log 'webkitgpusmoke: webkit_inprocess_contract_gate_matrix .*backend_identity=FB_GPU_BACKEND_F_OPENGL_SUBMIT .*backend_opengl_submit=0 .*backend_zero_rejected=1 .*display_bind_present_id=0 .*display_bind_completed_id=0 .*native_present_id=0 .*native_completed=0 .*native_present_ids_zero_rejected=1 .*gate=closed .*native_present_credit=0 .*opengl_submit_credit=0 .*webkit_accel_credit=0 .*status=PASS' \
	        "WebKit C contract validator emits zero-credit consumer gate"
	    require_log 'webkitgpusmoke: gpu-contract .*env_run_id=d3d12-contract-negative .*d3d12_run_id_match=0 .*ok=0' \
	        "WebKit C contract validator rejects missing/mismatched WebKit run id"
    require_log 'webkitgpusmoke: d3d12-contract-only expected=fail rc=-1' \
        "WebKit contract-negative expected failure"
    require_log 'webkit_contract_negative_status=0' \
        "WebKit contract-negative validator exit status"
    reject_log 'backend_opengl_submit 1' \
        "Hyper-V OpenGL-submit enabled before native-present/FPS/WebKit proof"
    reject_log 'webkit_gpu_policy .*effective_accel=1' \
        "WebKit acceleration enabled without validated shared-surface contract"
    reject_log 'webkit_gpu_policy .*gpu_contract=d3d12-shared-surface' \
        "WebKit D3D12 contract policy before prerequisites"
    reject_log 'webkit_gpu_policy .*gpu_contract=virgl-opengl-submit' \
        "WebKit virgl contract on Hyper-V negative image"
    reject_log 'webkitgpusmoke: d3d12-contract-only expected=fail rc=0' \
        "WebKit C validator accepting the unavailable D3D12 contract"
    reject_log 'panic|fatal page fault|SIGABRT|coredump: generating' \
        "crash marker"
    reject_log 'wlcomp exited' "compositor exit"

    echo "hyperv-webkit-gpu-validate: passed contract-negative (${LOG})" |
        tee -a "${LOG}"
}

run_preflight_stale_negative_validate()
{
    local tmpdir
    local output
    local rc
    local old_epoch

    tmpdir="$(mktemp -d "${BUILD_DIR}/webkit-preflight-stale.XXXXXX")"
    old_epoch="$(( $(date +%s) - CONTRACT_MAX_AGE_SEC - 30 ))"

    printf '%s\n' \
        'hyperv-gpu-core-validate: passed validation_run_id=stale-preflight' \
        'ttmtest: ok' \
        'drmiftest: ok' \
        'nouveauabitest: ok' \
        'backend_opengl_submit 0' \
        >"${tmpdir}/core.log"
    printf '%s\n' \
        'hyperv-dxg-validate: validation_run_id=stale-preflight' \
        'backend_opengl_submit 0' \
        >"${tmpdir}/dxg.log"
    printf '%s\n' \
        'hyperv-3d-fps-validate: ok validation_run_id=stale-preflight render=640x480' \
        'backend_opengl_submit 1' \
        'effective_presented_fps=999' \
        >"${tmpdir}/fps.log"
    touch -d "@${old_epoch}" "${tmpdir}/fps.log"

    set +e
    output="$(
        {
            CORE_CONTRACT_LOG="${tmpdir}/core.log"
            DXG_CONTRACT_LOG="${tmpdir}/dxg.log"
            FPS_CONTRACT_LOG="${tmpdir}/fps.log"
            require_prior_shared_surface_contract
        } 2>&1
    )"
    rc=$?
    set -e

    printf '%s\n' "${output}" | tee -a "${LOG}"
    if (( rc == 0 )); then
        fail "stale FPS preflight evidence was accepted"
    fi
    if ! grep -Fq 'stale prior FPS/OpenGL-submit contract evidence log' <<<"${output}"; then
        fail "stale FPS preflight negative did not fail for stale FPS evidence"
    fi
    if grep -Eq 'prior finite 480p FPS validation|prior Hyper-V OpenGL-submit gate' <<<"${output}"; then
        fail "stale FPS preflight negative reached accelerated WebKit/FPS acceptance checks"
    fi

    echo "hyperv-webkit-gpu-validate: stale-fps-preflight-negative status=PASS stale_log=${tmpdir}/fps.log" |
        tee -a "${LOG}"
}

case "${MODE}" in
full)
    if [[ "${WEBKIT_GPU_POLICY_PREFLIGHT}" == "1" ]]; then
        run_policy_negative_preflight
    fi
    ;;
contract-negative)
    if [[ "${WEBKIT_GPU_POLICY_PREFLIGHT}" == "1" ]]; then
        run_policy_negative_preflight
    fi
    run_contract_negative_validate
    exit 0
    ;;
preflight-stale-negative)
    if [[ "${WEBKIT_GPU_POLICY_PREFLIGHT}" == "1" ]]; then
        run_policy_negative_preflight
    fi
    run_preflight_stale_negative_validate
    exit 0
    ;;
*)
    fail "unsupported WEBKIT_GPU_VALIDATE_MODE=${MODE}"
    ;;
esac

echo "hyperv-webkit-gpu-validate: requiring prior GPU evidence core=${CORE_CONTRACT_LOG} dxg=${DXG_CONTRACT_LOG} fps=${FPS_CONTRACT_LOG}" |
    tee -a "${LOG}"
require_prior_shared_surface_contract
echo "hyperv-webkit-gpu-validate: building kernel/rootfs" | tee -a "${LOG}"
cmake --build "${BUILD_DIR}" --target kernel -j"${WEBKIT_GPU_VALIDATE_JOBS:-2}" |
    tee -a "${LOG}"
cmake --build "${BUILD_DIR}" --target rootfs -j"${WEBKIT_GPU_VALIDATE_JOBS:-2}" |
    tee -a "${LOG}"

echo "hyperv-webkit-gpu-validate: building ${OUT_VHDX}" | tee -a "${LOG}"
HYPERV_CMDLINE="BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=1 webkit_accel=1 webkit_contract_wait_ms=120000 webkit_api_smoke=1 webkit_webgl_smoke=1 webkit_logging=1 webkit_reopen=${REOPEN} webkit_timeout_ms=${TIMEOUT_MS} desktop_exit_after_smoke=1 video=1024x640 acpi_cpus=6" \
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
run_guest "cat /proc/cmdline; cat /proc/uptime; rm -f /tmp/wlcomp-d3d12-present; XV6_GPU_VALIDATE_RUN_ID=${VALIDATION_RUN_ID} XV6_WLCOMP_D3D12_RUN_ID=${VALIDATION_RUN_ID} d3d12sharedsmoke --runtime --require-present; cat /proc/uptime; sleep 90; cat /tmp/webkit-title; cat /tmp/webkit-gpu-policy; cat /tmp/webkit_log.txt; cat /tmp/wlcomp-d3d12-present; ps; fbstat; cat /proc/uptime" \
    "${READ_MS}"

run_webkit_animated_fixture_liveness

require_log 'webkit=1 .*webkit_accel=1 .*webkit_contract_wait_ms=120000 .*webkit_api_smoke=1 .*webkit_webgl_smoke=1 .*webkit_logging=1' \
    "WebKit local GPU/API smoke command line"
require_log 'backend hyperv-dxg flags' "Hyper-V GPU backend"
if webkit_shared_surface_contract_validated; then
    require_current_native_present_contract
    emit_webkit_animated_content_native_present_gate_open
    require_webkit_animated_content_native_present_gate
    emit_webkit_enabled_artifact_contract_matrix_open
    require_webkit_enabled_artifact_contract_matrix
    require_log 'webkit_gpu_policy .*requested_accel=1 .*effective_accel=1 .*shared_surface=1 .*validated_shared_surface=1 .*d3d12_present=1 .*opengl_submit=1 .*d3d12_contract_evidence=1 .*d3d12_same_adapter=1 .*d3d12_no_readback=1 .*d3d12_shared_resource=1 .*d3d12_fence=1 .*gpu_contract=d3d12-shared-surface .*fallback=none' \
        "WebKit D3D12 shared-surface/OpenGL-submit acceleration gate"
    require_log 'webkit_gpu_policy .*gpu_contract=d3d12-shared-surface .*d3d12_native_present_required=1 .*d3d12_copy_export=0 .*d3d12_readback=0 .*fallback=none' \
        "WebKit D3D12 native-present/no-readback policy"
	    require_log 'webkitgpusmoke: gpu-contract backend=hyperv-dxg .*render_node=1 .*shared_surface=1 .*d3d12_present=1 .*opengl_submit=1 .*dxg_transport=1 .*d3dkmt=1 .*d3d12_contract_evidence=1 .*d3d12_same_adapter=1 .*d3d12_no_readback=1 .*d3d12_shared_resource=1 .*d3d12_fence=1 .*env_contract=d3d12-shared-surface .*env_d3d12=1 .*env_virgl=0 .*env_software=0 .*env_d3d12_driver=1 .*env_d3d12_loader=1 .*env_d3d12_xv6gpu=1 .*env_d3d12_inplace=1 .*env_d3d12_throttle0=1 .*env_d3d12_perf0=1 .*env_d3d12_vblank0=1 .*env_egl_wayland=1 .*env_libgl_dri=1 .*env_d3d12_native_present_enabled=1 .*env_d3d12_native_present_required=1 .*env_d3d12_native_present_disabled=0 .*env_d3d12_copy_export=0 .*force_compositing=1 .*require=1 .*ok=1' \
	        "WebKit in-process D3D12 GPU contract audit"
	    require_log "webkitgpusmoke: gpu-contract .*d3d12_run_id=${VALIDATION_RUN_ID} .*d3d12_compositor_run_id=${VALIDATION_RUN_ID} .*env_run_id=${VALIDATION_RUN_ID} .*d3d12_run_id_match=1 .*ok=1" \
	        "WebKit D3D12 evidence belongs to the current WebKit run"
    require_log 'd3d12_gpu_present_complete[s]?=[1-9][0-9]*' \
        "WebKit D3D12 native-present completion evidence"
    require_log 'd3d12_present_resource=0x[1-9a-fA-F][0-9a-fA-F]*' \
        "WebKit D3D12 native-present shared resource"
    require_log 'd3d12_present_allocation_count=[1-9][0-9]*' \
        "WebKit D3D12 native-present allocation metadata"
    require_log 'd3d12_present_fence=0x[1-9a-fA-F][0-9a-fA-F]*' \
        "WebKit D3D12 native-present fence"
    require_log 'd3d12_present_fence_target=[1-9][0-9]*' \
        "WebKit D3D12 native-present fence target"
    require_log 'd3d12_present_release_fence=[1-9][0-9]*' \
        "WebKit D3D12 native-present release fence"
    require_log 'd3d12_present_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8})' \
        "WebKit D3D12 native-present source LUID"
    require_log 'd3d12_present_matched_luid=([0-9a-fA-F]{8}):([0-9a-fA-F]{8})' \
        "WebKit D3D12 native-present matched LUID"
    require_latest_d3d12_luid_match
else
    require_log 'backend_opengl_submit 0' "Hyper-V OpenGL-submit fallback gate"
    require_log "webkit_animated_content_native_present_gate_matrix .*fixture=${WEBKIT_GPU_ANIMATED_CONTENT_FIXTURE} .*gate=closed .*native_present_credit=0 .*opengl_submit_credit=0 .*webkit_accel_credit=0 .*status=PASS" \
        "WebKit animated-content/native-present gate remains closed"
    require_log 'webkit_enabled_artifact_contract_matrix .*gate=closed .*native_present_credit=0 .*opengl_submit_credit=0 .*webkit_accel_credit=0 .*status=PASS' \
        "WebKit enabled artifact contract remains closed"
    require_log 'webkit_gpu_policy .*requested_accel=1 .*effective_accel=0 .*opengl_submit=0 .*fallback=opengl_submit_unavailable' \
        "WebKit acceleration fallback gate"
    require_log 'webkit_gpu_policy .*shared_surface=0 .*validated_shared_surface=0 .*d3d12_present=0 .*gpu_contract=none .*fallback=opengl_submit_unavailable' \
        "WebKit shared-surface/D3D12-present contract remains gated"
    require_log 'webkitgpusmoke: gpu-contract backend=hyperv-dxg .*shared_surface=0 .*d3d12_present=0 .*opengl_submit=0 .*env_contract=none .*env_d3d12=0 .*env_virgl=0 .*env_software=1 .*ok=1' \
        "WebKit in-process fallback GPU contract audit"
    reject_log 'webkit_gpu_policy .*gpu_contract=d3d12-shared-surface' \
        "WebKit D3D12 contract before Hyper-V OpenGL-submit validation"
    reject_log 'webkit_gpu_policy .*gpu_contract=virgl-opengl-submit' \
        "WebKit virgl contract on Hyper-V fallback image"
    reject_log 'webkitgpusmoke: gpu-contract .*env_d3d12=1' \
        "WebKit D3D12 environment without validated contract"
    reject_log 'd3d12_gpu_present_complete[s]?=[1-9][0-9]*' \
        "WebKit D3D12 native present while OpenGL-submit is gated"
fi
if grep -Eq 'backend_opengl_submit 1' "${LOG}" &&
   ! webkit_shared_surface_contract_validated; then
    fail "Hyper-V advertised OpenGL-submit without the validated WebKit shared-surface contract"
fi
reject_dxg_present_accounting_state_file "${LOG}" \
    "WebKit current-run D3D12 evidence"
reject_protocol_only_strict_present_artifact_file "${LOG}" \
    "WebKit current-run D3D12 evidence"
require_log 'webkitgpusmoke pid=' "WebKit API smoke launch"
require_log 'relaunch(ed)? webkitgpusmoke' "WebKit reopen cycle"
require_log 'WebKit (API reopen|timeout) smoke complete' \
    "WebKit repeated smoke completion"
require_log 'wlcomp' "Wayland compositor remained observable"
reject_log 'panic|fatal page fault|SIGABRT|coredump: generating' \
    "crash marker"
reject_log 'wlcomp exited' "compositor exit"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*validated_shared_surface=0' \
    "WebKit acceleration without validated shared-surface contract"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*d3d12_contract_evidence=0' \
    "WebKit acceleration without D3D12 contract evidence"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*d3d12_same_adapter=0' \
    "WebKit acceleration without same-adapter D3D12 evidence"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*d3d12_no_readback=0' \
    "WebKit acceleration through readback-capable D3D12 evidence"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*d3d12_shared_resource=0' \
    "WebKit acceleration without D3D12 shared resource evidence"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*d3d12_fence=0' \
    "WebKit acceleration without D3D12 fence evidence"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*gpu_contract=none' \
    "WebKit acceleration without a named GPU contract"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*fallback=(opengl_submit_unavailable|render_node_unavailable|shared_surface_unavailable|d3d12_contract_evidence_unavailable)' \
    "WebKit acceleration despite a fallback reason"
reject_log 'webkit_gpu_policy .*effective_accel=1 .*dmabuf=1' \
    "WebKit acceleration through dmabuf instead of D3D12 shared-resource contract"
reject_log 'webkit_gpu_policy .*dmabuf=1 .*validated_shared_surface=0' \
    "WebKit dmabuf without validated shared-surface contract"
reject_log 'webkitgpusmoke: gpu-contract .*ok=0' \
    "WebKit GPU contract audit failure"
reject_log 'webkitgpusmoke: gpu-contract .*env_d3d12_native_present_disabled=1' \
    "WebKit D3D12 native present disabled"
reject_log 'webkitgpusmoke: gpu-contract .*env_d3d12_copy_export=1' \
    "WebKit D3D12 copy-export fallback"
if grep -Eq 'webkit_gpu_policy .*effective_accel=1' "${LOG}"; then
    require_webkit_enabled_artifact_contract_matrix
fi
if grep -Eq 'd3d12_present_errno=95|present_errno=95|(^|[[:space:]])(commit_errno|commit_status|d3d12_.*commit_(errno|status))[ =]95($|[[:space:]])|present_id=0|completed=0|callbacks_blocked=1|releases_blocked=1|d3d12_present_source_(commit_rejected_eopnotsupp|no_present_id_completed|gpu_p_or_dda_transport_absent|no_gpu_p_or_dda_display_bind|same_frame_callbacks_blocked|same_frame_releases_blocked)[ =]1' "${LOG}" &&
   ! grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${LOG}"; then
    require_fail_closed_gpup_dda_diagnostic_file "${LOG}" "WebKit current-run D3D12 present"
    fail "fail-closed present: missing GPU-P/DDA resource-scanout-bind dependency; WebKit remains gated off until nonzero present_id, completed>=present_id, callbacks/releases, no readback, strict FPS, and backend_opengl_submit=1 exist"
fi
if grep -Eq 'webkit_gpu_policy .*effective_accel=1' "${LOG}" &&
   grep -Eq 'd3d12sharedsmoke: (shareobjects failed|CreateSharedHandle\(resource\) failed|present validation failed|GPU present evidence (file missing or unreadable|missing/unchanged)|could not remove stale present evidence)|cat: cannot open /tmp/wlcomp-d3d12-present' "${LOG}"; then
    fail "WebKit acceleration was claimed after a failed D3D12 shared-surface preflight"
fi
if grep -Eq 'd3d12sharedsmoke: runtime CreateSharedHandle\(resource\) ok' "${LOG}" &&
   grep -Eq 'd3d12sharedsmoke: runtime CreateSharedHandle\(fence\) ok' "${LOG}" &&
   ! grep -Eq 'd3d12sharedsmoke: native present evidence ok path=d3d12-dxg-present-source-display-handoff' "${LOG}" &&
   grep -Eq 'GPU present evidence (file missing or unreadable|missing/unchanged)|cat: cannot open /tmp/wlcomp-d3d12-present|Open.*[Ff]ence.*(failed|0x80070057)|0x80070057' "${LOG}"; then
    fail "fail-closed state: resource/fence export succeeded, but native-present evidence is missing or incomplete; WebKit remains gated off"
fi
reject_log 'xv6-mesa: d3d12 native present unavailable; refusing DRI software/readback present' \
    "WebKit D3D12 DRI software/readback fallback"
reject_log 'd3d12_display_handoff_implemented=0|d3d12_native_present_unimplemented=1|d3d12_present_errno=[1-9][0-9]*|callbacks_blocked=1|releases_blocked=1' \
    "incomplete D3D12 native present path"
reject_log 'd3d12_display_handoff_implemented=0|d3d12_(cpu_readback|cpu_mapping|cpu_copy|readback|present_state_only|present_fence_only|present_import_only|present_open_only|present_callback_only|present_release_only|present_errno)=[1-9][0-9]*' \
    "WebKit D3D12 CPU/readback or partial-present fallback"

echo "hyperv-webkit-gpu-validate: passed (${LOG})"
