#!/usr/bin/env bash
# Combine the §13 YouTube decoder-QoS and host-visible cadence metrics.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

GST_LOG=""
DURATION_SECONDS=""
CADENCE_CROP=""
MIN_DURATION_SECONDS="300"
MAX_DROPS_PER_MINUTE="5"
MAX_LATENESS_MS="100"
MIN_CHANGED_PIXELS="100"
ALLOW_INACTIVE_PAIRS="0"
IMAGES=()

usage() {
    cat >&2 <<'EOF'
usage: webkit-youtube-smoothness-report.sh --gst-log LOG [options] -- IMAGES...

Options:
  --duration-seconds N        Authoritative watch duration for QoS.
  --min-duration-seconds N    Minimum QoS duration (default: 300).
  --max-drops-per-minute N    Maximum avdec_h264 QoS drops/minute (default: 5).
  --max-lateness-ms N         Maximum QoS lateness in ms (default: 100).
  --crop WxH+X+Y              Crop whole screenshots before cadence comparison.
  --min-changed-pixels N      Changed-pixel threshold per adjacent pair (default: 100).
  --allow-inactive-pairs N    Adjacent pairs allowed below threshold (default: 0).
  -h, --help                  Show this help.

Examples:
  webkit-youtube-smoothness-report.sh \
    --gst-log /tmp/xv6-youtube-host-cadence/webkit-gst-debug.log \
    --duration-seconds 300 --crop 560x300+480+545 -- '/tmp/.../host-*.png'
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --gst-log)
            [[ $# -ge 2 ]] || { echo "missing value for --gst-log" >&2; exit 2; }
            GST_LOG="$2"
            shift 2
            ;;
        --duration-seconds)
            [[ $# -ge 2 ]] || { echo "missing value for --duration-seconds" >&2; exit 2; }
            DURATION_SECONDS="$2"
            shift 2
            ;;
        --min-duration-seconds)
            [[ $# -ge 2 ]] || { echo "missing value for --min-duration-seconds" >&2; exit 2; }
            MIN_DURATION_SECONDS="$2"
            shift 2
            ;;
        --max-drops-per-minute)
            [[ $# -ge 2 ]] || { echo "missing value for --max-drops-per-minute" >&2; exit 2; }
            MAX_DROPS_PER_MINUTE="$2"
            shift 2
            ;;
        --max-lateness-ms)
            [[ $# -ge 2 ]] || { echo "missing value for --max-lateness-ms" >&2; exit 2; }
            MAX_LATENESS_MS="$2"
            shift 2
            ;;
        --crop)
            [[ $# -ge 2 ]] || { echo "missing value for --crop" >&2; exit 2; }
            CADENCE_CROP="$2"
            shift 2
            ;;
        --min-changed-pixels)
            [[ $# -ge 2 ]] || { echo "missing value for --min-changed-pixels" >&2; exit 2; }
            MIN_CHANGED_PIXELS="$2"
            shift 2
            ;;
        --allow-inactive-pairs)
            [[ $# -ge 2 ]] || { echo "missing value for --allow-inactive-pairs" >&2; exit 2; }
            ALLOW_INACTIVE_PAIRS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            IMAGES+=("$@")
            break
            ;;
        *)
            IMAGES+=("$1")
            shift
            ;;
    esac
done

[[ -n "${GST_LOG}" ]] || { usage; echo "missing --gst-log" >&2; exit 2; }
[[ ${#IMAGES[@]} -gt 0 ]] || { usage; echo "missing cadence images" >&2; exit 2; }

qos_args=(
    "${GST_LOG}"
    --min-duration-seconds "${MIN_DURATION_SECONDS}"
    --max-drops-per-minute "${MAX_DROPS_PER_MINUTE}"
    --max-lateness-ms "${MAX_LATENESS_MS}"
)
if [[ -n "${DURATION_SECONDS}" ]]; then
    qos_args+=(--duration-seconds "${DURATION_SECONDS}")
fi

cadence_args=(
    --min-changed-pixels "${MIN_CHANGED_PIXELS}"
    --allow-inactive-pairs "${ALLOW_INACTIVE_PAIRS}"
)
if [[ -n "${CADENCE_CROP}" ]]; then
    cadence_args+=(--crop "${CADENCE_CROP}")
fi
cadence_args+=("${IMAGES[@]}")

qos_tmp="$(mktemp)"
cadence_tmp="$(mktemp)"
trap 'rm -f "${qos_tmp}" "${cadence_tmp}"' EXIT

qos_rc=0
python3 "${SCRIPT_DIR}/webkit-qos-report.py" "${qos_args[@]}" >"${qos_tmp}" 2>&1 || qos_rc=$?
cadence_rc=0
python3 "${SCRIPT_DIR}/webkit-cadence-report.py" "${cadence_args[@]}" >"${cadence_tmp}" 2>&1 || cadence_rc=$?

cat "${qos_tmp}"
cat "${cadence_tmp}"

if [[ "${qos_rc}" -eq 0 && "${cadence_rc}" -eq 0 ]]; then
    echo "YOUTUBE-SMOOTHNESS-RESULT pass qos=pass cadence=pass gst_log=${GST_LOG}"
    exit 0
fi

echo "YOUTUBE-SMOOTHNESS-RESULT fail qos_rc=${qos_rc} cadence_rc=${cadence_rc} gst_log=${GST_LOG}"
exit 1
