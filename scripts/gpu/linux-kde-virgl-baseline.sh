#!/usr/bin/env bash
# Capture a Linux KDE/Plasma KVM+virgl baseline.
#
# This boots Alpine Linux under QEMU KVM with virtio-vga-gl, installs upstream
# KDE/Plasma packages, starts a minimal KWin Wayland + plasmashell session, and
# records guest timing plus QEMU virtio-gpu trace shape. It does not modify xv6
# build artifacts.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

ALPINE_VERSION="${ALPINE_VERSION:-3.23.4}"
ALPINE_FLAVOR="${ALPINE_FLAVOR:-standard}"
ALPINE_ARCH="${ALPINE_ARCH:-x86_64}"
CAPTURE_DIR="${LINUX_KDE_BASELINE_DIR:-${ROOT}/build-x86_64/linux-kde-virgl-baseline/$(date +%Y%m%d-%H%M%S)}"
WORK_DIR="${LINUX_KDE_WORK_DIR:-/tmp/linux-kde-virgl-baseline}"
ISO="${ALPINE_ISO:-${WORK_DIR}/alpine-${ALPINE_FLAVOR}-${ALPINE_VERSION}-${ALPINE_ARCH}.iso}"
ISO_URL="${ALPINE_ISO_URL:-https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/releases/${ALPINE_ARCH}/$(basename "${ISO}")}"
BOOT_DIR="${WORK_DIR}/boot"
KERNEL="${BOOT_DIR}/vmlinuz-lts"
INITRD="${BOOT_DIR}/initramfs-lts"
TRACE_EVENTS="${CAPTURE_DIR}/qemu-trace-events"
QEMU_TRACE="${CAPTURE_DIR}/linux-kde-qemu.trace"
SERIAL_LOG="${CAPTURE_DIR}/linux-kde-serial.expect.log"
MONITOR_SOCK="${CAPTURE_DIR}/qemu-monitor.sock"
SCREENSHOT="${CAPTURE_DIR}/linux-kde-desktop.ppm"
SUMMARY="${CAPTURE_DIR}/linux-kde-virgl-summary.txt"
DISPLAY_BACKEND="${LINUX_KDE_QEMU_DISPLAY:-gtk,gl=on,show-cursor=on}"
QEMU_DEVICE="${LINUX_KDE_QEMU_DEVICE:-virtio-vga-gl,xres=1280,yres=800}"
QEMU_MEMORY="${LINUX_KDE_QEMU_MEMORY:-8192}"
RUN_SECONDS="${LINUX_KDE_RUN_SECONDS:-20}"

mkdir -p "${CAPTURE_DIR}" "${WORK_DIR}" "${BOOT_DIR}"

need()
{
    command -v "$1" >/dev/null 2>&1 || {
        echo "linux-kde-virgl-baseline: missing required tool: $1" >&2
        exit 1
    }
}

need expect
need qemu-system-x86_64
need curl

if [[ ! -f "${ISO}" ]]; then
    echo "linux-kde-virgl-baseline: downloading ${ISO_URL}" | tee -a "${SUMMARY}"
    curl -fL "${ISO_URL}" -o "${ISO}"
fi

if [[ ! -f "${KERNEL}" || ! -f "${INITRD}" ]]; then
    need xorriso
    echo "linux-kde-virgl-baseline: extracting Alpine kernel/initrd" | tee -a "${SUMMARY}"
    rm -rf "${BOOT_DIR}"
    mkdir -p "${BOOT_DIR}"
    xorriso -osirrox on -indev "${ISO}" \
        -extract /boot/vmlinuz-lts "${KERNEL}" \
        -extract /boot/initramfs-lts "${INITRD}" \
        >"${CAPTURE_DIR}/xorriso-extract.log" 2>&1
fi

cat >"${TRACE_EVENTS}" <<'EOF'
displaysurface_create
displaysurface_create_from
displaysurface_create_pixman
displaysurface_free
displaychangelistener_register
displaychangelistener_unregister
gd_gl_area_create_context
gd_gl_area_destroy_context
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
virtio_gpu_update_cursor
virtio_gpu_fence_ctrl
virtio_gpu_fence_resp
EOF

cat >"${CAPTURE_DIR}/run-linux-kde.expect" <<EOF
#!/usr/bin/expect -f
set timeout 2400
match_max 5000000
log_file -a "${SERIAL_LOG}"

spawn qemu-system-x86_64 \\
  -enable-kvm -cpu host -smp 4 -m "${QEMU_MEMORY}" \\
  -kernel "${KERNEL}" -initrd "${INITRD}" \\
  -append "modules=loop,squashfs,sd-mod,usb-storage quiet console=ttyS0" \\
  -cdrom "${ISO}" -boot d \\
  -vga none -device "${QEMU_DEVICE}" \\
  -display "${DISPLAY_BACKEND}" \\
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \\
  -serial stdio -monitor "unix:${MONITOR_SOCK},server,nowait" -no-reboot \\
  -trace "events=${TRACE_EVENTS},file=${QEMU_TRACE}"

expect {
  -re "localhost login:" { send "root\r" }
  -re "alpine login:" { send "root\r" }
  timeout { puts "LINUX_KDE_BASELINE_FAIL login-timeout"; exit 2 }
}
expect {
  -re "localhost:~#" {}
  -re "~ #" {}
  -re "# " {}
  timeout { puts "LINUX_KDE_BASELINE_FAIL root-prompt-timeout"; exit 3 }
}

proc cmd {s {t 900}} {
  set timeout \$t
  send -- "\$s\r"
  expect {
    -re "LINUX_KDE_CTRL# " {}
    timeout { puts "LINUX_KDE_BASELINE_FAIL command-timeout command=\$s"; exit 4 }
  }
}

send -- "export PS1='LINUX_KDE_CTRL# '\r"
expect -re "LINUX_KDE_CTRL# "
cmd "date -Iseconds; uname -a; cat /etc/alpine-release" 60
cmd "ip link set eth0 up || true; udhcpc -i eth0 -q -n || true; ip addr show eth0" 120
cmd "printf '%s\\n' https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/main https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/community > /etc/apk/repositories; apk update" 300
cmd "mount -o remount,size=6G / || true; df -h / /tmp; apk add --no-cache eudev dbus dbus-x11 elogind polkit polkit-elogind polkit-kde-agent-1 udisks2 plasma-workspace plasma-desktop kwin kscreen kactivitymanagerd kde-cli-tools kded plasma-integration qqc2-desktop-style xwayland mesa-dri-gallium mesa-egl mesa-gbm mesa-gl mesa-gles mesa-utils mesa-demos libinput hwdata font-dejavu; df -h / /tmp" 2400
cmd "modprobe virtio_gpu || true; mkdir -p /run/dbus /run/user/0 /tmp/.X11-unix; chmod 700 /run/user/0; dbus-daemon --system --fork || true; ls -l /dev/dri || true; dmesg | grep -Ei 'virtio_gpu|virgl|drm|cap set|features' | tail -160" 120
cmd "command -v kwin_wayland; command -v plasmashell; command -v kactivitymanagerd || true; command -v kded6 || true; command -v glxgears || true" 60
cmd {cat > /root/run-kde-baseline.sh <<'SH'
#!/bin/sh
set -eu
export HOME=/root
export USER=root
export LOGNAME=root
export XDG_RUNTIME_DIR=/run/user/0
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=KDE
export XDG_SESSION_DESKTOP=KDE
export KDE_FULL_SESSION=true
export KDE_SESSION_VERSION=6
export QT_QPA_PLATFORM=wayland
export GALLIUM_DRIVER=virgl
export KWIN_COMPOSE=O2ES
export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/kde-session-bus
mkdir -p /run/user/0 /tmp/.X11-unix
chmod 700 /run/user/0
dbus-daemon --session --fork --address=unix:path=/tmp/kde-session-bus --print-address=1 >/tmp/kde-session-bus.addr
printf 'LINUX_KDE_METRIC start_uptime_s='; cut -d' ' -f1 /proc/uptime
kwin_wayland --drm --xwayland --no-lockscreen >/tmp/kwin-wayland.log 2>&1 &
seq 1 300 | while read ignored; do [ -S /run/user/0/wayland-0 ] && break; sleep 0.1; done
printf 'LINUX_KDE_METRIC wayland_socket_uptime_s='; cut -d' ' -f1 /proc/uptime
export WAYLAND_DISPLAY=wayland-0
[ -S /run/user/0/wayland-0 ] || echo LINUX_KDE_FAIL no-wayland-socket
(kactivitymanagerd >/tmp/kactivitymanagerd.log 2>&1 || true) &
(kded6 >/tmp/kded6.log 2>&1 || true) &
plasmashell >/tmp/plasmashell.log 2>&1 &
seq 1 300 | while read ignored; do pidof plasmashell >/dev/null 2>&1 && break; sleep 0.1; done
printf 'LINUX_KDE_METRIC plasmashell_uptime_s='; cut -d' ' -f1 /proc/uptime
seq 1 80 | while read ignored; do [ -S /tmp/.X11-unix/X0 ] && break; sleep 0.1; done
printf 'LINUX_KDE_METRIC xwayland_uptime_s='; cut -d' ' -f1 /proc/uptime
[ -S /tmp/.X11-unix/X0 ] || echo LINUX_KDE_WARN no-xwayland-socket
sleep 5
DISPLAY=:0 timeout 10 glxgears -info -geometry 640x480+320+180 >/tmp/glxgears.log 2>&1 || true
sleep ${RUN_SECONDS}
printf 'LINUX_KDE_METRIC final_uptime_s='; cut -d' ' -f1 /proc/uptime
pidof kwin_wayland plasmashell kactivitymanagerd || true
printf '%s\n' '--- kwin log ---'; tail -120 /tmp/kwin-wayland.log || true
printf '%s\n' '--- plasmashell log ---'; tail -120 /tmp/plasmashell.log || true
printf '%s\n' '--- glxgears log ---'; cat /tmp/glxgears.log || true
SH
} 60
cmd "chmod +x /root/run-kde-baseline.sh" 30
cmd "stty -echo" 30
set timeout 1200
send -- "/root/run-kde-baseline.sh; echo LINUX_KDE_SCRIPT_DONE\r"
expect {
  -re "LINUX_KDE_SCRIPT_DONE" {}
  timeout { puts "LINUX_KDE_BASELINE_FAIL kde-script-timeout"; exit 5 }
}
cmd "stty echo" 30
if {[catch { exec sh -c "if command -v nc >/dev/null 2>&1; then printf 'screendump ${SCREENSHOT}\\r\\n' | nc -U -w 4 -q 1 ${MONITOR_SOCK} > ${CAPTURE_DIR}/screendump-monitor.log 2>&1 || true; else echo 'nc missing' > ${CAPTURE_DIR}/screendump-monitor.log; fi" } err]} {
  puts "screendump command failed: \$err"
}
send -- "echo LINUX_KDE_BASELINE_DONE\r"
expect -re "LINUX_KDE_BASELINE_DONE"
exit 0
EOF
chmod +x "${CAPTURE_DIR}/run-linux-kde.expect"

echo "linux-kde-virgl-baseline: capture dir ${CAPTURE_DIR}" | tee -a "${SUMMARY}"
if ! "${CAPTURE_DIR}/run-linux-kde.expect" >"${CAPTURE_DIR}/run-linux-kde.host.log" 2>&1; then
    echo "linux-kde-virgl-baseline: run failed; see ${CAPTURE_DIR}" >&2
    exit 1
fi

{
    echo "Linux KDE Plasma KVM+virgl baseline"
    echo "capture_dir=${CAPTURE_DIR}"
    echo "iso=${ISO}"
    echo "qemu_device=${QEMU_DEVICE}"
    echo "qemu_memory=${QEMU_MEMORY}"
    echo "display=${DISPLAY_BACKEND}"
    echo "run_seconds=${RUN_SECONDS}"
    echo "screenshot=${SCREENSHOT}"
    if [[ -s "${SCREENSHOT}" ]]; then
        echo "screenshot_status=present"
    else
        echo "screenshot_status=missing"
        [[ -f "${CAPTURE_DIR}/screendump-monitor.log" ]] &&
            sed 's/^/screendump_log=/' "${CAPTURE_DIR}/screendump-monitor.log"
    fi
    echo
    echo "[guest KDE metrics]"
    grep -E 'LINUX_KDE_(METRIC|WARN|FAIL)' "${SERIAL_LOG}" || true
    echo
    echo "[guest renderer / GL]"
    grep -E 'OpenGL renderer string|GL_RENDERER|direct rendering|frames in|FPS' "${SERIAL_LOG}" | tail -40 || true
    echo
    echo "[guest process check]"
    grep -E 'kwin_wayland|plasmashell|kactivitymanagerd' "${SERIAL_LOG}" | tail -40 || true
    echo
    echo "[qemu virtio-gpu trace counts]"
    for event in virtio_gpu_cmd_ctx_submit virtio_gpu_cmd_set_scanout \
                 virtio_gpu_cmd_res_flush virtio_gpu_fence_ctrl \
                 virtio_gpu_fence_resp virtio_gpu_cmd_res_create_3d \
                 virtio_gpu_cmd_res_xfer_toh_3d; do
        printf '%s %s\n' "${event}" "$(grep -c "${event}" "${QEMU_TRACE}" 2>/dev/null || true)"
    done
    echo
    echo "[steady-state trace tail]"
    grep -E 'virtio_gpu_cmd_(ctx_submit|set_scanout|res_flush|res_xfer_toh_3d)|virtio_gpu_fence_(ctrl|resp)' "${QEMU_TRACE}" |
        tail -100 || true
} >>"${SUMMARY}"

echo "linux-kde-virgl-baseline: wrote ${SUMMARY}"
