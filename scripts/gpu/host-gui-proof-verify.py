#!/usr/bin/env python3
"""Verify host-GUI proof artifact bundles.

The verifier is intentionally file-based: app-specific harnesses may launch
and drive clients differently, but they must leave durable frame captures and
logs that prove more than a desktop icon changed.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


FAIL_RE = re.compile(
    r"FAIL|panic|fatal page fault|SIGABRT|coredump: generating|"
    r"virtio_gpu: async command|cannot execute|not found"
)


def die(message: str) -> None:
    print(f"HOSTGUI-PROOF-VERIFY-FAIL {message}", file=sys.stderr)
    raise SystemExit(1)


def read_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    offset = 0

    def token() -> bytes:
        nonlocal offset
        while offset < len(data) and data[offset] in b" \t\r\n":
            offset += 1
        if offset < len(data) and data[offset:offset + 1] == b"#":
            while offset < len(data) and data[offset] not in b"\r\n":
                offset += 1
            return token()
        start = offset
        while offset < len(data) and data[offset] not in b" \t\r\n":
            offset += 1
        return data[start:offset]

    magic = token()
    width = int(token())
    height = int(token())
    maxval = int(token())
    if offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    pixels = data[offset:]
    if magic != b"P6" or maxval != 255:
        die(f"{path}: unsupported PPM header")
    expected = width * height * 3
    if len(pixels) != expected:
        die(f"{path}: pixel bytes={len(pixels)} expected={expected}")
    return width, height, pixels


def nonblack_pixels(pixels: bytes) -> int:
    return sum(
        1
        for i in range(0, len(pixels), 3)
        if pixels[i:i + 3] != b"\x00\x00\x00"
    )


def changed_pixels(a: bytes, b: bytes) -> int:
    return sum(
        1
        for i in range(0, len(a), 3)
        if a[i:i + 3] != b[i:i + 3]
    )


def parse_phase(text: str) -> tuple[str, pathlib.Path]:
    if "=" not in text:
        die(f"phase must be name=path, got {text!r}")
    name, value = text.split("=", 1)
    if not name:
        die(f"empty phase name in {text!r}")
    return name, pathlib.Path(value)


def parse_diff(text: str) -> tuple[str, str, int]:
    fields = text.split(":")
    if len(fields) != 3:
        die(f"diff must be from:to:min_changed_pixels, got {text!r}")
    return fields[0], fields[1], int(fields[2])


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--out-dir", required=True, type=pathlib.Path)
    parser.add_argument("--log", action="append", default=[], type=pathlib.Path)
    parser.add_argument("--require-log", action="append", default=[])
    parser.add_argument("--phase", action="append", default=[], type=parse_phase)
    parser.add_argument("--diff", action="append", default=[], type=parse_diff)
    parser.add_argument("--min-nonblack", default=1000, type=int)
    parser.add_argument("--summary", type=pathlib.Path)
    args = parser.parse_args(argv)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    summary = args.summary or args.out_dir / f"{args.app}-proof-summary.tsv"

    log_text = ""
    for log in args.log:
        if not log.exists():
            die(f"missing log {log}")
        log_text += log.read_text(errors="replace") + "\n"
    if log_text and FAIL_RE.search(log_text):
        die(f"{args.app}: failure marker found in logs")
    for pattern in args.require_log:
        if not re.search(pattern, log_text, re.MULTILINE):
            die(f"{args.app}: missing log pattern {pattern!r}")

    phases: dict[str, tuple[pathlib.Path, int, int, bytes, int]] = {}
    for name, path in args.phase:
        if not path.exists():
            die(f"{args.app}: missing phase {name} file {path}")
        width, height, pixels = read_ppm(path)
        nonblack = nonblack_pixels(pixels)
        if nonblack < args.min_nonblack:
            die(f"{args.app}: phase {name} nonblack={nonblack}")
        phases[name] = (path, width, height, pixels, nonblack)

    if not phases:
        die(f"{args.app}: no phases provided")

    rows = ["app\tkind\tfrom\tto\tvalue\tthreshold\tstatus\tpath"]
    for name, (path, width, height, _pixels, nonblack) in phases.items():
        rows.append(
            f"{args.app}\tphase\t{name}\t-\t{nonblack}\t"
            f"{args.min_nonblack}\tPASS\t{path}"
        )
        if (width, height) != (1280, 800):
            die(f"{args.app}: phase {name} dimensions={width}x{height}")

    for left, right, minimum in args.diff:
        if left not in phases or right not in phases:
            die(f"{args.app}: diff references unknown phase {left}:{right}")
        changed = changed_pixels(phases[left][3], phases[right][3])
        status = "PASS" if changed >= minimum else "FAIL"
        rows.append(
            f"{args.app}\tdiff\t{left}\t{right}\t{changed}\t"
            f"{minimum}\t{status}\t-"
        )
        if status != "PASS":
            die(
                f"{args.app}: diff {left}->{right} changed={changed} "
                f"threshold={minimum}"
            )

    summary.write_text("\n".join(rows) + "\n")
    print(f"HOSTGUI-PROOF-VERIFY-PASS app={args.app} summary={summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
