#!/usr/bin/env python3
"""Compare WSL and xv6 D3D12 CREATEALLOCATION private blobs."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


WSL_RUNTIME_RE = re.compile(
    r"create_allocation_runtime_full\s+"
    r"size=(?P<size>\d+)\s+dumped=(?P<dumped>\d+)\s+"
    r"truncated=(?P<truncated>[01])\s+"
    r"hash_dumped=0x(?P<hash>[0-9a-fA-F]+)\s+"
    r"hex=(?P<hex>[0-9a-fA-F]+)"
)
WSL_CREATE_ALLOCATION_RE = re.compile(
    r"create_allocation\s+device=(?P<device>0x[0-9a-fA-F]+)\s+"
    r"resource=(?P<resource>0x[0-9a-fA-F]+)\s+"
    r"alloc_count=(?P<alloc_count>\d+)\s+runtime=(?P<runtime>\d+)\s+"
    r"priv=(?P<priv>\d+)\s+flags=(?P<flags>0x[0-9a-fA-F]+)\s+"
    r"rt_resource=(?P<rt_resource>0x[0-9a-fA-F]+)"
)
WSL_ALLOC_PRIV_FULL_RE = re.compile(
    r"create_allocation_alloc_priv_full\s+"
    r"phase=(?P<phase>\S+)\s+index=(?P<index>\d+)\s+"
    r"size=(?P<size>\d+)\s+dumped=(?P<dumped>\d+)\s+"
    r"truncated=(?P<truncated>[01])\s+"
    r"hash_dumped=0x(?P<hash>[0-9a-fA-F]+)\s+"
    r"hex=(?P<hex>[0-9a-fA-F]+)"
)
XV6_RUNTIME_RE = re.compile(
    r"dxg_d3d12_shared_runtime_len:(?P<size>\d+)\s+"
    r"dxg_d3d12_shared_runtime:(?P<hex>[0-9a-fA-F]*)"
)
XV6_ALLOC_PRIV_RE = re.compile(
    r"dxg_d3d12_shared_alloc_priv_len:(?P<size>\d+)\s+"
    r"dxg_d3d12_shared_alloc_priv:(?P<hex>[0-9a-fA-F]*)"
)
XV6_ALLOC_OUT_PRIV_RE = re.compile(
    r"dxg_d3d12_shared_alloc_out_priv_len:(?P<size>\d+)\s+"
    r"dxg_d3d12_shared_alloc_out_priv:(?P<hex>[0-9a-fA-F]*)"
)
NAMED_HEX_RE = re.compile(
    r"^(?P<name>WSL(?:\w*)?_RUNTIME|XV6(?:\w*)?_RUNTIME)\s+"
    r"(?P<hex>[0-9a-fA-F]+)\s*$"
)


@dataclass
class Blob:
    size: int
    data: bytes
    hash_text: str | None = None
    line: int = 0


@dataclass
class CreateAllocationEvent:
    line: int
    runtime_size: int
    flags: str
    device: str
    resource: str
    alloc_count: int
    runtime: Blob | None = None
    alloc_in: dict[int, Blob] = field(default_factory=dict)
    alloc_out: dict[int, Blob] = field(default_factory=dict)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare a WSL dxgtrace create_allocation_runtime_full line "
            "against an xv6 /dev/dxg dxg_d3d12_shared_runtime line."
        )
    )
    parser.add_argument("--wsl", required=True, help="WSL trace file")
    parser.add_argument("--xv6", required=True, help="xv6 log or /dev/dxg capture")
    parser.add_argument(
        "--size",
        type=int,
        default=264,
        help="preferred runtime blob size to select from each file",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="print matching qword/dword rows as well as mismatches",
    )
    parser.add_argument(
        "--alloc-private",
        action="store_true",
        help=(
            "also compare alloc-private input/output blobs paired with the "
            "selected WSL create_allocation_runtime_full event"
        ),
    )
    parser.add_argument(
        "--wsl-alloc-in-hash",
        help="select the WSL runtime event whose alloc-private input hash matches this hex value",
    )
    return parser.parse_args()


def bytes_from_hex(hex_text: str, source: str) -> bytes:
    if len(hex_text) % 2 != 0:
        raise ValueError(f"{source}: odd-length hex payload")
    return bytes.fromhex(hex_text)


def pick_blob(path: str, regex: re.Pattern[str], preferred_size: int,
              source: str, named_prefix: str | None = None) -> bytes:
    matches: list[tuple[int, bytes]] = []
    text = Path(path).read_text(errors="replace")
    for line in text.splitlines():
        match = regex.search(line)
        if match:
            size = int(match.group("size"))
            data = bytes_from_hex(match.group("hex"), f"{source}:{path}")
            matches.append((size, data))
            continue
        if named_prefix is not None:
            named = NAMED_HEX_RE.match(line)
            if named and named.group("name").startswith(named_prefix):
                data = bytes_from_hex(named.group("hex"), f"{source}:{path}")
                matches.append((len(data), data))

    if not matches:
        raise ValueError(f"{source}: no runtime blob found in {path}")

    for size, data in reversed(matches):
        if size == preferred_size or len(data) == preferred_size:
            return data
    return matches[-1][1]


def parse_wsl_events(path: str) -> list[CreateAllocationEvent]:
    text = Path(path).read_text(errors="replace")
    events: list[CreateAllocationEvent] = []
    current: CreateAllocationEvent | None = None

    for lineno, line in enumerate(text.splitlines(), 1):
        header = WSL_CREATE_ALLOCATION_RE.search(line)
        if header:
            current = CreateAllocationEvent(
                line=lineno,
                runtime_size=int(header.group("runtime")),
                flags=header.group("flags"),
                device=header.group("device"),
                resource=header.group("resource"),
                alloc_count=int(header.group("alloc_count")),
            )
            events.append(current)
            continue

        if current is None:
            continue

        runtime = WSL_RUNTIME_RE.search(line)
        if runtime:
            data = bytes_from_hex(runtime.group("hex"), f"wsl:{path}:{lineno}")
            current.runtime = Blob(
                size=int(runtime.group("size")),
                data=data,
                hash_text=runtime.group("hash").lower(),
                line=lineno,
            )
            continue

        alloc = WSL_ALLOC_PRIV_FULL_RE.search(line)
        if alloc:
            data = bytes_from_hex(alloc.group("hex"), f"wsl:{path}:{lineno}")
            blob = Blob(
                size=int(alloc.group("size")),
                data=data,
                hash_text=alloc.group("hash").lower(),
                line=lineno,
            )
            index = int(alloc.group("index"))
            phase = alloc.group("phase")
            if phase == "create_allocation_in":
                current.alloc_in[index] = blob
            elif phase == "create_allocation_out":
                current.alloc_out[index] = blob

    return events


def normalize_hash_text(hash_text: str | None) -> str | None:
    if hash_text is None:
        return None
    return hash_text.lower().removeprefix("0x")


def pick_wsl_event(path: str, preferred_size: int,
                   alloc_in_hash: str | None) -> CreateAllocationEvent:
    events = parse_wsl_events(path)
    requested_hash = normalize_hash_text(alloc_in_hash)
    candidates: list[CreateAllocationEvent] = []

    for event in events:
        if event.runtime is None:
            continue
        if event.runtime.size != preferred_size and len(event.runtime.data) != preferred_size:
            continue
        if requested_hash is not None:
            if all(blob.hash_text != requested_hash
                   for blob in event.alloc_in.values()):
                continue
        candidates.append(event)

    if not candidates:
        detail = f" runtime size {preferred_size}"
        if requested_hash is not None:
            detail += f" alloc-in hash 0x{requested_hash}"
        raise ValueError(f"wsl: no create_allocation event matching{detail} in {path}")
    return candidates[-1]


def read_u32(data: bytes, offset: int) -> int:
    chunk = data[offset:offset + 4]
    return int.from_bytes(chunk.ljust(4, b"\0"), "little")


def read_u64(data: bytes, offset: int) -> int:
    chunk = data[offset:offset + 8]
    return int.from_bytes(chunk.ljust(8, b"\0"), "little")


def pointer_like_u64(value: int) -> bool:
    if value == 0:
        return False
    high16 = value >> 48
    high32 = value >> 32
    low32 = value & 0xFFFFFFFF
    if high16 == 0 and 0x7000 <= high32 <= 0x7FFF:
        return True
    if high16 == 0 and 0x5500 <= high32 <= 0x6FFF and low32 == 0:
        return True
    if high16 == 0 and 0x55000000 <= (value >> 16) <= 0x7FFFFFFF:
        return True
    return False


def pointer_like_u32_pair(low: int, high: int) -> bool:
    return pointer_like_u64((high << 32) | low)


def classify_qword(wsl: int, xv6: int) -> str:
    if wsl == xv6:
        return "match"
    if pointer_like_u64(wsl) or pointer_like_u64(xv6):
        return "pointer-like"
    return "stable-scalar"


def classify_dword(offset: int, wsl_data: bytes, xv6_data: bytes) -> str:
    wsl = read_u32(wsl_data, offset)
    xv6 = read_u32(xv6_data, offset)
    if wsl == xv6:
        return "match"
    qoff = offset & ~0x7
    wsl_low = read_u32(wsl_data, qoff)
    wsl_high = read_u32(wsl_data, qoff + 4)
    xv6_low = read_u32(xv6_data, qoff)
    xv6_high = read_u32(xv6_data, qoff + 4)
    if (pointer_like_u32_pair(wsl_low, wsl_high) or
            pointer_like_u32_pair(xv6_low, xv6_high)):
        return "pointer-like"
    return "stable-scalar"


def print_qword_report(wsl: bytes, xv6: bytes, show_all: bool,
                       label: str = "qword_mismatches") -> int:
    limit = max(len(wsl), len(xv6))
    mismatches = 0
    print(f"{label}:")
    print("  off   wsl_qword          xv6_qword          class")
    for offset in range(0, limit, 8):
        wsl_value = read_u64(wsl, offset) if offset < len(wsl) else 0
        xv6_value = read_u64(xv6, offset) if offset < len(xv6) else 0
        klass = classify_qword(wsl_value, xv6_value)
        if klass != "match":
            mismatches += 1
        if show_all or klass != "match":
            print(
                f"  0x{offset:03x}  0x{wsl_value:016x}  "
                f"0x{xv6_value:016x}  {klass}"
            )
    return mismatches


def print_dword_report(wsl: bytes, xv6: bytes, show_all: bool,
                       label: str = "dword_mismatches") -> int:
    limit = max(len(wsl), len(xv6))
    mismatches = 0
    print(f"{label}:")
    print("  off   wsl_dword   xv6_dword   class")
    for offset in range(0, limit, 4):
        wsl_value = read_u32(wsl, offset) if offset < len(wsl) else 0
        xv6_value = read_u32(xv6, offset) if offset < len(xv6) else 0
        klass = classify_dword(offset, wsl, xv6)
        if klass != "match":
            mismatches += 1
        if show_all or klass != "match":
            print(
                f"  0x{offset:03x}  0x{wsl_value:08x}  "
                f"0x{xv6_value:08x}  {klass}"
            )
    return mismatches


def stable_scalar_offsets(data_a: bytes, data_b: bytes, width: int) -> list[int]:
    offsets: list[int] = []
    limit = max(len(data_a), len(data_b))
    for offset in range(0, limit, width):
        if width == 8:
            a = read_u64(data_a, offset) if offset < len(data_a) else 0
            b = read_u64(data_b, offset) if offset < len(data_b) else 0
            if classify_qword(a, b) == "stable-scalar":
                offsets.append(offset)
        else:
            if classify_dword(offset, data_a, data_b) == "stable-scalar":
                offsets.append(offset)
    return offsets


def print_blob_compare(name: str, wsl: bytes, xv6: bytes,
                       show_all: bool) -> tuple[int, int, list[int], list[int]]:
    print(f"{name}_blob_compare wsl_len={len(wsl)} xv6_len={len(xv6)}")
    if len(wsl) != len(xv6):
        print(f"{name}_length_mismatch=1")
    print(f"{name}_byte_equal={int(wsl == xv6)}")
    qword_mismatches = print_qword_report(
        wsl, xv6, show_all, f"{name}_qword_mismatches")
    dword_mismatches = print_dword_report(
        wsl, xv6, show_all, f"{name}_dword_mismatches")
    q_stable = stable_scalar_offsets(wsl, xv6, 8)
    d_stable = stable_scalar_offsets(wsl, xv6, 4)
    print(
        f"{name}_summary "
        f"qword_mismatches={qword_mismatches} "
        f"dword_mismatches={dword_mismatches} "
        "stable_qword_offsets="
        f"{','.join(f'0x{o:03x}' for o in q_stable) or 'none'} "
        "stable_dword_offsets="
        f"{','.join(f'0x{o:03x}' for o in d_stable) or 'none'}"
    )
    return qword_mismatches, dword_mismatches, q_stable, d_stable


def main() -> int:
    args = parse_args()
    try:
        event = pick_wsl_event(args.wsl, args.size, args.wsl_alloc_in_hash)
        assert event.runtime is not None
        wsl = event.runtime.data
        xv6 = pick_blob(args.xv6, XV6_RUNTIME_RE, args.size, "xv6", "XV6")
        if args.alloc_private:
            wsl_alloc_in = event.alloc_in.get(0)
            wsl_alloc_out = event.alloc_out.get(0)
            if wsl_alloc_in is None or wsl_alloc_out is None:
                raise ValueError(
                    "wsl: selected runtime event lacks paired alloc-private input/output"
                )
            xv6_alloc_in = pick_blob(
                args.xv6, XV6_ALLOC_PRIV_RE, wsl_alloc_in.size,
                "xv6-alloc-in")
            xv6_alloc_out = pick_blob(
                args.xv6, XV6_ALLOC_OUT_PRIV_RE, wsl_alloc_out.size,
                "xv6-alloc-out")
        else:
            wsl_alloc_in = None
            wsl_alloc_out = None
            xv6_alloc_in = b""
            xv6_alloc_out = b""
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print(
        "selected_wsl_event "
        f"line={event.line} runtime_line={event.runtime.line} "
        f"runtime_hash=0x{event.runtime.hash_text} "
        f"runtime_size={event.runtime.size} flags={event.flags} "
        f"alloc_in_hashes="
        f"{','.join(f'{i}:0x{b.hash_text}' for i, b in sorted(event.alloc_in.items())) or 'none'} "
        f"alloc_out_hashes="
        f"{','.join(f'{i}:0x{b.hash_text}' for i, b in sorted(event.alloc_out.items())) or 'none'}"
    )
    qword_mismatches, dword_mismatches, q_stable, d_stable = print_blob_compare(
        "runtime", wsl, xv6, args.all)
    print(
        "summary "
        f"qword_mismatches={qword_mismatches} "
        f"dword_mismatches={dword_mismatches} "
        "stable_qword_offsets="
        f"{','.join(f'0x{o:03x}' for o in q_stable) or 'none'} "
        "stable_dword_offsets="
        f"{','.join(f'0x{o:03x}' for o in d_stable) or 'none'}"
    )
    if args.alloc_private and wsl_alloc_in is not None and wsl_alloc_out is not None:
        print(
            "selected_alloc_private "
            f"in_line={wsl_alloc_in.line} in_hash=0x{wsl_alloc_in.hash_text} "
            f"out_line={wsl_alloc_out.line} out_hash=0x{wsl_alloc_out.hash_text}"
        )
        print_blob_compare("alloc_in", wsl_alloc_in.data, xv6_alloc_in, args.all)
        print_blob_compare("alloc_out", wsl_alloc_out.data, xv6_alloc_out, args.all)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
