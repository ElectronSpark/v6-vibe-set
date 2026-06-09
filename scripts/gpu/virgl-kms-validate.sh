#!/usr/bin/env bash
# Validate the Alpine/kmscube-style xv6 virgl path: GBM + EGL/GLES + KMS flips.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-build-x86_64}"
LOG="${VIRGL_KMS_VALIDATE_LOG:-${ROOT}/${BUILD_DIR}/virgl-kms-validate.log}"
MODE="${VIRGL_KMS_VALIDATE_MODE:-gtk}"
TIMEOUT="${VIRGL_KMS_VALIDATE_TIMEOUT:-120s}"
DURATION_SECONDS="${VIRGL_KMS_VALIDATE_SECONDS:-4}"
XRES="${VIRGL_KMS_VALIDATE_XRES:-1280}"
YRES="${VIRGL_KMS_VALIDATE_YRES:-800}"
TOKEN="${VIRGL_KMS_VALIDATE_TOKEN:-virglkms-$$}"
SCREENSHOT="${VIRGL_KMS_VALIDATE_SCREENSHOT:-${ROOT}/${BUILD_DIR}/virgl-kms-validate.ppm}"
GUEST_SCREENSHOT="${VIRGL_KMS_VALIDATE_GUEST_SCREENSHOT:-/xv6-virgl-kms-current.ppm}"
MONITOR_SOCK="$(mktemp -u /tmp/xv6-virgl-kms-monitor.XXXXXX)"

mkdir -p "$(dirname "${LOG}")"
: > "${LOG}"

fail()
{
    echo "virgl-kms-validate: $*" >&2
    echo "virgl-kms-validate: log: ${LOG}" >&2
    exit 1
}

require_log()
{
    local pattern="$1"
    local why="$2"

    if ! grep -Eq "${pattern}" "${LOG}"; then
        fail "missing ${why}"
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

cleanup_qemu()
{
    pkill -TERM -f "qemu-system-x86_64 .*${TOKEN}" 2>/dev/null || true
    rm -f "${MONITOR_SOCK}"
}

extract_guest_screenshot()
{
    grep -q '__VIRGL_KMS_FB_CAPTURED__' "${LOG}" || return 1
    command -v debugfs >/dev/null 2>&1 ||
        fail "debugfs is required to extract the guest framebuffer sample"
    debugfs -R "dump ${GUEST_SCREENSHOT} ${SCREENSHOT}" \
        "${BUILD_DIR}/fs.img" >>"${LOG}" 2>&1
}

validate_screenshot()
{
    python3 - "${SCREENSHOT}" "${XRES}" "${YRES}" >>"${LOG}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
want_w = int(sys.argv[2])
want_h = int(sys.argv[3])
data = path.read_bytes()

def token(offset):
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    if offset < len(data) and data[offset:offset + 1] == b"#":
        while offset < len(data) and data[offset] not in b"\r\n":
            offset += 1
        return token(offset)
    start = offset
    while offset < len(data) and data[offset] not in b" \t\r\n":
        offset += 1
    return data[start:offset], offset

magic, off = token(0)
width, off = token(off)
height, off = token(off)
maxval, off = token(off)
if off < len(data) and data[off] in b" \t\r\n":
    off += 1
w = int(width)
h = int(height)
pixels = data[off:]
expected = w * h * 3
if magic != b"P6" or int(maxval) != 255 or len(pixels) != expected:
    print(f"virgl_kms_screenshot_matrix path={path} status=FAIL reason=header width={w} height={h} bytes={len(pixels)} expected={expected}")
    sys.exit(1)

total = w * h
stride = max(1, total // 4096)
nonblack = 0
bright = 0
sampled = set()
for idx in range(total):
    base = idx * 3
    rgb = pixels[base:base + 3]
    if rgb != b"\x00\x00\x00":
        nonblack += 1
    if max(rgb) >= 128:
        bright += 1
    if idx % stride == 0:
        sampled.add(rgb)

status = "PASS"
reason = "ok"
if w != want_w or h != want_h:
    status = "FAIL"
    reason = "dimensions"
elif nonblack < max(1000, total // 50):
    status = "FAIL"
    reason = "blank"
elif bright < max(100, total // 200):
    status = "FAIL"
    reason = "dark"
elif len(sampled) < 8:
    status = "FAIL"
    reason = "low_variance"

print(f"virgl_kms_screenshot_matrix path={path} width={w} height={h} nonblack={nonblack} bright={bright} unique_sample={len(sampled)} status={status} reason={reason}")
if status != "PASS":
    sys.exit(1)
PY
}

if [[ "${VIRGL_KMS_VALIDATE_BUILD:-0}" == "1" ]]; then
    cmake --build "${BUILD_DIR}/ports" --target port-wayland -j"${VIRGL_KMS_VALIDATE_JOBS:-2}" | tee -a "${LOG}"
    scripts/image/make-rootfs.sh "${BUILD_DIR}/sysroot" "${BUILD_DIR}/fs.img" "${VIRGL_KMS_VALIDATE_IMAGE_MB:-2304}" | tee -a "${LOG}"
fi

command -v expect >/dev/null 2>&1 ||
    fail "expect is required for prompt-synchronized guest commands"

trap cleanup_qemu EXIT

mkdir -p "$(dirname "${SCREENSHOT}")"

echo "virgl-kms-validate: running direct KMS virgl demo" | tee -a "${LOG}"
if ! expect >>"${LOG}" 2>&1 <<EOF; then
set timeout 120
match_max 2000000
proc wait_prompt {} {
    set saved_timeout \$::timeout
    set ::timeout 45
    expect {
        -re {root:/# ?} { }
        timeout {
            send "\r"
            expect -re {root:/# ?}
        }
    }
    set ::timeout \$saved_timeout
}
set env(DISPLAY_MODE) "${MODE}"
set env(USE_KVM) "${USE_KVM:-1}"
set env(QEMU_GPU) "${QEMU_GPU:-virtio-vga-gl-primary}"
set env(QEMU_INPUT) "${QEMU_INPUT:-virtio}"
set env(QEMU_NET) "${QEMU_NET:-0}"
set env(QEMU_VIRTIO_GPU_XRES) "${XRES}"
set env(QEMU_VIRTIO_GPU_YRES) "${YRES}"
set env(QEMU_ALLOW_WSL_SDL_GL) "${QEMU_ALLOW_WSL_SDL_GL:-1}"
set env(QEMU_APPEND) "root=/dev/disk0 netsurf=0 webkit=0 glsmoke=0 desktop=0 video=${XRES}x${YRES} ${TOKEN}"
set env(QEMU_EXTRA) "-monitor unix:${MONITOR_SOCK},server,nowait ${QEMU_EXTRA:-}"
spawn timeout --foreground ${TIMEOUT} bash scripts/launch/launch-gui.sh
wait_prompt
send "GALLIUM_DRIVER=virgl MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu XV6_GBM_DEBUG=1 mesakmsgl --seconds=${DURATION_SECONDS}\r"
expect -re {mesakmsgl: kms connector=[0-9]+ crtc=[0-9]+ mode=${XRES}x${YRES}@60}
expect -re {xv6-mesa: gbm_dri_bo_create image bo handle=[0-9]+ .* has_export=1}
expect -re {mesakmsgl: GL vendor=.* renderer=virgl .* version=}
after 1500
catch { exec sh -c "printf 'screendump ${SCREENSHOT}\\r\\n' | nc -U -w 2 -N ${MONITOR_SOCK} >/dev/null 2>&1 || true" }
expect -re {mesakmsgl: [0-9.]+ FPS frames=[1-9][0-9]*}
expect -re {root:/# ?}
send "fbstat ppm-current ${GUEST_SCREENSHOT} 0 0 ${XRES} ${YRES}; echo __VIRGL_KMS_FB_CAPTURED__\r"
expect -re {__VIRGL_KMS_FB_CAPTURED__}
expect -re {root:/# ?}
exit 0
EOF
    fail "direct KMS virgl VM run failed"
fi

cleanup_qemu
trap - EXIT

require_log "mode=${XRES}x${YRES}@60" "KMS mode"
require_log 'has_export=1' "DRM PRIME export/import capability"
require_log 'renderer=virgl' "virgl renderer"
require_log 'D3D12 \(NVIDIA' "host D3D12/NVIDIA virgl renderer"
require_log 'mesakmsgl: [0-9.]+ FPS frames=' "FPS samples"
reject_log 'panic|fatal page fault|SIGABRT|coredump: generating' \
    "kernel/userspace crash marker"
reject_log 'virtio_gpu: command .* timed out' "virtio-gpu timeout"

if [[ ! -s "${SCREENSHOT}" ]] && ! extract_guest_screenshot; then
    if [[ "${VIRGL_KMS_VALIDATE_REQUIRE_SCREENSHOT:-0}" == "1" ]]; then
        fail "screenshot was not captured"
    fi
    echo "virgl-kms-validate: framebuffer screenshot unavailable (${SCREENSHOT})" | tee -a "${LOG}"
fi

if [[ -s "${SCREENSHOT}" ]]; then
    validate_screenshot || fail "framebuffer screenshot validation failed"
fi

echo "virgl-kms-validate: PASS (${LOG})"
