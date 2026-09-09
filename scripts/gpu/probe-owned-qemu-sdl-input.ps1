Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Xv6SdlInputNative {
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
    public static extern bool EnumWindows(EnumWindowsProc callback,
                                          IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr parent,
                                                EnumWindowsProc callback,
                                                IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text,
                                           int count);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")]
    public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint dx, uint dy,
                                          uint data, UIntPtr extraInfo);
}
"@

$title = $env:XV6_QEMU_WINDOW_TITLE
$guestWidth = [int]$env:XV6_GUEST_WIDTH
$guestHeight = [int]$env:XV6_GUEST_HEIGHT
$expectedSurfaceWidth = [int]$env:XV6_SURFACE_WIDTH
$expectedSurfaceHeight = [int]$env:XV6_SURFACE_HEIGHT
if ([string]::IsNullOrWhiteSpace($title) -or
    $guestWidth -le 0 -or $guestHeight -le 0 -or
    $expectedSurfaceWidth -le 0 -or $expectedSurfaceHeight -le 0) {
    Write-Output "status=FAIL reason=invalid-arguments"
    exit 2
}

[void][Xv6SdlInputNative]::SetThreadDpiAwarenessContext([IntPtr](-4))

$script:found = [IntPtr]::Zero
$script:foundTitle = ""
$callback = [Xv6SdlInputNative+EnumWindowsProc]{
    param([IntPtr]$hWnd, [IntPtr]$lParam)
    if (-not [Xv6SdlInputNative]::IsWindowVisible($hWnd)) {
        return $true
    }
    $text = New-Object System.Text.StringBuilder 512
    [void][Xv6SdlInputNative]::GetWindowText($hWnd, $text, $text.Capacity)
    # WSLg appends host window-state text such as " (Ubuntu)" to QEMU's
    # unique, run-tokenized title.  Require the full QEMU title verbatim, but
    # allow that host-owned suffix just as fit-owned-qemu-window.sh does.
    if ($text.ToString().Contains($title)) {
        $script:found = $hWnd
        $script:foundTitle = $text.ToString()
        return $false
    }
    return $true
}
[void][Xv6SdlInputNative]::EnumWindows($callback, [IntPtr]::Zero)
if ($script:found -eq [IntPtr]::Zero) {
    Write-Output "status=FAIL reason=tokenized-window-not-found title=$title"
    exit 3
}

$client = New-Object Xv6SdlInputNative+RECT
$origin = New-Object Xv6SdlInputNative+POINT
$origin.X = 0
$origin.Y = 0
if (-not [Xv6SdlInputNative]::GetClientRect($script:found, [ref]$client) -or
    -not [Xv6SdlInputNative]::ClientToScreen($script:found, [ref]$origin)) {
    Write-Output "status=FAIL reason=client-geometry"
    exit 4
}
$clientWidth = $client.Right - $client.Left
$clientHeight = $client.Bottom - $client.Top
if ($clientWidth -le 0 -or $clientHeight -le 0) {
    Write-Output "status=FAIL reason=empty-client"
    exit 4
}

# WSLg's decorated top-level client spans the work area after maximize, while
# the SDL/X11 surface is its largest visible child (1908x987 on the reference
# host).  QEMU's aspect-fit math uses that SDL surface, so drive coordinates
# against the same child rectangle used by the independent screen capture.
$script:bestChildArea = 0
$script:bestChildRect = $null
$childCallback = [Xv6SdlInputNative+EnumWindowsProc]{
    param([IntPtr]$hWnd, [IntPtr]$lParam)
    if (-not [Xv6SdlInputNative]::IsWindowVisible($hWnd)) {
        return $true
    }
    $rect = New-Object Xv6SdlInputNative+RECT
    if (-not [Xv6SdlInputNative]::GetWindowRect($hWnd, [ref]$rect)) {
        return $true
    }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -lt 320 -or $height -lt 240) {
        return $true
    }
    $area = $width * $height
    if ($area -gt $script:bestChildArea) {
        $script:bestChildArea = $area
        $script:bestChildRect = $rect
    }
    return $true
}
[void][Xv6SdlInputNative]::EnumChildWindows(
    $script:found, $childCallback, [IntPtr]::Zero)
if ($script:bestChildRect -ne $null) {
    $surfaceX = $script:bestChildRect.Left
    $surfaceY = $script:bestChildRect.Top
    $surfaceWidth = $script:bestChildRect.Right - $script:bestChildRect.Left
    $surfaceHeight = $script:bestChildRect.Bottom - $script:bestChildRect.Top
    $surfaceSource = "child-window"
} else {
    # WSLg can flatten the rootless X11 SDL surface into the decorated Win32
    # top level.  In that case QEMU's authenticated aspect-fit row supplies
    # the real surface size.  Maximization centers it horizontally and leaves
    # the remaining vertical decoration above the SDL content.
    $surfaceWidth = $expectedSurfaceWidth
    $surfaceHeight = $expectedSurfaceHeight
    if ($surfaceWidth -gt $clientWidth -or $surfaceHeight -gt $clientHeight) {
        Write-Output "status=FAIL reason=surface-larger-than-client"
        exit 4
    }
    $surfaceX = $origin.X + [Math]::Floor(($clientWidth - $surfaceWidth) / 2)
    $surfaceY = $origin.Y + ($clientHeight - $surfaceHeight)
    $surfaceSource = "qemu-fit-with-wsl-decoration"
}
if ($surfaceWidth -ne $expectedSurfaceWidth -or
    $surfaceHeight -ne $expectedSurfaceHeight) {
    Write-Output "status=FAIL reason=surface-size-mismatch"
    exit 4
}

if (($surfaceWidth * $guestHeight) -le ($surfaceHeight * $guestWidth)) {
    $viewportWidth = $surfaceWidth
    $viewportHeight = [Math]::Max(1, [Math]::Floor(
        ($surfaceWidth * [long]$guestHeight) / $guestWidth))
} else {
    $viewportHeight = $surfaceHeight
    $viewportWidth = [Math]::Max(1, [Math]::Floor(
        ($surfaceHeight * [long]$guestWidth) / $guestHeight))
}
$viewportX = [Math]::Floor(($surfaceWidth - $viewportWidth) / 2)
$viewportY = [Math]::Floor(($surfaceHeight - $viewportHeight) / 2)

$saved = New-Object Xv6SdlInputNative+POINT
if (-not [Xv6SdlInputNative]::GetCursorPos([ref]$saved)) {
    Write-Output "status=FAIL reason=get-cursor"
    exit 5
}

# Stay two client pixels inside the content so each point is unambiguously in
# the fitted viewport even when integer division trims the far edge.
$points = @(
    @("top-left",     ($viewportX + 2),                  ($viewportY + 2)),
    @("top-right",    ($viewportX + $viewportWidth - 3), ($viewportY + 2)),
    @("bottom-right", ($viewportX + $viewportWidth - 3), ($viewportY + $viewportHeight - 3)),
    @("bottom-left",  ($viewportX + 2),                  ($viewportY + $viewportHeight - 3))
)

[void][Xv6SdlInputNative]::SetForegroundWindow($script:found)
$leftDown = 0x0002
$leftUp = 0x0004
foreach ($point in $points) {
    $label = $point[0]
    $clientX = [int]$point[1]
    $clientY = [int]$point[2]
    $screenX = $surfaceX + $clientX
    $screenY = $surfaceY + $clientY
    if (-not [Xv6SdlInputNative]::SetCursorPos($screenX, $screenY)) {
        [void][Xv6SdlInputNative]::SetCursorPos($saved.X, $saved.Y)
        Write-Output "status=FAIL reason=set-cursor label=$label"
        exit 6
    }
    Start-Sleep -Milliseconds 250
    [Xv6SdlInputNative]::mouse_event($leftDown, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [Xv6SdlInputNative]::mouse_event($leftUp, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    Write-Output "input_point label=$label client_x=$clientX client_y=$clientY screen_x=$screenX screen_y=$screenY click=left"
}

Start-Sleep -Milliseconds 250
[void][Xv6SdlInputNative]::SetCursorPos($saved.X, $saved.Y)
Write-Output "status=PASS expected_title=$title actual_title=$script:foundTitle outer_client=$clientWidth`x$clientHeight surface_source=$surfaceSource surface=$surfaceX,$surfaceY $surfaceWidth`x$surfaceHeight viewport=$viewportX,$viewportY $viewportWidth`x$viewportHeight restored_x=$($saved.X) restored_y=$($saved.Y)"
exit 0
