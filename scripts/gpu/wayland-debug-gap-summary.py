#!/usr/bin/env python3
"""Summarize WAYLAND_DEBUG timing gaps from xv6 KDE interaction logs."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass


LINE_RE = re.compile(r"wayland-debug-ms=(?P<host>[0-9]+)\s+line=(?P<line>.*)")
PROTO_RE = re.compile(r"^\[(?P<ts>[0-9]+(?:\.[0-9]+)?)\]\s+(?P<msg>.*)$")


@dataclass
class Event:
    index: int
    host_ms: int
    proto_ms: float | None
    line: str
    kind: str


@dataclass
class Gap:
    host_gap_ms: int
    proto_gap_ms: float
    prev: Event
    cur: Event


def quote(value: str, limit: int = 220) -> str:
    value = value.replace("\t", " ").replace("\r", "").replace("\n", " ")
    if len(value) > limit:
        value = value[: limit - 3] + "..."
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def classify(line: str) -> str:
    if "kde_app_launch_probe launch app=konsole" in line:
        return "probe-launch"
    if "kde_app_launch_probe marker phase=konsole-ready status=found" in line:
        return "probe-marker-found"
    if "kde_app_launch_probe marker phase=konsole-ready status=waiting" in line:
        return "probe-marker-waiting"
    if "kde_app_launch_probe konsole_phase" in line:
        return "probe-konsole-phase"
    if "kde_app_launch_probe konsole_phase_detail" in line:
        return "probe-konsole-phase-detail"
    if "xv6-konsole-shell" in line:
        return "probe-shell"
    if "xdg_surface" in line and ".configure" in line:
        return "xdg-surface-configure"
    if "xdg_surface" in line and ".ack_configure" in line:
        return "xdg-surface-ack"
    if "xdg_toplevel" in line and ".configure" in line:
        return "xdg-toplevel-configure"
    if "wl_surface" in line and ".frame" in line:
        return "surface-frame-request"
    if "wl_callback" in line and ".done" in line:
        return "callback-done"
    if "wl_surface" in line and ".commit" in line:
        return "surface-commit"
    if "wl_surface" in line and ".attach" in line:
        return "surface-attach"
    if "wl_shm" in line and "create_pool" in line:
        return "shm-create-pool"
    if "create_buffer" in line:
        return "buffer-create"
    if "wl_buffer" in line and ".release" in line:
        return "buffer-release"
    if "wl_registry" in line or "registry" in line:
        return "registry"
    if "zwp_linux_dmabuf" in line or "dmabuf" in line:
        return "dmabuf"
    if "text_input" in line:
        return "text-input"
    if "wl_keyboard" in line:
        return "keyboard"
    return "other"


def parse_events(path: str) -> list[Event]:
    events: list[Event] = []
    with open(path, "r", errors="replace") as handle:
        for raw in handle:
            match = LINE_RE.search(raw)
            if not match:
                continue
            line = match.group("line").rstrip("\n")
            proto_match = PROTO_RE.match(line)
            proto_ms = None
            msg = line
            if proto_match:
                proto_ms = float(proto_match.group("ts"))
                msg = proto_match.group("msg")
            events.append(
                Event(
                    index=len(events) + 1,
                    host_ms=int(match.group("host")),
                    proto_ms=proto_ms,
                    line=line,
                    kind=classify(msg),
                )
            )
    return events


def emit_summary(path: str, events: list[Event], top: int, min_gap: float) -> int:
    proto_events = [event for event in events if event.proto_ms is not None]
    gaps: list[Gap] = []
    previous: Event | None = None
    for event in proto_events:
        if previous is not None and event.proto_ms is not None and previous.proto_ms is not None:
            proto_gap = event.proto_ms - previous.proto_ms
            host_gap = event.host_ms - previous.host_ms
            if proto_gap >= min_gap:
                gaps.append(Gap(host_gap, proto_gap, previous, event))
        previous = event

    gaps.sort(key=lambda gap: gap.proto_gap_ms, reverse=True)
    max_proto = gaps[0].proto_gap_ms if gaps else 0.0
    max_host = max((gap.host_gap_ms for gap in gaps), default=0)
    print(
        "wayland_debug_summary "
        f"status=PASS path={quote(path)} total_lines={len(events)} "
        f"proto_lines={len(proto_events)} gaps_ge_ms={min_gap:.3f} "
        f"gap_count={len(gaps)} max_proto_gap_ms={max_proto:.3f} "
        f"max_host_gap_ms={max_host}"
    )

    counts: dict[str, int] = {}
    for event in events:
        counts[event.kind] = counts.get(event.kind, 0) + 1
    for kind, count in sorted(counts.items(), key=lambda item: (-item[1], item[0])):
        print(f"wayland_debug_kind kind={kind} count={count}")

    interesting = [
        event
        for event in events
        if event.kind
        in {
            "probe-launch",
            "probe-marker-waiting",
            "probe-marker-found",
            "probe-konsole-phase",
            "probe-konsole-phase-detail",
            "probe-shell",
            "xdg-surface-configure",
            "xdg-surface-ack",
            "xdg-toplevel-configure",
            "surface-frame-request",
            "surface-commit",
            "callback-done",
        }
    ]
    first_host = events[0].host_ms if events else 0
    for event in interesting[: max(top, 0)]:
        since_first = event.host_ms - first_host if first_host else 0
        proto = -1.0 if event.proto_ms is None else event.proto_ms
        print(
            "wayland_debug_event "
            f"idx={event.index} host_since_first_ms={since_first} "
            f"host_ms={event.host_ms} proto_ms={proto:.3f} "
            f"kind={event.kind} line={quote(event.line)}"
        )

    for idx, gap in enumerate(gaps[:top], 1):
        print(
            "wayland_debug_gap "
            f"rank={idx} proto_gap_ms={gap.proto_gap_ms:.3f} "
            f"host_gap_ms={gap.host_gap_ms} prev_idx={gap.prev.index} "
            f"cur_idx={gap.cur.index} prev_kind={gap.prev.kind} "
            f"cur_kind={gap.cur.kind} prev={quote(gap.prev.line)} "
            f"cur={quote(gap.cur.line)}"
        )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--min-gap-ms", type=float, default=50.0)
    args = parser.parse_args(argv)

    events = parse_events(args.log)
    if not events:
        print(
            "wayland_debug_summary "
            f"status=FAIL reason=no-wayland-debug-lines path={quote(args.log)}"
        )
        return 1
    return emit_summary(args.log, events, args.top, args.min_gap_ms)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
