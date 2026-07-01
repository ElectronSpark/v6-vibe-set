#!/usr/bin/env bash
# Move the host-visible QEMU cursor to a guest absolute tablet coordinate.
#
# This is for harness-driven input injection only.  In the normal xv6 GTK
# cursor policy, QEMU's frontend cursor is the single visible cursor and guest
# hardware cursor image uploads are suppressed by virtio_gpu_host_cursor_only.
# Explicit guest hardware-cursor debugging should disable callers of this helper
# and use QEMU_GTK_CURSOR_MODE=guest instead.
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
        echo "status=FAIL reason=usage"
        exit 2
fi

clamp_coord()
{
        local value="$1"
        if [[ ! "${value}" =~ ^-?[0-9]+$ ]]; then
                value=0
        fi
        if (( value < 0 )); then
                value=0
        elif (( value > 65535 )); then
                value=65535
        fi
        printf '%s\n' "${value}"
}

guest_x="$(clamp_coord "$1")"
guest_y="$(clamp_coord "$2")"
title="${3:-${XV6_QEMU_WINDOW_TITLE:-${QEMU_WINDOW_TITLE:-xv6-os QEMU}}}"

sync_with_windows_cursor()
{
        command -v powershell.exe >/dev/null 2>&1 || return 77
        command -v wslpath >/dev/null 2>&1 || return 77

        local ps1 ps1_win wslenv
        ps1="$(mktemp /tmp/xv6-qemu-host-cursor-sync.XXXXXX.ps1)"
        trap 'rm -f "${ps1:-}"' RETURN
        cat >"${ps1}" <<'PS1'
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Xv6CursorNative {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc,
                                          IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text,
                                           int count);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);
}
"@

$title = $env:XV6_QEMU_WINDOW_TITLE
if ([string]::IsNullOrWhiteSpace($title)) {
    $title = "xv6-os QEMU"
}
$guestX = [int]$env:XV6_CURSOR_X
$guestY = [int]$env:XV6_CURSOR_Y
if ($guestX -lt 0) { $guestX = 0 }
if ($guestX -gt 65535) { $guestX = 65535 }
if ($guestY -lt 0) { $guestY = 0 }
if ($guestY -gt 65535) { $guestY = 65535 }

$found = [IntPtr]::Zero
$callback = [Xv6CursorNative+EnumWindowsProc]{
    param([IntPtr]$hWnd, [IntPtr]$lParam)
    if (-not [Xv6CursorNative]::IsWindowVisible($hWnd)) {
        return $true
    }
    $text = New-Object System.Text.StringBuilder 512
    [void][Xv6CursorNative]::GetWindowText($hWnd, $text, $text.Capacity)
    $name = $text.ToString()
    if ($name.Contains($title) -or
        ($title -eq "xv6-os QEMU" -and $name.Contains("QEMU"))) {
        $script:found = $hWnd
        return $false
    }
    return $true
}
[void][Xv6CursorNative]::EnumWindows($callback, [IntPtr]::Zero)
if ($found -eq [IntPtr]::Zero) {
    Write-Output "status=SKIP backend=windows reason=window-not-found title=$title"
    exit 77
}

$rect = New-Object Xv6CursorNative+RECT
if (-not [Xv6CursorNative]::GetClientRect($found, [ref]$rect)) {
    Write-Output "status=FAIL backend=windows reason=get-client-rect"
    exit 2
}
$origin = New-Object Xv6CursorNative+POINT
$origin.X = 0
$origin.Y = 0
if (-not [Xv6CursorNative]::ClientToScreen($found, [ref]$origin)) {
    Write-Output "status=FAIL backend=windows reason=client-to-screen"
    exit 2
}

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) {
    Write-Output "status=FAIL backend=windows reason=empty-client width=$width height=$height"
    exit 2
}
$screenX = $origin.X + [int][Math]::Round(($guestX / 65535.0) * ($width - 1))
$screenY = $origin.Y + [int][Math]::Round(($guestY / 65535.0) * ($height - 1))
if (-not [Xv6CursorNative]::SetCursorPos($screenX, $screenY)) {
    Write-Output "status=FAIL backend=windows reason=set-cursor-pos screen_x=$screenX screen_y=$screenY"
    exit 2
}
Write-Output "status=PASS backend=windows screen_x=$screenX screen_y=$screenY client_x=$($origin.X) client_y=$($origin.Y) client_w=$width client_h=$height title=$title"
exit 0
PS1

        ps1_win="$(wslpath -w "${ps1}")"
        wslenv="XV6_CURSOR_X:XV6_CURSOR_Y:XV6_QEMU_WINDOW_TITLE"
        if [[ -n "${WSLENV:-}" ]]; then
                wslenv="${wslenv}:${WSLENV}"
        fi
        XV6_CURSOR_X="${guest_x}" \
        XV6_CURSOR_Y="${guest_y}" \
        XV6_QEMU_WINDOW_TITLE="${title}" \
        WSLENV="${wslenv}" \
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${ps1_win}"
}

sync_with_xdotool()
{
        [[ -n "${DISPLAY:-}" ]] || return 77
        command -v xdotool >/dev/null 2>&1 || return 77

        local win geom x y width height screen_x screen_y
        win="$(xdotool search --onlyvisible --name "${title}" 2>/dev/null | tail -n 1 || true)"
        if [[ -z "${win}" && "${title}" == "xv6-os QEMU" ]]; then
                win="$(xdotool search --onlyvisible --name QEMU 2>/dev/null | tail -n 1 || true)"
        fi
        if [[ -z "${win}" ]]; then
                echo "status=SKIP backend=xdotool reason=window-not-found title=${title}"
                return 77
        fi
        geom="$(xdotool getwindowgeometry --shell "${win}" 2>/dev/null || true)"
        eval "${geom}"
        x="${X:-0}"
        y="${Y:-0}"
        width="${WIDTH:-0}"
        height="${HEIGHT:-0}"
        if (( width <= 0 || height <= 0 )); then
                echo "status=FAIL backend=xdotool reason=empty-window width=${width} height=${height}"
                return 2
        fi
        screen_x=$(( x + (guest_x * (width - 1) + 32767) / 65535 ))
        screen_y=$(( y + (guest_y * (height - 1) + 32767) / 65535 ))
        xdotool mousemove "${screen_x}" "${screen_y}"
        echo "status=PASS backend=xdotool screen_x=${screen_x} screen_y=${screen_y} window_x=${x} window_y=${y} window_w=${width} window_h=${height} title=${title}"
}

if sync_with_windows_cursor; then
        exit 0
else
        rc=$?
fi
if [[ "${rc}" != "77" ]]; then
        exit "${rc}"
fi

if sync_with_xdotool; then
        exit 0
else
        rc=$?
fi
if [[ "${rc}" != "77" ]]; then
        exit "${rc}"
fi

echo "status=SKIP reason=no-host-cursor-backend title=${title}"
exit 77
