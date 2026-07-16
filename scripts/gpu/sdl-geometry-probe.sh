#!/usr/bin/env bash
# Reduce QEMU SDL geometry failures without owning or terminating a VM.
#
# contract: resolve the xv6 launcher with QEMU_DRY_RUN=1 and fail unless the
#           command really selects accelerated SDL at the requested mode.
# observe:  inspect one caller-supplied live QEMU PID/window read-only, retain
#           host geometry evidence, and classify the first numeric divergence.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_WIDTH="${SDL_GEOMETRY_WIDTH:-1280}"
DEFAULT_HEIGHT="${SDL_GEOMETRY_HEIGHT:-800}"

usage() {
    cat >&2 <<'EOF'
usage:
  scripts/gpu/sdl-geometry-probe.sh contract [options]
  scripts/gpu/sdl-geometry-probe.sh observe --pid PID [options]

contract options:
  --out DIR                 evidence directory
  --sdl-driver x11|wayland  SDL host video driver (default: x11)
  --gpu MODEL               QEMU GPU model (default: virtio-vga-gl-primary)
  --host-gl MODE            QEMU host GL mode (default: auto)
  --hidpi on|off            SDL high-DPI flag (default: off)
  --mode WIDTHxHEIGHT       requested guest scanout (default: 1280x800)

observe options:
  --pid PID                 exact live QEMU PID to inspect; required
  --window-id ID            X11 client window id; discovered by _NET_WM_PID
                            when omitted
  --window-title TEXT        Windows QEMU title hint (default: QEMU)
  --guest-mode WIDTHxHEIGHT guest-reported actual scanout
  --guest-content-mode WIDTHxHEIGHT active compositor/virgl scanout
  --guest-pitch BYTES       guest-reported scanout pitch
  --guest-log FILE          copy a guest fbstat/scanout receipt
  --guest-screenshot FILE   copy an independent guest screenshot
  --sdl-trace FILE          copy and parse sdl-geometry-trace output
  --qemu-log FILE           copy and parse patched QEMU aspect-fit receipts
  --out DIR                 evidence directory

The observe command never sends input, monitor commands, or signals. It never
starts, stops, pauses, or reconfigures the supplied QEMU process.
EOF
}

die() {
    echo "sdl-geometry-probe: $*" >&2
    exit 2
}

timestamp() {
    date -u +%Y%m%dT%H%M%SZ
}

parse_mode() {
    local value="$1"
    local -n width_ref="$2"
    local -n height_ref="$3"

    if [[ ! "${value}" =~ ^([1-9][0-9]*)x([1-9][0-9]*)$ ]]; then
        die "invalid mode '${value}' (expected WIDTHxHEIGHT)"
    fi
    width_ref="${BASH_REMATCH[1]}"
    height_ref="${BASH_REMATCH[2]}"
}

write_result() {
    local out="$1"
    local status="$2"
    local owner="$3"
    local detail="$4"

    printf 'status=%s\nowner=%s\ndetail=%s\n' \
        "${status}" "${owner}" "${detail}" >"${out}/result.txt"
    printf 'sdl-geometry-probe: RESULT status=%s owner=%s detail=%s\n' \
        "${status}" "${owner}" "${detail}"
}

host_is_wsl() {
    [[ -n "${WSL_INTEROP:-}" || -n "${WSL_DISTRO_NAME:-}" ]] && return 0
    [[ -r /proc/sys/kernel/osrelease ]] || return 1
    local release
    release="$(</proc/sys/kernel/osrelease)"
    [[ "${release,,}" == *microsoft* ]]
}

capture_host_display_state() {
    local out="$1"

    if command -v xdpyinfo >/dev/null 2>&1; then
        xdpyinfo >"${out}/xdpyinfo.txt" 2>&1 || true
    fi
    if command -v xrandr >/dev/null 2>&1; then
        xrandr --current >"${out}/xrandr-current.txt" 2>&1 || true
        xrandr --listmonitors >"${out}/xrandr-monitors.txt" 2>&1 || true
    fi
    if command -v powershell.exe >/dev/null 2>&1; then
        powershell.exe -NoProfile -Command \
            "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::AllScreens | ForEach-Object { 'device={0} bounds={1} workarea={2} primary={3}' -f \$_.DeviceName,\$_.Bounds,\$_.WorkingArea,\$_.Primary }; Get-ItemProperty 'HKCU:\Control Panel\Desktop' | Select-Object LogPixels,Win8DpiScaling | Format-List" \
            >"${out}/windows-display.txt" 2>&1 || true
    fi
}

contract_probe() {
    local out=""
    local sdl_driver="x11"
    local gpu="virtio-vga-gl-primary"
    local host_gl="auto"
    local hidpi="off"
    local mode="${DEFAULT_WIDTH}x${DEFAULT_HEIGHT}"
    local width height rc command diagnostics status owner detail

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --out)
                [[ $# -ge 2 ]] || die "--out needs a directory"
                out="$2"
                shift 2
                ;;
            --sdl-driver)
                [[ $# -ge 2 ]] || die "--sdl-driver needs a value"
                sdl_driver="$2"
                shift 2
                ;;
            --gpu)
                [[ $# -ge 2 ]] || die "--gpu needs a value"
                gpu="$2"
                shift 2
                ;;
            --host-gl)
                [[ $# -ge 2 ]] || die "--host-gl needs a value"
                host_gl="$2"
                shift 2
                ;;
            --hidpi)
                [[ $# -ge 2 ]] || die "--hidpi needs on or off"
                hidpi="$2"
                shift 2
                ;;
            --mode)
                [[ $# -ge 2 ]] || die "--mode needs WIDTHxHEIGHT"
                mode="$2"
                shift 2
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                die "unknown contract option: $1"
                ;;
        esac
    done

    case "${sdl_driver}" in
        x11|wayland) ;;
        *) die "unsupported SDL driver: ${sdl_driver}" ;;
    esac
    case "${hidpi}" in
        on|off) ;;
        *) die "unsupported high-DPI mode: ${hidpi}" ;;
    esac
    parse_mode "${mode}" width height
    out="${out:-${ROOT}/build-x86_64/sdl-geometry-probe/contract-$(timestamp)}"
    mkdir -p -- "${out}"
    {
        printf 'probe=contract\n'
        printf 'requested_display=sdl\n'
        printf 'requested_sdl_driver=%s\n' "${sdl_driver}"
        printf 'requested_gpu=%s\n' "${gpu}"
        printf 'requested_host_gl=%s\n' "${host_gl}"
        printf 'requested_sdl_hidpi=%s\n' "${hidpi}"
        printf 'requested_guest_mode=%sx%s\n' "${width}" "${height}"
        printf 'requested_cpus=6\n'
        printf 'requested_memory=8G\n'
        printf 'sdl_geometry_trace=off\n'
        printf 'host_is_wsl=%s\n' "$(host_is_wsl && printf 1 || printf 0)"
        printf 'display=%s\n' "${DISPLAY:-unset}"
        printf 'wayland_display=%s\n' "${WAYLAND_DISPLAY:-unset}"
    } >"${out}/request.txt"

    if command -v qemu-system-x86_64 >/dev/null 2>&1; then
        qemu-system-x86_64 --version >"${out}/qemu-version.txt" 2>&1 || true
        qemu-system-x86_64 -display help >"${out}/qemu-display-help.txt" 2>&1 || true
    fi
    if command -v sdl2-config >/dev/null 2>&1; then
        sdl2-config --version >"${out}/sdl-version.txt" 2>&1 || true
    fi
    capture_host_display_state "${out}"

    set +e
    env \
        AUTO_BUILD=0 \
        DISPLAY_MODE=sdl \
        QEMU_DISPLAY_FALLBACK=error \
        QEMU_DISPLAY_REPORT=1 \
        QEMU_ALLOW_WSL_SDL_GL=1 \
        QEMU_WSL_SDL_VIDEODRIVER="${sdl_driver}" \
        QEMU_SDL_HIGHDPI="${hidpi}" \
        QEMU_GPU="${gpu}" \
        QEMU_HOST_GL="${host_gl}" \
        QEMU_VIRTIO_GPU_XRES="${width}" \
        QEMU_VIRTIO_GPU_YRES="${height}" \
        QEMU_INPUT=virtio \
        QEMU_CPUS=6 \
        QEMU_MEMORY=8G \
        QEMU_NET=0 \
        QEMU_AUDIO=none \
        USE_KVM=1 \
        QEMU_REQUIRE_KVM=1 \
        QEMU_DRY_RUN=1 \
        "${ROOT}/scripts/launch/launch-gui.sh" \
        >"${out}/resolved-command.txt" \
        2>"${out}/launcher-diagnostics.txt"
    rc=$?
    set -e

    printf 'launcher_rc=%s\n' "${rc}" >>"${out}/request.txt"
    command="$(<"${out}/resolved-command.txt")"
    diagnostics="$(<"${out}/launcher-diagnostics.txt")"
    status="PASS"
    owner="none"
    detail="resolved-sdl-contract"

    if [[ "${rc}" -ne 0 ]]; then
        status="FAIL"
        owner="launcher"
        detail="dry-run-exit-${rc}"
    elif [[ "${command}" != *"-display sdl,gl=on"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="resolved-command-is-not-sdl-gl"
    elif [[ "${diagnostics}" != *"display-contract requested=sdl resolved=sdl"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="missing-or-mismatched-display-contract"
    elif [[ "${command}" != *"video=${width}x${height}"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="guest-mode-not-resolved"
    elif [[ "${command}" != *"-enable-kvm"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="kvm-not-resolved"
    elif [[ "${command}" != *"-smp 6 -m 8G"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="matched-linux-resources-not-resolved"
    elif host_is_wsl && [[ "${command}" != *"SDL_VIDEODRIVER=${sdl_driver}"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="sdl-driver-not-resolved"
    elif [[ "${hidpi}" == "off" && "${command}" != *"SDL_VIDEO_HIGHDPI_DISABLED=1"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="sdl-highdpi-disable-not-resolved"
    elif [[ "${hidpi}" == "on" && "${command}" == *"SDL_VIDEO_HIGHDPI_DISABLED=1"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="sdl-highdpi-enable-not-resolved"
    elif [[ "${command}" == *"LD_PRELOAD="* ||
            "${diagnostics}" != *"sdl_trace=off"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="acceptance-contract-must-be-trace-off"
    elif [[ "${diagnostics}" != *"sdl_aspect_fix=patched"* ||
            "${command}" != *"/build-x86_64/qemu-sdl/bin/qemu-system-x86_64"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="corrected-sdl-qemu-not-resolved"
    elif host_is_wsl && [[ "${diagnostics}" != *"sdl_fit=maximize"* ||
                           "${command}" != *"-name xv6-sdl-"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="wsl-workarea-fit-or-unique-name-not-resolved"
    elif host_is_wsl && [[ "${gpu}" == *-gl* ]] &&
         [[ "${command}" != *"MESA_LOADER_DRIVER_OVERRIDE=d3d12"* ||
            "${command}" != *"GALLIUM_DRIVER=d3d12"* ]]; then
        status="FAIL"
        owner="launcher"
        detail="wsl-d3d12-environment-not-resolved"
    fi

    write_result "${out}" "${status}" "${owner}" "${detail}"
    printf 'evidence=%s\n' "${out}"
    [[ "${status}" == "PASS" ]]
}

discover_x11_window() {
    local pid="$1"
    local root_list token property

    command -v xprop >/dev/null 2>&1 || return 1
    root_list="$(xprop -root _NET_CLIENT_LIST_STACKING 2>/dev/null || true)"
    root_list="${root_list#*# }"
    root_list="${root_list//,/ }"
    for token in ${root_list}; do
        [[ "${token}" == 0x* ]] || continue
        property="$(xprop -id "${token}" _NET_WM_PID 2>/dev/null || true)"
        if [[ "${property}" == *"= ${pid}" ]]; then
            printf '%s\n' "${token}"
            return 0
        fi
    done
    return 1
}

observe_probe() {
    local pid=""
    local window_id=""
    local window_title="QEMU"
    local guest_mode=""
    local guest_content_mode=""
    local guest_pitch=""
    local guest_log=""
    local guest_screenshot=""
    local sdl_trace=""
    local qemu_log=""
    local out=""
    local expected_width="${DEFAULT_WIDTH}"
    local expected_height="${DEFAULT_HEIGHT}"
    local guest_width=""
    local guest_height=""
    local content_width=""
    local content_height=""
    local exe base stat_line start_ticks
    local xwin="" client_width="" client_height=""
    local windows_client_width="" windows_client_height=""
    local windows_dpi=""
    local sdl_logical_width="" sdl_logical_height=""
    local sdl_drawable_width="" sdl_drawable_height=""
    local fit_guest_width="" fit_guest_height=""
    local fit_window_width="" fit_window_height=""
    local fit_x="" fit_y="" fit_width="" fit_height=""
    local status owner detail screenshot_status="missing"
    local -a cmdline stat_fields

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --pid)
                [[ $# -ge 2 ]] || die "--pid needs a value"
                pid="$2"
                shift 2
                ;;
            --window-id)
                [[ $# -ge 2 ]] || die "--window-id needs a value"
                window_id="$2"
                shift 2
                ;;
            --window-title)
                [[ $# -ge 2 ]] || die "--window-title needs text"
                window_title="$2"
                shift 2
                ;;
            --guest-mode)
                [[ $# -ge 2 ]] || die "--guest-mode needs WIDTHxHEIGHT"
                guest_mode="$2"
                shift 2
                ;;
            --guest-content-mode)
                [[ $# -ge 2 ]] || die "--guest-content-mode needs WIDTHxHEIGHT"
                guest_content_mode="$2"
                shift 2
                ;;
            --guest-pitch)
                [[ $# -ge 2 ]] || die "--guest-pitch needs a value"
                guest_pitch="$2"
                shift 2
                ;;
            --guest-log)
                [[ $# -ge 2 ]] || die "--guest-log needs a file"
                guest_log="$2"
                shift 2
                ;;
            --guest-screenshot)
                [[ $# -ge 2 ]] || die "--guest-screenshot needs a file"
                guest_screenshot="$2"
                shift 2
                ;;
            --sdl-trace)
                [[ $# -ge 2 ]] || die "--sdl-trace needs a file"
                sdl_trace="$2"
                shift 2
                ;;
            --qemu-log)
                [[ $# -ge 2 ]] || die "--qemu-log needs a file"
                qemu_log="$2"
                shift 2
                ;;
            --out)
                [[ $# -ge 2 ]] || die "--out needs a directory"
                out="$2"
                shift 2
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                die "unknown observe option: $1"
                ;;
        esac
    done

    [[ "${pid}" =~ ^[1-9][0-9]*$ ]] || die "observe requires a numeric --pid"
    [[ -r "/proc/${pid}/exe" && -r "/proc/${pid}/cmdline" ]] ||
        die "PID ${pid} is not readable/live"
    exe="$(readlink -f -- "/proc/${pid}/exe")"
    base="${exe##*/}"
    case "${base}" in
        qemu-system-*|qemu-kvm) ;;
        *) die "PID ${pid} is not an exact qemu-system-* or qemu-kvm executable (${exe})" ;;
    esac

    out="${out:-${ROOT}/build-x86_64/sdl-geometry-probe/observe-${pid}-$(timestamp)}"
    mkdir -p -- "${out}"
    mapfile -d '' -t cmdline <"/proc/${pid}/cmdline"
    printf '%q ' "${cmdline[@]}" >"${out}/qemu-command.txt"
    printf '\n' >>"${out}/qemu-command.txt"

    stat_line="$(</proc/${pid}/stat)"
    read -r -a stat_fields <<<"${stat_line##*) }"
    start_ticks="${stat_fields[19]:-unknown}"
    {
        printf 'pid=%s\n' "${pid}"
        printf 'exe=%s\n' "${exe}"
        printf 'start_ticks=%s\n' "${start_ticks}"
        printf 'observation=read-only\n'
        printf 'signals_sent=0\n'
        printf 'monitor_commands_sent=0\n'
        printf 'input_events_sent=0\n'
    } >"${out}/process-identity.txt"

    while IFS= read -r -d '' entry; do
        case "${entry}" in
            DISPLAY=*|WAYLAND_DISPLAY=*|XDG_SESSION_TYPE=*|SDL_VIDEODRIVER=*|MESA_LOADER_DRIVER_OVERRIDE=*|GALLIUM_DRIVER=*|MESA_D3D12_DEFAULT_ADAPTER_NAME=*|LIBGL_ALWAYS_SOFTWARE=*)
                printf '%s\n' "${entry}"
                ;;
        esac
    done <"/proc/${pid}/environ" >"${out}/qemu-display-environment.txt"

    if [[ -z "${window_id}" ]]; then
        window_id="$(discover_x11_window "${pid}" || true)"
    fi
    printf 'window_id=%s\n' "${window_id:-not-found}" >"${out}/window.txt"

    capture_host_display_state "${out}"

    if [[ -n "${window_id}" ]] && command -v xwininfo >/dev/null 2>&1; then
        xwininfo -id "${window_id}" >"${out}/xwininfo.txt" 2>&1 || true
        xwin="$(<"${out}/xwininfo.txt")"
        while IFS= read -r line; do
            case "${line}" in
                *"Width:"*) client_width="${line##*: }" ;;
                *"Height:"*) client_height="${line##*: }" ;;
            esac
        done <<<"${xwin}"
        printf 'client_width=%s\nclient_height=%s\n' \
            "${client_width:-unknown}" "${client_height:-unknown}" >>"${out}/window.txt"

        if command -v import >/dev/null 2>&1; then
            if import -window "${window_id}" "${out}/host-window.png" 2>"${out}/host-screenshot.err"; then
                screenshot_status="png"
            fi
        elif command -v xwd >/dev/null 2>&1; then
            if xwd -silent -id "${window_id}" -out "${out}/host-window.xwd" 2>"${out}/host-screenshot.err"; then
                screenshot_status="xwd"
            fi
        fi
    fi
    printf 'host_screenshot=%s\n' "${screenshot_status}" >>"${out}/window.txt"

    if [[ -x "${ROOT}/scripts/gpu/capture-host-screen.sh" ]]; then
        local windows_capture_rc windows_capture_log token
        set +e
        QEMU_WINDOW_TITLE="${window_title}" \
            "${ROOT}/scripts/gpu/capture-host-screen.sh" \
            "${out}/host-window-windows.png" \
            >"${out}/host-window-windows.log" 2>&1
        windows_capture_rc=$?
        set -e
        windows_capture_log="$(<"${out}/host-window-windows.log")"
        for token in ${windows_capture_log}; do
            case "${token}" in
                client_w=*) windows_client_width="${token#client_w=}" ;;
                client_h=*) windows_client_height="${token#client_h=}" ;;
                window_dpi=*) windows_dpi="${token#window_dpi=}" ;;
            esac
        done
        printf 'windows_capture_rc=%s\nwindows_client_width=%s\nwindows_client_height=%s\nwindows_dpi=%s\n' \
            "${windows_capture_rc}" \
            "${windows_client_width:-unknown}" \
            "${windows_client_height:-unknown}" \
            "${windows_dpi:-unknown}" >>"${out}/window.txt"
    fi

    if [[ -n "${guest_mode}" ]]; then
        parse_mode "${guest_mode}" guest_width guest_height
    fi
    if [[ -n "${guest_content_mode}" ]]; then
        parse_mode "${guest_content_mode}" content_width content_height
    fi
    {
        printf 'expected_mode=%sx%s\n' "${expected_width}" "${expected_height}"
        printf 'guest_mode=%s\n' "${guest_mode:-missing}"
        printf 'guest_content_mode=%s\n' "${guest_content_mode:-missing}"
        printf 'guest_pitch=%s\n' "${guest_pitch:-missing}"
    } >"${out}/guest-evidence.txt"
    if [[ -n "${guest_log}" ]]; then
        [[ -f "${guest_log}" ]] || die "guest log is not a regular file: ${guest_log}"
        cp -- "${guest_log}" "${out}/guest-scanout.log"
    fi
    if [[ -n "${guest_screenshot}" ]]; then
        local guest_screenshot_ext=""
        [[ -f "${guest_screenshot}" ]] || die "guest screenshot is not a regular file: ${guest_screenshot}"
        if [[ "${guest_screenshot##*/}" == *.* ]]; then
            guest_screenshot_ext=".${guest_screenshot##*.}"
        fi
        cp -- "${guest_screenshot}" "${out}/guest-screenshot${guest_screenshot_ext}"
        sha256sum -- "${guest_screenshot}" >"${out}/guest-screenshot.sha256"
    fi
    if [[ -n "${sdl_trace}" ]]; then
        local trace_line
        [[ -f "${sdl_trace}" ]] || die "SDL trace is not a regular file: ${sdl_trace}"
        cp -- "${sdl_trace}" "${out}/sdl-geometry-trace.log"
        while IFS= read -r trace_line; do
            if [[ "${trace_line}" =~ logical=([0-9]+)x([0-9]+).*drawable=([0-9]+)x([0-9]+) ]]; then
                sdl_logical_width="${BASH_REMATCH[1]}"
                sdl_logical_height="${BASH_REMATCH[2]}"
                sdl_drawable_width="${BASH_REMATCH[3]}"
                sdl_drawable_height="${BASH_REMATCH[4]}"
            fi
        done <"${sdl_trace}"
        printf 'sdl_logical_width=%s\nsdl_logical_height=%s\nsdl_drawable_width=%s\nsdl_drawable_height=%s\n' \
            "${sdl_logical_width:-unknown}" \
            "${sdl_logical_height:-unknown}" \
            "${sdl_drawable_width:-unknown}" \
            "${sdl_drawable_height:-unknown}" >>"${out}/window.txt"
    fi
    if [[ -n "${qemu_log}" ]]; then
        local qemu_line
        [[ -f "${qemu_log}" ]] || die "QEMU log is not a regular file: ${qemu_log}"
        cp -- "${qemu_log}" "${out}/qemu-run.log"
        while IFS= read -r qemu_line; do
            if [[ "${qemu_line}" =~ sdl2:\ aspect-fit\ guest=([0-9]+)x([0-9]+)\ window=([0-9]+)x([0-9]+)\ viewport=([0-9]+),([0-9]+)\ ([0-9]+)x([0-9]+) ]]; then
                fit_guest_width="${BASH_REMATCH[1]}"
                fit_guest_height="${BASH_REMATCH[2]}"
                fit_window_width="${BASH_REMATCH[3]}"
                fit_window_height="${BASH_REMATCH[4]}"
                fit_x="${BASH_REMATCH[5]}"
                fit_y="${BASH_REMATCH[6]}"
                fit_width="${BASH_REMATCH[7]}"
                fit_height="${BASH_REMATCH[8]}"
            fi
        done <"${qemu_log}"
        printf 'fit_guest=%sx%s\nfit_window=%sx%s\nfit_viewport=%s,%s %sx%s\n' \
            "${fit_guest_width:-unknown}" "${fit_guest_height:-unknown}" \
            "${fit_window_width:-unknown}" "${fit_window_height:-unknown}" \
            "${fit_x:-unknown}" "${fit_y:-unknown}" \
            "${fit_width:-unknown}" "${fit_height:-unknown}" >>"${out}/window.txt"
    fi

    status="INCOMPLETE"
    owner="evidence"
    detail="guest-mode-or-x11-client-geometry-missing"
    if [[ -n "${guest_width}" && -n "${guest_height}" ]]; then
        if [[ "${guest_width}" != "${expected_width}" ||
              "${guest_height}" != "${expected_height}" ]]; then
            status="FAIL"
            owner="guest-scanout"
            detail="guest-${guest_width}x${guest_height}-expected-${expected_width}x${expected_height}"
        elif [[ -n "${qemu_log}" && -z "${fit_width}" ]]; then
            status="FAIL"
            owner="qemu-sdl-aspect-fit"
            detail="patched-qemu-aspect-receipt-missing"
        elif [[ -n "${fit_width}" ]]; then
            local expected_fit_width expected_fit_height expected_fit_x expected_fit_y
            if ((fit_window_width * guest_height <= fit_window_height * guest_width)); then
                expected_fit_width="${fit_window_width}"
                expected_fit_height=$((fit_window_width * guest_height / guest_width))
            else
                expected_fit_height="${fit_window_height}"
                expected_fit_width=$((fit_window_height * guest_width / guest_height))
            fi
            expected_fit_x=$(((fit_window_width - expected_fit_width) / 2))
            expected_fit_y=$(((fit_window_height - expected_fit_height) / 2))
            if [[ "${fit_guest_width}" != "${guest_width}" ||
                  "${fit_guest_height}" != "${guest_height}" ]]; then
                status="FAIL"
                owner="qemu-sdl-aspect-fit"
                detail="fit-guest-${fit_guest_width}x${fit_guest_height}-actual-${guest_width}x${guest_height}"
            elif [[ "${fit_width}" != "${expected_fit_width}" ||
                    "${fit_height}" != "${expected_fit_height}" ||
                    "${fit_x}" != "${expected_fit_x}" ||
                    "${fit_y}" != "${expected_fit_y}" ]]; then
                status="FAIL"
                owner="qemu-sdl-aspect-fit"
                detail="viewport-${fit_x},${fit_y}-${fit_width}x${fit_height}-expected-${expected_fit_x},${expected_fit_y}-${expected_fit_width}x${expected_fit_height}"
            elif [[ -n "${client_width}" && -n "${client_height}" ]] &&
                 [[ "${client_width}" != "${fit_window_width}" ||
                    "${client_height}" != "${fit_window_height}" ]]; then
                status="FAIL"
                owner="qemu-sdl-window"
                detail="x11-client-${client_width}x${client_height}-qemu-window-${fit_window_width}x${fit_window_height}"
            else
                status="NUMERIC_PASS"
                owner="visual-input-pending"
                detail="aspect-fit-guest-${guest_width}x${guest_height}-viewport-${fit_x},${fit_y}-${fit_width}x${fit_height}-window-${fit_window_width}x${fit_window_height}"
            fi
        elif [[ -n "${sdl_logical_width}" && -n "${sdl_logical_height}" ]] &&
             [[ "${sdl_logical_width}" != "${guest_width}" ||
                "${sdl_logical_height}" != "${guest_height}" ]]; then
            status="FAIL"
            owner="qemu-sdl-window"
            detail="sdl-logical-${sdl_logical_width}x${sdl_logical_height}-guest-${guest_width}x${guest_height}"
        elif [[ -n "${sdl_drawable_width}" && -n "${sdl_drawable_height}" ]] &&
             [[ "${sdl_drawable_width}" != "${sdl_logical_width}" ||
                "${sdl_drawable_height}" != "${sdl_logical_height}" ]]; then
            status="FAIL"
            owner="qemu-sdl-logical-drawable"
            detail="logical-${sdl_logical_width}x${sdl_logical_height}-drawable-${sdl_drawable_width}x${sdl_drawable_height}"
        elif [[ -n "${client_width}" && -n "${client_height}" ]] &&
             [[ "${client_width}" != "${guest_width}" ||
                "${client_height}" != "${guest_height}" ]]; then
            status="FAIL"
            owner="qemu-sdl-window"
            detail="client-${client_width}x${client_height}-guest-${guest_width}x${guest_height}"
        elif [[ -n "${windows_client_width}" && -n "${windows_client_height}" &&
                -n "${content_width}" && -n "${content_height}" ]]; then
            local frame_width=$((windows_client_width - content_width))
            local frame_height=$((windows_client_height - content_height))
            if ((frame_width < 0 || frame_height < 0 ||
                 frame_width > 128 || frame_height > 128)); then
                status="FAIL"
                owner="qemu-sdl-window"
                detail="windows-client-${windows_client_width}x${windows_client_height}-active-content-${content_width}x${content_height}-frame-${frame_width}x${frame_height}"
            else
                status="NUMERIC_PASS"
                owner="visual-input-pending"
                detail="active-content-${content_width}x${content_height}-inside-windows-${windows_client_width}x${windows_client_height}-frame-${frame_width}x${frame_height}"
            fi
        elif [[ -n "${windows_client_width}" && -n "${windows_client_height}" ]]; then
            local dpi_expected_width="${guest_width}"
            local dpi_expected_height="${guest_height}"
            if [[ "${windows_dpi}" =~ ^[1-9][0-9]*$ ]]; then
                dpi_expected_width=$(((guest_width * windows_dpi + 48) / 96))
                dpi_expected_height=$(((guest_height * windows_dpi + 48) / 96))
            fi
            if [[ "${windows_client_width}" != "${dpi_expected_width}" ||
                  "${windows_client_height}" != "${dpi_expected_height}" ]]; then
                status="FAIL"
                owner="wslg-windows-dpi"
                detail="windows-client-${windows_client_width}x${windows_client_height}-expected-${dpi_expected_width}x${dpi_expected_height}-guest-${guest_width}x${guest_height}-dpi-${windows_dpi:-unknown}"
            else
                status="NUMERIC_PASS"
                owner="dpi-visual-input-pending"
                detail="guest-${guest_width}x${guest_height}-physical-${windows_client_width}x${windows_client_height}-dpi-${windows_dpi:-96}"
            fi
        elif [[ -n "${client_width}" && -n "${client_height}" ]] ||
             [[ -n "${windows_client_width}" && -n "${windows_client_height}" ]]; then
            status="NUMERIC_PASS"
            owner="dpi-visual-input-pending"
            detail="guest-and-client-${guest_width}x${guest_height}"
        fi
    fi

    write_result "${out}" "${status}" "${owner}" "${detail}"
    printf 'evidence=%s\n' "${out}"
    [[ "${status}" == "NUMERIC_PASS" ]]
}

[[ $# -gt 0 ]] || {
    usage
    exit 2
}

command="$1"
shift
case "${command}" in
    contract) contract_probe "$@" ;;
    observe) observe_probe "$@" ;;
    -h|--help|help) usage ;;
    *) die "unknown command: ${command}" ;;
esac
