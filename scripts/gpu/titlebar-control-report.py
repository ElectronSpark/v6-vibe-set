#!/usr/bin/env python3
"""Compare before/after framebuffer PPMs for titlebar-control proofs."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def _read_token(data: bytes, pos: int) -> tuple[bytes, int]:
    n = len(data)
    while pos < n:
        c = data[pos]
        if c == ord("#"):
            while pos < n and data[pos] not in b"\r\n":
                pos += 1
        elif chr(c).isspace():
            pos += 1
        else:
            break
    start = pos
    while pos < n and not chr(data[pos]).isspace():
        pos += 1
    if start == pos:
        raise ValueError("unexpected end of PPM header")
    return data[start:pos], pos


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, pos = _read_token(data, 0)
    if magic not in (b"P6", b"P3"):
        raise ValueError(f"{path}: unsupported PPM magic {magic!r}")
    width_s, pos = _read_token(data, pos)
    height_s, pos = _read_token(data, pos)
    maxval_s, pos = _read_token(data, pos)
    width = int(width_s)
    height = int(height_s)
    maxval = int(maxval_s)
    if width <= 0 or height <= 0:
        raise ValueError(f"{path}: invalid dimensions {width}x{height}")
    if maxval != 255:
        raise ValueError(f"{path}: unsupported maxval {maxval}")
    if magic == b"P6":
        if pos < len(data) and chr(data[pos]).isspace():
            pos += 1
        pixels = data[pos:]
        want = width * height * 3
        if len(pixels) < want:
            raise ValueError(f"{path}: truncated pixel data")
        return width, height, pixels[:want]

    values: list[int] = []
    while len(values) < width * height * 3:
        tok, pos = _read_token(data, pos)
        values.append(int(tok))
    return width, height, bytes(values)


def parse_crop(spec: str | None, width: int, height: int) -> tuple[int, int, int, int]:
    if not spec:
        return 0, 0, width, height
    try:
        size, origin = spec.split("+", 1)
        w_s, h_s = size.split("x", 1)
        x_s, y_s = origin.split("+", 1)
        x = int(x_s)
        y = int(y_s)
        w = int(w_s)
        h = int(h_s)
    except ValueError as exc:
        raise ValueError("crop must be WIDTHxHEIGHT+X+Y") from exc
    if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > width or y + h > height:
        raise ValueError(f"crop {spec!r} outside {width}x{height}")
    return x, y, w, h


def diff_pixels(before: bytes, after: bytes, width: int,
                crop: tuple[int, int, int, int]) -> tuple[int, int]:
    x0, y0, w, h = crop
    changed = 0
    total = w * h
    for y in range(y0, y0 + h):
        row = y * width * 3
        for x in range(x0, x0 + w):
            i = row + x * 3
            if before[i:i + 3] != after[i:i + 3]:
                changed += 1
    return changed, total


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control", required=True)
    parser.add_argument("--before", required=True, type=Path)
    parser.add_argument("--after", required=True, type=Path)
    parser.add_argument("--crop")
    parser.add_argument("--min-changed", type=int, default=1)
    args = parser.parse_args()

    try:
        bw, bh, bp = read_ppm(args.before)
        aw, ah, ap = read_ppm(args.after)
        if (bw, bh) != (aw, ah):
            raise ValueError(f"dimension mismatch before={bw}x{bh} after={aw}x{ah}")
        crop = parse_crop(args.crop, bw, bh)
        changed, total = diff_pixels(bp, ap, bw, crop)
    except Exception as exc:
        print(f"TITLEBAR-CONTROL-RESULT fail control={args.control} error={exc}")
        return 2

    ratio = changed / total if total else 0.0
    status = "pass" if changed >= args.min_changed else "fail"
    print(
        f"TITLEBAR-CONTROL-RESULT {status} control={args.control} "
        f"width={bw} height={bh} crop={crop[2]}x{crop[3]}+{crop[0]}+{crop[1]} "
        f"changed_pixels={changed} total_pixels={total} "
        f"changed_ratio={ratio:.6f} min_changed={args.min_changed}"
    )
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
