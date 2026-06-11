#!/usr/bin/env bash
# Run the §8 WebKit fullscreen-video performance gate repeatedly and summarize
# the pass/fail metrics needed by docs/linux-drm-abi-compat-plan.md §13.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

RUNS="${PERF_VIDEO_GATE_RUNS:-10}"
OUTDIR="${PERF_VIDEO_GATE_MATRIX_OUTDIR:-${REPO_ROOT}/build-x86_64/perf-video-gate-matrix}"
EXPECT_BIN="${EXPECT:-expect}"
GATE="${REPO_ROOT}/scripts/gpu/perf-video-gate.expect"

usage() {
    cat >&2 <<'EOF'
usage: perf-video-gate-matrix.sh [options]

Options:
  --runs N       Number of consecutive §8 gate runs (default: 10).
  --outdir DIR   Directory for per-run logs and summary.
  -h, --help     Show this help.

Environment:
  PERF_VIDEO_GATE_RUNS, PERF_VIDEO_GATE_MATRIX_OUTDIR, EXPECT
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --runs)
            [[ $# -ge 2 ]] || { echo "missing value for --runs" >&2; exit 2; }
            RUNS="$2"
            shift 2
            ;;
        --outdir)
            [[ $# -ge 2 ]] || { echo "missing value for --outdir" >&2; exit 2; }
            OUTDIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

[[ "${RUNS}" =~ ^[0-9]+$ && "${RUNS}" -gt 0 ]] || {
    echo "PERF-GATE-MATRIX-FAIL invalid-runs=${RUNS}" >&2
    exit 2
}
command -v "${EXPECT_BIN}" >/dev/null || {
    echo "PERF-GATE-MATRIX-FAIL missing-expect=${EXPECT_BIN}" >&2
    exit 2
}
[[ -r "${GATE}" ]] || {
    echo "PERF-GATE-MATRIX-FAIL missing-gate=${GATE}" >&2
    exit 2
}

mkdir -p "${OUTDIR}"
summary="${OUTDIR}/summary.tsv"
printf 'run\tstatus\tfps\tspeed\tdecodedFPS\tdropPct\tadvanced\tlog\n' > "${summary}"

passes=0
failures=0
crashes=0
missing_results=0

echo "PERF-GATE-MATRIX-BEGIN runs=${RUNS} outdir=${OUTDIR}"
for ((run = 1; run <= RUNS; run++)); do
    run_dir="${OUTDIR}/run-$(printf '%02d' "${run}")"
    rm -rf "${run_dir}"
    mkdir -p "${run_dir}"
    echo "PERF-GATE-MATRIX-RUN-BEGIN run=${run} dir=${run_dir}"

    rc=0
    (
        cd "${REPO_ROOT}"
        PERF_VIDEO_GATE_OUTDIR="${run_dir}" "${EXPECT_BIN}" "${GATE}"
    ) >"${run_dir}/driver.log" 2>&1 || rc=$?

    log="${run_dir}/run.log"
    result="$(grep -aE 'xv6-perf-video:RESULT' "${log}" 2>/dev/null | tail -1 || true)"
    if grep -aEq 'panic|fatal page fault|coredump: generating|virtio_failure|WebKit smoke failed|GATE-FAIL' "${run_dir}/driver.log" "${log}" 2>/dev/null; then
        crashes=$((crashes + 1))
    fi

    if [[ "${rc}" -eq 0 && "${result}" =~ xv6-perf-video:RESULT[[:space:]]+pass ]]; then
        status="pass"
        passes=$((passes + 1))
    else
        status="fail"
        failures=$((failures + 1))
        [[ -n "${result}" ]] || missing_results=$((missing_results + 1))
    fi

    fps="$(sed -nE 's/.*(^|[[:space:]])fps=([^[:space:]]+).*/\2/p' <<<"${result}" | tail -1)"
    speed="$(sed -nE 's/.*(^|[[:space:]])speed=([^[:space:]]+).*/\2/p' <<<"${result}" | tail -1)"
    decoded="$(sed -nE 's/.*(^|[[:space:]])decodedFPS=([^[:space:]]+).*/\2/p' <<<"${result}" | tail -1)"
    drop="$(sed -nE 's/.*(^|[[:space:]])dropPct=([^[:space:]]+).*/\2/p' <<<"${result}" | tail -1)"
    advanced="$(sed -nE 's/.*(^|[[:space:]])advanced=([^[:space:]]+).*/\2/p' <<<"${result}" | tail -1)"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${run}" "${status}" "${fps:-NA}" "${speed:-NA}" "${decoded:-NA}" \
        "${drop:-NA}" "${advanced:-NA}" "${log}" >> "${summary}"

    echo "PERF-GATE-MATRIX-RUN-${status^^} run=${run} rc=${rc} result=${result:-missing}"
    if [[ "${status}" != "pass" ]]; then
        break
    fi
done

if [[ "${failures}" -eq 0 && "${passes}" -eq "${RUNS}" && "${crashes}" -eq 0 && "${missing_results}" -eq 0 ]]; then
    echo "PERF-GATE-MATRIX-PASS runs=${RUNS} passes=${passes} failures=0 crashes=0 missing_results=0 summary=${summary}"
    exit 0
fi

echo "PERF-GATE-MATRIX-FAIL runs=${RUNS} passes=${passes} failures=${failures} crashes=${crashes} missing_results=${missing_results} summary=${summary}"
exit 1
