#!/usr/bin/env bash
# Validate the xv6 GPU substrate without relying on browser/toolkit behavior.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-build-x86_64}"
LOG="${GPU_VALIDATE_LOG:-${ROOT}/${BUILD_DIR}/gpu-validate.log}"
MODE="${GPU_VALIDATE_MODE:-gtk}"
TIMEOUT="${GPU_VALIDATE_TIMEOUT:-180s}"
GPU_VALIDATE_XRES="${GPU_VALIDATE_XRES:-1280}"
GPU_VALIDATE_YRES="${GPU_VALIDATE_YRES:-800}"
GPU_VALIDATE_SECONDS="${GPU_VALIDATE_SECONDS:-2}"
GPU_VALIDATE_3D_SECONDS="${GPU_VALIDATE_3D_SECONDS:-${GPU_VALIDATE_SECONDS}}"
GPU_VALIDATE_TOKEN="${GPU_VALIDATE_TOKEN:-gpuv-$$}"
APPEND_BASE="${QEMU_APPEND:-root=/dev/disk0 netsurf=0 webkit=0 glsmoke=0 gpu_validate=1 video=${GPU_VALIDATE_XRES}x${GPU_VALIDATE_YRES}}"
APPEND_BASE="${APPEND_BASE} gpu_validate_token=${GPU_VALIDATE_TOKEN}"

mkdir -p "$(dirname "${LOG}")"
: > "${LOG}"

fail()
{
    echo "gpu-validate: $*" >&2
    echo "gpu-validate: log: ${LOG}" >&2
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

cleanup_validation_qemu()
{
    pkill -TERM -f "qemu-system-x86_64 .*gpu_validate_token=${GPU_VALIDATE_TOKEN}" 2>/dev/null || true
}

validate_launch_contract()
{
    local dry

    dry="$(QEMU_DRY_RUN=1 DISPLAY_MODE=gtk USE_KVM=1 QEMU_GPU=virtio-vga-gl-primary \
        QEMU_VIRTIO_GPU_XRES="${GPU_VALIDATE_XRES}" \
        QEMU_VIRTIO_GPU_YRES="${GPU_VALIDATE_YRES}" \
        QEMU_INPUT=virtio QEMU_NET=0 QEMU_APPEND="${APPEND_BASE}" \
        bash scripts/launch/run-qemu.sh x86_64 \
        "${BUILD_DIR}/kernel/build/kernel/xv6.bin" "${BUILD_DIR}/fs.img")"
    printf '%s\n' "${dry}" >>"${LOG}"
    grep -Eq -- '-display gtk,gl=(on|es)' <<<"${dry}" ||
        fail "GTK/GL display contract missing"
    grep -q -- 'zoom-to-fit=off' <<<"${dry}" ||
        fail "GTK launch must not scale the guest canvas"
    grep -q -- 'full-screen=off' <<<"${dry}" ||
        fail "GTK launch must stay windowed"
    grep -q -- 'show-menubar=off' <<<"${dry}" ||
        fail "GTK menubar must stay hidden for deterministic geometry"
    grep -q -- 'show-tabs=off' <<<"${dry}" ||
        fail "GTK tabs must stay hidden for deterministic geometry"
    grep -q -- '-vga none' <<<"${dry}" ||
        fail "primary virtio-gpu launch must disable default VGA"
    grep -Eq -- "virtio-vga-gl([^[:space:]]*,)?xres=${GPU_VALIDATE_XRES},yres=${GPU_VALIDATE_YRES}" <<<"${dry}" ||
        fail "virtio-vga-gl primary geometry contract missing"
    grep -q -- 'virtio-tablet-pci' <<<"${dry}" ||
        fail "virtio tablet input contract missing"
    grep -q -- "video=${GPU_VALIDATE_XRES}x${GPU_VALIDATE_YRES}" <<<"${dry}" ||
        fail "guest video mode contract missing"
    grep -q -- 'gpu_validate=1' <<<"${dry}" ||
        fail "guest GPU substrate validation cmdline missing"
}

run_substrate()
{
    command -v expect >/dev/null 2>&1 ||
        fail "expect is required for prompt-synchronized guest commands"

    echo "gpu-validate: running substrate checks (${MODE})" | tee -a "${LOG}"
    trap cleanup_validation_qemu RETURN
    if ! expect >>"${LOG}" 2>&1 <<EOF; then
set timeout 180
match_max 2000000
proc wait_prompt {} {
    set saved_timeout \$::timeout
    set ::timeout 30
    expect {
        -re {[^\r\n]*# ?} { }
        timeout {
            send "\r"
            expect -re {[^\r\n]*# ?}
        }
    }
    set ::timeout \$saved_timeout
}
set env(DISPLAY_MODE) "${MODE}"
set env(USE_KVM) "${USE_KVM:-1}"
set env(QEMU_GPU) "${QEMU_GPU:-virtio-vga-gl-primary}"
set env(QEMU_INPUT) "${QEMU_INPUT:-virtio}"
set env(QEMU_NET) "${QEMU_NET:-0}"
set env(QEMU_VIRTIO_GPU_XRES) "${GPU_VALIDATE_XRES}"
set env(QEMU_VIRTIO_GPU_YRES) "${GPU_VALIDATE_YRES}"
set env(QEMU_ALLOW_WSL_SDL_GL) "${QEMU_ALLOW_WSL_SDL_GL:-1}"
set env(QEMU_APPEND) "${APPEND_BASE}"
spawn timeout --foreground ${TIMEOUT} bash scripts/launch/launch-gui.sh
expect -re {wlcomp: entering main loop}
expect -re {__GPUV_READY__}
expect {
    -re {__GPUV_FBSTAT_DONE_0__} { }
    -re {GPU substrate validator exited \(status 0\)} { }
    -re {GPU substrate validator exited \(status [1-9][0-9]*\)} { exit 1 }
    eof { exit 1 }
    timeout { exit 1 }
}
exit 0
EOF
        reject_log 'GPU substrate validator exited \(status [1-9][0-9]*\)' \
            "GPU substrate validator nonzero exit"
        require_log '(__GPUV_FBSTAT_DONE_0__|GPU substrate validator exited \(status 0\))' \
            "GPU substrate validator completion marker"
    fi
    cleanup_validation_qemu
    trap - RETURN

    require_log 'gpu-substrate-validate: run gbmtest' "GBM probe start"
    require_log 'gbmtest: backend=xv6-gbm' "GBM xv6 backend"
    require_log 'BO create/map/export/import/destroy' \
        "GBM BO import/export pass"
    require_log '(__GPUV_DMABUF_DONE_0__|dmabufsmoke: presented linux-dmabuf buffer)' \
        "linux-dmabuf presentation pass"
    require_log '(__GPUV_MESAWLEGL4_DONE_0__|mesawlegl\[[0-9]+\]: complete frames=[1-9][0-9]* seconds=[1-9][0-9]* .*status=0|mesawlegl_completion_matrix .*seconds=[1-9][0-9]* .*status=0)' \
        "Mesa Wayland EGL resize/swap pass"
    require_log '(__GPUV_MESAWLEGL6_DONE_0__|mesawlegl\[[0-9]+\]: complete frames=[1-9][0-9]* seconds=[1-9][0-9]* .*status=0|mesawlegl_completion_matrix .*seconds=[1-9][0-9]* .*status=0)' \
        "multi-client mesawlegl completion"
    require_log '(__GPUV_MESAGL_DONE_0__|mesaglsmoke\[[0-9]+\]: complete frames=[1-9][0-9]* seconds=[1-9][0-9]* status=0)' \
        "multi-client mesaglsmoke completion"
    require_log '(__GPUV_VIRGL_DONE_0__|virgltest: ctx=[0-9]+ res=[0-9]+ map=[0-9]+ fence=[0-9]+ signaled=[0-9]+)' \
        "virgl resource/submit/fence pass"
    require_log 'virgltest: copy-region ok' \
        "virgl resource copy pass"
    require_log 'virgltest: copy-region-scanout ok' \
        "virgl scanout-bind resource copy pass"
    require_log 'virgltest: copy-region-clear-src ok' \
        "virgl GPU-cleared source copy pass"
    require_log 'virgltest: copy-region-clear-src-render-bind ok' \
        "virgl render-target source copy pass"
    require_log 'virgltest: async-submit queued' \
        "virgl async submit/fence pass"
    require_log 'virgltest: invalid-submit rejected invalid ioctls' \
        "virgl invalid ioctl rejection pass"
    require_log 'virgltest: bad-submit isolated' \
        "virgl forced failure isolation pass"
    require_log 'virgltest: dmabuf-resource-import ok .*imported_resource=[1-9][0-9]*' \
        "virgl dmabuf import preserves resource identity"
    require_log '^backend virgl flags 0x[0-9a-fA-F]+ renderer ' \
        "KVM/virgl backend selected"
    require_log '^backend_opengl_submit 1[[:space:]]*$' \
        "KVM/virgl advertises OpenGL-submit"
    require_log '^backend_opengl_submit_gate open[[:space:]]*$' \
        "KVM/virgl OpenGL-submit gate is open"
    require_log '^backend_virgl_opengl 1[[:space:]]*$' \
        "KVM/virgl renderer flag"
    require_log 'opengl_submit_backend_separation_matrix .*backend=virgl .*dxg_transport=0 .*d3dkmt=0 .*virgl_opengl=1 .*backend_opengl_submit=1 .*allowed_submit_backend=virgl .*hyperv_dxg_transport_is_submit=0 .*hyperv_d3dkmt_is_submit=0 .*kvm_virgl_submit_allowed=1 .*native_present_credit=0 .*opengl_submit_credit=1 .*status=PASS' \
        "KVM/virgl positive OpenGL-submit control matrix"
    require_log 'mouseinject: absolute x=65535 y=65535' \
        "input injection while GPU clients are active"
    require_log 'virtio_input: initialized' "virtio-tablet input device"
    require_log 'gpubuftest: completed 3 buffer cycles' \
        "graphics buffer/fence cycles"
    require_log 'gpubuftest: render fd ownership verified' \
        "render fd ownership cleanup"
    require_log '^bo_handles 7[[:space:]]*$' \
        "bounded compositor triple-scanout BO set remains live"
    require_log '^virtio_resources 8[[:space:]]*$' \
        "bounded compositor triple-scanout resource set remains live"
    require_log 'wlcomp: using (direct scanout|fb GPU buffer|virgl framebuffer)' \
        "compositor GPU-backed/direct framebuffer mode"
    require_log '^display_presents [1-9][0-9]*[[:space:]]*$' \
        "display present completion accounting"
    require_log '^display_completions [1-9][0-9]*[[:space:]]*$' \
        "display completion accounting"
    require_log '^display_last_complete [1-9][0-9]*[[:space:]]*$' \
        "latest display completion sequence"
    require_log '^bo_fd_live 0[[:space:]]*$' "clean BO fd accounting"
    require_log '^fence_fd_live 0[[:space:]]*$' "clean fence fd accounting"
    require_log '^rejected_blits [01][[:space:]]*$' \
        "no unexpected rejected blit growth"
    require_log '^virtio_failures 0[[:space:]]*$' "no virtio failures"
    require_log '^virtio_timeouts 0[[:space:]]*$' "no virtio timeouts"
    require_log '^virtio_context_failed 0[[:space:]]*$' \
        "no live failed virgl contexts after recovery"
    require_log '^virtio_context_failures [1-9][0-9]*[[:space:]]*$' \
        "forced virgl context failure was accounted"
}

run_visible_3d()
{
    local sock ppm

    sock="$(mktemp -u /tmp/xv6-gpu-monitor.XXXXXX)"
    ppm="${GPU_VALIDATE_SCREENSHOT:-${ROOT}/${BUILD_DIR}/gpu-validate.ppm}"
    command -v expect >/dev/null 2>&1 ||
        fail "expect is required for prompt-synchronized guest commands"

    echo "gpu-validate: running visible virgl demo (${ppm})" | tee -a "${LOG}"
    trap cleanup_validation_qemu RETURN
    if ! expect >>"${LOG}" 2>&1 <<EOF; then
set timeout 120
match_max 2000000
set env(DISPLAY_MODE) "gtk"
set env(USE_KVM) "${USE_KVM:-1}"
set env(QEMU_GPU) "virtio-vga-gl-primary"
set env(QEMU_INPUT) "${QEMU_INPUT:-virtio}"
set env(QEMU_NET) "${QEMU_NET:-0}"
set env(QEMU_APPEND) "root=/dev/disk0 netsurf=0 webkit=0 glsmoke=1 glsmoke_demo=1 glsmoke_accel=1 glsmoke_seconds=${GPU_VALIDATE_3D_SECONDS} video=${GPU_VALIDATE_XRES}x${GPU_VALIDATE_YRES} gpu_validate_token=${GPU_VALIDATE_TOKEN}"
set env(QEMU_EXTRA) "-monitor unix:${sock},server,nowait ${QEMU_EXTRA:-}"
spawn timeout --foreground ${GPU_VALIDATE_3D_TIMEOUT:-120s} bash scripts/launch/launch-gui.sh
expect -re {wlcomp: entering main loop}
expect -re {renderer=virgl .*spherical-poly-demo}
expect -re {demo_surface_matrix .*status=PASS}
expect -re {mesawlegl\[[0-9]+\]: app_loop_fps=}
after 500
catch { exec sh -c "printf 'screendump ${ppm}\\n' | nc -U ${sock} >/dev/null 2>&1 || true" }
expect -re {mesawlegl\[[0-9]+\]: complete frames=[1-9][0-9]* seconds=[1-9][0-9]* status=0}
exit 0
EOF
        fail "visible 3D VM run failed"
    fi
    cleanup_validation_qemu
    trap - RETURN

    require_log 'renderer=virgl' "virgl renderer"
    require_log 'spherical-poly-demo' "spherical polygon demo marker"
    require_log 'virgltest: bad-submit isolated' \
        "virgl context failure isolation"
    require_log 'status=0' "3D smoke clean exit"
    require_log '^virtio_context_failed 0[[:space:]]*$' \
        "no live failed virgl contexts after recovery"
    require_log '^virtio_timeouts 0[[:space:]]*$' "no virtio timeouts after 3D"
    if command -v nc >/dev/null 2>&1 && [[ ! -s "${ppm}" ]]; then
        fail "screenshot was not captured"
    fi
}

reject_common_failures()
{
    reject_log 'panic|fatal page fault|SIGABRT|coredump: generating' \
        "kernel/userspace crash marker"
    reject_log 'freewalk: WARNING' "page-table leak warning"
    reject_log 'virtio_gpu: command .* timed out' "virtio-gpu timeout"
}

if [[ "${GPU_VALIDATE_BUILD:-0}" == "1" ]]; then
    cmake --build "${BUILD_DIR}" --target kernel -j"${GPU_VALIDATE_JOBS:-2}" | tee -a "${LOG}"
    cmake --build "${BUILD_DIR}/ports" --target port-wayland port-xv6-gbm -j"${GPU_VALIDATE_JOBS:-2}" | tee -a "${LOG}"
    cmake --build "${BUILD_DIR}" --target rootfs -j"${GPU_VALIDATE_JOBS:-2}" | tee -a "${LOG}"
fi

validate_launch_contract
run_substrate
if [[ "${GPU_VALIDATE_VISIBLE_3D:-0}" == "1" ]]; then
    run_visible_3d
fi
reject_common_failures

echo "gpu-validate: PASS (${LOG})"
