#!/usr/bin/env bash
# Capture Linux KDE/Plasma interaction latency under KVM+virgl.
#
# This is a Linux reference control for xv6's desktop-interaction-latency
# reducer.  It intentionally mirrors the xv6 phase/action log shape while
# keeping Linux/KDE packages upstream-clean.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

ALPINE_VERSION="${ALPINE_VERSION:-3.23.4}"
ALPINE_FLAVOR="${ALPINE_FLAVOR:-standard}"
ALPINE_ARCH="${ALPINE_ARCH:-x86_64}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
CAPTURE_DIR="${LINUX_KDE_INTERACTION_DIR:-${ROOT}/build-x86_64/linux-kde-interaction-proof/${STAMP}}"
WORK_DIR="${LINUX_KDE_WORK_DIR:-/tmp/linux-kde-interaction-proof}"
ISO="${ALPINE_ISO:-${WORK_DIR}/alpine-${ALPINE_FLAVOR}-${ALPINE_VERSION}-${ALPINE_ARCH}.iso}"
ISO_URL="${ALPINE_ISO_URL:-https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/releases/${ALPINE_ARCH}/$(basename "${ISO}")}"
BOOT_DIR="${WORK_DIR}/boot"
KERNEL="${BOOT_DIR}/vmlinuz-lts"
INITRD="${BOOT_DIR}/initramfs-lts"
TRACE_EVENTS="${CAPTURE_DIR}/qemu-trace-events"
QEMU_TRACE="${CAPTURE_DIR}/qemu-virtio-gpu.trace"
SERIAL_LOG="${CAPTURE_DIR}/serial.expect.log"
HOST_LOG="${CAPTURE_DIR}/host.log"
MONITOR_SOCK="${LINUX_KDE_MONITOR_SOCK:-${WORK_DIR}/qemu-monitor-${STAMP}.sock}"
INTERACTION_LOG="${CAPTURE_DIR}/linux-kde-interaction-latency.log"
SUMMARY="${CAPTURE_DIR}/summary.txt"
STATUS="${CAPTURE_DIR}/status.txt"
DISPLAY_BACKEND="${LINUX_KDE_QEMU_DISPLAY:-gtk,gl=on,show-cursor=on}"
QEMU_DEVICE="${LINUX_KDE_QEMU_DEVICE:-virtio-vga-gl,xres=1280,yres=800}"
QEMU_INPUT_DEVICE="${LINUX_KDE_QEMU_INPUT_DEVICE:-virtio-tablet-pci}"
SAMPLE_SOURCE="${LINUX_KDE_SAMPLE_SOURCE:-screendump}"
QEMU_MEMORY="${LINUX_KDE_QEMU_MEMORY:-8192}"
QEMU_SMP="${LINUX_KDE_QEMU_SMP:-4}"
WIDTH="${LINUX_KDE_WIDTH:-1280}"
HEIGHT="${LINUX_KDE_HEIGHT:-800}"
HOVER_TIMEOUT_MS="${LINUX_KDE_HOVER_TIMEOUT_MS:-2500}"
TRAY_TIMEOUT_MS="${LINUX_KDE_TRAY_TIMEOUT_MS:-5000}"
INTERVAL_MS="${LINUX_KDE_SAMPLE_INTERVAL_MS:-100}"
VISIBLE_ATTEMPTS="${LINUX_KDE_VISIBLE_ATTEMPTS:-120}"
VISIBLE_INTERVAL_MS="${LINUX_KDE_VISIBLE_INTERVAL_MS:-500}"
VISIBLE_MIN_NONBLACK="${LINUX_KDE_VISIBLE_MIN_NONBLACK:-1}"

mkdir -p "${CAPTURE_DIR}" "${WORK_DIR}" "${BOOT_DIR}"

need()
{
    command -v "$1" >/dev/null 2>&1 || {
        echo "linux-kde-interaction-proof: missing required tool: $1" >&2
        exit 1
    }
}

need expect
need qemu-system-x86_64
need curl
need nc
need python3

if [[ ! -f "${ISO}" ]]; then
    echo "linux-kde-interaction-proof: downloading ${ISO_URL}" | tee -a "${HOST_LOG}"
    curl -fL "${ISO_URL}" -o "${ISO}"
fi

if [[ ! -f "${KERNEL}" || ! -f "${INITRD}" ]]; then
    need xorriso
    echo "linux-kde-interaction-proof: extracting Alpine kernel/initrd" | tee -a "${HOST_LOG}"
    rm -rf "${BOOT_DIR}"
    mkdir -p "${BOOT_DIR}"
    xorriso -osirrox on -indev "${ISO}" \
        -extract /boot/vmlinuz-lts "${KERNEL}" \
        -extract /boot/initramfs-lts "${INITRD}" \
        >"${CAPTURE_DIR}/xorriso-extract.log" 2>&1
fi

cat >"${TRACE_EVENTS}" <<'EOF'
virtio_gpu_cmd_ctx_submit
virtio_gpu_cmd_set_scanout
virtio_gpu_cmd_res_flush
virtio_gpu_cmd_res_create_3d
virtio_gpu_cmd_res_xfer_toh_3d
virtio_gpu_fence_ctrl
virtio_gpu_fence_resp
EOF

cat >"${CAPTURE_DIR}/ppm_stats.py" <<'PY'
#!/usr/bin/env python3
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
label = sys.argv[2] if len(sys.argv) > 2 else path.stem
data = path.read_bytes()
if not data.startswith(b"P6"):
    raise SystemExit(f"phase=sample action={label} status=FAIL reason=not-p6")

pos = 2
tokens = []
while len(tokens) < 3:
    while pos < len(data) and data[pos] in b" \t\r\n":
        pos += 1
    if pos < len(data) and data[pos] == ord("#"):
        while pos < len(data) and data[pos] not in b"\r\n":
            pos += 1
        continue
    start = pos
    while pos < len(data) and data[pos] not in b" \t\r\n":
        pos += 1
    tokens.append(data[start:pos])
while pos < len(data) and data[pos] in b" \t\r\n":
    pos += 1

width, height, maxval = [int(x) for x in tokens]
pix = data[pos:]
if maxval != 255 or len(pix) < width * height * 3:
    raise SystemExit(f"phase=sample action={label} status=FAIL reason=bad-ppm width={width} height={height} maxval={maxval} bytes={len(pix)}")

pix = pix[: width * height * 3]
h = 0xCBF29CE484222325
for b in pix[::97] + pix[-4096::31]:
    h ^= b
    h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
nonblack = 0
nonzero = 0
for i in range(0, len(pix), 3):
    r, g, b = pix[i], pix[i + 1], pix[i + 2]
    if r or g or b:
        nonzero += 1
    if r > 8 or g > 8 or b > 8:
        nonblack += 1
cx = width // 2
cy = height // 2
off = (cy * width + cx) * 3
center = (0xFF << 24) | (pix[off] << 16) | (pix[off + 1] << 8) | pix[off + 2]
print(f"phase=sample action={label} status=PASS hash=0x{h:016x} nonzero={nonzero} nonblack={nonblack} center=0x{center:08x} width={width} height={height}")
PY
chmod +x "${CAPTURE_DIR}/ppm_stats.py"

cat >"${CAPTURE_DIR}/run-linux-kde-interaction.expect" <<EOF
#!/usr/bin/expect -f
set timeout 2400
match_max 5000000
log_file -a "${SERIAL_LOG}"

set interaction_log "${INTERACTION_LOG}"
set status_file "${STATUS}"
set monitor_sock "${MONITOR_SOCK}"
set capture_dir "${CAPTURE_DIR}"
set ppm_stats "${CAPTURE_DIR}/ppm_stats.py"
set width ${WIDTH}
set height ${HEIGHT}
set hover_timeout_ms ${HOVER_TIMEOUT_MS}
set tray_timeout_ms ${TRAY_TIMEOUT_MS}
set interval_ms ${INTERVAL_MS}
set sample_source "${SAMPLE_SOURCE}"

proc ilog {line} {
    global interaction_log
    set fh [open \$interaction_log a]
    puts \$fh \$line
    close \$fh
}

proc monitor_cmd {cmd {out_path ""}} {
    global monitor_sock capture_dir
    set log_path "\$capture_dir/monitor-command.log"
    set cmd_path "\$capture_dir/monitor-command-[pid]-[clock microseconds].txt"
    set fh [open \$cmd_path w]
    puts \$fh \$cmd
    close \$fh
    set rc 0
    set err ""
    if {\$out_path ne ""} {
        set rc [catch { exec nc -U -w 8 -q 1 \$monitor_sock < \$cmd_path > "\$out_path.nc.log" 2>@1 } err]
    } else {
        set rc [catch { exec nc -U -w 4 -q 1 \$monitor_sock < \$cmd_path >> \$log_path 2>@1 } err]
    }
    file delete -force \$cmd_path
    if {\$rc != 0} {
        ilog "phase=monitor action=command status=FAIL cmd=[string map {\n { } \r { }} \$cmd] error=[string map {\n { } \r { }} \$err]"
    }
}

proc sample_once {label} {
    global sample_source
    if {\$sample_source eq "grim"} {
        return [guest_sample_once \$label]
    }

    global capture_dir ppm_stats
    set path "\$capture_dir/\$label.ppm"
    set start_ms [clock milliseconds]
    monitor_cmd "screendump \$path" "\$path"
    set helper_elapsed_ms [expr {[clock milliseconds] - \$start_ms}]
    if {![file exists \$path] || [file size \$path] <= 0} {
        ilog "phase=sample action=\$label status=FAIL reason=missing-screendump helper_elapsed_ms=\$helper_elapsed_ms"
        return [list missing -1 missing -1]
    }
    set stats "missing"
    if {[catch { exec python3 \$ppm_stats \$path \$label } stats]} {
        ilog "phase=sample action=\$label status=FAIL reason=ppm-stats helper_elapsed_ms=\$helper_elapsed_ms error=[string map {\n { } \r { }} \$stats]"
        return [list missing -1 missing -1]
    }
    append stats " helper_elapsed_ms=\$helper_elapsed_ms path=\$path"
    ilog \$stats
    set hash missing
    set nonblack -1
    set center missing
    set nonzero -1
    regexp {hash=(0x[0-9a-fA-F]+)} \$stats -> hash
    regexp {nonblack=([0-9]+)} \$stats -> nonblack
    regexp {center=(0x[0-9a-fA-F]+)} \$stats -> center
    regexp {nonzero=([0-9]+)} \$stats -> nonzero
    return [list \$hash \$nonblack \$center \$nonzero]
}

proc guest_sample_once {label} {
    global timeout
    set old_timeout \$timeout
    set timeout 30
    set marker "LSAMPLE[clock microseconds]"
    set start_ms [clock milliseconds]
    set stats ""
    set fail ""
    send -- "/root/sample-screen.sh \$label; echo \$marker\r"
    expect {
        -re {(phase=sample action=[^\r\n]+)} {
            set stats \$expect_out(1,string)
            exp_continue
        }
        -re {LINUX_KDE_SAMPLE_FAIL[^\r\n]*} {
            set fail \$expect_out(0,string)
            exp_continue
        }
        -re \$marker {}
        timeout {
            set timeout \$old_timeout
            set helper_elapsed_ms [expr {[clock milliseconds] - \$start_ms}]
            ilog "phase=sample action=\$label status=FAIL reason=guest-sample-timeout helper_elapsed_ms=\$helper_elapsed_ms"
            return [list missing -1 missing -1]
        }
    }
    set timeout \$old_timeout
    set helper_elapsed_ms [expr {[clock milliseconds] - \$start_ms}]
    if {\$stats eq ""} {
        ilog "phase=sample action=\$label status=FAIL reason=guest-sample-missing helper_elapsed_ms=\$helper_elapsed_ms detail=[string map {\n { } \r { }} \$fail]"
        return [list missing -1 missing -1]
    }
    append stats " helper_elapsed_ms=\$helper_elapsed_ms sample_source=grim"
    ilog \$stats
    set hash missing
    set nonblack -1
    set center missing
    set nonzero -1
    regexp {hash=(0x[0-9a-fA-F]+)} \$stats -> hash
    regexp {nonblack=([0-9]+)} \$stats -> nonblack
    regexp {center=(0x[0-9a-fA-F]+)} \$stats -> center
    regexp {nonzero=([0-9]+)} \$stats -> nonzero
    return [list \$hash \$nonblack \$center \$nonzero]
}

proc measure_visible {label attempts delay_ms min_nonblack} {
    set start_ms [clock milliseconds]
    set first_nonzero_ms -1
    set samples 0
    set last [list missing -1 missing -1]
    for {set i 0} {\$i < \$attempts} {incr i} {
        incr samples
        set last [sample_once "\${label}_\$i"]
        set nonblack [lindex \$last 1]
        set nonzero [lindex \$last 3]
        if {\$first_nonzero_ms < 0 && \$nonzero > 0} {
            set first_nonzero_ms [expr {[clock milliseconds] - \$start_ms}]
        }
        if {\$nonblack >= \$min_nonblack} {
            set elapsed [expr {[clock milliseconds] - \$start_ms}]
            ilog "phase=startup action=\$label status=visible min_nonblack=\$min_nonblack first_visible_ms=\$elapsed first_nonzero_ms=\$first_nonzero_ms samples=\$samples interval_ms=\$delay_ms last_hash=[lindex \$last 0] last_nonzero=\$nonzero last_nonblack=\$nonblack last_center=[lindex \$last 2]"
            return
        }
        after \$delay_ms
    }
    set elapsed [expr {[clock milliseconds] - \$start_ms}]
    ilog "phase=startup action=\$label status=timeout min_nonblack=\$min_nonblack elapsed_ms=\$elapsed first_nonzero_ms=\$first_nonzero_ms samples=\$samples interval_ms=\$delay_ms last_hash=[lindex \$last 0] last_nonzero=[lindex \$last 3] last_nonblack=[lindex \$last 1] last_center=[lindex \$last 2]"
}

proc do_action {kind x y} {
    if {\$kind eq "move"} {
        monitor_cmd "mouse_move \$x \$y"
    } elseif {\$kind eq "click"} {
        monitor_cmd "mouse_move \$x \$y"
        after 80
        monitor_cmd "mouse_button 1"
        after 80
        monitor_cmd "mouse_button 0"
    } elseif {\$kind eq "esc"} {
        monitor_cmd "sendkey esc"
    }
}

proc measure_action {phase action_label x y kind timeout_ms interval_ms} {
    set before [sample_once "\${phase}_\${action_label}_before"]
    set before_hash [lindex \$before 0]
    set action_start_ms [clock milliseconds]
    do_action \$kind \$x \$y
    set visual_start_ms [clock milliseconds]
    set action_elapsed_ms [expr {\$visual_start_ms - \$action_start_ms}]
    set first_changed_ms -1
    set first_changed_visual_ms -1
    set first_hash missing
    set first_nonblack -1
    set first_center missing
    set last_hash missing
    set last_nonblack -1
    set last_center missing
    set samples 0
    set deadline [expr {\$visual_start_ms + \$timeout_ms}]
    while {[clock milliseconds] <= \$deadline} {
        after \$interval_ms
        incr samples
        set cur [sample_once "\${phase}_\${action_label}_\$samples"]
        set last_hash [lindex \$cur 0]
        set last_nonblack [lindex \$cur 1]
        set last_center [lindex \$cur 2]
        if {\$before_hash ne "missing" && \$last_hash ne "missing" && \$last_hash ne \$before_hash} {
            set first_changed_ms [expr {[clock milliseconds] - \$action_start_ms}]
            set first_changed_visual_ms [expr {[clock milliseconds] - \$visual_start_ms}]
            set first_hash \$last_hash
            set first_nonblack \$last_nonblack
            set first_center \$last_center
            break
        }
    }
    set end_ms [expr {[clock milliseconds] - \$action_start_ms}]
    set status [expr {\$first_changed_ms >= 0 ? "changed" : "no-change"}]
    ilog "phase=\$phase action=\$action_label status=\$status x=\$x y=\$y action_kind=\$kind action_elapsed_ms=\$action_elapsed_ms first_changed_ms=\$first_changed_ms first_changed_visual_ms=\$first_changed_visual_ms end_ms=\$end_ms samples=\$samples interval_ms=\$interval_ms timeout_ms=\$timeout_ms before_hash=\$before_hash first_hash=\$first_hash first_nonblack=\$first_nonblack first_center=\$first_center last_hash=\$last_hash last_nonblack=\$last_nonblack last_center=\$last_center"
}

proc cmd {s {t 900}} {
    set timeout \$t
    send -- "\$s\r"
    expect {
        -re "LINUX_KDE_CTRL# " {}
        timeout { puts "LINUX_KDE_INTERACTION_FAIL command-timeout command=\$s"; exit 4 }
    }
}

spawn qemu-system-x86_64 \\
  -enable-kvm -cpu host -smp ${QEMU_SMP} -m "${QEMU_MEMORY}" \\
  -kernel "${KERNEL}" -initrd "${INITRD}" \\
  -append "modules=loop,squashfs,sd-mod,usb-storage quiet console=ttyS0" \\
  -cdrom "${ISO}" -boot d \\
  -vga none -device "${QEMU_DEVICE}" \\
  -device "${QEMU_INPUT_DEVICE}" \\
  -display "${DISPLAY_BACKEND}" \\
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \\
  -serial stdio -monitor "unix:${MONITOR_SOCK},server,nowait" -no-reboot \\
  -trace "events=${TRACE_EVENTS},file=${QEMU_TRACE}"

expect {
  -re "localhost login:" { send "root\r" }
  -re "alpine login:" { send "root\r" }
  timeout { puts "LINUX_KDE_INTERACTION_FAIL login-timeout"; exit 2 }
}
expect {
  -re "# " {}
  timeout { puts "LINUX_KDE_INTERACTION_FAIL root-prompt-timeout"; exit 3 }
}

send -- "export PS1='LINUX_KDE_CTRL# '\r"
expect -re "LINUX_KDE_CTRL# "
cmd "date -Iseconds; uname -a; cat /etc/alpine-release" 60
cmd "ip link set eth0 up || true; udhcpc -i eth0 -q -n || true; ip addr show eth0" 120
cmd "printf '%s\\n' https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/main https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/community > /etc/apk/repositories; apk update" 300
cmd "mount -o remount,size=6G / || true; apk add --no-cache eudev dbus dbus-x11 elogind polkit polkit-elogind polkit-kde-agent-1 udisks2 plasma-workspace plasma-desktop kwin kscreen kactivitymanagerd kde-cli-tools kded plasma-integration qqc2-desktop-style xwayland konsole grim mesa-dri-gallium mesa-egl mesa-gbm mesa-gl mesa-gles mesa-utils mesa-demos libinput hwdata font-dejavu" 2400
cmd "modprobe virtio_gpu || true; mkdir -p /run/dbus /run/user/0 /tmp/.X11-unix; chmod 700 /run/user/0; dbus-daemon --system --fork || true; ls -l /dev/dri || true" 120
cmd {cat > /root/konsole-marker.sh <<'SH'
#!/bin/sh
printf 'linux-konsole-marker start_ms=%s pid=%s ppid=%s\n' "\$(cut -d' ' -f1 /proc/uptime)" "\$\$" "\$PPID" > /tmp/linux-konsole-ready
exec /bin/sh -i
SH
chmod +x /root/konsole-marker.sh
cat > /root/sample-screen.sh <<'SH'
#!/bin/sh
set -u
label="\${1:-sample}"
safe="\$(printf '%s' "\$label" | tr -c 'A-Za-z0-9_.-' '_')"
out="/tmp/linux-kde-sample-\${safe}.ppm"
log="/tmp/linux-kde-sample-\${safe}.log"
export XDG_RUNTIME_DIR=/run/user/0
export WAYLAND_DISPLAY=wayland-0
if ! grim -t ppm "\$out" >"\$log" 2>&1; then
    printf 'LINUX_KDE_SAMPLE_FAIL action=%s reason=grim log=%s\n' "\$label" "\$(tr '\n\r' '  ' < "\$log" 2>/dev/null)"
    exit 0
fi
python3 - "\$out" "\$label" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
label = sys.argv[2]
data = path.read_bytes()
if not data.startswith(b"P6"):
    print(f"phase=sample action={label} status=FAIL reason=not-p6")
    raise SystemExit(0)

pos = 2
tokens = []
while len(tokens) < 3:
    while pos < len(data) and data[pos] in b" \t\r\n":
        pos += 1
    if pos < len(data) and data[pos] == ord("#"):
        while pos < len(data) and data[pos] not in b"\r\n":
            pos += 1
        continue
    start = pos
    while pos < len(data) and data[pos] not in b" \t\r\n":
        pos += 1
    tokens.append(data[start:pos])
while pos < len(data) and data[pos] in b" \t\r\n":
    pos += 1

width, height, maxval = [int(x) for x in tokens]
pix = data[pos:pos + width * height * 3]
if maxval != 255 or len(pix) < width * height * 3:
    print(f"phase=sample action={label} status=FAIL reason=bad-ppm width={width} height={height} maxval={maxval} bytes={len(pix)}")
    raise SystemExit(0)

h = 0xCBF29CE484222325
for b in pix[::97] + pix[-4096::31]:
    h ^= b
    h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
nonblack = 0
nonzero = 0
for i in range(0, len(pix), 3):
    r, g, b = pix[i], pix[i + 1], pix[i + 2]
    if r or g or b:
        nonzero += 1
    if r > 8 or g > 8 or b > 8:
        nonblack += 1
cx = width // 2
cy = height // 2
off = (cy * width + cx) * 3
center = (0xFF << 24) | (pix[off] << 16) | (pix[off + 1] << 8) | pix[off + 2]
print(f"phase=sample action={label} status=PASS hash=0x{h:016x} nonzero={nonzero} nonblack={nonblack} center=0x{center:08x} width={width} height={height}")
PY
SH
chmod +x /root/sample-screen.sh
cat > /root/run-kde-interaction.sh <<'SH'
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
mkdir -p /run/user/0 /tmp/.X11-unix /root/Desktop
chmod 700 /run/user/0
cat > /root/Desktop/Chromium.desktop <<'D'
[Desktop Entry]
Type=Application
Name=Chromium
Exec=true
Icon=applications-internet
D
cat > /root/Desktop/Dolphin.desktop <<'D'
[Desktop Entry]
Type=Application
Name=Dolphin
Exec=true
Icon=system-file-manager
D
cat > /root/Desktop/KWrite.desktop <<'D'
[Desktop Entry]
Type=Application
Name=KWrite
Exec=true
Icon=accessories-text-editor
D
cat > /root/Desktop/Konsole.desktop <<'D'
[Desktop Entry]
Type=Application
Name=Konsole
Exec=konsole
Icon=utilities-terminal
D
dbus-daemon --session --fork --address=unix:path=/tmp/kde-session-bus --print-address=1 >/tmp/kde-session-bus.addr
printf 'LINUX_KDE_METRIC start_uptime_s='; cut -d' ' -f1 /proc/uptime
kwin_wayland --drm --xwayland --no-lockscreen >/tmp/kwin-wayland.log 2>&1 &
seq 1 300 | while read ignored; do [ -S /run/user/0/wayland-0 ] && break; sleep 0.1; done
export WAYLAND_DISPLAY=wayland-0
printf 'LINUX_KDE_METRIC wayland_socket_uptime_s='; cut -d' ' -f1 /proc/uptime
(kactivitymanagerd >/tmp/kactivitymanagerd.log 2>&1 || true) &
(kded6 >/tmp/kded6.log 2>&1 || true) &
plasmashell >/tmp/plasmashell.log 2>&1 &
seq 1 300 | while read ignored; do pidof plasmashell >/dev/null 2>&1 && break; sleep 0.1; done
printf 'LINUX_KDE_METRIC plasmashell_uptime_s='; cut -d' ' -f1 /proc/uptime
seq 1 80 | while read ignored; do [ -S /tmp/.X11-unix/X0 ] && break; sleep 0.1; done
printf 'LINUX_KDE_METRIC xwayland_uptime_s='; cut -d' ' -f1 /proc/uptime
echo LINUX_KDE_READY
sleep 3600
SH
chmod +x /root/run-kde-interaction.sh} 60
cmd "stty -echo" 30
set timeout 1200
send -- "/root/run-kde-interaction.sh &\r"
expect {
  -re "LINUX_KDE_READY" {}
  timeout { puts "LINUX_KDE_INTERACTION_FAIL kde-ready-timeout"; exit 5 }
}
expect -re "LINUX_KDE_CTRL# "

ilog "phase=meta reducer=linux-kde-interaction-latency hover_timeout_ms=${HOVER_TIMEOUT_MS} tray_timeout_ms=${TRAY_TIMEOUT_MS} interval_ms=${INTERVAL_MS} visible_min_nonblack=${VISIBLE_MIN_NONBLACK} visible_attempts=${VISIBLE_ATTEMPTS} visible_interval_ms=${VISIBLE_INTERVAL_MS} input_source=qemu-monitor"
measure_visible "desktop-interaction-visible" ${VISIBLE_ATTEMPTS} ${VISIBLE_INTERVAL_MS} ${VISIBLE_MIN_NONBLACK}
after 1500
sample_once "desktop-interaction-before"

measure_action hover chromium 3000 3300 move \$hover_timeout_ms \$interval_ms
measure_action hover dolphin 8900 3300 move \$hover_timeout_ms \$interval_ms
measure_action hover kwrite 14850 3300 move \$hover_timeout_ms \$interval_ms
measure_action hover konsole 20800 3300 move \$hover_timeout_ms \$interval_ms
measure_action tray open 60000 64400 click \$tray_timeout_ms \$interval_ms
after 500
measure_action tray close 60000 64400 esc \$tray_timeout_ms \$interval_ms
sample_once "desktop-interaction-after"

set direct_start [clock milliseconds]
ilog "phase=direct-launch action=konsole status=start start_ms=\$direct_start"
set direct_cmd {rm -f /tmp/linux-konsole-ready; export HOME=/root USER=root LOGNAME=root XDG_RUNTIME_DIR=/run/user/0 XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=KDE XDG_SESSION_DESKTOP=KDE KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 QT_QPA_PLATFORM=wayland GALLIUM_DRIVER=virgl DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/kde-session-bus WAYLAND_DISPLAY=wayland-0; konsole --separate --workdir /root -e /root/konsole-marker.sh >/tmp/linux-konsole.log 2>&1 & kp=\$!; i=0; while test "\$i" -lt 450; do test -s /tmp/linux-konsole-ready && break; i=\$((i+1)); sleep 0.1; done; test -s /tmp/linux-konsole-ready && echo LINUX_KDE_DIRECT_PASS pid=\$kp || echo LINUX_KDE_DIRECT_FAIL pid=\$kp; cat /tmp/linux-konsole-ready 2>/dev/null || true; echo LINUX_KDE_DIRECT_DONE}
send -- "\$direct_cmd\r"
set direct_status "timeout"
expect {
  -re "LINUX_KDE_DIRECT_PASS" { set direct_status "PASS"; exp_continue }
  -re "LINUX_KDE_DIRECT_FAIL" { set direct_status "FAIL"; exp_continue }
  -re "LINUX_KDE_DIRECT_DONE" {}
  timeout { set direct_status "timeout" }
}
set direct_elapsed [expr {[clock milliseconds] - \$direct_start}]
ilog "phase=direct-launch action=konsole status=\$direct_status elapsed_ms=\$direct_elapsed konsole_wait_ms=\$direct_elapsed"

set fh [open "${STATUS}" w]
if {\$direct_status eq "PASS"} {
    puts \$fh "status_code=0"
    puts \$fh "label=LINUX-KDE-INTERACTION-DONE"
    close \$fh
} else {
    puts \$fh "status_code=6"
    puts \$fh "label=LINUX-KDE-INTERACTION-FAIL direct-launch"
    close \$fh
    exit 6
}
puts "LINUX_KDE_INTERACTION_DONE"
exit 0
EOF
chmod +x "${CAPTURE_DIR}/run-linux-kde-interaction.expect"

echo "linux-kde-interaction-proof: capture dir ${CAPTURE_DIR}" | tee -a "${HOST_LOG}"
if [[ "${LINUX_KDE_INTERACTION_GENERATE_ONLY:-0}" == "1" ]]; then
    {
        echo "status_code=0"
        echo "label=LINUX-KDE-INTERACTION-GENERATED"
    } >"${STATUS}"
    echo "linux-kde-interaction-proof: generated ${CAPTURE_DIR}/run-linux-kde-interaction.expect"
    exit 0
fi

set +e
"${CAPTURE_DIR}/run-linux-kde-interaction.expect" >"${CAPTURE_DIR}/run.expect.host.log" 2>&1
rc=$?
set -e

{
    echo "Linux KDE interaction proof"
    echo "capture_dir=${CAPTURE_DIR}"
    echo "status=${rc}"
    echo "iso=${ISO}"
    echo "qemu_device=${QEMU_DEVICE}"
    echo "qemu_memory=${QEMU_MEMORY}"
    echo "display=${DISPLAY_BACKEND}"
    echo "sample_source=${SAMPLE_SOURCE}"
    echo "interaction_log=${INTERACTION_LOG}"
    echo
    echo "[interaction metrics]"
    [[ -f "${INTERACTION_LOG}" ]] && cat "${INTERACTION_LOG}" || true
    echo
    echo "[guest KDE metrics]"
    grep -E 'LINUX_KDE_(METRIC|WARN|FAIL|READY|DIRECT|INTERACTION)' "${SERIAL_LOG}" || true
    echo
    echo "[qemu virtio-gpu trace counts]"
    for event in virtio_gpu_cmd_ctx_submit virtio_gpu_cmd_set_scanout \
                 virtio_gpu_cmd_res_flush virtio_gpu_fence_ctrl \
                 virtio_gpu_fence_resp virtio_gpu_cmd_res_create_3d \
                 virtio_gpu_cmd_res_xfer_toh_3d; do
        printf '%s %s\n' "${event}" "$(grep -c "${event}" "${QEMU_TRACE}" 2>/dev/null || true)"
    done
} >"${SUMMARY}"

if [[ ! -f "${STATUS}" ]]; then
    {
        echo "status_code=${rc}"
        if [[ ${rc} -eq 0 ]]; then
            echo "label=LINUX-KDE-INTERACTION-DONE"
        else
            echo "label=LINUX-KDE-INTERACTION-FAIL rc=${rc}"
        fi
    } >"${STATUS}"
fi

echo "linux-kde-interaction-proof: wrote ${SUMMARY}"
exit "${rc}"
