#!/usr/bin/env bash
# Capture and sanity-check the Hyper-V framebuffer while the 3D demo is visible.

set -euo pipefail

VM_NAME=${VM_NAME:-xv6-os-hyperv}
WIDTH=${WIDTH:-1024}
HEIGHT=${HEIGHT:-768}
OUT_PNG=${OUT_PNG:-/mnt/c/Temp/xv6-hyperv-3d-visual.png}
OUT_RAW=${OUT_RAW:-/mnt/c/Temp/xv6-hyperv-3d-visual.raw}

fail() {
    echo "hyperv-3d-visual-check: $*" >&2
    exit 1
}

command -v powershell.exe >/dev/null || fail "missing powershell.exe"
command -v python3 >/dev/null || fail "missing python3"

raw_win=$(wslpath -w "${OUT_RAW}")
png_abs=$(readlink -f "${OUT_PNG}")

ps_vm=${VM_NAME//\'/\'\'}
ps_raw=${raw_win//\'/\'\'}
powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -Command "\$vmName = '${ps_vm}'; \$rawPath = '${ps_raw}'; \$vm = Get-WmiObject -Namespace root\\virtualization\\v2 -Class Msvm_ComputerSystem -Filter \"ElementName='\$vmName'\"; if (-not \$vm) { throw \"VM not found: \$vmName\" }; \$svc = Get-WmiObject -Namespace root\\virtualization\\v2 -Class Msvm_VirtualSystemManagementService; \$img = \$svc.GetVirtualSystemThumbnailImage(\$vm, ${WIDTH}, ${HEIGHT}).ImageData; [System.IO.File]::WriteAllBytes(\$rawPath, \$img); \"raw bytes: \$([int]\$img.Length)\""

python3 - "${OUT_RAW}" "${png_abs}" "${WIDTH}" "${HEIGHT}" <<'PY'
from pathlib import Path
import sys
from PIL import Image, ImageStat

raw_path = Path(sys.argv[1])
png_path = Path(sys.argv[2])
w = int(sys.argv[3])
h = int(sys.argv[4])
raw = raw_path.read_bytes()
expected = w * h * 2
if len(raw) < expected:
    raise SystemExit(f"thumbnail too small: {len(raw)} < {expected}")

img = Image.frombytes("RGB", (w, h), raw[:expected], "raw", "BGR;16")
png_path.parent.mkdir(parents=True, exist_ok=True)
img.save(png_path)

stat = ImageStat.Stat(img)
colors = len(img.resize((256, 192)).getcolors(maxcolors=10_000_000))
mask = img.point(lambda v: 255 if v > 20 else 0).convert("L")
bbox = mask.getbbox()
if bbox is None:
    raise SystemExit("blank thumbnail")
area = (bbox[2] - bbox[0]) * (bbox[3] - bbox[1])
if area < w * h * 0.05:
    raise SystemExit(f"visible area too small: {area}")
if max(stat.stddev) < 8:
    raise SystemExit(f"low contrast thumbnail: {stat.stddev}")
if colors < 128:
    raise SystemExit(f"not enough color variation: {colors}")

# The FPS overlay lives near the top-left of the GL surface. This crop should
# contain bright glyph strokes over a dark demo background.
overlay = img.crop((80, 60, min(w, 460), min(h, 180)))
bright = 0
dark = 0
data_method = getattr(overlay, "get_flattened_data", None)
overlay_pixels = data_method() if data_method is not None else overlay.getdata()
for r, g, b in overlay_pixels:
    if r > 170 and g > 170 and b > 170:
        bright += 1
    if r < 35 and g < 45 and b < 55:
        dark += 1
if bright < 300 or dark < 1000:
    raise SystemExit(
        f"FPS overlay crop missing expected contrast: bright={bright} dark={dark}"
    )

print(
    "hyperv-3d-visual-check: ok "
    f"png={png_path} bbox={bbox} colors={colors} "
    f"stddev={tuple(round(x, 2) for x in stat.stddev)} "
    f"overlay_bright={bright} overlay_dark={dark}"
)
PY
