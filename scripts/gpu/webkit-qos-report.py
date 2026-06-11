#!/usr/bin/env python3
"""Summarize WebKit/GStreamer decoder QoS drops for the YouTube gate.

The §13 live-YouTube criterion is intentionally log-based:

  * at least a 300 s real watch-page soak,
  * <= 5 avdec_h264 QoS drops per minute,
  * max QoS lateness < 100 ms.

This parser reads a persisted GStreamer debug log and turns those requirements
into one machine-checkable RESULT line.  Use --duration-seconds when the
harness knows the exact wall-clock watch duration; otherwise the parser falls
back to the media timestamp span in the QoS messages.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


QOS_RE = re.compile(
    r"<(?P<element>avdec_h264[^>]*)>\s+Dropping frame due to QoS\."
    r".*?\bstart:(?P<start>\d+:\d\d:\d\d\.\d+)"
    r".*?\bdeadline:(?P<deadline>\d+:\d\d:\d\d\.\d+)"
    r".*?\bearliest_time:(?P<earliest>\d+:\d\d:\d\d\.\d+)"
)


def parse_gst_time(value: str) -> float:
    hours_s, minutes_s, seconds_s = value.split(":", 2)
    return int(hours_s) * 3600 + int(minutes_s) * 60 + float(seconds_s)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Report avdec_h264 QoS drop rate and lateness from a GST log."
    )
    parser.add_argument("log", type=Path, help="GStreamer debug log to parse")
    parser.add_argument(
        "--duration-seconds",
        type=float,
        default=None,
        help="authoritative wall-clock watch duration; defaults to QoS media span",
    )
    parser.add_argument(
        "--min-duration-seconds",
        type=float,
        default=300.0,
        help="minimum duration required for PASS (default: 300)",
    )
    parser.add_argument(
        "--max-drops-per-minute",
        type=float,
        default=5.0,
        help="maximum allowed avdec_h264 QoS drops per minute (default: 5)",
    )
    parser.add_argument(
        "--max-lateness-ms",
        type=float,
        default=100.0,
        help="maximum allowed QoS lateness in milliseconds (default: 100)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.log.is_file():
        print(f"QOS-RESULT fail reason=missing-log path={args.log}", file=sys.stderr)
        return 2

    drops = 0
    first_media_time: float | None = None
    last_media_time: float | None = None
    max_lateness_ms = 0.0

    with args.log.open("r", errors="ignore") as fh:
        for line in fh:
            match = QOS_RE.search(line)
            if not match:
                continue
            drops += 1
            start = parse_gst_time(match.group("start"))
            deadline = parse_gst_time(match.group("deadline"))
            earliest = parse_gst_time(match.group("earliest"))
            first_media_time = start if first_media_time is None else min(first_media_time, start)
            last_media_time = start if last_media_time is None else max(last_media_time, start)
            max_lateness_ms = max(max_lateness_ms, max(0.0, (earliest - deadline) * 1000.0))

    if args.duration_seconds is not None:
        duration = args.duration_seconds
        duration_source = "wall"
    elif first_media_time is not None and last_media_time is not None:
        duration = max(0.0, last_media_time - first_media_time)
        duration_source = "qos-media-span"
    else:
        duration = 0.0
        duration_source = "none"

    drops_per_min = drops / (duration / 60.0) if duration > 0 else (float("inf") if drops else 0.0)
    pass_duration = duration >= args.min_duration_seconds
    pass_rate = drops_per_min <= args.max_drops_per_minute
    pass_late = max_lateness_ms < args.max_lateness_ms
    status = "pass" if pass_duration and pass_rate and pass_late else "fail"

    print(
        "QOS-RESULT "
        f"{status} drops={drops} duration={duration:.3f} duration_source={duration_source} "
        f"drops_per_min={drops_per_min:.3f} max_lateness_ms={max_lateness_ms:.3f} "
        f"min_duration={args.min_duration_seconds:.3f} "
        f"max_drops_per_min={args.max_drops_per_minute:.3f} "
        f"max_allowed_lateness_ms={args.max_lateness_ms:.3f}"
    )
    if status != "pass":
        reasons = []
        if not pass_duration:
            reasons.append("duration")
        if not pass_rate:
            reasons.append("drop-rate")
        if not pass_late:
            reasons.append("lateness")
        print(f"QOS-FAIL-REASON {','.join(reasons)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
