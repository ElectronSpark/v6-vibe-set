#!/usr/bin/env python3
"""Generate the Linux userland upstream de-patching inventory."""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PLAN = ROOT / "docs/linux-userland-upstream-depatch-plan.md"
DEFAULT_OUT = ROOT / "build-x86_64/userland-depatch-inventory.tsv"
DEFAULT_ALLOWLIST = ROOT / "build-x86_64/userland-depatch-allowlist.tsv"
DEFAULT_REVIEWED_ALLOWLIST = ROOT / "docs/linux-userland-depatch-allowlist.tsv"
DEFAULT_SOURCE_REFS = ROOT / "build-x86_64/userland-depatch-source-refs.tsv"
DEFAULT_REVIEWED_SOURCE_REFS = ROOT / "docs/linux-userland-upstream-refs.tsv"
DEFAULT_USER_PROGRAM_AUDIT = ROOT / "build-x86_64/userland-user-program-audit.tsv"
DEFAULT_REVIEWED_USER_PROGRAM_AUDIT = ROOT / "docs/linux-userland-user-program-audit.tsv"
DEFAULT_REVIEWED_PHASES_GLOB = "docs/linux-userland-depatch-phase*.tsv"

FIELDS = [
    "name",
    "path",
    "kind",
    "upstream_ref",
    "source_delta_count",
    "xv6_marker_count",
    "abi_adaptation_hits",
    "patch_files",
    "wrapper_files",
    "status",
    "next_owner",
]

ALLOWLIST_FIELDS = ["name", "path", "kind", "role", "allowed_scope"]
SOURCE_REF_FIELDS = [
    "name",
    "path",
    "kind",
    "upstream_ref",
    "original_upstream_url",
    "source_delta_count",
]
USER_PROGRAM_AUDIT_FIELDS = [
    "name",
    "path",
    "role",
    "source_delta_count",
    "xv6_marker_count",
    "abi_adaptation_hits",
    "status",
    "allowed_scope",
]
PHASE_FIELDS = [
    "item",
    "kind",
    "upstream_ref",
    "current_delta",
    "abi_adaptation",
    "target_owner",
    "status",
    "validation",
]

BUILD_WRAPPERS = {
    "cmake",
    "glib-host",
    "libpng-host",
    "netsurf-buildsystem",
    "wayland-host",
}

DATA_OR_HEADERS = {
    "hwdata",
    "khronos-headers",
    "linux-uapi-headers",
    "wayland-protocols",
    "xkeyboard-config",
}

LOCAL_SHIMS = {
    "libevdev",
    "libinput",
    "libseat",
    "libudev",
    "wayland",
    "xv6-gbm",
}

PRODUCTION_PROGRAMS = {
    "cat",
    "cp",
    "dd",
    "echo",
    "find",
    "free",
    "grep",
    "init",
    "kill",
    "ln",
    "losetup",
    "lsblk",
    "mkdir",
    "mknod",
    "mount",
    "mv",
    "ps",
    "reboot",
    "rm",
    "sh",
    "shutdown",
    "sleep",
    "sync",
    "top",
    "umount",
    "wc",
    "xargs",
}

FILESYSTEM_TOOLS = {
    "dumpchan",
    "dumpinode",
    "dumppcache",
    "dumprq",
    "mkfs_xv6fs",
}

ABI_PROBES = {
    "cloexectest",
    "clonetest",
    "cowtest",
    "d3d12probe",
    "devtest",
    "drmabitest",
    "drmiftest",
    "drmprimeprobe",
    "dxgprobe",
    "fbstat",
    "fdtabletest",
    "gpubuftest",
    "gpucorevalidate",
    "iovectest",
    "kqueuetest",
    "linuxsyscallabitest",
    "mmapbigfile",
    "mmaptest",
    "nouveauabitest",
    "pngtest",
    "syscalltest",
    "testsig",
    "timerdemo",
    "timerfdstress",
    "ttmtest",
    "vforktest",
    "virgltest",
    "webkitabitest",
    "webkitnettest",
}

STRESS_TESTS = {
    "bigfile",
    "blocksendwake",
    "crashtest",
    "dnsstress",
    "dontwaitsend",
    "forktest",
    "grind",
    "iobench",
    "preempttest",
    "stressfs",
    "tcpstress",
    "usertests",
    "zombie",
}

LOCAL_DIAGNOSTICS = {
    "dh",
    "gldemo",
    "keyinject",
    "kprofile",
    "mouseinject",
    "mousetest",
    "pingpong",
    "primes",
    "randtest",
    "symlinktest",
    "waitgdb",
    "wallclock",
}

ORIGINAL_UPSTREAM_URLS = {
    "atk": "https://gitlab.gnome.org/Archive/atk.git",
    "cpython": "https://github.com/python/cpython.git",
    "fontconfig": "https://gitlab.freedesktop.org/fontconfig/fontconfig.git",
    "gdk-pixbuf": "https://gitlab.gnome.org/GNOME/gdk-pixbuf.git",
    "glib": "https://gitlab.gnome.org/GNOME/glib.git",
    "gtk3": "https://gitlab.gnome.org/GNOME/gtk.git",
    "libdrm": "https://gitlab.freedesktop.org/mesa/drm.git",
    "libffi": "https://github.com/libffi/libffi.git",
    "mesa": "https://gitlab.freedesktop.org/mesa/mesa.git",
    "netsurf": "git://git.netsurf-browser.org/netsurf.git",
    "openssl": "https://github.com/openssl/openssl.git",
    "pango": "https://gitlab.gnome.org/GNOME/pango.git",
    "peanut-gb": "https://github.com/deltabeard/Peanut-GB.git",
    "readline": "https://git.savannah.gnu.org/git/readline.git",
    "sqlite": "https://github.com/sqlite/sqlite.git",
    "vim": "https://github.com/vim/vim.git",
    "wayland-src": "https://gitlab.freedesktop.org/wayland/wayland.git",
    "weston": "https://gitlab.freedesktop.org/wayland/weston.git",
}

ABI_PATTERNS = {
    "xv6_preprocessor": re.compile(
        r"#\s*(?:if|ifdef|ifndef|elif)\b[^\n]*(?:__xv6__|__XV6__|\bxv6\b)",
        re.IGNORECASE,
    ),
    "raw_syscall": re.compile(r"\bsyscall\s*\("),
    "linux_syscall_number": re.compile(r"\b(?:__NR_|SYS_)[A-Za-z0-9_]+"),
    "errno_rewrite": re.compile(r"\b(?:errno\s*=\s*E[A-Z0-9_]+|return\s+-E[A-Z0-9_]+)"),
    "linux_struct_surface": re.compile(
        r"(?:\bstruct\s+(?:statx?|dirent64?|termios|pollfd|iovec|msghdr|cmsghdr|"
        r"sockaddr|input_event|drm_[A-Za-z0-9_]+)\s*\{|"
        r"\btypedef\s+struct\s+(?:statx?|dirent64?|termios|pollfd|iovec|msghdr|"
        r"cmsghdr|sockaddr|input_event|drm_[A-Za-z0-9_]+)\b)"
    ),
    "procfs_fallback": re.compile(r"(?:\"/proc|'/proc|/proc/|\bprocfs\b)", re.IGNORECASE),
    "feature_disable": re.compile(
        r"(?:xv6|__xv6__|__XV6__).{0,80}\b(?:disable|without|unsupported|stub|fallback)\b|"
        r"\b(?:disable|without|unsupported|stub|fallback)\b.{0,80}(?:xv6|__xv6__|__XV6__)|"
        r"-D[A-Za-z0-9_]*(?:XV6|xv6)[A-Za-z0-9_]*(?:DISABLE|WITHOUT|NO_)[A-Za-z0-9_]*=",
        re.IGNORECASE,
    ),
    "gui_event_ipc_surface": re.compile(
        r"\b(?:DRM_IOCTL|drmMode|Wayland|X11|Xwayland|wl_display|epoll|poll|"
        r"futex|SCM_RIGHTS)\b"
    ),
}

MARKER_RE = re.compile(r"(?:__xv6__|__XV6__|\bxv6\b)", re.IGNORECASE)
CHECKLIST_RE = re.compile(r"^- \[ \] `([^`]+)`\s*$")
INVENTORY_SECTIONS = {
    "Local User Libraries",
    "Local User Programs",
    "Ports, Libraries, Programs, Data, And Headers",
}
TEXT_SUFFIXES = {
    "",
    ".S",
    ".c",
    ".cc",
    ".cfg",
    ".cmake",
    ".conf",
    ".cpp",
    ".css",
    ".h",
    ".hpp",
    ".in",
    ".ini",
    ".json",
    ".m4",
    ".make",
    ".md",
    ".meson",
    ".pc",
    ".pl",
    ".py",
    ".sh",
    ".txt",
    ".xml",
    ".yaml",
    ".yml",
}


@dataclass(frozen=True)
class Item:
    name: str
    section: str
    path: Path
    kind: str
    role: str


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def run_git(path: Path, *args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(path), *args],
            cwd=ROOT,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except subprocess.CalledProcessError:
        return ""


def is_git_worktree(path: Path) -> bool:
    return run_git(path, "rev-parse", "--is-inside-work-tree") == "true"


def git_toplevel(path: Path) -> Path | None:
    top = run_git(path, "rev-parse", "--show-toplevel")
    if not top:
        return None
    return Path(top).resolve()


def is_git_root(path: Path) -> bool:
    top = git_toplevel(path)
    return top == path.resolve() if top else False


def parse_checklist(plan: Path) -> list[tuple[str, str]]:
    if not plan.exists():
        raise SystemExit(f"missing plan file: {rel(plan)}")
    section = ""
    rows: list[tuple[str, str]] = []
    for line in plan.read_text(encoding="utf-8").splitlines():
        if line.startswith("### "):
            section = line[4:].strip()
            continue
        match = CHECKLIST_RE.match(line)
        if match and section in INVENTORY_SECTIONS:
            rows.append((section, match.group(1)))
    if not rows:
        raise SystemExit(f"no checklist entries found in {rel(plan)}")
    return rows


def program_role(name: str) -> str:
    if name in PRODUCTION_PROGRAMS:
        return "production-command"
    if name in FILESYSTEM_TOOLS:
        return "xv6-filesystem-tool"
    if name in ABI_PROBES:
        return "abi-probe"
    if name in STRESS_TESTS:
        return "stress-test"
    if name in LOCAL_DIAGNOSTICS:
        return "local-diagnostic"
    return "local-program-review"


def classify(section: str, name: str) -> Item:
    if section == "Local User Libraries":
        kind = "local-shim"
        role = "xv6-user-library"
        path = ROOT / name
    elif section == "Local User Programs":
        kind = "local-program"
        role = program_role(name)
        path = ROOT / "user/programs" / name
    elif section == "Ports, Libraries, Programs, Data, And Headers":
        if name in BUILD_WRAPPERS:
            kind = "build-wrapper"
            role = "build-wrapper"
        elif name in DATA_OR_HEADERS:
            kind = "data-or-headers"
            role = "data-or-headers"
        elif name in LOCAL_SHIMS:
            kind = "local-shim"
            role = "local-compat-shim"
        else:
            kind = "imported-source"
            role = "imported-source"
        path = ROOT / "ports" / name
    else:
        raise SystemExit(f"checklist entry {name!r} appears in unknown section {section!r}")
    return Item(name=name, section=section, path=path, kind=kind, role=role)


def actual_inventory_names() -> set[str]:
    names = {"user/lib", "user/lib/x86_64"}
    programs_dir = ROOT / "user/programs"
    ports_dir = ROOT / "ports"
    names.update(
        path.name
        for path in programs_dir.iterdir()
        if path.is_dir() and not path.name.startswith(".")
    )
    names.update(
        path.name
        for path in ports_dir.iterdir()
        if path.is_dir() and not path.name.startswith(".")
    )
    return names


def plan_names(items: list[Item]) -> set[str]:
    return {item.name for item in items}


def source_paths(item: Item) -> list[Path]:
    if item.name in {"user/lib", "user/lib/x86_64"}:
        return [item.path]
    if item.kind == "local-program":
        return [item.path]
    if not item.path.exists():
        return []

    candidates: list[Path] = []
    direct_src = item.path / "src"
    if direct_src.exists():
        candidates.append(direct_src)
    for child in sorted(item.path.iterdir()):
        if child.name.startswith(".") or child.name == "patches":
            continue
        if child.is_dir() and is_git_root(child):
            candidates.append(child)
    if item.kind in {"build-wrapper", "data-or-headers"} and not candidates:
        return []
    if item.kind == "local-shim" and not candidates and item.path.exists():
        candidates.append(item.path)
    return sorted(set(candidates))


def git_ref(path: Path) -> str:
    if not is_git_root(path):
        return ""
    head = run_git(path, "rev-parse", "--short=12", "HEAD")
    branch = run_git(path, "rev-parse", "--abbrev-ref", "HEAD")
    desc = run_git(path, "describe", "--tags", "--always", "--dirty")
    parts = [part for part in (desc, head, branch if branch != "HEAD" else "") if part]
    return " ".join(parts)


def embedded_source_ref(item: Item, source: Path) -> str:
    if item.name == "zlib":
        zlib_h = source / "zlib.h"
        if zlib_h.exists():
            match = re.search(
                r'#define\s+ZLIB_VERSION\s+"([^"]+)"',
                zlib_h.read_text(encoding="utf-8", errors="ignore"),
            )
            if match:
                return f"zlib-{match.group(1)}"
    if item.name == "hwdata":
        pc_in = source / "hwdata.pc.in"
        if pc_in.exists():
            match = re.search(
                r"^Version:\s*(\S+)",
                pc_in.read_text(encoding="utf-8", errors="ignore"),
                re.M,
            )
            if match:
                return f"local-minimal-hwdata-{match.group(1)}"
    return ""


def source_ref(item: Item, source: Path) -> str:
    ref = git_ref(source)
    if ref:
        return ref
    ref = embedded_source_ref(item, source)
    if ref:
        return ref
    if item.kind in {"local-shim", "build-wrapper"}:
        return "local"
    return "unknown"


def original_upstream_url(item: Item, source: Path) -> str:
    if item.kind in {"local-program", "local-shim"}:
        return "local"
    if item.name in ORIGINAL_UPSTREAM_URLS:
        return ORIGINAL_UPSTREAM_URLS[item.name]
    if item.name == "zlib":
        return "https://github.com/madler/zlib.git"
    if item.name == "hwdata":
        return "local-minimal-hwdata"
    if is_git_worktree(source):
        url = run_git(source, "config", "--get", "remote.origin.url")
        if url:
            return url
    return "unknown"


def upstream_ref(item: Item, sources: list[Path]) -> str:
    if item.kind in {"local-program", "local-shim", "build-wrapper"}:
        return "local"
    refs = []
    for src in sources:
        ref = source_ref(item, src)
        if ref:
            refs.append(f"{rel(src)}@{ref}")
    if refs:
        return ";".join(refs)
    if item.kind in {"local-program", "local-shim", "build-wrapper"}:
        return "local"
    return "unknown"


def source_delta_count(sources: list[Path]) -> int:
    total = 0
    for src in sources:
        top = git_toplevel(src)
        if not top:
            continue
        if top == src.resolve():
            status = run_git(src, "status", "--porcelain")
        else:
            try:
                pathspec = src.resolve().relative_to(top).as_posix()
            except ValueError:
                continue
            status = run_git(top, "status", "--porcelain", "--", pathspec)
        if status:
            total += len(status.splitlines())
    return total


def patch_files(item: Item) -> list[Path]:
    if not item.path.exists():
        return []
    found: set[Path] = set()
    patches_dir = item.path / "patches"
    if patches_dir.exists():
        found.update(path for path in patches_dir.rglob("*") if path.is_file())
    found.update(path for path in item.path.rglob("*.patch") if path.is_file())
    return sorted(found)


def wrapper_files(item: Item, sources: list[Path], patches: list[Path]) -> list[Path]:
    if not item.path.exists() or item.kind == "local-program":
        return []
    source_roots = {src.resolve() for src in sources}
    patch_set = {path.resolve() for path in patches}
    wrappers: list[Path] = []
    for path in item.path.rglob("*"):
        if not path.is_file():
            continue
        resolved = path.resolve()
        if resolved in patch_set:
            continue
        if any(resolved == root or root in resolved.parents for root in source_roots):
            continue
        if ".git" in path.parts:
            continue
        wrappers.append(path)
    return sorted(wrappers)


def should_scan_file(path: Path) -> bool:
    if ".git" in path.parts:
        return False
    if path.suffix not in TEXT_SUFFIXES:
        return False
    try:
        chunk = path.read_bytes()[:4096]
    except OSError:
        return False
    return b"\0" not in chunk


def list_scan_files(root: Path) -> list[Path]:
    if root.is_file():
        return [root]
    if not root.exists():
        return []
    if is_git_worktree(root):
        output = run_git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard")
        if output:
            return [root / name for name in output.split("\0") if name]
    return [path for path in root.rglob("*") if path.is_file()]


def rg_pattern(pattern: re.Pattern[str]) -> str:
    if pattern.flags & re.IGNORECASE:
        return "(?i)" + pattern.pattern
    return pattern.pattern


def rg_count(pattern: str, paths: list[Path]) -> int | None:
    if shutil.which("rg") is None:
        return None
    valid = [str(path) for path in paths if path.exists()]
    if not valid:
        return 0
    command = [
        "rg",
        "--count-matches",
        "--no-heading",
        "--color",
        "never",
        "-I",
        "-S",
        "-e",
        pattern,
        "--",
        *valid,
    ]
    proc = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if proc.returncode not in {0, 1}:
        return None
    total = 0
    for line in proc.stdout.splitlines():
        try:
            total += int(line.rsplit(":", 1)[-1])
        except ValueError:
            continue
    return total


def scan_text(paths: list[Path]) -> tuple[int, Counter[str]]:
    marker_total = rg_count(rg_pattern(MARKER_RE), paths)
    hit_totals: Counter[str] = Counter()
    rg_complete = marker_total is not None
    for name, pattern in ABI_PATTERNS.items():
        count = rg_count(rg_pattern(pattern), paths)
        if count is None:
            rg_complete = False
            break
        if count:
            hit_totals[name] = count
    if rg_complete:
        return int(marker_total or 0), hit_totals

    marker_count = 0
    hits: Counter[str] = Counter()
    seen: set[Path] = set()
    for root in paths:
        for path in list_scan_files(root):
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            if not should_scan_file(path):
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            marker_count += len(MARKER_RE.findall(text))
            for name, pattern in ABI_PATTERNS.items():
                count = len(pattern.findall(text))
                if count:
                    hits[name] += count
    return marker_count, hits


def format_list(paths: list[Path], limit: int = 40) -> str:
    values = [rel(path) for path in paths]
    if len(values) > limit:
        shown = values[:limit]
        shown.append(f"...(+{len(values) - limit})")
        values = shown
    return ",".join(values)


def format_hits(hits: Counter[str]) -> str:
    if not hits:
        return "0"
    return ",".join(f"{name}={hits[name]}" for name in sorted(hits))


def infer_status(item: Item, deltas: int, patches: list[Path], hits: Counter[str]) -> str:
    if not item.path.exists():
        return "found"
    if item.kind == "local-shim":
        return "local-shim"
    if item.kind == "build-wrapper":
        return "assigned-wrapper"
    if item.kind == "local-program":
        return item.role
    if deltas or patches or hits:
        return "found"
    return "marker-only"


def infer_owner(item: Item, hits: Counter[str], patches: list[Path]) -> str:
    if item.kind == "local-program":
        return "local-program-audit"
    if item.kind == "local-shim":
        return "local-shim"
    if item.kind == "build-wrapper":
        return "build-wrapper"
    if item.kind == "data-or-headers":
        return "sysroot-data-headers"
    if {"raw_syscall", "linux_syscall_number", "procfs_fallback", "gui_event_ipc_surface"} & set(hits):
        return "kernel-abi"
    if {"errno_rewrite", "feature_disable"} & set(hits):
        return "libc-sysroot-or-kernel"
    if patches:
        return "build-wrapper"
    return "upstream-baseline-audit"


def build_rows(items: list[Item]) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    rows: list[dict[str, str]] = []
    allowlist: list[dict[str, str]] = []
    for item in items:
        sources = source_paths(item)
        patches = patch_files(item)
        wrappers = wrapper_files(item, sources, patches)
        if item.kind in {"imported-source", "data-or-headers"}:
            scan_roots = [*sources, *patches]
        elif item.kind == "build-wrapper":
            scan_roots = [*wrappers, *patches]
        else:
            scan_roots = [*sources, *patches, *wrappers]
        marker_count, hits = scan_text(scan_roots)
        deltas = source_delta_count(sources)
        rows.append(
            {
                "name": item.name,
                "path": rel(item.path),
                "kind": item.kind,
                "upstream_ref": upstream_ref(item, sources),
                "source_delta_count": str(deltas),
                "xv6_marker_count": str(marker_count),
                "abi_adaptation_hits": format_hits(hits),
                "patch_files": format_list(patches),
                "wrapper_files": format_list(wrappers),
                "status": infer_status(item, deltas, patches, hits),
                "next_owner": infer_owner(item, hits, patches),
            }
        )
        if item.kind == "local-shim":
            allowlist.append(
                {
                    "name": item.name,
                    "path": rel(item.path),
                    "kind": item.kind,
                    "role": item.role,
                    "allowed_scope": "xv6-owned compatibility surface; keep upstream-compatible ABI, not app-specific shortcuts",
                }
            )
        elif item.kind == "local-program" and item.role != "production-command":
            allowlist.append(
                {
                    "name": item.name,
                    "path": rel(item.path),
                    "kind": item.kind,
                    "role": item.role,
                    "allowed_scope": "xv6-authored diagnostic/test/probe code; raw ABI checks must remain test-only",
                }
            )
    return rows, allowlist


def build_source_ref_rows(items: list[Item]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for item in items:
        if item.section != "Ports, Libraries, Programs, Data, And Headers":
            continue
        for source in source_paths(item):
            if source.name != "src" and not is_git_worktree(source) and item.kind != "local-shim":
                continue
            rows.append(
                {
                    "name": item.name,
                    "path": rel(source),
                    "kind": item.kind,
                    "upstream_ref": source_ref(item, source),
                    "original_upstream_url": original_upstream_url(item, source),
                    "source_delta_count": str(source_delta_count([source])),
                }
            )
    return rows


def build_user_program_audit_rows(items: list[Item]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for item in items:
        if item.kind != "local-program":
            continue
        sources = source_paths(item)
        wrappers = wrapper_files(item, sources, [])
        marker_count, hits = scan_text([*sources, *wrappers])
        deltas = source_delta_count(sources)
        allowed_scope = (
            "ordinary Linux/libc-visible behavior; no private ABI adaptation"
            if item.role == "production-command"
            else "xv6-authored diagnostic/test/probe code; raw ABI checks must remain test-only"
        )
        status = (
            "needs-review"
            if item.role == "production-command" and hits
            else item.role
        )
        rows.append(
            {
                "name": item.name,
                "path": rel(item.path),
                "role": item.role,
                "source_delta_count": str(deltas),
                "xv6_marker_count": str(marker_count),
                "abi_adaptation_hits": format_hits(hits),
                "status": status,
                "allowed_scope": allowed_scope,
            }
        )
    return rows


def write_tsv(path: Path, fields: list[str], rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, dialect="excel-tab", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def read_tsv(path: Path, fields: list[str]) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != fields:
            raise SystemExit(
                f"{rel(path)} header mismatch: expected {fields}, got {reader.fieldnames}"
            )
        return [{field: row.get(field, "") for field in fields} for row in reader]


def row_key(row: dict[str, str]) -> tuple[str, str, str]:
    return (row["name"], row["path"], row.get("kind", row.get("role", "")))


def verify_reviewed_rows(
    generated: list[dict[str, str]],
    reviewed_path: Path,
    fields: list[str],
    label: str,
) -> None:
    if not reviewed_path.exists():
        raise SystemExit(f"reviewed {label} missing: {rel(reviewed_path)}")

    reviewed = read_tsv(reviewed_path, fields)
    generated_by_key = {row_key(row): row for row in generated}
    reviewed_by_key = {row_key(row): row for row in reviewed}

    missing = sorted(set(generated_by_key) - set(reviewed_by_key))
    stale = sorted(set(reviewed_by_key) - set(generated_by_key))
    changed = sorted(
        key
        for key in set(generated_by_key) & set(reviewed_by_key)
        if generated_by_key[key] != reviewed_by_key[key]
    )

    errors = []
    if missing:
        errors.append(
            f"{label} entries need review: "
            + ", ".join(f"{name} ({path})" for name, path, _kind in missing)
        )
    if stale:
        errors.append(
            f"reviewed {label} has stale entries: "
            + ", ".join(f"{name} ({path})" for name, path, _kind in stale)
        )
    if changed:
        errors.append(
            f"reviewed {label} entries changed: "
            + ", ".join(f"{name} ({path})" for name, path, _kind in changed)
        )
    if errors:
        raise SystemExit("\n".join(errors))


def verify_reviewed_allowlist(generated: list[dict[str, str]], reviewed_path: Path) -> None:
    verify_reviewed_rows(generated, reviewed_path, ALLOWLIST_FIELDS, "allowlist")


def verify_reviewed_source_refs(generated: list[dict[str, str]], reviewed_path: Path) -> None:
    verify_reviewed_rows(generated, reviewed_path, SOURCE_REF_FIELDS, "source refs")


def verify_reviewed_user_program_audit(
    generated: list[dict[str, str]],
    reviewed_path: Path,
) -> None:
    verify_reviewed_rows(
        generated,
        reviewed_path,
        USER_PROGRAM_AUDIT_FIELDS,
        "user program audit",
    )


def read_phase_rows(pattern: str) -> list[dict[str, str]]:
    paths = sorted(ROOT.glob(pattern))
    if not paths:
        raise SystemExit(f"reviewed phase files missing: {pattern}")

    rows: list[dict[str, str]] = []
    for path in paths:
        with path.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            if reader.fieldnames != PHASE_FIELDS:
                raise SystemExit(
                    f"{rel(path)} header mismatch: expected {PHASE_FIELDS}, got {reader.fieldnames}"
                )
            rows.extend(
                {field: row.get(field, "") for field in PHASE_FIELDS}
                for row in reader
            )
    return rows


def verify_reviewed_phase_coverage(
    inventory_rows: list[dict[str, str]],
    reviewed_phases_glob: str,
) -> None:
    reviewed_rows = read_phase_rows(reviewed_phases_glob)
    reviewed_by_item: dict[str, list[dict[str, str]]] = {}
    for row in reviewed_rows:
        reviewed_by_item.setdefault(row["item"], []).append(row)

    errors: list[str] = []
    checked = 0
    for row in inventory_rows:
        name = row["name"]
        kind = row["kind"]
        if kind == "local-program":
            continue
        checked += 1
        matching = reviewed_by_item.get(name, [])
        accepted = [
            phase
            for phase in matching
            if phase["status"] == "done" and phase["abi_adaptation"] == "no"
        ]
        if not accepted:
            errors.append(f"{name} ({kind}) lacks done/no reviewed phase coverage")
        if kind in {"imported-source", "data-or-headers"} and row["source_delta_count"] != "0":
            errors.append(
                f"{name} ({kind}) has source_delta_count={row['source_delta_count']}"
            )

    if errors:
        raise SystemExit("\n".join(errors))

    print(f"reviewed phase coverage accepts {checked} non-local-program inventory rows")


def verify_coverage(items: list[Item]) -> None:
    actual = actual_inventory_names()
    expected = plan_names(items)
    missing = sorted(actual - expected)
    stale = sorted(expected - actual)
    errors = []
    if missing:
        errors.append("live tree entries missing from plan: " + ", ".join(missing))
    if stale:
        errors.append("plan entries missing from live tree: " + ", ".join(stale))
    if errors:
        raise SystemExit("\n".join(errors))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--allowlist-output", type=Path, default=DEFAULT_ALLOWLIST)
    parser.add_argument("--reviewed-allowlist", type=Path, default=DEFAULT_REVIEWED_ALLOWLIST)
    parser.add_argument("--check-reviewed-allowlist", action="store_true")
    parser.add_argument("--source-refs-output", type=Path, default=DEFAULT_SOURCE_REFS)
    parser.add_argument("--reviewed-source-refs", type=Path, default=DEFAULT_REVIEWED_SOURCE_REFS)
    parser.add_argument("--check-reviewed-source-refs", action="store_true")
    parser.add_argument("--user-programs-output", type=Path, default=DEFAULT_USER_PROGRAM_AUDIT)
    parser.add_argument(
        "--reviewed-user-programs",
        type=Path,
        default=DEFAULT_REVIEWED_USER_PROGRAM_AUDIT,
    )
    parser.add_argument("--check-reviewed-user-programs", action="store_true")
    parser.add_argument("--reviewed-phases-glob", default=DEFAULT_REVIEWED_PHASES_GLOB)
    parser.add_argument("--check-reviewed-phases", action="store_true")
    parser.add_argument("--no-allowlist", action="store_true")
    parser.add_argument("--no-source-refs", action="store_true")
    parser.add_argument("--no-user-programs", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows = parse_checklist(args.plan)
    items = [classify(section, name) for section, name in rows]
    verify_coverage(items)
    inventory_rows, allowlist_rows = build_rows(items)
    source_ref_rows = build_source_ref_rows(items)
    user_program_rows = build_user_program_audit_rows(items)
    write_tsv(args.output, FIELDS, inventory_rows)
    if not args.no_allowlist:
        write_tsv(args.allowlist_output, ALLOWLIST_FIELDS, allowlist_rows)
    if not args.no_source_refs:
        write_tsv(args.source_refs_output, SOURCE_REF_FIELDS, source_ref_rows)
    if not args.no_user_programs:
        write_tsv(args.user_programs_output, USER_PROGRAM_AUDIT_FIELDS, user_program_rows)
    if args.check_reviewed_allowlist:
        verify_reviewed_allowlist(allowlist_rows, args.reviewed_allowlist)
    if args.check_reviewed_source_refs:
        verify_reviewed_source_refs(source_ref_rows, args.reviewed_source_refs)
    if args.check_reviewed_user_programs:
        verify_reviewed_user_program_audit(user_program_rows, args.reviewed_user_programs)
    if args.check_reviewed_phases:
        verify_reviewed_phase_coverage(inventory_rows, args.reviewed_phases_glob)

    print(f"wrote {len(inventory_rows)} inventory rows to {rel(args.output)}")
    if not args.no_allowlist:
        print(f"wrote {len(allowlist_rows)} allowlist rows to {rel(args.allowlist_output)}")
    if not args.no_source_refs:
        print(f"wrote {len(source_ref_rows)} source ref rows to {rel(args.source_refs_output)}")
    if not args.no_user_programs:
        print(f"wrote {len(user_program_rows)} user program audit rows to {rel(args.user_programs_output)}")
    if args.check_reviewed_allowlist:
        print(f"reviewed allowlist matches {rel(args.reviewed_allowlist)}")
    if args.check_reviewed_source_refs:
        print(f"reviewed source refs match {rel(args.reviewed_source_refs)}")
    if args.check_reviewed_user_programs:
        print(f"reviewed user program audit matches {rel(args.reviewed_user_programs)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BrokenPipeError:
        os._exit(1)
