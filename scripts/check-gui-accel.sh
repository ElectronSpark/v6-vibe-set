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

if check_device_rw /dev/kvm; then
        say_ok "/dev/kvm is available"
elif [[ -e /dev/kvm ]]; then
        say_warn "/dev/kvm exists but is not readable/writable by this user"
        missing=1
else
        say_warn "/dev/kvm is not present; QEMU will use slow TCG"
        missing=1
fi

shopt -s nullglob
dri_nodes=(/dev/dri/renderD* /dev/dri/card*)
if ((${#dri_nodes[@]} > 0)); then
        usable_dri=0
        for node in "${dri_nodes[@]}"; do
                if check_device_rw "${node}"; then
                        usable_dri=1
                        break
                fi
        done
        if ((usable_dri)); then
                say_ok "host DRI is available: ${dri_nodes[*]}"
        else
                say_warn "host DRI nodes exist but are not readable/writable: ${dri_nodes[*]}"
                missing=1
        fi
else
        say_warn "no host /dev/dri nodes are visible; virgl falls back to software GL"
        missing=1
fi

if check_device_rw /dev/udmabuf; then
        say_ok "/dev/udmabuf is available for virtio-gpu blob resources"
elif [[ -e /dev/udmabuf ]]; then
        say_warn "/dev/udmabuf exists but is not readable/writable"
        missing=1
else
        say_warn "/dev/udmabuf is not present; virtio-gpu blob hostmem is disabled"
        missing=1
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
