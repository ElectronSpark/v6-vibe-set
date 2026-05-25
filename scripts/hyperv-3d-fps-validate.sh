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
line = (
    "hyperv-3d-fps-validate: anti-inflation selftest ok "
    f"validation_run_id={run_id} mode={mode} negative_rejected=1 "
    "displayed_fps=40.000 visual_progress_fps=5.000 "
    "stale_run_rejected=1 static_content_rejected=1 "
    "frozen_window_negative=1 frozen_window_rejected=1 "
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
content_line = (
    "hyperv-3d-fps-validate: fps_visible_progress_negative_matrix "
    f"validation_run_id={run_id} outside_overlay_crc_changes=0 "
    "content_frame_delta=0 native_present_delta=0 rejected=1 "
    "status=PASS"
)
print(content_line)
with log_path.open("a", encoding="utf-8") as out:
    out.write(line + "\n")
    out.write(overlay_line + "\n")
    out.write(content_line + "\n")
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
serial_read "rm -f /tmp/mesawlegl-fps /tmp/wlcomp-d3d12-present /tmp/hyperv-3d-fps-demo.log /tmp/hyperv-3d-fps-demo.pid; XV6_GPU_VALIDATE_RUN_ID=${VALIDATION_RUN_ID} XV6_WLCOMP_D3D12_RUN_ID=${VALIDATION_RUN_ID} mesademo --frames=${DEMO_FRAMES} --size=${DEMO_SIZE} --render-div=1 --present-interval=1 --pace-us=0 >/tmp/hyperv-3d-fps-demo.log 2>&1 & echo \$! >/tmp/hyperv-3d-fps-demo.pid" 30000 |
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
for i in $(seq 0 "${SAMPLE_SEC}"); do
    echo "hyperv-3d-fps-validate: sample ${i}" | tee -a "${LOG}"
    serial_read 'cat /proc/uptime; cat /tmp/mesawlegl-fps; cat /tmp/wlcomp-fps; cat /tmp/wlcomp-d3d12-present; fbstat' 30000 | tee -a "${LOG}"
    if [[ "${i}" -eq 0 ]]; then
        echo "hyperv-3d-fps-validate: visual thumbnails inside finite sample window samples=${VISUAL_SAMPLES} interval_ms=${VISUAL_SAMPLE_MS}" |
            tee -a "${LOG}"
        for j in $(seq 1 "${VISUAL_SAMPLES}"); do
            capture_thumbnail_raw "${VISUAL_DIR}/frame-${j}.raw" | tee -a "${LOG}"
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
    "${VISUAL_MIN_FPS}" "${LOW_VISUAL_CADENCE_FPS}" <<'PY'
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
visual_min_fps = float(sys.argv[17])
low_visual_cadence_fps = float(sys.argv[18])
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
d3d12_gpup_dda_commit_successes = []
d3d12_same_frame_callback_releases = []
d3d12_same_frame_callbacks = []
d3d12_same_frame_releases = []
d3d12_content_crcs = []
d3d12_content_frames = []
d3d12_run_ids = []
d3d12_native_paths = []
d3d12_reject_evidence = []
backend_modes = []
backend_opengl_submit_samples = []
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
visual_gpup_dda_commit_successes = []
visual_same_frame_callback_releases = []
visual_same_frame_callbacks = []
visual_same_frame_releases = []
visual_content_crcs = []
visual_content_frames = []
visual_run_ids = []
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
)
content_crc_keys = (
    "d3d12_present_content_crc",
    "d3d12_present_region_crc",
    "d3d12_client_content_crc",
    "d3d12_visible_content_crc",
    "d3d12_present_crc",
)
content_frame_keys = (
    "d3d12_present_content_frame",
    "d3d12_present_content_frames",
    "d3d12_present_content_change",
    "d3d12_present_content_changes",
    "d3d12_visible_content_frame",
    "d3d12_visible_content_frames",
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

    transitions = []
    for a, b in zip(frames, frames[1:]):
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
        transitions.append(changed)
    return frames, frame_times, outside_overlay_crcs, transitions

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
        run_id = token_value(line, run_id_keys)
        if run_id is not None:
            visual_run_ids.append(run_id)
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
        run_id = token_value(line, run_id_keys)
        if run_id is not None:
            d3d12_run_ids.append(run_id)
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
    if fps and seq:
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
        demo_records.append(record)

in_window_demo_times = [
    record["sample_time"]
    for record in demo_records
    if record["in_sampling"] and record["sample_time"] is not None
]
demo_time_min = min(in_window_demo_times) if in_window_demo_times else None
demo_time_max = max(in_window_demo_times) if in_window_demo_times else None
in_window_demo_keys = {
    (record["seq"], record["sample_time"])
    for record in demo_records
    if record["in_sampling"]
}
accepted_demo_records = []
for record in demo_records:
    timestamped_in_window = (
        demo_time_min is not None and
        record["sample_time"] is not None and
        demo_time_min <= record["sample_time"] <= demo_time_max
    )
    duplicate_in_window_sample = (
        (record["seq"], record["sample_time"]) in in_window_demo_keys
    )
    if (record["in_sampling"] or
            (timestamped_in_window and not duplicate_in_window_sample)):
        accepted_demo_records.append(record)
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
frames, frame_times, outside_overlay_crcs, transitions = load_thumbnail_progress()
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
d3d12_gpup_dda_commit_deltas = require_advancing_counter(
    "GPU-P/DDA present-source commit accepted",
    d3d12_gpup_dda_commit_successes,
)
sample_present_id_delta = d3d12_present_ids[-1] - d3d12_present_ids[0]
sample_present_completed_delta = (
    d3d12_present_completed[-1] - d3d12_present_completed[0]
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
    for native, display, callback, release, generation, evidence_time, present_id, present_done, gpup_dda_commit in zip(
        native_completion_deltas,
        display_completion_deltas,
        callback_deltas,
        release_deltas,
        d3d12_generation_deltas,
        d3d12_evidence_time_deltas,
        d3d12_present_id_deltas,
        d3d12_present_completed_deltas,
        d3d12_gpup_dda_commit_deltas,
    )
    if (native > 0 and display > 0 and callback > 0 and release > 0 and
        generation > 0 and evidence_time > 0 and present_id > 0 and
        present_done > 0 and gpup_dda_commit > 0)
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
    len(d3d12_gpup_dda_commit_deltas),
)
if active_full_evidence_intervals < required_full_evidence_intervals:
    raise SystemExit(
        "native/display/callback/release/run-evidence/DXG-present/GPU-P/DDA-commit counters "
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
        f"gpup_dda_commit_deltas={','.join(str(v) for v in d3d12_gpup_dda_commit_deltas)}"
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
            r"candidate_cmds[:=]presenthistory=34,redirected_flip_fence=35,blt=38",
            log,
    ):
        fail_validation(
            "D3D12 present fail-closed GPU-P/DDA display-bind diagnostic "
            "lacked candidate_cmds presenthistory=34,redirected_flip_fence=35,blt=38"
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
visual_gpup_dda_commit_deltas = require_advancing_counter(
    "visual-window GPU-P/DDA present-source commit accepted",
    visual_gpup_dda_commit_successes,
)
visual_present_id_delta = visual_present_ids[-1] - visual_present_ids[0]
visual_present_completed_delta = (
    visual_present_completed[-1] - visual_present_completed[0]
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
effective_presented_fps = min(native_fps, completion_fps, visual_native_fps)
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
    f"gpup_dda_commit_delta={visual_gpup_dda_commit_delta} "
    f"evidence_generation_delta={sum(visual_generation_deltas)} "
    f"evidence_time_delta_us={sum(visual_evidence_time_deltas)} "
    f"frame_callback_delta={visual_callbacks} "
    f"buffer_release_delta={visual_releases} "
    f"content_crc_changes={visual_content_crc_changes} "
    f"content_frame_delta={visual_content_frame_delta} "
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
    f"sample_gpup_dda_commit_delta={sample_gpup_dda_commit_delta} "
    f"sample_evidence_generation_delta={sum(d3d12_generation_deltas)} "
    f"sample_evidence_time_delta_us={sum(d3d12_evidence_time_deltas)} "
    f"sample_content_crc_changes={sample_content_crc_changes} "
    f"sample_content_frame_delta={sample_content_frame_delta} "
    f"active_native_intervals={active_native_intervals} "
    f"active_display_intervals={active_display_intervals} "
    f"active_callback_intervals={active_callback_intervals} "
    f"active_release_intervals={active_release_intervals} "
    f"active_present_id_intervals={active_present_id_intervals} "
    f"active_completed_intervals={active_present_completed_intervals} "
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
if effective_presented_fps <= min_fps:
    raise SystemExit(
        "effective real-presented FPS below gate: "
        f"effective_presented_fps={effective_presented_fps:.3f} "
        f"native_present_fps={native_fps:.3f} "
        f"display_completion_fps={completion_fps:.3f} "
        f"visual_window_native_present_fps={visual_native_fps:.3f} "
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
    f"sample_present_id_delta={sample_present_id_delta} "
    f"sample_completed_delta={sample_present_completed_delta} "
    f"sample_gpup_dda_commit_delta={sample_gpup_dda_commit_delta} "
    f"visual_present_id_delta={visual_present_id_delta} "
    f"visual_completed_delta={visual_present_completed_delta} "
    f"visual_gpup_dda_commit_delta={visual_gpup_dda_commit_delta} "
    f"visual_window_native_present_fps={visual_native_fps:.3f} "
    f"effective_presented_fps={effective_presented_fps:.3f} "
    f"app_visible_avg={avg:.3f}"
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
    f"sample_gpup_dda_commit_delta={sample_gpup_dda_commit_delta} "
    f"sample_content_crc_changes={sample_content_crc_changes} "
    f"sample_content_frame_delta={sample_content_frame_delta} "
    f"active_native_intervals={active_native_intervals} "
    f"active_display_intervals={active_display_intervals} "
    f"active_callback_intervals={active_callback_intervals} "
    f"active_release_intervals={active_release_intervals} "
    f"active_present_id_intervals={active_present_id_intervals} "
    f"active_completed_intervals={active_present_completed_intervals} "
    f"active_gpup_dda_commit_intervals={active_gpup_dda_commit_intervals} "
    f"active_content_frame_intervals={active_content_frame_intervals} "
    f"active_native_display_intervals={active_native_display_intervals} "
    f"active_native_display_callback_release_intervals="
    f"{active_native_display_callback_release_intervals} "
    f"active_full_evidence_intervals={active_full_evidence_intervals} "
    f"visual_window_native_present_delta={visual_native_completed} "
    f"visual_window_present_id_delta={visual_present_id_delta} "
    f"visual_window_completed_delta={visual_present_completed_delta} "
    f"visual_window_gpup_dda_commit_delta={visual_gpup_dda_commit_delta} "
    f"visual_window_evidence_generation_delta={sum(visual_generation_deltas)} "
    f"visual_window_evidence_time_delta_us={sum(visual_evidence_time_deltas)} "
    f"visual_window_frame_callback_delta={visual_callbacks} "
    f"visual_window_buffer_release_delta={visual_releases} "
    f"visual_window_content_crc_changes={visual_content_crc_changes} "
    f"visual_window_content_frame_delta={visual_content_frame_delta} "
    f"visual_window_native_present_fps={visual_native_fps:.3f} "
    f"display_completion_delta={completed} "
    f"display_completion_fps={completion_fps:.3f} "
    f"visible_avg={avg:.3f} visible_low={low:.3f} "
    f"visual_progress_fps={visual_progress_fps:.3f} "
    f"outside_overlay_crc_changes={outside_overlay_crc_transitions} "
    f"backend_mode={backend_mode} backend_opengl_submit=1 "
    f"window={ww}x{wh} render={rw}x{rh} render_div={rdiv} "
    f"changed={','.join(str(v) for v in transitions)} "
    f"samples={','.join(f'{v:.3f}' for v in values)}"
)
PY
