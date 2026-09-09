#!/usr/bin/env bash
# Capture a known-good Linux desktop virgl control run.
#
# The run boots Alpine with QEMU virtio-vga-gl, starts an Xorg/Openbox desktop,
# launches glxgears, and records the guest DRM/GL facts plus QEMU virtio-gpu
# trace events.  This is intentionally a host-side comparison tool; it does not
# modify xv6 build artifacts.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

ALPINE_VERSION="${ALPINE_VERSION:-3.23.4}"
ALPINE_FLAVOR="${ALPINE_FLAVOR:-standard}"
ALPINE_ARCH="${ALPINE_ARCH:-x86_64}"
CAPTURE_DIR="${ALPINE_CAPTURE_DIR:-${ROOT}/build-x86_64/alpine-trace/live-$(date +%Y%m%d-%H%M%S)}"
WORK_DIR="${ALPINE_WORK_DIR:-/tmp/alpine-desktop-capture}"
ISO="${ALPINE_ISO:-${WORK_DIR}/alpine-${ALPINE_FLAVOR}-${ALPINE_VERSION}-${ALPINE_ARCH}.iso}"
ISO_URL="${ALPINE_ISO_URL:-https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/releases/${ALPINE_ARCH}/$(basename "${ISO}")}"
BOOT_DIR="${WORK_DIR}/boot"
KERNEL="${BOOT_DIR}/vmlinuz-lts"
INITRD="${BOOT_DIR}/initramfs-lts"
TRACE_EVENTS="${CAPTURE_DIR}/qemu-trace-events"
QEMU_TRACE="${CAPTURE_DIR}/alpine-xorg-qemu.trace"
SERIAL_LOG="${CAPTURE_DIR}/alpine-xorg-serial.expect.log"
MONITOR_SOCK="${CAPTURE_DIR}/qemu-monitor.sock"
SCREENSHOT="${CAPTURE_DIR}/alpine-desktop.ppm"
SUMMARY="${CAPTURE_DIR}/alpine-virgl-desktop-summary.txt"
DISPLAY_BACKEND="${ALPINE_QEMU_DISPLAY:-sdl,gl=on,show-cursor=on}"
QEMU_DEVICE="${ALPINE_QEMU_DEVICE:-virtio-vga-gl,xres=1280,yres=800}"
RUN_SECONDS="${ALPINE_RUN_SECONDS:-12}"

mkdir -p "${CAPTURE_DIR}" "${WORK_DIR}" "${BOOT_DIR}"

need()
{
    command -v "$1" >/dev/null 2>&1 || {
        echo "alpine-virgl-desktop-capture: missing required tool: $1" >&2
        exit 1
    }
}

need expect
need qemu-system-x86_64
need curl

if [[ ! -f "${ISO}" ]]; then
    echo "alpine-virgl-desktop-capture: downloading ${ISO_URL}" | tee -a "${SUMMARY}"
    curl -fL "${ISO_URL}" -o "${ISO}"
fi

if [[ ! -f "${KERNEL}" || ! -f "${INITRD}" ]]; then
    need xorriso
    echo "alpine-virgl-desktop-capture: extracting Alpine kernel/initrd" | tee -a "${SUMMARY}"
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

cat >"${CAPTURE_DIR}/run-alpine-desktop.expect" <<EOF
#!/usr/bin/expect -f
set timeout 1800
match_max 4000000
log_file -a "${SERIAL_LOG}"

spawn qemu-system-x86_64 \\
  -enable-kvm -cpu host -smp 4 -m 4096 \\
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
  timeout { puts "TIMEOUT waiting for login"; exit 2 }
}
expect {
  -re "localhost:~#" {}
  -re "~ #" {}
  -re "# " {}
  timeout { puts "TIMEOUT waiting for root prompt"; exit 3 }
}

proc cmd {s {t 900}} {
  set timeout \$t
  send -- "\$s\r"
  expect {
    -re "ALPINE_CTRL# " {}
    timeout { puts "TIMEOUT running: \$s"; exit 4 }
  }
}

send -- "export PS1='ALPINE_CTRL# '\r"
expect -re "ALPINE_CTRL# "
cmd "date; uname -a; cat /etc/alpine-release" 60
cmd "ip link set eth0 up || true; udhcpc -i eth0 -q -n || true; ip addr show eth0" 120
cmd "printf '%s\\n' https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/main https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/community > /etc/apk/repositories; apk update" 300
cmd {for p in eudev dbus xorg-server xinit xf86-video-modesetting xf86-input-libinput openbox xterm xsetroot mesa-dri-gallium mesa-gl mesa-egl mesa-gbm mesa-utils mesa-demos kmscube libdrm libdrm-tests font-dejavu; do echo APK_ADD:\$p; apk add --no-cache \$p || echo APK_FAIL:\$p; done} 1800
cmd "modprobe virtio_gpu || true; ls -l /dev/dri || true; dmesg | grep -Ei 'virtio_gpu|virgl|drm|cap set|features' | tail -180 > /tmp/alpine-drm.log; cat /tmp/alpine-drm.log" 120
cmd "glxinfo -B > /tmp/glxinfo-before-x.txt 2>&1 || true; cat /tmp/glxinfo-before-x.txt" 60
cmd "cat > /root/.xinitrc <<'SH'\nxsetroot -solid '#102040' || true\nopenbox-session &\nsleep 2\nxterm -geometry 100x12+20+20 -title Alpine-virgl -e sh -lc 'echo Alpine virgl desktop; echo; glxinfo -B; echo; sleep 300' &\nsleep 1\nDISPLAY=:0 GALLIUM_DRIVER=virgl LIBGL_DEBUG=verbose MESA_DEBUG=1 glxgears -info -geometry 640x480+360+180 >/tmp/glxgears.log 2>&1 &\nsleep 300\nSH\nchmod +x /root/.xinitrc\n(export GALLIUM_DRIVER=virgl; export LIBGL_ALWAYS_SOFTWARE=0; startx /root/.xinitrc -- :0 -noreset -verbose 3 > /tmp/startx.log 2>&1 &)\nsleep ${RUN_SECONDS}" 180
if {[catch { exec sh -c "if command -v nc >/dev/null 2>&1; then printf 'screendump ${SCREENSHOT}\\r\\n' | nc -U -w 3 -q 1 ${MONITOR_SOCK} > ${CAPTURE_DIR}/screendump-monitor.log 2>&1 || true; else echo 'nc missing' > ${CAPTURE_DIR}/screendump-monitor.log; fi" } err]} {
  puts "screendump command failed: \$err"
}
cmd "DISPLAY=:0 glxinfo -B > /tmp/glxinfo-x.txt 2>&1 || true; pkill glxgears || true; pkill Xorg || true; sleep 1; tail -120 /tmp/startx.log; tail -80 /tmp/glxgears.log; cat /tmp/glxinfo-x.txt; tar -C /tmp -czf /tmp/alpine-desktop-gl-trace.tar.gz alpine-drm.log glxinfo-before-x.txt glxinfo-x.txt glxgears.log startx.log" 120
send -- "echo ALPINE_CAPTURE_DONE\r"
expect -re "ALPINE_CAPTURE_DONE"
exit 0
EOF
chmod +x "${CAPTURE_DIR}/run-alpine-desktop.expect"

echo "alpine-virgl-desktop-capture: capture dir ${CAPTURE_DIR}" | tee -a "${SUMMARY}"
if ! "${CAPTURE_DIR}/run-alpine-desktop.expect" >"${CAPTURE_DIR}/run-alpine-desktop.host.log" 2>&1; then
    echo "alpine-virgl-desktop-capture: run failed; see ${CAPTURE_DIR}" >&2
    exit 1
fi

{
    echo "Alpine virgl desktop capture"
    echo "capture_dir=${CAPTURE_DIR}"
    echo "iso=${ISO}"
    echo "qemu_device=${QEMU_DEVICE}"
    echo "display=${DISPLAY_BACKEND}"
    echo "screenshot=${SCREENSHOT}"
    if [[ -s "${SCREENSHOT}" ]]; then
        echo "screenshot_status=present"
    else
        echo "screenshot_status=missing"
        if [[ -f "${CAPTURE_DIR}/screendump-monitor.log" ]]; then
            sed 's/^/screendump_log=/' "${CAPTURE_DIR}/screendump-monitor.log"
        fi
    fi
    echo
    echo "[guest renderer]"
    grep -E 'OpenGL renderer string|Device:|Accelerated:|direct rendering:' "${SERIAL_LOG}" | tail -20 || true
    echo
    echo "[qemu virtio-gpu trace counts]"
    for event in virtio_gpu_cmd_ctx_submit virtio_gpu_cmd_set_scanout \
                 virtio_gpu_cmd_res_flush virtio_gpu_fence_ctrl \
                 virtio_gpu_fence_resp virtio_gpu_cmd_res_create_3d; do
        printf '%s %s\n' "${event}" "$(grep -c "${event}" "${QEMU_TRACE}" 2>/dev/null || true)"
    done
    echo
    echo "[steady-state trace tail]"
    grep -E 'virtio_gpu_cmd_(ctx_submit|set_scanout|res_flush)|virtio_gpu_fence_(ctrl|resp)' "${QEMU_TRACE}" |
        tail -80 || true
} >>"${SUMMARY}"

echo "alpine-virgl-desktop-capture: wrote ${SUMMARY}"
