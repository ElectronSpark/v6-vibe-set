#!/usr/bin/env bash
set -euo pipefail

FSIMG="${TITLEBAR_MATRIX_FSIMG:-build-x86_64/fs.img}"
OUTDIR="${TITLEBAR_MATRIX_OUTDIR:-build-x86_64/titlebar-control-matrix}"
APP="${TITLEBAR_MATRIX_APP:-}"
CONTROLS="${TITLEBAR_MATRIX_CONTROLS:-min max close}"
MIN_CHANGED="${TITLEBAR_MATRIX_MIN_CHANGED:-1}"
CROP="${TITLEBAR_MATRIX_CROP:-}"

usage() {
    cat <<'EOF'
Usage: scripts/gpu/titlebar-control-matrix.sh --app APP [options]

Extract before/after guest PPMs from fs.img and emit TITLEBAR-CONTROL-RESULT
lines through titlebar-control-report.py. This is the host-side metric stage
for the titlebar expect probes.

Options:
  --app NAME            app prefix in guest PPM names, e.g. netsurf or glmaze
  --controls LIST       space-separated controls (default: "min max close")
  --fsimg PATH          rootfs image (default: build-x86_64/fs.img)
  --outdir PATH         extraction/report directory
  --min-changed N       minimum changed pixels for pass
  --crop WxH+X+Y        optional crop passed to the reporter

Default guest paths are /APP-CONTROL-before.ppm and /APP-CONTROL-after.ppm.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app) APP="$2"; shift 2 ;;
        --controls) CONTROLS="$2"; shift 2 ;;
        --fsimg) FSIMG="$2"; shift 2 ;;
        --outdir) OUTDIR="$2"; shift 2 ;;
        --min-changed) MIN_CHANGED="$2"; shift 2 ;;
        --crop) CROP="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "titlebar-control-matrix: unknown argument $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "${APP}" ]]; then
    echo "titlebar-control-matrix: --app is required" >&2
    exit 2
fi
if [[ ! -f "${FSIMG}" ]]; then
    echo "titlebar-control-matrix: missing fs image ${FSIMG}" >&2
    exit 2
fi
command -v debugfs >/dev/null 2>&1 || {
    echo "titlebar-control-matrix: debugfs is required" >&2
    exit 2
}

mkdir -p "${OUTDIR}"
summary="${OUTDIR}/${APP}-summary.tsv"
printf 'app\tcontrol\tstatus\tchanged_pixels\ttotal_pixels\tchanged_ratio\n' >"${summary}"

rc=0
for control in ${CONTROLS}; do
    guest_before="/${APP}-${control}-before.ppm"
    guest_after="/${APP}-${control}-after.ppm"
    host_before="${OUTDIR}/${APP}-${control}-before.ppm"
    host_after="${OUTDIR}/${APP}-${control}-after.ppm"
    log="${OUTDIR}/${APP}-${control}.log"
    rm -f "${host_before}" "${host_after}" "${log}"

    if ! debugfs -R "dump ${guest_before} ${host_before}" "${FSIMG}" >"${log}" 2>&1 ||
       [[ ! -s "${host_before}" ]]; then
        echo "TITLEBAR-CONTROL-RESULT fail app=${APP} control=${control} error=missing-before path=${guest_before}" | tee -a "${log}"
        rc=1
        continue
    fi
    if ! debugfs -R "dump ${guest_after} ${host_after}" "${FSIMG}" >>"${log}" 2>&1 ||
       [[ ! -s "${host_after}" ]]; then
        echo "TITLEBAR-CONTROL-RESULT fail app=${APP} control=${control} error=missing-after path=${guest_after}" | tee -a "${log}"
        rc=1
        continue
    fi

    args=(--control "${APP}-${control}" --before "${host_before}" --after "${host_after}" --min-changed "${MIN_CHANGED}")
    if [[ -n "${CROP}" ]]; then
        args+=(--crop "${CROP}")
    fi
    line="$(scripts/gpu/titlebar-control-report.py "${args[@]}")" || rc=1
    echo "${line}" | tee -a "${log}"
    status="$(awk '{print $2}' <<<"${line}")"
    changed="$(sed -n 's/.*changed_pixels=\([0-9][0-9]*\).*/\1/p' <<<"${line}")"
    total="$(sed -n 's/.*total_pixels=\([0-9][0-9]*\).*/\1/p' <<<"${line}")"
    ratio="$(sed -n 's/.*changed_ratio=\([0-9.][0-9.]*\).*/\1/p' <<<"${line}")"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${APP}" "${control}" "${status}" "${changed:-0}" "${total:-0}" "${ratio:-0}" >>"${summary}"
done

echo "TITLEBAR-CONTROL-MATRIX app=${APP} controls=\"${CONTROLS}\" summary=${summary} status=$([[ ${rc} -eq 0 ]] && echo pass || echo fail)"
exit "${rc}"
