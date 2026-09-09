#!/usr/bin/env python3
"""Offline analysis of finalized bottleneck-controller snapshots; never reads /proc.

Supply exact guest/host before/after JSON paths and this boot's measured TSC Hz.
Exit 0 means the report was produced, not that playback or scheduling passed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat as stat_module

GUEST_METRICS = ("CpuTime", "RunWaitTicks", "RunSlices")
HOST_METRICS = ("runtime_ns", "runqueue_wait_ns", "timeslices")


def integer(value, label, minimum=0):
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise ValueError(label + ": expected integer")
    if isinstance(value, str) and not re.fullmatch(r"-?\d+", value):
        raise ValueError(label + ": malformed integer")
    result = int(value)
    if result < minimum:
        raise ValueError(label + ": below minimum")
    return result


def load_snapshot(path, cap):
    path = Path(path)
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat_module.S_ISREG(before.st_mode) or not 0 < before.st_size <= cap:
            raise ValueError(str(path) + ": not a bounded nonempty regular file")
        data = stream.read(cap + 1)
        after = os.fstat(stream.fileno())
    identity = lambda st: (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns)
    if identity(before) != identity(after) or len(data) != before.st_size:
        raise ValueError(str(path) + ": changed while reading")
    row = json.loads(data)
    if not isinstance(row, dict):
        raise ValueError(str(path) + ": expected JSON object")
    return row, {"path": str(path.resolve()), "bytes": len(data),
                 "sha256": hashlib.sha256(data).hexdigest()}


def parse_stat(raw):
    if not isinstance(raw, str):
        raise ValueError("stat unavailable: " + repr(raw))
    opening = raw.find(" (")
    closing = raw.rfind(") ")
    if opening < 1 or closing <= opening:
        raise ValueError("malformed stat command delimiters")
    pid = integer(raw[:opening], "stat pid", 1)
    fields = raw[closing + 2:].split()
    if len(fields) < 20 or len(fields[0]) != 1:
        raise ValueError("short/malformed stat")
    result = {"pid": pid, "name": raw[opening + 2:closing], "state": fields[0],
              "start_ticks": integer(fields[19], "stat starttime"),
              "priority": integer(fields[15], "stat priority", -(1 << 63)),
              "nice": integer(fields[16], "stat nice", -(1 << 63))}
    if len(fields) > 36:
        result["processor"] = integer(fields[36], "stat processor", -1)
    return result


def parse_status(raw):
    if not isinstance(raw, str):
        raise ValueError("status unavailable: " + repr(raw))
    result = {}
    for line in raw.splitlines():
        key, separator, value = line.partition(":")
        if separator:
            if key in result:
                raise ValueError("duplicate status field " + key)
            result[key] = value.strip()
    return result


def role_for(process, status):
    # Worker names do not identify their workqueue. Do not turn an empty argv
    # or a generic worker_thread name into GPU/RCU attribution.
    if status.get("Kthread") == "1":
        return "kernel:unattributed"
    argv = process.get("argv")
    if not isinstance(argv, list) or not argv or not all(isinstance(x, str) for x in argv):
        argv = []
    exe = process.get("exe")
    name = Path(exe).name if isinstance(exe, str) else Path(argv[0]).name if argv else ""
    if name == "kwin_wayland":
        return "kwin_wayland"
    if name not in ("chrome", "chromium"):
        return "unidentified"
    process_type = "browser"
    subtype = None
    for index, arg in enumerate(argv):
        if arg.startswith("--type="):
            process_type = arg.split("=", 1)[1]
        elif arg == "--type" and index + 1 < len(argv):
            process_type = argv[index + 1]
        elif arg.startswith("--utility-sub-type="):
            subtype = arg.split("=", 1)[1]
    return "chromium:" + process_type + (":" + subtype if subtype else "")


def metric_values(raw, names):
    values, errors = {}, {}
    for name in names:
        try:
            values[name] = integer(raw.get(name), name)
        except ValueError as error:
            errors[name] = str(error)
    return values, errors


def index_tasks(snapshot, host=False):
    indexed, rejected, duplicates = {}, [], []
    if host:
        processes = [{"pid": snapshot.get("qemu_pid"), "tasks": snapshot.get("tasks", [])}]
        limit = 256
    else:
        processes = snapshot.get("processes")
        limit = 2048
    if not isinstance(processes, list) or not processes or len(processes) > 4096:
        raise ValueError("missing/oversized process inventory")
    seen = 0
    for process in processes:
        tgid = integer(process.get("pid"), "process pid", 1)
        process_error = None
        process_start = None
        if not host:
            try:
                process_stat = parse_stat(process.get("stat"))
                if process_stat["pid"] != tgid:
                    raise ValueError("process stat PID mismatch")
                process_start = process_stat["start_ticks"]
                if process.get("identity_stable") is False:
                    raise ValueError("process identity changed during snapshot")
            except ValueError as error:
                process_error = str(error)
        tasks = process.get("tasks")
        if not isinstance(tasks, list):
            raise ValueError("missing task inventory")
        for task in tasks:
            seen += 1
            if seen > limit:
                raise ValueError("task inventory exceeds controller cap")
            identity = {"tgid": tgid, "tid": task.get("tid")}
            try:
                tid = integer(task.get("tid"), "task tid", 1)
                info = parse_stat(task.get("stat"))
                if info["pid"] != tid:
                    raise ValueError("task stat PID mismatch")
                if host:
                    role = ("qemu:main" if tid == tgid else
                            "qemu:vcpu" if re.fullmatch(r"CPU \d+/KVM", info["name"]) else
                            "qemu:other")
                    raw = task.get("schedstat")
                    fields = raw.split() if isinstance(raw, str) else []
                    values, errors = metric_values(
                        dict(zip(HOST_METRICS, fields)) if len(fields) == 3 else {}, HOST_METRICS)
                    status = {}
                else:
                    status = parse_status(task.get("status"))
                    if integer(status.get("Tgid"), "status Tgid", 1) != tgid:
                        raise ValueError("status Tgid differs from process inventory PID")
                    if integer(status.get("Pid"), "status Pid", 1) != tid:
                        raise ValueError("status Pid differs from task inventory TID")
                    if task.get("identity_stable") is False:
                        raise ValueError("task identity changed during snapshot")
                    role = role_for(process, status)
                    values, errors = metric_values(status, GUEST_METRICS)
                key = (tgid, tid, info["start_ticks"])
                record = {**identity, "start_ticks": info["start_ticks"], "role": role,
                          "stat": info, "metrics": values, "metric_errors": errors}
                if not host:
                    record.update(process_start_ticks=process_start,
                                  process_identity_error=process_error,
                                  status_name=status.get("Name"),
                                  executable=process.get("exe"),
                                  collector_role=process.get("role"),
                                  kernel_thread=status.get("Kthread"),
                                  affinity=status.get("Cpus_allowed_list"),
                                  util_avg=status.get("UtilAvg"),
                                  load_contrib=status.get("LoadContrib"))
                if key in indexed:
                    duplicates.append({**identity, "start_ticks": key[2],
                                       "reason": "duplicate identity; excluded from matching"})
                    indexed[key] = None
                else:
                    indexed[key] = record
            except ValueError as error:
                rejected.append({**identity, "reason": str(error)})
    return {key: value for key, value in indexed.items() if value is not None}, rejected, duplicates


def windows(before, after, host=False):
    domain = "host_monotonic" if host else "guest_monotonic"
    bounds = [integer(row.get(domain + suffix), domain + suffix)
              for row in (before, after) for suffix in ("_start_ns", "_end_ns")]
    bs, be, ats, ae = bounds
    if not bs <= be <= ats <= ae:
        raise ValueError("snapshot windows overlap or regress in " + domain)
    return {"clock_domain": domain, "before_start_ns": bs, "before_end_ns": be,
            "after_start_ns": ats, "after_end_ns": ae,
            "before_acquisition_ms": (be - bs) / 1e6,
            "after_acquisition_ms": (ae - ats) / 1e6,
            "per_counter_elapsed_min_ms": (ats - be) / 1e6,
            "per_counter_elapsed_max_ms": (ae - bs) / 1e6,
            "limitation": "Each field was read somewhere inside its snapshot window; no per-field timestamps or atomic multi-field snapshot. Bracket is not the exact 15s playback window."}


def compare(before, after, hz, host=False):
    if before.get("trial") != after.get("trial") or not isinstance(before.get("trial"), str):
        raise ValueError("trial identity mismatch")
    if before.get("phase") != "before" or after.get("phase") != "after":
        raise ValueError("expected before/after phases")
    if host:
        for field in ("qemu_pid", "qemu_start_ticks"):
            if integer(before.get(field), field) != integer(after.get(field), field):
                raise ValueError("QEMU process identity changed: " + field)
    timing = windows(before, after, host)
    left, left_bad, left_dup = index_tasks(before, host)
    right, right_bad, right_dup = index_tasks(after, host)
    matched, role_sums = [], {}
    names = HOST_METRICS if host else GUEST_METRICS
    for key in sorted(left.keys() & right.keys()):
        b, a = left[key], right[key]
        flags = []
        if b["role"] != a["role"]:
            flags.append("process role changed; excluded from role totals")
        if not host and b["executable"] != a["executable"]:
            flags.append("executable identity changed; excluded from role totals")
        if not host and (b["process_identity_error"] or a["process_identity_error"] or
                         b["process_start_ticks"] != a["process_start_ticks"]):
            flags.append("process identity unverified/changed; excluded from role totals")
        deltas = {}
        for name in names:
            if name not in b["metrics"] or name not in a["metrics"]:
                deltas[name] = {"valid": False, "reason": "missing/malformed before or after metric"}
                continue
            delta = a["metrics"][name] - b["metrics"][name]
            if delta < 0:
                deltas[name] = {"valid": False, "reason": "counter regressed", "raw_delta": delta}
                continue
            metric = {"valid": True, "raw_delta": delta}
            if name not in ("RunSlices", "timeslices"):
                metric["ms"] = delta / 1e6 if host else delta * 1000 / hz
            deltas[name] = metric
        record = {"identity": {"tgid": key[0], "tid": key[1], "start_ticks": key[2]},
                  "role": a["role"], "before": b, "after": a,
                  "deltas": deltas, "flags": flags}
        matched.append(record)
        if flags:
            continue
        group = role_sums.setdefault(a["role"], {"matched_threads": 0, "metrics": {}})
        group["matched_threads"] += 1
        for name, delta in deltas.items():
            total = group["metrics"].setdefault(name, {"valid_threads": 0, "invalid_threads": 0,
                                                      "observed_raw_sum": 0})
            if not delta["valid"]:
                total["invalid_threads"] += 1
                continue
            total["valid_threads"] += 1
            total["observed_raw_sum"] += delta["raw_delta"]
    for group in role_sums.values():
        for name, metric in group["metrics"].items():
            metric["complete_for_matched_role"] = metric["invalid_threads"] == 0
            if metric["valid_threads"] == 0:
                metric["observed_raw_sum"] = None
            elif name not in ("RunSlices", "timeslices"):
                metric["observed_sum_ms"] = (metric["observed_raw_sum"] / 1e6 if host else
                                              metric["observed_raw_sum"] * 1000 / hz)
    return {"trial": before["trial"], "domain": "host_QEMU" if host else "guest",
            "counter_units": "host schedstat nanoseconds" if host else "guest raw r_time ticks",
            "timebase_hz": None if host else hz, "snapshot_windows": timing,
            "matched_task_count": len(matched), "matched_tasks": matched,
            "missing_after": [left[k] for k in sorted(left.keys() - right.keys())],
            "new_after": [right[k] for k in sorted(right.keys() - left.keys())],
            "rejected_before": left_bad, "rejected_after": right_bad,
            "duplicate_before": left_dup, "duplicate_after": right_dup,
            "collector_selection": None if host else {
                "before": before.get("selection"), "after": after.get("selection")},
            "role_totals_matched_tasks_only": role_sums,
            "role_total_limit": "Observed sums over matched tasks only; simultaneous waits overlap. Never interpret sums as per-frame stall time or sum guest and host counters. New, missing and invalid tasks are not assigned zero activity."}


def self_test():
    def stat_line(pid, name, start):
        fields = ["S"] + ["0"] * 49
        fields[15], fields[16], fields[19] = "20", "0", str(start)
        return str(pid) + " (" + name + ") " + " ".join(fields)
    assert parse_stat(stat_line(7, "odd ) command (x)", 99))["start_ticks"] == 99
    def guest(phase, start, counters, task_start=99):
        status = "Tgid:\t7\nPid:\t7\n" + "".join(k + ":\t" + str(v) + "\n" for k, v in counters.items())
        return {"trial": "test", "phase": phase, "guest_monotonic_start_ns": start,
                "guest_monotonic_end_ns": start + 1_000_000,
                "processes": [{"pid": 7, "argv": ["chrome", "--type=renderer"],
                               "stat": stat_line(7, "chrome", task_start),
                               "tasks": [{"tid": 7, "stat": stat_line(7, "thread", task_start),
                                          "status": status}]}]}
    b = guest("before", 10_000_000, dict(CpuTime=100, RunWaitTicks=50, RunSlices=1))
    a = guest("after", 30_000_000, dict(CpuTime=120, RunWaitTicks=60, RunSlices=2))
    report = compare(b, a, 1000)
    assert report["matched_tasks"][0]["deltas"]["CpuTime"]["ms"] == 20
    assert report["snapshot_windows"]["per_counter_elapsed_min_ms"] == 19
    reused = guest("after", 30_000_000, dict(CpuTime=120), task_start=100)
    assert compare(b, reused, 1000)["matched_task_count"] == 0
    malformed = guest("after", 30_000_000, dict(CpuTime=1, RunSlices=2))
    deltas = compare(b, malformed, 1000)["matched_tasks"][0]["deltas"]
    assert not deltas["CpuTime"]["valid"] and not deltas["RunWaitTicks"]["valid"]
    def host(phase, start, runtime):
        return {"trial": "test", "phase": phase, "qemu_pid": 7, "qemu_start_ticks": "99",
                "host_monotonic_start_ns": start, "host_monotonic_end_ns": start + 1,
                "tasks": [{"tid": 7, "stat": stat_line(7, "qemu", 99),
                           "schedstat": str(runtime) + " 50 1"}]}
    h = compare(host("before", 10, 100), host("after", 20, 1_000_100), None, True)
    assert h["matched_tasks"][0]["deltas"]["runtime_ns"]["ms"] == 1
    for snap in (b, a):
        proc = snap["processes"][0]
        proc["argv"] = []
        proc["tasks"][0]["status"] += "Kthread:\t1\n"
    assert compare(b, a, 1000)["matched_tasks"][0]["role"] == "kernel:unattributed"
    a["processes"][0]["tasks"][0]["identity_stable"] = False
    rejected = compare(b, a, 1000)
    assert rejected["matched_task_count"] == 0 and rejected["rejected_after"]
    assert role_for({"exe": "/path/chrome", "argv": ["rewritten", "--type=gpu-process"]}, {}) == "chromium:gpu-process"
    assert role_for({"argv": []}, {}) == "unidentified"
    print("PASS: stat parsing, identity reuse and stability, counter regression/missing fields, windows, guest/host units, kernel and GPU roles")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--guest-before")
    parser.add_argument("--guest-after")
    parser.add_argument("--host-before")
    parser.add_argument("--host-after")
    parser.add_argument("--timebase-hz", type=int, help="Exact measured r_time/TSC frequency from this boot")
    parser.add_argument("--output")
    parser.add_argument("--confirmed-final", action="store_true", help="Root confirmed these receipts are finalized")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.confirmed_final:
        parser.error("wait for root completion/receipt confirmation, then supply --confirmed-final")
    if not all((args.guest_before, args.guest_after, args.output, args.timebase_hz)) or args.timebase_hz <= 0:
        parser.error("guest before/after, positive exact timebase Hz and output are required")
    if bool(args.host_before) != bool(args.host_after):
        parser.error("host before and after must be supplied together")
    sources = {}
    before, sources["guest_before"] = load_snapshot(args.guest_before, 4 * 1024 * 1024)
    after, sources["guest_after"] = load_snapshot(args.guest_after, 4 * 1024 * 1024)
    report = {"schema_version": 1, "sources": sources,
              "guest": compare(before, after, args.timebase_hz),
              "limitations": [
                  "Source snapshots are not atomic; acquisition windows bracket every field read.",
                  "Guest runnable wait starts after enqueue and excludes prior wake/notification/lock delay.",
                  "Guest CpuTime is elapsed r_time while selected; host vCPU descheduling can inflate it.",
                  "RunSlices counts accounted queue intervals, not a latency histogram or video frames.",
                  "Kernel workers share generic names; their queue/subsystem identity is unknown.",
                  "Chromium argv can retain zygote roles; same-run trace metadata is needed for runtime roles.",
                  "Host schedstat runtime/wait are nanoseconds and remain separate from guest counters.",
                  "Zero host schedstat wait does not establish that host schedstats collection was enabled.",
                  "Guest/context-switch placeholders and /proc/stat utilization are intentionally unused.",
                  "A produced report is not a scheduler verdict, causal attribution, or media PASS."
              ]}
    if args.host_before:
        hb, sources["host_before"] = load_snapshot(args.host_before, 1024 * 1024)
        ha, sources["host_after"] = load_snapshot(args.host_after, 1024 * 1024)
        if hb.get("trial") != before.get("trial"):
            raise ValueError("guest/host trial mismatch")
        report["host"] = compare(hb, ha, None, True)
    output = Path(args.output)
    inputs = {item["path"] for item in sources.values()}
    if str(output.resolve()) in inputs:
        raise ValueError("output would overwrite an input")
    with output.open("x") as stream:
        json.dump(report, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"report": str(output.resolve()), "trial": before["trial"],
                      "guest_matched": report["guest"]["matched_task_count"],
                      "host_matched": report.get("host", {}).get("matched_task_count"),
                      "runtime_verdict": "not inferred"}))


if __name__ == "__main__":
    main()
