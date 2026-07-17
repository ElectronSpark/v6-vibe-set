#!/usr/bin/env bash
# Stage WebKit smoke media into a generated rootfs overlay.
set -euo pipefail

OVERLAY="${1:?usage: $0 <overlay>}"
BUILD_DIR="${WEBKIT_MEDIA_BUILD_DIR:-build-x86_64/webkit-media}"
DEST="${OVERLAY}/share/webkit"
MEDIA_THREADS="${WEBKIT_MEDIA_THREADS:-2}"

[[ "${MEDIA_THREADS}" =~ ^[1-9][0-9]*$ ]] || {
    echo "stage-webkit-media: WEBKIT_MEDIA_THREADS must be positive" >&2
    exit 2
}

mkdir -p "${DEST}" "${BUILD_DIR}"
failures=0

copy_from_source_dir() {
    local name="$1"
    local out="${BUILD_DIR}/${name}"

    if [[ -n "${WEBKIT_MEDIA_SOURCE_DIR:-}" && -f "${WEBKIT_MEDIA_SOURCE_DIR}/${name}" ]]; then
        cp -a "${WEBKIT_MEDIA_SOURCE_DIR}/${name}" "${out}"
        cp -a "${out}" "${DEST}/${name}"
        return 0
    fi
    return 1
}

generate_with_gst() {
    local name="$1"
    local pipeline="$2"
    local out="${BUILD_DIR}/${name}"
    local -a pipeline_args=()

    if [[ -f "${out}" ]]; then
        cp -a "${out}" "${DEST}/${name}"
        return 0
    fi
    command -v gst-launch-1.0 >/dev/null 2>&1 || return 1
    read -r -a pipeline_args <<<"${pipeline}"
    if gst-launch-1.0 -q "${pipeline_args[@]}" '!' filesink "location=${out}"; then
        cp -a "${out}" "${DEST}/${name}"
        return 0
    fi
    rm -f "${out}"
    return 1
}

generate_with_ffmpeg() {
    local name="$1"
    local width="$2"
    local height="$3"
    local fps="$4"
    local duration="$5"
    local container="$6"
    local detail_profile="${7:-basic}"
    local out="${BUILD_DIR}/${name}"
    local meta="${out}.profile"
    local profile_version="${detail_profile}"
    local source="testsrc2=size=${width}x${height}:rate=${fps}"

    if [[ "${detail_profile}" == "detail" ]]; then
        profile_version="detail-v4-gradient-${width}x${height}-${fps}fps-${duration}s"
    fi

    if [[ -f "${out}" ]] &&
       [[ "${detail_profile}" != "detail" ||
          ( -f "${meta}" && "$(cat "${meta}")" == "${profile_version}" ) ]]; then
        cp -a "${out}" "${DEST}/${name}"
        return 0
    fi
    command -v ffmpeg >/dev/null 2>&1 || return 1

    if [[ "${detail_profile}" == "detail" ]]; then
        source="gradients=size=${width}x${height}:rate=${fps}:duration=${duration}:c0=black:c1=white:c2=red:c3=lime:c4=blue:c5=cyan:c6=magenta:c7=yellow:nb_colors=8:speed=0.015:type=linear,drawgrid=w=8:h=8:t=1:c=white@0.28,drawgrid=w=64:h=64:t=2:c=black@0.25"
    fi

    case "${container}" in
        mp4)
            if ffmpeg -hide_banner -loglevel error -y \
                -filter_threads "${MEDIA_THREADS}" \
                -f lavfi -i "${source}" \
                -t "${duration}" -an -c:v libx264 -threads "${MEDIA_THREADS}" -preset veryfast -crf 12 \
                -profile:v high -pix_fmt yuv420p -g "${fps}" \
                -x264-params "keyint=${fps}:min-keyint=${fps}:scenecut=0" \
                -movflags +faststart \
                "${out}"; then
                printf '%s\n' "${profile_version}" > "${meta}"
                cp -a "${out}" "${DEST}/${name}"
                return 0
            fi
            if ffmpeg -hide_banner -loglevel error -y \
                -filter_threads "${MEDIA_THREADS}" \
                -f lavfi -i "${source}" \
                -t "${duration}" -an -c:v mpeg4 -threads "${MEDIA_THREADS}" -q:v 1 -b:v 24000k -movflags +faststart \
                "${out}"; then
                printf '%s\n' "${profile_version}" > "${meta}"
                cp -a "${out}" "${DEST}/${name}"
                return 0
            fi
            ;;
        webm)
            if ffmpeg -hide_banner -loglevel error -y \
                -filter_threads "${MEDIA_THREADS}" \
                -f lavfi -i "${source}" \
                -t "${duration}" -an -c:v libvpx -threads "${MEDIA_THREADS}" -deadline realtime -cpu-used 8 -b:v 800k \
                "${out}"; then
                cp -a "${out}" "${DEST}/${name}"
                return 0
            fi
            ;;
    esac

    rm -f "${out}"
    rm -f "${meta}"
    return 1
}

missing_asset() {
    local name="$1"

    echo "stage-webkit-media: error: could not stage ${name}" >&2
    failures=$((failures + 1))
}

stage_webm() {
    local name="$1"
    local width="${2:-320}"
    local height="${3:-180}"
    local fps="${4:-15}"
    local duration="${5:-2}"
    local detail_profile="${6:-basic}"
    local buffers=$((fps * duration))

    copy_from_source_dir "${name}" && return 0
    generate_with_ffmpeg "${name}" "${width}" "${height}" "${fps}" "${duration}" webm "${detail_profile}" &&
        return 0
    generate_with_gst "${name}" \
        "videotestsrc num-buffers=${buffers} ! video/x-raw,width=${width},height=${height},framerate=${fps}/1 ! vp8enc deadline=1 ! webmmux" &&
        return 0
    missing_asset "${name}"
}

stage_mp4() {
    local name="$1"
    local width="${2:-320}"
    local height="${3:-180}"
    local fps="${4:-15}"
    local duration="${5:-2}"
    local detail_profile="${6:-basic}"
    local buffers=$((fps * duration))

    copy_from_source_dir "${name}" && return 0
    generate_with_ffmpeg "${name}" "${width}" "${height}" "${fps}" "${duration}" mp4 "${detail_profile}" &&
        return 0
    generate_with_gst "${name}" \
        "videotestsrc num-buffers=${buffers} ! video/x-raw,width=${width},height=${height},framerate=${fps}/1 ! videoconvert ! avenc_mpeg4 ! mp4mux" &&
        return 0
    missing_asset "${name}"
}

stage_webm libsoup-test.webm
stage_mp4 long-test.mp4
stage_mp4 perf-1280x800-60fps.mp4 1280 800 60 16 detail
stage_webm test-mse-audio.webm
stage_mp4 test-mse.mp4
stage_webm test-mse.webm
stage_mp4 test-without-audio-track.mp4

if ((failures > 0)); then
    echo "stage-webkit-media: install gstreamer1.0-tools with the libav/good/bad plugins, install ffmpeg, or set WEBKIT_MEDIA_SOURCE_DIR" >&2
    exit 1
fi
