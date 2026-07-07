#!/usr/bin/env python3
"""Attribute kprofile user-PC samples to Konsole launch phases."""

import collections
import hashlib
import os
import re
import shutil
import subprocess
import sys


KV_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_-]*)=("(?:[^"\\]|\\.)*"|\S+)')
LOAD_RE = re.compile(
    r"^\s*LOAD\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+"
    r"0x[0-9A-Fa-f]+\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)"
)
NM_RE = re.compile(
    r"^\s*([0-9A-Fa-f]+)(?:\s+([0-9A-Fa-f]+))?\s+([A-Za-z])\s+(.+)$"
)
SYMBOLIZED_MODULE_CLASSES = {"loader", "libc", "xv6_abi"}
SYMBOL_ROWS_PER_WINDOW = 12
TOOL_TIMEOUT_SEC = 20


def parse_kv(text):
    out = {}
    for key, value in KV_RE.findall(text):
        if value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
            value = value.replace(r'\"', '"').replace(r'\\', '\\')
        out[key] = value
    return out


def as_int(value, default=None, base=10):
    try:
        return int(value, base)
    except (TypeError, ValueError):
        return default


def as_hex(value, default=None):
    return as_int(value, default, 16)


def hex_value(value):
    return f"0x{value:x}" if value is not None else "missing"


def safe_value(value):
    value = str(value)
    if value == "":
        return "missing"
    value = value.replace("\\", "/")
    value = re.sub(r"[\r\n\t ]+", "_", value)
    value = value.replace("[", "(").replace("]", ")")
    value = value.replace("$", "S")
    return value


def row(**fields):
    return " ".join(f"{key}={safe_value(value)}" for key, value in fields.items())


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def script_repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def display_path(path):
    root = script_repo_root()
    abs_path = os.path.abspath(path)
    try:
        rel = os.path.relpath(abs_path, root)
    except ValueError:
        return abs_path
    if rel == "." or rel.startswith(".."):
        return abs_path
    return rel


def dedupe_existing_dirs(paths):
    out = []
    seen = set()
    for path in paths:
        if not path:
            continue
        abs_path = os.path.abspath(path)
        if abs_path in seen or not os.path.isdir(abs_path):
            continue
        seen.add(abs_path)
        out.append(abs_path)
    return out


def dedupe_existing_files(paths):
    out = []
    seen = set()
    for path in paths:
        if not path:
            continue
        abs_path = os.path.abspath(path)
        if abs_path in seen or not os.path.isfile(abs_path):
            continue
        seen.add(abs_path)
        out.append(abs_path)
    return out


def default_symbol_images(app_path):
    root = script_repo_root()
    archive_dir = os.path.abspath(os.path.dirname(app_path)) if app_path else ""
    env_images = [
        path
        for value in (
            os.environ.get("KPROFILE_USERPC_SYMBOL_IMAGES", ""),
            os.environ.get("FSIMG", ""),
        )
        for path in value.split(os.pathsep)
        if path
    ]
    return dedupe_existing_files(
        env_images
        + [
            os.path.join(archive_dir, "fs.img"),
            os.path.join(root, "build-x86_64", "fs.img"),
        ]
    )


def default_symbol_cache_dir(app_path):
    if not app_path:
        return None
    archive_dir = os.path.abspath(os.path.dirname(app_path))
    return os.path.join(archive_dir, ".kprofile-userpc-symbol-cache")


def default_symbol_roots(app_path):
    root = script_repo_root()
    archive_dir = os.path.abspath(os.path.dirname(app_path)) if app_path else ""
    return dedupe_existing_dirs(
        [
            os.path.join(archive_dir, "rootfs"),
            os.path.join(archive_dir, "sysroot"),
            os.path.join(root, "build-x86_64", "rootfs-generated-overlays", "kde-runtime"),
            os.path.join(root, "build-x86_64", "sysroot"),
            os.path.join(root, "build-x86_64", "rootfs-generated-overlays", "host-gui"),
            os.path.join(root, "build-x86_64", "webkit-runtime-cache", "root"),
            os.path.join(root, "build-x86_64", "webkit-runtime-deps-cache", "root"),
            os.path.join(root, "build-x86_64", "webkit-gst-runtime-cache", "root"),
            os.path.join(root, "build-x86_64", "webkit-gio-tls-runtime-cache", "root"),
        ]
    )


def guest_path_candidates(guest_path):
    if guest_path in ("", "-", "missing", "unmapped"):
        return []
    if not guest_path.startswith("/"):
        return [guest_path]

    rel = guest_path.lstrip("/")
    base = os.path.basename(guest_path)
    candidates = [rel]

    if guest_path.startswith("/lib64/") or guest_path.startswith("/usr/lib64/"):
        candidates.extend(
            [
                os.path.join("usr/lib/x86_64-linux-gnu", base),
                os.path.join("lib/x86_64-linux-gnu", base),
            ]
        )
    elif guest_path.startswith("/lib/"):
        rest = guest_path[len("/lib/") :]
        candidates.extend(
            [
                os.path.join("lib/x86_64-linux-gnu", rest),
                os.path.join("usr/lib/x86_64-linux-gnu", base),
            ]
        )
    elif guest_path.startswith("/usr/lib/") and "/x86_64-linux-gnu/" not in guest_path:
        rest = guest_path[len("/usr/lib/") :]
        candidates.append(os.path.join("usr/lib/x86_64-linux-gnu", rest))

    if guest_path == "/usr/lib/x86_64-linux-gnu/libc.so.6":
        candidates.append("lib/libc.so.6")
    if guest_path == "/lib64/ld-linux-x86-64.so.2":
        candidates.append("lib/ld-linux-x86-64.so.2")

    out = []
    seen = set()
    for candidate in candidates:
        normalized = os.path.normpath(candidate)
        if normalized in seen:
            continue
        seen.add(normalized)
        out.append(normalized)
    return out


def debugfs_guest_path(guest_path):
    if not guest_path.startswith("/") or "\x00" in guest_path:
        return None
    normalized = os.path.normpath(guest_path)
    if not normalized.startswith("/") or normalized == "/":
        return None
    if not re.match(r"^/[A-Za-z0-9_./+@%:,=-]+$", normalized):
        return None
    return normalized


def run_tool(args, timeout=TOOL_TIMEOUT_SEC):
    try:
        return subprocess.run(
            args,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.SubprocessError):
        return None


class OfflineSymbolizer:
    def __init__(self, roots, image_paths=None, image_cache_dir=None):
        self.roots = dedupe_existing_dirs(roots)
        self.image_paths = dedupe_existing_files(image_paths or [])
        self.image_cache_dir = image_cache_dir
        self.tools = {
            "addr2line": shutil.which("addr2line"),
            "debugfs": shutil.which("debugfs"),
            "readelf": shutil.which("readelf"),
            "nm": shutil.which("nm"),
        }
        self.host_elf_cache = {}
        self.image_elf_cache = {}
        self.load_segments_cache = {}
        self.nm_symbols_cache = {}
        self.image_cache_hits = 0
        self.image_extracts = 0
        self.image_extract_failures = 0

    def available_tools(self):
        return ",".join(name for name, path in sorted(self.tools.items()) if path) or "missing"

    def image_cache_root(self, image_path):
        if not self.image_cache_dir:
            return None
        try:
            st = os.stat(image_path)
        except OSError:
            return None
        identity = f"{os.path.abspath(image_path)}\0{st.st_size}\0{st.st_mtime_ns}"
        digest = hashlib.sha256(identity.encode("utf-8")).hexdigest()[:16]
        name = safe_value(os.path.basename(image_path)) or "fs.img"
        return os.path.join(self.image_cache_dir, f"{name}-{digest}")

    def extract_image_elf(self, image_path, guest_path):
        debugfs = self.tools.get("debugfs")
        normalized = debugfs_guest_path(guest_path)
        cache_root = self.image_cache_root(image_path)
        if not debugfs or not normalized or not cache_root:
            return None

        rel = normalized.lstrip("/")
        dest = os.path.join(cache_root, rel)
        if os.path.isfile(dest) and os.path.getsize(dest) > 0:
            self.image_cache_hits += 1
            return dest

        tmp = f"{dest}.tmp.{os.getpid()}"
        try:
            os.makedirs(os.path.dirname(dest), exist_ok=True)
        except OSError:
            self.image_extract_failures += 1
            return None

        try:
            if os.path.exists(tmp):
                os.unlink(tmp)
        except OSError:
            pass

        result = run_tool([debugfs, "-R", f"dump {normalized} {tmp}", image_path])
        if result is not None and result.returncode == 0 and os.path.isfile(tmp):
            try:
                if os.path.getsize(tmp) > 0:
                    os.replace(tmp, dest)
                    self.image_extracts += 1
                    return dest
            except OSError:
                pass

        try:
            if os.path.exists(tmp):
                os.unlink(tmp)
        except OSError:
            pass
        self.image_extract_failures += 1
        return None

    def find_image_elf(self, guest_path):
        if guest_path in self.image_elf_cache:
            return self.image_elf_cache[guest_path]

        for image_path in self.image_paths:
            host_elf = self.extract_image_elf(image_path, guest_path)
            if host_elf is not None:
                self.image_elf_cache[guest_path] = host_elf
                return host_elf
        self.image_elf_cache[guest_path] = None
        return None

    def find_host_elf(self, guest_path):
        if guest_path in self.host_elf_cache:
            return self.host_elf_cache[guest_path]

        for rel in guest_path_candidates(guest_path):
            for root in self.roots:
                candidate = os.path.join(root, rel)
                if os.path.isfile(candidate):
                    self.host_elf_cache[guest_path] = candidate
                    return candidate
        host_elf = self.find_image_elf(guest_path)
        self.host_elf_cache[guest_path] = host_elf
        return host_elf

    def load_segments(self, host_elf):
        if host_elf in self.load_segments_cache:
            return self.load_segments_cache[host_elf]

        segments = []
        readelf = self.tools.get("readelf")
        if readelf:
            result = run_tool([readelf, "-W", "-l", host_elf])
            if result is not None and result.returncode == 0:
                for line in result.stdout.splitlines():
                    match = LOAD_RE.match(line)
                    if not match:
                        continue
                    offset = int(match.group(1), 16)
                    vaddr = int(match.group(2), 16)
                    filesz = int(match.group(3), 16)
                    memsz = int(match.group(4), 16)
                    size = max(filesz, memsz)
                    if size > 0:
                        segments.append((offset, vaddr, size))
        self.load_segments_cache[host_elf] = segments
        return segments

    def elf_addr_for_file_offset(self, host_elf, file_offset):
        for offset, vaddr, size in self.load_segments(host_elf):
            if offset <= file_offset < offset + size:
                return vaddr + (file_offset - offset)
        return file_offset

    def addr2line_symbols(self, host_elf, addresses):
        addr2line = self.tools.get("addr2line")
        if not addr2line or not addresses:
            return {}

        ordered = sorted(set(addresses))
        result = run_tool([addr2line, "-f", "-C", "-e", host_elf] + [hex(addr) for addr in ordered])
        if result is None or result.returncode != 0:
            return {}

        lines = result.stdout.splitlines()
        out = {}
        for index, addr in enumerate(ordered):
            func_index = index * 2
            if func_index + 1 >= len(lines):
                break
            symbol = lines[func_index].strip()
            source = lines[func_index + 1].strip()
            if not symbol or symbol == "??":
                continue
            if source in ("", "??", "??:0", "??:?", ":0", ":?"):
                source = "missing"
            out[addr] = {
                "symbol": symbol,
                "source": source,
                "method": "addr2line",
            }
        return out

    def nm_symbols(self, host_elf):
        if host_elf in self.nm_symbols_cache:
            return self.nm_symbols_cache[host_elf]

        symbols = []
        nm = self.tools.get("nm")
        if nm:
            for opts in (
                [nm, "-D", "-S", "--defined-only", "--numeric-sort", host_elf],
                [nm, "-S", "--defined-only", "--numeric-sort", host_elf],
            ):
                result = run_tool(opts)
                if result is None or result.returncode != 0:
                    continue
                parsed = []
                for line in result.stdout.splitlines():
                    match = NM_RE.match(line)
                    if not match:
                        continue
                    addr = int(match.group(1), 16)
                    size = int(match.group(2), 16) if match.group(2) else 0
                    sym_type = match.group(3)
                    name = match.group(4).strip()
                    if sym_type not in "TtWwIi" or size <= 0 or not name:
                        continue
                    name = re.sub(r"@@?.*$", "", name)
                    parsed.append((addr, addr + size, name))
                if parsed:
                    symbols = parsed
                    break
        self.nm_symbols_cache[host_elf] = symbols
        return symbols

    def nm_symbol_for_addr(self, host_elf, addr):
        for start, end, name in self.nm_symbols(host_elf):
            if start <= addr < end:
                return {
                    "symbol": name,
                    "source": "missing",
                    "method": "nm",
                }
        return None

    def resolve_many(self, host_elf, addresses):
        resolved = self.addr2line_symbols(host_elf, addresses)
        for addr in addresses:
            if addr in resolved:
                continue
            fallback = self.nm_symbol_for_addr(host_elf, addr)
            if fallback is not None:
                resolved[addr] = fallback
        return resolved


def parse_counter_line(line):
    match = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s+(-?[0-9]+)$", line)
    if not match:
        return None
    return match.group(1), int(match.group(2))


def parse_inputs(app_text, phase_text):
    samples = []
    maps = []
    userpc_summary = {}
    maps_summary = {}
    summary_modules = []
    phase = {}
    counters = {}

    for source_text in (app_text, phase_text):
        for raw_line in source_text.splitlines():
            line = raw_line.strip()
            if "kde_interaction_helper wayland-debug-ms=" in line:
                _, _, line = line.partition(" line=")
            if "kde_app_launch_probe konsole_phase" in line:
                phase.update(parse_kv(line))
            elif "kde_app_launch_probe launch " in line:
                phase.update(parse_kv(line))

    for raw_line in app_text.splitlines():
        line = raw_line.strip()
        if "kde_interaction_helper wayland-debug-ms=" in line:
            _, _, line = line.partition(" line=")
        if line.startswith("kprofile-userpc-summary:"):
            userpc_summary.update(parse_kv(line))
            continue
        if line.startswith("kprofile-userpc-maps:"):
            maps_summary.update(parse_kv(line))
            continue
        if line.startswith("kprofile-userpc-module:"):
            summary_modules.append(parse_kv(line))
            continue
        if line.startswith("kprofile-userpc-map:"):
            kv = parse_kv(line)
            start = as_hex(kv.get("start"))
            end = as_hex(kv.get("end"))
            tgid = as_int(kv.get("tgid"))
            if start is None or end is None or tgid is None:
                continue
            maps.append(
                {
                    "tgid": tgid,
                    "start": start,
                    "end": end,
                    "perms": kv.get("perms", "missing"),
                    "offset": as_hex(kv.get("offset")),
                    "dev": kv.get("dev", "missing"),
                    "inode": kv.get("inode", "missing"),
                    "path": kv.get("path", "missing"),
                }
            )
            continue
        if line.startswith("kprofile-userpc:"):
            kv = parse_kv(line)
            seq = as_int(kv.get("seq"))
            uptime = as_int(kv.get("uptime_ms"))
            tgid = as_int(kv.get("tgid"))
            rip = as_hex(kv.get("rip"))
            if seq is None or uptime is None or tgid is None or rip is None:
                continue
            samples.append(
                {
                    "seq": seq,
                    "uptime_ms": uptime,
                    "tgid": tgid,
                    "rip": rip,
                    "comm": kv.get("comm", "missing"),
                    "pid": as_int(kv.get("pid")),
                }
            )
            continue
        parsed_counter = parse_counter_line(line)
        if parsed_counter is not None:
            key, value = parsed_counter
            counters[key] = value

    maps_by_tgid = collections.defaultdict(list)
    seen_maps = set()
    for mapping in maps:
        ident = (
            mapping["tgid"],
            mapping["start"],
            mapping["end"],
            mapping["perms"],
            mapping["offset"],
            mapping["dev"],
            mapping["inode"],
            mapping["path"],
        )
        if ident in seen_maps:
            continue
        seen_maps.add(ident)
        maps_by_tgid[mapping["tgid"]].append(mapping)
    for entries in maps_by_tgid.values():
        entries.sort(key=lambda item: (item["start"], item["end"]))

    return samples, maps, maps_by_tgid, userpc_summary, maps_summary, summary_modules, phase, counters


def mapping_for_sample(sample, maps_by_tgid):
    for mapping in maps_by_tgid.get(sample["tgid"], []):
        if mapping["start"] <= sample["rip"] < mapping["end"]:
            return mapping
    return None


def module_for_sample(sample, maps_by_tgid):
    mapping = mapping_for_sample(sample, maps_by_tgid)
    return mapping["path"] if mapping is not None else "unmapped"


def sample_file_offset(sample, mapping):
    if mapping is None or mapping.get("offset") is None:
        return None
    return mapping["offset"] + (sample["rip"] - mapping["start"])


def module_class(module):
    if module in ("unmapped", "-", "missing"):
        return "unmapped"
    lowered = module.lower()
    base = lowered.rsplit("/", 1)[-1]
    if "ld-linux" in base or "ld-musl" in base or re.match(r"ld-[^/]*\.so", base):
        return "loader"
    if "libc.so" in base or base.startswith("libpthread") or base.startswith("libdl"):
        return "libc"
    if base.startswith("libqt") or "/qt" in lowered or "qtwayland" in lowered:
        return "qt"
    if "konsole" in lowered or base == "kde-app-launch-probe":
        return "konsole"
    if (
        "mesa" in lowered
        or "gallium" in lowered
        or "virgl" in lowered
        or base.startswith(("libgl.so", "libglx", "libegl", "libgbm", "libdrm"))
        or base.startswith("libglapi")
        or "/dri/" in lowered
    ):
        return "mesa"
    if "fontconfig" in lowered or "libfreetype" in base or "libharfbuzz" in base:
        return "fontconfig"
    if "xv6" in lowered or "/opt/xv6-kde-abi-libs/" in lowered:
        return "xv6_abi"
    return "other"


def loader_category(symbol, source, module_kind):
    if module_kind not in SYMBOLIZED_MODULE_CLASSES:
        return "unknown"

    symbol_l = (symbol or "").lower()
    source_l = (source or "").lower()
    combined = f"{symbol_l} {source_l}"
    if not symbol_l or symbol_l in ("missing", "??"):
        return "unknown"

    if "ifunc" in combined or "/multiarch/" in source_l:
        return "ifunc_string_mem"

    if "tls" in combined or "dl-tls" in source_l:
        return "tls_init"

    lookup_markers = (
        "_dl_new_hash",
        "do_lookup_x",
        "_dl_lookup_symbol",
        "check_match",
        "strcmp",
        "dl-lookup",
        "dl-new-hash",
        "gnu_hash",
        "elf_hash",
    )
    if any(marker in combined for marker in lookup_markers):
        return "lookup_hash"

    relocation_markers = (
        "rela",
        "reloc",
        "_dl_fixup",
        "_dl_runtime_resolve",
        "elf_dynamic_do_",
        "elf_machine_rela",
        "dl-reloc",
        "dynamic-link",
        "dl-machine",
    )
    if any(marker in combined for marker in relocation_markers):
        return "relocation"

    mmap_open_markers = (
        "_dl_map_object",
        "_dl_map_segments",
        "open_path",
        "open_verify",
        "map_segment",
        "mmap",
        "munmap",
        "dl-load",
        "dl-map-segments",
    )
    if any(marker in combined for marker in mmap_open_markers):
        return "mmap_open"

    string_mem_names = (
        "memcpy",
        "memmove",
        "memset",
        "memcmp",
        "strlen",
        "strcmp",
        "strncmp",
        "strcpy",
        "stpcpy",
        "strchr",
        "rawmemchr",
        "wmem",
    )
    if any(name in symbol_l for name in string_mem_names):
        return "ifunc_string_mem"

    if module_kind in ("libc", "xv6_abi"):
        return "libc_support"
    if module_kind == "loader":
        return "other_loader"
    return "unknown"


def first_int(*values):
    for value in values:
        parsed = as_int(value)
        if parsed is not None and parsed >= 0:
            return parsed
    return None


def phase_times(phase):
    launch_uptime = first_int(
        phase.get("launch_start_uptime_ms"),
        phase.get("konsole_launch_start_uptime_ms"),
    )
    launch_old = first_int(phase.get("konsole_launch_start_ms"), phase.get("launch_start_ms"))
    ptmx_delta = first_int(phase.get("pty_ptmx_since_launch_ms"))
    wrapper_delta = first_int(phase.get("wrapper_start_since_launch_ms"))
    prompt_delta = first_int(phase.get("bash_prompt_since_launch_ms"))
    source = "uptime_fields" if launch_uptime is not None else "fallback_monotonic"
    times = {
        "source": source,
        "fallback_used": 0,
        "launch_start": launch_uptime if launch_uptime is not None else launch_old,
        "ptmx": first_int(phase.get("pty_ptmx_uptime_ms"), phase.get("ptmx_uptime_ms")),
        "pty_pts": first_int(phase.get("pty_pts_uptime_ms")),
        "wrapper_start": first_int(phase.get("wrapper_start_uptime_ms")),
        "bash_prompt": first_int(phase.get("bash_prompt_uptime_ms")),
    }
    if times["ptmx"] is None and times["launch_start"] is not None and ptmx_delta is not None:
        times["ptmx"] = times["launch_start"] + ptmx_delta
        times["fallback_used"] = 1
    if (
        times["wrapper_start"] is None
        and times["launch_start"] is not None
        and wrapper_delta is not None
    ):
        times["wrapper_start"] = times["launch_start"] + wrapper_delta
        times["fallback_used"] = 1
    if (
        times["bash_prompt"] is None
        and times["launch_start"] is not None
        and prompt_delta is not None
    ):
        times["bash_prompt"] = times["launch_start"] + prompt_delta
        times["fallback_used"] = 1
    if launch_uptime is None:
        times["fallback_used"] = 1
    return times


def build_windows(samples, times):
    sample_start = min(sample["uptime_ms"] for sample in samples)
    sample_end = max(sample["uptime_ms"] for sample in samples)
    sample_end_exclusive = sample_end + 1
    windows = []

    def add(name, start, end, missing_reason):
        if start is None or end is None:
            return
        windows.append((name, start, end, missing_reason))

    add("pre_launch", sample_start, times["launch_start"], "missing_launch_start")
    add("launch_to_ptmx", times["launch_start"], times["ptmx"], "missing_ptmx")
    add("ptmx_to_wrapper", times["ptmx"], times["wrapper_start"], "missing_wrapper_start")
    add("wrapper_to_prompt", times["wrapper_start"], times["bash_prompt"], "missing_bash_prompt")
    add("launch_to_prompt", times["launch_start"], times["bash_prompt"], "missing_bash_prompt")
    add("post_prompt", times["bash_prompt"], sample_end_exclusive, "missing_bash_prompt")
    return sample_start, sample_end, windows


def summarize_window(name, start, end, samples, maps_by_tgid):
    selected = [sample for sample in samples if start <= sample["uptime_ms"] < end]
    modules_by_path = {}
    unmapped = 0
    for sample in selected:
        mapping = mapping_for_sample(sample, maps_by_tgid)
        module = mapping["path"] if mapping is not None else "unmapped"
        if module == "unmapped":
            unmapped += 1
        stats = modules_by_path.setdefault(
            module,
            {
                "tgids": set(),
                "pids": set(),
                "comms": set(),
                "map_ids": set(),
                "module": module,
                "module_class": module_class(module),
                "samples": 0,
                "first_seq": sample["seq"],
                "last_seq": sample["seq"],
                "min_rip": sample["rip"],
                "max_rip": sample["rip"],
                "min_file_offset": None,
                "max_file_offset": None,
            },
        )
        stats["tgids"].add(sample["tgid"])
        if sample["pid"] is not None:
            stats["pids"].add(sample["pid"])
        stats["comms"].add(sample["comm"])
        if mapping is not None:
            stats["map_ids"].add(
                (
                    mapping["tgid"],
                    mapping["start"],
                    mapping["end"],
                    mapping["offset"],
                    mapping["dev"],
                    mapping["inode"],
                )
            )
        stats["samples"] += 1
        stats["first_seq"] = min(stats["first_seq"], sample["seq"])
        stats["last_seq"] = max(stats["last_seq"], sample["seq"])
        stats["min_rip"] = min(stats["min_rip"], sample["rip"])
        stats["max_rip"] = max(stats["max_rip"], sample["rip"])
        file_offset = sample_file_offset(sample, mapping)
        if file_offset is not None:
            if stats["min_file_offset"] is None or file_offset < stats["min_file_offset"]:
                stats["min_file_offset"] = file_offset
            if stats["max_file_offset"] is None or file_offset > stats["max_file_offset"]:
                stats["max_file_offset"] = file_offset

    first_seq = min((sample["seq"] for sample in selected), default="missing")
    last_seq = max((sample["seq"] for sample in selected), default="missing")
    ranked = sorted(
        modules_by_path.values(),
        key=lambda item: (-item["samples"], item["first_seq"], item["module"]),
    )
    for item in ranked:
        tgids = sorted(item["tgids"])
        item["tgid"] = str(tgids[0]) if len(tgids) == 1 else "mixed"
        pids = sorted(item["pids"])
        item["pid"] = str(pids[0]) if len(pids) == 1 else ("mixed" if pids else "missing")
        comms = sorted(item["comms"])
        item["comm"] = comms[0] if len(comms) == 1 else "mixed"
        item["map_count"] = len(item["map_ids"])
    top = ranked[0] if ranked else None
    reason = "ok" if selected else "no_userpc_samples_in_window"
    return {
        "name": name,
        "start": start,
        "end": end,
        "samples": len(selected),
        "first_seq": first_seq,
        "last_seq": last_seq,
        "unmapped": unmapped,
        "top_module": top["module"] if top else "missing",
        "top_samples": top["samples"] if top else 0,
        "reason": reason,
        "ranked": ranked,
    }


def pct(count, total):
    if total <= 0:
        return "0.00"
    return f"{(count * 100.0 / total):.2f}"


def emit_if_any(phase_name, **fields):
    if any(value != "missing" for key, value in fields.items() if key != "action"):
        print(row(phase=phase_name, **fields))


def counter(counters, key):
    return counters.get(key, "missing")


def emit_counter_summaries(counters):
    emit_if_any(
        "direct-launch-kprofile-syscall-summary",
        action="kprofile-counter",
        sys_openat_calls=counter(counters, "sys_openat_calls"),
        sys_openat_ms=counter(counters, "sys_openat_ms"),
        sys_openat_lookup_calls=counter(counters, "sys_openat_lookup_calls"),
        sys_openat_lookup_ms=counter(counters, "sys_openat_lookup_ms"),
        sys_openat_fileopen_calls=counter(counters, "sys_openat_fileopen_calls"),
        sys_openat_fileopen_ms=counter(counters, "sys_openat_fileopen_ms"),
        sys_poll_calls=counter(counters, "sys_poll_calls"),
        sys_poll_ms=counter(counters, "sys_poll_ms"),
        sys_ppoll_calls=counter(counters, "sys_ppoll_calls"),
        sys_ppoll_ms=counter(counters, "sys_ppoll_ms"),
        sys_poll_blocking_calls=counter(counters, "sys_poll_blocking_calls"),
        sys_poll_blocking_ms=counter(counters, "sys_poll_blocking_ms"),
    )
    emit_if_any(
        "direct-launch-kprofile-vfs-summary",
        action="kprofile-counter",
        ext4_lookup_calls=counter(counters, "ext4_lookup_calls"),
        ext4_lookup_lock_hold_ms=counter(counters, "ext4_lookup_lock_hold_ms"),
        ext4_lookup_dir_find_ms=counter(counters, "ext4_lookup_dir_find_ms"),
        ext4_lookup_found=counter(counters, "ext4_lookup_found"),
        ext4_lookup_enoent=counter(counters, "ext4_lookup_enoent"),
        ext4_pcache_read_page_calls=counter(counters, "ext4_pcache_read_page_calls"),
        ext4_pcache_read_page_ms=counter(counters, "ext4_pcache_read_page_ms"),
        ext4_pcache_pages_filled=counter(counters, "ext4_pcache_pages_filled"),
        ext4_pcache_readahead_pages=counter(counters, "ext4_pcache_readahead_pages"),
        ext4_fault_calls=counter(counters, "ext4_fault_calls"),
        ext4_fault_ms=counter(counters, "ext4_fault_ms"),
        vm_file_faults=counter(counters, "vm_file_faults"),
    )
    emit_if_any(
        "direct-launch-kprofile-prepty-summary",
        action="kprofile-counter",
        konsole_prepty_poll_total_calls=counter(counters, "konsole_prepty_poll_total_calls"),
        konsole_prepty_poll_total_ms=counter(counters, "konsole_prepty_poll_total_ms"),
        konsole_prepty_poll_wayland_calls=counter(counters, "konsole_prepty_poll_wayland_calls"),
        konsole_prepty_poll_wayland_ms=counter(counters, "konsole_prepty_poll_wayland_ms"),
        konsole_prepty_poll_qdbus_calls=counter(counters, "konsole_prepty_poll_qdbus_calls"),
        konsole_prepty_poll_qdbus_ms=counter(counters, "konsole_prepty_poll_qdbus_ms"),
    )


def parse_args(argv):
    symbolize = True
    symbol_roots = []
    positionals = []
    index = 1
    while index < len(argv):
        arg = argv[index]
        if arg == "--no-symbolize":
            symbolize = False
        elif arg == "--symbol-root":
            index += 1
            if index >= len(argv):
                return None
            symbol_roots.append(argv[index])
        elif arg.startswith("--symbol-root="):
            symbol_roots.append(arg.split("=", 1)[1])
        else:
            positionals.append(arg)
        index += 1
    if len(positionals) != 2:
        return None
    return {
        "app_path": positionals[0],
        "phase_path": positionals[1],
        "symbolize": symbolize,
        "symbol_roots": symbol_roots,
    }


def selected_samples_for_window(samples, start, end):
    return [sample for sample in samples if start <= sample["uptime_ms"] < end]


def collect_symbolized_samples(samples, maps_by_tgid, symbolizer):
    stats = {
        "interesting_samples": 0,
        "attempted_samples": 0,
        "resolved_samples": 0,
        "missing_elf_samples": 0,
        "no_offset_samples": 0,
        "unresolved_samples": 0,
        "resolved_loader_samples": 0,
        "resolved_libc_samples": 0,
        "resolved_xv6_abi_samples": 0,
    }
    missing_modules = set()
    pending = []
    pending_by_host = collections.defaultdict(set)

    for sample in samples:
        mapping = mapping_for_sample(sample, maps_by_tgid)
        module = mapping["path"] if mapping is not None else "unmapped"
        module_kind = module_class(module)
        if module_kind not in SYMBOLIZED_MODULE_CLASSES:
            continue
        stats["interesting_samples"] += 1
        file_offset = sample_file_offset(sample, mapping)
        if file_offset is None:
            stats["no_offset_samples"] += 1
            continue
        host_elf = symbolizer.find_host_elf(module)
        if host_elf is None:
            stats["missing_elf_samples"] += 1
            missing_modules.add(module)
            continue
        elf_addr = symbolizer.elf_addr_for_file_offset(host_elf, file_offset)
        stats["attempted_samples"] += 1
        entry = {
            "sample": sample,
            "mapping": mapping,
            "module": module,
            "module_class": module_kind,
            "file_offset": file_offset,
            "elf_addr": elf_addr,
            "host_elf": host_elf,
        }
        pending.append(entry)
        pending_by_host[host_elf].add(elf_addr)

    resolved_by_host = {}
    for host_elf, addresses in pending_by_host.items():
        resolved_by_host[host_elf] = symbolizer.resolve_many(host_elf, addresses)

    resolved = {}
    for entry in pending:
        symbol = resolved_by_host.get(entry["host_elf"], {}).get(entry["elf_addr"])
        if symbol is None:
            stats["unresolved_samples"] += 1
            continue
        entry = dict(entry)
        entry.update(symbol)
        entry["category"] = loader_category(
            entry.get("symbol"), entry.get("source"), entry["module_class"]
        )
        seq = entry["sample"]["seq"]
        resolved[seq] = entry
        stats["resolved_samples"] += 1
        class_key = f"resolved_{entry['module_class']}_samples"
        if class_key in stats:
            stats[class_key] += 1

    return resolved, stats, missing_modules


def finish_symbol_stats(stats):
    tgids = sorted(stats["tgids"])
    pids = sorted(stats["pids"])
    comms = sorted(stats["comms"])
    stats["tgid"] = str(tgids[0]) if len(tgids) == 1 else "mixed"
    stats["pid"] = str(pids[0]) if len(pids) == 1 else ("mixed" if pids else "missing")
    stats["comm"] = comms[0] if len(comms) == 1 else "mixed"
    return stats


def emit_symbolization(samples, maps_by_tgid, windows, symbol_roots, symbol_images, symbol_cache_dir):
    symbolizer = OfflineSymbolizer(symbol_roots, symbol_images, symbol_cache_dir)
    if not any(symbolizer.tools.values()):
        return

    symbolized, stats, missing_modules = collect_symbolized_samples(
        samples, maps_by_tgid, symbolizer
    )
    if not symbolized:
        return

    resolved_modules = sorted({entry["module"] for entry in symbolized.values()})
    resolved_host_elves = sorted({entry["host_elf"] for entry in symbolized.values()})
    print(
        row(
            phase="direct-launch-symbolization",
            action="kprofile-userpc-symbolize",
            status="PASS",
            target_module_classes="loader,libc,xv6_abi",
            tools=symbolizer.available_tools(),
            symbol_root_count=len(symbolizer.roots),
            primary_symbol_root=display_path(symbolizer.roots[0]) if symbolizer.roots else "missing",
            symbol_image_count=len(symbolizer.image_paths),
            primary_symbol_image=display_path(symbolizer.image_paths[0])
            if symbolizer.image_paths
            else "missing",
            image_cache_dir=display_path(symbolizer.image_cache_dir)
            if symbolizer.image_cache_dir
            else "missing",
            image_cache_hits=symbolizer.image_cache_hits,
            image_extracts=symbolizer.image_extracts,
            image_extract_failures=symbolizer.image_extract_failures,
            interesting_samples=stats["interesting_samples"],
            attempted_samples=stats["attempted_samples"],
            resolved_samples=stats["resolved_samples"],
            resolved_sample_pct=pct(stats["resolved_samples"], stats["interesting_samples"]),
            unresolved_samples=stats["unresolved_samples"],
            missing_elf_samples=stats["missing_elf_samples"],
            no_offset_samples=stats["no_offset_samples"],
            missing_elf_modules=len(missing_modules),
            resolved_modules=len(resolved_modules),
            resolved_host_elves=len(resolved_host_elves),
            resolved_loader_samples=stats["resolved_loader_samples"],
            resolved_libc_samples=stats["resolved_libc_samples"],
            resolved_xv6_abi_samples=stats["resolved_xv6_abi_samples"],
        )
    )

    for name, start, end, _missing_reason in windows:
        selected = selected_samples_for_window(samples, start, end)
        by_symbol = {}
        by_category = {}
        symbolized_in_window = 0
        for sample in selected:
            mapping = mapping_for_sample(sample, maps_by_tgid)
            module = mapping["path"] if mapping is not None else "unmapped"
            module_kind = module_class(module)
            entry = symbolized.get(sample["seq"])
            if module_kind in SYMBOLIZED_MODULE_CLASSES:
                category = entry["category"] if entry is not None else "unknown"
                category_key = (category, module_kind)
                category_item = by_category.setdefault(
                    category_key,
                    {
                        "category": category,
                        "module_class": module_kind,
                        "samples": 0,
                        "symbolized_samples": 0,
                        "first_seq": sample["seq"],
                        "last_seq": sample["seq"],
                    },
                )
                category_item["samples"] += 1
                if entry is not None:
                    category_item["symbolized_samples"] += 1
                category_item["first_seq"] = min(category_item["first_seq"], sample["seq"])
                category_item["last_seq"] = max(category_item["last_seq"], sample["seq"])
            if entry is None:
                continue
            symbolized_in_window += 1
            key = (
                entry["module"],
                entry["module_class"],
                entry["category"],
                entry["symbol"],
                entry["source"],
                entry["host_elf"],
                entry["method"],
            )
            item = by_symbol.setdefault(
                key,
                {
                    "module": entry["module"],
                    "module_class": entry["module_class"],
                    "category": entry["category"],
                    "symbol": entry["symbol"],
                    "source": entry["source"],
                    "host_elf": entry["host_elf"],
                    "method": entry["method"],
                    "samples": 0,
                    "tgids": set(),
                    "pids": set(),
                    "comms": set(),
                    "first_seq": sample["seq"],
                    "last_seq": sample["seq"],
                    "min_file_offset": entry["file_offset"],
                    "max_file_offset": entry["file_offset"],
                    "min_elf_addr": entry["elf_addr"],
                    "max_elf_addr": entry["elf_addr"],
                },
            )
            item["samples"] += 1
            item["tgids"].add(sample["tgid"])
            if sample["pid"] is not None:
                item["pids"].add(sample["pid"])
            item["comms"].add(sample["comm"])
            item["first_seq"] = min(item["first_seq"], sample["seq"])
            item["last_seq"] = max(item["last_seq"], sample["seq"])
            item["min_file_offset"] = min(item["min_file_offset"], entry["file_offset"])
            item["max_file_offset"] = max(item["max_file_offset"], entry["file_offset"])
            item["min_elf_addr"] = min(item["min_elf_addr"], entry["elf_addr"])
            item["max_elf_addr"] = max(item["max_elf_addr"], entry["elf_addr"])

        ranked_categories = sorted(
            by_category.values(),
            key=lambda item: (
                -item["samples"],
                item["first_seq"],
                item["module_class"],
                item["category"],
            ),
        )
        for rank, item in enumerate(ranked_categories, start=1):
            print(
                row(
                    phase="direct-launch-phase-loader-category",
                    action="kprofile-userpc-symbolize",
                    phase_window=name,
                    rank=rank,
                    module_class=item["module_class"],
                    category=item["category"],
                    samples=item["samples"],
                    sample_pct=pct(item["samples"], len(selected)),
                    symbolized_samples=item["symbolized_samples"],
                    symbolized_sample_pct=pct(item["symbolized_samples"], item["samples"]),
                    first_seq=item["first_seq"],
                    last_seq=item["last_seq"],
                )
            )

        ranked = sorted(
            (finish_symbol_stats(item) for item in by_symbol.values()),
            key=lambda item: (-item["samples"], item["first_seq"], item["module"], item["symbol"]),
        )
        for rank, item in enumerate(ranked[:SYMBOL_ROWS_PER_WINDOW], start=1):
            print(
                row(
                    phase="direct-launch-phase-symbol",
                    action="kprofile-userpc-symbolize",
                    phase_window=name,
                    rank=rank,
                    tgid=item["tgid"],
                    pid=item["pid"],
                    comm=item["comm"],
                    module=item["module"],
                    module_class=item["module_class"],
                    category=item["category"],
                    symbol=item["symbol"],
                    source=item["source"],
                    samples=item["samples"],
                    sample_pct=pct(item["samples"], len(selected)),
                    symbolized_sample_pct=pct(item["samples"], symbolized_in_window),
                    first_seq=item["first_seq"],
                    last_seq=item["last_seq"],
                    min_file_offset=hex_value(item["min_file_offset"]),
                    max_file_offset=hex_value(item["max_file_offset"]),
                    min_elf_addr=hex_value(item["min_elf_addr"]),
                    max_elf_addr=hex_value(item["max_elf_addr"]),
                    host_elf=display_path(item["host_elf"]),
                    method=item["method"],
                )
            )


def emit_skip(status, reason, app_path, phase_path):
    print(
        row(
            phase="direct-launch-summary",
            action="kprofile-userpc-phase-attribution",
            status=status,
            reason=reason,
            app_log=app_path,
            phase_log=phase_path,
        )
    )


def main(argv):
    args = parse_args(argv)
    if args is None:
        emit_skip("FAIL", "usage_app_log_phase_log", "missing", "missing")
        return 0

    app_path = args["app_path"]
    phase_path = args["phase_path"]
    try:
        app_text = read_text(app_path)
    except OSError:
        emit_skip("SKIP", "missing_app_log", app_path, phase_path)
        return 0
    try:
        phase_text = read_text(phase_path)
    except OSError:
        phase_text = ""

    (
        samples,
        maps,
        maps_by_tgid,
        summary,
        maps_summary,
        summary_modules,
        phase,
        counters,
    ) = parse_inputs(
        app_text, phase_text
    )
    if not samples:
        emit_skip("SKIP", "no_userpc_samples", app_path, phase_path)
        return 0

    times = phase_times(phase)
    if times["launch_start"] is None:
        emit_skip("SKIP", "missing_launch_start", app_path, phase_path)
        return 0

    sample_start, sample_end, windows = build_windows(samples, times)
    sample_end_exclusive = sample_end + 1
    launch_to_prompt_samples = [
        sample
        for sample in samples
        if times["bash_prompt"] is not None
        and times["launch_start"] <= sample["uptime_ms"] < times["bash_prompt"]
    ]
    covers_launch_to_prompt = (
        times["bash_prompt"] is not None
        and sample_start <= times["launch_start"]
        and sample_end >= times["bash_prompt"]
    )
    samples_end_before_ptmx = times["ptmx"] is not None and sample_end < times["ptmx"]
    samples_end_before_prompt = times["bash_prompt"] is not None and sample_end < times["bash_prompt"]
    print(
        row(
            phase="direct-launch-summary",
            action="kprofile-userpc-phase-attribution",
            status="PASS",
            time_base="uptime_ms",
            time_source=times["source"],
            fallback_used=times["fallback_used"],
            samples=len(samples),
            stored=summary.get("stored", len(samples)),
            maps=maps_summary.get("maps", len(maps)),
            map_dropped=maps_summary.get("map_dropped", "missing"),
            unmapped_samples=maps_summary.get("unmapped_samples", "missing"),
            dropped=summary.get("dropped", "missing"),
            overwritten=summary.get("overwritten", "missing"),
            summary_modules=len(summary_modules),
            sample_start_uptime_ms=sample_start,
            sample_end_uptime_ms=sample_end,
            sample_duration_ms=sample_end_exclusive - sample_start,
            launch_start_uptime_ms=times["launch_start"],
            ptmx_uptime_ms=times["ptmx"] if times["ptmx"] is not None else "missing",
            pty_pts_uptime_ms=times["pty_pts"]
            if times["pty_pts"] is not None
            else "missing",
            wrapper_start_uptime_ms=times["wrapper_start"]
            if times["wrapper_start"] is not None
            else "missing",
            bash_prompt_uptime_ms=times["bash_prompt"]
            if times["bash_prompt"] is not None
            else "missing",
            app_log=app_path,
            phase_log=phase_path if phase_text else "missing",
        )
    )
    print(
        row(
            phase="direct-launch-phase-coverage",
            action="kprofile-userpc",
            time_base="uptime_ms",
            sample_start_uptime_ms=sample_start,
            sample_end_uptime_ms=sample_end,
            sample_duration_ms=sample_end_exclusive - sample_start,
            launch_to_prompt_duration_ms=times["bash_prompt"] - times["launch_start"]
            if times["bash_prompt"] is not None
            else "missing",
            launch_to_prompt_samples=len(launch_to_prompt_samples)
            if times["bash_prompt"] is not None
            else "missing",
            launch_to_prompt_sample_pct=pct(len(launch_to_prompt_samples), len(samples))
            if times["bash_prompt"] is not None
            else "missing",
            covers_launch_to_prompt=1 if covers_launch_to_prompt else 0,
            samples_end_before_ptmx=1 if samples_end_before_ptmx else 0,
            samples_end_before_prompt=1 if samples_end_before_prompt else 0,
        )
    )
    emit_counter_summaries(counters)

    for name, start, end, missing_reason in windows:
        summary_row = summarize_window(name, start, end, samples, maps_by_tgid)
        print(
            row(
                phase="direct-launch-phase-window",
                action="kprofile-userpc",
                phase_window=name,
                start_uptime_ms=start,
                end_uptime_ms=end,
                duration_ms=end - start,
                samples=summary_row["samples"],
                sample_pct=pct(summary_row["samples"], len(samples)),
                first_seq=summary_row["first_seq"],
                last_seq=summary_row["last_seq"],
                unmapped_samples=summary_row["unmapped"],
                top_module=summary_row["top_module"],
                top_module_class=module_class(summary_row["top_module"]),
                top_samples=summary_row["top_samples"],
                covers_launch_to_prompt=1
                if times["bash_prompt"] is not None
                and start <= times["launch_start"]
                and end >= times["bash_prompt"]
                else 0,
                window_ends_before_ptmx=1 if times["ptmx"] is not None and end <= times["ptmx"] else 0,
                window_ends_before_prompt=1
                if times["bash_prompt"] is not None and end <= times["bash_prompt"]
                else 0,
                reason=summary_row["reason"] if windows else missing_reason,
            )
        )
        for rank, module in enumerate(summary_row["ranked"][:8], start=1):
            print(
                row(
                    phase="direct-launch-phase-module",
                    action="kprofile-userpc",
                    phase_window=name,
                    rank=rank,
                    tgid=module["tgid"],
                    pid=module["pid"],
                    comm=module["comm"],
                    module=module["module"],
                    module_class=module["module_class"],
                    samples=module["samples"],
                    sample_pct=pct(module["samples"], summary_row["samples"]),
                    first_seq=module["first_seq"],
                    last_seq=module["last_seq"],
                    min_rip=hex_value(module["min_rip"]),
                    max_rip=hex_value(module["max_rip"]),
                    min_file_offset=hex_value(module["min_file_offset"]),
                    max_file_offset=hex_value(module["max_file_offset"]),
                    map_count=module["map_count"],
                )
            )

    if args["symbolize"]:
        roots = dedupe_existing_dirs(args["symbol_roots"] + default_symbol_roots(app_path))
        emit_symbolization(
            samples,
            maps_by_tgid,
            windows,
            roots,
            default_symbol_images(app_path),
            default_symbol_cache_dir(app_path),
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
