#!/usr/bin/env bash
# Fit one exact-title QEMU window inside its Windows monitor work area. Native
# mode also makes its client area match the requested guest scanout exactly.
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
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
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
$client = New-Object Xv6FitWindowNative+RECT
if (-not [Xv6FitWindowNative]::GetClientRect($found, [ref]$client)) {
    Write-Output "status=FAIL reason=get-client-rect title=$wanted"
    exit 2
}
$clientWidth = $client.Right - $client.Left
$clientHeight = $client.Bottom - $client.Top
$changed = $false
$fitMode = $env:XV6_QEMU_WINDOW_FIT_MODE
if ([string]::IsNullOrWhiteSpace($fitMode)) { $fitMode = "clamp" }
if ($fitMode -eq "maximize") {
    if (-not [Xv6FitWindowNative]::ShowWindowAsync($found, 3)) {
        Write-Output "status=FAIL reason=maximize-window title=$wanted"
        exit 2
    }
    $changed = $true
} elseif ($fitMode -eq "native") {
    $targetWidth = 0
    $targetHeight = 0
    if (-not [int]::TryParse($env:XV6_QEMU_WINDOW_TARGET_WIDTH,
                            [ref]$targetWidth) -or
        -not [int]::TryParse($env:XV6_QEMU_WINDOW_TARGET_HEIGHT,
                            [ref]$targetHeight) -or
        $targetWidth -le 0 -or $targetHeight -le 0) {
        Write-Output "status=FAIL reason=invalid-native-target title=$wanted"
        exit 2
    }
    $targetOuterWidth = $targetWidth + ($width - $clientWidth)
    $targetOuterHeight = $targetHeight + ($height - $clientHeight)
    if ($targetOuterWidth -gt $work.Width -or
        $targetOuterHeight -gt $work.Height) {
        Write-Output "status=FAIL reason=native-target-exceeds-workarea target_client=${targetWidth}x${targetHeight} target_outer=${targetOuterWidth}x${targetOuterHeight} workarea=$($work.Width)x$($work.Height) title=$wanted"
        exit 2
    }
    $newX = [Math]::Max($work.Left,
                        [Math]::Min($rect.Left,
                                    $work.Right - $targetOuterWidth))
    $newY = [Math]::Max($work.Top,
                        [Math]::Min($rect.Top,
                                    $work.Bottom - $targetOuterHeight))
    # SWP_NOZORDER | SWP_NOACTIVATE. Resize the outer frame by the measured
    # decoration delta so the DPI-aware Win32 client is exactly the guest mode.
    if ($clientWidth -ne $targetWidth -or
        $clientHeight -ne $targetHeight -or
        $rect.Left -ne $newX -or $rect.Top -ne $newY) {
        if (-not [Xv6FitWindowNative]::SetWindowPos(
            $found, [IntPtr]::Zero, $newX, $newY,
            $targetOuterWidth, $targetOuterHeight, 0x14)) {
            Write-Output "status=FAIL reason=set-native-window-pos title=$wanted"
            exit 2
        }
        $changed = $true
    }
} elseif ($fitMode -eq "clamp") {
    $newX = [Math]::Max($work.Left,
                        [Math]::Min($rect.Left, $work.Right - $width))
    $newY = [Math]::Max($work.Top,
                        [Math]::Min($rect.Top, $work.Bottom - $height))
    if ($newX -eq $rect.Left -and $newY -eq $rect.Top) {
        # Already wholly inside the work area.
    # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    } elseif (-not [Xv6FitWindowNative]::SetWindowPos(
        $found, [IntPtr]::Zero, $newX, $newY, 0, 0, 0x15)) {
        Write-Output "status=FAIL reason=set-window-pos title=$wanted"
        exit 2
    } else {
        $changed = $true
    }
} else {
    Write-Output "status=FAIL reason=invalid-fit-mode mode=$fitMode title=$wanted"
    exit 2
}
if ($changed) {
    Start-Sleep -Milliseconds 1000
} else {
    Start-Sleep -Milliseconds 100
}
$final = New-Object Xv6FitWindowNative+RECT
if (-not [Xv6FitWindowNative]::GetWindowRect($found, [ref]$final)) {
    Write-Output "status=FAIL reason=final-window-rect title=$wanted"
    exit 2
}
$finalWidth = $final.Right - $final.Left
$finalHeight = $final.Bottom - $final.Top
$finalClient = New-Object Xv6FitWindowNative+RECT
if (-not [Xv6FitWindowNative]::GetClientRect($found, [ref]$finalClient)) {
    Write-Output "status=FAIL reason=final-client-rect title=$wanted"
    exit 2
}
$finalClientWidth = $finalClient.Right - $finalClient.Left
$finalClientHeight = $finalClient.Bottom - $finalClient.Top
$inside = $final.Left -ge $work.Left -and $final.Top -ge $work.Top -and `
          $final.Right -le $work.Right -and $final.Bottom -le $work.Bottom
if (-not $inside) {
    Write-Output "status=FAIL reason=outside-workarea mode=$fitMode before=$($rect.Left),$($rect.Top),${width}x${height} client_before=${clientWidth}x${clientHeight} final=$($final.Left),$($final.Top),${finalWidth}x${finalHeight} client_final=${finalClientWidth}x${finalClientHeight} workarea=$($work.Left),$($work.Top),$($work.Width)x$($work.Height) title=$wanted"
    exit 2
}
if ($fitMode -eq "native" -and
    ($finalClientWidth -ne $targetWidth -or
     $finalClientHeight -ne $targetHeight)) {
    Write-Output "status=FAIL reason=native-client-mismatch target_client=${targetWidth}x${targetHeight} client_final=${finalClientWidth}x${finalClientHeight} final_outer=${finalWidth}x${finalHeight} dpi=$([Xv6FitWindowNative]::GetDpiForWindow($found)) title=$wanted"
    exit 2
}
Write-Output "status=PASS mode=$fitMode dpi=$([Xv6FitWindowNative]::GetDpiForWindow($found)) before=$($rect.Left),$($rect.Top),${width}x${height} client_before=${clientWidth}x${clientHeight} final=$($final.Left),$($final.Top),${finalWidth}x${finalHeight} client_final=${finalClientWidth}x${finalClientHeight} workarea=$($work.Left),$($work.Top),$($work.Width)x$($work.Height) expected_title=$wanted actual_title=$foundTitle"
EOF

ps1_win=$(wslpath -w "$ps1")
wslenv="XV6_QEMU_WINDOW_TITLE:XV6_QEMU_WINDOW_FIT_MODE:XV6_QEMU_WINDOW_TARGET_WIDTH:XV6_QEMU_WINDOW_TARGET_HEIGHT"
if [[ -n "${WSLENV:-}" ]]; then
        wslenv="${wslenv}:${WSLENV}"
fi
watch_seconds="${XV6_QEMU_WINDOW_FIT_WATCH_SECONDS:-0}"
if [[ ! "${watch_seconds}" =~ ^[0-9]+$ || "${watch_seconds}" -gt 600 ]]; then
        echo "fit-owned-qemu-window: XV6_QEMU_WINDOW_FIT_WATCH_SECONDS must be 0..600" >&2
        exit 2
fi

fit_once() {
        XV6_QEMU_WINDOW_TITLE="$title" WSLENV="$wslenv" \
                powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$ps1_win"
}

fit_once
if ((watch_seconds > 0)); then
        deadline=$((SECONDS + watch_seconds))
        pass=1
        while ((SECONDS < deadline)); do
                sleep 2
                output="$(fit_once)" || {
                        rc=$?
                        if [[ "${rc}" == "77" ]]; then
                                printf 'fit-owned-qemu-window: watch ended because the exact window closed\n'
                                exit 0
                        fi
                        printf '%s\n' "${output}" >&2
                        exit "${rc}"
                }
                pass=$((pass + 1))
                printf 'fit-owned-qemu-window: watch_pass=%s %s\n' "${pass}" "${output}"
        done
fi
