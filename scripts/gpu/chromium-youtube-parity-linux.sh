#!/usr/bin/env bash
# One owned Linux/KDE YouTube parity trial.  The caller repeats this script;
# a trial never pools modes, disks, or browser profiles.
set -Eeuo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mode=${1:-}
case "$mode" in windowed|fullscreen) ;; *)
    echo "usage: $0 <windowed|fullscreen>" >&2; exit 64;;
esac

qemu_variant=${LINUX_YOUTUBE_QEMU_VARIANT:-apt}
apt_modules=${LINUX_YOUTUBE_APT_MODULES:-system}
allow_external_qemu=${LINUX_YOUTUBE_ALLOW_EXTERNAL_QEMU:-0}
recover_cloud_init=${LINUX_YOUTUBE_RECOVER_CLOUD_INIT:-0}
case "$qemu_variant" in apt|patched) ;; *)
    echo "unsupported LINUX_YOUTUBE_QEMU_VARIANT=$qemu_variant" >&2; exit 64;;
esac
case "$apt_modules" in system|corrected) ;; *)
    echo "unsupported LINUX_YOUTUBE_APT_MODULES=$apt_modules" >&2; exit 64;;
esac
case "$allow_external_qemu" in 0|1) ;; *)
    echo "unsupported LINUX_YOUTUBE_ALLOW_EXTERNAL_QEMU=$allow_external_qemu" >&2
    exit 64
    ;;
esac
case "$recover_cloud_init" in 0|1) ;; *)
    echo "unsupported LINUX_YOUTUBE_RECOVER_CLOUD_INIT=$recover_cloud_init" >&2
    exit 64
    ;;
esac
qemu_env=(-u LD_PRELOAD)
qemu_data_args=()
if [[ "$qemu_variant" == apt ]]; then
    qemu=${QEMU_SDL_APT_BIN:-/usr/bin/qemu-system-x86_64}
    if [[ "$apt_modules" == corrected ]]; then
        apt_module_dir=${QEMU_SDL_APT_MODULE_DIR:-$root/build-x86_64/qemu-apt-ui-corrected-modules}
        qemu_env+=("QEMU_MODULE_DIR=$apt_module_dir")
    else
        qemu_env+=(-u QEMU_MODULE_DIR)
    fi
else
    qemu="$root/build-x86_64/qemu-sdl/bin/qemu-system-x86_64"
    qemu_data="$root/build-x86_64/qemu-sdl/share/qemu"
    qemu_data_args=(-L "$qemu_data")
    qemu_env+=(-u QEMU_MODULE_DIR)
    apt_modules=not-applicable
fi
prepared_base=${LINUX_YOUTUBE_PREPARED_BASE:-$root/build-x86_64/linux-kde-reference/linux-kde-behavior-20260714T024146Z.qcow2}
cloud_base="$root/build-x86_64/linux-kde-reference/ubuntu-24.04-server-cloudimg-amd64.img"
if [[ -f "$prepared_base" ]]; then
    base="$prepared_base"
    base_kind=prepared
else
    base="$cloud_base"
    base_kind=official-cloud-bootstrap
fi
chromium_dir=/usr/lib/chromium
probe_dir="$root/rootfs-overlay/share/chromium-youtube-media-probe"
trace_dir="$root/build-x86_64/host-gui-runtime"
"$root/scripts/gpu/build-chromium-pulse-byte-trace.sh" \
    >"$root/build-x86_64/host-gui-runtime/pulse-byte-trace-build.log"
openh264=/usr/lib/x86_64-linux-gnu/libopenh264.so.7
xnvc=/usr/lib/x86_64-linux-gnu/libXNVCtrl.so.0
for required in "$qemu" "$base" "$chromium_dir/chromium" \
    "$probe_dir/manifest.json" "$trace_dir/chromium-pulse-byte-trace-preload.so" \
    "$openh264" "$xnvc"; do
    [[ -e "$required" ]] || { echo "missing prerequisite: $required" >&2; exit 2; }
done
for command in qemu-img genisoimage ssh sshpass python3 powershell.exe; do
    command -v "$command" >/dev/null || { echo "missing command: $command" >&2; exit 2; }
done

if [[ "$allow_external_qemu" == 1 ]]; then
    inventory_before=$("$root/scripts/launch/qemu-exact-inventory.sh")
else
    inventory_before=$("$root/scripts/launch/qemu-wait-natural-zero.sh" 120)
fi
printf '%s\n' "$inventory_before"
[[ "$allow_external_qemu" == 1 || "$inventory_before" == *"exact_qemu_count=0"* ]] || {
    echo "YOUTUBE_PARITY_LINUX status=SKIP reason=preexisting-qemu-timeout"; exit 75;
}

stamp=$(date -u +%Y%m%dT%H%M%SZ)
nonce=$(printf '%032x' "$(( (10#$(date +%s) << 12) ^ $$ ))")
token="linux-yt-${mode}-${stamp}-$$"
out="$root/build-x86_64/youtube-parity/linux-${mode}-${stamp}-$$-${qemu_variant}-${apt_modules}"
mkdir -p "$out"
mkdir -p "$out/host-libs"
install -m 0755 "$openh264" "$out/host-libs/libopenh264.so.7"
install -m 0755 "$xnvc" "$out/host-libs/libXNVCtrl.so.0"
overlay="$out/linux-trial.qcow2"
seed="$out/parity-seed.iso"
wav="$out/qemu-audio.wav"
pidfile="$out/qemu.pid"
qemu_log="$out/qemu.log"
serial_socket="$out/serial.sock"
display_log="$out/display.log"
audio_log="$out/audio.log"
probe_log="$out/probe.log"
touch "$display_log" "$audio_log" "$probe_log"

choose_port() {
    python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}
ssh_port=$(choose_port)
cdp_port=$(choose_port)
[[ "$ssh_port" != "$cdp_port" ]] || cdp_port=$(choose_port)
printf 'guest=linux mode=%s nonce=%s token=%s ssh_port=%s cdp_port=%s\n' \
    "$mode" "$nonce" "$token" "$ssh_port" "$cdp_port" >"$out/contract.txt"
printf 'qemu_variant=%s apt_modules=%s qemu=%s\n' \
    "$qemu_variant" "$apt_modules" "$qemu" >>"$out/contract.txt"
printf 'allow_external_qemu=%s\n' "$allow_external_qemu" >>"$out/contract.txt"
printf 'recover_cloud_init=%s\n' "$recover_cloud_init" >>"$out/contract.txt"
printf 'base_kind=%s base=%s\n' "$base_kind" "$base" >>"$out/contract.txt"
printf '%s\n' "$inventory_before" >"$out/baseline-qemu-inventory.txt"

qemu-img create -q -f qcow2 -F qcow2 -b "$base" "$overlay"
if [[ "$base_kind" == official-cloud-bootstrap ]]; then
    qemu-img resize "$overlay" 40G >/dev/null
fi
mkdir -p "$out/seed"
printf '%s\n' \
    'instance-id: youtube-parity-'"$token" \
    'local-hostname: linux-kde-reference' >"$out/seed/meta-data"
printf '%s\n' \
    '#cloud-config' \
    'users:' \
    '  - default' \
    'chpasswd:' \
    '  expire: false' \
    '  users:' \
    '    - {name: ubuntu, password: linux-kde-reference, type: text}' \
    'package_update: true' \
    'packages:' \
    '  - plasma-desktop' \
    '  - plasma-workspace-wayland' \
    '  - kwin-wayland' \
    '  - sddm' \
    '  - network-manager' \
    '  - konsole' \
    '  - mesa-utils' \
    '  - glmark2-wayland' \
    '  - kde-spectacle' \
    '  - openssh-server' \
    '  - linux-modules-extra-6.8.0-134-generic' \
    '  - pulseaudio-utils' \
    '  - alsa-utils' \
    'write_files:' \
    '  - path: /etc/sddm.conf.d/90-linux-kde-reference.conf' \
    '    permissions: "0644"' \
    '    content: |' \
    '      [Autologin]' \
    '      User=ubuntu' \
    '      Session=plasmawayland' \
    '      Relogin=true' \
    'bootcmd:' \
    '  - [rm, -f, /home/ubuntu/.config/autostart/linux-kde-reference.desktop, /home/ubuntu/.config/autostart/linux-kde-behavior.desktop]' \
    '  - [systemctl, mask, --runtime, linux-kde-reference-timeout.service]' \
    '  - [systemctl, disable, linux-kde-reference-timeout.service]' \
    '  - [systemctl, stop, linux-kde-reference-timeout.service]' \
    'runcmd:' \
    '  - [systemctl, enable, --now, ssh.service]' \
    '  - [systemctl, restart, NetworkManager.service]' \
    '  - [systemctl, set-default, graphical.target]' \
    '  - [systemctl, enable, sddm.service]' \
    '  - [systemctl, restart, sddm.service]' \
    'ssh_pwauth: true' >"$out/seed/user-data"
printf '%s\n' \
    'version: 2' \
    'ethernets:' \
    '  parity:' \
    "    match: {name: 'en*'}" \
    '    dhcp4: true' \
    '    dhcp6: false' >"$out/seed/network-config"
genisoimage -quiet -output "$seed" -volid cidata -joliet -rock \
    "$out/seed/user-data" "$out/seed/meta-data" "$out/seed/network-config"

qemu_pid=0
qemu_start=0
tunnel_pid=0
tunnel_pgid=0
cleanup() {
    rc=$?
    trap - EXIT INT TERM
    set +e
    if ((tunnel_pid > 0)) && [[ -r "/proc/$tunnel_pid/stat" ]]; then
        tunnel_stat=$(<"/proc/$tunnel_pid/stat")
        read -r -a tunnel_fields <<<"${tunnel_stat##*) }"
        if [[ "${tunnel_fields[2]}" == "$tunnel_pgid" && "$tunnel_pgid" == "$tunnel_pid" ]]; then
            kill -TERM -- "-$tunnel_pgid" 2>/dev/null
        fi
    fi
    if ((tunnel_pid > 0)); then wait "$tunnel_pid" 2>/dev/null; fi
    if ((qemu_pid > 0)) && [[ -r "/proc/$qemu_pid/stat" ]]; then
        "$root/scripts/launch/cleanup-owned-qemu.sh" \
            "$qemu_pid" "$qemu_start" "$token" >>"$out/cleanup.log" 2>&1
    fi
    if ((qemu_pid > 0)); then wait "$qemu_pid" 2>/dev/null; fi
    final_inventory=$("$root/scripts/launch/qemu-exact-inventory.sh")
    printf '%s\n' "$final_inventory" >"$out/final-qemu-inventory.txt"
    if [[ "$allow_external_qemu" == 1 && "$final_inventory" != "$inventory_before" ]]; then
        printf '%s\n' \
            'external QEMU inventory changed independently during owned Linux trial' \
            >>"$out/cleanup.log"
    fi
    exit "$rc"
}
trap cleanup EXIT INT TERM

export SDL_VIDEODRIVER=x11 SDL_VIDEO_HIGHDPI_DISABLED=1
export GALLIUM_DRIVER=d3d12 MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA
export LIBGL_ALWAYS_SOFTWARE=0
prelaunch_inventory=$("$root/scripts/launch/qemu-exact-inventory.sh") || {
    printf '%s\n' "$prelaunch_inventory" >"$out/prelaunch-qemu-inventory.txt"
    echo "YOUTUBE_PARITY_LINUX status=SKIP reason=qemu-inventory-failed-before-launch"; exit 75;
}
printf '%s\n' "$prelaunch_inventory" >"$out/prelaunch-qemu-inventory.txt"
if [[ "$prelaunch_inventory" != "$inventory_before" ]]; then
    echo "YOUTUBE_PARITY_LINUX status=SKIP reason=qemu-inventory-changed-before-launch"
    exit 75
fi
setsid env "${qemu_env[@]}" "$qemu" "${qemu_data_args[@]}" \
    -name "xv6-${token}" -pidfile "$pidfile" \
    -machine pc,vmport=off,accel=kvm -enable-kvm -cpu host -smp 6 -m 8G \
    -display sdl,gl=on,show-cursor=on,grab-mod=lctrl-lalt \
    -serial "unix:$serial_socket,server=on,wait=off" \
    -device virtio-vga-gl -device virtio-tablet-pci \
    -drive "file=$overlay,if=virtio,format=qcow2" \
    -drive "file=$seed,media=cdrom,readonly=on,format=raw" \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:${ssh_port}-:22" \
    -device virtio-net-pci,netdev=n0,romfile= \
    -audiodev "wav,id=ytwav0,path=$wav" \
    -device intel-hda -device hda-output,audiodev=ytwav0 \
    -fsdev "local,id=chromium,path=$chromium_dir,security_model=none,readonly=on" \
    -device virtio-9p-pci,fsdev=chromium,mount_tag=hostchromium \
    -fsdev "local,id=ytprobe,path=$probe_dir,security_model=none,readonly=on" \
    -device virtio-9p-pci,fsdev=ytprobe,mount_tag=ytprobe \
    -fsdev "local,id=yttrace,path=$trace_dir,security_model=none,readonly=on" \
    -device virtio-9p-pci,fsdev=yttrace,mount_tag=yttrace \
    -fsdev "local,id=ythostlibs,path=$out/host-libs,security_model=none,readonly=on" \
    -device virtio-9p-pci,fsdev=ythostlibs,mount_tag=ythostlibs \
    >"$qemu_log" 2>&1 &
qemu_pid=$!
for _ in $(seq 1 100); do [[ -s "$pidfile" ]] && break; sleep 0.05; done
[[ -s "$pidfile" && "$(<"$pidfile")" == "$qemu_pid" ]] || {
    echo "QEMU pidfile mismatch" >&2; exit 3;
}
stat_line=$(<"/proc/$qemu_pid/stat")
read -r -a stat_fields <<<"${stat_line##*) }"
qemu_start=${stat_fields[19]}
[[ "${stat_fields[2]}" == "$qemu_pid" ]] || { echo "QEMU PGID mismatch" >&2; exit 3; }
"$root/scripts/launch/cleanup-owned-qemu.sh" --check-only \
    "$qemu_pid" "$qemu_start" "$token" >"$out/owned-process.txt"

title="QEMU (xv6-${token}-0)"
for _ in $(seq 1 40); do
    if QEMU_WINDOW_TITLE="$title" "$root/scripts/gpu/capture-host-screen.sh" \
        "$out/host-window-before-fit.png" >"$out/host-window-before-fit.log" 2>&1; then
        break
    fi
    sleep 0.25
done
fit_mode=${LINUX_YOUTUBE_FIT_MODE:-}
if [[ -z "$fit_mode" ]]; then
    [[ "$qemu_variant" == apt ]] && fit_mode=native || fit_mode=maximize
fi
for _ in $(seq 1 80); do
    if fit=$(
        env XV6_QEMU_WINDOW_FIT_MODE="$fit_mode" \
            XV6_QEMU_WINDOW_TARGET_WIDTH=1280 \
            XV6_QEMU_WINDOW_TARGET_HEIGHT=768 \
            "$root/scripts/gpu/fit-owned-qemu-window.sh" "$title" 2>/dev/null
    ); then
        printf '%s\n' "$fit" >"$out/fit-window.txt"
        break
    fi
    sleep 0.25
done
[[ -s "$out/fit-window.txt" ]] || { echo "SDL window fit failed" >&2; exit 4; }
QEMU_WINDOW_TITLE="$title" "$root/scripts/gpu/capture-host-screen.sh" \
    "$out/host-window-after-fit.png" >"$out/host-window-after-fit.log" 2>&1 || true
if [[ -s "$out/host-window-after-fit.png" ]] && command -v convert >/dev/null 2>&1; then
    convert "$out/host-window-after-fit.png" \
        -format '%[fx:mean] %[fx:standard_deviation] %[fx:maxima] %[colors]\n' \
        info: >"$out/host-window-visual-stats.txt"
fi

ssh_base=(sshpass -p linux-kde-reference ssh -p "$ssh_port" \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR -o ConnectTimeout=3 ubuntu@127.0.0.1)
ssh_ready=0
for _ in $(seq 1 45); do
    if "${ssh_base[@]}" 'test "$(hostname)" = linux-kde-reference' \
        >/dev/null 2>&1; then
        ssh_ready=1
        break
    fi
    sleep 1
done
if ((ssh_ready == 0)); then
    expect "$root/scripts/gpu/linux-kde-serial-prepare.expect" \
        "$serial_socket" "$nonce" >"$out/serial-prepare.log" 2>&1
fi
for _ in $(seq 1 180); do
    if "${ssh_base[@]}" 'test "$(hostname)" = linux-kde-reference' \
        >/dev/null 2>&1; then break; fi
    sleep 1
done
"${ssh_base[@]}" 'test "$(hostname)" = linux-kde-reference'
cloud_rc=0
"${ssh_base[@]}" 'sudo cloud-init status --wait --long' \
    >"$out/cloud-init-status.txt" 2>&1 || cloud_rc=$?
if ((cloud_rc != 0)) || [[ "$(<"$out/cloud-init-status.txt")" != *"status: done"* ]]; then
    if [[ "$recover_cloud_init" != 1 ]]; then
        echo "Linux reference package provisioning failed" >&2
        exit 4
    fi
    "${ssh_base[@]}" \
        'sudo env DEBIAN_FRONTEND=noninteractive dpkg --configure -a && sudo env DEBIAN_FRONTEND=noninteractive apt-get -f install -y && sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y plasma-desktop plasma-workspace-wayland kwin-wayland sddm network-manager konsole mesa-utils glmark2-wayland kde-spectacle openssh-server linux-modules-extra-6.8.0-134-generic pulseaudio-utils alsa-utils && sudo systemctl set-default graphical.target && sudo systemctl enable ssh.service sddm.service && sudo systemctl restart ssh.service sddm.service' \
        >"$out/cloud-init-recovery.txt" 2>&1 || {
            echo "Linux reference package recovery failed" >&2
            exit 4
        }
    printf 'status=recovered\n' >"$out/cloud-init-recovery-status.txt"
fi
"${ssh_base[@]}" 'sync'
sleep 5
setsid sshpass -p linux-kde-reference ssh -N -p "$ssh_port" \
    -L "127.0.0.1:${cdp_port}:127.0.0.1:9222" \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR -o ExitOnForwardFailure=yes -o ServerAliveInterval=5 \
    ubuntu@127.0.0.1 >"$out/cdp-tunnel.log" 2>&1 &
tunnel_pid=$!
for _ in $(seq 1 100); do
    [[ -r "/proc/$tunnel_pid/stat" ]] || break
    tunnel_stat=$(<"/proc/$tunnel_pid/stat")
    read -r -a tunnel_fields <<<"${tunnel_stat##*) }"
    tunnel_pgid=${tunnel_fields[2]}
    [[ "$tunnel_pgid" == "$tunnel_pid" ]] && break
    sleep 0.05
done
[[ "$tunnel_pgid" == "$tunnel_pid" && -r "/proc/$tunnel_pid/stat" ]] || {
    echo "CDP SSH tunnel ownership check failed" >&2; exit 4;
}

guest_root="/tmp/youtube-parity-$nonce"
"${ssh_base[@]}" "sudo mkdir -p /mnt/hostchromium /mnt/ytprobe /mnt/yttrace /mnt/ythostlibs; sudo mount -t 9p -o trans=virtio,version=9p2000.L,ro hostchromium /mnt/hostchromium; sudo mount -t 9p -o trans=virtio,version=9p2000.L,ro ytprobe /mnt/ytprobe; sudo mount -t 9p -o trans=virtio,version=9p2000.L,ro yttrace /mnt/yttrace; sudo mount -t 9p -o trans=virtio,version=9p2000.L,ro ythostlibs /mnt/ythostlibs; rm -rf '$guest_root'; mkdir -p '$guest_root'"
"${ssh_base[@]}" 'LD_LIBRARY_PATH=/mnt/ythostlibs ldd /mnt/hostchromium/chromium; ldd /mnt/yttrace/chromium-pulse-byte-trace-preload.so' \
    >"$out/chromium-ldd.txt"
if [[ "$(<"$out/chromium-ldd.txt")" == *"not found"* ]]; then
    echo "matched Chromium still has unresolved guest libraries" >&2
    exit 5
fi

session_env='export XDG_RUNTIME_DIR=/run/user/$(id -u); export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus; userenv=$(systemctl --user show-environment); for key in DISPLAY WAYLAND_DISPLAY XAUTHORITY; do value=$(printf "%s\n" "$userenv" | sed -n "s/^${key}=//p" | tail -n1); test -n "$value" && export "$key=$value"; done'
"${ssh_base[@]}" "$session_env; sudo modprobe snd_hda_intel 2>&1 || true; systemctl --user restart pipewire.service pipewire-pulse.service wireplumber.service 2>&1 || true; sleep 3; printf '%s\\n' '=== lspci audio ==='; lspci -nnk | sed -n '/Audio/,/^$/p'; printf '%s\\n' '=== aplay ==='; aplay -l 2>&1 || true; printf '%s\\n' '=== sinks ==='; pactl list short sinks 2>&1 || true" \
    >"$out/audio-device.txt"
renderer=
for _ in $(seq 1 120); do
    renderer=$("${ssh_base[@]}" "$session_env; glxinfo -B 2>&1 || true")
    [[ "$renderer" == *"virgl (D3D12 (NVIDIA"* ]] && break
    sleep 1
done
printf '%s\n' "$renderer" >"$out/renderer.txt"
[[ "$renderer" == *"virgl (D3D12 (NVIDIA"* ]] || {
    echo "renderer is not virgl/NVIDIA D3D12" >&2; exit 5;
}
QEMU_WINDOW_TITLE="$title" "$root/scripts/gpu/capture-host-screen.sh" \
    "$out/host-window-kde-ready.png" >"$out/host-window-kde-ready.log" 2>&1 || true
if [[ -s "$out/host-window-kde-ready.png" ]] && command -v convert >/dev/null 2>&1; then
    convert "$out/host-window-kde-ready.png" \
        -format '%[fx:mean] %[fx:standard_deviation] %[fx:maxima] %[colors]\n' \
        info: >"$out/host-window-kde-ready-visual-stats.txt"
fi
env XV6_QEMU_WINDOW_FIT_MODE="$fit_mode" \
    XV6_QEMU_WINDOW_TARGET_WIDTH=1280 \
    XV6_QEMU_WINDOW_TARGET_HEIGHT=768 \
    "$root/scripts/gpu/fit-owned-qemu-window.sh" "$title" \
    >"$out/fit-window-kde-ready.txt"
QEMU_WINDOW_TITLE="$title" "$root/scripts/gpu/capture-host-screen.sh" \
    "$out/host-window-kde-ready-fitted.png" \
    >"$out/host-window-kde-ready-fitted.log" 2>&1 || true

url="https://www.youtube.com/watch?v=aqz-KE-bpKQ&vq=hd720&autoplay=1&xv6ytmode=$mode#xv6ytprobe=$nonce&xv6ythd720=1"
# The xv6 image has no accepted microphone-capture workload.  Match that
# boundary here: keep real Pulse output enabled, but omit Chromium's otherwise
# unused Chrome-wide AEC output mixer so normal media-element replacement does
# not add a capture-only stream lifecycle to either side of the comparison.
launch="$session_env; export CHROME_LOG_FILE='$guest_root/chrome.log' XV6_PULSE_TRACE=1 XV6_PULSE_TRACE_LOG='$guest_root/pulse.log'; export LD_LIBRARY_PATH=/mnt/ythostlibs LD_PRELOAD=/mnt/yttrace/chromium-pulse-byte-trace-preload.so; nohup /mnt/hostchromium/chromium --user-data-dir='$guest_root/profile' --no-first-run --no-default-browser-check --ozone-platform=wayland --enable-logging=stderr --v=1 --autoplay-policy=no-user-gesture-required --disable-features=ChromeWideEchoCancellation --remote-debugging-address=0.0.0.0 --remote-debugging-port=9222 --load-extension=/mnt/ytprobe '$url' >'$guest_root/launch.log' 2>&1 & echo \$!"
chrome_pid=$("${ssh_base[@]}" "$launch" | tail -n 1)
[[ "$chrome_pid" =~ ^[1-9][0-9]*$ ]] || { echo "Chromium launch failed" >&2; exit 6; }

if [[ "$mode" == fullscreen ]]; then
    action='(async()=>{const v=document.querySelector("video");const p=document.getElementById("movie_player");if(!v||!p)return {ready:false};const owns=()=>{const f=document.fullscreenElement;return !!f&&(f===p||f.contains(p)||p.contains(f));};v.muted=false;v.volume=0.5;await v.play();if(document.fullscreenElement&&!owns())await document.exitFullscreen();if(!owns()){const b=p.querySelector(".ytp-fullscreen-button");if(b)b.click();}for(let i=0;i<20&&!owns();i++)await new Promise(r=>setTimeout(r,100));return {ready:true,documentFullscreen:!!document.fullscreenElement,playerFullscreen:owns(),videoPlaying:!v.paused&&!v.ended,width:v.videoWidth,height:v.videoHeight};})()'
else
    action='(async()=>{const v=document.querySelector("video");const p=document.getElementById("movie_player");if(!v||!p)return {ready:false};if(document.fullscreenElement)await document.exitFullscreen();v.muted=false;v.volume=0.5;await v.play();return {ready:true,documentFullscreen:!!document.fullscreenElement,playerFullscreen:false,videoPlaying:!v.paused&&!v.ended,width:v.videoWidth,height:v.videoHeight};})()'
fi
cdp_ready=0
for _ in $(seq 1 18); do
    if python3 "$root/scripts/gpu/chromium-cdp-eval.py" --port "$cdp_port" \
        --expression "$action" --timeout 5 >"$out/cdp-action.json" && \
        python3 - "$mode" "$out/cdp-action.json" <<'PY'
import json, sys
with open(sys.argv[2], encoding="utf-8") as source:
    value = json.load(source).get("value") or {}
requested = sys.argv[1]
actual_fullscreen = bool(value.get("documentFullscreen")) and bool(value.get("playerFullscreen"))
ok = bool(value.get("ready")) and bool(value.get("videoPlaying"))
ok = ok and (actual_fullscreen if requested == "fullscreen" else not actual_fullscreen)
raise SystemExit(0 if ok else 1)
PY
    then
        cdp_ready=1
        break
    fi
    sleep 1
done
if ((cdp_ready == 0)); then
    "${ssh_base[@]}" "cat '$guest_root/launch.log' 2>/dev/null || true" \
        >"$out/chromium-launch.log"
    "${ssh_base[@]}" "cat '$guest_root/chrome.log' 2>/dev/null || true" \
        >"$out/probe.log"
    "${ssh_base[@]}" 'ps -u 1000 -o pid=,stat=,comm=,args=; ss -lntp 2>/dev/null || true' \
        >"$out/chromium-processes.txt"
    python3 - "$cdp_port" >"$out/cdp-targets.json" 2>&1 <<'PY' || true
import json, sys, urllib.request
with urllib.request.urlopen(f"http://127.0.0.1:{sys.argv[1]}/json", timeout=3) as response:
    print(json.dumps(json.load(response), separators=(",", ":")))
PY
    echo "Chromium CDP target/display mode did not become ready" >&2
    exit 6
fi
env XV6_QEMU_WINDOW_FIT_MODE="$fit_mode" \
    XV6_QEMU_WINDOW_TARGET_WIDTH=1280 \
    XV6_QEMU_WINDOW_TARGET_HEIGHT=768 \
    "$root/scripts/gpu/fit-owned-qemu-window.sh" "$title" \
    >"$out/fit-window-youtube-ready.txt"
QEMU_WINDOW_TITLE="$title" "$root/scripts/gpu/capture-host-screen.sh" \
    "$out/host-window-youtube-ready.png" >"$out/host-window-youtube-ready.log" 2>&1 || true
if [[ -s "$out/host-window-youtube-ready.png" ]] && command -v convert >/dev/null 2>&1; then
    convert "$out/host-window-youtube-ready.png" \
        -format '%[fx:mean] %[fx:standard_deviation] %[fx:maxima] %[colors]\n' \
        info: >"$out/host-window-youtube-ready-visual-stats.txt"
fi

state_expr='(()=>{const v=document.querySelector("video");const p=document.getElementById("movie_player");const f=document.fullscreenElement;const owns=!!p&&!!f&&(f===p||f.contains(p)||p.contains(f));return {documentFullscreen:!!f,playerFullscreen:owns,videoPlaying:!!v&&!v.paused&&!v.ended,width:v?v.videoWidth:0,height:v?v.videoHeight:0};})()'
snapshot_display() {
    local json now
    now=$(date +%s%3N)
    json=$(python3 "$root/scripts/gpu/chromium-cdp-eval.py" --port "$cdp_port" \
        --expression "$state_expr" --timeout 10)
    python3 -c 'import json,sys; d=json.loads(sys.argv[1])["value"]; expected=sys.argv[3]=="fullscreen"; actual=bool(d["documentFullscreen"]) and bool(d["playerFullscreen"]); valid=bool(d["videoPlaying"]) and d["width"]==1280 and d["height"]==720 and actual==expected; valid or sys.exit(1); print("YT_DISPLAY_STATE_V1 monotonic_ms=%s mode=%s document_fullscreen=%d player_fullscreen=%d video_playing=%d video_width=%d video_height=%d"%(sys.argv[2],sys.argv[3],d["documentFullscreen"],d["playerFullscreen"],d["videoPlaying"],d["width"],d["height"]))' \
        "$json" "$now" "$mode" >>"$display_log"
}

trace_snapshot() {
    local phase=$1
    local trace="$out/pulse-$phase.log" snd="$out/alsa-$phase.txt"
    "${ssh_base[@]}" "cat '$guest_root/pulse.log' 2>/dev/null || true" >"$trace"
    "${ssh_base[@]}" 'for f in /proc/asound/card*/pcm*p/sub*/status; do test -r "$f" || continue; printf "FILE %s\n" "$f"; cat "$f"; done' >"$snd"
    local stream_count stream_bytes hw_frames hw_bytes sink sink_null
    read -r stream_count stream_bytes < <(awk '
      /event=stream_write/ {id=""; bytes=""; for(i=1;i<=NF;i++){if($i~/^stream_id=/){sub(/^stream_id=/,"",$i);id=$i};if($i~/^bytes=/){sub(/^bytes=/,"",$i);bytes=$i}}; if(id!=""&&bytes~/^[0-9]+$/){seen[id]=1;sum+=bytes}}
      END{n=0;for(i in seen)n++;print n+0,sum+0}' "$trace")
    hw_frames=$(awk '/hw_ptr/ {gsub(/[^0-9]/,"",$NF); if($NF!="")v=$NF} END{print v+0}' "$snd")
    hw_bytes=$((hw_frames * 4))
    sink=$("${ssh_base[@]}" 'XDG_RUNTIME_DIR=/run/user/1000 pactl get-default-sink 2>/dev/null || true' | tail -n 1)
    sink_null=0
    case "${sink,,}" in ""|*null*|*dummy*|*auto_null*) sink_null=1;; esac
    printf 'YT_AUDIO_STATE_V1 phase=%s sink_null=%s stream_count=%s stream_bytes=%s hw_bytes=%s\n' \
        "$phase" "$sink_null" "$stream_count" "$stream_bytes" "$hw_bytes" >>"$audio_log"
}

for _ in $(seq 1 20); do
    snapshot_display || true
    "${ssh_base[@]}" "test -s '$guest_root/pulse.log'" >/dev/null 2>&1 && break
    sleep 1
done
wav_before=$(stat -c %s "$wav" 2>/dev/null || printf 0)
trace_snapshot before

done_seen=0
for _ in $(seq 1 45); do
    sleep 1
    snapshot_display
    if "${ssh_base[@]}" "grep -q 'YT_MEDIA_PROBE_V1 kind=done schema=1 nonce=$nonce status=complete' '$guest_root/launch.log'" 2>/dev/null; then
        done_seen=1
        break
    fi
done
if ((done_seen == 0)); then
    "${ssh_base[@]}" "cat '$guest_root/launch.log' 2>/dev/null || true" \
        >"$probe_log"
    echo "media probe did not complete" >&2
    exit 7
fi
snapshot_display
trace_snapshot after
wav_after=$(stat -c %s "$wav" 2>/dev/null || printf 0)
"${ssh_base[@]}" "cat '$guest_root/launch.log'" >"$probe_log"
"${ssh_base[@]}" "cat '$guest_root/launch.log'" >"$out/chromium-launch.log"

expect "$root/scripts/gpu/chromium-youtube-parity-validate.tcl" \
    "$probe_log" "$nonce" "$mode" "$display_log" "$audio_log" \
    "$wav_before" "$wav_after" linux | tee "$out/result.txt"
printf 'YOUTUBE_PARITY_LINUX status=PASS evidence=%s\n' "$out"
