#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 OUT.png" >&2
    exit 2
fi

out=$1
mkdir -p "$(dirname "$out")"
rm -f "$out"

if [ -n "${HOST_SCREENSHOT_CMD:-}" ]; then
    # shellcheck disable=SC2086
    if $HOST_SCREENSHOT_CMD "$out"; then
        test -s "$out"
        exit 0
    fi
fi

try_file() {
    test -s "$out"
}

if command -v grim >/dev/null 2>&1 && [ -n "${WAYLAND_DISPLAY:-}" ]; then
    grim "$out" >/dev/null 2>&1 && try_file && exit 0
fi

if command -v gnome-screenshot >/dev/null 2>&1; then
    gnome-screenshot -f "$out" >/dev/null 2>&1 && try_file && exit 0
fi

if command -v spectacle >/dev/null 2>&1; then
    spectacle -b -n -o "$out" >/dev/null 2>&1 && try_file && exit 0
fi

if command -v maim >/dev/null 2>&1; then
    maim "$out" >/dev/null 2>&1 && try_file && exit 0
fi

if command -v scrot >/dev/null 2>&1; then
    scrot "$out" >/dev/null 2>&1 && try_file && exit 0
fi

if command -v import >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
    import -window root "$out" >/dev/null 2>&1 && try_file && exit 0
fi

if command -v xwd >/dev/null 2>&1 && command -v convert >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
    tmp="${out%.png}.xwd"
    rm -f "$tmp"
    if xwd -root -silent -out "$tmp" >/dev/null 2>&1 && convert "$tmp" "$out" >/dev/null 2>&1; then
        rm -f "$tmp"
        try_file && exit 0
    fi
    rm -f "$tmp"
fi

if command -v powershell.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
    out_abs=$(readlink -f "$out")
    out_win=$(wslpath -w "$out_abs")
    if XV6_HOST_SCREENSHOT_OUT_WIN="$out_win" WSLENV="XV6_HOST_SCREENSHOT_OUT_WIN${WSLENV:+:$WSLENV}" powershell.exe -NoProfile -ExecutionPolicy Bypass -Command '
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$out = $env:XV6_HOST_SCREENSHOT_OUT_WIN
$bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($bounds.X, $bounds.Y, 0, 0, $bounds.Size)
$bitmap.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
' >/dev/null 2>&1; then
        try_file && exit 0
    fi
fi

echo "capture-host-screen: no host screenshot backend succeeded for $out" >&2
exit 1
