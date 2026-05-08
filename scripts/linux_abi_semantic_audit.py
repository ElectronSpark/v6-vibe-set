#!/usr/bin/env python3
"""Generate a semantic Linux x86_64 syscall ABI audit.

This consumes docs/linux-abi-audit.csv, which is the number/dispatch inventory,
and adds behavior-level status, evidence, and remaining semantic gaps.
"""

from __future__ import annotations

import csv
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INPUT_CSV = ROOT / "docs/linux-abi-audit.csv"
OUT_CSV = ROOT / "docs/linux-abi-semantic-audit.csv"
OUT_MD = ROOT / "docs/linux-abi-semantic-audit.md"


AREA_BY_NAME = {
    "read": "fd-vfs", "write": "fd-vfs", "open": "fd-vfs",
    "close": "fd-vfs", "stat": "fd-vfs", "fstat": "fd-vfs",
    "lstat": "fd-vfs", "poll": "fd-vfs", "lseek": "fd-vfs",
    "ioctl": "fd-vfs", "pread64": "fd-vfs", "pwrite64": "fd-vfs",
    "readv": "fd-vfs", "writev": "fd-vfs", "access": "fd-vfs",
    "pipe": "fd-vfs", "dup": "fd-vfs", "dup2": "fd-vfs",
    "fcntl": "fd-vfs", "flock": "fd-vfs", "fsync": "fd-vfs",
    "fdatasync": "fd-vfs", "ftruncate": "fd-vfs",
    "getdents": "fd-vfs", "getcwd": "fd-vfs", "chdir": "fd-vfs",
    "rename": "fd-vfs", "mkdir": "fd-vfs", "link": "fd-vfs",
    "unlink": "fd-vfs", "symlink": "fd-vfs", "readlink": "fd-vfs",
    "fchmod": "fd-vfs", "fchown": "fd-vfs", "statfs": "fd-vfs",
    "fstatfs": "fd-vfs", "chroot": "fd-vfs", "sync": "fd-vfs",
    "mount": "fd-vfs", "umount2": "fd-vfs", "openat": "fd-vfs",
    "mkdirat": "fd-vfs", "mknodat": "fd-vfs", "fchownat": "fd-vfs",
    "newfstatat": "fd-vfs", "unlinkat": "fd-vfs", "renameat": "fd-vfs",
    "linkat": "fd-vfs", "symlinkat": "fd-vfs",
    "readlinkat": "fd-vfs", "fchmodat": "fd-vfs",
    "faccessat": "fd-vfs", "ppoll": "fd-vfs", "dup3": "fd-vfs",
    "pipe2": "fd-vfs", "preadv": "fd-vfs", "pwritev": "fd-vfs",
    "preadv2": "fd-vfs", "pwritev2": "fd-vfs", "statx": "fd-vfs",
    "mmap": "memory", "mprotect": "memory", "munmap": "memory",
    "brk": "memory", "mremap": "memory", "msync": "memory",
    "mincore": "memory", "madvise": "memory", "mlock": "memory",
    "munlock": "memory", "mlockall": "memory", "munlockall": "memory",
    "mlock2": "memory",
    "rt_sigaction": "signal", "rt_sigprocmask": "signal",
    "rt_sigreturn": "signal", "kill": "signal",
    "rt_sigpending": "signal", "rt_sigtimedwait": "signal",
    "rt_sigqueueinfo": "signal", "rt_sigsuspend": "signal",
    "sigaltstack": "signal", "tkill": "signal", "tgkill": "signal",
    "pause": "signal",
    "clone": "process-thread", "fork": "process-thread",
    "vfork": "process-thread", "execve": "process-thread",
    "exit": "process-thread", "wait4": "process-thread",
    "getpid": "process-thread", "getppid": "process-thread",
    "getuid": "identity", "getgid": "identity", "setuid": "identity",
    "setgid": "identity", "geteuid": "identity", "getegid": "identity",
    "setpgid": "identity", "setsid": "identity", "getgroups": "identity",
    "setgroups": "identity", "setresuid": "identity",
    "getresuid": "identity", "setresgid": "identity",
    "getresgid": "identity", "getpgid": "identity", "getsid": "identity",
    "prctl": "process-thread", "arch_prctl": "process-thread",
    "set_tid_address": "process-thread", "gettid": "process-thread",
    "exit_group": "process-thread", "setrlimit": "process-thread",
    "getrlimit": "process-thread", "getrusage": "process-thread",
    "sysinfo": "process-thread", "prlimit64": "process-thread",
    "clone3": "process-thread",
    "socket": "socket-network", "connect": "socket-network",
    "accept": "socket-network", "sendto": "socket-network",
    "recvfrom": "socket-network", "sendmsg": "socket-network",
    "recvmsg": "socket-network", "shutdown": "socket-network",
    "bind": "socket-network", "listen": "socket-network",
    "getsockname": "socket-network", "getpeername": "socket-network",
    "socketpair": "socket-network", "setsockopt": "socket-network",
    "getsockopt": "socket-network", "accept4": "socket-network",
    "sendmmsg": "socket-network", "recvmmsg": "socket-network",
    "epoll_wait": "event", "epoll_ctl": "event",
    "eventfd2": "event", "epoll_create1": "event",
    "epoll_pwait": "event", "timerfd_create": "event",
    "timerfd_settime": "event", "timerfd_gettime": "event",
    "epoll_pwait2": "event",
}


OVERRIDES = {
    "open": ("semantic-partial", "raw open/openat and O_CLOEXEC paths are covered",
             "O_PATH, O_TMPFILE, O_DIRECTORY, and some creation/trailing-slash details remain partial"),
    "openat": ("semantic-partial", "raw openat, read/write, and fstatat follow-up are covered",
               "O_PATH, O_TMPFILE, O_DIRECTORY, and some creation/trailing-slash details remain partial"),
    "stat": ("probed-compatible-core", "raw stat('/') layout test passes", "broader inode-type/time edge cases remain"),
    "fstat": ("probed-compatible-core", "raw fstat(fd) layout test passes", "broader inode-type/time edge cases remain"),
    "lstat": ("probed-compatible-core", "raw lstat('/') layout test passes", "symlink edge coverage remains"),
    "newfstatat": ("probed-compatible-core", "raw newfstatat path and AT_EMPTY_PATH tests pass",
                   "AT_SYMLINK_NOFOLLOW symlink matrix still needs broader coverage"),
    "statfs": ("probed-compatible-core", "raw statfs('/') layout and dispatch test passes",
               "filesystem-specific fields are minimal"),
    "fstatfs": ("semantic-partial", "native fstatfs number reaches matching handler",
                "raw fstatfs layout regression still needed"),
    "getdents": ("probed-compatible-core", "raw getdents(78) old dirent layout test passes",
                 "large-directory and short-buffer behavior need broader coverage"),
    "getdents64": ("probed-compatible-core", "raw getdents64(217) layout test passes",
                   "large-directory and short-buffer behavior need broader coverage"),
    "mmap": ("probed-compatible-core", "raw anonymous mmap and fixed collision paths are exercised indirectly",
             "full MAP_* flag matrix remains partial"),
    "munmap": ("probed-compatible-core", "raw munmap and unmap-self sequence pass", "full partial-unmap matrix remains"),
    "mprotect": ("probed-compatible-core", "native number is wired and covered by memory ABI tests",
                 "full protection/fault matrix remains"),
    "mremap": ("probed-compatible-core", "raw mremap growth/move regression passes",
               "full MREMAP_FIXED overlap matrix remains"),
    "msync": ("probed-compatible-core", "raw msync basic call passes", "file-backed durability semantics remain partial"),
    "mincore": ("probed-compatible-core", "raw mincore basic call passes", "resident-bit precision remains partial"),
    "madvise": ("probed-compatible-core", "raw madvise basic call passes", "many MADV_* modes are no-op or partial"),
    "rt_sigaction": ("probed-compatible-core", "raw rt_sigaction layout and sigsetsize tests pass",
                     "Linux signal frame/ucontext compatibility remains partial"),
    "rt_sigprocmask": ("probed-compatible-core", "raw rt_sigprocmask sigsetsize and Linux SIG_SETMASK tests pass",
                       "threaded signal-mask edge cases remain"),
    "rt_sigpending": ("probed-compatible-core", "raw rt_sigpending sigsetsize tests pass",
                      "queued realtime signal detail is partial"),
    "rt_sigsuspend": ("probed-compatible-core", "raw rt_sigsuspend bad-size test passes",
                      "blocking/delivery matrix remains"),
    "rt_sigtimedwait": ("semantic-partial", "raw zero-timeout and sigsetsize tests pass",
                        "nonzero timeout currently falls through to sigwait-style blocking"),
    "rt_sigqueueinfo": ("semantic-partial", "native number reaches matching handler",
                        "siginfo payload fidelity and permission semantics are partial"),
    "sigaltstack": ("semantic-partial", "native number reaches matching handler",
                    "Linux altstack flag matrix and signal-frame use need more tests"),
    "clone": ("semantic-partial", "Linux clone(flags, stack, ptid, ctid, tls) path is wired",
              "flag validation, pidfd, namespace, and thread-group edge cases are partial"),
    "clone3": ("semantic-partial", "Linux clone_args layout is copied and mapped to thread_clone",
               "size validation, pidfd, set_tid array, cgroup, and namespace semantics are partial"),
    "vfork": ("probed-compatible-core", "VM boot vforktest passes exit, exec, and parent-blocking cases",
              "deep signal/thread interactions remain"),
    "wait4": ("semantic-partial", "waitpid-compatible handler is wired", "rusage and option matrix are partial"),
    "fcntl": ("semantic-partial", "F_GETFD/F_SETFD/F_DUPFD/F_SETFL and seals are implemented",
              "fcntl command surface is partial"),
    "poll": ("semantic-partial", "poll number and basic runtime tests pass", "full event mask semantics are partial"),
    "ppoll": ("semantic-partial", "ppoll handler validates timespec and mask size", "signal restore/race matrix remains"),
    "pselect6": ("semantic-partial", "native number is wired", "Linux pselect6 sigmask-argument struct matrix remains"),
    "ioctl": ("semantic-partial", "device ioctl dispatcher is wired", "request coverage is device-specific and partial"),
    "pipe": ("semantic-partial", "pipe number is covered", "pipe capacity and atomicity matrix remains"),
    "pipe2": ("probed-compatible-core", "raw pipe2(O_CLOEXEC) test passes", "O_NONBLOCK edge tests remain"),
    "dup3": ("probed-compatible-core", "raw dup3(O_CLOEXEC) test passes", "EINVAL oldfd==newfd edge remains"),
    "readv": ("semantic-partial", "native scatter/gather handler is wired", "raw Linux-number readv regression still needed"),
    "writev": ("semantic-partial", "native scatter/gather handler is wired", "raw Linux-number writev regression still needed"),
    "preadv": ("probed-compatible-core", "raw preadv regression passes", "large iovec/overflow matrix remains"),
    "pwritev": ("probed-compatible-core", "raw pwritev regression passes", "large iovec/overflow matrix remains"),
    "preadv2": ("probed-compatible-core", "raw preadv2 regression passes", "RWF_* semantics are partial"),
    "pwritev2": ("probed-compatible-core", "raw pwritev2 regression passes", "RWF_* semantics are partial"),
    "statx": ("semantic-partial", "raw bad-pointer dispatch test passes and Linux-sized struct is copied",
              "mask/sync flags and extended statx fields are partial"),
    "arch_prctl": ("semantic-partial", "ARCH_SET_FS/GET_FS path is wired for TLS",
                   "GS and full arch_prctl command surface are deliberately limited"),
    "getrlimit": ("semantic-partial", "Linux struct rlimit layout is used", "resource coverage is partial"),
    "setrlimit": ("semantic-partial", "Linux struct rlimit layout is used", "privilege/resource enforcement is partial"),
    "getrusage": ("semantic-partial", "Linux rusage layout is used", "many counters are zero/minimal"),
    "sysinfo": ("probed-compatible-core", "raw sysinfo call passes with Linux layout",
                "load/memory fields are approximate"),
    "getgroups": ("semantic-partial", "native number reaches matching handler", "supplementary groups are minimal"),
    "setgroups": ("semantic-partial", "native number reaches matching handler", "permission/group database semantics are minimal"),
    "getresuid": ("probed-compatible-core", "raw getresuid test passes", "credential permission model is minimal"),
    "getresgid": ("probed-compatible-core", "raw getresgid test passes", "credential permission model is minimal"),
    "connect": ("semantic-partial", "raw connect error path is covered", "blocking, nonblocking, and sockaddr matrix remain"),
    "accept": ("semantic-partial", "native accept number is wired", "blocking and sockaddr matrix remain"),
    "accept4": ("semantic-partial", "native accept4 number is wired", "SOCK_NONBLOCK/SOCK_CLOEXEC matrix remains"),
    "sendto": ("semantic-partial", "native sendto number is wired", "flags and sockaddr matrix remain"),
    "recvfrom": ("semantic-partial", "native recvfrom number is wired", "flags and sockaddr matrix remain"),
    "sendmsg": ("semantic-partial", "native sendmsg number is wired", "msghdr/control-message semantics are partial"),
    "recvmsg": ("semantic-partial", "native recvmsg number is wired", "msghdr/control-message semantics are partial"),
    "sendmmsg": ("semantic-partial", "native sendmmsg number is wired", "partial-send/error semantics need coverage"),
    "recvmmsg": ("semantic-partial", "native recvmmsg number is wired", "timeout and vector semantics need coverage"),
    "getsockname": ("semantic-partial", "native getsockname number is wired", "sockaddr length/value matrix remains"),
    "getpeername": ("semantic-partial", "native getpeername number is wired", "sockaddr length/value matrix remains"),
    "setsockopt": ("semantic-partial", "native setsockopt number is wired", "option coverage is partial"),
    "getsockopt": ("semantic-partial", "native getsockopt number is wired", "option coverage is partial"),
    "epoll_wait": ("semantic-partial", "epoll wait handler is wired", "full epoll readiness semantics are partial"),
    "epoll_ctl": ("semantic-partial", "epoll ctl handler is wired", "full flag/event semantics are partial"),
    "epoll_pwait": ("semantic-partial", "epoll_pwait handler is wired", "signal mask semantics are partial"),
    "epoll_pwait2": ("semantic-partial", "epoll_pwait2 handler is wired", "timespec and signal mask semantics are partial"),
    "timerfd_settime": ("semantic-partial", "timerfd settime handler is wired", "absolute/cancel-on-set semantics are partial"),
    "timerfd_gettime": ("semantic-partial", "timerfd gettime handler is wired", "timer edge semantics are partial"),
    "uname": ("semantic-partial", "uname handler is wired", "reported Linux identity is xv6-specific"),
}


def area_for(name: str) -> str:
    if name in AREA_BY_NAME:
        return AREA_BY_NAME[name]
    if name.startswith(("sched_", "clock_", "timer_", "gettimeofday", "setitimer", "getitimer", "alarm", "nanosleep")):
        return "time-scheduler"
    if name.startswith(("sem", "shm", "msg")):
        return "ipc"
    return "other"


def classify(row: dict[str, str]) -> tuple[str, str, str]:
    name = row["linux_name"]
    dispatch_status = row["status"]

    if dispatch_status == "unsupported-ok":
        if row["dispatch_handlers"] == "sys_ni_enosys":
            return ("unsupported-enosys", "explicit sys_ni_enosys dispatch",
                    "Linux programs requiring this syscall still need an implementation")
        return ("unsupported-enosys", "unmapped default dispatch returns -ENOSYS",
                "Linux programs requiring this syscall still need an implementation")

    if dispatch_status in {"wrong-dispatch", "native-missing", "unsupported-bad"}:
        return ("semantic-blocked", row["notes"],
                "fix dispatch before semantic compatibility can be assessed")

    if name in OVERRIDES:
        return OVERRIDES[name]

    if row["test_covered"] == "yes":
        return ("probed-compatible-core", "raw ABI regression covers this number",
                "full Linux conformance matrix not exhaustively tested")

    if dispatch_status == "struct-risk":
        return ("semantic-partial", "native number reaches matching handler",
                "struct, flag, errno, or edge-behavior matrix remains partial")

    return ("compatible-by-inspection", "simple native handler is wired to matching Linux number",
            "no dedicated raw semantic regression yet")


def write_outputs() -> None:
    with INPUT_CSV.open(newline="") as f:
        input_rows = list(csv.DictReader(f))

    rows: list[dict[str, str]] = []
    for row in input_rows:
        semantic_status, evidence, gap = classify(row)
        rows.append({
            "number": row["number"],
            "linux_name": row["linux_name"],
            "semantic_area": area_for(row["linux_name"]),
            "dispatch_status": row["status"],
            "semantic_status": semantic_status,
            "test_covered": row["test_covered"],
            "dispatch_handlers": row["dispatch_handlers"],
            "evidence": evidence,
            "remaining_gap": gap,
        })

    with OUT_CSV.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    status_counts = Counter(row["semantic_status"] for row in rows)
    area_counts = Counter(row["semantic_area"] for row in rows)
    partial = [row for row in rows if row["semantic_status"] == "semantic-partial"]

    lines: list[str] = []
    lines.append("# Linux x86_64 Syscall Semantic ABI Audit")
    lines.append("")
    lines.append("> Generated by `scripts/linux_abi_semantic_audit.py`. Do not edit tables by hand.")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    for key, count in sorted(status_counts.items()):
        lines.append(f"- `{key}`: {count}")
    lines.append("")
    lines.append("## Areas")
    lines.append("")
    for key, count in sorted(area_counts.items()):
        lines.append(f"- `{key}`: {count}")
    lines.append("")
    lines.append("## High-Priority Semantic Gaps")
    lines.append("")
    for row in partial:
        if row["linux_name"] in {
            "open", "openat", "clone", "clone3", "rt_sigtimedwait",
            "rt_sigqueueinfo", "sigaltstack", "fcntl", "pselect6", "ppoll",
            "statx", "sendmsg", "recvmsg", "accept4", "epoll_pwait2",
        }:
            lines.append(
                f"- `{row['linux_name']}({row['number']})`: {row['remaining_gap']}"
            )
    lines.append("")
    lines.append("## Full Semantic Table")
    lines.append("")
    lines.append("| Nr | Linux name | Area | Dispatch | Semantic | Test | Evidence | Remaining gap |")
    lines.append("|---:|---|---|---|---|---|---|---|")
    for row in rows:
        lines.append(
            f"| {row['number']} | `__NR_{row['linux_name']}` | "
            f"`{row['semantic_area']}` | `{row['dispatch_status']}` | "
            f"`{row['semantic_status']}` | {row['test_covered']} | "
            f"{row['evidence']} | {row['remaining_gap']} |"
        )
    OUT_MD.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    write_outputs()
