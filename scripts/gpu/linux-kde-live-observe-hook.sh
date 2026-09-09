#!/usr/bin/env bash
# Ready hook for linux-kde-inspect.sh: run the reusable behavior probe in the
# real Plasma Wayland session and retain results through the owned 9p share.
set -Eeuo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
ssh_port=${LINUX_KDE_INSPECT_SSH_PORT:-}
run_out=${LINUX_KDE_INSPECT_OUT:-}
behavior_out=${LINUX_KDE_INSPECT_SHARE:-}
[[ "$ssh_port" =~ ^[1-9][0-9]*$ && -d "$run_out" && -d "$behavior_out" ]] || {
    echo "linux-kde-live-observe-hook: invalid inspection environment" >&2
    exit 2
}

ssh_base=(sshpass -p linux-kde-reference ssh -p "$ssh_port"
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
    -o LogLevel=ERROR -o ConnectTimeout=5 ubuntu@127.0.0.1)
scp_base=(sshpass -p linux-kde-reference scp -P "$ssh_port"
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
    -o LogLevel=ERROR -o ConnectTimeout=5)
remote_probe=/tmp/linux-kde-behavior-probe.sh
"${scp_base[@]}" "$root/scripts/gpu/linux-kde-behavior-probe.sh" \
    "ubuntu@127.0.0.1:$remote_probe"
remote_strace=/tmp/linux-kde-strace
"${scp_base[@]}" /usr/bin/strace "ubuntu@127.0.0.1:$remote_strace"
"${ssh_base[@]}" \
    "sudo install -d -m 0755 /opt/linux-kde-behavior/lib; sudo install -m 0755 '$remote_strace' /opt/linux-kde-behavior/strace; /opt/linux-kde-behavior/strace -V" \
    >"$run_out/strace-stage.txt"

session_env='export XDG_RUNTIME_DIR=/run/user/$(id -u); export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus; userenv=$(systemctl --user show-environment); for key in DISPLAY WAYLAND_DISPLAY XAUTHORITY XDG_SESSION_TYPE; do value=$(printf "%s\n" "$userenv" | sed -n "s/^${key}=//p" | tail -n1); test -n "$value" && export "$key=$value"; done; export XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-wayland}; export QT_QPA_PLATFORM=wayland'
"${ssh_base[@]}" \
    "$session_env; env LINUX_KDE_BEHAVIOR_POWEROFF=0 LINUX_KDE_BEHAVIOR_REQUIRE_STRACE=1 bash '$remote_probe'" \
    | tee "$run_out/behavior-hook.log"
"${ssh_base[@]}" "rm -f '$remote_probe' '$remote_strace'" >/dev/null 2>&1 || true

[[ -f "$behavior_out/STATUS" && "$(<"$behavior_out/STATUS")" == LINUX_KDE_BEHAVIOR_DONE ]] || {
    echo "linux-kde-live-observe-hook: behavior probe did not pass" >&2
    exit 3
}
printf 'LINUX_KDE_LIVE_OBSERVE_DONE evidence=%s\n' "$behavior_out"
