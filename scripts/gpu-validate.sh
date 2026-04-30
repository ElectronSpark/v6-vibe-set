#!/usr/bin/env bash
# Validate the xv6 GPU substrate without relying on browser/toolkit behavior.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-build-x86_64}"
LOG="${GPU_VALIDATE_LOG:-${ROOT}/${BUILD_DIR}/gpu-validate.log}"
MODE="${GPU_VALIDATE_MODE:-nographic}"
TIMEOUT="${GPU_VALIDATE_TIMEOUT:-180s}"
APPEND_BASE="${QEMU_APPEND:-root=/dev/disk0 netsurf=0 webkit=0 glsmoke=0 video=1280x800}"

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

validate_launch_contract()
{
    local dry

    dry="$(QEMU_DRY_RUN=1 DISPLAY_MODE=gtk USE_KVM=1 QEMU_GPU=virtio-gpu-gl \
        QEMU_INPUT=virtio QEMU_NET=0 QEMU_APPEND="${APPEND_BASE}" \
        bash scripts/run-qemu.sh x86_64 \
        "${BUILD_DIR}/kernel/kernel.elf" "${BUILD_DIR}/fs.img")"
    printf '%s\n' "${dry}" >>"${LOG}"
    grep -q -- '-display gtk,gl=on' <<<"${dry}" ||
        fail "GTK/GL display contract missing"
    grep -q -- 'zoom-to-fit=off' <<<"${dry}" ||
        fail "GTK launch must not scale the guest canvas"
    grep -q -- 'full-screen=off' <<<"${dry}" ||
        fail "GTK launch must stay windowed"
    grep -q -- 'show-menubar=off' <<<"${dry}" ||
        fail "GTK menubar must stay hidden for deterministic geometry"
    grep -q -- 'show-tabs=off' <<<"${dry}" ||
        fail "GTK tabs must stay hidden for deterministic geometry"
    grep -q -- 'virtio-gpu-gl-pci,xres=1280,yres=800' <<<"${dry}" ||
        fail "virtio-gpu-gl geometry contract missing"
    grep -q -- 'virtio-tablet-pci' <<<"${dry}" ||
        fail "virtio tablet input contract missing"
    grep -q -- 'video=1280x800' <<<"${dry}" ||
        fail "guest video mode contract missing"
}

run_substrate()
{
    command -v expect >/dev/null 2>&1 ||
        fail "expect is required for prompt-synchronized guest commands"

    echo "gpu-validate: running substrate checks (${MODE})" | tee -a "${LOG}"
expect >>"${LOG}" 2>&1 <<EOF || fail "substrate VM run failed"
set timeout 180
proc wait_prompt {} {
    set saved_timeout \$::timeout
    set ::timeout 30
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
set env(QEMU_GPU) "${QEMU_GPU:-virtio-gpu}"
set env(QEMU_INPUT) "${QEMU_INPUT:-virtio}"
set env(QEMU_NET) "${QEMU_NET:-0}"
set env(QEMU_APPEND) "${APPEND_BASE}"
spawn timeout --foreground ${TIMEOUT} bash scripts/launch-gui.sh
expect "wlcomp: entering main loop"
wait_prompt
send "export XDG_RUNTIME_DIR=/tmp\r"
wait_prompt
send "export WAYLAND_DISPLAY=wayland-0\r"
wait_prompt
send "gbmtest\r"
expect -re {gbmtest: passed linear BO create/map/export/import/destroy}
wait_prompt
send "dmabufsmoke\r"
expect -re {dmabufsmoke: presented linux-dmabuf buffer}
wait_prompt
send "mesawlegl --frames=4 --loops=1 --resize-every=2\r"
expect -re {mesawlegl\[[0-9]+\]: complete frames=4 status=0}
wait_prompt
send "mesawlegl --frames=6 --loops=1 --resize-every=3 &\r"
wait_prompt
send "mesaglsmoke --frames=6 --loops=1 --resize-every=3 &\r"
wait_prompt
send "mouseinject 65535 65535\r"
expect -re {mouseinject: absolute x=65535 y=65535}
wait_prompt
set saw_mesawlegl 0
set saw_mesaglsmoke 0
while { !(\$saw_mesawlegl && \$saw_mesaglsmoke) } {
    expect {
        -re {mesawlegl\[[0-9]+\]: complete frames=6 status=0} {
            set saw_mesawlegl 1
        }
        -re {mesaglsmoke\[[0-9]+\]: complete frames=6 status=0} {
            set saw_mesaglsmoke 1
        }
        timeout {
            exit 3
        }
    }
}
wait_prompt
send "gpubuftest 3\r"
expect -re {gpubuftest: completed 3 buffer cycles}
wait_prompt
send "gpubuftest --render-owner\r"
expect -re {gpubuftest: render fd ownership verified}
wait_prompt
send "sleep 1\r"
wait_prompt
send "fbstat\r"
wait_prompt
send "shutdown\r"
expect eof
catch wait result
exit [lindex \$result 3]
EOF

    require_log 'gbmtest: passed linear BO create/map/export/import/destroy' \
        "GBM BO import/export pass"
    require_log 'dmabufsmoke: presented linux-dmabuf buffer' \
        "linux-dmabuf presentation pass"
    require_log 'mesawlegl\[[0-9]+\]: complete frames=4 status=0' \
        "Mesa Wayland EGL resize/swap pass"
    require_log 'mesawlegl\[[0-9]+\]: complete frames=6 status=0' \
        "multi-client mesawlegl completion"
    require_log 'mesaglsmoke\[[0-9]+\]: complete frames=6 status=0' \
        "multi-client mesaglsmoke completion"
    require_log 'mouseinject: absolute x=65535 y=65535' \
        "input injection while GPU clients are active"
    require_log 'virtio_input: initialized' "virtio-tablet input device"
    require_log 'gpubuftest: completed 3 buffer cycles' \
        "graphics buffer/fence cycles"
    require_log 'gpubuftest: render fd ownership verified' \
        "render fd ownership cleanup"
    require_log '^bo_handles 0[[:space:]]*$' "clean BO handle accounting"
    require_log '^bo_live_bytes 0[[:space:]]*$' "clean BO byte accounting"
    require_log '^bo_fd_live 0[[:space:]]*$' "clean BO fd accounting"
    require_log '^fence_fd_live 0[[:space:]]*$' "clean fence fd accounting"
    require_log '^rejected_blits 0[[:space:]]*$' "no rejected blits"
    require_log '^virtio_failures 0[[:space:]]*$' "no virtio failures"
    require_log '^virtio_timeouts 0[[:space:]]*$' "no virtio timeouts"
}

run_visible_3d()
{
    local sock ppm monitor_cmd

    sock="$(mktemp -u /tmp/xv6-gpu-monitor.XXXXXX)"
    ppm="${GPU_VALIDATE_SCREENSHOT:-${ROOT}/${BUILD_DIR}/gpu-validate.ppm}"
    command -v expect >/dev/null 2>&1 ||
        fail "expect is required for prompt-synchronized guest commands"

    echo "gpu-validate: running visible virgl demo (${ppm})" | tee -a "${LOG}"
    (
        sleep "${GPU_VALIDATE_SCREENSHOT_DELAY:-4}"
        if command -v nc >/dev/null 2>&1; then
            monitor_cmd="screendump ${ppm}\n"
            printf "${monitor_cmd}" | nc -U "${sock}" >/dev/null 2>&1 || true
        fi
    ) &
expect >>"${LOG}" 2>&1 <<EOF || fail "visible 3D VM run failed"
set timeout 120
proc wait_prompt {} {
    set saved_timeout \$::timeout
    set ::timeout 30
    expect {
        -re {root:/# ?} { }
        timeout {
            send "\r"
            expect -re {root:/# ?}
        }
    }
    set ::timeout \$saved_timeout
}
set env(DISPLAY_MODE) "gtk"
set env(USE_KVM) "${USE_KVM:-1}"
set env(QEMU_GPU) "virtio-gpu-gl"
set env(QEMU_INPUT) "${QEMU_INPUT:-virtio}"
set env(QEMU_NET) "${QEMU_NET:-0}"
set env(QEMU_APPEND) "root=/dev/disk0 netsurf=0 webkit=0 glsmoke=1 glsmoke_demo=1 glsmoke_accel=1 glsmoke_frames=${GPU_VALIDATE_FRAMES:-120} video=1280x800"
set env(QEMU_EXTRA) "-monitor unix:${sock},server,nowait ${QEMU_EXTRA:-}"
spawn timeout --foreground ${GPU_VALIDATE_3D_TIMEOUT:-120s} bash scripts/launch-gui.sh
expect "wlcomp: entering main loop"
wait_prompt
after 8000
send "export XDG_RUNTIME_DIR=/tmp\r"
wait_prompt
send "export WAYLAND_DISPLAY=wayland-0\r"
wait_prompt
send "virgltest --bad-submit\r"
expect -re {virgltest: bad-submit isolated}
wait_prompt
send "sleep 1\r"
wait_prompt
send "fbstat\r"
wait_prompt
send "shutdown\r"
expect eof
catch wait result
exit [lindex \$result 3]
EOF

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
