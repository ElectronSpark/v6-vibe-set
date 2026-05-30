#!/usr/bin/env bash
set -euo pipefail

require=0
if [[ "${1:-}" == "--require" ]]; then
        require=1
elif [[ $# -gt 0 ]]; then
        echo "usage: $0 [--require]" >&2
        exit 2
fi

missing=0

say_ok() {
        printf 'gui-accel-check: ok: %s\n' "$*"
}

say_warn() {
        printf 'gui-accel-check: warning: %s\n' "$*" >&2
}

check_device_rw() {
        local path="$1"
        [[ -e "${path}" && -r "${path}" && -w "${path}" ]]
}

drm_node_is_known_software() {
        local path="$1"
        local name
        local sysdev

        name="$(basename -- "${path}")"
        sysdev="$(readlink -f "/sys/class/drm/${name}/device" 2>/dev/null || true)"
        case "${sysdev}" in
                */vgem|*/vgem/*)
                        return 0
                        ;;
        esac
        if [[ -r "/sys/class/drm/${name}/device/uevent" ]] &&
           grep -Eq '(^|=)(vgem|VKMS|vkms)($|=)' "/sys/class/drm/${name}/device/uevent" 2>/dev/null; then
                return 0
        fi
        return 1
}

describe_drm_node() {
        local path="$1"
        local name
        local sysdev

        name="$(basename -- "${path}")"
        sysdev="$(readlink -f "/sys/class/drm/${name}/device" 2>/dev/null || true)"
        if [[ -n "${sysdev}" ]]; then
                printf '%s[%s]' "${path}" "${sysdev##*/}"
        else
                printf '%s' "${path}"
        fi
}

wsl_d3d12_available() {
        grep -qi microsoft /proc/version 2>/dev/null || return 1
        check_device_rw /dev/dxg || return 1
        [[ -e /usr/lib/x86_64-linux-gnu/dri/d3d12_dri.so ]] || return 1
        [[ -e /usr/lib/wsl/lib/libd3d12.so ]] || return 1
        [[ -e /usr/lib/wsl/lib/libdxcore.so ]] || return 1
        return 0
}

if check_device_rw /dev/kvm; then
        say_ok "/dev/kvm is available"
elif [[ -e /dev/kvm ]]; then
        say_warn "/dev/kvm exists but is not readable/writable by this user"
        missing=1
else
        say_warn "/dev/kvm is not present; QEMU will use slow TCG"
        missing=1
fi

wsl_d3d12=0
if wsl_d3d12_available; then
        say_ok "WSL D3D12 GPU path is available through /dev/dxg"
        wsl_d3d12=1
elif grep -qi microsoft /proc/version 2>/dev/null; then
        say_warn "WSL detected but /dev/dxg or Mesa d3d12 support is incomplete"
fi

shopt -s nullglob
dri_nodes=(/dev/dri/renderD* /dev/dri/card*)
render_nodes=(/dev/dri/renderD*)
if ((${#dri_nodes[@]} > 0)); then
        usable_dri=0
        software_dri=0
        blocked_render=0
        for node in "${dri_nodes[@]}"; do
                if drm_node_is_known_software "${node}"; then
                        software_dri=1
                        continue
                fi
                if [[ "${node}" == /dev/dri/renderD* ]] && check_device_rw "${node}"; then
                        usable_dri=1
                        break
                fi
        done
        if ((usable_dri)); then
                say_ok "host DRI render acceleration is available: ${render_nodes[*]}"
        else
                for node in "${render_nodes[@]}"; do
                        if ! check_device_rw "${node}"; then
                                blocked_render=1
                        fi
                done
                if ((software_dri)); then
                        say_warn "host DRI nodes are software-only, not hardware acceleration: $(for node in "${dri_nodes[@]}"; do describe_drm_node "${node}"; printf ' '; done)"
                elif ((blocked_render)); then
                        say_warn "host DRI render nodes exist but are not readable/writable: ${render_nodes[*]}"
                else
                        say_warn "host DRI nodes exist, but no usable render node was found: ${dri_nodes[*]}"
                fi
                if ((wsl_d3d12 == 0)); then
                        missing=1
                fi
        fi
else
        say_warn "no host /dev/dri nodes are visible; virgl falls back to software GL"
        if ((wsl_d3d12 == 0)); then
                missing=1
        fi
fi

if check_device_rw /dev/udmabuf; then
        say_ok "/dev/udmabuf is available for virtio-gpu blob resources"
elif [[ -e /dev/udmabuf ]]; then
        say_warn "/dev/udmabuf exists but is not readable/writable"
        if ((wsl_d3d12 == 0)); then
                missing=1
        fi
else
        say_warn "/dev/udmabuf is not present; virtio-gpu blob hostmem is disabled"
        if ((wsl_d3d12 == 0)); then
                missing=1
        fi
fi

if [[ -f /.dockerenv ]]; then
        cat >&2 <<'EOF'
gui-accel-check: Docker launch hint:
  docker run --rm -it \
    --device /dev/kvm \
    --device /dev/dri \
    --device /dev/udmabuf \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -e DISPLAY="$DISPLAY" \
    -e WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
    -v "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}:${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" \
    -e XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
    <image> bash
EOF
fi

if ((missing && require)); then
        exit 1
fi
