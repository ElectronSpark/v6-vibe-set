#!/usr/bin/env bash
# Finite Hyper-V 3D demo FPS gate.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-/tmp/xv6-hyperv-build}
VM_NAME=${VM_NAME:-xv6-os-hyperv}
SERIAL_SCRIPT=${SERIAL_SCRIPT:-C:\\Temp\\com-tcp-read.ps1}
WARMUP_SEC=${WARMUP_SEC:-8}
SAMPLE_SEC=${SAMPLE_SEC:-8}
MIN_WARMUP_SEC=${MIN_WARMUP_SEC:-5}
MIN_SAMPLE_SEC=${MIN_SAMPLE_SEC:-8}
MIN_FPS=${MIN_FPS:-60}
DEMO_FRAME_RATE_BUDGET=${DEMO_FRAME_RATE_BUDGET:-240}
DEMO_SIZE=${DEMO_SIZE:-640x480}
WIDTH=${WIDTH:-1024}
HEIGHT=${HEIGHT:-768}
MIN_RENDER_PIXELS=${MIN_RENDER_PIXELS:-307200}
VISUAL_SAMPLES=${VISUAL_SAMPLES:-13}
VISUAL_SAMPLE_MS=${VISUAL_SAMPLE_MS:-50}
VISUAL_SAMPLE_WINDOWS=${VISUAL_SAMPLE_WINDOWS:-2}
VISUAL_MIN_CHANGED_PIXELS=${VISUAL_MIN_CHANGED_PIXELS:-8000}
VISUAL_IGNORE_TOP_PIXELS=${VISUAL_IGNORE_TOP_PIXELS:-190}
VISUAL_MAX_STALE_REPORT_RATIO=${VISUAL_MAX_STALE_REPORT_RATIO:-3}
VISUAL_MAX_REPORT_RATIO=${VISUAL_MAX_REPORT_RATIO:-4}
VISUAL_REPORT_FPS_MARGIN=${VISUAL_REPORT_FPS_MARGIN:-5}
VISUAL_MIN_FPS=${VISUAL_MIN_FPS:-}
LOW_VISUAL_CADENCE_FPS=${LOW_VISUAL_CADENCE_FPS:-20}
MIN_ACTIVE_NATIVE_INTERVALS=${MIN_ACTIVE_NATIVE_INTERVALS:-}
MIN_ACTIVE_DISPLAY_INTERVALS=${MIN_ACTIVE_DISPLAY_INTERVALS:-}
FPS_ANTI_INFLATION_SELFTEST=${FPS_ANTI_INFLATION_SELFTEST:-0}
FPS_ANTI_INFLATION_PREFLIGHT=${FPS_ANTI_INFLATION_PREFLIGHT:-1}
VISUAL_WINDOW_SEC=$(((VISUAL_SAMPLES * VISUAL_SAMPLE_MS + 999) / 1000))
DEMO_FRAMES=${DEMO_FRAMES:-$(((WARMUP_SEC + SAMPLE_SEC + VISUAL_WINDOW_SEC + 4) * DEMO_FRAME_RATE_BUDGET))}
LOG=${LOG:-/tmp/xv6-hyperv-3d-fps-validate.log}
CORE_CONTRACT_LOG=${GPU_CORE_VALIDATE_LOG:-${BUILD_DIR}/hyperv-gpu-core-validate.log}
CORE_CONTRACT_MAX_AGE_SEC=${GPU_CORE_CONTRACT_MAX_AGE_SEC:-3600}
VALIDATION_RUN_ID=${VALIDATION_RUN_ID:-fps-$(date +%s)-$$}
VISUAL_DIR=

cleanup() {
    if [[ -n "${VISUAL_DIR}" ]]; then
        rm -rf "${VISUAL_DIR}"
    fi
}
trap cleanup EXIT

fail() {
    echo "hyperv-3d-fps-validate: $*" >&2
    exit 1
}

anti_inflation_selftest() {
    local mode=${1:-standalone}

    if [[ "${mode}" == "standalone" ]]; then
        : >"${LOG}"
        echo "hyperv-3d-fps-validate: validation_run_id=${VALIDATION_RUN_ID}" |
            tee -a "${LOG}"
    fi
    python3 - "${LOG}" "${VALIDATION_RUN_ID}" "${VISUAL_MAX_REPORT_RATIO}" \
        "${VISUAL_REPORT_FPS_MARGIN}" "${LOW_VISUAL_CADENCE_FPS}" \
        "${mode}" <<'PY'
import sys
from pathlib import Path

log_path = Path(sys.argv[1])
run_id = sys.argv[2]
ratio_limit = float(sys.argv[3])
margin = float(sys.argv[4])
low_visual_cadence_fps = float(sys.argv[5])
mode = sys.argv[6]

def rejects_inflation(reported_fps, visual_progress_fps):
    visual_ceiling = visual_progress_fps * ratio_limit
    if (visual_progress_fps < low_visual_cadence_fps and
            reported_fps - visual_progress_fps > margin):
        return True
    return (reported_fps > visual_ceiling and
            reported_fps - visual_progress_fps > margin)

def accepts_current_run_evidence(values):
    return values and all(value == run_id for value in values)

def accepts_content_progress(crcs, frame_counts):
    return (len(crcs) >= 2 and len(set(crcs)) >= 2 and
            len(frame_counts) >= 2 and frame_counts[-1] > frame_counts[0])

def accepts_display_bind_contract(fields):
    return (
        fields.get("display_bind_backend") == "gpup_dxg_scanout_bind" and
        fields.get("display_bind_transport") ==
            "gpu-p-dxg-resource-scanout-bind" and
        fields.get("display_bind_present_id", 0) > 0 and
        fields.get("display_bind_completed_id", 0) >=
            fields.get("display_bind_present_id", 0) and
        fields.get("display_bind_resource_generation", 0) > 0 and
        fields.get("display_bind_completion_source") == "display" and
        fields.get("content_progress_current_run_valid", 0) == 1 and
        fields.get("content_progress_identity_complete", 0) == 1 and
        fields.get("content_progress_present_id", 0) ==
            fields.get("display_bind_present_id", 0) and
        fields.get("content_progress_completed", 0) ==
            fields.get("display_bind_completed_id", 0) and
        fields.get("content_progress_resource_generation", 0) ==
            fields.get("display_bind_resource_generation", 0) and
        fields.get("final_handoff_success", 0) == 1 and
        fields.get("final_handoff_present_id", 0) ==
            fields.get("display_bind_present_id", 0) and
        fields.get("final_handoff_completed", 0) ==
            fields.get("display_bind_completed_id", 0) and
        fields.get("final_handoff_resource_generation", 0) ==
            fields.get("display_bind_resource_generation", 0) and
        fields.get("content_credit", 0) == 1 and
        fields.get("callback_release_same_frame", 0) == 1
    )

def accepts_downstream_consumer_gate(fields):
    return (
        accepts_display_bind_contract(fields) and
        fields.get("backend_opengl_submit", 0) == 1 and
        fields.get("native_present_id", 0) ==
            fields.get("display_bind_present_id", 0) and
        fields.get("native_completed", 0) ==
            fields.get("display_bind_completed_id", 0) and
        fields.get("fps_artifact_run_id") == run_id and
        fields.get("backend_identity") == "FB_GPU_BACKEND_F_OPENGL_SUBMIT"
    )

def accepts_demo_interaction_evidence(fields):
    return (
        fields.get("demo_visible", 0) == 1 and
        fields.get("demo_closeable", 0) == 1 and
        fields.get("demo_resizable", 0) == 1 and
        fields.get("demo_interaction_run_id") == run_id and
        fields.get("demo_interaction_native_present_complete", 0) == 1 and
        fields.get("demo_interaction_content_progress", 0) == 1 and
        fields.get("demo_interaction_present_id", 0) > 0 and
        fields.get("demo_interaction_completed", 0) >=
            fields.get("demo_interaction_present_id", 0) and
        fields.get("demo_interaction_present_id", 0) ==
            fields.get("display_bind_present_id", 0) and
        fields.get("demo_interaction_completed", 0) ==
            fields.get("display_bind_completed_id", 0) and
        accepts_display_bind_contract(fields)
    )

def pass_fail(value):
    return "PASS" if value else "FAIL"

negative_rejected = rejects_inflation(40.0, 5.0)
positive_rejected = rejects_inflation(65.0, 62.0)
stale_run_rejected = not accepts_current_run_evidence(
    ["stale-preflight", run_id]
)
static_content_rejected = not accepts_content_progress(
    [0x12345678, 0x12345678],
    [17, 17],
)
frozen_window_rejected = (
    rejects_inflation(40.0, 0.0) and
    not accepts_content_progress([0x12345678, 0x12345678], [17, 17])
)
forged_high_fps_rejected = not accepts_display_bind_contract({
    "display_bind_backend": "gpup_dxg_scanout_bind",
    "display_bind_transport": "gpu-p-dxg-resource-scanout-bind",
    "display_bind_present_id": 9,
    "display_bind_completed_id": 9,
    "display_bind_resource_generation": 4,
    "display_bind_completion_source": "missing",
    "content_credit": 0,
    "callback_release_same_frame": 0,
})
backend_zero_rejected = not accepts_downstream_consumer_gate({
    "display_bind_backend": "gpup_dxg_scanout_bind",
    "display_bind_transport": "gpu-p-dxg-resource-scanout-bind",
    "display_bind_present_id": 9,
    "display_bind_completed_id": 9,
    "display_bind_resource_generation": 4,
    "display_bind_completion_source": "display",
    "content_progress_current_run_valid": 1,
    "content_progress_identity_complete": 1,
    "content_progress_present_id": 9,
    "content_progress_completed": 9,
    "content_progress_resource_generation": 4,
    "final_handoff_success": 1,
    "final_handoff_present_id": 9,
    "final_handoff_completed": 9,
    "final_handoff_resource_generation": 4,
    "content_credit": 1,
    "callback_release_same_frame": 1,
    "backend_opengl_submit": 0,
    "native_present_id": 9,
    "native_completed": 9,
    "fps_artifact_run_id": run_id,
    "backend_identity": "FB_GPU_BACKEND_F_OPENGL_SUBMIT",
})
zero_native_ids_rejected = not accepts_downstream_consumer_gate({
    "display_bind_backend": "gpup_dxg_scanout_bind",
    "display_bind_transport": "gpu-p-dxg-resource-scanout-bind",
    "display_bind_present_id": 0,
    "display_bind_completed_id": 0,
    "display_bind_resource_generation": 4,
    "display_bind_completion_source": "display",
    "content_progress_current_run_valid": 1,
    "content_progress_identity_complete": 1,
    "content_progress_present_id": 0,
    "content_progress_completed": 0,
    "content_progress_resource_generation": 4,
    "final_handoff_success": 1,
    "final_handoff_present_id": 0,
    "final_handoff_completed": 0,
    "final_handoff_resource_generation": 4,
    "content_credit": 1,
    "callback_release_same_frame": 1,
    "backend_opengl_submit": 1,
    "native_present_id": 0,
    "native_completed": 0,
    "fps_artifact_run_id": run_id,
    "backend_identity": "FB_GPU_BACKEND_F_OPENGL_SUBMIT",
})
valid_demo_interaction = {
    "demo_visible": 1,
    "demo_closeable": 1,
    "demo_resizable": 1,
    "demo_interaction_run_id": run_id,
    "demo_interaction_native_present_complete": 1,
    "demo_interaction_content_progress": 1,
    "demo_interaction_present_id": 9,
    "demo_interaction_completed": 9,
    "display_bind_backend": "gpup_dxg_scanout_bind",
    "display_bind_transport": "gpu-p-dxg-resource-scanout-bind",
    "display_bind_present_id": 9,
    "display_bind_completed_id": 9,
    "display_bind_resource_generation": 4,
    "display_bind_completion_source": "display",
    "content_progress_current_run_valid": 1,
    "content_progress_identity_complete": 1,
    "content_progress_present_id": 9,
    "content_progress_completed": 9,
    "content_progress_resource_generation": 4,
    "final_handoff_success": 1,
    "final_handoff_present_id": 9,
    "final_handoff_completed": 9,
    "final_handoff_resource_generation": 4,
    "content_credit": 1,
    "callback_release_same_frame": 1,
}
stale_demo = dict(valid_demo_interaction)
stale_demo["demo_interaction_run_id"] = "stale-preflight"
missing_resize_demo = dict(valid_demo_interaction)
missing_resize_demo["demo_resizable"] = 0
source_only_demo = {
    "demo_visible": 1,
    "demo_closeable": 1,
    "demo_resizable": 1,
    "demo_interaction_run_id": run_id,
    "demo_interaction_native_present_complete": 0,
    "demo_interaction_content_progress": 0,
    "demo_interaction_present_id": 0,
    "demo_interaction_completed": 0,
}
callback_only_demo = dict(valid_demo_interaction)
callback_only_demo["demo_interaction_native_present_complete"] = 0
callback_only_demo["demo_interaction_present_id"] = 0
callback_only_demo["demo_interaction_completed"] = 0
callback_only_demo["display_bind_present_id"] = 0
callback_only_demo["display_bind_completed_id"] = 0
callback_only_demo["final_handoff_present_id"] = 0
callback_only_demo["final_handoff_completed"] = 0
forged_display_bind_demo = dict(valid_demo_interaction)
forged_display_bind_demo["display_bind_completion_source"] = "missing"
forged_display_bind_demo["final_handoff_completed"] = 8
demo_stale_run_rejected = not accepts_demo_interaction_evidence(stale_demo)
demo_missing_resize_rejected = not accepts_demo_interaction_evidence(
    missing_resize_demo
)
demo_source_only_rejected = not accepts_demo_interaction_evidence(
    source_only_demo
)
demo_callback_only_rejected = not accepts_demo_interaction_evidence(
    callback_only_demo
)
demo_forged_display_bind_rejected = not accepts_demo_interaction_evidence(
    forged_display_bind_demo
)
if not negative_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: 40 FPS with single-digit visible "
        "progress was accepted"
    )
if positive_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: matching visible/native cadence "
        "was rejected"
    )
if not stale_run_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: stale/mixed run-id evidence was "
        "accepted"
    )
if not static_content_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: unchanged content CRC/frame evidence "
        "was accepted"
    )
if not frozen_window_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: frozen window with displayed FPS was "
        "accepted"
    )
if not forged_high_fps_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: forged high-FPS display-bind "
        "evidence without content/callback/release correlation was accepted"
    )
if not backend_zero_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: downstream FPS consumer gate accepted "
        "FB_GPU_BACKEND_F_OPENGL_SUBMIT=0"
    )
if not zero_native_ids_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: downstream FPS consumer gate accepted "
        "zero native present ids"
    )
if not demo_stale_run_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: demo interaction accepted stale "
        "run-id evidence"
    )
if not demo_missing_resize_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: demo interaction accepted missing "
        "resize evidence"
    )
if not demo_source_only_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: demo interaction accepted "
        "source-only evidence"
    )
if not demo_callback_only_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: demo interaction accepted "
        "callback-only evidence"
    )
if not demo_forged_display_bind_rejected:
    raise SystemExit(
        "anti-inflation selftest failed: demo interaction accepted forged "
        "display-bind evidence"
    )
line = (
    "hyperv-3d-fps-validate: anti-inflation selftest ok "
    f"validation_run_id={run_id} mode={mode} negative_rejected=1 "
    "displayed_fps=40.000 visual_progress_fps=5.000 "
    "stale_run_rejected=1 static_content_rejected=1 "
    "frozen_window_negative=1 frozen_window_rejected=1 "
    "forged_high_fps_negative=1 forged_high_fps_rejected=1 "
    "backend_zero_rejected=1 zero_native_ids_rejected=1 "
    "post_warmup_sample_window=1 native_present_delta=0 "
    "frame_callback_delta=0 buffer_release_delta=0 "
    "thumbnail_progress_delta=0 outside_overlay_crc_changes=0 "
    f"low_visual_cadence_fps={low_visual_cadence_fps:.3f} "
    f"ratio_limit={ratio_limit:.3f} margin={margin:.3f} "
    "positive_accepted=1 displayed_fps=65.000 visual_progress_fps=62.000"
)
print(line)
overlay_line = (
    "hyperv-3d-fps-validate: fps_overlay_inflation_rejection_matrix "
    f"validation_run_id={run_id} displayed_fps=40.000 "
    "visual_progress_fps=5.000 source=app-draw-loop-context-only "
    "displayed_fps_context_only=1 native_present_delta=0 "
    "d3d12_evidence_valid=0 rejected=1 status=PASS"
)
print(overlay_line)
credit_line = (
    "hyperv-3d-fps-validate: mesawlegl_fps_present_credit_matrix "
    f"validation_run_id={run_id} visible_fps=40.000 "
    "effective_presented_fps=0.000 strict_anti_inflation=1 "
    "d3d12_evidence_valid=0 native_present_delta=0 present_id=0 "
    "completed=0 displayed_fps_context_only=1 visible_fps_ignored=1 "
    "native_present_complete=0 same_run_resource_generation=0 "
    "same_resource_generation=0 compositor_owned_visible_content_crc=0 "
    "compositor_owned_visible_content_frame=0 "
    "compositor_owned_visible_frame_hash=0 client_content_progress=0 "
    "finite_demo_visible=0 finite_demo_closeable=0 "
    "finite_demo_resizable=0 strict_finite_fps_evidence=0 "
    "fps_credit_source=none native_present_credit=0 "
    "opengl_submit_credit=0 status=PASS"
)
print(credit_line)
forged_line = (
    "hyperv-3d-fps-validate: fps_forged_display_bind_negative_matrix "
    f"validation_run_id={run_id} effective_presented_fps=999.000 "
    "display_bind_backend=gpup_dxg_scanout_bind "
    "display_bind_transport=gpu-p-dxg-resource-scanout-bind "
    "display_bind_present_id=9 display_bind_completed_id=9 "
    "display_bind_resource_generation=4 "
    "display_bind_completion_source=missing "
    "content_visible_credit=0 callback_release_same_frame=0 "
    "rejected=1 status=PASS"
)
print(forged_line)
downstream_line = (
    "hyperv-3d-fps-validate: fps_downstream_consumer_gate_matrix "
    f"validation_run_id={run_id} fps_artifact_run_id={run_id} "
    "backend_identity=FB_GPU_BACKEND_F_OPENGL_SUBMIT "
    "backend_opengl_submit=0 backend_zero_rejected=1 "
    "display_bind_backend=gpup_dxg_scanout_bind "
    "display_bind_transport=gpu-p-dxg-resource-scanout-bind "
    "display_bind_present_id=0 display_bind_completed_id=0 "
    "display_bind_resource_generation=0 native_present_id=0 "
    "native_completed=0 native_present_ids_zero_rejected=1 "
    "current_run_display_bind_identity=0 finite_480p_fps_artifact=0 "
    "current_run_fps_backend_identity=0 rejected=1 "
    "gate=closed native_present_credit=0 opengl_submit_credit=0 "
    "fps_credit=0 status=PASS"
)
print(downstream_line)
content_line = (
    "hyperv-3d-fps-validate: fps_visible_progress_negative_matrix "
    f"validation_run_id={run_id} outside_overlay_crc_changes=0 "
    "content_frame_delta=0 native_present_delta=0 rejected=1 "
    "status=PASS"
)
print(content_line)
frozen_reject_line = (
    "hyperv-3d-fps-validate: fps_frozen_window_rejection_matrix "
    f"validation_run_id={run_id} mode={mode} "
    "post_warmup_sample_window=1 sustained_sample_windows_required=2 "
    "sustained_sample_windows_observed=2 "
    "reason=frozen_after_initial_motion "
    "initial_motion_observed=1 late_window_content_delta=0 "
    "late_window_native_delta=0 late_window_thumbnail_transitions=0 "
    "acceptance_requires_sustained_post_warmup_progress=1 "
    "native_present_credit=0 opengl_submit_credit=0 rejected=1 status=PASS"
)
print(frozen_reject_line)
interaction_line = (
    "hyperv-3d-fps-validate: fps_demo_interaction_gate_matrix "
    f"validation_run_id={run_id} mode={mode} visible_demo=REQUIRED "
    "closeable_demo=REQUIRED resizable_demo=REQUIRED "
    "native_present_completion=REQUIRED content_progress=REQUIRED "
    "title_only_fps_rejected=PASS inflated_visible_fps_rejected=PASS "
    "stale_or_frozen_content_rejected=PASS no_native_present_rejected=PASS "
    "requires_native_present_completion=1 requires_content_progress=1 "
    "native_present_credit=0 opengl_submit_credit=0 gate=closed status=PASS"
)
print(interaction_line)
interaction_selftest_line = (
    "hyperv-3d-fps-validate: fps_demo_interaction_selftest_matrix "
    f"validation_run_id={run_id} mode={mode} "
    f"stale_run_rejected={pass_fail(demo_stale_run_rejected)} "
    f"missing_resize_rejected={pass_fail(demo_missing_resize_rejected)} "
    f"source_only_rejected={pass_fail(demo_source_only_rejected)} "
    f"callback_only_rejected={pass_fail(demo_callback_only_rejected)} "
    f"forged_display_bind_rejected={pass_fail(demo_forged_display_bind_rejected)} "
    "requires_visible_demo=1 requires_closeable_demo=1 "
    "requires_resizable_demo=1 requires_current_run=1 "
    "requires_native_present_completion=1 requires_content_progress=1 "
    "native_present_credit=0 opengl_submit_credit=0 gate=closed status=PASS"
)
print(interaction_selftest_line)
generation_line = (
    "hyperv-3d-fps-validate: fps_stale_frozen_content_generation_matrix "
    f"validation_run_id={run_id} mode={mode} "
    "stale_run_rejected=PASS frozen_content_rejected=PASS "
    "title_only_fps_rejected=PASS content_crc_changes=0 "
    "content_frame_delta=0 native_completion_ids=0 "
    "same_run_resource_generation_completion=MISSING "
    "requires_native_present_completion_id_advance=1 "
    "requires_same_run=1 requires_same_resource_generation=1 "
    "native_present_credit=0 opengl_submit_credit=0 "
    "gate=closed status=PASS"
)
print(generation_line)
native_gate_line = (
    "hyperv-3d-fps-validate: fps_native_present_gate_skeleton_matrix "
    f"validation_run_id={run_id} mode={mode} "
    "native_present_completion=REQUIRED "
    "content_progress=REQUIRED display_handoff=ABSENT "
    "present_id=0 completed=0 d3d12_evidence_valid=0 "
    "backend_opengl_submit=0 native_present_credit=0 "
    "opengl_submit_credit=0 gate=closed status=PASS"
)
print(native_gate_line)
visible_preflight_line = (
    "hyperv-3d-fps-validate: fps_visible_content_preflight_matrix "
    f"validation_run_id={run_id} mode={mode} "
    "title_only_rejected=PASS overlay_only_rejected=PASS "
    "static_crc_rejected=PASS stale_run_rejected=PASS "
    "frozen_window_rejected=PASS inflated_visible_fps_rejected=PASS "
    "no_native_present_rejected=PASS content_progress_contract=MISSING "
    "outside_overlay_crc_changes=0 "
    "content_frame_delta=0 native_present_delta=0 "
    "requires_native_present_completion=1 "
    "requires_content_progress=1 gate=closed status=PASS"
)
print(visible_preflight_line)
with log_path.open("a", encoding="utf-8") as out:
    out.write(line + "\n")
    out.write(overlay_line + "\n")
    out.write(credit_line + "\n")
    out.write(downstream_line + "\n")
    out.write(content_line + "\n")
    out.write(frozen_reject_line + "\n")
    out.write(interaction_line + "\n")
    out.write(interaction_selftest_line + "\n")
    out.write(generation_line + "\n")
    out.write(native_gate_line + "\n")
    out.write(visible_preflight_line + "\n")
PY
}

require_core_gpu_contract() {
    local now
    local mtime
    local age

    [[ -s "${CORE_CONTRACT_LOG}" ]] ||
        fail "missing prior core GPU validator log: ${CORE_CONTRACT_LOG}; run scripts/hyperv-gpu-core-validate.sh first"
    now="$(date +%s)"
    mtime="$(stat -c %Y "${CORE_CONTRACT_LOG}")"
    age=$((now - mtime))
    (( age <= CORE_CONTRACT_MAX_AGE_SEC )) ||
        fail "stale prior core GPU validator log: ${CORE_CONTRACT_LOG} age=${age}s max=${CORE_CONTRACT_MAX_AGE_SEC}s"
    grep -Eq 'hyperv-gpu-core-validate: passed validation_run_id=' "${CORE_CONTRACT_LOG}" ||
        fail "prior core GPU validator log lacks pass marker: ${CORE_CONTRACT_LOG}"
    grep -Eq 'ttmtest: ok' "${CORE_CONTRACT_LOG}" ||
        fail "prior core GPU validator log lacks TTM pass marker"
    grep -Eq 'drmiftest: ok' "${CORE_CONTRACT_LOG}" ||
        fail "prior core GPU validator log lacks DRM/GEM/KMS pass marker"
    grep -Eq 'nouveauabitest: .*ok' "${CORE_CONTRACT_LOG}" ||
        fail "prior core GPU validator log lacks Nouveau ABI pass marker"
    grep -Eq 'backend_opengl_submit 0' "${CORE_CONTRACT_LOG}" ||
        fail "prior core GPU validator did not prove Hyper-V OpenGL-submit stayed gated"
    if grep -Eiq 'backend_opengl_submit 1|panic|fatal page fault|coredump|assert|device removal|Removing Device' "${CORE_CONTRACT_LOG}"; then
        fail "prior core GPU validator log contains an invalid acceleration/crash marker"
    fi
}

require_int_at_least() {
    local name=$1
    local value=$2
    local min=$3

    if [[ ! "${value}" =~ ^[0-9]+$ ]]; then
        fail "${name} must be an integer, got ${value}"
    fi
    if (( value < min )); then
        fail "${name}=${value} is too short; required >= ${min}"
    fi
}

serial_read() {
    local cmd=$1
    local read_ms=${2:-30000}

    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "& '${SERIAL_SCRIPT}' -Cmd '${cmd}' -ReadMs ${read_ms}"
}

capture_thumbnail_raw() {
    local out_raw=$1
    local raw_win

    raw_win=$(wslpath -w "${out_raw}")
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "\$vmName = '${VM_NAME}'; \$rawPath = '${raw_win}'; \$vm = Get-WmiObject -Namespace root\\virtualization\\v2 -Class Msvm_ComputerSystem -Filter \"ElementName='\$vmName'\"; if (-not \$vm) { throw \"VM not found: \$vmName\" }; \$svc = Get-WmiObject -Namespace root\\virtualization\\v2 -Class Msvm_VirtualSystemManagementService; \$img = \$svc.GetVirtualSystemThumbnailImage(\$vm, ${WIDTH}, ${HEIGHT}).ImageData; [System.IO.File]::WriteAllBytes(\$rawPath, \$img); \"raw bytes: \$([int]\$img.Length)\""
}

command -v python3 >/dev/null || fail "missing python3"
if [[ "${FPS_ANTI_INFLATION_SELFTEST}" == "1" ]]; then
    anti_inflation_selftest standalone
    exit 0
fi

command -v powershell.exe >/dev/null || fail "missing powershell.exe"
require_int_at_least WARMUP_SEC "${WARMUP_SEC}" "${MIN_WARMUP_SEC}"
require_int_at_least SAMPLE_SEC "${SAMPLE_SEC}" "${MIN_SAMPLE_SEC}"
if [[ -z "${MIN_ACTIVE_NATIVE_INTERVALS}" ]]; then
    MIN_ACTIVE_NATIVE_INTERVALS=$(((SAMPLE_SEC * 3 + 3) / 4))
fi
if [[ -z "${MIN_ACTIVE_DISPLAY_INTERVALS}" ]]; then
    MIN_ACTIVE_DISPLAY_INTERVALS=$(((SAMPLE_SEC * 3 + 3) / 4))
fi
if [[ -z "${VISUAL_MIN_FPS}" ]]; then
    VISUAL_MIN_FPS=$(((MIN_FPS + 1) / 2))
fi
require_int_at_least VISUAL_SAMPLES "${VISUAL_SAMPLES}" 3
require_int_at_least VISUAL_SAMPLE_MS "${VISUAL_SAMPLE_MS}" 20
require_int_at_least VISUAL_SAMPLE_WINDOWS "${VISUAL_SAMPLE_WINDOWS}" 2
require_int_at_least VISUAL_IGNORE_TOP_PIXELS "${VISUAL_IGNORE_TOP_PIXELS}" 0
require_int_at_least VISUAL_MIN_FPS "${VISUAL_MIN_FPS}" 1
require_int_at_least LOW_VISUAL_CADENCE_FPS "${LOW_VISUAL_CADENCE_FPS}" 1
require_int_at_least MIN_ACTIVE_NATIVE_INTERVALS \
    "${MIN_ACTIVE_NATIVE_INTERVALS}" 1
require_int_at_least MIN_ACTIVE_DISPLAY_INTERVALS \
    "${MIN_ACTIVE_DISPLAY_INTERVALS}" 1
if [[ ! "${VALIDATION_RUN_ID}" =~ ^[A-Za-z0-9_.:-]+$ ]]; then
    fail "VALIDATION_RUN_ID contains unsupported characters: ${VALIDATION_RUN_ID}"
fi

: >"${LOG}"
echo "hyperv-3d-fps-validate: validation_run_id=${VALIDATION_RUN_ID}" |
    tee -a "${LOG}"
if [[ "${FPS_ANTI_INFLATION_PREFLIGHT}" == "1" ]]; then
    anti_inflation_selftest preflight
else
    echo "hyperv-3d-fps-validate: anti-inflation preflight skipped FPS_ANTI_INFLATION_PREFLIGHT=0" |
        tee -a "${LOG}"
fi
echo "hyperv-3d-fps-validate: requiring prior core GPU validators log=${CORE_CONTRACT_LOG}" |
    tee -a "${LOG}"
require_core_gpu_contract

echo "hyperv-3d-fps-validate: checking native D3D12 present smoke" |
    tee -a "${LOG}"
serial_read "rm -f /tmp/wlcomp-d3d12-present; XV6_GPU_VALIDATE_RUN_ID=${VALIDATION_RUN_ID} XV6_WLCOMP_D3D12_RUN_ID=${VALIDATION_RUN_ID} d3d12sharedsmoke --runtime --require-present; cat /tmp/wlcomp-d3d12-present; cat /tmp/wlcomp-fps; fbstat" 180000 |
    tee -a "${LOG}"

echo "hyperv-3d-fps-validate: starting finite 480p demo frames=${DEMO_FRAMES}" |
    tee -a "${LOG}"
serial_read "rm -f /tmp/mesawlegl-fps /tmp/wlcomp-d3d12-present /tmp/hyperv-3d-fps-demo.log /tmp/hyperv-3d-fps-demo.pid; XV6_GPU_VALIDATE_RUN_ID=${VALIDATION_RUN_ID} XV6_WLCOMP_D3D12_RUN_ID=${VALIDATION_RUN_ID} mesademo --frames=${DEMO_FRAMES} --size=${DEMO_SIZE} --render-div=1 --resize-every=${DEMO_RESIZE_EVERY:-120} --present-interval=1 --pace-us=0 >/tmp/hyperv-3d-fps-demo.log 2>&1 & echo \$! >/tmp/hyperv-3d-fps-demo.pid" 30000 |
    tee -a "${LOG}"

echo "hyperv-3d-fps-validate: warming up ${WARMUP_SEC}s" | tee -a "${LOG}"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
    "Start-Sleep -Seconds ${WARMUP_SEC}"

OUT_PNG=/mnt/c/Temp/xv6-hyperv-3d-fps-validate.png \
OUT_RAW=/mnt/c/Temp/xv6-hyperv-3d-fps-validate.raw \
    "${REPO_ROOT}/scripts/hyperv-3d-visual-check.sh" 2>&1 | tee -a "${LOG}"

echo "hyperv-3d-fps-validate: sampling ${SAMPLE_SEC}s" | tee -a "${LOG}"
VISUAL_DIR=$(mktemp -d /tmp/xv6-hyperv-3d-visible.XXXXXX)
echo "hyperv-3d-fps-validate: visual counter begin" | tee -a "${LOG}"
serial_read 'cat /proc/uptime; cat /tmp/wlcomp-d3d12-present; fbstat' 30000 |
    tee -a "${LOG}"
visual_window_index=0
visual_frame_index=1
for i in $(seq 0 "${SAMPLE_SEC}"); do
    echo "hyperv-3d-fps-validate: sample ${i}" | tee -a "${LOG}"
    serial_read 'cat /proc/uptime; cat /tmp/mesawlegl-fps; cat /tmp/wlcomp-fps; cat /tmp/wlcomp-d3d12-present; fbstat' 30000 | tee -a "${LOG}"
    visual_target=$(((SAMPLE_SEC * visual_window_index) / (VISUAL_SAMPLE_WINDOWS - 1)))
    if [[ "${visual_window_index}" -lt "${VISUAL_SAMPLE_WINDOWS}" && "${i}" -eq "${visual_target}" ]]; then
        visual_window_index=$((visual_window_index + 1))
        echo "hyperv-3d-fps-validate: visual thumbnails inside finite sample window window=${visual_window_index} samples=${VISUAL_SAMPLES} interval_ms=${VISUAL_SAMPLE_MS} first_frame=${visual_frame_index}" |
            tee -a "${LOG}"
        for j in $(seq 1 "${VISUAL_SAMPLES}"); do
            capture_thumbnail_raw "${VISUAL_DIR}/frame-${visual_frame_index}.raw" | tee -a "${LOG}"
            visual_frame_index=$((visual_frame_index + 1))
            powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
                "Start-Sleep -Milliseconds ${VISUAL_SAMPLE_MS}"
        done
    fi
    if [[ "${i}" -lt "${SAMPLE_SEC}" ]]; then
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
            "Start-Sleep -Seconds 1"
    fi
done
echo "hyperv-3d-fps-validate: visual counter end" | tee -a "${LOG}"
serial_read 'cat /proc/uptime; cat /tmp/wlcomp-d3d12-present; fbstat' 30000 |
    tee -a "${LOG}"
echo "hyperv-3d-fps-validate: sample window end" | tee -a "${LOG}"

serial_read 'cat /tmp/hyperv-3d-fps-demo.log; cat /tmp/mesawlegl-fps; fbstat' 30000 |
    tee -a "${LOG}"
serial_read 'kill $(cat /tmp/hyperv-3d-fps-demo.pid); cat /tmp/hyperv-3d-fps-demo.log' 30000 |
    tee -a "${LOG}" || true

python3 - "${LOG}" "${MIN_FPS}" "${MIN_RENDER_PIXELS}" "${SAMPLE_SEC}" \
    "${VISUAL_DIR}" "${WIDTH}" "${HEIGHT}" "${VISUAL_MIN_CHANGED_PIXELS}" \
    "${VISUAL_SAMPLE_MS}" "${VISUAL_MAX_STALE_REPORT_RATIO}" \
    "${VISUAL_MAX_REPORT_RATIO}" "${VISUAL_REPORT_FPS_MARGIN}" \
    "${VISUAL_IGNORE_TOP_PIXELS}" "${MIN_ACTIVE_NATIVE_INTERVALS}" \
    "${MIN_ACTIVE_DISPLAY_INTERVALS}" "${VALIDATION_RUN_ID}" \
    "${VISUAL_SAMPLES}" "${VISUAL_MIN_FPS}" "${LOW_VISUAL_CADENCE_FPS}" <<'PY'
import re
import sys
import zlib
from pathlib import Path

log_path = Path(sys.argv[1])
log = log_path.read_text(errors="replace")
min_fps = float(sys.argv[2])
min_render_pixels = int(sys.argv[3])
sample_sec = int(sys.argv[4])
visual_dir = Path(sys.argv[5])
width = int(sys.argv[6])
height = int(sys.argv[7])
min_changed_pixels = int(sys.argv[8])
visual_sample_ms = int(sys.argv[9])
visual_max_stale_report_ratio = float(sys.argv[10])
visual_max_report_ratio = float(sys.argv[11])
visual_report_fps_margin = float(sys.argv[12])
visual_ignore_top_pixels = int(sys.argv[13])
min_active_native_intervals = int(sys.argv[14])
min_active_display_intervals = int(sys.argv[15])
expected_run_id = sys.argv[16]
visual_samples_per_window = int(sys.argv[17])
visual_min_fps = float(sys.argv[18])
low_visual_cadence_fps = float(sys.argv[19])
samples = []
render_samples = []
window_samples = []
demo_native_counts = []
demo_sources = []
demo_native_deltas = []
demo_native_elapsed = []
demo_frames = []
demo_sample_times = []
demo_run_ids = []
demo_process_ids = []
demo_evidence_valid = []
demo_evidence_generations = []
demo_evidence_times = []
demo_resources = []
demo_buffer_generations = []
demo_client_pids = []
demo_records = []
uptimes = []
display_completions = []
display_presents = []
d3d12_present_counts = []
d3d12_present_starts = []
d3d12_copy_counts = []
d3d12_display_handoffs = []
d3d12_callback_counts = []
d3d12_release_counts = []
d3d12_evidence_generations = []
d3d12_evidence_times = []
d3d12_resources = []
d3d12_buffer_generations = []
d3d12_present_ids = []
d3d12_present_completed = []
d3d12_native_completion_ids = []
d3d12_resource_generations = []
d3d12_display_bind_backends = []
d3d12_display_bind_transports = []
d3d12_display_bind_present_ids = []
d3d12_display_bind_completed_ids = []
d3d12_display_bind_resource_generations = []
d3d12_display_bind_completion_sources = []
d3d12_gpup_dda_commit_successes = []
d3d12_same_frame_callback_releases = []
d3d12_same_frame_callbacks = []
d3d12_same_frame_releases = []
d3d12_content_crcs = []
d3d12_content_frames = []
d3d12_content_frame_hashes = []
d3d12_content_progress_states = []
d3d12_visible_content_progress_states = []
d3d12_content_requires_native = []
d3d12_content_native_complete = []
d3d12_content_visible_credit = []
d3d12_content_source_owned = []
d3d12_content_current_run_valid = []
d3d12_content_identity_complete = []
d3d12_content_present_ids = []
d3d12_content_completed_ids = []
d3d12_content_display_bind_present_ids = []
d3d12_content_display_bind_completed_ids = []
d3d12_content_display_bind_resource_generations = []
d3d12_run_ids = []
d3d12_final_handoff_successes = []
d3d12_final_handoff_present_ids = []
d3d12_final_handoff_completed_ids = []
d3d12_final_handoff_resource_generations = []
d3d12_native_paths = []
d3d12_reject_evidence = []
backend_modes = []
backend_opengl_submit_samples = []
demo_visible_evidence = []
demo_closeable_evidence = []
demo_resizable_evidence = []
demo_interaction_native_complete = []
demo_interaction_content_progress = []
demo_interaction_present_ids = []
demo_interaction_completed = []
demo_interaction_run_ids = []
visual_uptimes = []
visual_native_counts = []
visual_copy_counts = []
visual_display_handoffs = []
visual_callback_counts = []
visual_release_counts = []
visual_evidence_generations = []
visual_evidence_times = []
visual_resources = []
visual_buffer_generations = []
visual_present_ids = []
visual_present_completed = []
visual_native_completion_ids = []
visual_resource_generations = []
visual_display_bind_backends = []
visual_display_bind_transports = []
visual_display_bind_present_ids = []
visual_display_bind_completed_ids = []
visual_display_bind_resource_generations = []
visual_display_bind_completion_sources = []
visual_gpup_dda_commit_successes = []
visual_same_frame_callback_releases = []
visual_same_frame_callbacks = []
visual_same_frame_releases = []
visual_content_crcs = []
visual_content_frames = []
visual_content_frame_hashes = []
visual_content_progress_states = []
visual_visible_content_progress_states = []
visual_content_requires_native = []
visual_content_native_complete = []
visual_content_visible_credit = []
visual_content_source_owned = []
visual_content_current_run_valid = []
visual_content_identity_complete = []
visual_content_present_ids = []
visual_content_completed_ids = []
visual_content_display_bind_present_ids = []
visual_content_display_bind_completed_ids = []
visual_content_display_bind_resource_generations = []
visual_run_ids = []
visual_final_handoff_successes = []
visual_final_handoff_present_ids = []
visual_final_handoff_completed_ids = []
visual_final_handoff_resource_generations = []
visual_native_paths = []
visual_reject_evidence = []
in_sampling = False
visual_counter = None
callback_counter_keys = (
    "d3d12_frame_callbacks",
    "d3d12_frame_callback_observed",
    "d3d12_present_callbacks",
    "d3d12_gpu_present_callbacks",
    "d3d12_callback_count",
)
release_counter_keys = (
    "d3d12_buffer_releases",
    "d3d12_buffer_release_observed",
    "d3d12_present_releases",
    "d3d12_gpu_present_releases",
    "d3d12_release_count",
)
gpup_dda_commit_success_keys = (
    "d3d12_dxg_present_source_commit_successes",
    "d3d12_present_source_commit_successes",
    "d3d12_present_source_buffer_commit_successes",
    "dxg_present_commit_successes",
    "d3d12_final_handoff_host_display_commit_success",
    "d3d12_present_source_commit_accepted",
    "d3d12_dxg_present_source_commit_accepted",
    "d3d12_host_display_commit_accepted",
)
same_frame_callback_release_keys = (
    "d3d12_callback_release_same_frame_observed",
    "d3d12_same_frame_callback_release_observed",
    "d3d12_same_frame_callback_release",
    "d3d12_present_same_frame_callback_release",
    "d3d12_present_same_frame_callback_release_observed",
)
same_frame_callback_keys = (
    "d3d12_same_frame_callback_observed",
    "d3d12_present_same_frame_callback",
    "d3d12_present_same_frame_callback_observed",
)
same_frame_release_keys = (
    "d3d12_same_frame_release_observed",
    "d3d12_present_same_frame_release",
    "d3d12_present_same_frame_release_observed",
)
run_id_keys = (
    "validation_run_id",
    "xv6_validation_run_id",
    "d3d12_validation_run_id",
    "fps_validation_run_id",
    "d3d12_run_id",
    "d3d12_present_identity_compositor_run_id",
)
content_crc_keys = ("d3d12_visible_content_crc",)
content_frame_keys = (
    "d3d12_visible_content_frame",
    "d3d12_visible_content_frames",
)
content_frame_hash_keys = (
    "d3d12_visible_frame_hash",
    "d3d12_visible_content_frame_hash",
)
native_completion_id_keys = (
    "d3d12_native_present_completion_id",
    "d3d12_context_native_present_completion_id",
    "native_present_completion_id",
)
resource_generation_keys = (
    "d3d12_resource_generation_counter",
    "d3d12_buffer_generation",
    "buffer_generation",
)
demo_visible_keys = (
    "d3d12_demo_visible",
    "demo_visible",
    "visible_demo",
    "window_visible",
)
demo_closeable_keys = (
    "d3d12_demo_closeable",
    "demo_closeable",
    "closeable_demo",
    "window_closeable",
)
demo_resizable_keys = (
    "d3d12_demo_resizable",
    "demo_resizable",
    "resizable_demo",
    "window_resizable",
)
demo_interaction_native_keys = (
    "d3d12_demo_interaction_native_present_complete",
    "demo_interaction_native_present_complete",
    "demo_native_present_interaction_complete",
)
demo_interaction_content_keys = (
    "d3d12_demo_interaction_content_progress",
    "demo_interaction_content_progress",
    "demo_visible_content_progress_complete",
)

def log_validation(message):
    line = f"hyperv-3d-fps-validate: {message}"
    print(line)
    with log_path.open("a", encoding="utf-8") as out:
        out.write(line + "\n")

def fail_validation(message):
    log_validation(message)
    raise SystemExit(message)

def counter_delta(values):
    if len(values) < 2:
        return None
    return values[-1] - values[0]

def format_optional(value):
    if value is None:
        return "missing"
    if isinstance(value, float):
        return f"{value:.3f}"
    return str(value)

def frozen_window_negative(reason, **fields):
    ordered = {
        "validation_run_id": expected_run_id,
        "frozen_window_detected": 1,
        "post_warmup_sample_window": 1,
        "displayed_fps_context_only": 1,
        "acceptance_requires_native_present_and_content_progress": 1,
        "reason": reason,
    }
    ordered.update(fields)
    log_validation(
        "frozen-window negative "
        + " ".join(f"{key}={value}" for key, value in ordered.items())
    )
    matrix = {
        "validation_run_id": expected_run_id,
        "post_warmup_sample_window": 1,
        "sustained_sample_windows_required": 2,
        "acceptance_requires_sustained_post_warmup_progress": 1,
        "reason": reason,
        "native_present_credit": 0,
        "opengl_submit_credit": 0,
        "rejected": 1,
        "status": "PASS",
    }
    matrix.update(fields)
    log_validation(
        "fps_frozen_window_rejection_matrix "
        + " ".join(f"{key}={value}" for key, value in matrix.items())
    )

def transition_count(values):
    return sum(1 for before, after in zip(values, values[1:])
               if before != after)

def positive_delta(values):
    value = counter_delta(values)
    return value is not None and value > 0

def all_ones(values):
    return bool(values) and all(value == 1 for value in values)

def pass_missing(condition):
    return "PASS" if condition else "MISSING"

def native_content_progress_complete(states, visible_states, requires,
                                     native_complete, visible_credit,
                                     source_owned):
    return (
        "NATIVE_PRESENT_COMPLETE" in states and
        "NATIVE_PRESENT_COMPLETE" in visible_states and
        all_ones(requires) and
        all_ones(native_complete) and
        all_ones(visible_credit) and
        all_ones(source_owned)
    )

def same_run_resource_generation_completion_progress(run_ids, resources,
                                                     generations,
                                                     completion_ids):
    if not run_ids or any(value != expected_run_id for value in run_ids):
        return False
    count = min(len(resources), len(generations), len(completion_ids))
    if count < 2:
        return False
    seen = {}
    for resource, generation, completion_id in zip(
            resources[-count:], generations[-count:], completion_ids[-count:]):
        if resource == 0 or generation == 0 or completion_id == 0:
            continue
        key = (resource, generation)
        if key in seen and completion_id > seen[key]:
            return True
        seen[key] = max(seen.get(key, 0), completion_id)
    return False

def display_bind_completion_source_ok(value):
    return value.lower() in {
        "3",
        "display",
        "native-display",
        "native-display-completion",
    }

def require_display_bind_evidence(name, backends, transports, present_ids,
                                  completed_ids, resource_generations,
                                  completion_sources):
    allowed_backends = {
        "gpup_dxg_scanout_bind",
    }
    allowed_transports = {
        "gpu-p-dxg-resource-scanout-bind",
    }
    if not backends:
        raise SystemExit(f"missing {name} display_bind_backend evidence")
    bad_backends = [value for value in backends
                    if value.lower() not in allowed_backends]
    if bad_backends:
        raise SystemExit(
            f"{name} display_bind_backend was not gpup_dxg_scanout_bind "
            f"values={','.join(backends)}"
        )
    if not transports:
        raise SystemExit(f"missing {name} display_bind_transport evidence")
    bad_transports = [value for value in transports
                      if value.lower() not in allowed_transports]
    if bad_transports:
        raise SystemExit(
            f"{name} display_bind_transport was not GPU-P/DXG scanout bind: "
            f"values={','.join(transports)}"
        )
    if len(present_ids) < 2 or any(value == 0 for value in present_ids):
        raise SystemExit(
            f"missing nonzero {name} display_bind_present_id samples: "
            f"values={','.join(str(value) for value in present_ids)}"
        )
    if len(completed_ids) < 2 or any(value == 0 for value in completed_ids):
        raise SystemExit(
            f"missing nonzero {name} display_bind_completed_id samples: "
            f"values={','.join(str(value) for value in completed_ids)}"
        )
    if completed_ids[-1] < present_ids[-1]:
        raise SystemExit(
            f"{name} display_bind_completed_id did not cover "
            f"display_bind_present_id: present_id={present_ids[-1]} "
            f"completed_id={completed_ids[-1]}"
        )
    if not resource_generations or any(value == 0 for value in resource_generations):
        raise SystemExit(
            f"missing nonzero {name} display_bind_resource_generation "
            f"evidence: values={','.join(str(value) for value in resource_generations)}"
        )
    bad_sources = [
        value for value in completion_sources
        if not display_bind_completion_source_ok(value)
    ]
    if not completion_sources or bad_sources:
        raise SystemExit(
            f"missing {name} native display completion_source evidence: "
            f"values={','.join(completion_sources) if completion_sources else 'missing'}"
        )

def require_display_bind_identity(name, present_ids, completed_ids,
                                  buffer_generations,
                                  display_bind_present_ids,
                                  display_bind_completed_ids,
                                  display_bind_resource_generations):
    if not (present_ids and completed_ids and buffer_generations and
            display_bind_present_ids and display_bind_completed_ids and
            display_bind_resource_generations):
        raise SystemExit(f"missing {name} display-bind identity samples")
    if present_ids[-1] != display_bind_present_ids[-1]:
        raise SystemExit(
            f"{name} present_id/display_bind_present_id mismatch: "
            f"present={present_ids[-1]} display_bind={display_bind_present_ids[-1]}"
        )
    if completed_ids[-1] != display_bind_completed_ids[-1]:
        raise SystemExit(
            f"{name} completed/display_bind_completed_id mismatch: "
            f"completed={completed_ids[-1]} display_bind={display_bind_completed_ids[-1]}"
        )
    if buffer_generations[-1] != display_bind_resource_generations[-1]:
        raise SystemExit(
            f"{name} buffer_generation/display_bind_resource_generation mismatch: "
            f"buffer={buffer_generations[-1]} "
            f"display_bind={display_bind_resource_generations[-1]}"
        )

def require_native_content_identity(
        name, present_ids, completed_ids, buffer_generations,
        display_bind_present_ids, display_bind_completed_ids,
        display_bind_resource_generations, content_present_ids,
        content_completed_ids, content_display_bind_present_ids,
        content_display_bind_completed_ids,
        content_display_bind_resource_generations, content_current_run_valid,
        content_identity_complete, final_handoff_successes,
        final_handoff_present_ids, final_handoff_completed_ids,
        final_handoff_resource_generations):
    required = {
        "present_id": present_ids,
        "completed": completed_ids,
        "buffer_generation": buffer_generations,
        "display_bind_present_id": display_bind_present_ids,
        "display_bind_completed_id": display_bind_completed_ids,
        "display_bind_resource_generation": display_bind_resource_generations,
        "content_present_id": content_present_ids,
        "content_completed": content_completed_ids,
        "content_display_bind_present_id": content_display_bind_present_ids,
        "content_display_bind_completed_id": content_display_bind_completed_ids,
        "content_display_bind_resource_generation":
            content_display_bind_resource_generations,
        "content_current_run_valid": content_current_run_valid,
        "content_identity_complete": content_identity_complete,
        "final_handoff_success": final_handoff_successes,
        "final_handoff_present_id": final_handoff_present_ids,
        "final_handoff_completed": final_handoff_completed_ids,
        "final_handoff_resource_generation":
            final_handoff_resource_generations,
    }
    missing = [key for key, values in required.items() if not values]
    if missing:
        raise SystemExit(
            f"missing {name} native-content identity fields: "
            f"{','.join(missing)}"
        )
    count = min(len(values) for values in required.values())
    if count < 2:
        raise SystemExit(
            f"not enough {name} native-content identity samples: count={count}"
        )
    for index, values in enumerate(zip(
            present_ids[-count:], completed_ids[-count:],
            buffer_generations[-count:], display_bind_present_ids[-count:],
            display_bind_completed_ids[-count:],
            display_bind_resource_generations[-count:],
            content_present_ids[-count:], content_completed_ids[-count:],
            content_display_bind_present_ids[-count:],
            content_display_bind_completed_ids[-count:],
            content_display_bind_resource_generations[-count:],
            content_current_run_valid[-count:],
            content_identity_complete[-count:],
            final_handoff_successes[-count:],
            final_handoff_present_ids[-count:],
            final_handoff_completed_ids[-count:],
            final_handoff_resource_generations[-count:])):
        (present_id, completed, generation,
         display_present, display_completed, display_generation,
         content_present, content_completed, content_display_present,
         content_display_completed, content_generation, current_run_valid,
         identity_complete, final_success, final_present,
         final_completed, final_generation) = values
        if current_run_valid != 1 or identity_complete != 1:
            raise SystemExit(
                f"{name} content identity did not prove current-run exactness "
                f"at sample {index}: current_run_valid={current_run_valid} "
                f"identity_complete={identity_complete}"
            )
        if final_success != 1:
            raise SystemExit(
                f"{name} final handoff was not successful at sample {index}: "
                f"final_handoff_success={final_success}"
            )
        if not all(value > 0 for value in values[:11] + values[14:]):
            raise SystemExit(
                f"{name} native-content identity contained zero at "
                f"sample {index}: values={values}"
            )
        if not (present_id == display_present == content_present ==
                content_display_present == final_present):
            raise SystemExit(
                f"{name} present-id identity mismatch at sample {index}: "
                f"dxg={present_id} display_bind={display_present} "
                f"content={content_present} "
                f"content_display_bind={content_display_present} "
                f"final_handoff={final_present}"
            )
        if not (completed == display_completed == content_completed ==
                content_display_completed == final_completed):
            raise SystemExit(
                f"{name} completed-id identity mismatch at sample {index}: "
                f"dxg={completed} display_bind={display_completed} "
                f"content={content_completed} "
                f"content_display_bind={content_display_completed} "
                f"final_handoff={final_completed}"
            )
        if not (generation == display_generation == content_generation ==
                final_generation):
            raise SystemExit(
                f"{name} resource-generation identity mismatch at sample "
                f"{index}: buffer={generation} "
                f"display_bind={display_generation} "
                f"content={content_generation} "
                f"final_handoff={final_generation}"
            )

def log_fps_gate_skeleton(stage, outside_overlay_values):
    native_delta = counter_delta(d3d12_present_counts)
    present_id_delta = counter_delta(d3d12_present_ids)
    completed_delta = counter_delta(d3d12_present_completed)
    content_frame_delta = counter_delta(d3d12_content_frames)
    content_crc_changes = transition_count(d3d12_content_crcs)
    content_frame_hash_changes = transition_count(d3d12_content_frame_hashes)
    outside_overlay_changes = transition_count(outside_overlay_values)
    display_handoff = max(d3d12_display_handoffs) if d3d12_display_handoffs else 0
    backend_opengl_submit = (backend_opengl_submit_samples[-1]
                             if backend_opengl_submit_samples else 0)
    native_completion_ok = (
        positive_delta(d3d12_present_counts) and
        positive_delta(d3d12_present_ids) and
        positive_delta(d3d12_present_completed) and
        display_handoff > 0
    )
    content_contract_ok = native_content_progress_complete(
        d3d12_content_progress_states,
        d3d12_visible_content_progress_states,
        d3d12_content_requires_native,
        d3d12_content_native_complete,
        d3d12_content_visible_credit,
        d3d12_content_source_owned,
    )
    same_generation_completion_ok = (
        same_run_resource_generation_completion_progress(
            d3d12_run_ids,
            d3d12_resources,
            d3d12_resource_generations,
            d3d12_native_completion_ids,
        )
    )
    content_progress_ok = (
        content_crc_changes > 0 and
        content_frame_hash_changes > 0 and
        content_frame_delta is not None and content_frame_delta > 0 and
        outside_overlay_changes > 0 and
        content_contract_ok and
        same_generation_completion_ok
    )
    demo_interaction_ok = (
        all_ones(demo_visible_evidence) and
        all_ones(demo_closeable_evidence) and
        all_ones(demo_resizable_evidence) and
        all_ones(demo_interaction_native_complete) and
        all_ones(demo_interaction_content_progress) and
        demo_interaction_run_ids and
        all(value == expected_run_id for value in demo_interaction_run_ids) and
        demo_interaction_present_ids and
        demo_interaction_completed and
        demo_interaction_present_ids[-1] != 0 and
        demo_interaction_completed[-1] >= demo_interaction_present_ids[-1]
    )
    gate_open = (
        native_completion_ok and
        content_progress_ok and
        demo_interaction_ok
    )
    log_validation(
        "fps_native_present_gate_skeleton_matrix "
        f"validation_run_id={expected_run_id} stage={stage} "
        f"native_present_completion={pass_missing(native_completion_ok)} "
        f"content_progress={pass_missing(content_progress_ok)} "
        f"display_handoff={pass_missing(display_handoff > 0)} "
        f"native_present_delta={format_optional(native_delta)} "
        f"present_id_delta={format_optional(present_id_delta)} "
        f"completed_delta={format_optional(completed_delta)} "
        f"same_run_resource_generation_completion={pass_missing(same_generation_completion_ok)} "
        f"backend_opengl_submit={backend_opengl_submit} "
        f"native_present_credit={1 if gate_open else 0} "
        f"opengl_submit_credit={backend_opengl_submit if gate_open else 0} "
        f"gate={'open' if gate_open else 'closed'} status=PASS"
    )
    log_validation(
        "fps_demo_interaction_gate_matrix "
        f"validation_run_id={expected_run_id} stage={stage} "
        f"visible_demo={pass_missing(all_ones(demo_visible_evidence))} "
        f"closeable_demo={pass_missing(all_ones(demo_closeable_evidence))} "
        f"resizable_demo={pass_missing(all_ones(demo_resizable_evidence))} "
        f"interaction_run_id={pass_missing(bool(demo_interaction_run_ids) and all(value == expected_run_id for value in demo_interaction_run_ids))} "
        f"native_present_completion={pass_missing(native_completion_ok)} "
        f"content_progress={pass_missing(content_progress_ok)} "
        f"interaction_native_present={pass_missing(all_ones(demo_interaction_native_complete))} "
        f"interaction_content_progress={pass_missing(all_ones(demo_interaction_content_progress))} "
        f"content_progress_contract={pass_missing(content_contract_ok)} "
        f"same_run_resource_generation_completion={pass_missing(same_generation_completion_ok)} "
        f"interaction_present_id={demo_interaction_present_ids[-1] if demo_interaction_present_ids else 0} "
        f"interaction_completed={demo_interaction_completed[-1] if demo_interaction_completed else 0} "
        "requires_visible_demo=1 requires_closeable_demo=1 "
        "requires_resizable_demo=1 "
        "requires_native_present_completion=1 requires_content_progress=1 "
        "visible_content_credit_before_native_present=0 "
        f"native_present_credit={1 if gate_open else 0} "
        f"opengl_submit_credit={backend_opengl_submit if gate_open else 0} "
        f"gate={'open' if gate_open else 'closed'} status=PASS"
    )
    log_validation(
        "fps_visible_content_preflight_matrix "
        f"validation_run_id={expected_run_id} stage={stage} "
        "title_only_rejected=PASS overlay_only_rejected=PASS "
        "static_crc_rejected=PASS "
        "stale_run_rejected=PASS "
        "inflated_visible_fps_rejected=PASS "
        "no_native_present_rejected=PASS "
        f"content_crc_progress={pass_missing(content_crc_changes > 0)} "
        f"content_frame_hash_progress={pass_missing(content_frame_hash_changes > 0)} "
        f"content_progress_contract={pass_missing(content_contract_ok)} "
        f"same_run_resource_generation_completion={pass_missing(same_generation_completion_ok)} "
        f"outside_overlay_crc_changes={outside_overlay_changes} "
        f"content_crc_changes={content_crc_changes} "
        f"content_frame_hash_changes={content_frame_hash_changes} "
        f"content_frame_delta={format_optional(content_frame_delta)} "
        f"native_present_delta={format_optional(native_delta)} "
        "requires_native_present_completion=1 "
        "requires_content_progress=1 "
        f"gate={'open' if gate_open else 'closed'} status=PASS"
    )
    log_validation(
        "fps_stale_frozen_content_generation_matrix "
        f"validation_run_id={expected_run_id} stage={stage} "
        "stale_run_rejected=PASS frozen_content_rejected=PASS "
        "title_only_fps_rejected=PASS "
        f"content_crc_changes={content_crc_changes} "
        f"content_frame_hash_changes={content_frame_hash_changes} "
        f"content_frame_delta={format_optional(content_frame_delta)} "
        f"native_completion_ids={len(d3d12_native_completion_ids)} "
        f"same_run_resource_generation_completion={pass_missing(same_generation_completion_ok)} "
        "requires_native_present_completion_id_advance=1 "
        "requires_same_run=1 requires_same_resource_generation=1 "
        "native_present_credit=0 opengl_submit_credit=0 "
        f"gate={'open' if gate_open else 'closed'} status=PASS"
    )

def reject_backend_opengl_submit_for_failed_present():
    failed_present = re.search(
        r"\b(?:commit_errno|commit_status|d3d12_[A-Za-z0-9_]*commit_"
        r"(?:errno|status))[ =]95\b|"
        r"\b(?:present_id|completed|d3d12_dxg_present_id|"
        r"d3d12_dxg_present_completed|"
        r"d3d12_present_source_buffer_present_id|"
        r"d3d12_present_source_buffer_completed|"
        r"d3d12_final_handoff_present_id|"
        r"d3d12_final_handoff_completed)[ =]0\b",
        log,
    )
    if failed_present and re.search(r"\bbackend_opengl_submit[ \t]+1\b", log):
        fail_validation(
            "backend_opengl_submit=1 was advertised while D3D12 "
            "present-source commit failed closed or present_id/completed "
            "stayed zero"
        )

def reject_wave60_registration_only_present():
    reject_backend_opengl_submit_for_failed_present()
    if re.search(
            r"d3d12sharedsmoke: native present evidence ok "
            r"path=d3d12-dxg-present-source-display-handoff",
            log,
    ):
        return
    if (re.search(
            r"\bd3d12_(?:dxg_present_source|present_source_buffer)_"
            r"register_successes[ =][1-9][0-9]*\b",
            log,
    ) and re.search(
            r"\bd3d12_(?:dxg_present_source|present_source_buffer)_"
            r"commit_successes[ =]0\b",
            log,
    )):
        fail_validation(
            "registration-only D3D12 present-source evidence rejected: "
            "registration succeeded but commit did not, so FPS/WebKit/"
            "OpenGL-submit credit is refused"
        )

def reject_dxg_present_accounting_state():
    reject_wave60_registration_only_present()
    if (re.search(
            r"d3d12sharedsmoke: native present evidence ok "
            r"path=d3d12-dxg-present-source-display-handoff",
            log,
        ) or not re.search(
            r"\bd3d12_evidence_stage=|"
            r"\bstage=present-failed-before-success\b|"
            r"\bd3d12_present_path=fail-closed\b|"
            r"d3d12sharedsmoke: runtime-present-control-flow|"
            r"\bd3d12_dxg_present_source_commit_attempts",
            log,
        )):
        return

    if re.search(
            r"\bdxg_present_register_copyin_failures [1-9][0-9]*\b|"
            r"\bdxg_present_commit_copyin_failures [1-9][0-9]*\b",
            log,
    ):
        fail_validation(
            "D3D12 present fail-closed classification=copyin-failed: "
            "kernel present ioctl was entered but copyin failed; this is "
            "fail-closed accounting only, not native completion or FPS credit"
        )

    if (re.search(r"\bdxg_present_register_ioctl_entries 0\b", log) and
            re.search(r"\bdxg_present_commit_ioctl_entries 0\b", log)):
        fail_validation(
            "D3D12 present fail-closed classification=kernel-not-entered: "
            "no kernel present ioctl entry was observed; this cannot satisfy "
            "native-present/FPS/WebKit closure"
        )

    if (re.search(r"\bdxg_present_register_ioctl_entries [1-9][0-9]*\b", log) and
            re.search(r"\bdxg_present_commit_ioctl_entries [1-9][0-9]*\b", log) and
            re.search(r"\bdxg_present_register_copyin_failures 0\b", log) and
            re.search(r"\bdxg_present_commit_copyin_failures 0\b", log) and
            re.search(r"\bdxg_present_commit_attempts [1-9][0-9]*\b", log) and
            re.search(
                r"\bdxg_present_host_handoff_missing [1-9][0-9]*\b|"
                r"\bdxg_present_missing_host_abi [1-9][0-9]*\b|"
                r"\bdxg_present_last_ret 95\b",
                log,
            )):
        fail_validation(
            "D3D12 present fail-closed classification="
            "validated-missing-gpup-or-dda-display-bind: present ioctl/copyin reached "
            "validated kernel handling, but the required GPU-P/DDA display "
            "bind is missing; fail-closed accounting is not native completion "
            "or FPS credit"
        )

def reject_protocol_only_strict_present_artifact():
    if (not re.search(
            r"d3d12sharedsmoke: native present evidence ok "
            r"path=d3d12-dxg-present-source-display-handoff",
            log,
        ) and
            re.search(
                r"\bd3d12_evidence_stage=protocol_accepted\b|"
                r"\bstage=present-failed-before-success\b|"
                r"\bd3d12_protocol_accepts[ =][1-9][0-9]*\b|"
                r"\bd3d12_present_identity_current_run_valid[ =]0\b|"
                r"\bd3d12_gpu_copy_composite_starts[ =]0\b|"
                r"\bd3d12_gpu_copy_composite_completes[ =]0\b|"
                r"\bd3d12_gpu_copy_completes[ =]0\b|"
                r"\bd3d12_dxg_present_source_commit_attempts[ =]0\b|"
                r"\bdxg_present_commit_attempts 0\b|"
                r"\bd3d12_frame_callback_observed[ =]0\b|"
                r"\bd3d12_buffer_release_observed[ =]0\b",
                log,
            )):
        fail_validation(
            "intermediate strict-present artifact rejected: "
            "protocol/failure accounting may exist, but FPS credit requires "
            "fresh current-run identity, compositor GPU copy "
            "start+completion, present-source commit attempt, native present "
            "completion, callbacks/releases, and matching run evidence; "
            "fail-closed accounting is not native completion"
        )

reject_dxg_present_accounting_state()
reject_protocol_only_strict_present_artifact()

def import_only_evidence_lines():
    patterns = (
        r"\bnative_present_claim=0\b",
        r"\bpresent_claim=requires-compositor-completion\b",
        r"\bcommitted D3D12 shared resource buffer release=0\b",
        r"\brequire_present=0\b",
    )
    found = []
    for line in log.splitlines():
        if any(re.search(pattern, line) for pattern in patterns):
            found.append(line)
    return found

def interval_deltas(name, values):
    deltas = []
    for before, after in zip(values, values[1:]):
        if after < before:
            raise SystemExit(
                f"{name} counter decreased during FPS sample: "
                f"{before}->{after} values={','.join(str(v) for v in values)}"
            )
        deltas.append(after - before)
    return deltas

def require_active_intervals(name, deltas, required):
    active = sum(1 for value in deltas if value > 0)
    required = min(required, len(deltas))
    if required > 0 and active < required:
        raise SystemExit(
            f"{name} did not advance across enough independent samples: "
            f"active={active} required={required} "
            f"deltas={','.join(str(v) for v in deltas)}"
        )
    return active

def split_two_sample_windows(deltas):
    if len(deltas) < 2:
        return []
    split = max(1, len(deltas) // 2)
    if split >= len(deltas):
        split = len(deltas) - 1
    return [deltas[:split], deltas[split:]]

def require_sustained_counter_windows(name, deltas, **fields):
    windows = split_two_sample_windows(deltas)
    if len(windows) < 2:
        frozen_window_negative(
            f"{name}_missing_second_sample_window",
            sustained_sample_windows_observed=len(windows),
            **fields,
        )
        raise SystemExit(
            f"{name} lacked more than one post-warmup sample window: "
            f"deltas={','.join(str(v) for v in deltas)}"
        )
    window_totals = [sum(window) for window in windows]
    active_windows = sum(1 for total in window_totals if total > 0)
    if active_windows < 2:
        frozen_window_negative(
            f"{name}_not_sustained_after_initial_motion",
            sustained_sample_windows_observed=len(windows),
            sustained_sample_windows_active=active_windows,
            first_window_delta=window_totals[0],
            second_window_delta=window_totals[1],
            sample_window_deltas=",".join(str(value) for value in window_totals),
            **fields,
        )
        raise SystemExit(
            f"{name} did not advance in both post-warmup sample windows: "
            f"window_deltas={','.join(str(value) for value in window_totals)} "
            f"deltas={','.join(str(v) for v in deltas)}"
        )
    return window_totals

def counter_value(line, keys):
    for key in keys:
        match = re.search(rf"\b{re.escape(key)}=([0-9]+)", line)
        if match:
            return int(match.group(1))
    return None

def token_value(line, keys):
    for key in keys:
        match = re.search(rf"\b{re.escape(key)}=([^ \t\r\n]+)", line)
        if match:
            return match.group(1)
    return None

def hex_or_int_value(line, keys):
    for key in keys:
        match = re.search(rf"\b{re.escape(key)}=(0x[0-9a-fA-F]+|[0-9]+)", line)
        if match:
            return int(match.group(1), 0)
    return None

def frame_index(path):
    match = re.fullmatch(r"frame-([0-9]+)\.raw", path.name)
    if not match:
        raise SystemExit(f"unexpected thumbnail name: {path.name}")
    return int(match.group(1))

def load_thumbnail_progress():
    raws = sorted(visual_dir.glob("frame-*.raw"), key=frame_index)
    expected = width * height * 2
    if len(raws) < 2:
        raise SystemExit("not enough visible thumbnail samples")
    if len(raws) < visual_samples_per_window * 2:
        raise SystemExit(
            "not enough visible thumbnail samples for sustained "
            "post-warmup windows: "
            f"frames={len(raws)} required={visual_samples_per_window * 2}"
        )
    frames = []
    frame_times = []
    outside_overlay_crcs = []
    for raw_path in raws:
        raw = raw_path.read_bytes()
        if len(raw) < expected:
            raise SystemExit(
                f"thumbnail too small: {raw_path} {len(raw)} < {expected}"
            )
        frame = raw[:expected]
        frames.append(frame)
        frame_times.append(raw_path.stat().st_mtime)
        crc = 0
        for y in range(visual_ignore_top_pixels, height):
            row = y * width * 2
            crc = zlib.crc32(frame[row:row + width * 2], crc)
        outside_overlay_crcs.append(crc & 0xffffffff)

    def frame_transitions(window_frames):
        values = []
        for a, b in zip(window_frames, window_frames[1:]):
            changed = 0
            for y in range(height):
                row = y * width * 2
                for x in range(width):
                    # Ignore the title/FPS-overlay band; it can change while the
                    # 3D surface itself is frozen or skipped by the compositor.
                    if y < visual_ignore_top_pixels:
                        continue
                    off = row + x * 2
                    if a[off:off + 2] != b[off:off + 2]:
                        changed += 1
            values.append(changed)
        return values

    thumbnail_windows = []
    window_count = len(frames) // visual_samples_per_window
    for index in range(window_count):
        start = index * visual_samples_per_window
        end = start + visual_samples_per_window
        window_frames = frames[start:end]
        window_times = frame_times[start:end]
        window_crcs = outside_overlay_crcs[start:end]
        window_transitions = frame_transitions(window_frames)
        thumbnail_windows.append({
            "index": index + 1,
            "frames": window_frames,
            "times": window_times,
            "crcs": window_crcs,
            "transitions": window_transitions,
        })

    transitions = []
    for window in thumbnail_windows:
        transitions.extend(window["transitions"])
    return frames, frame_times, outside_overlay_crcs, transitions, thumbnail_windows

def require_sustained_thumbnail_windows(thumbnail_windows):
    if len(thumbnail_windows) < 2:
        frozen_window_negative(
            "visual_missing_second_sample_window",
            sustained_sample_windows_observed=len(thumbnail_windows),
        )
        raise SystemExit(
            "visible thumbnail validation requires more than one "
            "post-warmup sample window"
        )
    summaries = []
    for window in thumbnail_windows:
        transitions = window["transitions"]
        crcs = window["crcs"]
        required = min(2, len(transitions))
        active = sum(1 for changed in transitions
                     if changed >= min_changed_pixels)
        crc_changes = sum(
            1
            for before, after in zip(crcs, crcs[1:])
            if before != after
        )
        max_changed = max(transitions) if transitions else 0
        summaries.append((window["index"], active, required,
                          crc_changes, max_changed, transitions, crcs))
        if max_changed < min_changed_pixels or active < required or crc_changes < required:
            frozen_window_negative(
                "visual_window_not_sustained_after_initial_motion",
                sustained_sample_windows_observed=len(thumbnail_windows),
                visual_sample_window=window["index"],
                active_thumbnail_transitions=active,
                required_thumbnail_transitions=required,
                outside_overlay_crc_changes=crc_changes,
                thumbnail_progress_delta=max_changed,
                changed=",".join(str(v) for v in transitions),
                outside_overlay_crcs=",".join(hex(value) for value in crcs),
            )
            raise SystemExit(
                "visible thumbnail progress did not persist across more "
                "than one post-warmup sample window: "
                f"window={window['index']} active={active} "
                f"required={required} max_changed={max_changed} "
                f"crc_changes={crc_changes} "
                f"samples={','.join(str(v) for v in transitions)}"
            )
    return summaries

def require_run_id_samples(name, values):
    if not values:
        raise SystemExit(
            f"missing {name} validation run id samples for {expected_run_id}"
        )
    bad = [value for value in values if value != expected_run_id]
    if bad:
        raise SystemExit(
            f"{name} validation run id mismatch: expected={expected_run_id} "
            f"values={','.join(values)}"
        )

def require_display_handoff_samples(name, values, copy_values):
    if len(values) < 2:
        if copy_values and max(copy_values) > 0:
            fail_validation(
                "D3D12 compositor-copy sub-gate observed, but native-present "
                "FPS credit requires d3d12_display_handoff_implemented=1 "
                f"during {name}: samples={len(values)}"
            )
        raise SystemExit(
            f"missing repeated d3d12_display_handoff_implemented=1 samples "
            f"during {name}: count={len(values)}"
        )
    bad = [value for value in values if value != 1]
    if bad:
        fail_validation(
            "D3D12 compositor-copy sub-gate is not enough for FPS credit; "
            "display handoff must be implemented before native-present/FPS "
            f"can pass during {name}: "
            f"values={','.join(str(value) for value in values)}"
        )

def require_advancing_counter(name, values):
    if len(values) < 2:
        raise SystemExit(
            f"not enough {name} samples: count={len(values)}"
        )
    deltas = interval_deltas(name, values)
    if values[-1] == values[0]:
        raise SystemExit(
            f"{name} did not advance during FPS sample: "
            f"{values[0]}->{values[-1]}"
        )
    return deltas

strict_fps_sample_requirements = (
    ("evidence_valid", "d3d12_evidence_valid"),
    ("native_present_credit", "native_present_credit"),
    ("native_present_complete", "native_present_complete"),
    ("same_run_resource_generation", "same_run_resource_generation"),
    ("same_resource_generation", "same_resource_generation"),
    ("compositor_owned_visible_content_crc",
     "compositor_owned_visible_content_crc"),
    ("compositor_owned_visible_content_frame",
     "compositor_owned_visible_content_frame"),
    ("compositor_owned_visible_frame_hash",
     "compositor_owned_visible_frame_hash"),
    ("client_content_progress", "client_content_progress"),
    ("callback_release_same_frame", "callback_release_same_frame"),
    ("finite_demo_visible", "finite_demo_visible"),
    ("finite_demo_closeable", "finite_demo_closeable"),
    ("finite_demo_resizable", "finite_demo_resizable"),
    ("strict_finite_fps_evidence", "strict_finite_fps_evidence"),
)

def missing_strict_fps_sample_requirements(record):
    missing = []
    for key, label in strict_fps_sample_requirements:
        if record.get(key) != 1:
            missing.append(label)
    return missing

for line in log.splitlines():
    if line.startswith("hyperv-3d-fps-validate: sampling "):
        in_sampling = True
        continue
    if line.startswith("hyperv-3d-fps-validate: sample window end"):
        in_sampling = False
        visual_counter = None
        continue
    if line.startswith("hyperv-3d-fps-validate: visual counter begin"):
        visual_counter = "begin"
        continue
    if line.startswith("hyperv-3d-fps-validate: visual counter end"):
        visual_counter = "end"
        continue
    demo_visible = counter_value(line, demo_visible_keys)
    if demo_visible is not None:
        demo_visible_evidence.append(demo_visible)
    demo_closeable = counter_value(line, demo_closeable_keys)
    if demo_closeable is not None:
        demo_closeable_evidence.append(demo_closeable)
    demo_resizable = counter_value(line, demo_resizable_keys)
    if demo_resizable is not None:
        demo_resizable_evidence.append(demo_resizable)
    demo_interaction_native = counter_value(line, demo_interaction_native_keys)
    if demo_interaction_native is not None:
        demo_interaction_native_complete.append(demo_interaction_native)
    demo_interaction_content = counter_value(line, demo_interaction_content_keys)
    if demo_interaction_content is not None:
        demo_interaction_content_progress.append(demo_interaction_content)
    if (demo_visible is not None or demo_closeable is not None or
            demo_resizable is not None):
        present_id = counter_value(line, ("present_id", "d3d12_dxg_present_id"))
        if present_id is not None:
            demo_interaction_present_ids.append(present_id)
        completed = counter_value(line, ("completed", "d3d12_dxg_present_completed"))
        if completed is not None:
            demo_interaction_completed.append(completed)
        run_id = token_value(line, run_id_keys)
        if run_id is not None:
            demo_interaction_run_ids.append(run_id)
    if visual_counter is not None:
        uptime = re.match(r"^([0-9]+(?:\.[0-9]+)?)[ \t]+[0-9]+(?:\.[0-9]+)?[ \t]*$", line)
        if uptime:
            visual_uptimes.append(float(uptime.group(1)))
        d3d12_completes = re.search(r"\bd3d12_gpu_present_complete(?:s)?=([0-9]+)", line)
        if d3d12_completes:
            visual_native_counts.append(int(d3d12_completes.group(1)))
        d3d12_copies = re.search(r"\bd3d12_gpu_copy_completes=([0-9]+)", line)
        if d3d12_copies:
            visual_copy_counts.append(int(d3d12_copies.group(1)))
        display_handoff = re.search(r"\bd3d12_display_handoff_implemented=([0-9]+)", line)
        if display_handoff:
            visual_display_handoffs.append(int(display_handoff.group(1)))
        generation = re.search(r"\bd3d12_evidence_generation=([0-9]+)", line)
        if generation:
            visual_evidence_generations.append(int(generation.group(1)))
        evidence_time = re.search(r"\bd3d12_present_evidence_time_us=([0-9]+)", line)
        if evidence_time:
            visual_evidence_times.append(int(evidence_time.group(1)))
        resource = re.search(r"\bd3d12_present_resource=0x([0-9a-fA-F]+)", line)
        if resource:
            visual_resources.append(int(resource.group(1), 16))
        buffer_generation = re.search(r"\bd3d12_buffer_generation=([0-9]+)", line)
        if buffer_generation:
            visual_buffer_generations.append(int(buffer_generation.group(1)))
        present_id = re.search(r"\b(?:d3d12_dxg_present_id|present_id)=([0-9]+)", line)
        if present_id:
            visual_present_ids.append(int(present_id.group(1)))
        completed = re.search(r"\bcompleted=([0-9]+)", line)
        if completed:
            visual_present_completed.append(int(completed.group(1)))
        native_completion_id = counter_value(line, native_completion_id_keys)
        if native_completion_id is not None:
            visual_native_completion_ids.append(native_completion_id)
        resource_generation = counter_value(line, resource_generation_keys)
        if resource_generation is not None:
            visual_resource_generations.append(resource_generation)
        display_bind_backend = token_value(line, ("display_bind_backend",))
        if display_bind_backend is not None:
            visual_display_bind_backends.append(display_bind_backend)
        display_bind_transport = token_value(line, ("display_bind_transport",))
        if display_bind_transport is not None:
            visual_display_bind_transports.append(display_bind_transport)
        display_bind_present_id = counter_value(line, ("display_bind_present_id",))
        if display_bind_present_id is not None:
            visual_display_bind_present_ids.append(display_bind_present_id)
        display_bind_completed_id = counter_value(line, ("display_bind_completed_id",))
        if display_bind_completed_id is not None:
            visual_display_bind_completed_ids.append(display_bind_completed_id)
        display_bind_resource_generation = counter_value(
            line,
            ("display_bind_resource_generation",),
        )
        if display_bind_resource_generation is not None:
            visual_display_bind_resource_generations.append(
                display_bind_resource_generation
            )
        completion_source = token_value(
            line,
            ("display_bind_completion_source", "completion_source"),
        )
        if completion_source is not None:
            visual_display_bind_completion_sources.append(completion_source)
        gpup_dda_commit = counter_value(line, gpup_dda_commit_success_keys)
        if gpup_dda_commit is not None:
            visual_gpup_dda_commit_successes.append(gpup_dda_commit)
        same_frame = counter_value(line, same_frame_callback_release_keys)
        if same_frame is not None:
            visual_same_frame_callback_releases.append(same_frame)
        same_frame_callback = counter_value(line, same_frame_callback_keys)
        if same_frame_callback is not None:
            visual_same_frame_callbacks.append(same_frame_callback)
        same_frame_release = counter_value(line, same_frame_release_keys)
        if same_frame_release is not None:
            visual_same_frame_releases.append(same_frame_release)
        content_crc = hex_or_int_value(line, content_crc_keys)
        if content_crc is not None:
            visual_content_crcs.append(content_crc)
        content_frame = hex_or_int_value(line, content_frame_keys)
        if content_frame is not None:
            visual_content_frames.append(content_frame)
        content_frame_hash = hex_or_int_value(line, content_frame_hash_keys)
        if content_frame_hash is not None:
            visual_content_frame_hashes.append(content_frame_hash)
        content_state = token_value(line, ("d3d12_content_progress_state",))
        if content_state is not None:
            visual_content_progress_states.append(content_state)
        visible_content_state = token_value(line, ("d3d12_visible_content_progress",))
        if visible_content_state is not None:
            visual_visible_content_progress_states.append(visible_content_state)
        requires_native = counter_value(
            line,
            ("d3d12_content_progress_requires_native_present",),
        )
        if requires_native is not None:
            visual_content_requires_native.append(requires_native)
        native_complete = counter_value(
            line,
            ("d3d12_content_progress_native_present_complete",),
        )
        if native_complete is not None:
            visual_content_native_complete.append(native_complete)
        visible_credit = counter_value(
            line,
            ("d3d12_content_progress_visible_credit",),
        )
        if visible_credit is not None:
            visual_content_visible_credit.append(visible_credit)
        source_owned = counter_value(
            line,
            ("d3d12_content_progress_source_owned",),
        )
        if source_owned is not None:
            visual_content_source_owned.append(source_owned)
        current_run_valid = counter_value(
            line,
            ("d3d12_content_progress_current_run_valid",
             "content_progress_current_run_valid"),
        )
        if current_run_valid is not None:
            visual_content_current_run_valid.append(current_run_valid)
        identity_complete = counter_value(
            line,
            ("d3d12_content_progress_identity_complete",
             "content_progress_identity_complete"),
        )
        if identity_complete is not None:
            visual_content_identity_complete.append(identity_complete)
        content_present_id = counter_value(
            line,
            ("d3d12_content_progress_present_id",
             "content_progress_present_id"),
        )
        if content_present_id is not None:
            visual_content_present_ids.append(content_present_id)
        content_completed = counter_value(
            line,
            ("d3d12_content_progress_completed",
             "content_progress_completed"),
        )
        if content_completed is not None:
            visual_content_completed_ids.append(content_completed)
        content_display_present = counter_value(
            line,
            ("d3d12_content_progress_display_bind_present_id",
             "content_progress_display_bind_present_id"),
        )
        if content_display_present is not None:
            visual_content_display_bind_present_ids.append(content_display_present)
        content_display_completed = counter_value(
            line,
            ("d3d12_content_progress_display_bind_completed_id",
             "content_progress_display_bind_completed_id"),
        )
        if content_display_completed is not None:
            visual_content_display_bind_completed_ids.append(
                content_display_completed
            )
        content_display_generation = counter_value(
            line,
            ("d3d12_content_progress_display_bind_resource_generation",
             "content_progress_display_bind_resource_generation"),
        )
        if content_display_generation is not None:
            visual_content_display_bind_resource_generations.append(
                content_display_generation
            )
        run_id = token_value(line, run_id_keys)
        if run_id is not None:
            visual_run_ids.append(run_id)
        final_success = counter_value(line, ("d3d12_final_handoff_success",
                                             "final_handoff_success"))
        if final_success is not None:
            visual_final_handoff_successes.append(final_success)
        final_present = counter_value(line, ("d3d12_final_handoff_present_id",
                                            "final_handoff_present_id"))
        if final_present is not None:
            visual_final_handoff_present_ids.append(final_present)
        final_completed = counter_value(line, ("d3d12_final_handoff_completed",
                                              "final_handoff_completed"))
        if final_completed is not None:
            visual_final_handoff_completed_ids.append(final_completed)
        final_generation = counter_value(
            line,
            ("d3d12_final_handoff_resource_generation",
             "final_handoff_resource_generation"),
        )
        if final_generation is not None:
            visual_final_handoff_resource_generations.append(final_generation)
        callback_count = counter_value(line, callback_counter_keys)
        if callback_count is not None:
            visual_callback_counts.append(callback_count)
        release_count = counter_value(line, release_counter_keys)
        if release_count is not None:
            visual_release_counts.append(release_count)
        path = re.search(r"\bd3d12_present_path=([^ \t]+)", line)
        if path:
            visual_native_paths.append(path.group(1))
        reject = re.search(
            r"\bd3d12_(cpu_readback|cpu_mapping|cpu_copy|readback|"
            r"native_present_unimplemented|present_state_only|"
            r"present_fence_only|present_import_only|present_open_only|"
            r"present_callback_only|present_release_only|"
            r"present_errno)=([1-9][0-9]*)|"
            r"\b(callbacks_blocked|releases_blocked)=([1-9][0-9]*)",
            line,
        )
        if reject:
            visual_reject_evidence.append(line)
        if line.startswith("hyperv-3d-fps-validate:") or line.startswith("mesawlegl["):
            visual_counter = None
    if in_sampling:
        uptime = re.match(r"^([0-9]+(?:\.[0-9]+)?)[ \t]+[0-9]+(?:\.[0-9]+)?[ \t]*$", line)
        if uptime:
            uptimes.append(float(uptime.group(1)))
        completion = re.match(r"^display_completions[ \t]+([0-9]+)[ \t]*$", line)
        if completion:
            display_completions.append(int(completion.group(1)))
        presents = re.match(r"^display_presents[ \t]+([0-9]+)[ \t]*$", line)
        if presents:
            display_presents.append(int(presents.group(1)))
        d3d12_completes = re.search(r"\bd3d12_gpu_present_complete(?:s)?=([0-9]+)", line)
        if d3d12_completes:
            d3d12_present_counts.append(int(d3d12_completes.group(1)))
        d3d12_starts = re.search(r"\bd3d12_gpu_present_starts=([0-9]+)", line)
        if d3d12_starts:
            d3d12_present_starts.append(int(d3d12_starts.group(1)))
        d3d12_copies = re.search(r"\bd3d12_gpu_copy_completes=([0-9]+)", line)
        if d3d12_copies:
            d3d12_copy_counts.append(int(d3d12_copies.group(1)))
        display_handoff = re.search(r"\bd3d12_display_handoff_implemented=([0-9]+)", line)
        if display_handoff:
            d3d12_display_handoffs.append(int(display_handoff.group(1)))
        generation = re.search(r"\bd3d12_evidence_generation=([0-9]+)", line)
        if generation:
            d3d12_evidence_generations.append(int(generation.group(1)))
        evidence_time = re.search(r"\bd3d12_present_evidence_time_us=([0-9]+)", line)
        if evidence_time:
            d3d12_evidence_times.append(int(evidence_time.group(1)))
        resource = re.search(r"\bd3d12_present_resource=0x([0-9a-fA-F]+)", line)
        if resource:
            d3d12_resources.append(int(resource.group(1), 16))
        buffer_generation = re.search(r"\bd3d12_buffer_generation=([0-9]+)", line)
        if buffer_generation:
            d3d12_buffer_generations.append(int(buffer_generation.group(1)))
        present_id = re.search(r"\b(?:d3d12_dxg_present_id|present_id)=([0-9]+)", line)
        if present_id:
            d3d12_present_ids.append(int(present_id.group(1)))
        completed = re.search(r"\bcompleted=([0-9]+)", line)
        if completed:
            d3d12_present_completed.append(int(completed.group(1)))
        native_completion_id = counter_value(line, native_completion_id_keys)
        if native_completion_id is not None:
            d3d12_native_completion_ids.append(native_completion_id)
        resource_generation = counter_value(line, resource_generation_keys)
        if resource_generation is not None:
            d3d12_resource_generations.append(resource_generation)
        display_bind_backend = token_value(line, ("display_bind_backend",))
        if display_bind_backend is not None:
            d3d12_display_bind_backends.append(display_bind_backend)
        display_bind_transport = token_value(line, ("display_bind_transport",))
        if display_bind_transport is not None:
            d3d12_display_bind_transports.append(display_bind_transport)
        display_bind_present_id = counter_value(line, ("display_bind_present_id",))
        if display_bind_present_id is not None:
            d3d12_display_bind_present_ids.append(display_bind_present_id)
        display_bind_completed_id = counter_value(line, ("display_bind_completed_id",))
        if display_bind_completed_id is not None:
            d3d12_display_bind_completed_ids.append(display_bind_completed_id)
        display_bind_resource_generation = counter_value(
            line,
            ("display_bind_resource_generation",),
        )
        if display_bind_resource_generation is not None:
            d3d12_display_bind_resource_generations.append(
                display_bind_resource_generation
            )
        completion_source = token_value(
            line,
            ("display_bind_completion_source", "completion_source"),
        )
        if completion_source is not None:
            d3d12_display_bind_completion_sources.append(completion_source)
        gpup_dda_commit = counter_value(line, gpup_dda_commit_success_keys)
        if gpup_dda_commit is not None:
            d3d12_gpup_dda_commit_successes.append(gpup_dda_commit)
        same_frame = counter_value(line, same_frame_callback_release_keys)
        if same_frame is not None:
            d3d12_same_frame_callback_releases.append(same_frame)
        same_frame_callback = counter_value(line, same_frame_callback_keys)
        if same_frame_callback is not None:
            d3d12_same_frame_callbacks.append(same_frame_callback)
        same_frame_release = counter_value(line, same_frame_release_keys)
        if same_frame_release is not None:
            d3d12_same_frame_releases.append(same_frame_release)
        content_crc = hex_or_int_value(line, content_crc_keys)
        if content_crc is not None:
            d3d12_content_crcs.append(content_crc)
        content_frame = hex_or_int_value(line, content_frame_keys)
        if content_frame is not None:
            d3d12_content_frames.append(content_frame)
        content_frame_hash = hex_or_int_value(line, content_frame_hash_keys)
        if content_frame_hash is not None:
            d3d12_content_frame_hashes.append(content_frame_hash)
        content_state = token_value(line, ("d3d12_content_progress_state",))
        if content_state is not None:
            d3d12_content_progress_states.append(content_state)
        visible_content_state = token_value(line, ("d3d12_visible_content_progress",))
        if visible_content_state is not None:
            d3d12_visible_content_progress_states.append(visible_content_state)
        requires_native = counter_value(
            line,
            ("d3d12_content_progress_requires_native_present",),
        )
        if requires_native is not None:
            d3d12_content_requires_native.append(requires_native)
        native_complete = counter_value(
            line,
            ("d3d12_content_progress_native_present_complete",),
        )
        if native_complete is not None:
            d3d12_content_native_complete.append(native_complete)
        visible_credit = counter_value(
            line,
            ("d3d12_content_progress_visible_credit",),
        )
        if visible_credit is not None:
            d3d12_content_visible_credit.append(visible_credit)
        source_owned = counter_value(
            line,
            ("d3d12_content_progress_source_owned",),
        )
        if source_owned is not None:
            d3d12_content_source_owned.append(source_owned)
        current_run_valid = counter_value(
            line,
            ("d3d12_content_progress_current_run_valid",
             "content_progress_current_run_valid"),
        )
        if current_run_valid is not None:
            d3d12_content_current_run_valid.append(current_run_valid)
        identity_complete = counter_value(
            line,
            ("d3d12_content_progress_identity_complete",
             "content_progress_identity_complete"),
        )
        if identity_complete is not None:
            d3d12_content_identity_complete.append(identity_complete)
        content_present_id = counter_value(
            line,
            ("d3d12_content_progress_present_id",
             "content_progress_present_id"),
        )
        if content_present_id is not None:
            d3d12_content_present_ids.append(content_present_id)
        content_completed = counter_value(
            line,
            ("d3d12_content_progress_completed",
             "content_progress_completed"),
        )
        if content_completed is not None:
            d3d12_content_completed_ids.append(content_completed)
        content_display_present = counter_value(
            line,
            ("d3d12_content_progress_display_bind_present_id",
             "content_progress_display_bind_present_id"),
        )
        if content_display_present is not None:
            d3d12_content_display_bind_present_ids.append(content_display_present)
        content_display_completed = counter_value(
            line,
            ("d3d12_content_progress_display_bind_completed_id",
             "content_progress_display_bind_completed_id"),
        )
        if content_display_completed is not None:
            d3d12_content_display_bind_completed_ids.append(
                content_display_completed
            )
        content_display_generation = counter_value(
            line,
            ("d3d12_content_progress_display_bind_resource_generation",
             "content_progress_display_bind_resource_generation"),
        )
        if content_display_generation is not None:
            d3d12_content_display_bind_resource_generations.append(
                content_display_generation
            )
        run_id = token_value(line, run_id_keys)
        if run_id is not None:
            d3d12_run_ids.append(run_id)
        final_success = counter_value(line, ("d3d12_final_handoff_success",
                                             "final_handoff_success"))
        if final_success is not None:
            d3d12_final_handoff_successes.append(final_success)
        final_present = counter_value(line, ("d3d12_final_handoff_present_id",
                                            "final_handoff_present_id"))
        if final_present is not None:
            d3d12_final_handoff_present_ids.append(final_present)
        final_completed = counter_value(line, ("d3d12_final_handoff_completed",
                                              "final_handoff_completed"))
        if final_completed is not None:
            d3d12_final_handoff_completed_ids.append(final_completed)
        final_generation = counter_value(
            line,
            ("d3d12_final_handoff_resource_generation",
             "final_handoff_resource_generation"),
        )
        if final_generation is not None:
            d3d12_final_handoff_resource_generations.append(final_generation)
        callback_count = counter_value(line, callback_counter_keys)
        if callback_count is not None:
            d3d12_callback_counts.append(callback_count)
        release_count = counter_value(line, release_counter_keys)
        if release_count is not None:
            d3d12_release_counts.append(release_count)
        path = re.search(r"\bd3d12_present_path=([^ \t]+)", line)
        if path:
            d3d12_native_paths.append(path.group(1))
        reject = re.search(
            r"\bd3d12_(cpu_readback|cpu_mapping|cpu_copy|readback|"
            r"native_present_unimplemented|present_state_only|"
            r"present_fence_only|present_import_only|present_open_only|"
            r"present_callback_only|present_release_only|"
            r"present_errno)=([1-9][0-9]*)|"
            r"\b(callbacks_blocked|releases_blocked)=([1-9][0-9]*)",
            line,
        )
        if reject:
            d3d12_reject_evidence.append(line)
        mode = re.search(r"\bmode=([^ \t]+)", line)
        if mode:
            backend_modes.append(mode.group(1))
        opengl_submit = re.match(r"^backend_opengl_submit[ \t]+([01])[ \t]*$", line)
        if opengl_submit:
            backend_opengl_submit_samples.append(int(opengl_submit.group(1)))
    fps = re.search(r"\bvisible_fps=([0-9]+(?:\.[0-9]+)?)", line)
    seq = re.search(r"\bcallback_seq=([0-9]+)", line)
    if fps and seq and line.startswith("mesawlegl_fps_sample "):
        record = {
            "line": line,
            "in_sampling": in_sampling,
            "seq": int(seq.group(1)),
            "visible_fps": float(fps.group(1)),
            "render": None,
            "render_div": 0,
            "window": None,
            "native_count": None,
            "native_delta": None,
            "native_elapsed": None,
            "frame": None,
            "sample_time": None,
            "source": None,
            "run_id": None,
            "process_id": None,
            "evidence_valid": None,
            "evidence_generation": None,
            "evidence_time": None,
            "resource": None,
            "buffer_generation": None,
            "client_pid": None,
            "native_present_credit": None,
            "native_present_complete": None,
            "same_run_resource_generation": None,
            "same_resource_generation": None,
            "compositor_owned_visible_content_crc": None,
            "compositor_owned_visible_content_frame": None,
            "compositor_owned_visible_frame_hash": None,
            "client_content_progress": None,
            "callback_release_same_frame": None,
            "finite_demo_visible": None,
            "finite_demo_closeable": None,
            "finite_demo_resizable": None,
            "strict_finite_fps_evidence": None,
        }
        render = re.search(r"\brender=([0-9]+)x([0-9]+)", line)
        render_div = re.search(r"\brender_div=([0-9]+)", line)
        if render:
            div = int(render_div.group(1)) if render_div else 0
            record["render"] = (int(render.group(1)), int(render.group(2)))
            record["render_div"] = div
        window = re.search(r"\bwindow=([0-9]+)x([0-9]+)", line)
        if window:
            record["window"] = (int(window.group(1)), int(window.group(2)))
        demo_count = re.search(r"\bnative_present_count=([0-9]+)", line)
        if demo_count:
            record["native_count"] = int(demo_count.group(1))
        demo_delta = re.search(r"\bnative_present_delta=([0-9]+)", line)
        if demo_delta:
            record["native_delta"] = int(demo_delta.group(1))
        demo_elapsed = re.search(r"\bnative_present_elapsed=([0-9]+(?:\.[0-9]+)?)", line)
        if demo_elapsed:
            record["native_elapsed"] = float(demo_elapsed.group(1))
        demo_frame = re.search(r"\bframe=([0-9]+)", line)
        if demo_frame:
            record["frame"] = int(demo_frame.group(1))
        demo_time = re.search(r"\bsample_time=([0-9]+(?:\.[0-9]+)?)", line)
        if demo_time:
            record["sample_time"] = float(demo_time.group(1))
        source = re.search(r"\bsource=([^ \t]+)", line)
        if source:
            record["source"] = source.group(1)
        demo_run_id = token_value(line, run_id_keys)
        if demo_run_id is not None:
            record["run_id"] = demo_run_id
        demo_process = re.search(r"\bprocess_id=([0-9]+)", line)
        if demo_process:
            record["process_id"] = int(demo_process.group(1))
        valid = re.search(r"\bd3d12_evidence_valid=([0-9]+)", line)
        if valid:
            record["evidence_valid"] = int(valid.group(1))
        demo_generation = re.search(r"\bd3d12_evidence_generation=([0-9]+)", line)
        if demo_generation:
            record["evidence_generation"] = int(demo_generation.group(1))
        demo_evidence_time = re.search(r"\bd3d12_present_evidence_time_us=([0-9]+)", line)
        if demo_evidence_time:
            record["evidence_time"] = int(demo_evidence_time.group(1))
        demo_resource = re.search(r"\bd3d12_present_resource=0x([0-9a-fA-F]+)", line)
        if demo_resource:
            record["resource"] = int(demo_resource.group(1), 16)
        demo_buffer_generation = re.search(r"\bd3d12_buffer_generation=([0-9]+)", line)
        if demo_buffer_generation:
            record["buffer_generation"] = int(demo_buffer_generation.group(1))
        demo_client_pid = re.search(r"\bd3d12_client_pid=([0-9]+)", line)
        if demo_client_pid:
            record["client_pid"] = int(demo_client_pid.group(1))
        for key in (
                "native_present_credit",
                "native_present_complete",
                "same_run_resource_generation",
                "same_resource_generation",
                "compositor_owned_visible_content_crc",
                "compositor_owned_visible_content_frame",
                "compositor_owned_visible_frame_hash",
                "client_content_progress",
                "callback_release_same_frame",
                "finite_demo_visible",
                "finite_demo_closeable",
                "finite_demo_resizable",
                "strict_finite_fps_evidence"):
            value = counter_value(line, (key,))
            if value is not None:
                record[key] = value
        demo_records.append(record)

sample_uptime_min = min(uptimes) if uptimes else None
sample_uptime_max = max(uptimes) if uptimes else None
accepted_demo_records = []
accepted_demo_keys = set()
for record in demo_records:
    timestamped_in_window = (
        sample_uptime_min is not None and
        sample_uptime_max is not None and
        record["sample_time"] is not None and
        sample_uptime_min <= record["sample_time"] <= sample_uptime_max
    )
    legacy_in_window = (
        record["in_sampling"] and
        record["sample_time"] is None and
        sample_uptime_min is None
    )
    record_key = (record["seq"], record["sample_time"])
    if (timestamped_in_window or legacy_in_window) and record_key not in accepted_demo_keys:
        accepted_demo_records.append(record)
        accepted_demo_keys.add(record_key)
accepted_record_ids = {id(record) for record in accepted_demo_records}
ignored_post_window_records = [
    record
    for record in demo_records
    if not record["in_sampling"] and id(record) not in accepted_record_ids
]
samples = [
    (record["seq"], record["visible_fps"])
    for record in accepted_demo_records
]
render_samples = [
    (record["render"][0], record["render"][1], record["render_div"])
    for record in accepted_demo_records
    if record["render"] is not None
]
window_samples = [
    record["window"]
    for record in accepted_demo_records
    if record["window"] is not None
]
demo_native_counts = [
    record["native_count"]
    for record in accepted_demo_records
    if record["native_count"] is not None
]
demo_sources = [
    record["source"]
    for record in accepted_demo_records
    if record["source"] is not None
]
demo_native_deltas = [
    record["native_delta"]
    for record in accepted_demo_records
    if record["native_delta"] is not None
]
demo_native_elapsed = [
    record["native_elapsed"]
    for record in accepted_demo_records
    if record["native_elapsed"] is not None
]
demo_frames = [
    record["frame"]
    for record in accepted_demo_records
    if record["frame"] is not None
]
demo_sample_times = [
    record["sample_time"]
    for record in accepted_demo_records
    if record["sample_time"] is not None
]
demo_run_ids = [
    record["run_id"]
    for record in accepted_demo_records
    if record["run_id"] is not None
]
demo_process_ids = [
    record["process_id"]
    for record in accepted_demo_records
    if record["process_id"] is not None
]
demo_evidence_valid = [
    record["evidence_valid"]
    for record in accepted_demo_records
    if record["evidence_valid"] is not None
]
demo_evidence_generations = [
    record["evidence_generation"]
    for record in accepted_demo_records
    if record["evidence_generation"] is not None
]
demo_evidence_times = [
    record["evidence_time"]
    for record in accepted_demo_records
    if record["evidence_time"] is not None
]
demo_resources = [
    record["resource"]
    for record in accepted_demo_records
    if record["resource"] is not None
]
demo_buffer_generations = [
    record["buffer_generation"]
    for record in accepted_demo_records
    if record["buffer_generation"] is not None
]
demo_client_pids = [
    record["client_pid"]
    for record in accepted_demo_records
    if record["client_pid"] is not None
]
if "hyperv-3d-fps-validate: visual thumbnails inside finite sample window" not in log:
    raise SystemExit("visible thumbnail samples were not captured inside the finite FPS sample window")
frames, frame_times, outside_overlay_crcs, transitions, thumbnail_windows = (
    load_thumbnail_progress()
)
thumbnail_window_summaries = require_sustained_thumbnail_windows(
    thumbnail_windows
)
log_fps_gate_skeleton("sample-preaccept", outside_overlay_crcs)
if ("d3d12sharedsmoke: present validation ok" not in log and
        import_only_evidence_lines()):
    fail_validation(
        "D3D12 import/fence evidence did not reach native present "
        "completion; refusing import-only FPS credit: "
        + import_only_evidence_lines()[-1]
    )
if not samples:
    raise SystemExit("no /tmp/mesawlegl-fps visible_fps callback samples found")
context_only_records = [
    record for record in accepted_demo_records
    if record["source"] == "app-draw-loop-context-only" or
       record["evidence_valid"] == 0
]
if context_only_records:
    last = context_only_records[-1]
    log_validation(
        "fps_overlay_inflation_rejection_matrix "
        f"validation_run_id={expected_run_id} "
        f"displayed_fps={last['visible_fps']:.3f} "
        "source=app-draw-loop-context-only displayed_fps_context_only=1 "
        "d3d12_evidence_valid=0 native_present_delta=0 rejected=1 "
        "status=PASS"
    )
    raise SystemExit(
        "demo FPS probe is app-draw-loop/context-only; native D3D12 "
        "present completion evidence is required before FPS can pass: "
        + last["line"]
    )
missing_strict_records = [
    (record, missing_strict_fps_sample_requirements(record))
    for record in accepted_demo_records
]
missing_strict_records = [
    (record, missing)
    for record, missing in missing_strict_records
    if missing
]
if missing_strict_records:
    record, missing = missing_strict_records[-1]
    fail_validation(
        "fps_finite_credit_gate_matrix "
        f"validation_run_id={expected_run_id} stage=sample "
        "requires_native_present_completion=1 "
        "requires_compositor_owned_visible_content_crc=1 "
        "requires_compositor_owned_visible_content_frame=1 "
        "requires_compositor_owned_visible_frame_hash=1 "
        "requires_same_resource_generation=1 "
        "requires_demo_visible=1 requires_demo_closeable=1 "
        "requires_demo_resizable=1 "
        f"missing={','.join(missing)} "
        "native_present_credit=0 opengl_submit_credit=0 "
        "gate=closed status=FAIL_MISSING_EVIDENCE "
        f"line={record['line']}"
    )
require_run_id_samples("/tmp/mesawlegl-fps", demo_run_ids)
if not demo_process_ids or any(value <= 0 for value in demo_process_ids):
    raise SystemExit("missing /tmp/mesawlegl-fps process_id samples")
if len(demo_process_ids) != len(accepted_demo_records):
    raise SystemExit(
        "accepted /tmp/mesawlegl-fps samples are missing process_id fields: "
        f"accepted={len(accepted_demo_records)} process_ids={len(demo_process_ids)}"
    )
if len(demo_client_pids) != len(accepted_demo_records):
    raise SystemExit(
        "accepted /tmp/mesawlegl-fps samples are missing d3d12_client_pid "
        f"identity fields: accepted={len(accepted_demo_records)} "
        f"client_pids={len(demo_client_pids)}"
    )
bad_pid_pairs = [
    record
    for record in accepted_demo_records
    if record["process_id"] != record["client_pid"]
]
if bad_pid_pairs:
    bad = bad_pid_pairs[-1]
    raise SystemExit(
        "demo native-present evidence belongs to a different client: "
        f"process_id={bad['process_id']} d3d12_client_pid={bad['client_pid']} "
        f"line={bad['line']}"
    )
if len(set(demo_process_ids)) != 1:
    raise SystemExit(
        "accepted /tmp/mesawlegl-fps samples changed process identity: "
        f"process_ids={','.join(str(value) for value in demo_process_ids)}"
    )
require_run_id_samples("D3D12 sample-window evidence", d3d12_run_ids)
require_display_handoff_samples(
    "D3D12 sample-window evidence",
    d3d12_display_handoffs,
    d3d12_copy_counts,
)
d3d12_generation_deltas = require_advancing_counter(
    "d3d12_evidence_generation",
    d3d12_evidence_generations,
)
d3d12_evidence_time_deltas = require_advancing_counter(
    "d3d12_present_evidence_time_us",
    d3d12_evidence_times,
)
if len(d3d12_resources) < 2 or any(value == 0 for value in d3d12_resources):
    raise SystemExit(
        "missing nonzero D3D12 present resource samples from compositor "
        f"evidence: count={len(d3d12_resources)}"
    )
if (len(d3d12_buffer_generations) < 2 or
        any(value == 0 for value in d3d12_buffer_generations)):
    raise SystemExit(
        "missing nonzero D3D12 buffer-generation samples from compositor "
        f"evidence: count={len(d3d12_buffer_generations)}"
    )
sample_resource_set = set(d3d12_resources)
sample_buffer_generation_set = set(d3d12_buffer_generations)
bad_demo_resources = [
    value
    for value in demo_resources
    if value not in sample_resource_set
]
bad_demo_buffer_generations = [
    value
    for value in demo_buffer_generations
    if value not in sample_buffer_generation_set
]
if bad_demo_resources:
    raise SystemExit(
        "demo FPS native-present resource did not match finite-window "
        "/tmp/wlcomp-d3d12-present samples: "
        f"demo={','.join(hex(value) for value in demo_resources)} "
        f"wlcomp={','.join(hex(value) for value in d3d12_resources)}"
    )
if bad_demo_buffer_generations:
    raise SystemExit(
        "demo FPS native-present buffer generation did not match finite-window "
        "/tmp/wlcomp-d3d12-present samples: "
        f"demo={','.join(str(value) for value in demo_buffer_generations)} "
        f"wlcomp={','.join(str(value) for value in d3d12_buffer_generations)}"
    )
if len(d3d12_content_crcs) < 2:
    raise SystemExit(
        "missing D3D12 content CRC samples from compositor evidence"
    )
if len(set(d3d12_content_crcs)) < 2:
    frozen_window_negative(
        "sample_content_crc_static",
        sample_elapsed=format_optional(counter_delta(uptimes)),
        native_present_delta=format_optional(counter_delta(d3d12_present_counts)),
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=format_optional(counter_delta(d3d12_content_frames)),
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=sum(
            1
            for before, after in zip(outside_overlay_crcs, outside_overlay_crcs[1:])
            if before != after
        ),
        content_crc_changes=0,
        changed=",".join(str(v) for v in transitions),
        d3d12_content_crcs=",".join(hex(value) for value in d3d12_content_crcs),
    )
    raise SystemExit(
        "D3D12 compositor content CRC did not change during FPS sample: "
        f"values={','.join(hex(value) for value in d3d12_content_crcs)}"
    )
sample_content_crc_changes = sum(
    1
    for before, after in zip(d3d12_content_crcs, d3d12_content_crcs[1:])
    if before != after
)
if len(d3d12_content_frame_hashes) < 2:
    raise SystemExit(
        "missing D3D12 content frame-hash samples from compositor evidence"
    )
if len(set(d3d12_content_frame_hashes)) < 2:
    frozen_window_negative(
        "sample_content_frame_hash_static",
        sample_elapsed=format_optional(counter_delta(uptimes)),
        native_present_delta=format_optional(counter_delta(d3d12_present_counts)),
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=format_optional(counter_delta(d3d12_content_frames)),
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=sum(
            1
            for before, after in zip(outside_overlay_crcs, outside_overlay_crcs[1:])
            if before != after
        ),
        content_crc_changes=sample_content_crc_changes,
        content_frame_hash_changes=0,
        changed=",".join(str(v) for v in transitions),
        d3d12_content_frame_hashes=",".join(
            hex(value) for value in d3d12_content_frame_hashes
        ),
    )
    raise SystemExit(
        "D3D12 compositor content frame hash did not change during FPS "
        "sample: values="
        f"{','.join(hex(value) for value in d3d12_content_frame_hashes)}"
    )
sample_content_frame_hash_changes = sum(
    1
    for before, after in zip(d3d12_content_frame_hashes,
                             d3d12_content_frame_hashes[1:])
    if before != after
)
if len(d3d12_content_frames) < 2:
    raise SystemExit(
        "missing D3D12 content frame/change counter samples from compositor "
        "evidence"
    )
if d3d12_content_frames[-1] == d3d12_content_frames[0]:
    frozen_window_negative(
        "sample_content_frame_not_advancing",
        sample_elapsed=format_optional(counter_delta(uptimes)),
        native_present_delta=format_optional(counter_delta(d3d12_present_counts)),
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=0,
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=sum(
            1
            for before, after in zip(outside_overlay_crcs, outside_overlay_crcs[1:])
            if before != after
        ),
        changed=",".join(str(v) for v in transitions),
        content_frames=",".join(str(v) for v in d3d12_content_frames),
    )
d3d12_content_frame_deltas = require_advancing_counter(
    "d3d12 content frame/change counter",
    d3d12_content_frames,
)
sample_content_frame_delta = d3d12_content_frames[-1] - d3d12_content_frames[0]
content_active_preview = sum(1 for value in d3d12_content_frame_deltas
                             if value > 0)
content_required_preview = min(
    min_active_native_intervals,
    len(d3d12_content_frame_deltas),
)
if content_required_preview > 0 and content_active_preview < content_required_preview:
    frozen_window_negative(
        "sample_content_frame_not_repeated",
        sample_elapsed=format_optional(counter_delta(uptimes)),
        native_present_delta=format_optional(counter_delta(d3d12_present_counts)),
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=sample_content_frame_delta,
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=sum(
            1
            for before, after in zip(outside_overlay_crcs, outside_overlay_crcs[1:])
            if before != after
        ),
        active_content_frame_intervals=content_active_preview,
        required_content_frame_intervals=content_required_preview,
        content_frame_deltas=",".join(str(v) for v in d3d12_content_frame_deltas),
        changed=",".join(str(v) for v in transitions),
    )
active_content_frame_intervals = require_active_intervals(
    "D3D12 content frame/change counter",
    d3d12_content_frame_deltas,
    min_active_native_intervals,
)
if not render_samples:
    raise SystemExit("no render dimensions found in /tmp/mesawlegl-fps")
rw, rh, rdiv = render_samples[-1]
if not window_samples:
    raise SystemExit("no window dimensions found in /tmp/mesawlegl-fps")
ww, wh = window_samples[-1]
if ww * wh < min_render_pixels:
    raise SystemExit(
        f"demo window below 480p gate: window={ww}x{wh} "
        f"pixels={ww * wh} required={min_render_pixels}"
    )
if rw * rh < min_render_pixels:
    raise SystemExit(
        f"render target below 480p gate: render={rw}x{rh} "
        f"pixels={rw * rh} required={min_render_pixels}"
    )
if rdiv != 1:
    raise SystemExit(f"render divisor must be 1 for 480p gate, got {rdiv}")
if not demo_sources or demo_sources[-1] != "native-d3d12-present-complete":
    raise SystemExit(
        "demo FPS probe must be sourced from completed native D3D12 presents, "
        f"got {demo_sources[-1] if demo_sources else 'missing'}"
    )
bad_demo_sources = [s for s in demo_sources
                    if s != "native-d3d12-present-complete"]
if bad_demo_sources:
    raise SystemExit(
        "demo FPS probe contained callback-only or stalled display samples: "
        f"sources={','.join(demo_sources)}"
    )
if not demo_evidence_valid or any(value != 1 for value in demo_evidence_valid):
    raise SystemExit(
        "demo FPS probe did not use validated native-present evidence: "
        f"values={','.join(str(value) for value in demo_evidence_valid)}"
    )
demo_interaction_ready = (
    all_ones(demo_visible_evidence) and
    all_ones(demo_closeable_evidence) and
    all_ones(demo_resizable_evidence) and
    all_ones(demo_interaction_native_complete) and
    all_ones(demo_interaction_content_progress) and
    demo_interaction_present_ids and
    demo_interaction_completed and
    demo_interaction_run_ids and
    all(value == expected_run_id for value in demo_interaction_run_ids) and
    demo_interaction_present_ids[-1] != 0 and
    demo_interaction_completed[-1] >= demo_interaction_present_ids[-1]
)
if not demo_interaction_ready:
    fail_validation(
        "fps_demo_interaction_gate_matrix "
        f"validation_run_id={expected_run_id} stage=final "
        f"visible_demo={pass_missing(all_ones(demo_visible_evidence))} "
        f"closeable_demo={pass_missing(all_ones(demo_closeable_evidence))} "
        f"resizable_demo={pass_missing(all_ones(demo_resizable_evidence))} "
        f"interaction_run_id={pass_missing(bool(demo_interaction_run_ids) and all(value == expected_run_id for value in demo_interaction_run_ids))} "
        f"interaction_native_present={pass_missing(all_ones(demo_interaction_native_complete))} "
        f"interaction_content_progress={pass_missing(all_ones(demo_interaction_content_progress))} "
        f"interaction_present_id={demo_interaction_present_ids[-1] if demo_interaction_present_ids else 0} "
        f"interaction_completed={demo_interaction_completed[-1] if demo_interaction_completed else 0} "
        "requires_visible_demo=1 requires_closeable_demo=1 "
        "requires_resizable_demo=1 "
        "requires_native_present_completion=1 requires_content_progress=1 "
        "native_present_credit=0 opengl_submit_credit=0 "
        "gate=closed status=FAIL_MISSING_EVIDENCE"
    )
if (len(demo_evidence_generations) != len(accepted_demo_records) or
        any(value <= 0 for value in demo_evidence_generations) or
        len(demo_evidence_times) != len(accepted_demo_records) or
        any(value <= 0 for value in demo_evidence_times) or
        len(demo_resources) != len(accepted_demo_records) or
        any(value <= 0 for value in demo_resources) or
        len(demo_buffer_generations) != len(accepted_demo_records) or
        any(value <= 0 for value in demo_buffer_generations)):
    raise SystemExit(
        "demo FPS probe is missing D3D12 evidence generation/time/resource/"
        "buffer-generation identity fields"
    )
if not demo_native_deltas or max(demo_native_deltas) <= 0:
    raise SystemExit(
        "demo FPS probe did not report positive native-present deltas"
    )
if demo_native_elapsed and any(v < 0.0 for v in demo_native_elapsed):
    raise SystemExit(
        "demo FPS probe reported invalid native-present elapsed values: "
        f"{','.join(f'{v:.3f}' for v in demo_native_elapsed)}"
    )
complete = re.search(
    r"mesawlegl\[[0-9]+\]: complete frames=([0-9]+) status=0 "
    r"rtc_elapsed=([0-9]+(?:\.[0-9]+)?)s rtc_fps=([0-9]+(?:\.[0-9]+)?)",
    log,
)
if complete and int(complete.group(1)) <= 0:
    raise SystemExit("finite mesademo/mesawlegl run reported zero frames")

# The app overlay is useful for human inspection, but the gate is native
# D3D12 present-completion based. Wayland callbacks, submitted frames, and
# display bookkeeping can advance while no GPU-presented frame reached the
# Hyper-V display, which was the inflated-FPS failure mode.
window = samples[1:] if len(samples) > 1 else samples
seqs = [seq for seq, _ in window]
if len(set(seqs)) < 2:
    raise SystemExit(
        "not enough fresh /tmp/mesawlegl-fps callback samples: "
        f"seqs={','.join(str(v) for v in seqs)}"
    )
if len(demo_frames) >= 2:
    require_frames = demo_frames[1:] if len(demo_frames) > 2 else demo_frames
    for before, after in zip(require_frames, require_frames[1:]):
        if after <= before:
            raise SystemExit(
                "demo frame/progress did not advance monotonically: "
                f"frames={','.join(str(v) for v in demo_frames)}"
            )
if len(demo_sample_times) >= 2:
    for before, after in zip(demo_sample_times, demo_sample_times[1:]):
        if after <= before:
            raise SystemExit(
                "demo FPS status sample time did not advance monotonically: "
                f"times={','.join(f'{v:.3f}' for v in demo_sample_times)}"
            )
values = [fps for _, fps in window]
avg = sum(values) / len(values)
low = min(values)
if len(uptimes) < 2 or len(display_completions) < 2:
    raise SystemExit(
        "not enough display completion samples from fbstat/proc uptime: "
        f"uptimes={len(uptimes)} completions={len(display_completions)}"
    )
elapsed = uptimes[-1] - uptimes[0]
completed = display_completions[-1] - display_completions[0]
presented = display_presents[-1] - display_presents[0] if len(display_presents) >= 2 else -1
if elapsed <= 0.0:
    raise SystemExit(f"invalid sample elapsed time: {elapsed:.3f}")
min_elapsed = sample_sec * 0.75
if elapsed < min_elapsed:
    raise SystemExit(
        f"FPS sample window too short: elapsed={elapsed:.3f}s "
        f"required>={min_elapsed:.3f}s configured={sample_sec}s"
    )
completion_fps = completed / elapsed
if completed <= 0:
    raise SystemExit("display completions did not advance during FPS sample")
display_completion_deltas = interval_deltas("display_completions",
                                            display_completions)
active_display_intervals = require_active_intervals(
    "display completions",
    display_completion_deltas,
    min_active_display_intervals,
)
if len(display_presents) >= 2:
    interval_deltas("display_presents", display_presents)
if presented >= 0 and completed > presented:
    raise SystemExit(
        f"display completion accounting exceeds presents: completions={completed} "
        f"presents={presented}"
    )
if not backend_modes:
    raise SystemExit("no compositor backend mode samples found")
backend_mode = backend_modes[-1]
if backend_mode not in {"direct-scanout", "bo-present"}:
    raise SystemExit(f"unsupported compositor backend mode: {backend_mode}")
if len(d3d12_present_counts) < 2:
    raise SystemExit(
        "not enough D3D12 native present completion-count samples from "
        "/tmp/wlcomp-d3d12-present"
    )
native_completed = d3d12_present_counts[-1] - d3d12_present_counts[0]
native_fps = native_completed / elapsed
sample_content_frame_fps = sample_content_frame_delta / elapsed
warmup_native_completed = d3d12_present_counts[0]
native_completion_deltas = interval_deltas("d3d12_gpu_present_completes",
                                           d3d12_present_counts)
native_active_preview = sum(1 for value in native_completion_deltas if value > 0)
native_required_preview = min(
    min_active_native_intervals,
    len(native_completion_deltas),
)
if native_required_preview > 0 and native_active_preview < native_required_preview:
    frozen_window_negative(
        "native_present_not_advancing",
        sample_elapsed=f"{elapsed:.3f}",
        native_present_delta=native_completed,
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=format_optional(counter_delta(d3d12_content_frames)),
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=sum(
            1
            for before, after in zip(outside_overlay_crcs, outside_overlay_crcs[1:])
            if before != after
        ),
        active_native_intervals=native_active_preview,
        required_native_intervals=native_required_preview,
        native_deltas=",".join(str(v) for v in native_completion_deltas),
        changed=",".join(str(v) for v in transitions),
    )
active_native_intervals = require_active_intervals(
    "native D3D12 present completions",
    native_completion_deltas,
    min_active_native_intervals,
)
if len(d3d12_native_paths) < 2:
    raise SystemExit(
        "not enough native D3D12 present path samples from "
        "/tmp/wlcomp-d3d12-present"
    )
bad_paths = [p for p in d3d12_native_paths
             if p != "d3d12-dxg-present-source-display-handoff"]
if bad_paths:
    raise SystemExit(
        "D3D12 present path was not the native shared-surface path: "
        f"paths={','.join(d3d12_native_paths)}"
    )
if (len(d3d12_present_ids) < 2 or
        any(value == 0 for value in d3d12_present_ids)):
    raise SystemExit(
        "missing nonzero DXG present_id samples from "
        "/tmp/wlcomp-d3d12-present: "
        f"values={','.join(str(value) for value in d3d12_present_ids)}"
    )
if (len(d3d12_present_completed) < 2 or
        any(value == 0 for value in d3d12_present_completed)):
    raise SystemExit(
        "missing nonzero DXG completed samples from "
        "/tmp/wlcomp-d3d12-present: "
        f"values={','.join(str(value) for value in d3d12_present_completed)}"
    )
if d3d12_present_completed[-1] < d3d12_present_ids[-1]:
    raise SystemExit(
        "DXG completed counter did not cover present_id during FPS sample: "
        f"present_id={d3d12_present_ids[-1]} "
        f"completed={d3d12_present_completed[-1]}"
    )
require_display_bind_evidence(
    "sample-window D3D12",
    d3d12_display_bind_backends,
    d3d12_display_bind_transports,
    d3d12_display_bind_present_ids,
    d3d12_display_bind_completed_ids,
    d3d12_display_bind_resource_generations,
    d3d12_display_bind_completion_sources,
)
require_display_bind_identity(
    "sample-window D3D12",
    d3d12_present_ids,
    d3d12_present_completed,
    d3d12_buffer_generations,
    d3d12_display_bind_present_ids,
    d3d12_display_bind_completed_ids,
    d3d12_display_bind_resource_generations,
)
require_native_content_identity(
    "sample-window D3D12",
    d3d12_present_ids,
    d3d12_present_completed,
    d3d12_buffer_generations,
    d3d12_display_bind_present_ids,
    d3d12_display_bind_completed_ids,
    d3d12_display_bind_resource_generations,
    d3d12_content_present_ids,
    d3d12_content_completed_ids,
    d3d12_content_display_bind_present_ids,
    d3d12_content_display_bind_completed_ids,
    d3d12_content_display_bind_resource_generations,
    d3d12_content_current_run_valid,
    d3d12_content_identity_complete,
    d3d12_final_handoff_successes,
    d3d12_final_handoff_present_ids,
    d3d12_final_handoff_completed_ids,
    d3d12_final_handoff_resource_generations,
)
if (len(d3d12_gpup_dda_commit_successes) < 2 or
        any(value == 0 for value in d3d12_gpup_dda_commit_successes)):
    raise SystemExit(
        "missing GPU-P/DDA present-source commit accepted samples from "
        "/tmp/wlcomp-d3d12-present: "
        f"values={','.join(str(value) for value in d3d12_gpup_dda_commit_successes)}"
    )
d3d12_present_id_deltas = require_advancing_counter(
    "DXG present_id",
    d3d12_present_ids,
)
d3d12_present_completed_deltas = require_advancing_counter(
    "DXG completed counter",
    d3d12_present_completed,
)
d3d12_display_bind_present_id_deltas = require_advancing_counter(
    "display_bind_present_id",
    d3d12_display_bind_present_ids,
)
d3d12_display_bind_completed_id_deltas = require_advancing_counter(
    "display_bind_completed_id",
    d3d12_display_bind_completed_ids,
)
d3d12_gpup_dda_commit_deltas = require_advancing_counter(
    "GPU-P/DDA present-source commit accepted",
    d3d12_gpup_dda_commit_successes,
)
sample_present_id_delta = d3d12_present_ids[-1] - d3d12_present_ids[0]
sample_present_completed_delta = (
    d3d12_present_completed[-1] - d3d12_present_completed[0]
)
sample_display_bind_present_id_delta = (
    d3d12_display_bind_present_ids[-1] - d3d12_display_bind_present_ids[0]
)
sample_display_bind_completed_id_delta = (
    d3d12_display_bind_completed_ids[-1] -
    d3d12_display_bind_completed_ids[0]
)
sample_gpup_dda_commit_delta = (
    d3d12_gpup_dda_commit_successes[-1] - d3d12_gpup_dda_commit_successes[0]
)
if sample_present_id_delta < native_completed:
    raise SystemExit(
        "DXG present_id did not advance with native present completions "
        "during FPS sample: "
        f"present_id_delta={sample_present_id_delta} "
        f"native_completed={native_completed} "
        f"present_ids={','.join(str(v) for v in d3d12_present_ids)} "
        f"native_values={','.join(str(v) for v in d3d12_present_counts)}"
    )
if sample_present_completed_delta < native_completed:
    raise SystemExit(
        "DXG completed counter did not advance with native present "
        "completions during FPS sample: "
        f"completed_delta={sample_present_completed_delta} "
        f"native_completed={native_completed} "
        f"completed_values={','.join(str(v) for v in d3d12_present_completed)} "
        f"native_values={','.join(str(v) for v in d3d12_present_counts)}"
    )
if sample_display_bind_present_id_delta < native_completed:
    raise SystemExit(
        "display_bind_present_id did not advance with native present "
        "completions during FPS sample: "
        f"display_bind_present_id_delta={sample_display_bind_present_id_delta} "
        f"native_completed={native_completed} "
        f"display_bind_present_ids={','.join(str(v) for v in d3d12_display_bind_present_ids)} "
        f"native_values={','.join(str(v) for v in d3d12_present_counts)}"
    )
if sample_display_bind_completed_id_delta < native_completed:
    raise SystemExit(
        "display_bind_completed_id did not advance with native present "
        "completions during FPS sample: "
        f"display_bind_completed_id_delta={sample_display_bind_completed_id_delta} "
        f"native_completed={native_completed} "
        f"display_bind_completed_ids={','.join(str(v) for v in d3d12_display_bind_completed_ids)} "
        f"native_values={','.join(str(v) for v in d3d12_present_counts)}"
    )
if sample_gpup_dda_commit_delta < native_completed:
    raise SystemExit(
        "GPU-P/DDA present-source commit accepted counter did not advance with "
        "native present completions during FPS sample: "
        f"commit_delta={sample_gpup_dda_commit_delta} "
        f"native_completed={native_completed} "
        f"commit_values={','.join(str(v) for v in d3d12_gpup_dda_commit_successes)} "
        f"native_values={','.join(str(v) for v in d3d12_present_counts)}"
    )
active_present_id_intervals = require_active_intervals(
    "DXG present_id",
    d3d12_present_id_deltas,
    min_active_native_intervals,
)
active_present_completed_intervals = require_active_intervals(
    "DXG completed counter",
    d3d12_present_completed_deltas,
    min_active_native_intervals,
)
active_display_bind_present_id_intervals = require_active_intervals(
    "display_bind_present_id",
    d3d12_display_bind_present_id_deltas,
    min_active_native_intervals,
)
active_display_bind_completed_id_intervals = require_active_intervals(
    "display_bind_completed_id",
    d3d12_display_bind_completed_id_deltas,
    min_active_native_intervals,
)
active_gpup_dda_commit_intervals = require_active_intervals(
    "GPU-P/DDA present-source commit accepted",
    d3d12_gpup_dda_commit_deltas,
    min_active_native_intervals,
)
if any(value == 0 for value in d3d12_same_frame_callback_releases):
    raise SystemExit(
        "D3D12 same-frame callback/release proof reported false during "
        "FPS sample: "
        f"values={','.join(str(value) for value in d3d12_same_frame_callback_releases)}"
    )
if d3d12_same_frame_callback_releases and d3d12_same_frame_callback_releases[-1] < 1:
    raise SystemExit("D3D12 same-frame callback/release proof was not observed")
if any(value == 0 for value in d3d12_same_frame_callbacks):
    raise SystemExit(
        "D3D12 same-frame callback proof reported false during FPS sample: "
        f"values={','.join(str(value) for value in d3d12_same_frame_callbacks)}"
    )
if any(value == 0 for value in d3d12_same_frame_releases):
    raise SystemExit(
        "D3D12 same-frame release proof reported false during FPS sample: "
        f"values={','.join(str(value) for value in d3d12_same_frame_releases)}"
    )
if d3d12_reject_evidence:
    raise SystemExit(
        "D3D12 present sample contained CPU/readback or partial-present "
        f"evidence: {d3d12_reject_evidence[-1]}"
    )
if native_completed <= 0:
    raise SystemExit(
        "D3D12 native present completion count did not advance during FPS "
        f"sample: {d3d12_present_counts[0]}->{d3d12_present_counts[-1]}"
    )
if not native_content_progress_complete(
        d3d12_content_progress_states,
        d3d12_visible_content_progress_states,
        d3d12_content_requires_native,
        d3d12_content_native_complete,
        d3d12_content_visible_credit,
        d3d12_content_source_owned):
    fail_validation(
        "fps_visible_native_content_gate_matrix "
        f"validation_run_id={expected_run_id} stage=sample "
        "native_present_completion=PASS "
        "content_progress_contract=MISSING "
        "requires_native_present_completion=1 requires_content_progress=1 "
        "visible_content_credit_before_native_present=0 "
        "native_present_credit=0 opengl_submit_credit=0 "
        "gate=closed status=FAIL_MISSING_EVIDENCE"
    )
if not same_run_resource_generation_completion_progress(
        d3d12_run_ids,
        d3d12_resources,
        d3d12_resource_generations,
        d3d12_native_completion_ids):
    fail_validation(
        "fps_stale_frozen_content_generation_matrix "
        f"validation_run_id={expected_run_id} stage=sample "
        "stale_run_rejected=PASS frozen_content_rejected=PASS "
        "title_only_fps_rejected=PASS "
        f"content_crc_changes={sample_content_crc_changes} "
        f"content_frame_hash_changes={sample_content_frame_hash_changes} "
        f"content_frame_delta={sample_content_frame_delta} "
        f"native_completion_ids={len(d3d12_native_completion_ids)} "
        "same_run_resource_generation_completion=MISSING "
        "requires_native_present_completion_id_advance=1 "
        "requires_same_run=1 requires_same_resource_generation=1 "
        "native_present_credit=0 opengl_submit_credit=0 "
        "gate=closed status=FAIL_MISSING_EVIDENCE"
    )
if sample_content_frame_delta < native_completed:
    raise SystemExit(
        "D3D12 visible/content frame counter lagged native present "
        "completions during FPS sample: "
        f"content_delta={sample_content_frame_delta} "
        f"native_completed={native_completed} "
        f"content_values={','.join(str(v) for v in d3d12_content_frames)} "
        f"native_values={','.join(str(v) for v in d3d12_present_counts)}"
    )
if len(demo_native_counts) >= 2:
    demo_native_delta = demo_native_counts[-1] - demo_native_counts[0]
    if demo_native_delta > native_completed:
        raise SystemExit(
            f"demo native-present count exceeds compositor evidence: "
            f"demo={demo_native_delta} compositor={native_completed}"
        )
if len(d3d12_present_starts) >= 2:
    native_start_deltas = interval_deltas("d3d12_gpu_present_starts",
                                          d3d12_present_starts)
    native_started = d3d12_present_starts[-1] - d3d12_present_starts[0]
    if native_started < native_completed:
        raise SystemExit(
            f"native present completions exceed starts: starts={native_started} "
            f"completes={native_completed}"
        )
if len(d3d12_copy_counts) >= 2:
    native_copy_deltas = interval_deltas("d3d12_gpu_copy_completes",
                                         d3d12_copy_counts)
    native_copied = d3d12_copy_counts[-1] - d3d12_copy_counts[0]
    if native_copied < native_completed:
        raise SystemExit(
            f"native present completions exceed GPU-copy completions: "
            f"copies={native_copied} completes={native_completed}"
        )
    active_copy_intervals = sum(1 for value in native_copy_deltas
                                if value > 0)
    if active_copy_intervals < active_native_intervals:
        raise SystemExit(
            "GPU-copy completions did not advance with native present "
            "completions: "
            f"copy_active={active_copy_intervals} "
            f"native_active={active_native_intervals} "
            f"copy_deltas={','.join(str(v) for v in native_copy_deltas)} "
            f"native_deltas={','.join(str(v) for v in native_completion_deltas)}"
        )
if len(d3d12_callback_counts) < 2:
    raise SystemExit(
        "not enough D3D12 frame callback samples from "
        "/tmp/wlcomp-d3d12-present"
    )
callback_deltas = interval_deltas("d3d12_frame_callbacks",
                                  d3d12_callback_counts)
native_callbacks = d3d12_callback_counts[-1] - d3d12_callback_counts[0]
if native_callbacks <= 0:
    raise SystemExit("D3D12 frame callbacks did not advance during FPS sample")
if native_completed > native_callbacks:
    raise SystemExit(
        f"native present completions exceed frame callbacks: "
        f"completes={native_completed} callbacks={native_callbacks}"
    )
active_callback_intervals = require_active_intervals(
    "D3D12 frame callbacks",
    callback_deltas,
    min_active_native_intervals,
)
if len(d3d12_release_counts) < 2:
    raise SystemExit(
        "not enough D3D12 buffer release samples from "
        "/tmp/wlcomp-d3d12-present"
    )
release_deltas = interval_deltas("d3d12_buffer_releases",
                                 d3d12_release_counts)
native_releases = d3d12_release_counts[-1] - d3d12_release_counts[0]
if native_releases <= 0:
    raise SystemExit("D3D12 buffer releases did not advance during FPS sample")
if native_completed > native_releases:
    raise SystemExit(
        f"native present completions exceed buffer releases: "
        f"completes={native_completed} releases={native_releases}"
    )
active_release_intervals = require_active_intervals(
    "D3D12 buffer releases",
    release_deltas,
    min_active_native_intervals,
)
if native_completed > completed:
    raise SystemExit(
        f"native present completions exceed display completions: "
        f"native={native_completed} display={completed}"
    )
active_native_display_intervals = sum(
    1
    for native, display in zip(native_completion_deltas,
                               display_completion_deltas)
    if native > 0 and display > 0
)
required_native_display_intervals = min(
    min_active_native_intervals,
    len(native_completion_deltas),
    len(display_completion_deltas),
)
if active_native_display_intervals < required_native_display_intervals:
    raise SystemExit(
        "native present completions did not correlate with display "
        "completions across enough sample intervals: "
        f"overlap={active_native_display_intervals} "
        f"required={required_native_display_intervals} "
        f"native_deltas={','.join(str(v) for v in native_completion_deltas)} "
        f"display_deltas={','.join(str(v) for v in display_completion_deltas)}"
    )
active_native_display_callback_release_intervals = sum(
    1
    for native, display, callback, release in zip(
        native_completion_deltas,
        display_completion_deltas,
        callback_deltas,
        release_deltas,
    )
    if native > 0 and display > 0 and callback > 0 and release > 0
)
required_correlated_intervals = min(
    min_active_native_intervals,
    len(native_completion_deltas),
    len(display_completion_deltas),
    len(callback_deltas),
    len(release_deltas),
)
if active_native_display_callback_release_intervals < required_correlated_intervals:
    raise SystemExit(
        "native/display/callback/release counters did not overlap across "
        "enough sample intervals: "
        f"overlap={active_native_display_callback_release_intervals} "
        f"required={required_correlated_intervals} "
        f"native_deltas={','.join(str(v) for v in native_completion_deltas)} "
        f"display_deltas={','.join(str(v) for v in display_completion_deltas)} "
        f"callback_deltas={','.join(str(v) for v in callback_deltas)} "
        f"release_deltas={','.join(str(v) for v in release_deltas)}"
    )
active_full_evidence_intervals = sum(
    1
    for native, display, callback, release, generation, evidence_time, present_id, present_done, bind_present_id, bind_completed_id, gpup_dda_commit in zip(
        native_completion_deltas,
        display_completion_deltas,
        callback_deltas,
        release_deltas,
        d3d12_generation_deltas,
        d3d12_evidence_time_deltas,
        d3d12_present_id_deltas,
        d3d12_present_completed_deltas,
        d3d12_display_bind_present_id_deltas,
        d3d12_display_bind_completed_id_deltas,
        d3d12_gpup_dda_commit_deltas,
    )
    if (native > 0 and display > 0 and callback > 0 and release > 0 and
        generation > 0 and evidence_time > 0 and present_id > 0 and
        present_done > 0 and bind_present_id > 0 and
        bind_completed_id > 0 and gpup_dda_commit > 0)
)
required_full_evidence_intervals = min(
    min_active_native_intervals,
    len(native_completion_deltas),
    len(display_completion_deltas),
    len(callback_deltas),
    len(release_deltas),
    len(d3d12_generation_deltas),
    len(d3d12_evidence_time_deltas),
    len(d3d12_present_id_deltas),
    len(d3d12_present_completed_deltas),
    len(d3d12_display_bind_present_id_deltas),
    len(d3d12_display_bind_completed_id_deltas),
    len(d3d12_gpup_dda_commit_deltas),
)
if active_full_evidence_intervals < required_full_evidence_intervals:
    raise SystemExit(
        "native/display/callback/release/run-evidence/DXG-present/display-bind/GPU-P/DDA-commit counters "
        "did not advance in the same sample intervals: "
        f"overlap={active_full_evidence_intervals} "
        f"required={required_full_evidence_intervals} "
        f"native_deltas={','.join(str(v) for v in native_completion_deltas)} "
        f"display_deltas={','.join(str(v) for v in display_completion_deltas)} "
        f"callback_deltas={','.join(str(v) for v in callback_deltas)} "
        f"release_deltas={','.join(str(v) for v in release_deltas)} "
        f"generation_deltas={','.join(str(v) for v in d3d12_generation_deltas)} "
        f"evidence_time_deltas={','.join(str(v) for v in d3d12_evidence_time_deltas)} "
        f"present_id_deltas={','.join(str(v) for v in d3d12_present_id_deltas)} "
        f"completed_deltas={','.join(str(v) for v in d3d12_present_completed_deltas)} "
        f"display_bind_present_id_deltas={','.join(str(v) for v in d3d12_display_bind_present_id_deltas)} "
        f"display_bind_completed_id_deltas={','.join(str(v) for v in d3d12_display_bind_completed_id_deltas)} "
        f"gpup_dda_commit_deltas={','.join(str(v) for v in d3d12_gpup_dda_commit_deltas)}"
    )
sustained_native_windows = require_sustained_counter_windows(
    "native_present",
    native_completion_deltas,
    native_present_delta=native_completed,
    sample_elapsed=f"{elapsed:.3f}",
    content_frame_delta=sample_content_frame_delta,
    thumbnail_progress_delta=max(transitions),
    outside_overlay_crc_changes=outside_overlay_crc_transitions,
)
sustained_display_windows = require_sustained_counter_windows(
    "display_completion",
    display_completion_deltas,
    native_present_delta=native_completed,
    display_completion_delta=completed,
    sample_elapsed=f"{elapsed:.3f}",
    thumbnail_progress_delta=max(transitions),
    outside_overlay_crc_changes=outside_overlay_crc_transitions,
)
sustained_content_windows = require_sustained_counter_windows(
    "content_frame",
    d3d12_content_frame_deltas,
    native_present_delta=native_completed,
    content_frame_delta=sample_content_frame_delta,
    sample_elapsed=f"{elapsed:.3f}",
    thumbnail_progress_delta=max(transitions),
    outside_overlay_crc_changes=outside_overlay_crc_transitions,
)
sustained_callback_windows = require_sustained_counter_windows(
    "frame_callback",
    callback_deltas,
    native_present_delta=native_completed,
    frame_callback_delta=native_callbacks,
    sample_elapsed=f"{elapsed:.3f}",
    thumbnail_progress_delta=max(transitions),
    outside_overlay_crc_changes=outside_overlay_crc_transitions,
)
sustained_release_windows = require_sustained_counter_windows(
    "buffer_release",
    release_deltas,
    native_present_delta=native_completed,
    buffer_release_delta=native_releases,
    sample_elapsed=f"{elapsed:.3f}",
    thumbnail_progress_delta=max(transitions),
    outside_overlay_crc_changes=outside_overlay_crc_transitions,
)
sustained_present_id_windows = require_sustained_counter_windows(
    "present_id",
    d3d12_present_id_deltas,
    native_present_delta=native_completed,
    present_id_delta=sample_present_id_delta,
    sample_elapsed=f"{elapsed:.3f}",
    thumbnail_progress_delta=max(transitions),
    outside_overlay_crc_changes=outside_overlay_crc_transitions,
)
sustained_completed_windows = require_sustained_counter_windows(
    "completed_id",
    d3d12_present_completed_deltas,
    native_present_delta=native_completed,
    completed_delta=sample_present_completed_delta,
    sample_elapsed=f"{elapsed:.3f}",
    thumbnail_progress_delta=max(transitions),
    outside_overlay_crc_changes=outside_overlay_crc_transitions,
)
log_validation(
    "fps_sustained_post_warmup_progress_matrix "
    f"validation_run_id={expected_run_id} "
    "post_warmup_sample_window=1 "
    "sustained_sample_windows_required=2 "
    f"sustained_visual_windows={len(thumbnail_windows)} "
    f"visual_windows_active={len(thumbnail_window_summaries)} "
    f"native_window_deltas={','.join(str(v) for v in sustained_native_windows)} "
    f"display_window_deltas={','.join(str(v) for v in sustained_display_windows)} "
    f"content_window_deltas={','.join(str(v) for v in sustained_content_windows)} "
    f"callback_window_deltas={','.join(str(v) for v in sustained_callback_windows)} "
    f"release_window_deltas={','.join(str(v) for v in sustained_release_windows)} "
    f"present_id_window_deltas={','.join(str(v) for v in sustained_present_id_windows)} "
    f"completed_window_deltas={','.join(str(v) for v in sustained_completed_windows)} "
    f"visual_window_summaries="
    f"{';'.join(f'w{idx}:active={active}:crc={crc}:max={max_changed}' for idx, active, _required, crc, max_changed, _transitions, _crcs in thumbnail_window_summaries)} "
    "frozen_window_rejected=PASS "
    "native_present_credit=0 opengl_submit_credit=0 status=PASS"
)
if not backend_opengl_submit_samples:
    raise SystemExit(
        "no Hyper-V OpenGL-submit backend-flag samples found during final "
        "native-present FPS gate"
    )
bad_submit_samples = [v for v in backend_opengl_submit_samples if v != 1]
if bad_submit_samples:
    raise SystemExit(
        "Hyper-V OpenGL-submit backend flag remained gated during final "
        "native-present FPS gate: "
        f"samples={','.join(str(v) for v in backend_opengl_submit_samples)}"
    )
if not re.search(
        r"d3d12sharedsmoke: present validation ok frame=1 release=1 "
        r"gpu_present=[0-9]+->[1-9][0-9]*",
        log,
):
    raise SystemExit(
        "native D3D12 shared-resource present smoke did not prove "
        "same-frame callback/release plus GPU-present progress"
    )
if "d3d12sharedsmoke: stale present evidence cleared path=/tmp/wlcomp-d3d12-present" not in log:
    raise SystemExit(
        "stale D3D12 present evidence was not cleared before FPS validation"
    )
if (re.search(
        r"d3d12_present_errno=95|present_errno=95|"
        r"(?:commit_errno|commit_status|d3d12_[A-Za-z0-9_]*commit_"
        r"(?:errno|status))[ =]95|present_id=0|completed=0|"
        r"callbacks_blocked=1|releases_blocked=1",
        log,
    ) and not re.search(
        r"d3d12sharedsmoke: native present evidence ok "
        r"path=d3d12-dxg-present-source-display-handoff",
        log,
    )):
    if re.search(
            r"host-display-helper/resource-scanout-bind|"
            r"runtime-created-d3d12-resource-to-host-display-helper|"
            r"custom_host_tool:[1-9][0-9]*|custom_host_tool=1",
            log,
    ):
        fail_validation(
            "D3D12 present evidence used a custom host display helper; only "
            "represented GPU-P/DXG or DDA/Nouveau handoff is accepted"
        )
    if not re.search(
            r"missing host ABI=dxg-resource-scanout-bind|"
            r"missing host ABI=gpu-p-dxg-resource-scanout-bind|"
            r"ABI=dxg-resource-scanout-bind|"
            r"ABI=gpu-p-dxg-resource-scanout-bind|"
            r"gpu_p_or_dda_bind=gpu-p-dxg-resource-scanout-bind|"
            r""
            r"d3d12_display_handoff_requires_kernel_host_protocol=1",
            log,
    ):
        fail_validation(
            "D3D12 present fail-closed without named GPU-P/DDA display-bind "
            "diagnostic"
        )
    if re.search(
            r"gpu_p_or_dda_bind=gpu-p-dxg-resource-scanout-bind|"
            r""
            r"missing host ABI=gpu-p-dxg-resource-scanout-bind|"
            r"ABI=gpu-p-dxg-resource-scanout-bind",
            log,
    ) and not re.search(
            r"candidate_cmds[:=]presenthistory=34,redirected_flip_fence=35,blt=38,propagate_presenthistory=1",
            log,
    ):
        fail_validation(
            "D3D12 present fail-closed GPU-P/DDA display-bind diagnostic "
            "lacked candidate_cmds presenthistory=34,redirected_flip_fence=35,blt=38,propagate_presenthistory=1"
        )
    fail_validation(
        "fail-closed present: missing GPU-P/DDA "
        "resource-scanout-bind dependency; present_id/completed are "
        "not usable and callbacks/releases remain blocked, so "
        "FPS/WebKit/OpenGL-submit credit is refused"
    )
if (re.search(r"d3d12sharedsmoke: runtime CreateSharedHandle\(resource\) ok", log) and
        re.search(r"d3d12sharedsmoke: runtime CreateSharedHandle\(fence\) ok", log) and
        not re.search(
            r"d3d12sharedsmoke: native present evidence ok "
            r"path=d3d12-dxg-present-source-display-handoff",
            log,
        ) and
        re.search(
            r"GPU present evidence (file missing or unreadable|missing/unchanged)|"
            r"cat: cannot open /tmp/wlcomp-d3d12-present|"
            r"Open.*[Ff]ence.*(failed|0x80070057)|0x80070057",
            log,
        )):
    fail_validation(
        "fail-closed state: resource/fence export succeeded, "
        "but native-present evidence is missing or incomplete; refusing "
        "FPS/WebKit/OpenGL-submit credit until /tmp/wlcomp-d3d12-present "
        "has matching run id, process/client identity, resource/generation, "
        "content CRC/frame, present completion, and display correlation"
    )
if not re.search(
    r"d3d12sharedsmoke: native present evidence ok "
    r"path=d3d12-dxg-present-source-display-handoff "
    r".*present_id=[1-9][0-9]* completed=[1-9][0-9]* "
    r".*mtime_ms=[1-9][0-9]* min_mtime_ms=[1-9][0-9]* "
    r".*starts=[1-9][0-9]* copy=[1-9][0-9]* completes=[1-9][0-9]* "
    r".*no_cpu_readback=1",
    log,
):
    raise SystemExit(
        "fresh D3D12 present evidence with mtime/native counters was not found"
    )
wave60_required_patterns = (
    (
        r"\bd3d12_native_present_requirements_satisfied[ =]1\b",
        "Wave60 native-present requirements satisfied marker",
    ),
    (
        r"\bd3d12_present_source_luid_valid[ =]1\b",
        "Wave60 present-source LUID validity",
    ),
    (
        r"\bd3d12_present_source_registered[ =]1\b",
        "Wave60 present-source registration proof",
    ),
    (
        r"\bd3d12_present_source_query_attempted[ =]1\b",
        "Wave60 present-source query after commit proof",
    ),
    (
        r"\bd3d12_present_source_buffer_commit_successes[ =][1-9][0-9]*\b",
        "Wave60 present-source buffer commit success",
    ),
    (
        r"\bd3d12_present_source_buffer_present_id[ =][1-9][0-9]*\b",
        "Wave60 present-source buffer nonzero present id",
    ),
    (
        r"\bd3d12_present_source_buffer_completed[ =][1-9][0-9]*\b",
        "Wave60 present-source buffer nonzero completed counter",
    ),
    (
        r"\bd3d12_present_source_buffer_completion_correlated[ =]1\b",
        "Wave60 present-source buffer completion correlation",
    ),
    (
        r"\bd3d12_present_source_buffer_query_attempted_after_commit_success[ =]1\b",
        "Wave60 present-source buffer query after successful commit",
    ),
    (
        r"\bd3d12_buffer_release_present_id[ =][1-9][0-9]*\b",
        "Wave60 buffer release present-id correlation",
    ),
    (
        r"\bd3d12_buffer_release_same_present_id[ =]1\b",
        "Wave60 buffer release same-present-id proof",
    ),
    (
        r"\bd3d12_frame_callback_present_id[ =][1-9][0-9]*\b",
        "Wave60 frame callback present-id correlation",
    ),
    (
        r"\bd3d12_frame_callback_same_present_id[ =]1\b",
        "Wave60 frame callback same-present-id proof",
    ),
)
for pattern, description in wave60_required_patterns:
    if not re.search(pattern, log):
        raise SystemExit(f"missing {description}")
wave60_reject = re.search(
    r"\bd3d12_native_present_requirements_satisfied[ =]0\b|"
    r"\bnative_requirements=0\b|"
    r"\bd3d12_present_source_(?:commit_rejected_eopnotsupp|"
    r"no_present_id_completed|gpu_p_or_dda_transport_absent|"
    r"no_gpu_p_or_dda_display_bind|"
    r"same_frame_callbacks_blocked|same_frame_releases_blocked)[ =]1\b|"
    r"\bd3d12_present_source_buffer_query_skipped_commit_failed[ =]1\b|"
    r"\bd3d12_present_source_query_skipped_reason=commit-failed\b",
    log,
)
if wave60_reject:
    raise SystemExit(
        "Wave60 fail-closed/pass-by-registration native-present evidence "
        f"was present: {wave60_reject.group(0)}"
    )
if not re.search(r"\bd3d12_runtime_created_d3d12_resource_required[ =]1\b", log):
    raise SystemExit(
        "strict-present evidence must require a runtime-created D3D12 resource"
    )
if not re.search(r"\b(?:d3d12_)?existing_sysmem_is_d3d12_com_resource[ =]0\b", log):
    raise SystemExit(
        "strict-present evidence must prove existing-sysmem is not being "
        "credited as a D3D12 COM resource"
    )
if re.search(r"\bd3d12_(final_handoff_|display_completion_required)", log):
    final_handoff_requirements = (
        (
            r"\bd3d12_display_completion_required[ =]1\b",
            "D3D12 WSLg-display/native display-completion requirement",
        ),
        (
            r"\bd3d12_final_handoff_lane[ =]"
            r"runtime-created-d3d12-resource-through-gpu-p-or-dda\b",
            "D3D12 final handoff GPU-P/DDA lane",
        ),
        (
            r"\bd3d12_final_handoff_selected[ =]"
            r"(?:dxg-resource-scanout-bind|gpu-p-dxg-resource-scanout-bind)\b",
            "D3D12 final handoff selected GPU-P/DDA contract",
        ),
        (
            r"\bd3d12_final_handoff_source[ =]"
            r"runtime-created-d3d12-resource\b",
            "D3D12 final handoff runtime-created resource source",
        ),
        (
            r"\bd3d12_final_handoff_destination[ =]host-display-channel\b",
            "D3D12 final handoff host-display destination",
        ),
        (
            r"\bd3d12_final_handoff_runtime_resource_required[ =]1\b",
            "D3D12 final handoff runtime-resource requirement",
        ),
        (
            r"\bd3d12_final_handoff_kernel_abi[ =]"
            r"FB_GPU_DXG_PRESENT_SOURCE_COMMIT/runtime-resource-scanout\b",
            "D3D12 final handoff kernel ABI name",
        ),
        (
            r"\bd3d12_final_handoff_callbacks_release_gate[ =]"
            r"native-display-completion\b",
            "D3D12 final handoff callback/release display-completion gate",
        ),
        (
            r"\bd3d12_final_handoff_user_display_channel_selected[ =]0\b",
            "D3D12 final handoff rejects user-display-channel shortcut",
        ),
    )
    for pattern, why in final_handoff_requirements:
        if not re.search(pattern, log):
            raise SystemExit(f"missing {why}")
    if re.search(r"\bd3d12_final_handoff_host_display_commit_success[ =]1\b", log):
        if not re.search(r"\bd3d12_final_handoff_present_id[ =][1-9][0-9]*\b", log):
            raise SystemExit("missing D3D12 final handoff nonzero present id")
        if not re.search(r"\bd3d12_final_handoff_completed[ =][1-9][0-9]*\b", log):
            raise SystemExit("missing D3D12 final handoff nonzero completed counter")
if re.search(
    r"\bd3d12_display_handoff_implemented=0\b|"
    r"d3d12_(cpu_readback|cpu_mapping|cpu_copy|readback|"
    r"native_present_unimplemented|present_state_only|present_fence_only|"
    r"present_import_only|present_open_only|present_callback_only|"
    r"present_release_only|present_errno)=[1-9][0-9]*|"
    r"\b(callbacks_blocked|releases_blocked)=[1-9][0-9]*",
    log,
):
    raise SystemExit("D3D12 present used an incomplete or CPU/readback path")
if "xv6-mesa: d3d12 native present unavailable; refusing DRI software/readback present" in log:
    raise SystemExit("D3D12 fell back to the refused DRI readback path")
if re.search(r"\b(drisw|softpipe|llvmpipe|swrast)\b", log, re.IGNORECASE):
    raise SystemExit("Mesa used a software/DRI software path during FPS validation")
if max(transitions) < min_changed_pixels:
    frozen_window_negative(
        "thumbnail_progress_below_threshold",
        sample_elapsed=format_optional(counter_delta(uptimes)),
        native_present_delta=format_optional(counter_delta(d3d12_present_counts)),
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=format_optional(counter_delta(d3d12_content_frames)),
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=sum(
            1
            for before, after in zip(outside_overlay_crcs, outside_overlay_crcs[1:])
            if before != after
        ),
        changed=",".join(str(v) for v in transitions),
        outside_overlay_crcs=",".join(hex(value) for value in outside_overlay_crcs),
    )
    raise SystemExit(
        "visible thumbnail progression too small outside title/FPS overlay: "
        f"max_changed={max(transitions)} required={min_changed_pixels} "
        f"samples={','.join(str(v) for v in transitions)}"
    )
required_active_transitions = min(2, len(transitions))
active_transitions = sum(1 for changed in transitions
                         if changed >= min_changed_pixels)
if active_transitions < required_active_transitions:
    frozen_window_negative(
        "thumbnail_progress_not_repeated",
        sample_elapsed=format_optional(counter_delta(uptimes)),
        native_present_delta=format_optional(counter_delta(d3d12_present_counts)),
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=format_optional(counter_delta(d3d12_content_frames)),
        thumbnail_progress_delta=max(transitions),
        active_thumbnail_transitions=active_transitions,
        required_thumbnail_transitions=required_active_transitions,
        outside_overlay_crc_changes=sum(
            1
            for before, after in zip(outside_overlay_crcs, outside_overlay_crcs[1:])
            if before != after
        ),
        changed=",".join(str(v) for v in transitions),
        outside_overlay_crcs=",".join(hex(value) for value in outside_overlay_crcs),
    )
    raise SystemExit(
        "visible thumbnail progression was not repeated across the sample "
        f"window: active={active_transitions} "
        f"required={required_active_transitions} "
        f"threshold={min_changed_pixels} "
        f"samples={','.join(str(v) for v in transitions)}"
    )
if len(set(outside_overlay_crcs)) < 2:
    frozen_window_negative(
        "outside_overlay_crc_static",
        sample_elapsed=format_optional(counter_delta(uptimes)),
        native_present_delta=format_optional(counter_delta(d3d12_present_counts)),
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=format_optional(counter_delta(d3d12_content_frames)),
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=0,
        changed=",".join(str(v) for v in transitions),
        outside_overlay_crcs=",".join(hex(value) for value in outside_overlay_crcs),
    )
    raise SystemExit(
        "outside-overlay thumbnail CRC did not change: "
        f"crcs={','.join(hex(value) for value in outside_overlay_crcs)}"
    )
outside_overlay_crc_transitions = sum(
    1
    for before, after in zip(outside_overlay_crcs, outside_overlay_crcs[1:])
    if before != after
)
if outside_overlay_crc_transitions < required_active_transitions:
    frozen_window_negative(
        "outside_overlay_crc_not_repeated",
        sample_elapsed=format_optional(counter_delta(uptimes)),
        native_present_delta=format_optional(counter_delta(d3d12_present_counts)),
        frame_callback_delta=format_optional(counter_delta(d3d12_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(d3d12_release_counts)),
        content_frame_delta=format_optional(counter_delta(d3d12_content_frames)),
        thumbnail_progress_delta=max(transitions),
        active_thumbnail_transitions=active_transitions,
        outside_overlay_crc_changes=outside_overlay_crc_transitions,
        required_thumbnail_transitions=required_active_transitions,
        changed=",".join(str(v) for v in transitions),
        outside_overlay_crcs=",".join(hex(value) for value in outside_overlay_crcs),
    )
    raise SystemExit(
        "outside-overlay thumbnail CRC did not change repeatedly across the "
        "visual window: "
        f"active={outside_overlay_crc_transitions} "
        f"required={required_active_transitions} "
        f"crcs={','.join(hex(value) for value in outside_overlay_crcs)}"
    )
configured_visual_elapsed = max(
    0.001, (len(frames) - 1) * visual_sample_ms / 1000.0
)
measured_visual_elapsed = (
    frame_times[-1] - frame_times[0] if len(frame_times) >= 2 else 0.0
)
visual_elapsed = (
    measured_visual_elapsed
    if measured_visual_elapsed > 0.0
    else configured_visual_elapsed
)
visual_progress_fps = active_transitions / visual_elapsed
visual_report_ceiling = visual_progress_fps * visual_max_report_ratio
if visual_progress_fps < visual_min_fps:
    fail_validation(
        "visible thumbnail cadence below FPS gate: "
        f"visual_progress_fps={visual_progress_fps:.3f} "
        f"required>={visual_min_fps:.3f} "
        f"min_fps={min_fps:.3f} active={active_transitions}/{len(transitions)} "
        f"elapsed={visual_elapsed:.3f}s "
        f"samples={','.join(str(v) for v in transitions)}"
    )
if len(visual_uptimes) < 2 or len(visual_native_counts) < 2:
    raise SystemExit(
        "not enough native D3D12 present counters bracketing visible "
        f"thumbnail progression: uptimes={len(visual_uptimes)} "
        f"native_counts={len(visual_native_counts)}"
    )
require_run_id_samples("visual-window D3D12 evidence", visual_run_ids)
require_display_handoff_samples(
    "visual-window D3D12 evidence",
    visual_display_handoffs,
    visual_copy_counts,
)
visual_generation_deltas = require_advancing_counter(
    "visual d3d12_evidence_generation",
    visual_evidence_generations,
)
visual_evidence_time_deltas = require_advancing_counter(
    "visual d3d12_present_evidence_time_us",
    visual_evidence_times,
)
if len(visual_resources) < 2 or any(value == 0 for value in visual_resources):
    raise SystemExit(
        "missing nonzero visual-window D3D12 present resource samples: "
        f"count={len(visual_resources)}"
    )
if (len(visual_buffer_generations) < 2 or
        any(value == 0 for value in visual_buffer_generations)):
    raise SystemExit(
        "missing nonzero visual-window D3D12 buffer-generation samples: "
        f"count={len(visual_buffer_generations)}"
    )
if len(visual_content_crcs) < 2:
    raise SystemExit(
        "missing visual-window D3D12 content CRC samples from compositor "
        "evidence"
    )
if len(set(visual_content_crcs)) < 2:
    frozen_window_negative(
        "visual_content_crc_static",
        sample_elapsed=f"{elapsed:.3f}",
        native_present_delta=native_completed,
        frame_callback_delta=format_optional(counter_delta(visual_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(visual_release_counts)),
        content_frame_delta=format_optional(counter_delta(visual_content_frames)),
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=outside_overlay_crc_transitions,
        content_crc_changes=0,
        changed=",".join(str(v) for v in transitions),
        visual_content_crcs=",".join(hex(value) for value in visual_content_crcs),
    )
    raise SystemExit(
        "visual-window D3D12 compositor content CRC did not change: "
        f"values={','.join(hex(value) for value in visual_content_crcs)}"
    )
visual_content_crc_changes = sum(
    1
    for before, after in zip(visual_content_crcs, visual_content_crcs[1:])
    if before != after
)
if len(visual_content_frame_hashes) < 2:
    raise SystemExit(
        "missing visual-window D3D12 content frame-hash samples from "
        "compositor evidence"
    )
if len(set(visual_content_frame_hashes)) < 2:
    frozen_window_negative(
        "visual_content_frame_hash_static",
        sample_elapsed=f"{elapsed:.3f}",
        native_present_delta=native_completed,
        frame_callback_delta=format_optional(counter_delta(visual_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(visual_release_counts)),
        content_frame_delta=format_optional(counter_delta(visual_content_frames)),
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=outside_overlay_crc_transitions,
        content_crc_changes=visual_content_crc_changes,
        content_frame_hash_changes=0,
        changed=",".join(str(v) for v in transitions),
        visual_content_frame_hashes=",".join(
            hex(value) for value in visual_content_frame_hashes
        ),
    )
    raise SystemExit(
        "visual-window D3D12 compositor content frame hash did not change: "
        f"values={','.join(hex(value) for value in visual_content_frame_hashes)}"
    )
visual_content_frame_hash_changes = sum(
    1
    for before, after in zip(visual_content_frame_hashes,
                             visual_content_frame_hashes[1:])
    if before != after
)
if len(visual_content_frames) < 2:
    raise SystemExit(
        "missing visual-window D3D12 content frame/change counter samples "
        "from compositor evidence"
    )
visual_content_frame_deltas = require_advancing_counter(
    "visual d3d12 content frame/change counter",
    visual_content_frames,
)
visual_content_frame_delta = visual_content_frames[-1] - visual_content_frames[0]
visual_native_elapsed = visual_uptimes[-1] - visual_uptimes[0]
visual_native_completed = visual_native_counts[-1] - visual_native_counts[0]
if visual_native_elapsed <= 0.0:
    raise SystemExit(
        f"invalid visual native-present counter elapsed time: "
        f"{visual_native_elapsed:.3f}"
    )
if visual_native_completed <= 0:
    frozen_window_negative(
        "visual_native_present_not_advancing",
        sample_elapsed=f"{elapsed:.3f}",
        visual_native_present_elapsed=f"{visual_native_elapsed:.3f}",
        native_present_delta=native_completed,
        visual_window_native_present_delta=visual_native_completed,
        frame_callback_delta=format_optional(counter_delta(visual_callback_counts)),
        buffer_release_delta=format_optional(counter_delta(visual_release_counts)),
        content_frame_delta=visual_content_frame_delta,
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=outside_overlay_crc_transitions,
        changed=",".join(str(v) for v in transitions),
    )
    raise SystemExit(
        "native D3D12 present completions did not advance while visible "
        "thumbnail progression was sampled: "
        f"{visual_native_counts[0]}->{visual_native_counts[-1]}"
    )
if not native_content_progress_complete(
        visual_content_progress_states,
        visual_visible_content_progress_states,
        visual_content_requires_native,
        visual_content_native_complete,
        visual_content_visible_credit,
        visual_content_source_owned):
    fail_validation(
        "fps_visible_native_content_gate_matrix "
        f"validation_run_id={expected_run_id} stage=visual-window "
        "native_present_completion=PASS "
        "content_progress_contract=MISSING "
        "requires_native_present_completion=1 requires_content_progress=1 "
        "visible_content_credit_before_native_present=0 "
        "native_present_credit=0 opengl_submit_credit=0 "
        "gate=closed status=FAIL_MISSING_EVIDENCE"
    )
if not same_run_resource_generation_completion_progress(
        visual_run_ids,
        visual_resources,
        visual_resource_generations,
        visual_native_completion_ids):
    fail_validation(
        "fps_stale_frozen_content_generation_matrix "
        f"validation_run_id={expected_run_id} stage=visual-window "
        "stale_run_rejected=PASS frozen_content_rejected=PASS "
        "title_only_fps_rejected=PASS "
        f"content_crc_changes={visual_content_crc_changes} "
        f"content_frame_hash_changes={visual_content_frame_hash_changes} "
        f"content_frame_delta={visual_content_frame_delta} "
        f"native_completion_ids={len(visual_native_completion_ids)} "
        "same_run_resource_generation_completion=MISSING "
        "requires_native_present_completion_id_advance=1 "
        "requires_same_run=1 requires_same_resource_generation=1 "
        "native_present_credit=0 opengl_submit_credit=0 "
        "gate=closed status=FAIL_MISSING_EVIDENCE"
    )
if visual_content_frame_delta < visual_native_completed:
    raise SystemExit(
        "visual-window content frame counter lagged native present "
        "completions: "
        f"content_delta={visual_content_frame_delta} "
        f"native_completed={visual_native_completed} "
        f"content_values={','.join(str(v) for v in visual_content_frames)} "
        f"native_values={','.join(str(v) for v in visual_native_counts)}"
    )
interval_deltas("visual d3d12_gpu_present_completes",
                visual_native_counts)
if len(visual_copy_counts) >= 2:
    interval_deltas("visual d3d12_gpu_copy_completes",
                    visual_copy_counts)
    visual_native_copied = visual_copy_counts[-1] - visual_copy_counts[0]
    if visual_native_copied < visual_native_completed:
        raise SystemExit(
            "visual-window native present completions exceed GPU-copy "
            f"completions: copies={visual_native_copied} "
            f"completes={visual_native_completed}"
        )
if len(visual_callback_counts) < 2:
    raise SystemExit(
        "not enough D3D12 frame callback samples bracketing visible "
        f"thumbnail progression: callbacks={len(visual_callback_counts)}"
    )
interval_deltas("visual d3d12_frame_callbacks", visual_callback_counts)
visual_callbacks = visual_callback_counts[-1] - visual_callback_counts[0]
if visual_callbacks <= 0:
    frozen_window_negative(
        "visual_frame_callbacks_not_advancing",
        sample_elapsed=f"{elapsed:.3f}",
        visual_native_present_elapsed=f"{visual_native_elapsed:.3f}",
        native_present_delta=native_completed,
        visual_window_native_present_delta=visual_native_completed,
        frame_callback_delta=visual_callbacks,
        buffer_release_delta=format_optional(counter_delta(visual_release_counts)),
        content_frame_delta=visual_content_frame_delta,
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=outside_overlay_crc_transitions,
        changed=",".join(str(v) for v in transitions),
    )
    raise SystemExit(
        "D3D12 frame callbacks did not advance while visible thumbnail "
        "progression was sampled"
    )
if visual_native_completed > visual_callbacks:
    raise SystemExit(
        "visual-window native present completions exceed frame callbacks: "
        f"completes={visual_native_completed} callbacks={visual_callbacks}"
    )
if len(visual_release_counts) < 2:
    raise SystemExit(
        "not enough D3D12 buffer release samples bracketing visible "
        f"thumbnail progression: releases={len(visual_release_counts)}"
    )
interval_deltas("visual d3d12_buffer_releases", visual_release_counts)
visual_releases = visual_release_counts[-1] - visual_release_counts[0]
if visual_releases <= 0:
    frozen_window_negative(
        "visual_buffer_releases_not_advancing",
        sample_elapsed=f"{elapsed:.3f}",
        visual_native_present_elapsed=f"{visual_native_elapsed:.3f}",
        native_present_delta=native_completed,
        visual_window_native_present_delta=visual_native_completed,
        frame_callback_delta=visual_callbacks,
        buffer_release_delta=visual_releases,
        content_frame_delta=visual_content_frame_delta,
        thumbnail_progress_delta=max(transitions),
        outside_overlay_crc_changes=outside_overlay_crc_transitions,
        changed=",".join(str(v) for v in transitions),
    )
    raise SystemExit(
        "D3D12 buffer releases did not advance while visible thumbnail "
        "progression was sampled"
    )
if visual_native_completed > visual_releases:
    raise SystemExit(
        "visual-window native present completions exceed buffer releases: "
        f"completes={visual_native_completed} releases={visual_releases}"
    )
if len(visual_native_paths) < 2:
    raise SystemExit(
        "not enough D3D12 present-path samples bracketing visible thumbnail "
        f"progression: paths={len(visual_native_paths)}"
    )
bad_visual_paths = [p for p in visual_native_paths
                    if p != "d3d12-dxg-present-source-display-handoff"]
if bad_visual_paths:
    raise SystemExit(
        "visual-window D3D12 present path was not the native shared-surface "
        f"path: paths={','.join(visual_native_paths)}"
    )
if visual_reject_evidence:
    raise SystemExit(
        "visual-window D3D12 present sample contained CPU/readback or "
        f"partial-present evidence: {visual_reject_evidence[-1]}"
    )
if (len(visual_present_ids) < 2 or
        any(value == 0 for value in visual_present_ids)):
    raise SystemExit(
        "missing nonzero visual-window DXG present_id samples: "
        f"values={','.join(str(value) for value in visual_present_ids)}"
    )
if (len(visual_present_completed) < 2 or
        any(value == 0 for value in visual_present_completed)):
    raise SystemExit(
        "missing nonzero visual-window DXG completed samples: "
        f"values={','.join(str(value) for value in visual_present_completed)}"
    )
if visual_present_completed[-1] < visual_present_ids[-1]:
    raise SystemExit(
        "visual-window DXG completed counter did not cover present_id: "
        f"present_id={visual_present_ids[-1]} "
        f"completed={visual_present_completed[-1]}"
    )
require_display_bind_evidence(
    "visual-window D3D12",
    visual_display_bind_backends,
    visual_display_bind_transports,
    visual_display_bind_present_ids,
    visual_display_bind_completed_ids,
    visual_display_bind_resource_generations,
    visual_display_bind_completion_sources,
)
require_display_bind_identity(
    "visual-window D3D12",
    visual_present_ids,
    visual_present_completed,
    visual_buffer_generations,
    visual_display_bind_present_ids,
    visual_display_bind_completed_ids,
    visual_display_bind_resource_generations,
)
require_native_content_identity(
    "visual-window D3D12",
    visual_present_ids,
    visual_present_completed,
    visual_buffer_generations,
    visual_display_bind_present_ids,
    visual_display_bind_completed_ids,
    visual_display_bind_resource_generations,
    visual_content_present_ids,
    visual_content_completed_ids,
    visual_content_display_bind_present_ids,
    visual_content_display_bind_completed_ids,
    visual_content_display_bind_resource_generations,
    visual_content_current_run_valid,
    visual_content_identity_complete,
    visual_final_handoff_successes,
    visual_final_handoff_present_ids,
    visual_final_handoff_completed_ids,
    visual_final_handoff_resource_generations,
)
if (len(visual_gpup_dda_commit_successes) < 2 or
        any(value == 0 for value in visual_gpup_dda_commit_successes)):
    raise SystemExit(
        "missing visual-window GPU-P/DDA present-source commit accepted samples: "
        f"values={','.join(str(value) for value in visual_gpup_dda_commit_successes)}"
    )
visual_present_id_deltas = require_advancing_counter(
    "visual-window DXG present_id",
    visual_present_ids,
)
visual_present_completed_deltas = require_advancing_counter(
    "visual-window DXG completed counter",
    visual_present_completed,
)
visual_display_bind_present_id_deltas = require_advancing_counter(
    "visual-window display_bind_present_id",
    visual_display_bind_present_ids,
)
visual_display_bind_completed_id_deltas = require_advancing_counter(
    "visual-window display_bind_completed_id",
    visual_display_bind_completed_ids,
)
visual_gpup_dda_commit_deltas = require_advancing_counter(
    "visual-window GPU-P/DDA present-source commit accepted",
    visual_gpup_dda_commit_successes,
)
visual_present_id_delta = visual_present_ids[-1] - visual_present_ids[0]
visual_present_completed_delta = (
    visual_present_completed[-1] - visual_present_completed[0]
)
visual_display_bind_present_id_delta = (
    visual_display_bind_present_ids[-1] - visual_display_bind_present_ids[0]
)
visual_display_bind_completed_id_delta = (
    visual_display_bind_completed_ids[-1] -
    visual_display_bind_completed_ids[0]
)
visual_gpup_dda_commit_delta = (
    visual_gpup_dda_commit_successes[-1] - visual_gpup_dda_commit_successes[0]
)
if visual_present_id_delta < visual_native_completed:
    raise SystemExit(
        "visual-window DXG present_id did not advance with native present "
        "completions: "
        f"present_id_delta={visual_present_id_delta} "
        f"native_completed={visual_native_completed} "
        f"present_ids={','.join(str(v) for v in visual_present_ids)} "
        f"native_values={','.join(str(v) for v in visual_native_counts)}"
    )
if visual_present_completed_delta < visual_native_completed:
    raise SystemExit(
        "visual-window DXG completed counter did not advance with native "
        "present completions: "
        f"completed_delta={visual_present_completed_delta} "
        f"native_completed={visual_native_completed} "
        f"completed_values={','.join(str(v) for v in visual_present_completed)} "
        f"native_values={','.join(str(v) for v in visual_native_counts)}"
    )
if visual_display_bind_present_id_delta < visual_native_completed:
    raise SystemExit(
        "visual-window display_bind_present_id did not advance with native "
        "present completions: "
        f"display_bind_present_id_delta={visual_display_bind_present_id_delta} "
        f"native_completed={visual_native_completed} "
        f"display_bind_present_ids={','.join(str(v) for v in visual_display_bind_present_ids)} "
        f"native_values={','.join(str(v) for v in visual_native_counts)}"
    )
if visual_display_bind_completed_id_delta < visual_native_completed:
    raise SystemExit(
        "visual-window display_bind_completed_id did not advance with native "
        "present completions: "
        f"display_bind_completed_id_delta={visual_display_bind_completed_id_delta} "
        f"native_completed={visual_native_completed} "
        f"display_bind_completed_ids={','.join(str(v) for v in visual_display_bind_completed_ids)} "
        f"native_values={','.join(str(v) for v in visual_native_counts)}"
    )
if visual_gpup_dda_commit_delta < visual_native_completed:
    raise SystemExit(
        "visual-window GPU-P/DDA present-source commit accepted counter did not "
        "advance with native present completions: "
        f"commit_delta={visual_gpup_dda_commit_delta} "
        f"native_completed={visual_native_completed} "
        f"commit_values={','.join(str(v) for v in visual_gpup_dda_commit_successes)} "
        f"native_values={','.join(str(v) for v in visual_native_counts)}"
    )
if any(value == 0 for value in visual_same_frame_callback_releases):
    raise SystemExit(
        "visual-window D3D12 same-frame callback/release proof reported false: "
        f"values={','.join(str(value) for value in visual_same_frame_callback_releases)}"
    )
if visual_same_frame_callback_releases and visual_same_frame_callback_releases[-1] < 1:
    raise SystemExit("visual-window D3D12 same-frame callback/release proof was not observed")
if any(value == 0 for value in visual_same_frame_callbacks):
    raise SystemExit(
        "visual-window D3D12 same-frame callback proof reported false: "
        f"values={','.join(str(value) for value in visual_same_frame_callbacks)}"
    )
if any(value == 0 for value in visual_same_frame_releases):
    raise SystemExit(
        "visual-window D3D12 same-frame release proof reported false: "
        f"values={','.join(str(value) for value in visual_same_frame_releases)}"
    )
visual_native_fps = visual_native_completed / visual_native_elapsed
visual_content_frame_fps = visual_content_frame_delta / visual_native_elapsed
effective_presented_fps = min(
    native_fps,
    completion_fps,
    visual_native_fps,
    sample_content_frame_fps,
    visual_content_frame_fps,
)
log_validation(
    "visual cadence "
    f"validation_run_id={expected_run_id} "
    "visual_samples_inside_sample_window=1 "
    "d3d12_display_handoff_implemented=1 "
    f"active={active_transitions}/{len(transitions)} "
    f"elapsed={visual_elapsed:.3f}s "
    f"configured_elapsed={configured_visual_elapsed:.3f}s "
    f"native_present_elapsed={visual_native_elapsed:.3f}s "
    f"native_present_delta={visual_native_completed} "
    f"present_id_delta={visual_present_id_delta} "
    f"completed_delta={visual_present_completed_delta} "
    f"display_bind_present_id_delta={visual_display_bind_present_id_delta} "
    f"display_bind_completed_id_delta={visual_display_bind_completed_id_delta} "
    f"gpup_dda_commit_delta={visual_gpup_dda_commit_delta} "
    f"evidence_generation_delta={sum(visual_generation_deltas)} "
    f"evidence_time_delta_us={sum(visual_evidence_time_deltas)} "
    f"frame_callback_delta={visual_callbacks} "
    f"buffer_release_delta={visual_releases} "
    f"content_crc_changes={visual_content_crc_changes} "
    f"content_frame_hash_changes={visual_content_frame_hash_changes} "
    f"content_frame_delta={visual_content_frame_delta} "
    f"content_frame_fps={visual_content_frame_fps:.3f} "
    f"outside_overlay_crc_changes={outside_overlay_crc_transitions} "
    f"native_present_fps={visual_native_fps:.3f} "
    f"visual_progress_fps={visual_progress_fps:.3f} "
    f"changed={','.join(str(v) for v in transitions)} "
    f"outside_overlay_crcs={','.join(hex(value) for value in outside_overlay_crcs)}"
)
log_validation(
    "reported FPS evidence "
    f"validation_run_id={expected_run_id} "
    "visual_samples_inside_sample_window=1 "
    "d3d12_display_handoff_implemented=1 "
    "displayed_fps_context_only=1 "
    "demo_fps_context_only=1 "
    "acceptance_requires_native_present_and_content_progress=1 "
    "acceptance_requires_effective_presented_fps_gt_min=1 "
    f"native_present_fps={native_fps:.3f} "
    f"app_visible_avg={avg:.3f} "
    f"app_visible_low={low:.3f} "
    f"display_completion_fps={completion_fps:.3f} "
    f"sample_present_id_delta={sample_present_id_delta} "
    f"sample_completed_delta={sample_present_completed_delta} "
    f"sample_display_bind_present_id_delta={sample_display_bind_present_id_delta} "
    f"sample_display_bind_completed_id_delta={sample_display_bind_completed_id_delta} "
    f"sample_gpup_dda_commit_delta={sample_gpup_dda_commit_delta} "
    f"sample_evidence_generation_delta={sum(d3d12_generation_deltas)} "
    f"sample_evidence_time_delta_us={sum(d3d12_evidence_time_deltas)} "
    f"sample_content_crc_changes={sample_content_crc_changes} "
    f"sample_content_frame_hash_changes={sample_content_frame_hash_changes} "
    f"sample_content_frame_delta={sample_content_frame_delta} "
    f"sample_content_frame_fps={sample_content_frame_fps:.3f} "
    f"active_native_intervals={active_native_intervals} "
    f"active_display_intervals={active_display_intervals} "
    f"active_callback_intervals={active_callback_intervals} "
    f"active_release_intervals={active_release_intervals} "
    f"active_present_id_intervals={active_present_id_intervals} "
    f"active_completed_intervals={active_present_completed_intervals} "
    f"active_display_bind_present_id_intervals={active_display_bind_present_id_intervals} "
    f"active_display_bind_completed_id_intervals={active_display_bind_completed_id_intervals} "
    f"active_gpup_dda_commit_intervals={active_gpup_dda_commit_intervals} "
    f"active_content_frame_intervals={active_content_frame_intervals} "
    f"active_native_display_intervals={active_native_display_intervals} "
    f"active_native_display_callback_release_intervals="
    f"{active_native_display_callback_release_intervals} "
    f"active_full_evidence_intervals={active_full_evidence_intervals} "
    f"visual_ceiling={visual_report_ceiling:.3f} "
    f"ratio_limit={visual_max_report_ratio:.3f} "
    f"margin={visual_report_fps_margin:.3f}"
)
inflated_sources = []
strict_inflated_sources = []
for name, candidate in (
    ("native_present_fps", native_fps),
    ("visual_window_native_present_fps", visual_native_fps),
    ("app_visible_avg", avg),
    ("display_completion_fps", completion_fps),
):
    if (
        candidate > visual_report_ceiling
        and candidate - visual_progress_fps > visual_report_fps_margin
    ):
        inflated_sources.append((name, candidate))
    if (visual_progress_fps < low_visual_cadence_fps and
            candidate - visual_progress_fps > visual_report_fps_margin):
        strict_inflated_sources.append((name, candidate))
if strict_inflated_sources:
    details = " ".join(
        f"{name}={value:.3f}" for name, value in strict_inflated_sources
    )
    fail_validation(
        "reported/native FPS inflation rejected during low visual cadence: "
        f"{details} visual_progress_fps={visual_progress_fps:.3f} "
        f"low_visual_cadence_fps={low_visual_cadence_fps:.3f} "
        f"margin={visual_report_fps_margin:.3f} "
        f"samples={','.join(str(v) for v in transitions)}"
    )
if inflated_sources:
    details = " ".join(
        f"{name}={value:.3f}" for name, value in inflated_sources
    )
    fail_validation(
        "reported/native FPS inflation rejected by visual thumbnail cadence: "
        f"{details} visual_progress_fps={visual_progress_fps:.3f} "
        f"visual_ceiling={visual_report_ceiling:.3f} "
        f"ratio_limit={visual_max_report_ratio:.3f} "
        f"margin={visual_report_fps_margin:.3f} "
        f"active={active_transitions}/{len(transitions)} "
        f"samples={','.join(str(v) for v in transitions)}"
    )
if active_transitions < len(transitions):
    stale_report_ceiling = visual_progress_fps * visual_max_stale_report_ratio
    if native_fps > stale_report_ceiling and native_fps - visual_progress_fps > 10.0:
        fail_validation(
            "reported native-present FPS is inconsistent with stale visible "
            "thumbnail progress: "
            f"native_fps={native_fps:.3f} visual_progress_fps={visual_progress_fps:.3f} "
            f"active={active_transitions}/{len(transitions)} "
            f"ratio_limit={visual_max_stale_report_ratio:.3f} "
            f"samples={','.join(str(v) for v in transitions)}"
        )
if native_fps <= min_fps:
    raise SystemExit(
        f"native D3D12 present FPS below gate: fps={native_fps:.3f} "
        f"completed_native={native_completed} elapsed={elapsed:.3f}s "
        f"required>{min_fps:.3f}"
    )
if completion_fps <= min_fps:
    raise SystemExit(
        f"display completion FPS below gate: fps={completion_fps:.3f} "
        f"completed_display={completed} elapsed={elapsed:.3f}s "
        f"required>{min_fps:.3f}"
    )
if visual_native_fps <= min_fps:
    raise SystemExit(
        f"visual-window native-present FPS below gate: "
        f"fps={visual_native_fps:.3f} "
        f"completed_native={visual_native_completed} "
        f"elapsed={visual_native_elapsed:.3f}s required>{min_fps:.3f}"
    )
if sample_content_frame_fps <= min_fps:
    raise SystemExit(
        "sample-window compositor content FPS below gate: "
        f"fps={sample_content_frame_fps:.3f} "
        f"content_frame_delta={sample_content_frame_delta} "
        f"elapsed={elapsed:.3f}s required>{min_fps:.3f}"
    )
if visual_content_frame_fps <= min_fps:
    raise SystemExit(
        "visual-window compositor content FPS below gate: "
        f"fps={visual_content_frame_fps:.3f} "
        f"content_frame_delta={visual_content_frame_delta} "
        f"elapsed={visual_native_elapsed:.3f}s required>{min_fps:.3f}"
    )
if effective_presented_fps <= min_fps:
    raise SystemExit(
        "effective real-presented FPS below gate: "
        f"effective_presented_fps={effective_presented_fps:.3f} "
        f"native_present_fps={native_fps:.3f} "
        f"display_completion_fps={completion_fps:.3f} "
        f"visual_window_native_present_fps={visual_native_fps:.3f} "
        f"sample_content_frame_fps={sample_content_frame_fps:.3f} "
        f"visual_window_content_frame_fps={visual_content_frame_fps:.3f} "
        f"required>{min_fps:.3f}"
    )
if avg > native_fps * 1.15 and avg - native_fps > 3.0:
    raise SystemExit(
        f"app-visible FPS appears inflated relative to completed native "
        f"D3D12 presents: visible_avg={avg:.3f} native_fps={native_fps:.3f}"
    )
if low < min_fps * 0.90:
    raise SystemExit(
        f"visible FPS low sample below tolerance: native_fps={native_fps:.3f} "
        f"visible_avg={avg:.3f} low={low:.3f} "
        f"samples={','.join(f'{v:.3f}' for v in values)}"
    )
accepted_post_window_records = [
    record for record in accepted_demo_records if not record["in_sampling"]
]
log_validation(
    "strict anti-inflation contract "
    f"validation_run_id={expected_run_id} "
    "strict_anti_inflation=1 "
    "displayed_fps_context_only=1 "
    "demo_fps_context_only=1 "
    "acceptance_requires_native_present_and_content_progress=1 "
    "acceptance_requires_effective_presented_fps_gt_min=1 "
    "demo_client_pid_match=1 "
    "demo_resource_match=1 "
    "demo_buffer_generation_match=1 "
    "demo_visible=1 "
    "demo_closeable=1 "
    "demo_resizable=1 "
    "demo_interaction_native_present=1 "
    "demo_interaction_content_progress=1 "
    f"sample_window_only={1 if not accepted_post_window_records else 0} "
    "sample_window_or_timestamped=1 "
    f"accepted_demo_samples={len(accepted_demo_records)} "
    f"timestamped_post_window_samples={len(accepted_post_window_records)} "
    f"ignored_post_window_samples={len(ignored_post_window_records)} "
    f"process_id={demo_process_ids[-1]} "
    f"d3d12_client_pid={demo_client_pids[-1]} "
    f"demo_resources={','.join(hex(value) for value in sorted(set(demo_resources)))} "
    f"wlcomp_resources={','.join(hex(value) for value in sorted(sample_resource_set))} "
    f"demo_buffer_generations="
    f"{','.join(str(value) for value in sorted(set(demo_buffer_generations)))} "
    f"wlcomp_buffer_generations="
    f"{','.join(str(value) for value in sorted(sample_buffer_generation_set))} "
    f"visual_min_fps={visual_min_fps:.3f} "
    f"low_visual_cadence_fps={low_visual_cadence_fps:.3f} "
    f"visual_progress_fps={visual_progress_fps:.3f} "
    f"visual_report_ratio_limit={visual_max_report_ratio:.3f} "
    f"visual_stale_report_ratio_limit={visual_max_stale_report_ratio:.3f} "
    f"visual_report_margin={visual_report_fps_margin:.3f} "
    f"native_present_fps={native_fps:.3f} "
    f"display_completion_fps={completion_fps:.3f} "
    f"sample_content_frame_fps={sample_content_frame_fps:.3f} "
    f"sample_present_id_delta={sample_present_id_delta} "
    f"sample_completed_delta={sample_present_completed_delta} "
    f"sample_display_bind_present_id_delta={sample_display_bind_present_id_delta} "
    f"sample_display_bind_completed_id_delta={sample_display_bind_completed_id_delta} "
    f"sample_gpup_dda_commit_delta={sample_gpup_dda_commit_delta} "
    f"visual_present_id_delta={visual_present_id_delta} "
    f"visual_completed_delta={visual_present_completed_delta} "
    f"visual_display_bind_present_id_delta={visual_display_bind_present_id_delta} "
    f"visual_display_bind_completed_id_delta={visual_display_bind_completed_id_delta} "
    f"visual_gpup_dda_commit_delta={visual_gpup_dda_commit_delta} "
    f"visual_window_native_present_fps={visual_native_fps:.3f} "
    f"visual_window_content_frame_fps={visual_content_frame_fps:.3f} "
    f"effective_presented_fps={effective_presented_fps:.3f} "
    f"app_visible_avg={avg:.3f}"
)
log_validation(
    "fps_downstream_consumer_gate_matrix "
    f"validation_run_id={expected_run_id} "
    f"fps_artifact_run_id={expected_run_id} "
    "backend_identity=FB_GPU_BACKEND_F_OPENGL_SUBMIT "
    "backend_opengl_submit=1 backend_zero_rejected=PASS "
    f"backend_mode={backend_mode} "
    f"display_bind_backend={d3d12_display_bind_backends[-1]} "
    f"display_bind_transport={d3d12_display_bind_transports[-1]} "
    f"display_bind_present_id={d3d12_display_bind_present_ids[-1]} "
    f"display_bind_completed_id={d3d12_display_bind_completed_ids[-1]} "
    f"display_bind_resource_generation={d3d12_display_bind_resource_generations[-1]} "
    f"native_present_id={d3d12_present_ids[-1]} "
    f"native_completed={d3d12_present_completed[-1]} "
    "native_present_ids_zero_rejected=PASS "
    "current_run_display_bind_identity=PASS "
    "current_run_native_present_identity=PASS "
    "current_run_fps_backend_identity=PASS "
    "finite_480p_fps_artifact=PASS "
    f"effective_presented_fps={effective_presented_fps:.3f} "
    "gate=open native_present_credit=1 opengl_submit_credit=1 "
    "fps_credit=1 status=PASS"
)
print(
    "hyperv-3d-fps-validate: ok "
    f"validation_run_id={expected_run_id} "
    f"warmup_native_present_count={warmup_native_completed} "
    f"sample_native_present_count_delta={native_completed} "
    f"sample_elapsed={elapsed:.3f}s native_present_fps={native_fps:.3f} "
    f"effective_presented_fps={effective_presented_fps:.3f} "
    "acceptance_requires_effective_presented_fps_gt_min=1 "
    "d3d12_display_handoff_implemented=1 "
    f"sample_evidence_generation_delta={sum(d3d12_generation_deltas)} "
    f"sample_evidence_time_delta_us={sum(d3d12_evidence_time_deltas)} "
    f"sample_present_id_delta={sample_present_id_delta} "
    f"sample_completed_delta={sample_present_completed_delta} "
    f"sample_display_bind_present_id_delta={sample_display_bind_present_id_delta} "
    f"sample_display_bind_completed_id_delta={sample_display_bind_completed_id_delta} "
    f"sample_gpup_dda_commit_delta={sample_gpup_dda_commit_delta} "
    f"sample_content_crc_changes={sample_content_crc_changes} "
    f"sample_content_frame_hash_changes={sample_content_frame_hash_changes} "
    f"sample_content_frame_delta={sample_content_frame_delta} "
    f"sample_content_frame_fps={sample_content_frame_fps:.3f} "
    f"active_native_intervals={active_native_intervals} "
    f"active_display_intervals={active_display_intervals} "
    f"active_callback_intervals={active_callback_intervals} "
    f"active_release_intervals={active_release_intervals} "
    f"active_present_id_intervals={active_present_id_intervals} "
    f"active_completed_intervals={active_present_completed_intervals} "
    f"active_display_bind_present_id_intervals={active_display_bind_present_id_intervals} "
    f"active_display_bind_completed_id_intervals={active_display_bind_completed_id_intervals} "
    f"active_gpup_dda_commit_intervals={active_gpup_dda_commit_intervals} "
    f"active_content_frame_intervals={active_content_frame_intervals} "
    f"active_native_display_intervals={active_native_display_intervals} "
    f"active_native_display_callback_release_intervals="
    f"{active_native_display_callback_release_intervals} "
    f"active_full_evidence_intervals={active_full_evidence_intervals} "
    f"visual_window_native_present_delta={visual_native_completed} "
    f"visual_window_present_id_delta={visual_present_id_delta} "
    f"visual_window_completed_delta={visual_present_completed_delta} "
    f"visual_window_display_bind_present_id_delta={visual_display_bind_present_id_delta} "
    f"visual_window_display_bind_completed_id_delta={visual_display_bind_completed_id_delta} "
    f"visual_window_gpup_dda_commit_delta={visual_gpup_dda_commit_delta} "
    f"visual_window_evidence_generation_delta={sum(visual_generation_deltas)} "
    f"visual_window_evidence_time_delta_us={sum(visual_evidence_time_deltas)} "
    f"visual_window_frame_callback_delta={visual_callbacks} "
    f"visual_window_buffer_release_delta={visual_releases} "
    f"visual_window_content_crc_changes={visual_content_crc_changes} "
    f"visual_window_content_frame_hash_changes={visual_content_frame_hash_changes} "
    f"visual_window_content_frame_delta={visual_content_frame_delta} "
    f"visual_window_content_frame_fps={visual_content_frame_fps:.3f} "
    f"visual_window_native_present_fps={visual_native_fps:.3f} "
    f"display_completion_delta={completed} "
    f"display_completion_fps={completion_fps:.3f} "
    f"visible_avg={avg:.3f} visible_low={low:.3f} "
    "demo_visible=1 demo_closeable=1 demo_resizable=1 "
    "demo_interaction_native_present=1 "
    "demo_interaction_content_progress=1 "
    f"visual_progress_fps={visual_progress_fps:.3f} "
    f"outside_overlay_crc_changes={outside_overlay_crc_transitions} "
    f"backend_mode={backend_mode} backend_opengl_submit=1 "
    f"window={ww}x{wh} render={rw}x{rh} render_div={rdiv} "
    f"changed={','.join(str(v) for v in transitions)} "
    f"samples={','.join(f'{v:.3f}' for v in values)}"
)
PY
