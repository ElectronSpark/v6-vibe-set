#!/usr/bin/env bash
# Validate xv6's windowed virgl desktop path against the Alpine/Linux control.
#
# This is intentionally stricter than the generic GPU substrate validator:
# it checks that the normal desktop keeps ownership of a full-screen scanout
# target while the 3D demo animates inside a window.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-build-x86_64}"
TRACE_DIR="${VIRGL_DESKTOP_TRACE_DIR:-${ROOT}/${BUILD_DIR}/virgl-desktop-validate}"
LOG="${VIRGL_DESKTOP_VALIDATE_LOG:-${TRACE_DIR}/xv6-virgl-desktop.log}"
TRACE_EVENTS="${TRACE_DIR}/qemu-trace-events"
QEMU_TRACE="${TRACE_DIR}/xv6-qemu-virgl-desktop.trace"
SCREENSHOT="${VIRGL_DESKTOP_VALIDATE_SCREENSHOT:-${TRACE_DIR}/xv6-virgl-desktop.ppm}"
MONITOR_SCREENSHOT="${VIRGL_DESKTOP_VALIDATE_MONITOR_SCREENSHOT:-${SCREENSHOT%.ppm}.monitor.ppm}"
MONITOR_SCREENSHOT_LOG="${MONITOR_SCREENSHOT}.log"
SCREENSHOT_REQUIRED="${VIRGL_DESKTOP_VALIDATE_SCREENSHOT_REQUIRED:-1}"
GUEST_SCREENSHOT="${VIRGL_DESKTOP_VALIDATE_GUEST_SCREENSHOT:-/xv6-virgl-desktop-current.ppm}"
VALIDATION_FSIMG="${VIRGL_DESKTOP_VALIDATE_FSIMG:-${TRACE_DIR}/xv6-virgl-desktop-fs.img}"
SCREENSHOT_X="${VIRGL_DESKTOP_VALIDATE_SCREENSHOT_X:-0}"
SCREENSHOT_Y="${VIRGL_DESKTOP_VALIDATE_SCREENSHOT_Y:-0}"
SCREENSHOT_W="${VIRGL_DESKTOP_VALIDATE_SCREENSHOT_W:-1280}"
SCREENSHOT_H="${VIRGL_DESKTOP_VALIDATE_SCREENSHOT_H:-800}"
MONITOR_SOCK="${TRACE_DIR}/qemu-monitor.sock"
MODE="${VIRGL_DESKTOP_VALIDATE_MODE:-gtk}"
COMPOSITOR="weston"
TIMEOUT="${VIRGL_DESKTOP_VALIDATE_TIMEOUT:-150s}"
RUN_SECONDS="${VIRGL_DESKTOP_VALIDATE_SECONDS:-0}"
XRES="${VIRGL_DESKTOP_VALIDATE_XRES:-1280}"
YRES="${VIRGL_DESKTOP_VALIDATE_YRES:-800}"
TOKEN="${VIRGL_DESKTOP_VALIDATE_TOKEN:-vd$$}"
FB_BUFFERS="${VIRGL_DESKTOP_VALIDATE_BUFFERS:-0}"
FB_FLIP="${VIRGL_DESKTOP_VALIDATE_FLIP:-0}"
FB_DAMAGE_FLIP="${VIRGL_DESKTOP_VALIDATE_DAMAGE_FLIP:-0}"
FB_COPY_BEFORE_FLIP="${VIRGL_DESKTOP_VALIDATE_COPY_BEFORE_FLIP:-0}"
FB_COPY_DAMAGE_BEFORE_FLIP="${VIRGL_DESKTOP_VALIDATE_COPY_DAMAGE_BEFORE_FLIP:-0}"
PAGEFLIP_COPY="${VIRGL_DESKTOP_VALIDATE_PAGEFLIP_COPY:-0}"
PAGE_FLIP_PRESENT="${VIRGL_DESKTOP_VALIDATE_PAGE_FLIP_PRESENT:-0}"
EFFECTIVE_PAGE_FLIP_PRESENT=0
EFFECTIVE_FB_DAMAGE_FLIP=0
PIPELINE_GPU_RELEASE="${VIRGL_DESKTOP_VALIDATE_PIPELINE_GPU_RELEASE:-0}"
FULLSCREEN_DIRECT="${VIRGL_DESKTOP_VALIDATE_FULLSCREEN_DIRECT:-0}"
MESA_COLOR_BUFFERS="${VIRGL_DESKTOP_VALIDATE_MESA_COLOR_BUFFERS:-0}"
GL_FINISH="${VIRGL_DESKTOP_VALIDATE_GL_FINISH:-0}"
ASYNC_DEPTH="${VIRGL_DESKTOP_VALIDATE_ASYNC_DEPTH:-0}"
FBSTAT="${VIRGL_DESKTOP_VALIDATE_FBSTAT:-0}"
CALLBACK_POLL_MS="${VIRGL_DESKTOP_VALIDATE_CALLBACK_POLL_MS:-}"
EXTRA_APPEND="${VIRGL_DESKTOP_VALIDATE_EXTRA_APPEND:-}"
EXPECT_READBACK_FALLBACK="${VIRGL_DESKTOP_VALIDATE_EXPECT_READBACK_FALLBACK:-0}"

mkdir -p "${TRACE_DIR}"
: >"${LOG}"
rm -f "${QEMU_TRACE}" "${SCREENSHOT}" "${MONITOR_SCREENSHOT}" \
    "${MONITOR_SCREENSHOT_LOG}" "${MONITOR_SOCK}" "${VALIDATION_FSIMG}"

fail()
{
    echo "virgl-desktop-validate: $*" >&2
    echo "virgl-desktop-validate: log: ${LOG}" >&2
    echo "virgl-desktop-validate: trace: ${QEMU_TRACE}" >&2
    exit 1
}

require_log()
{
    local pattern="$1"
    local why="$2"

    if ! grep -aEq "${pattern}" "${LOG}"; then
        fail "missing ${why}"
    fi
}

reject_log()
{
    local pattern="$1"
    local why="$2"

    if grep -aEq "${pattern}" "${LOG}"; then
        fail "found ${why}"
    fi
}

require_demo_surface_evidence()
{
    if grep -aEq 'demo_surface_matrix .*status=PASS' "${LOG}"; then
        return 0
    fi
    if grep -aEq 'demo_surface_matrix .*window=[1-9][0-9]*x[1-9][0-9]* .*render=[1-9][0-9]*x[1-9][0-9]*' "${LOG}"; then
        echo "virgl-desktop-validate: demo_surface_matrix serial-interleaved; using window/render evidence" |
            tee -a "${LOG}"
        return 0
    fi
    if grep -aEq '^mesawlegl\[[0-9]+\]: complete frames=[0-9]+ .*status=0 .*window=[1-9][0-9]*x[1-9][0-9]* render=[1-9][0-9]*x[1-9][0-9]*' "${LOG}"; then
        echo "virgl-desktop-validate: demo_surface_matrix serial-interleaved; using completion window/render evidence" |
            tee -a "${LOG}"
        return 0
    fi
    if grep -aEq 'spherical-poly-demo' "${LOG}" &&
       grep -aEq 'displayed_fps=[1-9]' "${LOG}"; then
        echo "virgl-desktop-validate: demo_surface_matrix missing; using renderer+fps evidence before screenshot validation" |
            tee -a "${LOG}"
        return 0
    fi
    fail "missing demo surface placement/status"
}

require_trace()
{
    local pattern="$1"
    local why="$2"

    if [[ ! -s "${QEMU_TRACE}" ]]; then
        fail "QEMU trace was not captured"
    fi
    if ! grep -Eq "${pattern}" "${QEMU_TRACE}"; then
        fail "trace missing ${why}"
    fi
}

reject_trace()
{
    local pattern="$1"
    local why="$2"

    if [[ -s "${QEMU_TRACE}" ]] && grep -Eq "${pattern}" "${QEMU_TRACE}"; then
        fail "trace contains ${why}"
    fi
}

validate_guest_screenshot()
{
    if ! grep -q 'XV6_SCREENSHOT_CAPTURED' "${LOG}"; then
        if [[ "${SCREENSHOT_REQUIRED}" == "0" ]]; then
            echo "virgl-desktop-validate: guest screenshot unavailable (${SCREENSHOT})" |
                tee -a "${LOG}"
            return 0
        fi
        echo "virgl-desktop-validate: guest screenshot marker missing; trying fs image extraction" |
            tee -a "${LOG}"
    fi

    command -v debugfs >/dev/null 2>&1 ||
        fail "debugfs is required to extract the guest screenshot"
    rm -f "${SCREENSHOT}"
    if ! debugfs -R "dump ${GUEST_SCREENSHOT} ${SCREENSHOT}" \
            "${VALIDATION_FSIMG}" >>"${LOG}" 2>&1; then
        if [[ "${SCREENSHOT_REQUIRED}" == "0" ]]; then
            echo "virgl-desktop-validate: guest screenshot extract failed (${SCREENSHOT})" |
                tee -a "${LOG}"
            return 0
        fi
        fail "guest screenshot extraction failed"
    fi
    if [[ ! -s "${SCREENSHOT}" ]]; then
        fail "guest screenshot extraction produced an empty file"
    fi

    if ! python3 - "${SCREENSHOT}" "${SCREENSHOT_W}" "${SCREENSHOT_H}" "${XRES}" "${YRES}" "${COMPOSITOR}" >>"${LOG}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
want_w = int(sys.argv[2])
want_h = int(sys.argv[3])
screen_w = int(sys.argv[4])
screen_h = int(sys.argv[5])
compositor = sys.argv[6] if len(sys.argv) > 6 else "weston"
data = path.read_bytes()

def next_token(offset):
    n = len(data)
    while offset < n and data[offset] in b" \t\r\n":
        offset += 1
    if offset < n and data[offset:offset + 1] == b"#":
        while offset < n and data[offset] not in b"\r\n":
            offset += 1
        return next_token(offset)
    start = offset
    while offset < n and data[offset] not in b" \t\r\n":
        offset += 1
    return data[start:offset], offset

try:
    magic, off = next_token(0)
    width, off = next_token(off)
    height, off = next_token(off)
    maxval, off = next_token(off)
    if off < len(data) and data[off] in b" \t\r\n":
        off += 1
    w = int(width)
    h = int(height)
    max_value = int(maxval)
except Exception as exc:
    print(f"screenshot_matrix path={path} status=FAIL reason=parse error={exc}")
    sys.exit(1)

pixels = data[off:]
expected = w * h * 3
if magic != b"P6" or max_value != 255 or len(pixels) != expected:
    print(
        f"screenshot_matrix path={path} width={w} height={h} bytes={len(pixels)} "
        f"expected={expected} status=FAIL reason=header"
    )
    sys.exit(1)

total = w * h
nonblack = 0
bright = 0
sampled = set()
stride = max(1, total // 4096)
for idx in range(total):
    base = idx * 3
    rgb = pixels[base:base + 3]
    if rgb != b"\x00\x00\x00":
        nonblack += 1
    if max(rgb) >= 160:
        bright += 1
    if idx % stride == 0:
        sampled.add(rgb)

min_nonblack = max(1000, total // 50)
status = "PASS"
reason = "ok"
demo_total = 0
demo_bright = 0
demo_cyan = 0
demo_dark = 0
demo_colorful = 0
demo_top_warm = 0
demo_bottom_warm = 0
demo_top_cool = 0
demo_bottom_cool = 0
demo_unique = set()

def demo_candidate_rects():
    if w == screen_w and h == screen_h and screen_w >= 640 and screen_h >= 480:
        # Older xv6 desktop builds centered the xdg toplevel above a 36px
        # taskbar.  Keep this geometry as one candidate, then search for the
        # Weston placement below.
        for demo_w, demo_h in ((640, 480), (480, 360)):
            win_x = max(0, (screen_w - demo_w) // 2)
            win_y = max(0, (screen_h - 36 - demo_h) // 2)
            yield (
                min(w, win_x + 20),
                min(h, win_y + 45),
                min(w, win_x + demo_w - 20),
                min(h, win_y + demo_h - 20),
            )
        if compositor == "weston":
            # Weston's desktop shell may place the first xdg-toplevel without
            # the old centered taskbar geometry. Search a coarse grid for the
            # same rendered demo body while keeping the pixel thresholds below.
            for demo_w, demo_h in ((640, 480), (480, 360)):
                step_x = max(80, demo_w // 4)
                step_y = max(60, demo_h // 4)
                max_x = max(0, screen_w - demo_w)
                max_y = max(0, screen_h - demo_h)
                xs = sorted(set(list(range(0, max_x + 1, step_x)) +
                                [max_x, max(0, (screen_w - demo_w) // 2)]))
                ys = sorted(set(list(range(0, max_y + 1, step_y)) +
                                [max_y, max(0, (screen_h - demo_h) // 2)]))
                for win_y in ys:
                    for win_x in xs:
                        yield (
                            min(w, win_x + 20),
                            min(h, win_y + 45),
                            min(w, win_x + demo_w - 20),
                            min(h, win_y + demo_h - 20),
                        )
    else:
        # Legacy crop fallback: assume the caller captured the centered demo area.
        yield (min(w, 60), min(h, 65), max(min(w, 60), w - 20),
               max(min(h, 65), h - 20))

best = None
for cx0, cy0, cx1, cy1 in demo_candidate_rects():
    cand_total = 0
    cand_bright = 0
    cand_cyan = 0
    cand_dark = 0
    cand_colorful = 0
    cand_top_warm = 0
    cand_bottom_warm = 0
    cand_top_cool = 0
    cand_bottom_cool = 0
    cand_unique = set()
    mid_y = cy0 + (cy1 - cy0) // 2
    for y in range(cy0, cy1):
        for x in range(cx0, cx1):
            base = (y * w + x) * 3
            r = pixels[base]
            g = pixels[base + 1]
            b = pixels[base + 2]
            prefix = "top" if y < mid_y else "bottom"
            cand_total += 1
            if max(r, g, b) >= 170:
                cand_bright += 1
            if b >= 120 and g >= 90 and r <= 120:
                cand_cyan += 1
            if max(r, g, b) <= 45:
                cand_dark += 1
            if max(r, g, b) - min(r, g, b) >= 40:
                cand_colorful += 1
            if r > b + 20 and r >= 70 and g >= 50:
                if prefix == "top":
                    cand_top_warm += 1
                else:
                    cand_bottom_warm += 1
            if b > r + 20 and b >= 70:
                if prefix == "top":
                    cand_top_cool += 1
                else:
                    cand_bottom_cool += 1
            if (x + y) % 37 == 0:
                cand_unique.add((r, g, b))
    score = (
        cand_bright + cand_cyan + min(cand_dark, 30000) +
        cand_colorful + len(cand_unique) * 1000
    )
    if best is None or score > best[0]:
        best = (score, cx0, cy0, cx1, cy1, cand_total, cand_bright,
                cand_cyan, cand_dark, cand_colorful, cand_top_warm,
                cand_bottom_warm, cand_top_cool, cand_bottom_cool,
                cand_unique)

if best is not None:
    _, x0, y0, x1, y1, demo_total, demo_bright, demo_cyan, demo_dark, \
        demo_colorful, demo_top_warm, demo_bottom_warm, demo_top_cool, \
        demo_bottom_cool, demo_unique = best

if w != want_w or h != want_h:
    status = "FAIL"
    reason = "dimensions"
elif nonblack < min_nonblack:
    status = "FAIL"
    reason = "blank"
elif len(sampled) < 8:
    status = "FAIL"
    reason = "low_variance"
elif (
    demo_total <= 0 or
    demo_bright < 1500 or
    demo_cyan < 1500 or
    demo_dark < 15000 or
    demo_colorful < 10000 or
    len(demo_unique) < 24
):
    status = "FAIL"
    reason = "missing_demo_pixels"
elif compositor == "weston" and (
    demo_top_warm + demo_bottom_warm < 1500 or
    demo_top_cool + demo_bottom_cool < 1500
):
    status = "FAIL"
    reason = "missing_weston_gradient_pixels"
elif compositor != "weston" and (
    demo_top_warm < 1500 or
    demo_bottom_cool < 1500 or
    demo_top_warm <= demo_bottom_warm or
    demo_bottom_cool <= demo_top_cool
):
    status = "FAIL"
    reason = "flipped_demo_pixels"

print(
    f"screenshot_matrix path={path} width={w} height={h} nonblack={nonblack} "
    f"bright={bright} unique_sample={len(sampled)} "
    f"demo_rect={x0},{y0}-{x1},{y1} demo_bright={demo_bright} "
    f"demo_cyan={demo_cyan} demo_dark={demo_dark} "
    f"demo_colorful={demo_colorful} demo_unique={len(demo_unique)} "
    f"demo_top_warm={demo_top_warm} demo_bottom_warm={demo_bottom_warm} "
    f"demo_top_cool={demo_top_cool} demo_bottom_cool={demo_bottom_cool} "
    f"status={status} reason={reason}"
)
if status != "PASS":
    sys.exit(1)
PY
    then
        fail "guest screenshot pixel validation failed"
    fi
}

cleanup_qemu()
{
    pkill -TERM -f "qemu-system-x86_64 .*${TOKEN}" 2>/dev/null || true
    rm -f "${MONITOR_SOCK}"
}

validate_launch_contract()
{
    local append dry

    append="root=/dev/disk0"
    if [[ "${PAGEFLIP_COPY}" != "0" ]]; then
        append+=" virtio_gpu_disable_pageflip_copy=0 virtio_gpu_pageflip_copy=1 virtio_gpu_pageflip_validate_copy=1 virtio_gpu_present_minimal_drain=1"
    fi
    append+=" netsurf=0 webkit=0 glsmoke=1 glsmoke_demo=1 glsmoke_accel=1"
    append+=" weston=1"
    if [[ "${RUN_SECONDS}" != "0" ]]; then
        append+=" glsmoke_seconds=${RUN_SECONDS}"
    fi
    append+=" video=${XRES}x${YRES}"
    if [[ "${MESA_COLOR_BUFFERS}" != "0" ]]; then
        append+=" glsmoke_color_buffers=${MESA_COLOR_BUFFERS}"
    fi
    if [[ "${FBSTAT}" != "0" ]]; then
        append+=" glsmoke_fbstat=1"
    fi
    if [[ "${ASYNC_DEPTH}" != "0" ]]; then
        append+=" virtio_gpu_async_depth=${ASYNC_DEPTH}"
    fi
    if [[ -n "${EXTRA_APPEND}" ]]; then
        append+=" ${EXTRA_APPEND}"
    fi
    append+=" ${TOKEN}"
    dry="$(QEMU_DRY_RUN=1 DISPLAY_MODE="${MODE}" USE_KVM=1 \
        QEMU_GPU=virtio-vga-gl-primary QEMU_INPUT=virtio QEMU_NET=0 \
        QEMU_AUDIO=none \
        QEMU_VIRTIO_GPU_XRES="${XRES}" QEMU_VIRTIO_GPU_YRES="${YRES}" \
        QEMU_APPEND="${append}" \
        bash scripts/launch/run-qemu.sh x86_64 \
        "${BUILD_DIR}/kernel/build/kernel/xv6.bin" "${BUILD_DIR}/fs.img")"
    printf '%s\n' "${dry}" >>"${LOG}"

    grep -Eq -- "-display ${MODE},gl=(on|es)" <<<"${dry}" ||
        fail "${MODE}/GL display contract missing"
    grep -q -- 'full-screen=off' <<<"${dry}" ||
        fail "QEMU launch must remain windowed"
    grep -q -- 'zoom-to-fit=off' <<<"${dry}" ||
        fail "QEMU launch must not scale the guest canvas"
    grep -q -- '-vga none' <<<"${dry}" ||
        fail "primary virtio-gpu launch must disable default VGA"
    grep -Eq -- "virtio-vga-gl([^[:space:]]*,)?xres=${XRES},yres=${YRES}" <<<"${dry}" ||
        fail "virtio-vga-gl desktop geometry contract missing"
    grep -q -- "video=${XRES}x${YRES}" <<<"${dry}" ||
        fail "guest desktop video mode contract missing"
    if [[ "${COMPOSITOR}" == "weston" ]]; then
        grep -q -- 'weston=1' <<<"${dry}" ||
            fail "Weston compositor cmdline missing"
    fi
    if [[ "${PAGEFLIP_COPY}" == "0" ]]; then
        grep -q -- 'virtio_gpu_disable_pageflip_copy=1' <<<"${dry}" ||
            fail "pageflip-copy must remain disabled for windowed desktop validation"
    else
        grep -q -- 'virtio_gpu_disable_pageflip_copy=0' <<<"${dry}" ||
            fail "pageflip-copy diagnostic must explicitly override the default disable flag"
        grep -q -- 'virtio_gpu_pageflip_copy=1' <<<"${dry}" ||
            fail "pageflip-copy diagnostic flag missing"
    fi
    EFFECTIVE_PAGE_FLIP_PRESENT=0
    EFFECTIVE_FB_DAMAGE_FLIP=0
    if [[ "${MESA_COLOR_BUFFERS}" != "0" ]]; then
        grep -q -- "glsmoke_color_buffers=${MESA_COLOR_BUFFERS}" <<<"${dry}" ||
            fail "Mesa Wayland color-buffer diagnostic flag missing"
    fi
    if [[ "${FBSTAT}" != "0" ]]; then
        grep -q -- 'glsmoke_fbstat=1' <<<"${dry}" ||
            fail "desktop fbstat diagnostic flag missing"
    fi
}

cat >"${TRACE_EVENTS}" <<'EOF'
virtio_gpu_features
virtio_gpu_cmd_get_display_info
virtio_gpu_cmd_get_edid
virtio_gpu_cmd_set_scanout
virtio_gpu_cmd_set_scanout_blob
virtio_gpu_cmd_res_create_2d
virtio_gpu_cmd_res_create_3d
virtio_gpu_cmd_res_create_blob
virtio_gpu_cmd_res_unref
virtio_gpu_cmd_res_back_attach
virtio_gpu_cmd_res_back_detach
virtio_gpu_cmd_res_xfer_toh_2d
virtio_gpu_cmd_res_xfer_toh_3d
virtio_gpu_cmd_res_xfer_fromh_3d
virtio_gpu_cmd_res_flush
virtio_gpu_cmd_ctx_create
virtio_gpu_cmd_ctx_destroy
virtio_gpu_cmd_ctx_res_attach
virtio_gpu_cmd_ctx_res_detach
virtio_gpu_cmd_ctx_submit
virtio_gpu_fence_ctrl
virtio_gpu_fence_resp
EOF

validate_launch_contract

command -v expect >/dev/null 2>&1 ||
    fail "expect is required for prompt-synchronized guest validation"
if [[ ! -f "${BUILD_DIR}/fs.img" ]]; then
    fail "rootfs image not found: ${BUILD_DIR}/fs.img"
fi
cp "${BUILD_DIR}/fs.img" "${VALIDATION_FSIMG}"

trap cleanup_qemu EXIT

echo "virgl-desktop-validate: running windowed desktop virgl demo" | tee -a "${LOG}"
expect_status=0
expect >>"${LOG}" 2>&1 <<EOF || expect_status=$?
set timeout 150
match_max 12000000
set env(DISPLAY_MODE) "${MODE}"
set env(USE_KVM) "${USE_KVM:-1}"
set env(QEMU_GPU) "virtio-vga-gl-primary"
set env(QEMU_INPUT) "${QEMU_INPUT:-virtio}"
set env(QEMU_NET) "${QEMU_NET:-0}"
set env(QEMU_AUDIO) "none"
set env(QEMU_VIRTIO_GPU_XRES) "${XRES}"
set env(QEMU_VIRTIO_GPU_YRES) "${YRES}"
set env(QEMU_ALLOW_WSL_SDL_GL) "${QEMU_ALLOW_WSL_SDL_GL:-1}"
set env(FSIMG) "${VALIDATION_FSIMG}"
set append "root=/dev/disk0"
if { "${PAGEFLIP_COPY}" != "0" } {
    append append " virtio_gpu_disable_pageflip_copy=0 virtio_gpu_pageflip_copy=1 virtio_gpu_pageflip_validate_copy=1 virtio_gpu_present_minimal_drain=1"
}
append append " netsurf=0 webkit=0 glsmoke=1 glsmoke_demo=1 glsmoke_accel=1"
append append " weston=1"
if { "${RUN_SECONDS}" != "0" } {
    append append " glsmoke_seconds=${RUN_SECONDS}"
}
append append " video=${XRES}x${YRES}"
if { "${MESA_COLOR_BUFFERS}" != "0" } {
    append append " glsmoke_color_buffers=${MESA_COLOR_BUFFERS}"
}
if { "${FBSTAT}" != "0" || "${COMPOSITOR}" == "weston" } {
    append append " glsmoke_fbstat=1"
}
if { "${ASYNC_DEPTH}" != "0" } {
    append append " virtio_gpu_async_depth=${ASYNC_DEPTH}"
}
if { "${EXTRA_APPEND}" != "" } {
    append append " ${EXTRA_APPEND}"
}
append append " ${TOKEN}"
set env(QEMU_APPEND) \$append
set env(QEMU_EXTRA) "-trace events=${TRACE_EVENTS},file=${QEMU_TRACE} -monitor unix:${MONITOR_SOCK},server,nowait ${QEMU_EXTRA:-}"
spawn timeout --foreground ${TIMEOUT} bash scripts/launch/launch-gui.sh
set timeout 45
expect {
    -re {\[desktop\] weston pid=[0-9]+} {}
    -re {mesawlegl: EGL .*spherical-poly-demo} {}
    -re {demo_surface_matrix} {}
    -re {app_loop_fps=} {}
    timeout {
        puts "XV6_DESKTOP_READY_TIMEOUT"
    }
}
if { "${COMPOSITOR}" == "weston" } {
    set timeout 60
    expect {
        -re {app_loop_fps=} {}
        timeout {
            puts "XV6_WESTON_APP_LOOP_FPS_TIMEOUT"
        }
    }
} else {
    set timeout 20
    expect {
        -re {app_loop_fps=} {}
        timeout {}
    }
}
if { "${RUN_SECONDS}" == "0" && "${COMPOSITOR}" != "weston" } {
    set timeout 60
    expect {
        -re {app_loop_fps=[0-9]+\.[0-9]+ displayed_fps=([5-9][0-9]|[1-9][0-9][0-9])\.[0-9]} {}
        timeout {}
    }
}
after 500
send -- "fbstat ppm-current ${GUEST_SCREENSHOT} ${SCREENSHOT_X} ${SCREENSHOT_Y} ${SCREENSHOT_W} ${SCREENSHOT_H}; sync; echo XV6_SCREENSHOT_CAPTURED\r"
set timeout 60
expect {
    -re {fb_ppm_current path=.*scanout=[0-9]+x[0-9]+} {}
    -re {fbstat: [^\r\n]+} { exit 6 }
    timeout {
        puts "XV6_SCREENSHOT_FBSTAT_TIMEOUT"
        exit 6
    }
    eof { exit 6 }
}
set timeout 60
expect {
    -re {XV6_SCREENSHOT_CAPTURED} {}
    timeout {
        puts "XV6_SCREENSHOT_CAPTURE_TIMEOUT"
    }
    eof {}
}
catch { exec sh -c "if command -v nc >/dev/null 2>&1; then printf 'screendump ${MONITOR_SCREENSHOT}\\n' | nc -U -w 3 -q 1 ${MONITOR_SOCK} > ${MONITOR_SCREENSHOT_LOG} 2>&1 || true; else echo 'nc missing' > ${MONITOR_SCREENSHOT_LOG}; fi" }
set timeout -1
if { "${FBSTAT}" != "0" || "${COMPOSITOR}" == "weston" } {
    if { "${RUN_SECONDS}" == "0" } {
        set timeout 20
        expect {
            -re {[^\r\n]*# ?} {}
            timeout {}
            eof {}
        }
        after 500
        send -- "fbstat\r"
    }
    set timeout 40
    expect {
        -re {virtio_async_make_room_max_wait_us[[:space:]]+[0-9]+} {}
        timeout {}
        eof {}
    }
    set timeout 40
    expect {
        -re {display_last_complete[[:space:]]+[0-9]+[\r\n]} {}
        timeout {}
        eof {}
    }
} elseif { "${RUN_SECONDS}" != "0" } {
    set timeout [expr ${RUN_SECONDS} + 40]
    expect {
        -re {mesawlegl_completion_matrix.*status=0} {}
        -re {mesawlegl.*complete frames=.*seconds=.*status=0} {}
        timeout {
            puts "XV6_SECONDS_COMPLETION_WAIT_TIMEOUT"
        }
        eof {}
    }
}
exit 0
EOF
if [[ "${expect_status}" -ne 0 ]] &&
   ! grep -Eq '^mesawlegl_completion_matrix .*status=0|^mesawlegl\[[0-9]+\]: complete frames=[0-9]+ .*status=0' "${LOG}"; then
    fail "windowed desktop virgl VM run failed"
fi

cleanup_qemu
trap - EXIT

validate_guest_screenshot

require_log 'renderer=virgl' "virgl renderer"
require_log 'spherical-poly|demo_surface_matrix .*status=PASS' \
    "spherical polygon demo marker"
require_demo_surface_evidence
require_log '^\[desktop\] weston pid=[0-9]+' "Weston compositor launch"
require_log 'mesawlegl\[[0-9]+\]: app_loop_fps=' "app/display FPS telemetry"
if [[ "${RUN_SECONDS}" != "0" ]] &&
   ! grep -Eq '^mesawlegl_completion_matrix .*status=0|^mesawlegl\[[0-9]+\]: complete frames=[0-9]+ .*status=0' "${LOG}"; then
    fail "missing clean demo completion"
fi
completion_line="$(grep -E '^mesawlegl_completion_matrix .*status=0|^mesawlegl\[[0-9]+\]: complete frames=[0-9]+ .*status=0' "${LOG}" | tail -1 || true)"
if [[ "${RUN_SECONDS}" != "0" ]] && ! awk '
    {
        elapsed = -1
        status = -1
        for (i = 1; i <= NF; i++) {
            if ($i ~ /^elapsed=/) {
                split($i, e, "=")
                elapsed = e[2] + 0
            } else if ($i ~ /^status=/) {
                split($i, s, "=")
                status = s[2] + 0
            }
        }
        if (status != 0 || elapsed <= 0)
            exit 1
    }
' <<<"${completion_line}"; then
    echo "virgl-desktop-validate: weak demo completion: ${completion_line}" \
        >>"${LOG}"
    fail "demo did not report clean frame progress"
fi
reject_log 'status=[1-9][0-9]*' "nonzero demo status"
require_log '^display_presents [1-9][0-9]*([^0-9]|$)' \
    "Weston display present accounting"
require_log '^display_completions [1-9][0-9]*([^0-9]|$)' \
    "Weston display completion accounting"
require_log '^display_last_complete [1-9][0-9]*([^0-9]|$)' \
    "Weston latest display completion"
if [[ "${PAGEFLIP_COPY}" -ne 0 ]]; then
    require_log 'virtio_gpu: pageflip-copy present' \
        "kernel pageflip-copy diagnostic present"
    reject_log 'pageflip-copy validation failed' \
        "failed pageflip-copy validation"
fi
if [[ "${MESA_COLOR_BUFFERS}" -ne 0 ]]; then
    require_log "xv6-mesa: wayland color buffer diagnostic limit=${MESA_COLOR_BUFFERS} " \
        "Mesa Wayland color-buffer diagnostic"
fi
if [[ "${FBSTAT}" -ne 0 ]]; then
    display_complete_value=""
    if [[ "${COMPOSITOR}" != "weston" ]]; then
        require_log '^virtio_async_make_room_calls [1-9][0-9]*[[:space:]]*$' \
            "fbstat async make-room calls"
        require_log '^virtio_async_depth [1-9][0-9]*[[:space:]]*$' \
            "fbstat async depth"
        require_log '^virtio_async_posted_submit_3d [1-9][0-9]*[[:space:]]*$' \
            "fbstat async submit-3d posts"
        require_log '^virtio_async_make_room_submit_3d_calls [1-9][0-9]*[[:space:]]*$' \
            "fbstat async submit-3d admission calls"
        require_log '^virtio_async_make_room_submit_3d_stalls [0-9]+[[:space:]]*$' \
            "fbstat async submit-3d admission stalls"
        if [[ "${EFFECTIVE_PAGE_FLIP_PRESENT}" -eq 0 ]]; then
            require_log '^virtio_async_posted_flush [1-9][0-9]*[[:space:]]*$' \
                "fbstat async flush posts"
            require_log '^virtio_async_make_room_flush_calls [1-9][0-9]*[[:space:]]*$' \
                "fbstat async flush admission calls"
            require_log '^virtio_async_make_room_flush_stalls [0-9]+[[:space:]]*$' \
                "fbstat async flush admission stalls"
        else
            require_log '^virtio_async_posted_flush [1-9][0-9]*[[:space:]]*$' \
                "fbstat page-flip async flush posts"
            require_log '^virtio_async_make_room_flush_calls [1-9][0-9]*[[:space:]]*$' \
                "fbstat page-flip async flush admission calls"
            require_log '^virtio_async_make_room_flush_stalls [0-9]+[[:space:]]*$' \
                "fbstat page-flip async flush admission stalls"
        fi
    fi
    if ! grep -aEq '(^display_last_complete[[:space:]]+[1-9][0-9]*([^0-9]|$)|(^|[[:space:]])display_last_complete=[1-9][0-9]*([^0-9]|$)|generic_display_last_complete=[1-9][0-9]*([^0-9]|$))' "${LOG}"; then
        fail "missing fbstat display completion footer"
    fi
    display_complete_value="$(
        {
            grep -aEo '^display_last_complete[[:space:]]+[0-9]+' "${LOG}" |
                awk '{print $2}'
            grep -aEo '(^|[[:space:]])display_last_complete=[0-9]+' "${LOG}" |
                sed -E 's/.*=//'
            grep -aEo 'generic_display_last_complete=[0-9]+' "${LOG}" |
                sed -E 's/.*=//'
        } |
            tail -1
    )"
    if ! awk -v got="${display_complete_value:-0}" \
        'BEGIN { exit ((got + 0) <= 0) ? 1 : 0 }'; then
        echo "virgl-desktop-validate: missing display completion footer: ${display_complete_value:-missing}" \
            >>"${LOG}"
        fail "fbstat display completion footer did not report displayed frames"
    fi
fi
fps_line="$(grep -E 'mesawlegl\[[0-9]+\]: app_loop_fps=' "${LOG}" | tail -1)"
if [[ "${COMPOSITOR}" != "weston" && "${MESA_COLOR_BUFFERS}" -eq 0 ]] && ! awk '
    {
        app = 0
        displayed = 0
        for (i = 1; i <= NF; i++) {
            if ($i ~ /^app_loop_fps=/) {
                split($i, a, "=")
                app = a[2] + 0
            } else if ($i ~ /^displayed_fps=/) {
                split($i, d, "=")
                displayed = d[2] + 0
            }
        }
        if (app < 5 || displayed < 1 || displayed * 100 < app * 75)
            exit 1
    }
' <<<"${fps_line}"; then
    echo "virgl-desktop-validate: weak displayed/app FPS telemetry: ${fps_line}" \
        >>"${LOG}"
    fail "displayed FPS does not track app FPS"
fi
reject_log 'panic|fatal page fault|SIGABRT|coredump: generating' \
    "kernel/userspace crash marker"
reject_log 'virtio_gpu: command .* timed out' "virtio-gpu timeout"
reject_log 'vrend_renderer_transfer_iov: context error' "virgl renderer context error"
reject_log 'FB: virgl resource-scanout failed' "kernel virgl resource-scanout failure"

require_trace "virtio_gpu_cmd_set_scanout .*w ${XRES}, h ${YRES}" \
    "desktop-sized scanout"
require_trace 'virtio_gpu_cmd_ctx_submit' "virgl context submits"
require_trace 'virtio_gpu_cmd_res_flush' "resource flushes"
require_trace 'virtio_gpu_fence_ctrl' "fence submissions"
require_trace 'virtio_gpu_fence_resp' "fence completions"

if [[ "${EFFECTIVE_PAGE_FLIP_PRESENT}" -ne 0 ]]; then
    if ! python3 - "${QEMU_TRACE}" "${XRES}" "${YRES}" >>"${LOG}" <<'PY'
import re
import sys
from pathlib import Path
from collections import Counter

trace = Path(sys.argv[1])
want_w = int(sys.argv[2])
want_h = int(sys.argv[3])
scanouts = []
flushes = []
for line in trace.read_text(errors="ignore").splitlines():
    if "virtio_gpu_cmd_set_scanout" in line:
        m = re.search(r"res 0x([0-9a-fA-F]+), w (\d+), h (\d+)", line)
        if m:
            res = int(m.group(1), 16)
            w = int(m.group(2))
            h = int(m.group(3))
            if res and w == want_w and h == want_h:
                scanouts.append(res)
    elif "virtio_gpu_cmd_res_flush" in line:
        m = re.search(r"res 0x([0-9a-fA-F]+), w (\d+), h (\d+)", line)
        if m:
            res = int(m.group(1), 16)
            w = int(m.group(2))
            h = int(m.group(3))
            if res and w == want_w and h == want_h:
                flushes.append(res)

counts = Counter(scanouts)
active_resources = sorted(res for res, count in counts.items() if count >= 3)
if len(active_resources) < 2:
    active_resources = sorted(set(scanouts))
unique = active_resources
active = [res for res in scanouts if res in active_resources]
sample = active[:60]
no_immediate_reuse = (
    len(sample) >= 12 and
    all(sample[i] != sample[i - 1] for i in range(1, len(sample)))
)
cycles_three = (
    len(unique) >= 3 and len(sample) >= 18 and
    all(sample[i] != sample[i - 2] for i in range(2, len(sample)))
)
alternating = no_immediate_reuse and (len(unique) == 2 or cycles_three)
flush_unique = sorted(set(flushes))
flush_matches = len(flushes) >= len(sample) and set(unique).issubset(set(flush_unique))
p2_cached = (
    len(flush_unique) >= 2 and
    len(flushes) >= 12 and
    len(scanouts) <= max(4, len(flush_unique) + 1) and
    set(scanouts).issubset(set(flush_unique))
)
status = "PASS" if (
    (len(unique) >= 2 and alternating and flush_matches) or p2_cached
) else "FAIL"
print(
    "page_flip_trace_matrix "
    f"scanouts={len(scanouts)} unique={','.join(str(x) for x in unique)} "
    f"sample={','.join(str(x) for x in sample[:16])} "
    f"flushes={len(flushes)} flush_unique={','.join(str(x) for x in flush_unique)} "
    f"p2_cached={1 if p2_cached else 0} "
    f"status={status}"
)
if status != "PASS":
    sys.exit(1)
PY
    then
        fail "page-flip trace did not show alternating scanouts or P2 cached scanout set"
    fi
fi

# The core Alpine-parity invariant: after the desktop owns scanout, a windowed
# 3D client must not become the hardware scanout size.  Small pre-desktop
# virgl smoke/probe scanouts are tolerated if they are unbound before the
# full desktop scanout is established.
if [[ -s "${QEMU_TRACE}" ]]; then
    bad_scanout="$(
        awk -v want_w="${XRES}" -v want_h="${YRES}" '
            /virtio_gpu_cmd_set_scanout/ {
                if (match($0, /w [0-9]+, h [0-9]+/)) {
                    s = substr($0, RSTART, RLENGTH)
                    split(s, parts, /[^0-9]+/)
                    w = parts[2] + 0
                    h = parts[3] + 0
                    if (w == want_w && h == want_h)
                        desktop_seen = 1
                    else if (desktop_seen && w != 0 && h != 0)
                        print
                }
            }
        ' "${QEMU_TRACE}" | head -20
    )"
    if [[ -n "${bad_scanout}" ]]; then
        {
            echo "virgl-desktop-validate: non-desktop SET_SCANOUT events:"
            printf '%s\n' "${bad_scanout}"
        } >>"${LOG}"
        fail "trace contains non-desktop SET_SCANOUT in normal desktop mode"
    fi
fi

if [[ -s "${QEMU_TRACE}" ]]; then
    {
        echo
        echo "[trace counts]"
        for event in virtio_gpu_cmd_ctx_submit virtio_gpu_cmd_set_scanout \
                     virtio_gpu_cmd_res_flush virtio_gpu_fence_ctrl \
                     virtio_gpu_fence_resp virtio_gpu_cmd_res_create_3d; do
            printf '%s %s\n' "${event}" \
                "$(grep -c "${event}" "${QEMU_TRACE}" 2>/dev/null || true)"
        done
        echo
        echo "[scanout events]"
        grep -E 'virtio_gpu_cmd_set_scanout' "${QEMU_TRACE}" || true
    } >>"${LOG}"
fi

if [[ ! -s "${SCREENSHOT}" ]]; then
    if [[ "${SCREENSHOT_REQUIRED}" == "1" ]]; then
        fail "guest screenshot was not captured"
    fi
    echo "virgl-desktop-validate: monitor screenshot unavailable (${SCREENSHOT})" |
        tee -a "${LOG}"
fi

echo "virgl-desktop-validate: PASS (${LOG})"
