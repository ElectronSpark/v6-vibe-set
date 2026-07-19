#!/usr/bin/env bash
# Launch one disposable, owned Linux/KDE inspection VM and wait for its window
# to close or for the bounded watchdog to stop it.
set -Eeuo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
base=${LINUX_KDE_INSPECT_BASE:-$root/build-x86_64/linux-kde-reference/linux-kde-behavior-20260714T024146Z.qcow2}
timeout_seconds=${LINUX_KDE_INSPECT_TIMEOUT:-1800}
allow_external_qemu=${LINUX_KDE_INSPECT_ALLOW_EXTERNAL_QEMU:-0}
share=${LINUX_KDE_INSPECT_SHARE:-}
ready_hook=${LINUX_KDE_INSPECT_READY_HOOK:-}
exit_after_hook=${LINUX_KDE_INSPECT_EXIT_AFTER_HOOK:-0}
[[ "$timeout_seconds" =~ ^[1-9][0-9]*$ ]] || {
    echo "LINUX_KDE_INSPECT_TIMEOUT must be a positive integer" >&2
    exit 64
}
case "$allow_external_qemu:$exit_after_hook" in
    0:0|0:1|1:0|1:1) ;;
    *)
        echo "LINUX_KDE_INSPECT_ALLOW_EXTERNAL_QEMU and LINUX_KDE_INSPECT_EXIT_AFTER_HOOK must be 0 or 1" >&2
        exit 64
        ;;
esac
[[ -f "$base" ]] || { echo "missing prepared Linux KDE image: $base" >&2; exit 2; }
[[ -z "$share" || -d "$share" ]] || { echo "missing inspection share: $share" >&2; exit 2; }
[[ -z "$ready_hook" || -x "$ready_hook" ]] || { echo "inspection hook is not executable: $ready_hook" >&2; exit 2; }

stamp=$(date -u +%Y%m%dT%H%M%SZ)
token="linux-kde-inspect-$stamp-$$"
out="$root/build-x86_64/linux-kde-inspect/$token"
mkdir -p "$out"
overlay="$out/inspect.qcow2"
pidfile="$out/qemu.pid"
serial="$out/serial.sock"
qemu_log="$out/qemu.log"
qemu-img create -q -f qcow2 -F qcow2 -b "$base" "$overlay"
ssh_port=$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)
printf 'token=%s\nbase=%s\nqemu=/usr/bin/qemu-system-x86_64\nqemu_modules=system\nssh_port=%s\ntimeout_seconds=%s\nallow_external_qemu=%s\n' \
    "$token" "$base" "$ssh_port" "$timeout_seconds" \
    "$allow_external_qemu" >"$out/contract.txt"
if [[ "$allow_external_qemu" == 1 ]]; then
    "$root/scripts/launch/qemu-exact-inventory.sh" \
        >"$out/prelaunch-qemu-inventory.txt"
else
    "$root/scripts/launch/qemu-exact-inventory.sh" --require-zero \
        >"$out/prelaunch-qemu-inventory.txt"
fi

qemu_pid=0
qemu_start=0
watchdog_pid=0
cleanup()
{
    rc=$?
    trap - EXIT INT TERM HUP
    set +e
    if ((watchdog_pid > 0)); then
        kill "$watchdog_pid" 2>/dev/null
        wait "$watchdog_pid" 2>/dev/null
    fi
    if ((qemu_pid > 0)) && [[ -r "/proc/$qemu_pid/stat" ]]; then
        "$root/scripts/launch/cleanup-owned-qemu.sh" \
            "$qemu_pid" "$qemu_start" "$token" >>"$out/cleanup.log" 2>&1
    fi
    if ((qemu_pid > 0)); then
        wait "$qemu_pid" 2>/dev/null
    fi
    "$root/scripts/launch/qemu-exact-inventory.sh" \
        >"$out/final-qemu-inventory.txt"
    printf 'LINUX_KDE_INSPECT_STOPPED rc=%s evidence=%s\n' "$rc" "$out"
    exit "$rc"
}
trap cleanup EXIT INT TERM HUP

export SDL_VIDEODRIVER=x11
export SDL_VIDEO_HIGHDPI_DISABLED=1
export GALLIUM_DRIVER=d3d12
export MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA
export LIBGL_ALWAYS_SOFTWARE=0
share_args=()
if [[ -n "$share" ]]; then
    share_args=(-fsdev "local,id=hostshare,path=$share,security_model=none"
        -device virtio-9p-pci,fsdev=hostshare,mount_tag=hostshare)
fi
setsid env -u LD_PRELOAD -u QEMU_MODULE_DIR /usr/bin/qemu-system-x86_64 \
    -name "xv6-$token" -pidfile "$pidfile" \
    -machine pc,vmport=off,accel=kvm -enable-kvm -cpu host -smp 6 -m 8G \
    -display sdl,gl=on,show-cursor=on,grab-mod=lctrl-lalt \
    -serial "unix:$serial,server=on,wait=off" \
    -device virtio-vga-gl -device virtio-tablet-pci \
    -drive "file=$overlay,if=virtio,format=qcow2" \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$ssh_port-:22" \
    -device virtio-net-pci,netdev=n0,romfile= \
    "${share_args[@]}" \
    -audiodev driver=none,id=noaudio \
    >"$qemu_log" 2>&1 &
qemu_pid=$!
for _ in $(seq 1 100); do
    [[ -s "$pidfile" ]] && break
    sleep 0.05
done
[[ -s "$pidfile" && "$(<"$pidfile")" == "$qemu_pid" ]] || {
    echo "QEMU pidfile mismatch" >&2
    exit 3
}
stat_line=$(<"/proc/$qemu_pid/stat")
read -r -a stat_fields <<<"${stat_line##*) }"
qemu_start=${stat_fields[19]}
[[ "${stat_fields[2]}" == "$qemu_pid" ]] || {
    echo "QEMU process-group mismatch" >&2
    exit 3
}
"$root/scripts/launch/cleanup-owned-qemu.sh" --check-only \
    "$qemu_pid" "$qemu_start" "$token" >"$out/owned-process.txt"

(
    sleep "$timeout_seconds"
    if [[ -r "/proc/$qemu_pid/stat" ]]; then
        "$root/scripts/launch/cleanup-owned-qemu.sh" \
            "$qemu_pid" "$qemu_start" "$token" >>"$out/watchdog.log" 2>&1
    fi
) &
watchdog_pid=$!

title="QEMU (xv6-$token-0)"
for _ in $(seq 1 120); do
    if env XV6_QEMU_WINDOW_FIT_MODE=native \
        XV6_QEMU_WINDOW_TARGET_WIDTH=1280 \
        XV6_QEMU_WINDOW_TARGET_HEIGHT=768 \
        "$root/scripts/gpu/fit-owned-qemu-window.sh" "$title" \
        >"$out/fit-early.txt" 2>&1; then
        break
    fi
    sleep 0.25
done

ssh_base=(sshpass -p linux-kde-reference ssh -p "$ssh_port"
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
    -o LogLevel=ERROR -o ConnectTimeout=3 ubuntu@127.0.0.1)
ready=0
for _ in $(seq 1 240); do
    if "${ssh_base[@]}" \
        'test "$(hostname)" = linux-kde-reference && pidof kwin_wayland >/dev/null && pidof plasmashell >/dev/null' \
        >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 1
done
if ((ready == 0)); then
    printf 'LINUX_KDE_INSPECT_NOT_READY evidence=%s\n' "$out" >&2
    exit 4
fi
session_env='export XDG_RUNTIME_DIR=/run/user/$(id -u); export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus; userenv=$(systemctl --user show-environment); for key in DISPLAY WAYLAND_DISPLAY XAUTHORITY XDG_SESSION_TYPE; do value=$(printf "%s\n" "$userenv" | sed -n "s/^${key}=//p" | tail -n1); test -n "$value" && export "$key=$value"; done; export XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-wayland}'
"${ssh_base[@]}" "$session_env; printf 'hostname=%s\\n' \"\$(hostname)\"; plasmashell --version; kwin_wayland --version; printf 'session_type=%s wayland_display=%s\\n' \"\${XDG_SESSION_TYPE-}\" \"\${WAYLAND_DISPLAY-}\"; pidof kwin_wayland; pidof plasmashell" \
    >"$out/guest-ready.txt" 2>&1
env XV6_QEMU_WINDOW_FIT_MODE=native \
    XV6_QEMU_WINDOW_TARGET_WIDTH=1280 \
    XV6_QEMU_WINDOW_TARGET_HEIGHT=768 \
    "$root/scripts/gpu/fit-owned-qemu-window.sh" "$title" \
    >"$out/fit-ready.txt" 2>&1

printf 'LINUX_KDE_INSPECT_READY title=%s ssh_port=%s timeout_seconds=%s evidence=%s\n' \
    "$title" "$ssh_port" "$timeout_seconds" "$out"
if [[ -n "$ready_hook" ]]; then
    LINUX_KDE_INSPECT_SSH_PORT="$ssh_port" \
        LINUX_KDE_INSPECT_OUT="$out" \
        LINUX_KDE_INSPECT_TITLE="$title" \
        "$ready_hook"
fi
if [[ "$exit_after_hook" == 1 ]]; then
    "${ssh_base[@]}" 'sudo systemctl poweroff' >/dev/null 2>&1 || true
fi
wait "$qemu_pid"
