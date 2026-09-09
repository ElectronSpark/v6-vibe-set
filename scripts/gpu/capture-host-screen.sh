#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
        echo "status=FAIL reason=usage"
        exit 2
fi

out_path=$1
out_dir=$(dirname "$out_path")
mkdir -p "$out_dir"

if ! command -v powershell.exe >/dev/null 2>&1 ||
   ! command -v wslpath >/dev/null 2>&1; then
        echo "status=SKIP reason=no-powershell"
        exit 77
fi

ps1=$(mktemp /tmp/xv6-host-capture.XXXXXX.ps1)
trap 'rm -f "$ps1"' EXIT

cat >"$ps1" <<'EOF'
param([string]$OutputPath)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Xv6CaptureNative {
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

    [StructLayout(LayoutKind.Sequential)]
    public struct CURSORINFO {
        public int cbSize;
        public int flags;
        public IntPtr hCursor;
        public POINT ptScreenPos;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc,
                                          IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr hWndParent,
                                               EnumWindowsProc lpEnumFunc,
                                               IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text,
                                           int count);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")]
    public static extern bool GetCursorInfo(out CURSORINFO pci);
    [DllImport("user32.dll")]
    public static extern bool DrawIconEx(IntPtr hdc, int xLeft, int yTop,
                                         IntPtr hIcon, int cxWidth,
                                         int cyHeight, int istepIfAniCur,
                                         IntPtr hbrFlickerFreeDraw,
                                         int diFlags);
    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hWnd);
}
"@

# WSLg reports Win32 window coordinates in the caller's DPI coordinate space,
# while CopyFromScreen samples physical desktop pixels.  Make this thread
# per-monitor-v2 aware before asking for either geometry, otherwise a 125%
# desktop turns an actual 1280x800 client into a misleading 1024x640 capture
# whose origin is displaced by the same scale factor.
$previousDpiContext = [Xv6CaptureNative]::SetThreadDpiAwarenessContext(
    [IntPtr](-4))

$title = $env:XV6_QEMU_WINDOW_TITLE
if ([string]::IsNullOrWhiteSpace($title)) {
    $title = "xv6-os QEMU"
}

$found = [IntPtr]::Zero
$callback = [Xv6CaptureNative+EnumWindowsProc]{
    param([IntPtr]$hWnd, [IntPtr]$lParam)
    if (-not [Xv6CaptureNative]::IsWindowVisible($hWnd)) {
        return $true
    }
    $text = New-Object System.Text.StringBuilder 512
    [void][Xv6CaptureNative]::GetWindowText($hWnd, $text, $text.Capacity)
    $name = $text.ToString()
    if ($name.Contains($title) -or
        ($title -eq "xv6-os QEMU" -and $name.Contains("QEMU"))) {
        $script:found = $hWnd
        return $false
    }
    return $true
}
[void][Xv6CaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
if ($found -eq [IntPtr]::Zero) {
    Write-Output "status=SKIP reason=window-not-found title=$title"
    exit 77
}

$rect = New-Object Xv6CaptureNative+RECT
if (-not [Xv6CaptureNative]::GetClientRect($found, [ref]$rect)) {
    Write-Output "status=FAIL reason=get-client-rect"
    exit 2
}
$origin = New-Object Xv6CaptureNative+POINT
$origin.X = 0
$origin.Y = 0
if (-not [Xv6CaptureNative]::ClientToScreen($found, [ref]$origin)) {
    Write-Output "status=FAIL reason=client-to-screen"
    exit 2
}

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$windowDpi = [Xv6CaptureNative]::GetDpiForWindow($found)
if ($width -le 0 -or $height -le 0) {
    Write-Output "status=FAIL reason=empty-client width=$width height=$height"
    exit 2
}

$displayX = $origin.X
$displayY = $origin.Y
$displayW = $width
$displayH = $height
$displaySource = "client"
$script:bestChildArea = 0
$script:bestChildRect = $null
$childCallback = [Xv6CaptureNative+EnumWindowsProc]{
    param([IntPtr]$hWnd, [IntPtr]$lParam)
    if (-not [Xv6CaptureNative]::IsWindowVisible($hWnd)) {
        return $true
    }
    $childRect = New-Object Xv6CaptureNative+RECT
    if (-not [Xv6CaptureNative]::GetWindowRect($hWnd, [ref]$childRect)) {
        return $true
    }
    $childW = $childRect.Right - $childRect.Left
    $childH = $childRect.Bottom - $childRect.Top
    if ($childW -lt 320 -or $childH -lt 240) {
        return $true
    }
    $area = $childW * $childH
    if ($area -gt $script:bestChildArea) {
        $script:bestChildArea = $area
        $script:bestChildRect = $childRect
    }
    return $true
}
[void][Xv6CaptureNative]::EnumChildWindows($found, $childCallback,
                                           [IntPtr]::Zero)
if ($script:bestChildRect -ne $null) {
    $displayX = $script:bestChildRect.Left
    $displayY = $script:bestChildRect.Top
    $displayW = $script:bestChildRect.Right - $script:bestChildRect.Left
    $displayH = $script:bestChildRect.Bottom - $script:bestChildRect.Top
    $displaySource = "child-window"
}

$bitmap = New-Object System.Drawing.Bitmap `
    $displayW, $displayH, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($displayX, $displayY, 0, 0, $bitmap.Size)

$cursor = New-Object Xv6CaptureNative+CURSORINFO
$cursor.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($cursor)
$cursorInClient = 0
$cursorX = -1
$cursorY = -1
if ([Xv6CaptureNative]::GetCursorInfo([ref]$cursor) -and
    (($cursor.flags -band 1) -ne 0)) {
    $cursorX = $cursor.ptScreenPos.X - $displayX
    $cursorY = $cursor.ptScreenPos.Y - $displayY
    if ($cursorX -ge 0 -and $cursorX -lt $displayW -and
        $cursorY -ge 0 -and $cursorY -lt $displayH) {
        $cursorInClient = 1
        $hdc = $graphics.GetHdc()
        try {
            [void][Xv6CaptureNative]::DrawIconEx(
                $hdc, $cursorX, $cursorY, $cursor.hCursor, 0, 0, 0,
                [IntPtr]::Zero, 3)
        } finally {
            $graphics.ReleaseHdc($hdc)
        }
    }
}

$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()

Write-Output "status=PASS path=$OutputPath dpi_awareness=per-monitor-v2 previous_dpi_context=$previousDpiContext window_dpi=$windowDpi client_x=$($origin.X) client_y=$($origin.Y) client_w=$width client_h=$height display_source=$displaySource display_x=$displayX display_y=$displayY display_w=$displayW display_h=$displayH cursor_in_client=$cursorInClient cursor_x=$cursorX cursor_y=$cursorY title=$title"
exit 0
EOF

out_win=$(wslpath -w "$out_path")
ps1_win=$(wslpath -w "$ps1")
wslenv="XV6_QEMU_WINDOW_TITLE"
if [[ -n "${WSLENV:-}" ]]; then
        wslenv="${wslenv}:${WSLENV}"
fi

XV6_QEMU_WINDOW_TITLE="${QEMU_WINDOW_TITLE:-xv6-os QEMU}" \
WSLENV="$wslenv" \
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$ps1_win" "$out_win"
