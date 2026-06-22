param(
    [string]$Title = "xv6-os QEMU",
    [string]$Target = "vscode",
    [int]$XOffset = 0,
    [int]$YOffset = 0,
    [double]$TimeoutSeconds = 8.0
)

Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class Win32Windows {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter,
        int X, int Y, int cx, int cy, uint uFlags);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool GetCursorPos(out POINT point);

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

    public class WindowInfo {
        public IntPtr Handle;
        public string Title;
        public RECT Rect;
    }

    public static List<WindowInfo> VisibleWindows() {
        var windows = new List<WindowInfo>();
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            if (!IsWindowVisible(hWnd))
                return true;
            var title = new StringBuilder(512);
            GetWindowText(hWnd, title, title.Capacity);
            string text = title.ToString();
            if (String.IsNullOrWhiteSpace(text))
                return true;
            RECT rect;
            if (!GetWindowRect(hWnd, out rect))
                return true;
            windows.Add(new WindowInfo { Handle = hWnd, Title = text, Rect = rect });
            return true;
        }, IntPtr.Zero);
        return windows;
    }
}
"@

function Window-CenterPoint($window) {
    $x = [int](($window.Rect.Left + $window.Rect.Right) / 2)
    $y = [int](($window.Rect.Top + $window.Rect.Bottom) / 2)
    return [System.Drawing.Point]::new($x, $y)
}

function Find-QemuWindow($title) {
    $windows = [Win32Windows]::VisibleWindows()
    $exact = $windows | Where-Object { $_.Title -eq $title } | Select-Object -First 1
    if ($exact) {
        return $exact
    }
    return $windows | Where-Object { $_.Title -like "*$title*" } | Select-Object -First 1
}

function Find-VSCodeWindow {
    $windows = [Win32Windows]::VisibleWindows()
    $repoWindow = $windows |
        Where-Object { $_.Title -like "*Visual Studio Code*" -and $_.Title -like "*xv6-os*" } |
        Select-Object -First 1
    if ($repoWindow) {
        return $repoWindow
    }
    return $windows |
        Where-Object { $_.Title -like "*Visual Studio Code*" } |
        Select-Object -First 1
}

function Resolve-TargetScreen($target) {
    if ($target -eq "vscode" -or $target -eq "current") {
        $codeWindow = Find-VSCodeWindow
        if ($codeWindow) {
            return [System.Windows.Forms.Screen]::FromPoint((Window-CenterPoint $codeWindow))
        }
    }
    if ($target -eq "pointer") {
        $point = New-Object Win32Windows+POINT
        if ([Win32Windows]::GetCursorPos([ref]$point)) {
            return [System.Windows.Forms.Screen]::FromPoint(
                [System.Drawing.Point]::new($point.X, $point.Y))
        }
    }
    foreach ($screen in [System.Windows.Forms.Screen]::AllScreens) {
        if ($screen.DeviceName -eq $target -or $screen.DeviceName.EndsWith("\$target")) {
            return $screen
        }
    }
    return [System.Windows.Forms.Screen]::PrimaryScreen
}

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$qemuWindow = $null
while ([DateTime]::UtcNow -lt $deadline) {
    $qemuWindow = Find-QemuWindow $Title
    if ($qemuWindow) {
        break
    }
    Start-Sleep -Milliseconds 100
}

if (-not $qemuWindow) {
    Write-Error "place-qemu-window-windows: no QEMU window found title='$Title'"
    exit 1
}

$screen = Resolve-TargetScreen $Target
$area = $screen.WorkingArea
$x = $area.X + $XOffset
$y = $area.Y + $YOffset
$width = $qemuWindow.Rect.Right - $qemuWindow.Rect.Left
$height = $qemuWindow.Rect.Bottom - $qemuWindow.Rect.Top

$SW_RESTORE = 9
$SWP_NOZORDER = 0x0004
$SWP_SHOWWINDOW = 0x0040
[void][Win32Windows]::ShowWindow($qemuWindow.Handle, $SW_RESTORE)
[void][Win32Windows]::SetWindowPos(
    $qemuWindow.Handle, [IntPtr]::Zero, $x, $y, $width, $height,
    $SWP_NOZORDER -bor $SWP_SHOWWINDOW)
[void][Win32Windows]::SetForegroundWindow($qemuWindow.Handle)

Write-Host ("place-qemu-window-windows: moved title='{0}' target={1} screen={2} working={3},{4} {5}x{6} window={7}x{8} pos={9},{10}" -f `
    $Title, $Target, $screen.DeviceName, $area.X, $area.Y, $area.Width,
    $area.Height, $width, $height, $x, $y)
