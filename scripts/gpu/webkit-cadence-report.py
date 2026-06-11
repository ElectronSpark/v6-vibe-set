#!/usr/bin/env python3
"""Report adjacent-frame visual progress for WebKit/YouTube captures.

The §13 YouTube closure needs host-visible player crops to keep changing
through the watch window.  This script turns a directory or glob of captured
frames into a machine-checkable result line:

  WEBKIT-CADENCE-RESULT pass frames=... pairs=... active_pairs=...

By default every adjacent pair must have at least 100 changed pixels.  Use
--crop WxH+X+Y when the input images are whole-window screenshots; omit it
when the inputs are already player crops.
"""

from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image
except Exception as exc:  # pragma: no cover - exercised on hosts without PIL
    print(f"WEBKIT-CADENCE-RESULT fail reason=missing-pillow detail={exc}", file=sys.stderr)
    raise SystemExit(2)


def parse_crop(value: str | None) -> tuple[int, int, int, int] | None:
    if not value:
        return None
    import re

    match = re.fullmatch(r"(\d+)x(\d+)\+(\d+)\+(\d+)", value)
    if not match:
        raise argparse.ArgumentTypeError("crop must be WxH+X+Y")
    w, h, x, y = (int(part) for part in match.groups())
    if w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError("crop width/height must be positive")
    return (x, y, x + w, y + h)


def expand_inputs(patterns: Iterable[str]) -> list[Path]:
    paths: list[Path] = []
    for pattern in patterns:
        matches = sorted(Path(path) for path in glob.glob(pattern))
        if matches:
            paths.extend(matches)
        else:
            paths.append(Path(pattern))
    seen: set[Path] = set()
    out: list[Path] = []
    for path in paths:
        key = path.resolve() if path.exists() else path
        if key not in seen:
            seen.add(key)
            out.append(path)
    return out


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check adjacent WebKit/YouTube frame captures for visual progress."
    )
    parser.add_argument("images", nargs="+", help="image paths or shell-style globs")
    parser.add_argument(
        "--crop",
        type=parse_crop,
        default=None,
        help="crop rectangle as WxH+X+Y before comparing frames",
    )
    parser.add_argument(
        "--min-changed-pixels",
        type=int,
        default=100,
        help="minimum changed pixels required for an active adjacent pair",
    )
    parser.add_argument(
        "--tolerance",
        type=int,
        default=0,
        help="per-channel delta ignored as noise (default: 0)",
    )
    parser.add_argument(
        "--min-frames",
        type=int,
        default=2,
        help="minimum number of frames required (default: 2)",
    )
    parser.add_argument(
        "--allow-inactive-pairs",
        type=int,
        default=0,
        help="number of adjacent pairs allowed below the changed-pixel threshold",
    )
    return parser.parse_args()


def load_image(path: Path, crop: tuple[int, int, int, int] | None) -> Image.Image:
    image = Image.open(path).convert("RGB")
    if crop is not None:
        if crop[2] > image.width or crop[3] > image.height:
            raise ValueError(
                f"crop {crop[2] - crop[0]}x{crop[3] - crop[1]}+{crop[0]}+{crop[1]} "
                f"exceeds {path} size {image.width}x{image.height}"
            )
        image = image.crop(crop)
    return image


def changed_pixels(a: Image.Image, b: Image.Image, tolerance: int) -> int:
    if a.size != b.size:
        raise ValueError(f"image size mismatch: {a.size} vs {b.size}")
    left = a.tobytes()
    right = b.tobytes()
    if tolerance == 0:
        return sum(
            1
            for offset in range(0, len(left), 3)
            if left[offset : offset + 3] != right[offset : offset + 3]
        )
    changed = 0
    for offset in range(0, len(left), 3):
        if (
            abs(left[offset] - right[offset]) > tolerance
            or abs(left[offset + 1] - right[offset + 1]) > tolerance
            or abs(left[offset + 2] - right[offset + 2]) > tolerance
        ):
            changed += 1
    return changed


def main() -> int:
    args = parse_args()
    paths = expand_inputs(args.images)
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        print(f"WEBKIT-CADENCE-RESULT fail reason=missing-image paths={','.join(missing)}")
        return 2
    if len(paths) < args.min_frames:
        print(
            "WEBKIT-CADENCE-RESULT fail "
            f"reason=too-few-frames frames={len(paths)} min_frames={args.min_frames}"
        )
        return 1
    if args.min_changed_pixels < 0 or args.tolerance < 0 or args.allow_inactive_pairs < 0:
        print("WEBKIT-CADENCE-RESULT fail reason=invalid-threshold")
        return 2

    try:
        frames = [load_image(path, args.crop) for path in paths]
        deltas = [
            changed_pixels(frames[index], frames[index + 1], args.tolerance)
            for index in range(len(frames) - 1)
        ]
    except Exception as exc:
        print(f"WEBKIT-CADENCE-RESULT fail reason=analysis-error detail={exc}")
        return 2

    active_pairs = sum(1 for value in deltas if value >= args.min_changed_pixels)
    inactive_pairs = len(deltas) - active_pairs
    max_changed = max(deltas) if deltas else 0
    min_changed = min(deltas) if deltas else 0
    status = "pass" if inactive_pairs <= args.allow_inactive_pairs else "fail"
    crop_label = "none"
    if args.crop is not None:
        crop_label = f"{args.crop[2] - args.crop[0]}x{args.crop[3] - args.crop[1]}+{args.crop[0]}+{args.crop[1]}"

    print(
        "WEBKIT-CADENCE-RESULT "
        f"{status} frames={len(paths)} pairs={len(deltas)} active_pairs={active_pairs} "
        f"inactive_pairs={inactive_pairs} allowed_inactive_pairs={args.allow_inactive_pairs} "
        f"min_changed={min_changed} max_changed={max_changed} "
        f"threshold={args.min_changed_pixels} tolerance={args.tolerance} crop={crop_label} "
        f"first={paths[0]} last={paths[-1]}"
    )
    print("WEBKIT-CADENCE-DELTAS " + ",".join(str(value) for value in deltas))
    if status != "pass":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
