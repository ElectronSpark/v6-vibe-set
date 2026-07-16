#!/usr/bin/env bash
# Move one exact-title QEMU window wholly inside its Windows monitor work area.
# This never enumerates, signals, or reconfigures a QEMU process.
set -euo pipefail

if [[ $# -ne 1 || -z "$1" ]]; then
        echo "usage: $0 EXACT_WINDOW_TITLE" >&2
        exit 2
fi
if ! command -v powershell.exe >/dev/null 2>&1; then
        echo "fit-owned-qemu-window: powershell.exe unavailable" >&2
        exit 77
fi

title=$1
ps1=$(mktemp /tmp/xv6-fit-qemu-window.XXXXXX.ps1)
trap 'rm -f "$ps1"' EXIT

cat >"$ps1" <<'EOF'
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Xv6FitWindowNative {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback,
                                          IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text,
                                           int count);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after,
                                           int x, int y, int cx, int cy,
                                           uint flags);
    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr hWnd, int command);
    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hWnd);
}
"@

[void][Xv6FitWindowNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$wanted = $env:XV6_QEMU_WINDOW_TITLE
$found = [IntPtr]::Zero
$foundTitle = ""
for ($attempt = 0; $attempt -lt 100 -and $found -eq [IntPtr]::Zero; $attempt++) {
    $callback = [Xv6FitWindowNative+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        if (-not [Xv6FitWindowNative]::IsWindowVisible($hWnd)) { return $true }
        $text = New-Object System.Text.StringBuilder 512
        [void][Xv6FitWindowNative]::GetWindowText(
            $hWnd, $text, $text.Capacity)
        # WSLg may append its own window-state text to QEMU's unique tokenized
        # title.  The complete expected QEMU title must still occur verbatim.
        if ($text.ToString().Contains($wanted)) {
            $script:found = $hWnd
            $script:foundTitle = $text.ToString()
            return $false
        }
        return $true
    }
    [void][Xv6FitWindowNative]::EnumWindows($callback, [IntPtr]::Zero)
    if ($found -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
}
if ($found -eq [IntPtr]::Zero) {
    Write-Output "status=SKIP reason=exact-window-not-found title=$wanted"
    exit 77
}

$rect = New-Object Xv6FitWindowNative+RECT
if (-not [Xv6FitWindowNative]::GetWindowRect($found, [ref]$rect)) {
    Write-Output "status=FAIL reason=get-window-rect title=$wanted"
    exit 2
}
$screen = [System.Windows.Forms.Screen]::FromHandle($found)
$work = $screen.WorkingArea
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$newX = [Math]::Max($work.Left,
                    [Math]::Min($rect.Left, $work.Right - $width))
$newY = [Math]::Max($work.Top,
                    [Math]::Min($rect.Top, $work.Bottom - $height))
$fitMode = $env:XV6_QEMU_WINDOW_FIT_MODE
if ([string]::IsNullOrWhiteSpace($fitMode)) { $fitMode = "clamp" }
if ($fitMode -eq "maximize") {
    if (-not [Xv6FitWindowNative]::ShowWindowAsync($found, 3)) {
        Write-Output "status=FAIL reason=maximize-window title=$wanted"
        exit 2
    }
} elseif ($fitMode -eq "clamp" -and
          ($newX -ne $rect.Left -or $newY -ne $rect.Top)) {
    # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    if (-not [Xv6FitWindowNative]::SetWindowPos(
        $found, [IntPtr]::Zero, $newX, $newY, 0, 0, 0x15)) {
        Write-Output "status=FAIL reason=set-window-pos title=$wanted"
        exit 2
    }
} elseif ($fitMode -ne "clamp") {
    Write-Output "status=FAIL reason=invalid-fit-mode mode=$fitMode title=$wanted"
    exit 2
}
Start-Sleep -Milliseconds 1000
$final = New-Object Xv6FitWindowNative+RECT
if (-not [Xv6FitWindowNative]::GetWindowRect($found, [ref]$final)) {
    Write-Output "status=FAIL reason=final-window-rect title=$wanted"
    exit 2
}
$finalWidth = $final.Right - $final.Left
$finalHeight = $final.Bottom - $final.Top
$inside = $final.Left -ge $work.Left -and $final.Top -ge $work.Top -and `
          $final.Right -le $work.Right -and $final.Bottom -le $work.Bottom
if (-not $inside) {
    Write-Output "status=FAIL reason=outside-workarea mode=$fitMode before=$($rect.Left),$($rect.Top),${width}x${height} final=$($final.Left),$($final.Top),${finalWidth}x${finalHeight} workarea=$($work.Left),$($work.Top),$($work.Width)x$($work.Height) title=$wanted"
    exit 2
}
Write-Output "status=PASS mode=$fitMode dpi=$([Xv6FitWindowNative]::GetDpiForWindow($found)) before=$($rect.Left),$($rect.Top),${width}x${height} final=$($final.Left),$($final.Top),${finalWidth}x${finalHeight} workarea=$($work.Left),$($work.Top),$($work.Width)x$($work.Height) expected_title=$wanted actual_title=$foundTitle"
EOF

ps1_win=$(wslpath -w "$ps1")
wslenv="XV6_QEMU_WINDOW_TITLE:XV6_QEMU_WINDOW_FIT_MODE"
if [[ -n "${WSLENV:-}" ]]; then
        wslenv="${wslenv}:${WSLENV}"
fi
XV6_QEMU_WINDOW_TITLE="$title" WSLENV="$wslenv" \
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$ps1_win"
