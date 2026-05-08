#!/usr/bin/env python3
"""Generate the Linux x86_64 syscall ABI audit artifacts."""

from __future__ import annotations

import csv
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LINUX_UNISTD = Path("/usr/include/x86_64-linux-gnu/asm/unistd_64.h")
KERNEL_SYSCALL_H = ROOT / "kernel/kernel/inc/syscall.h"
X86_DISPATCH = ROOT / "kernel/arch/x86_64/irq/syscall.c"
MUSL_SYSCALL_H = ROOT / "toolchain/musl-xv6/arch/x86_64/bits/syscall.h.in"
ABI_TEST = ROOT / "user/programs/linuxsyscallabitest/linuxsyscallabitest.c"
OUT_CSV = ROOT / "docs/linux-abi-audit.csv"
OUT_MD = ROOT / "docs/linux-abi-audit.md"


VFS_NAMES = {
    "open", "close", "read", "write", "fstat", "link", "unlink",
    "symlink", "mkdir", "mknod", "lseek", "dup", "dup2", "dup3",
    "fcntl", "ioctl", "access", "rename", "readlink", "stat", "lstat",
    "ftruncate", "poll", "ppoll", "pread64", "pwrite64", "preadv",
    "pwritev", "preadv2", "pwritev2", "readv", "writev", "chdir",
    "openat", "mkdirat", "mknodat",
    "unlinkat", "linkat", "symlinkat", "readlinkat", "renameat",
    "faccessat", "fchmod", "fchmodat", "fchown", "fchownat", "fsync",
    "fdatasync", "pipe", "pipe2",
}

HANDLER_ALIASES = {
    "accept": {"sys_accept"},
    "accept4": {"sys_accept4"},
    "bind": {"sys_bind"},
    "connect": {"sys_sconnect"},
    "execve": {"sys_exec"},
    "exit": {"sys_exit"},
    "exit_group": {"sys_exit_group"},
    "fstatfs": {"sys_fstatfs"},
    "getdents": {"sys_getdents", "sys_getdents_compat"},
    "getdents64": {"sys_getdents"},
    "newfstatat": {"sys_vfs_fstatat"},
    "recvmsg": {"sys_recvmsg"},
    "recvfrom": {"sys_recvfrom"},
    "sendmsg": {"sys_sendmsg"},
    "sendto": {"sys_sendto"},
    "setsockopt": {"sys_setsockopt"},
    "getsockopt": {"sys_getsockopt"},
    "shutdown": {"sys_shutdown"},
    "listen": {"sys_listen"},
    "socket": {"sys_socket"},
    "socketpair": {"sys_socketpair"},
    "statfs": {"sys_statfs"},
    "rt_sigaction": {"sys_sigaction", "sys_rt_sigaction"},
    "rt_sigpending": {"sys_sigpending", "sys_rt_sigpending"},
    "rt_sigprocmask": {"sys_sigprocmask", "sys_rt_sigprocmask"},
    "rt_sigqueueinfo": {"sys_rt_sigqueueinfo"},
    "rt_sigreturn": {"sys_sigreturn"},
    "rt_sigsuspend": {"sys_sigsuspend", "sys_rt_sigsuspend"},
    "rt_sigtimedwait": {"sys_sigwait", "sys_rt_sigtimedwait"},
    "umount2": {"sys_umount"},
    "wait4": {"sys_waitpid", "sys_wait"},
}

STRUCT_RISK = {
    "accept", "accept4", "arch_prctl", "clone", "clone3", "connect",
    "epoll_ctl", "epoll_pwait", "epoll_pwait2", "epoll_wait", "fcntl",
    "fstat", "fstatfs", "getdents", "getdents64", "getgroups",
    "getrlimit", "getresgid", "getresuid", "getrusage", "getsockname",
    "getpeername", "getsockopt", "ioctl", "lstat", "madvise", "mincore",
    "mmap", "mprotect", "mremap", "msync", "munmap", "newfstatat",
    "open", "openat", "pipe", "pipe2", "poll", "ppoll", "preadv",
    "preadv2", "pselect6", "pwritev", "pwritev2", "readv", "recvfrom",
    "recvmsg", "rt_sigaction", "rt_sigpending", "rt_sigqueueinfo",
    "rt_sigsuspend", "rt_sigtimedwait", "sendmsg", "sendmmsg", "sendto",
    "setgroups", "setrlimit", "setsockopt", "sigaltstack", "stat",
    "statfs", "statx", "sysinfo", "timerfd_gettime", "timerfd_settime",
    "uname", "writev",
}


def strip_inline_comment(line: str) -> str:
    return line.split("//", 1)[0]


def parse_linux_unistd(path: Path) -> list[tuple[int, str]]:
    rows: list[tuple[int, str]] = []
    for line in path.read_text().splitlines():
        m = re.match(r"#define\s+__NR_([A-Za-z0-9_]+)\s+([0-9]+)\b", line)
        if m:
            rows.append((int(m.group(2)), m.group(1)))
    return sorted(rows)


def active_defines(path: Path) -> dict[str, int]:
    defines: dict[str, int] = {}
    active = [True]
    condition_stack: list[tuple[bool, bool]] = []
    define_re = re.compile(r"#define\s+(SYS_[A-Za-z0-9_]+|__NR_[A-Za-z0-9_]+)\s+(-?[0-9]+)\b")

    for raw in path.read_text().splitlines():
        line = strip_inline_comment(raw).strip()
        if line.startswith("#if") and "__x86_64__" in line:
            cond = not re.search(r"!\s*defined\s*\(\s*__x86_64__\s*\)", line)
            condition_stack.append((active[-1], cond))
            active.append(active[-1] and cond)
            continue
        if line.startswith("#else") and condition_stack:
            parent, cond = condition_stack[-1]
            active[-1] = parent and not cond
            continue
        if line.startswith("#endif") and condition_stack:
            condition_stack.pop()
            active.pop()
            continue
        if not active[-1]:
            continue
        m = define_re.match(line)
        if m:
            defines[m.group(1)] = int(m.group(2))
    return defines


def parse_dispatch(path: Path, defines: dict[str, int]) -> dict[int, list[tuple[str, str]]]:
    dispatch: dict[int, list[tuple[str, str]]] = defaultdict(list)
    entry_re = re.compile(r"\[(SYS_[A-Za-z0-9_]+)\]\s*=?\s*([A-Za-z0-9_]+)")
    for line in path.read_text().splitlines():
        m = entry_re.search(line)
        if not m:
            continue
        macro, handler = m.groups()
        if macro in defines:
            dispatch[defines[macro]].append((macro, handler))
    return dispatch


def parse_legacy_aliases(path: Path, defines: dict[str, int]) -> dict[int, str]:
    aliases: dict[int, str] = {}
    alias_re = re.compile(r"case\s+([0-9]+):\s*return\s+(SYS_[A-Za-z0-9_]+)")
    for line in path.read_text().splitlines():
        m = alias_re.search(line)
        if m:
            aliases[int(m.group(1))] = m.group(2)
    return aliases


def parse_sys_handlers() -> set[str]:
    handlers: set[str] = set()
    definition_re = re.compile(r"^\s*uint64\s+(sys_[A-Za-z0-9_]+)\s*\([^;]*\)\s*\{", re.M)
    for path in (ROOT / "kernel").rglob("*.c"):
        text = path.read_text(errors="ignore")
        handlers.update(definition_re.findall(text))
    return handlers


def expected_handlers(name: str) -> set[str]:
    handlers = set(HANDLER_ALIASES.get(name, set()))
    handlers.add(f"sys_{name}")
    if name in VFS_NAMES:
        handlers.add(f"sys_vfs_{name}")
    if name == "readlinkat":
        handlers.add("sys_vfs_readlinkat")
    if name == "faccessat2":
        handlers.add("sys_vfs_faccessat")
    if name == "renameat2":
        handlers.add("sys_vfs_renameat")
    return handlers


def handler_matches(name: str, handlers: list[str]) -> bool:
    expected = expected_handlers(name)
    return any(handler in expected for handler in handlers)


def macro_names_for_number(defines: dict[str, int], nr: int) -> list[str]:
    return sorted(name for name, value in defines.items() if value == nr)


def musl_mapping_for(name: str, musl_defines: dict[str, int]) -> str:
    keys = [f"SYS_{name}"]
    if name == "stat":
        keys.append("SYS_newfstatat")
    found = [f"{key}={musl_defines[key]}" for key in keys if key in musl_defines]
    return ";".join(found)


def test_covers(name: str, nr: int, test_text: str) -> str:
    macro = f"LINUX_NR_{name.upper()}"
    if macro in test_text or re.search(rf"\b{nr}\b", test_text):
        return "yes"
    return "no"


def classify(
    name: str,
    nr: int,
    dispatch: dict[int, list[tuple[str, str]]],
    handlers: set[str],
    legacy_aliases: dict[int, str],
) -> tuple[str, str]:
    entries = dispatch.get(nr, [])
    entry_handlers = [handler for _, handler in entries]
    expected = expected_handlers(name)
    implemented = bool(expected & handlers)

    if entries:
        if all(handler == "sys_ni_enosys" for handler in entry_handlers):
            return "unsupported-ok", "dispatches to explicit ENOSYS stub"
        if handler_matches(name, entry_handlers):
            if name in STRUCT_RISK:
                return "struct-risk", "native number is wired; audit structs/flags/semantics"
            return "native-ok", "native number is wired to matching handler"
        return "wrong-dispatch", "native Linux number reaches unrelated handler"

    if implemented:
        return "native-missing", "matching kernel handler exists but Linux number is unmapped"

    if nr in legacy_aliases:
        return "unsupported-ok", "legacy alias exists in disabled compatibility block; default dispatch is ENOSYS"

    return "unsupported-ok", "unmapped default dispatch returns ENOSYS"


def write_outputs() -> None:
    linux_rows = parse_linux_unistd(LINUX_UNISTD)
    kernel_defines = active_defines(KERNEL_SYSCALL_H)
    musl_defines = active_defines(MUSL_SYSCALL_H)
    dispatch = parse_dispatch(X86_DISPATCH, kernel_defines)
    legacy_aliases = parse_legacy_aliases(X86_DISPATCH, kernel_defines)
    handlers = parse_sys_handlers()
    test_text = ABI_TEST.read_text()

    rows: list[dict[str, str]] = []
    for nr, name in linux_rows:
        status, notes = classify(name, nr, dispatch, handlers, legacy_aliases)
        entries = dispatch.get(nr, [])
        legacy = legacy_aliases.get(nr, "")
        if legacy:
            legacy_num = kernel_defines.get(legacy)
            legacy = f"{legacy}({legacy_num})"

        rows.append({
            "number": str(nr),
            "linux_name": name,
            "status": status,
            "dispatch_macros": ";".join(macro for macro, _ in entries),
            "dispatch_handlers": ";".join(handler for _, handler in entries),
            "kernel_macros_at_number": ";".join(macro_names_for_number(kernel_defines, nr)),
            "musl_mapping": musl_mapping_for(name, musl_defines),
            "legacy_alias_if_enabled": legacy,
            "test_covered": test_covers(name, nr, test_text),
            "notes": notes,
        })

    with OUT_CSV.open("w", newline="") as f:
        fieldnames = list(rows[0].keys())
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    counts = Counter(row["status"] for row in rows)
    wrong = [row for row in rows if row["status"] == "wrong-dispatch"]
    native_missing = [row for row in rows if row["status"] == "native-missing"]
    unsupported_bad = [row for row in rows if row["status"] == "unsupported-bad"]

    lines = [
        "# Linux x86_64 Syscall ABI Audit",
        "",
        "> Generated by `scripts/linux_abi_audit.py`. Do not edit tables by hand.",
        "",
        "## Inputs",
        "",
        f"- Linux syscall table: `{LINUX_UNISTD}`",
        f"- Kernel syscall numbers: `{KERNEL_SYSCALL_H.relative_to(ROOT)}`",
        f"- x86 dispatcher: `{X86_DISPATCH.relative_to(ROOT)}`",
        f"- musl-xv6 syscall header: `{MUSL_SYSCALL_H.relative_to(ROOT)}`",
        f"- Runtime ABI test: `{ABI_TEST.relative_to(ROOT)}`",
        "",
        "## Summary",
        "",
    ]
    for status in ["native-ok", "native-missing", "wrong-dispatch",
                   "unsupported-ok", "unsupported-bad", "struct-risk"]:
        lines.append(f"- `{status}`: {counts.get(status, 0)}")
    lines.extend([
        "",
        "Default x86_64 dispatch no longer uses the legacy xv6 alias table. "
        "Entries listed under `legacy_alias_if_enabled` are reachable only if "
        "`ENABLE_LEGACY_XV6_SYSCALL_ALIAS` is compiled in.",
        "",
        "## Immediate Gaps",
        "",
    ])
    if wrong:
        for row in wrong:
            lines.append(
                f"- `__NR_{row['linux_name']}` ({row['number']}) dispatches to "
                f"`{row['dispatch_handlers']}` via `{row['dispatch_macros']}`."
            )
    else:
        lines.append("- No `wrong-dispatch` entries detected in the generated inventory.")
    if unsupported_bad:
        lines.append("- Unsupported-bad entries remain:")
        for row in unsupported_bad:
            lines.append(f"- `__NR_{row['linux_name']}` ({row['number']}): {row['notes']}")
    lines.extend([
        "",
        "Native-missing entries are syscalls with a matching kernel handler but no "
        "Linux x86_64 number entry yet.",
        "",
        "## Native-Missing Snapshot",
        "",
    ])
    if native_missing:
        for row in native_missing[:80]:
            lines.append(f"- `{row['number']}` `__NR_{row['linux_name']}`: {row['notes']}")
        if len(native_missing) > 80:
            lines.append(f"- ... {len(native_missing) - 80} more; see CSV for the full list.")
    else:
        lines.append("- None detected.")

    lines.extend([
        "",
        "## Full Table",
        "",
        "| Nr | Linux name | Status | Dispatch | Kernel macros | musl-xv6 mapping | Legacy alias | Test | Notes |",
        "|---:|---|---|---|---|---|---|---|---|",
    ])
    for row in rows:
        dispatch_cell = row["dispatch_handlers"]
        if row["dispatch_macros"]:
            dispatch_cell = f"{row['dispatch_macros']} -> {row['dispatch_handlers']}"
        lines.append(
            "| {number} | `__NR_{linux_name}` | `{status}` | {dispatch} | {kernel} | {musl} | {legacy} | {test} | {notes} |".format(
                number=row["number"],
                linux_name=row["linux_name"],
                status=row["status"],
                dispatch=dispatch_cell or "",
                kernel=row["kernel_macros_at_number"],
                musl=row["musl_mapping"],
                legacy=row["legacy_alias_if_enabled"],
                test=row["test_covered"],
                notes=row["notes"],
            )
        )

    OUT_MD.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    write_outputs()
